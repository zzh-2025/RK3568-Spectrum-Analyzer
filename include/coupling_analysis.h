#pragma once

#include "multilorentz.h"
#include <limits>
#include <string>
#include <vector>

// A pair of fitted Lorentz components is a screening proxy for two spectral branches.
// Its energy separation is not, by itself, proof of strong coupling or a measured 2g.
struct CouplingPairSelection {
    enum class Mode { Strongest, Indices, Targets, Windows };
    Mode mode = Mode::Strongest;
    int index_a = 0;       // 1-based, ascending wavelength
    int index_b = 0;
    double up_target_nm = 0.0, lp_target_nm = 0.0;
    double up_min_nm = 0.0, up_max_nm = 0.0;
    double lp_min_nm = 0.0, lp_max_nm = 0.0;
};

struct CouplingPairMetrics {
    bool valid = false;
    int up_index = 0, lp_index = 0; // 1-based
    double up_nm = std::numeric_limits<double>::quiet_NaN();
    double lp_nm = std::numeric_limits<double>::quiet_NaN();
    double up_fwhm_nm = std::numeric_limits<double>::quiet_NaN();
    double lp_fwhm_nm = std::numeric_limits<double>::quiet_NaN();
    double up_e_ev = std::numeric_limits<double>::quiet_NaN();
    double lp_e_ev = std::numeric_limits<double>::quiet_NaN();
    double splitting_mev = std::numeric_limits<double>::quiet_NaN();
    double apparent_g_mev = std::numeric_limits<double>::quiet_NaN();
    double up_gamma_mev = std::numeric_limits<double>::quiet_NaN();
    double lp_gamma_mev = std::numeric_limits<double>::quiet_NaN();
    double avg_gamma_mev = std::numeric_limits<double>::quiet_NaN();
    double splitting_to_width_ratio = std::numeric_limits<double>::quiet_NaN();
    double separation_over_mean_fwhm = std::numeric_limits<double>::quiet_NaN();
    bool linewidth_criterion_pass = false;
    bool borderline = false;
    bool overlapping_pair = false;
    std::string warning;
};

// Requires peaks sorted by ascending peak_position_nm as returned by the fit.
// Strongest mode is a numerical convenience, not a physical UP/LP classifier.
CouplingPairMetrics calculate_coupling_pair(
    const std::vector<MultiLorentzPeak>& peaks,
    const CouplingPairSelection& selection);
