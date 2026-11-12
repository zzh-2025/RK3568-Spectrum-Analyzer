#include "model_selection.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::string validate_peaks(
    const MultiLorentzFitResult& fit,
    const ModelSelectionOptions& opts,
    double xmin,
    double xmax,
    double mean_step,
    double max_abs_intensity)
{
    if (!fit.converged)
        return "fit did not converge";
    if (!std::isfinite(fit.sse) || fit.sse < 0.0 ||
        !std::isfinite(fit.rmse) || !std::isfinite(fit.r_squared))
        return "non-finite fit metrics";
    if (fit.peaks.empty())
        return "no fitted peaks";

    const double span = xmax - xmin;
    std::vector<double> centers;
    for (std::size_t i = 0; i < fit.peaks.size(); ++i) {
        const auto& p = fit.peaks[i];
        const std::string prefix = "peak " + std::to_string(i + 1) + ": ";
        if (!std::isfinite(p.peak_position_nm) ||
            !std::isfinite(p.fwhm_nm) ||
            !std::isfinite(p.peak_height) ||
            !std::isfinite(p.peak_area))
            return prefix + "non-finite parameters";
        if (p.fwhm_nm < opts.min_width_steps * mean_step)
            return prefix + "too narrow to resolve";
        if (p.fwhm_nm > opts.max_width_span_fraction * span)
            return prefix + "width too large";
        if (p.peak_height <= opts.min_height_fraction * max_abs_intensity ||
            p.peak_area <= 0.0)
            return prefix + "non-positive or negligible intensity";
        if (p.peak_position_nm <= xmin + opts.edge_margin_steps * mean_step ||
            p.peak_position_nm >= xmax - opts.edge_margin_steps * mean_step)
            return prefix + "center near wavelength boundary";
        centers.push_back(p.peak_position_nm);
    }
    std::sort(centers.begin(), centers.end());
    for (std::size_t i = 1; i < centers.size(); ++i) {
        if (centers[i] - centers[i - 1] < opts.min_separation_steps * mean_step)
            return "two centers cannot be resolved at this sampling interval";
    }
    return {};
}

PeakModelDiagnostics diagnose_candidate(
    const MultiLorentzFitResult& fit,
    const std::vector<SpectrumPoint>& sorted,
    const ModelSelectionOptions& options)
{
    PeakModelDiagnostics d;
    if (fit.peaks.empty() || sorted.size() < 3 || !(fit.rmse > 0.0))
        return d;

    for (std::size_t i = 1; i < fit.peaks.size(); ++i) {
        const auto& a = fit.peaks[i - 1];
        const auto& b = fit.peaks[i];
        const double avg_width = (a.fwhm_nm + b.fwhm_nm) * 0.5;
        if (!(avg_width > 0.0)) continue;
        const double ratio = (b.peak_position_nm - a.peak_position_nm) / avg_width;
        if (!std::isfinite(d.min_spacing_over_mean_fwhm) ||
            ratio < d.min_spacing_over_mean_fwhm) {
            d.min_spacing_over_mean_fwhm = ratio;
            d.closest_peak_left = static_cast<int>(i);
            d.closest_peak_right = static_cast<int>(i + 1);
        }
    }
    d.overlapping_peaks = std::isfinite(d.min_spacing_over_mean_fwhm) &&
        d.min_spacing_over_mean_fwhm < options.overlap_review_ratio;

    const std::size_t n = sorted.size();
    std::vector<double> residuals;
    residuals.reserve(n);
    double residual_sum = 0.0;
    for (const auto& p : sorted) {
        const double residual = p.intensity - multi_lorentz(p.wavelength, fit.peaks);
        residuals.push_back(residual);
        residual_sum += residual;
    }
    const double mean = residual_sum / static_cast<double>(n);
    double variance = 0.0, lag_product = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double centered = residuals[i] - mean;
        variance += centered * centered;
        if (i > 0) lag_product += centered * (residuals[i - 1] - mean);
    }
    if (variance > 1e-20) {
        d.residual_lag1_correlation = lag_product / variance;
        d.correlated_residuals =
            std::abs(d.residual_lag1_correlation) >= options.residual_correlation_review;
    }

    const std::size_t edge_size = std::max<std::size_t>(1, n / 20);
    double left_sum = 0.0, right_sum = 0.0;
    for (std::size_t i = 0; i < edge_size; ++i) {
        left_sum += residuals[i];
        right_sum += residuals[n - 1 - i];
    }
    d.left_edge_bias_rmse = left_sum / (static_cast<double>(edge_size) * fit.rmse);
    d.right_edge_bias_rmse = right_sum / (static_cast<double>(edge_size) * fit.rmse);
    d.edge_bias = std::abs(d.left_edge_bias_rmse) >= options.edge_bias_review_rmse ||
                  std::abs(d.right_edge_bias_rmse) >= options.edge_bias_review_rmse;
    return d;
}

} // namespace

