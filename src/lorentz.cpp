
#include "lorentz.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {

// 求解 3x3 线性方程组：Ax = b
// 使用高斯消元和部分主元选取
bool solve_3x3(
    const double input[3][3],
    const double rhs[3],
    double solution[3]
) {
    double m[3][4];

    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            m[i][j] = input[i][j];
        }
        m[i][3] = rhs[i];
    }

    for (int col = 0; col < 3; ++col) {
        int pivot = col;

        for (int row = col + 1; row < 3; ++row) {
            if (std::abs(m[row][col]) >
                std::abs(m[pivot][col])) {
                pivot = row;
            }
        }

        if (std::abs(m[pivot][col]) < 1e-24) {
            return false;
        }

        for (int j = col; j < 4; ++j) {
            std::swap(m[col][j], m[pivot][j]);
        }

        const double divisor = m[col][col];

        for (int j = col; j < 4; ++j) {
            m[col][j] /= divisor;
        }

        for (int row = 0; row < 3; ++row) {
            if (row == col) {
                continue;
            }

            const double factor = m[row][col];

            for (int j = col; j < 4; ++j) {
                m[row][j] -= factor * m[col][j];
            }
        }
    }

    for (int i = 0; i < 3; ++i) {
        solution[i] = m[i][3];
    }

    return true;
}


// 计算残差平方和 SSE
double calculate_sse(
    const std::vector<SpectrumPoint>& data,
    const std::array<double, 3>& p
) {
    double sse = 0.0;

    for (const auto& point : data) {
        const double residual =
            point.intensity -
            lorentz(point.wavelength, p[0], p[1], p[2]);

        sse += residual * residual;
    }

    return sse;
}

} // namespace


// Lorentz 峰模型
double lorentz(double x, double xc, double w, double A) {
    const double d = x - xc;
    const double denominator = 4.0 * d * d + w * w;

    return (2.0 * A / std::acos(-1.0))
           * w / denominator;
}


// Lorentz 非线性最小二乘拟合
LorentzFitResult fit_lorentz(
    const std::vector<SpectrumPoint>& spectrum
) {
    if (spectrum.size() < 3) {
        throw std::invalid_argument(
            "At least 3 spectrum points are required."
        );
    }

    double xmin = spectrum.front().wavelength;
    double xmax = xmin;
    double ymax = spectrum.front().intensity;

    const SpectrumPoint* max_point = &spectrum.front();

    for (const auto& point : spectrum) {
        if (!std::isfinite(point.wavelength) ||
            !std::isfinite(point.intensity)) {
            throw std::invalid_argument(
                "Spectrum contains invalid values."
            );
        }

        xmin = std::min(xmin, point.wavelength);
        xmax = std::max(xmax, point.wavelength);

        if (point.intensity > ymax) {
            ymax = point.intensity;
            max_point = &point;
        }
    }

    if (!(xmax > xmin) || !(ymax > 0.0)) {
        throw std::invalid_argument(
            "Invalid wavelength range or intensity."
        );
    }

    const double pi = std::acos(-1.0);

    // 与 Python 版本使用相同的初始参数
    const double w0 = 140.0;
    const double A0 = ymax * pi * w0 / 2.0;

    // 参数依次为：峰位、FWHM、峰面积
    std::array<double, 3> p = {
        max_point->wavelength,
        w0,
        A0
    };

    // 参数缩放，改善不同参数量级带来的数值问题
    const std::array<double, 3> scale = {
        std::max(1.0, xmax - xmin),
        w0,
        A0
    };

    double current_sse = calculate_sse(spectrum, p);
    double lambda = 1e-3;

    bool converged = false;
    int iterations = 0;

    constexpr int max_iterations = 500;

    for (int iter = 0; iter < max_iterations; ++iter) {
        iterations = iter + 1;

        double H[3][3] = {};
        double g[3] = {};

        // 构造 J^T J 和 J^T r
        for (const auto& point : spectrum) {
            const double x = point.wavelength;
            const double y = point.intensity;

            const double d = x - p[0];
            const double w = p[1];
            const double A = p[2];

            const double denominator =
                4.0 * d * d + w * w;

            const double denominator2 =
                denominator * denominator;

            const double fitted =
                lorentz(x, p[0], w, A);

            const double residual = y - fitted;

            // Lorentz 函数对三个参数的解析导数
            const std::array<double, 3> jacobian = {
                16.0 * A * w * d /
                    (pi * denominator2),

                (2.0 * A / pi) *
                    (4.0 * d * d - w * w) /
                    denominator2,

                (2.0 / pi) * w / denominator
            };

            // 缩放后的雅可比矩阵
            std::array<double, 3> j;

            for (int i = 0; i < 3; ++i) {
                j[i] = jacobian[i] * scale[i];
            }

            for (int i = 0; i < 3; ++i) {
                g[i] += j[i] * residual;

                for (int k = 0; k < 3; ++k) {
                    H[i][k] += j[i] * j[k];
                }
            }
        }

        // LM 阻尼项
        double damped_H[3][3];

        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                damped_H[i][j] = H[i][j];
            }

            damped_H[i][i] +=
                lambda * std::max(H[i][i], 1e-12);
        }

        double delta[3] = {};

        if (!solve_3x3(damped_H, g, delta)) {
            lambda *= 10.0;

            if (lambda > 1e16) {
                break;
            }

            continue;
        }

        double step_norm = 0.0;

        for (int i = 0; i < 3; ++i) {
            step_norm += delta[i] * delta[i];
        }

        step_norm = std::sqrt(step_norm);

        auto candidate = p;

        for (int i = 0; i < 3; ++i) {
            candidate[i] += scale[i] * delta[i];
        }

        // 参数约束：峰位位于数据范围内，FWHM 和面积为正
        candidate[0] = std::clamp(
            candidate[0], xmin, xmax
        );

        candidate[1] = std::max(
            candidate[1], 1e-9
        );

        candidate[2] = std::max(
            candidate[2], 1e-12
        );

        const double candidate_sse =
            calculate_sse(spectrum, candidate);

        if (std::isfinite(candidate_sse) &&
            candidate_sse < current_sse) {

            const double improvement =
                current_sse - candidate_sse;

            p = candidate;
            current_sse = candidate_sse;

            lambda = std::max(lambda / 3.0, 1e-12);

            if (step_norm < 1e-8 ||
                improvement < 1e-12 * (1.0 + current_sse)) {
                converged = true;
                break;
            }
        } else {
            if (step_norm < 1e-8) {
                converged = true;
                break;
            }

            lambda *= 10.0;

            if (lambda > 1e16) {
                break;
            }
        }
    }

    // 计算 R²
    double mean_y = 0.0;

    for (const auto& point : spectrum) {
        mean_y += point.intensity;
    }

    mean_y /= static_cast<double>(spectrum.size());

    double ss_tot = 0.0;

    for (const auto& point : spectrum) {
        const double difference = point.intensity - mean_y;
        ss_tot += difference * difference;
    }

    const double r_squared =
        ss_tot > 0.0
        ? 1.0 - current_sse / ss_tot
        : std::numeric_limits<double>::quiet_NaN();

    return {
        p[0],
        p[1],
        2.0 * p[2] / (pi * p[1]),
        p[2],
        r_squared,
        iterations,
        converged
    };
}
