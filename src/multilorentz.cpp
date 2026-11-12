#include "multilorentz.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace {

constexpr double kPi = 3.14159265358979323846;

double clamp_value(double value, double low, double high)
{
    return std::max(low, std::min(value, high));
}

// 解一般规模的线性方程组 Ax=b。采用高斯消元和部分主元选取。
bool solve_linear_system(
    std::vector<std::vector<double>> matrix,
    std::vector<double> rhs,
    std::vector<double>& solution)
{
    const std::size_t n = rhs.size();
    if (matrix.size() != n)
        return false;

    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot = col;

        for (std::size_t row = col + 1; row < n; ++row) {
            if (std::abs(matrix[row][col]) >
                std::abs(matrix[pivot][col])) {
                pivot = row;
            }
        }

        if (!std::isfinite(matrix[pivot][col]) ||
            std::abs(matrix[pivot][col]) < 1e-20) {
            return false;
        }

        std::swap(matrix[col], matrix[pivot]);
        std::swap(rhs[col], rhs[pivot]);

        const double divisor = matrix[col][col];
        for (std::size_t j = col; j < n; ++j)
            matrix[col][j] /= divisor;
        rhs[col] /= divisor;

        for (std::size_t row = 0; row < n; ++row) {
            if (row == col)
                continue;

            const double factor = matrix[row][col];
            if (factor == 0.0)
                continue;

            for (std::size_t j = col; j < n; ++j)
                matrix[row][j] -= factor * matrix[col][j];
            rhs[row] -= factor * rhs[col];
        }
    }

    solution = std::move(rhs);
    return true;
}

// 参数按 [xc1,w1,A1, xc2,w2,A2, ...] 排列。
// 优化器内部使用无量纲 q 参数，physical p = q * scale。
double evaluate_model(
    double x,
    const std::vector<double>& q,
    const std::vector<double>& scale)
{
    double y = 0.0;

    for (std::size_t i = 0; i + 2 < q.size(); i += 3) {
        const double xc = q[i] * scale[i];
        const double w  = q[i + 1] * scale[i + 1];
        const double area = q[i + 2] * scale[i + 2];

        const double d = x - xc;
        const double denom = 4.0 * d * d + w * w;

        if (denom > 0.0 && std::isfinite(denom))
            y += (2.0 * area / kPi) * w / denom;
    }

    return y;
}

double calculate_sse(
    const std::vector<SpectrumPoint>& spectrum,
    const std::vector<double>& q,
    const std::vector<double>& scale)
{
    double sse = 0.0;
    for (const auto& point : spectrum) {
        const double residual =
            point.intensity - evaluate_model(point.wavelength, q, scale);
        sse += residual * residual;
    }
    return sse;
}

} // namespace

double multi_lorentz(
    double wavelength_nm,
    const std::vector<MultiLorentzPeak>& peaks)
{
    double y = 0.0;

    for (const auto& peak : peaks) {
        const double d = wavelength_nm - peak.peak_position_nm;
        const double denom =
            4.0 * d * d + peak.fwhm_nm * peak.fwhm_nm;

        if (peak.fwhm_nm > 0.0 && denom > 0.0) {
            y += (2.0 * peak.peak_area / kPi)
                 * peak.fwhm_nm / denom;
        }
    }

    return y;
}

