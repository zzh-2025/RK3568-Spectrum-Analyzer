#pragma once

#include "model_selection.h"

#include <limits>
#include <string>
#include <vector>

// Stage 5: compare N Lorentzian peaks with NO, CONSTANT and LINEAR background.
// A linear background is b0 + b1 * (x - xmid) / halfspan, so b1 is the
// baseline change from xmid to the right edge, NOT counts per nm.
enum class BaselineType { None, Constant, Linear };

struct BaselineCandidate {
    int peak_count = 0;
    BaselineType baseline = BaselineType::None;
    int parameter_count = 0;             // 3*N + 0/1/2
    bool converged = false;
    bool acceptable = false;
    bool from_no_baseline_seed = true;
    std::string note;
    MultiLorentzFitResult fit;            // fit.peaks and fit.sse etc.
    double b0 = 0.0;
    double b1 = 0.0;                    // normalized slope; left=b0-b1, right=b0+b1
    double aic = std::numeric_limits<double>::quiet_NaN();
    double bic = std::numeric_limits<double>::quiet_NaN();
    double delta_bic = std::numeric_limits<double>::quiet_NaN();
    double residual_lag1 = std::numeric_limits<double>::quiet_NaN();
    double edge_left_rmse = std::numeric_limits<double>::quiet_NaN();
    double edge_right_rmse = std::numeric_limits<double>::quiet_NaN();
    double min_gap_over_mean_fwhm = std::numeric_limits<double>::quiet_NaN();
    bool substantially_negative_baseline = false; // diagnostic only, not an automatic rejection
    double elapsed_ms = 0.0;
};

struct BaselineComparisonOptions {
    ModelSelectionOptions peak_options;
    int refinement_iterations = 300;
};

struct BaselineComparisonResult {
    std::vector<BaselineCandidate> models;
    int best_index = -1;
    int second_index = -1;
    double xmid_nm = 0.0;
    double halfspan_nm = 0.0;
    double elapsed_ms = 0.0;
    bool requires_review = true;
    std::string explanation;
};

const char* baseline_name(BaselineType baseline);

double baseline_value(double x, double xmid, double halfspan,
                      BaselineType type, double b0, double b1);

BaselineComparisonResult compare_baselines(
    const std::vector<SpectrumPoint>& spectrum,
    const BaselineComparisonOptions& options = {});
