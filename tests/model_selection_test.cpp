#include "model_selection.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void print_usage()
{
    std::cerr << "Usage: model_selection_test <file.asc> [max_peaks] [--full]"
                 " [--iterations N] [--range START_NM END_NM]\n"
                 "Example: model_selection_test data/3-大凹陷.asc 5\n";
}

int parse_positive_int(const std::string& token, const std::string& name)
{
    std::size_t consumed = 0;
    int value = std::stoi(token, &consumed);
    if (consumed != token.size() || value < 1)
        throw std::invalid_argument(name + " must be a positive integer.");
    return value;
}

double parse_double(const std::string& token, const std::string& name)
{
    std::size_t consumed = 0;
    const double value = std::stod(token, &consumed);
    if (consumed != token.size() || !std::isfinite(value))
        throw std::invalid_argument(name + " must be finite numeric data.");
    return value;
}

std::string display(double x, int digits = 3)
{
    if (!std::isfinite(x)) return "-";
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(digits) << x;
    return stream.str();
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc < 2) {
        print_usage();
        return 1;
    }

    try {
        const std::string file_path = argv[1];
        ModelSelectionOptions options;
        bool max_peaks_set = false;
        bool use_range = false;
        double range_min = 0.0, range_max = 0.0;
        for (int i = 2; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--full") {
                options.search_mode = AutoPeakSearchMode::Full;
            } else if (option == "--iterations") {
                if (++i >= argc) throw std::invalid_argument("Missing --iterations value.");
                options.max_iterations = parse_positive_int(argv[i], "iterations");
            } else if (option == "--range") {
                if (i + 2 >= argc) throw std::invalid_argument("Missing --range bounds.");
                range_min = parse_double(argv[++i], "range minimum");
                range_max = parse_double(argv[++i], "range maximum");
                if (!(range_max > range_min))
                    throw std::invalid_argument("Range maximum must exceed minimum.");
                use_range = true;
            } else if (option.rfind("--", 0) != 0 && !max_peaks_set) {
                options.max_peaks = parse_positive_int(option, "max_peaks");
                max_peaks_set = true;
            } else {
                throw std::invalid_argument("Unknown or duplicated option: " + option);
            }
        }

        std::ifstream input(file_path);
        if (!input) throw std::runtime_error("Cannot open ASC file: " + file_path);
        std::vector<SpectrumPoint> spectrum;
        std::string line;
        while (std::getline(input, line)) {
            std::istringstream ss(line);
            double x, y;
            if (ss >> x >> y && std::isfinite(x) && std::isfinite(y) &&
                (!use_range || (x >= range_min && x <= range_max)))
                spectrum.push_back({x, y});
        }
        if (spectrum.empty())
            throw std::runtime_error("No valid spectrum points were found.");

        const auto result = select_peak_count(spectrum, options);
        std::cout << "Input: " << file_path << '\n';
        std::cout << "Points: " << spectrum.size() << '\n';
        std::cout << "Search: " << (options.search_mode == AutoPeakSearchMode::Full
                              ? "full" : "adaptive") << '\n';
        std::cout << "Candidate N: " << options.min_peaks << ".." << options.max_peaks << '\n';
        std::cout << "N  Status   Trials  Conv  Fallback  R2        RMSE      SSE        AIC        BIC        dBIC       Time(ms)  Details\n";
        for (const auto& c : result.candidates) {
            const auto& fit = c.run.fit;
            std::cout << std::setw(1) << c.peak_count << "  "
                      << std::setw(7) << (c.acceptable ? "PASS" : "REVIEW") << "  "
                      << std::setw(6) << c.run.trials_attempted << "  "
                      << std::setw(4) << c.run.converged_trials << "  "
                      << std::setw(8) << (c.run.used_fallback ? "yes" : "no") << "  "
                      << std::setw(8) << display(fit.r_squared, 6) << "  "
                      << std::setw(8) << display(fit.rmse) << "  "
                      << std::setw(10) << display(fit.sse) << "  "
                      << std::setw(10) << display(c.aic) << "  "
                      << std::setw(10) << display(c.bic) << "  "
                      << std::setw(10) << display(c.delta_bic) << "  "
                      << std::setw(8) << display(c.elapsed_ms) << "  "
                      << (c.note.empty() ? "-" : c.note) << '\n';
        }
        std::cout << "Total elapsed: " << display(result.total_elapsed_ms) << " ms\n";
        if (result.recommended_peak_count == 0) {
            std::cout << "Recommended peaks: none (manual review needed)\n";
            std::cout << result.explanation << '\n';
            return 2;
        }
        std::cout << "Recommended peaks (BIC): " << result.recommended_peak_count
                  << (result.needs_manual_review ? " [PROVISIONAL / REVIEW]" : "") << '\n';
        std::cout << "Alternative peaks: " << (result.alternative_peak_count
                            ? std::to_string(result.alternative_peak_count) : "none") << '\n';
        std::cout << "Note: " << result.explanation << '\n';
        std::cout << "\nDiagnostics (informational; do NOT override BIC):\n";
        std::cout << "N  SSEgain(%)  ResidualLag1  MinGap/MeanFWHM  EdgeLeft/RMSE  EdgeRight/RMSE  Flags\n";
        for (const auto& c : result.candidates) {
            if (!c.acceptable) continue;
            const auto& d = c.diagnostics;
            std::cout << c.peak_count << "  "
                      << std::setw(10) << display(d.sse_gain_from_previous_percent, 2) << "  "
                      << std::setw(12) << display(d.residual_lag1_correlation, 3) << "  "
                      << std::setw(15) << display(d.min_spacing_over_mean_fwhm, 3) << "  "
                      << std::setw(13) << display(d.left_edge_bias_rmse, 2) << "  "
                      << std::setw(14) << display(d.right_edge_bias_rmse, 2) << "  ";
            bool flagged = false;
            if (d.overlapping_peaks) { std::cout << "OVERLAP "; flagged = true; }
            if (d.correlated_residuals) { std::cout << "CORRELATED "; flagged = true; }
            if (d.edge_bias) { std::cout << "EDGE_BIAS "; flagged = true; }
            if (!flagged) std::cout << "-";
            std::cout << '\n';
        }
        const auto& selected = result.candidates.at(
            static_cast<std::size_t>(result.recommended_peak_count - options.min_peaks));
        const auto& diagnostic = selected.diagnostics;
        if (diagnostic.closest_peak_left > 0) {
            std::cout << "Closest components in BIC model: #"
                      << diagnostic.closest_peak_left << " and #"
                      << diagnostic.closest_peak_right << ", gap/mean FWHM="
                      << display(diagnostic.min_spacing_over_mean_fwhm, 3) << '\n';
        }
        std::cout << "Diagnostic thresholds (heuristic): overlap < "
                  << display(options.overlap_review_ratio, 2)
                  << ", |lag1| >= " << display(options.residual_correlation_review, 2)
                  << ", |edge mean|/RMSE >= " << display(options.edge_bias_review_rmse, 2)
                  << ". These flags do not prove a component is false.\n";
        if (result.needs_manual_review)
            std::cout << "Decision: BIC ranking is PROVISIONAL; compare spectra, baseline, residuals and physical evidence.\n";
        for (const auto& c : result.candidates) {
            if (c.peak_count != result.recommended_peak_count) continue;
            std::cout << "Index,Center_nm,FWHM_nm,PeakHeight,Area\n";
            for (std::size_t i = 0; i < c.run.fit.peaks.size(); ++i) {
                const auto& p = c.run.fit.peaks[i];
                std::cout << (i + 1) << ',' << std::fixed << std::setprecision(6)
                          << p.peak_position_nm << ',' << p.fwhm_nm << ','
                          << p.peak_height << ',' << p.peak_area << '\n';
            }
            break;
        }
        std::cout << "Caution: best statistical fit != physical resonance count."
                     " Model has no baseline term.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Model selection error: " << e.what() << '\n';
        print_usage();
        return 3;
    }
}
