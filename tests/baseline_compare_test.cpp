#include "baseline_compare.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::string fmt(double x, int d=3) {
    if (!std::isfinite(x)) return "-";
    std::ostringstream s;
    s << std::fixed << std::setprecision(d) << x;
    return s.str();
}

int parse_positive_int(const std::string& x) {
    std::size_t end=0;
    const int value=std::stoi(x,&end);
    if (end!=x.size() || value<1 || value>20000)
        throw std::invalid_argument("Value must be an integer between 1 and 20000");
    return value;
}

void usage() {
    std::cerr << "Usage: baseline_compare_test <file.asc> [max_peaks] "
                 "[--csv summary.csv] [--curves curves.csv] [--iterations N] "
                 "[--range MIN_NM MAX_NM]\n";
}

void export_summary(const std::string& path, const BaselineComparisonResult& result) {
    std::ofstream f(path);
    if (!f) throw std::runtime_error("Cannot create CSV: "+path);
    f << "N,baseline,status,parameters,converged,R2,RMSE,SSE,AIC,BIC,deltaBIC,"
         "b0_mid,b1_halfspan,baseline_left,baseline_right,residual_lag1,"
         "edge_left_rmse,edge_right_rmse,min_gap_mean_fwhm,negative_baseline,time_ms,note\n";
    f << std::setprecision(13);
    for (const auto& m: result.models) {
        const double left=m.b0-m.b1, right=m.b0+m.b1;
        f << m.peak_count << ',' << baseline_name(m.baseline) << ','
          << (m.acceptable?"PASS":"REVIEW") << ',' << m.parameter_count << ','
          << (m.converged?"yes":"no") << ',' << m.fit.r_squared << ','
          << m.fit.rmse << ',' << m.fit.sse << ',' << m.aic << ',' << m.bic << ','
          << m.delta_bic << ',' << m.b0 << ',' << m.b1 << ',' << left << ',' << right << ','
          << m.residual_lag1 << ',' << m.edge_left_rmse << ',' << m.edge_right_rmse << ','
          << m.min_gap_over_mean_fwhm << ',' << (m.substantially_negative_baseline?"yes":"no")
          << ',' << m.elapsed_ms << ',' << m.note << '\n';
    }
}

void export_curves(const std::string& path, const std::vector<SpectrumPoint>& spectrum,
                   const BaselineComparisonResult& result)
{
    std::ofstream f(path);
    if (!f) throw std::runtime_error("Cannot create curve CSV: "+path);
    f << "N,baseline,wavelength_nm,measured,fit_total,fit_peaks,fit_baseline,residual\n";
    f << std::setprecision(13);
    for (const auto& m: result.models) {
        if (!m.acceptable) continue;
        for (const auto& p: spectrum) {
            const double peak=multi_lorentz(p.wavelength,m.fit.peaks);
            const double bg=baseline_value(p.wavelength,result.xmid_nm,
                            result.halfspan_nm,m.baseline,m.b0,m.b1);
            const double predicted=peak+bg;
            f << m.peak_count << ',' << baseline_name(m.baseline) << ','
              << p.wavelength << ',' << p.intensity << ',' << predicted << ','
              << peak << ',' << bg << ',' << p.intensity-predicted << '\n';
        }
    }
}
} // namespace

