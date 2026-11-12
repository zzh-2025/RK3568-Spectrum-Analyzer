#include "coupling_analysis.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {
constexpr double hc_ev_nm = 1239.841984;
constexpr double eps = 1e-12;

void append_warning(std::string& output, const char* warning) {
    if (!output.empty()) output += ";";
    output += warning;
}

double gamma_mev(double center, double fwhm) {
    const double left = center - 0.5 * fwhm;
    const double right = center + 0.5 * fwhm;
    if (!(left > 0 && right > left) || !std::isfinite(right))
        return std::numeric_limits<double>::quiet_NaN();
    return 1000.0 * hc_ev_nm * (1.0 / left - 1.0 / right);
}
} // namespace

CouplingPairMetrics calculate_coupling_pair(
    const std::vector<MultiLorentzPeak>& peaks,
    const CouplingPairSelection& selection) {
    CouplingPairMetrics r;
    if (peaks.size() < 2) {
        r.warning = "LESS_THAN_TWO_PEAKS";
        return r;
    }
    int a = -1, b = -1;
    switch (selection.mode) {
        case CouplingPairSelection::Mode::Indices:
            a = selection.index_a - 1;
            b = selection.index_b - 1;
            if (a < 0 || b < 0 || a >= static_cast<int>(peaks.size()) ||
                b >= static_cast<int>(peaks.size()) || a == b) {
                r.warning = "PAIR_INDEX_UNAVAILABLE";
                return r;
            }
            break;
        case CouplingPairSelection::Mode::Strongest: {
            std::vector<int> ix;
            ix.reserve(peaks.size());
            for (int i = 0; i < static_cast<int>(peaks.size()); ++i) ix.push_back(i);
            std::sort(ix.begin(), ix.end(), [&](int i, int j) {
                return peaks[i].peak_height > peaks[j].peak_height;
            });
            a = ix[0]; b = ix[1];
            append_warning(r.warning, "AUTO_STRONGEST_IS_HEURISTIC");
            break;
        }
        case CouplingPairSelection::Mode::Targets: {
            if (!(selection.up_target_nm > 0 && selection.lp_target_nm > 0 &&
                  selection.up_target_nm < selection.lp_target_nm)) {
                r.warning = "INVALID_TARGETS";
                return r;
            }
            double best_cost = std::numeric_limits<double>::infinity();
            for (int i = 0; i < static_cast<int>(peaks.size()); ++i)
                for (int j = i + 1; j < static_cast<int>(peaks.size()); ++j) {
                    const double cost = std::abs(peaks[i].peak_position_nm - selection.up_target_nm)
                                      + std::abs(peaks[j].peak_position_nm - selection.lp_target_nm);
                    if (cost < best_cost) { best_cost = cost; a = i; b = j; }
                }
            if (a >= 0 && (std::abs(peaks[a].peak_position_nm-selection.up_target_nm)>35.0 ||
                           std::abs(peaks[b].peak_position_nm-selection.lp_target_nm)>35.0))
                append_warning(r.warning, "FAR_FROM_TARGET");
            break;
        }
        case CouplingPairSelection::Mode::Windows: {
            if (!(selection.up_min_nm < selection.up_max_nm &&
                  selection.lp_min_nm < selection.lp_max_nm)) {
                r.warning = "INVALID_WINDOWS";
                return r;
            }
            double best_score = -std::numeric_limits<double>::infinity();
            for (int i = 0; i < static_cast<int>(peaks.size()); ++i)
                for (int j = i + 1; j < static_cast<int>(peaks.size()); ++j) {
                    const auto& up = peaks[i];
                    const auto& lp = peaks[j];
                    if (up.peak_position_nm < selection.up_min_nm ||
                        up.peak_position_nm > selection.up_max_nm ||
                        lp.peak_position_nm < selection.lp_min_nm ||
                        lp.peak_position_nm > selection.lp_max_nm) continue;
                    const double score = up.peak_height + lp.peak_height;
                    if (score > best_score) { best_score = score; a = i; b = j; }
                }
            if (a < 0) { r.warning = "NO_TWO_PEAKS_IN_WINDOWS"; return r; }
            break;
        }
    }
    if (a < 0 || b < 0) { r.warning = "PAIR_UNAVAILABLE"; return r; }
    if (peaks[a].peak_position_nm > peaks[b].peak_position_nm) std::swap(a,b);
    if (!(peaks[a].peak_position_nm > 0 && peaks[b].peak_position_nm > peaks[a].peak_position_nm &&
          peaks[a].fwhm_nm > eps && peaks[b].fwhm_nm > eps)) {
        r.warning = "INVALID_PEAK_PARAMETERS";
        return r;
    }
    r.up_index = a+1; r.lp_index = b+1;
    r.up_nm = peaks[a].peak_position_nm; r.lp_nm = peaks[b].peak_position_nm;
    r.up_fwhm_nm = peaks[a].fwhm_nm; r.lp_fwhm_nm = peaks[b].fwhm_nm;
    r.up_e_ev = hc_ev_nm / r.up_nm;
    r.lp_e_ev = hc_ev_nm / r.lp_nm;
    r.splitting_mev = 1000.0 * (r.up_e_ev-r.lp_e_ev);
    r.apparent_g_mev = r.splitting_mev/2.0;
    r.up_gamma_mev = gamma_mev(r.up_nm,r.up_fwhm_nm);
    r.lp_gamma_mev = gamma_mev(r.lp_nm,r.lp_fwhm_nm);
    r.avg_gamma_mev = (r.up_gamma_mev+r.lp_gamma_mev)/2.0;
    r.separation_over_mean_fwhm = (r.lp_nm-r.up_nm) /
        ((r.up_fwhm_nm+r.lp_fwhm_nm)/2.0);
    if (!(std::isfinite(r.avg_gamma_mev) && r.avg_gamma_mev > eps &&
          std::isfinite(r.splitting_mev))) {
        append_warning(r.warning,"INVALID_ENERGY_FWHM");
        return r;
    }
    r.splitting_to_width_ratio = r.splitting_mev / r.avg_gamma_mev;
    r.linewidth_criterion_pass = r.splitting_mev > r.avg_gamma_mev;
    r.borderline = r.splitting_to_width_ratio >= 0.9 &&
                   r.splitting_to_width_ratio <= 1.1;
    r.overlapping_pair = r.separation_over_mean_fwhm < 0.60;
    if (r.borderline) append_warning(r.warning,"BORDERLINE_LINEWIDTH");
    if (r.overlapping_pair) append_warning(r.warning,"OVERLAPPING_SELECTED_PEAKS");
    r.valid = true;
    return r;
}
