#include "autopeak.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

struct SeedSpec {
    double threshold_fraction;
    double spread;
    double shift;
};

std::vector<double> smooth_moving_average(
    const std::vector<SpectrumPoint>& spectrum, int half_window)
{
    const int n = static_cast<int>(spectrum.size());
    std::vector<double> prefix(n + 1, 0.0);
    for (int i = 0; i < n; ++i)
        prefix[i + 1] = prefix[i] + spectrum[i].intensity;

    std::vector<double> smooth(n);
    for (int i = 0; i < n; ++i) {
        const int left = std::max(0, i - half_window);
        const int right = std::min(n - 1, i + half_window);
        smooth[i] = (prefix[right + 1] - prefix[left]) /
                    static_cast<double>(right - left + 1);
    }
    return smooth;
}

std::vector<double> make_even_seeds(
    double left, double right, int count, double shift, double spread)
{
    const double mid = (left + right) * 0.5;
    const double half_span = (right - left) * 0.5;
    std::vector<double> centers;
    centers.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const double fraction = (static_cast<double>(i) + 1.0) /
                                (static_cast<double>(count) + 1.0);
        const double relative = (fraction - 0.5) * 2.0;
        centers.push_back(mid + (relative * spread + shift) * half_span);
    }
    return centers;
}

// 仅检验数值拟合是否出现明显退化；不等同于物理模式有效性判定。
bool plausible_peaks(const MultiLorentzFitResult& fit,
                     double span,
                     double sample_step,
                     double max_abs_intensity)
{
    if (!fit.converged || !std::isfinite(fit.sse) ||
        !std::isfinite(fit.r_squared) || fit.peaks.empty())
        return false;

    for (const auto& peak : fit.peaks) {
        if (!std::isfinite(peak.peak_position_nm) ||
            !std::isfinite(peak.fwhm_nm) ||
            !std::isfinite(peak.peak_height) ||
            !std::isfinite(peak.peak_area))
            return false;
        if (peak.fwhm_nm < 2.0 * sample_step ||
            peak.fwhm_nm > 1.5 * span ||
            peak.peak_height <= 0.005 * max_abs_intensity ||
            peak.peak_area <= 0.0)
            return false;
    }
    return true;
}

// 多组独立初值拟合到近似相同的参数组，才认为第一轮结果足够稳定。
bool similar_fit(const MultiLorentzFitResult& a,
                 const MultiLorentzFitResult& b,
                 double span)
{
    if (a.peaks.size() != b.peaks.size()) return false;
    const double relative_sse = std::abs(a.sse - b.sse) /
                                std::max(1.0, std::abs(b.sse));
    if (relative_sse > 0.002) return false;
    for (std::size_t i = 0; i < a.peaks.size(); ++i) {
        const auto& p = a.peaks[i];
        const auto& q = b.peaks[i];
        if (std::abs(p.peak_position_nm - q.peak_position_nm) > 0.01 * span)
            return false;
        if (std::abs(p.fwhm_nm - q.fwhm_nm) >
            0.08 * std::max(p.fwhm_nm, q.fwhm_nm))
            return false;
    }
    return true;
}

} // namespace