int main(int argc, char* argv[]) {
    if (argc<2) { usage(); return 1; }
    try {
        std::string file=argv[1], csv, curves;
        BaselineComparisonOptions options;
        bool count_set=false, has_range=false;
        double lo=0.0,hi=0.0;
        for (int i=2;i<argc;++i) {
            const std::string arg=argv[i];
            if (arg=="--csv" || arg=="--curves") {
                if (++i>=argc) throw std::invalid_argument("Missing output CSV path");
                if (arg=="--csv") csv=argv[i]; else curves=argv[i];
            } else if (arg=="--iterations") {
                if (++i>=argc) throw std::invalid_argument("Missing iteration count");
                options.refinement_iterations=parse_positive_int(argv[i]);
            } else if (arg=="--range") {
                if (i+2>=argc) throw std::invalid_argument("Missing range bounds");
                lo=std::stod(argv[++i]); hi=std::stod(argv[++i]);
                if (!std::isfinite(lo) || !std::isfinite(hi) || !(lo<hi))
                    throw std::invalid_argument("Invalid wavelength range");
                has_range=true;
            } else if (!count_set && arg.rfind("--",0)!=0) {
                options.peak_options.max_peaks=parse_positive_int(arg);
                if (options.peak_options.max_peaks > 12)
                    throw std::invalid_argument("max_peaks must be <= 12");
                count_set=true;
            } else throw std::invalid_argument("Unknown or duplicate option: "+arg);
        }
        std::ifstream input(file);
        if (!input) throw std::runtime_error("Cannot open ASC: "+file);
        std::vector<SpectrumPoint> spectrum;
        std::string line;
        while (std::getline(input,line)) {
            std::istringstream ss(line);
            double x,y;
            if (ss>>x>>y && std::isfinite(x) && std::isfinite(y) &&
                (!has_range || (x>=lo && x<=hi))) spectrum.push_back({x,y});
        }
        if (spectrum.empty()) throw std::runtime_error("No valid samples in ASC file");
        auto result=compare_baselines(spectrum,options);
        std::cout << "Input: " << file << "  Points: " << spectrum.size() << '\n';
        std::cout << "Comparing N=1.." << options.peak_options.max_peaks
                  << " and baselines: none, constant, linear\n";
        std::cout << "Linear baseline: b0 + b1*(x-xmid)/halfspan; xmid="
                  << fmt(result.xmid_nm) << ", halfspan=" << fmt(result.halfspan_nm) << " nm\n";
        std::cout << "N  Background Status  k   R2        RMSE       BIC        dBIC    "
                     "b0(mid)    b1(half)  Lag1   EdgeL   EdgeR    Time(ms) Note\n";
        for (const auto& m: result.models) {
            std::cout << std::setw(1) << m.peak_count << "  "
                      << std::setw(10) << baseline_name(m.baseline) << ' '
                      << std::setw(6) << (m.acceptable?"PASS":"REVIEW") << "  "
                      << std::setw(2) << m.parameter_count << "  "
                      << std::setw(8) << fmt(m.fit.r_squared,6) << "  "
                      << std::setw(8) << fmt(m.fit.rmse) << "  "
                      << std::setw(10) << fmt(m.bic) << "  "
                      << std::setw(8) << fmt(m.delta_bic) << "  "
                      << std::setw(9) << fmt(m.b0,2) << "  "
                      << std::setw(9) << fmt(m.b1,2) << "  "
                      << std::setw(5) << fmt(m.residual_lag1,2) << "  "
                      << std::setw(6) << fmt(m.edge_left_rmse,2) << "  "
                      << std::setw(6) << fmt(m.edge_right_rmse,2) << "  "
                      << std::setw(8) << fmt(m.elapsed_ms,1) << "  "
                      << (m.note.empty()?"-":m.note) << '\n';
        }
        std::cout << "Total: " << fmt(result.elapsed_ms) << " ms\n";
        if (result.best_index>=0) {
            const auto& best=result.models[static_cast<std::size_t>(result.best_index)];
            std::cout << "Lowest-BIC candidate: N=" << best.peak_count
                      << ", baseline=" << baseline_name(best.baseline)
                      << " [" << (result.requires_review?"PROVISIONAL / REVIEW":"STATISTICAL ONLY")
                      << "]\n";
            std::cout << "Fitted baseline at left/mid/right: "
                      << fmt(best.b0-best.b1,3) << " / " << fmt(best.b0,3)
                      << " / " << fmt(best.b0+best.b1,3) << '\n';
            std::cout << "Best-model diagnostics: residual lag1=" << fmt(best.residual_lag1,3)
                      << ", min peak gap/mean FWHM=" << fmt(best.min_gap_over_mean_fwhm,3)
                      << ", edge L/R (RMSE units)=" << fmt(best.edge_left_rmse,2)
                      << "/" << fmt(best.edge_right_rmse,2) << '\n';
            if (best.substantially_negative_baseline)
                std::cout << "REVIEW: fitted baseline becomes substantially negative; "
                             "verify blank/dark-correction data before physical interpretation.\n";
            std::cout << "Selected model peaks (center_nm, FWHM_nm, height, area):\n";
            for (const auto& p: best.fit.peaks)
                std::cout << fmt(p.peak_position_nm,5) << ", " << fmt(p.fwhm_nm,5)
                          << ", " << fmt(p.peak_height,4) << ", " << fmt(p.peak_area,4) << '\n';
        } else std::cout << "No acceptable model.\n";
        std::cout << "Important: " << result.explanation << '\n';
        if (!csv.empty()) { export_summary(csv,result); std::cout << "Summary CSV: " << csv << '\n'; }
        if (!curves.empty()) { export_curves(curves,spectrum,result); std::cout << "Curves CSV: " << curves << '\n'; }
        return result.best_index<0?2:0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n'; usage(); return 3;
    }
}
