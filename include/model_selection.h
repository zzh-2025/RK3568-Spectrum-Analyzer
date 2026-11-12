#pragma once

#include "autopeak.h"

#include <limits>
#include <string>
#include <vector>

// 第一阶段：在相同原始数据和同一个「无基线 Lorentzian 叠加模型」上选择峰数。
// 假设：残差独立、近似同方差高斯噪声。AIC/BIC 是统计参考，不证明物理峰数。
struct ModelSelectionOptions {
    int min_peaks = 1;
    int max_peaks = 5;
    int max_iterations = 350;
    AutoPeakSearchMode search_mode = AutoPeakSearchMode::Adaptive;

    // 数值可辨识性约束：与现有 autopeak.cpp 的峰形合理性检查尽量一致。
    double min_width_steps = 2.0;
    double max_width_span_fraction = 1.5;
    double min_height_fraction = 0.005;
    double edge_margin_steps = 1.0;
    double min_separation_steps = 2.0;

    // 仅用于提示「需要复核」，绝不用于擅自剔除一个已收敛的拟合模型。
    // 中心间距/(两个FWHM均值) 小于0.6提示分峰可能严重重叠。
    double overlap_review_ratio = 0.60;
    // 相邻残差相关度较大，独立残差假设可能不成立，BIC仅供参考。
    double residual_correlation_review = 0.25;
    // 两端5%波段残差均值如达到1倍整体RMSE，提示边缘系统误差。
    double edge_bias_review_rmse = 1.0;
};

struct PeakModelDiagnostics {
    // 相对前一个合格候选模型的 SSE 降低百分比，非嵌套/异常模型则为NaN。
    double sse_gain_from_previous_percent = std::numeric_limits<double>::quiet_NaN();
    double residual_lag1_correlation = std::numeric_limits<double>::quiet_NaN();
    double left_edge_bias_rmse = std::numeric_limits<double>::quiet_NaN();
    double right_edge_bias_rmse = std::numeric_limits<double>::quiet_NaN();
    double min_spacing_over_mean_fwhm = std::numeric_limits<double>::quiet_NaN();
    int closest_peak_left = 0;  // 1-based index
    int closest_peak_right = 0;
    bool overlapping_peaks = false;
    bool correlated_residuals = false;
    bool edge_bias = false;
};

struct PeakModelCandidate {
    int peak_count = 0;
    int parameter_count = 0; // 无基线 Lorentz 模型：每峰中心、峰宽、面积，共 3*N
    bool converged = false;
    bool acceptable = false;
    std::string note;  // 拟合异常或参数合理性失败的原因

    AutoPeakFitResult run;
    double elapsed_ms = 0.0; // wall time for this candidate, including failures
    double aic = std::numeric_limits<double>::quiet_NaN();
    double bic = std::numeric_limits<double>::quiet_NaN();
    double delta_bic = std::numeric_limits<double>::quiet_NaN();
    PeakModelDiagnostics diagnostics;
};

struct ModelSelectionResult {
    std::vector<PeakModelCandidate> candidates;
    int recommended_peak_count = 0;  // 0 表示没有满足条件的候选模型
    int alternative_peak_count = 0;  // 0 表示没有第二个可接受候选模型
    bool at_max_peak_limit = false; // 推荐数等于用户设置的 Nmax，需额外复核
    bool needs_manual_review = false; // 额外诊断提示；不覆盖统计结果
    double total_elapsed_ms = 0.0;
    std::string explanation;
};

ModelSelectionResult select_peak_count(
    const std::vector<SpectrumPoint>& spectrum,
    const ModelSelectionOptions& options = {});