AutoPeakFitResult fit_auto_centers(
    const std::vector<SpectrumPoint>& spectrum,
    int peak_count,
    int max_iterations,
    AutoPeakSearchMode mode)
{
    const auto t0 = std::chrono::steady_clock::now();
    if (peak_count < 1 || peak_count > 12)
        throw std::invalid_argument("Peak count must be within 1..12.");
    if (max_iterations < 1)
        throw std::invalid_argument("max_iterations must be positive.");
    if (spectrum.size() < static_cast<std::size_t>(3 * peak_count + 10))
        throw std::invalid_argument("Not enough spectrum points.");

    std::vector<SpectrumPoint> sorted = spectrum;
    std::sort(sorted.begin(), sorted.end(),
              [](const SpectrumPoint& a, const SpectrumPoint& b) {
                  return a.wavelength < b.wavelength;
              });
    double max_abs_intensity = 0.0;
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        if (!std::isfinite(sorted[i].wavelength) ||
            !std::isfinite(sorted[i].intensity))
            throw std::invalid_argument("Spectrum contains invalid values.");
        if (i > 0 && sorted[i].wavelength <= sorted[i - 1].wavelength)
            throw std::invalid_argument("Wavelengths must be distinct.");
        max_abs_intensity = std::max(max_abs_intensity,
                                     std::abs(sorted[i].intensity));
    }

    const double xmin = sorted.front().wavelength;
    const double xmax = sorted.back().wavelength;
    const double span = xmax - xmin;
    if (!(span > 0.0))
        throw std::invalid_argument("Invalid wavelength span.");

    const int half_window = std::max(2, static_cast<int>(sorted.size() / 100));
    const auto smooth = smooth_moving_average(sorted, half_window);
    const auto mm = std::minmax_element(smooth.begin(), smooth.end());
    const double ymin = *mm.first;
    const double ymax = *mm.second;
    if (!(ymax > ymin))
        throw std::invalid_argument("Spectrum has no usable intensity variation.");

    // 优先使用处于中心宽带、不同展宽/偏移的7组初值。
    const SeedSpec priority[] = {
        {0.40, 1.00,  0.00},
        {0.40, 1.00, -0.10},
        {0.40, 1.00,  0.10},
        {0.40, 0.88,  0.00},
        {0.40, 1.12,  0.00},
        {0.30, 1.00,  0.00},
        {0.50, 1.00,  0.00}
    };

    // 原始27组用于回退或完整模式，顺序与旧实现一致。
    std::vector<SeedSpec> full;
    for (double threshold : {0.30, 0.40, 0.50})
        for (double spread : {0.88, 1.00, 1.12})
            for (double shift : {-0.10, 0.0, 0.10})
                full.push_back({threshold, spread, shift});

    AutoPeakFitResult answer;
    std::vector<MultiLorentzFitResult> valid_candidates;
    bool have_fit = false;

    auto run_one = [&](const SeedSpec& spec) {
        const double threshold = ymin + spec.threshold_fraction * (ymax - ymin);
        int first = -1, last = -1;
        for (int i = 0; i < static_cast<int>(smooth.size()); ++i) {
            if (smooth[i] >= threshold) {
                if (first < 0) first = i;
                last = i;
            }
        }
        if (first < 0 || last <= first) return;

        const double left = sorted[first].wavelength;
        const double right = sorted[last].wavelength;
        auto centers = make_even_seeds(left, right, peak_count,
                                       spec.shift, spec.spread);
        for (double& c : centers)
            c = std::max(xmin + 1e-6 * span,
                         std::min(c, xmax - 1e-6 * span));

        ++answer.trials_attempted;
        try {
            auto fit = fit_multi_lorentz(sorted, centers, max_iterations);
            if (fit.converged) {
                ++answer.converged_trials;
                valid_candidates.push_back(fit);
            }
            if (!std::isfinite(fit.sse)) return;
            const bool candidate_better =
                !have_fit ||
                (fit.converged && !answer.fit.converged) ||
                (fit.converged == answer.fit.converged &&
                 fit.sse < answer.fit.sse);
            if (candidate_better) {
                have_fit = true;
                answer.fit = std::move(fit);
                answer.initial_centers_nm = std::move(centers);
            }
        } catch (const std::exception&) {
            // 失败的起点跳过；有必要时回退至完整搜索。
        }
    };

    if (mode == AutoPeakSearchMode::Full) {
        for (const auto& spec : full) run_one(spec);
    } else {
        for (const auto& spec : priority) run_one(spec);

        const double mean_step = span / (sorted.size() - 1);
        bool stable = have_fit &&
                      answer.fit.r_squared >= 0.95 &&
                      plausible_peaks(answer.fit, span, mean_step,
                                      max_abs_intensity);
        if (stable) {
            int matching = 0;
            for (const auto& candidate : valid_candidates) {
                if (similar_fit(candidate, answer.fit, span))
                    ++matching;
            }
            stable = matching >= 5; // 七次快速尝试中至少五次参数结果接近
        }
        if (!stable) {
            answer.used_fallback = true;
            // 避免重复执行优先的7个参数组合。
            for (const auto& spec : full) {
                bool already_run = false;
                for (const auto& first : priority) {
                    if (spec.threshold_fraction == first.threshold_fraction &&
                        spec.spread == first.spread &&
                        spec.shift == first.shift) {
                        already_run = true;
                        break;
                    }
                }
                if (!already_run) run_one(spec);
            }
        }
    }

    if (!have_fit)
        throw std::runtime_error("All automatic initialization trials failed.");

    const auto t1 = std::chrono::steady_clock::now();
    answer.elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return answer;
}