MultiLorentzFitResult fit_multi_lorentz(
    const std::vector<SpectrumPoint>& spectrum,
    const std::vector<double>& initial_centers_nm,
    int max_iterations)
{
    if (spectrum.size() < 3)
        throw std::invalid_argument("At least 3 spectrum points are required.");

    if (initial_centers_nm.empty())
        throw std::invalid_argument("At least one initial peak center is required.");

    if (max_iterations <= 0)
        throw std::invalid_argument("max_iterations must be positive.");

    double xmin = spectrum.front().wavelength;
    double xmax = spectrum.front().wavelength;
    double max_intensity = -std::numeric_limits<double>::infinity();

    for (const auto& point : spectrum) {
        if (!std::isfinite(point.wavelength) ||
            !std::isfinite(point.intensity)) {
            throw std::invalid_argument("Spectrum contains invalid values.");
        }

        xmin = std::min(xmin, point.wavelength);
        xmax = std::max(xmax, point.wavelength);
        max_intensity = std::max(max_intensity, point.intensity);
    }

    const double span = xmax - xmin;
    if (!(span > 0.0))
        throw std::invalid_argument("Invalid wavelength range.");

    const std::size_t peak_count = initial_centers_nm.size();
    const std::size_t parameter_count = peak_count * 3;

    if (spectrum.size() <= parameter_count)
        throw std::invalid_argument(
            "Not enough data points for the requested number of peaks.");

    std::vector<double> centers = initial_centers_nm;
    std::sort(centers.begin(), centers.end());

    for (double center : centers) {
        if (!std::isfinite(center) || center < xmin || center > xmax)
            throw std::invalid_argument(
                "An initial peak center is outside the spectrum wavelength range.");
    }

    // 参数缩放：中心位置用波段宽度，峰宽用典型峰宽，面积用强度×峰宽。
    const double initial_width =
        clamp_value(span / (1.25 * static_cast<double>(peak_count)),
                    span / 1000.0, span * 1.5);

    const double intensity_scale = std::max(1.0, std::abs(max_intensity));
    const double area_scale = intensity_scale * initial_width;

    std::vector<double> scale(parameter_count);
    std::vector<double> q(parameter_count);

    for (std::size_t i = 0; i < peak_count; ++i) {
        const std::size_t k = i * 3;
        scale[k] = span;
        scale[k + 1] = initial_width;
        scale[k + 2] = area_scale;

        // 以最近采样点的强度估算初始峰高；对重叠峰作温和折减。
        auto nearest = std::min_element(
            spectrum.begin(), spectrum.end(),
            [center = centers[i]](const SpectrumPoint& a, const SpectrumPoint& b) {
                return std::abs(a.wavelength - center) <
                       std::abs(b.wavelength - center);
            });

        const double estimated_height =
            std::max(1e-6, std::abs(nearest->intensity) /
                           std::sqrt(static_cast<double>(peak_count)));

        const double estimated_area =
            estimated_height * kPi * initial_width / 2.0;

        q[k] = centers[i] / scale[k];
        q[k + 1] = initial_width / scale[k + 1];
        q[k + 2] = estimated_area / scale[k + 2];
    }

    const double min_width =
        std::max(span / static_cast<double>(spectrum.size()) * 0.5, 1e-4);
    const double max_width = span * 2.0;
    const double min_area = std::max(1e-12, area_scale * 1e-12);

    auto apply_bounds = [&](std::vector<double>& params) {
        for (std::size_t i = 0; i < peak_count; ++i) {
            const std::size_t k = i * 3;

            double xc = params[k] * scale[k];
            double w = params[k + 1] * scale[k + 1];
            double area = params[k + 2] * scale[k + 2];

            xc = clamp_value(xc, xmin, xmax);
            w = clamp_value(w, min_width, max_width);
            area = std::max(area, min_area);

            params[k] = xc / scale[k];
            params[k + 1] = w / scale[k + 1];
            params[k + 2] = area / scale[k + 2];
        }
    };

    apply_bounds(q);

    double current_sse = calculate_sse(spectrum, q, scale);
    double lambda = 1e-3;
    bool converged = false;
    int iterations = 0;

    for (int iter = 0; iter < max_iterations; ++iter) {
        iterations = iter + 1;

        // Analytic Jacobian (in scaled q coordinates), accumulate J^T J and
        // J^T residual directly.  This avoids O(points * parameters) extra
        // Lorentz evaluations and large per-iteration Jacobian allocations.
        std::vector<std::vector<double>> hessian(
            parameter_count, std::vector<double>(parameter_count, 0.0));
        std::vector<double> gradient(parameter_count, 0.0);
        std::vector<double> jac(parameter_count, 0.0);

        for (const auto& point : spectrum) {
            const double x = point.wavelength;
            double predicted = 0.0;
            for (std::size_t k = 0; k < parameter_count; k += 3) {
                const double xc = q[k] * scale[k];
                const double w = q[k + 1] * scale[k + 1];
                const double area = q[k + 2] * scale[k + 2];
                const double d = x - xc;
                const double denom = 4.0 * d * d + w * w;
                const double factor = 2.0 / kPi;
                const double denom_sq = denom * denom;
                predicted += factor * area * w / denom;
                // derivative of predicted intensity w.r.t. q[k]
                jac[k]     = (factor * area * w * 8.0 * d / denom_sq) * scale[k];
                jac[k + 1] = (factor * area * (4.0 * d * d - w * w) / denom_sq) * scale[k + 1];
                jac[k + 2] = (factor * w / denom) * scale[k + 2];
            }
            const double residual = point.intensity - predicted;
            for (std::size_t i = 0; i < parameter_count; ++i) {
                gradient[i] += jac[i] * residual;
                for (std::size_t j = i; j < parameter_count; ++j)
                    hessian[i][j] += jac[i] * jac[j];
            }
        }
        for (std::size_t i = 0; i < parameter_count; ++i)
            for (std::size_t j = i + 1; j < parameter_count; ++j)
                hessian[j][i] = hessian[i][j];

        std::vector<std::vector<double>> damped = hessian;
        for (std::size_t i = 0; i < parameter_count; ++i) {
            damped[i][i] += lambda * std::max(hessian[i][i], 1e-12);
        }

        std::vector<double> delta;
        if (!solve_linear_system(damped, gradient, delta)) {
            lambda *= 10.0;
            if (lambda > 1e16)
                break;
            continue;
        }

        double step_norm = 0.0;
        std::vector<double> candidate = q;

        for (std::size_t i = 0; i < parameter_count; ++i) {
            candidate[i] += delta[i];
            step_norm += delta[i] * delta[i];
        }

        step_norm = std::sqrt(step_norm);
        apply_bounds(candidate);

        const double candidate_sse =
            calculate_sse(spectrum, candidate, scale);

        if (std::isfinite(candidate_sse) && candidate_sse < current_sse) {
            const double improvement = current_sse - candidate_sse;
            q = std::move(candidate);
            current_sse = candidate_sse;
            lambda = std::max(lambda / 3.0, 1e-12);

            if (step_norm < 1e-8 ||
                improvement < 1e-12 * (1.0 + current_sse)) {
                converged = true;
                break;
            }
        } else {
            lambda *= 10.0;
            if (lambda > 1e16)
                break;
        }
    }

    MultiLorentzFitResult result;
    result.sse = current_sse;
    result.rmse = std::sqrt(current_sse / static_cast<double>(spectrum.size()));
    result.iterations = iterations;
    result.converged = converged;

    double mean_y = 0.0;
    for (const auto& point : spectrum)
        mean_y += point.intensity;
    mean_y /= static_cast<double>(spectrum.size());

    double ss_tot = 0.0;
    for (const auto& point : spectrum) {
        const double d = point.intensity - mean_y;
        ss_tot += d * d;
    }

    result.r_squared = ss_tot > 0.0
        ? 1.0 - current_sse / ss_tot
        : std::numeric_limits<double>::quiet_NaN();

    for (std::size_t i = 0; i < peak_count; ++i) {
        const std::size_t k = i * 3;
        const double xc = q[k] * scale[k];
        const double w = q[k + 1] * scale[k + 1];
        const double area = q[k + 2] * scale[k + 2];

        result.peaks.push_back({
            xc,
            w,
            2.0 * area / (kPi * w),
            area
        });
    }

    std::sort(result.peaks.begin(), result.peaks.end(),
              [](const MultiLorentzPeak& a, const MultiLorentzPeak& b) {
                  return a.peak_position_nm < b.peak_position_nm;
              });

    return result;
}