ModelSelectionResult select_peak_count(
    const std::vector<SpectrumPoint>& spectrum,
    const ModelSelectionOptions& options)
{
    if (options.min_peaks < 1 || options.max_peaks > 12 ||
        options.max_peaks < options.min_peaks)
        throw std::invalid_argument("Peak range must satisfy 1 <= min <= max <= 12.");
    if (options.max_iterations < 1)
        throw std::invalid_argument("max_iterations must be positive.");
    if (options.min_width_steps <= 0.0 || options.max_width_span_fraction <= 0.0 ||
        options.min_height_fraction < 0.0 || options.edge_margin_steps < 0.0 ||
        options.min_separation_steps < 0.0 ||
        !std::isfinite(options.overlap_review_ratio) || options.overlap_review_ratio <= 0.0 ||
        !std::isfinite(options.residual_correlation_review) ||
        options.residual_correlation_review <= 0.0 ||
        options.residual_correlation_review > 1.0 ||
        !std::isfinite(options.edge_bias_review_rmse) ||
        options.edge_bias_review_rmse <= 0.0)
        throw std::invalid_argument("Invalid model selection constraints.");
    if (spectrum.size() < static_cast<std::size_t>(3 * options.max_peaks + 10))
        throw std::invalid_argument("Insufficient spectrum points.");

    std::vector<SpectrumPoint> sorted = spectrum;
    std::sort(sorted.begin(), sorted.end(),
              [](const SpectrumPoint& a, const SpectrumPoint& b) {
                  return a.wavelength < b.wavelength;
              });
    double max_abs_intensity = 0.0;
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        if (!std::isfinite(sorted[i].wavelength) ||
            !std::isfinite(sorted[i].intensity))
            throw std::invalid_argument("Non-finite spectrum data.");
        if (i > 0 && sorted[i].wavelength <= sorted[i - 1].wavelength)
            throw std::invalid_argument("Duplicate wavelengths.");
        max_abs_intensity = std::max(max_abs_intensity, std::abs(sorted[i].intensity));
    }
    const double xmin = sorted.front().wavelength;
    const double xmax = sorted.back().wavelength;
    const double span = xmax - xmin;
    if (!(span > 0.0) || !(max_abs_intensity > 0.0))
        throw std::invalid_argument("Invalid or zero-valued spectrum.");
    const double mean_step = span / static_cast<double>(sorted.size() - 1);

    ModelSelectionResult result;
    const auto start = std::chrono::steady_clock::now();
    result.candidates.reserve(static_cast<std::size_t>(options.max_peaks - options.min_peaks + 1));

    for (int n = options.min_peaks; n <= options.max_peaks; ++n) {
        PeakModelCandidate candidate;
        candidate.peak_count = n;
        candidate.parameter_count = 3 * n;
        const auto candidate_start = std::chrono::steady_clock::now();
        try {
            candidate.run = fit_auto_centers(sorted, n, options.max_iterations,
                                             options.search_mode);
            const auto& fit = candidate.run.fit;
            candidate.converged = fit.converged;
            candidate.note = validate_peaks(fit, options, xmin, xmax,
                                            mean_step, max_abs_intensity);
            candidate.acceptable = candidate.note.empty();
            if (candidate.acceptable)
                candidate.diagnostics = diagnose_candidate(fit, sorted, options);
            if (std::isfinite(fit.sse) && fit.sse >= 0.0) {
                const double num_points = static_cast<double>(sorted.size());
                const double k = static_cast<double>(candidate.parameter_count);
                // 高斯残差、未知共同噪声方差，忽略各模型相同的常数项。
                // 将 SSE==0 保护到最小正浮点值，避免 log(0)。
                const double effective_sse = std::max(fit.sse,
                    std::numeric_limits<double>::min());
                const double base = num_points * std::log(effective_sse / num_points);
                candidate.aic = base + 2.0 * k;
                candidate.bic = base + k * std::log(num_points);
            }
        } catch (const std::exception& e) {
            candidate.note = e.what();
        }
        candidate.elapsed_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - candidate_start).count();
        result.candidates.push_back(std::move(candidate));
    }

    // 峰数逐一增加时，输出新增峰带来的相对SSE改善；只作诊断，不是选择阈值。
    for (std::size_t i = 1; i < result.candidates.size(); ++i) {
        const auto& previous = result.candidates[i - 1];
        auto& current = result.candidates[i];
        if (previous.acceptable && current.acceptable &&
            previous.run.fit.sse > 0.0 && std::isfinite(current.run.fit.sse)) {
            current.diagnostics.sse_gain_from_previous_percent =
                100.0 * (previous.run.fit.sse - current.run.fit.sse) /
                previous.run.fit.sse;
        }
    }

    std::vector<std::size_t> valid_indices;
    for (std::size_t i = 0; i < result.candidates.size(); ++i) {
        const auto& c = result.candidates[i];
        if (c.acceptable && std::isfinite(c.bic))
            valid_indices.push_back(i);
    }
    std::sort(valid_indices.begin(), valid_indices.end(),
              [&](std::size_t a, std::size_t b) {
                  const auto& x = result.candidates[a];
                  const auto& y = result.candidates[b];
                  if (x.bic != y.bic) return x.bic < y.bic;
                  return x.peak_count < y.peak_count;
              });

    if (!valid_indices.empty()) {
        const auto& best = result.candidates[valid_indices[0]];
        result.recommended_peak_count = best.peak_count;
        result.at_max_peak_limit = best.peak_count == options.max_peaks;
        result.needs_manual_review = result.at_max_peak_limit ||
            best.diagnostics.overlapping_peaks ||
            best.diagnostics.correlated_residuals ||
            best.diagnostics.edge_bias;
        for (auto& c : result.candidates) {
            if (std::isfinite(c.bic)) c.delta_bic = c.bic - best.bic;
        }
        result.explanation = "Lowest BIC among converged, numerically acceptable models.";
        if (result.at_max_peak_limit)
            result.explanation += " WARNING: recommendation reached Nmax; larger models were not tested.";
        if (best.diagnostics.overlapping_peaks)
            result.explanation += " REVIEW: strongly overlapping component centers.";
        if (best.diagnostics.correlated_residuals)
            result.explanation += " REVIEW: residual serial correlation weakens independent-noise BIC assumptions.";
        if (best.diagnostics.edge_bias)
            result.explanation += " REVIEW: systematic error near wavelength edge; consider baseline/window settings.";
        if (valid_indices.size() > 1) {
            const auto& second = result.candidates[valid_indices[1]];
            result.alternative_peak_count = second.peak_count;
            const double gap = second.bic - best.bic;
            if (gap < 2.0)
                result.explanation += " Competing models have close BIC (<2): ambiguous choice.";
            else if (gap < 6.0)
                result.explanation += " Second-best model is still worth reviewing (delta BIC <6).";
        }
    } else {
        result.explanation = "No model converged with acceptable parameters; manual review required.";
    }
    result.total_elapsed_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    return result;
}
