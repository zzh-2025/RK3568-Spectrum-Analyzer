#pragma once

#include "multilorentz.h"

#include <vector>

// Adaptive:先尝试少量高优先级初值；若结果质量或稳定性不足，则回退到完整27组。
// Full:完整执行原始27组初始化，用于基准对照。
enum class AutoPeakSearchMode {
    Adaptive,
    Full
};

// 此模块只负责“指定峰数时”的自动初始化与联合拟合，不负责推断真实峰数。
struct AutoPeakFitResult {
    std::vector<double> initial_centers_nm;
    MultiLorentzFitResult fit;
    int trials_attempted = 0;
    int converged_trials = 0;
    bool used_fallback = false;
    double elapsed_ms = 0.0;
};

// 与旧接口兼容；第4参数为新增的可选搜索模式。
AutoPeakFitResult fit_auto_centers(
    const std::vector<SpectrumPoint>& spectrum,
    int peak_count,
    int max_iterations = 350,
    AutoPeakSearchMode mode = AutoPeakSearchMode::Adaptive);
