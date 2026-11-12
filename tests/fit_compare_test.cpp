// Stage 6: compare two fitted peak-count models, export components and offline SVG.
// This standalone test reuses the existing Stage-5 model; it does not modify the solver.
#include "baseline_compare.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace {
constexpr double pi = 3.14159265358979323846;

struct Config {
    std::string input;
    fs::path output = "results/stage6_compare";
    BaselineType baseline = BaselineType::Constant;
    int n1 = 4;
    int n2 = 5;
    bool has_range = false;
    double xmin = 0, xmax = 0;
};

struct Trace {
    const BaselineCandidate* candidate = nullptr;
    std::vector<double> total;
    std::vector<double> baseline;
    std::vector<double> residual;
    std::vector<std::vector<double>> components;  // Each component excludes baseline.
};

std::string format(double x, int digits = 3) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(digits) << x;
    return os.str();
}

double arg_double(const std::string& value) {
    std::size_t used = 0;
    double v = std::stod(value, &used);
    if (used != value.size() || !std::isfinite(v))
        throw std::invalid_argument("Invalid numeric argument: " + value);
    return v;
}

int arg_peaks(const std::string& value) {
    std::size_t used = 0;
    int n = std::stoi(value, &used);
    if (used != value.size() || n < 1 || n > 12)
        throw std::invalid_argument("Peak count must be 1..12: " + value);
    return n;
}

Config parse(int argc, char** argv) {
    if (argc < 2) throw std::invalid_argument(
        "Usage: fit_compare_test <data.asc> [--peaks 4 5] "
        "[--baseline constant|linear|none] [--out results/stage6_compare] "
        "[--range lower_nm upper_nm]");
    Config c;
    c.input = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::string s = argv[i];
        if (s == "--peaks" && i + 2 < argc) {
            c.n1 = arg_peaks(argv[++i]);
            c.n2 = arg_peaks(argv[++i]);
        } else if (s == "--out" && i + 1 < argc) {
            c.output = argv[++i];
        } else if (s == "--baseline" && i + 1 < argc) {
            const std::string val = argv[++i];
            if (val == "constant") c.baseline = BaselineType::Constant;
            else if (val == "linear") c.baseline = BaselineType::Linear;
            else if (val == "none") c.baseline = BaselineType::None;
            else throw std::invalid_argument("Unknown baseline: " + val);
        } else if (s == "--range" && i + 2 < argc) {
            c.xmin = arg_double(argv[++i]);
            c.xmax = arg_double(argv[++i]);
            c.has_range = true;
            if (!(c.xmin < c.xmax)) throw std::invalid_argument("Invalid range");
        } else throw std::invalid_argument("Unknown / incomplete argument: " + s);
    }
    if (c.n1 == c.n2) throw std::invalid_argument("Choose two different peak counts");
    return c;
}

std::vector<SpectrumPoint> load_spectrum(const Config& cfg) {
    std::ifstream file(cfg.input);
    if (!file) throw std::runtime_error("Cannot open ASC: " + cfg.input);
    std::vector<SpectrumPoint> points;
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream ss(line);
        double x, y;
        if (ss >> x >> y && std::isfinite(x) && std::isfinite(y) &&
            (!cfg.has_range || (x >= cfg.xmin && x <= cfg.xmax)))
            points.push_back({x, y});
    }
    std::sort(points.begin(), points.end(), [](const SpectrumPoint& a, const SpectrumPoint& b) {
        return a.wavelength < b.wavelength;
    });
    if (points.size() < 15) throw std::runtime_error("Too few valid data points");
    return points;
}

const BaselineCandidate& get_model(const BaselineComparisonResult& result,
                                   int n, BaselineType baseline) {
    for (const auto& m : result.models)
        if (m.peak_count == n && m.baseline == baseline) {
            if (!m.acceptable) throw std::runtime_error(
                "N=" + std::to_string(n) + " not acceptable: " + m.note);
            return m;
        }
    throw std::runtime_error("Requested candidate was not computed");
}

double component(double x, const MultiLorentzPeak& p) {
    const double dx = x - p.peak_position_nm;
    const double w = p.fwhm_nm;
    return (2.0 / pi) * p.peak_area * w / (4.0 * dx * dx + w * w);
}

Trace make_trace(const std::vector<SpectrumPoint>& spectrum,
                 const BaselineComparisonResult& result, const BaselineCandidate& m) {
    Trace t;
    t.candidate = &m;
    const auto size = spectrum.size();
    t.total.reserve(size);
    t.residual.reserve(size);
    t.baseline.reserve(size);
    t.components.resize(m.fit.peaks.size());
    for (auto& v : t.components) v.reserve(size);
    for (const auto& s : spectrum) {
        double bg = baseline_value(s.wavelength, result.xmid_nm, result.halfspan_nm,
                                   m.baseline, m.b0, m.b1);
        double fit = bg;
        for (std::size_t i = 0; i < m.fit.peaks.size(); ++i) {
            double individual = component(s.wavelength, m.fit.peaks[i]);
            t.components[i].push_back(individual);
            fit += individual;
        }
        t.baseline.push_back(bg);
        t.total.push_back(fit);
        t.residual.push_back(s.intensity - fit);
    }
    return t;
}

void write_curves(const fs::path& path, const std::vector<SpectrumPoint>& pts,
                  const Trace& first, const Trace& second) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Cannot write " + path.string());
    const std::size_t max_components = std::max(first.components.size(), second.components.size());
    out << "N,baseline_type,wavelength_nm,measured,fit_total,fit_baseline,residual";
    for (std::size_t i = 0; i < max_components; ++i)
        out << ",component_" << (i + 1);
    out << '\n' << std::setprecision(14);
    for (const Trace* t : {&first, &second}) {
        for (std::size_t j = 0; j < pts.size(); ++j) {
            out << t->candidate->peak_count << ',' << baseline_name(t->candidate->baseline) << ','
                << pts[j].wavelength << ',' << pts[j].intensity << ',' << t->total[j] << ','
                << t->baseline[j] << ',' << t->residual[j];
            for (std::size_t k = 0; k < max_components; ++k) {
                out << ',';
                if (k < t->components.size()) out << t->components[k][j];
            }
            out << '\n';
        }
    }
}

void write_regional_improvement(const fs::path& path,
                                const std::vector<SpectrumPoint>& points,
                                const Trace& a, const Trace& b) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Cannot write " + path.string());
    out << "start_nm,end_nm,point_count,first_SSE,second_SSE,SSE_improvement,"
           "percentage_of_net_improvement\n";
    out << std::setprecision(12);
    const double total_gain = a.candidate->fit.sse - b.candidate->fit.sse;
    const double step_nm = 20.0;
    const double start = std::floor(points.front().wavelength / step_nm) * step_nm;
    const double end = points.back().wavelength;
    for (double lower = start; lower <= end; lower += step_nm) {
        const double upper = lower + step_nm;
        std::size_t count = 0;
        double sse_a = 0.0, sse_b = 0.0;
        for (std::size_t i = 0; i < points.size(); ++i) {
            if (points[i].wavelength >= lower && points[i].wavelength < upper) {
                ++count;
                sse_a += a.residual[i] * a.residual[i];
                sse_b += b.residual[i] * b.residual[i];
            }
        }
        if (count == 0) continue;
        const double gain = sse_a - sse_b;
        out << lower << ',' << upper << ',' << count << ',' << sse_a << ',' << sse_b
            << ',' << gain << ',';
        if (std::abs(total_gain) > 1e-12) out << 100.0 * gain / total_gain;
        out << '\n';
    }
}

void write_peaks(const fs::path& path, const Trace& a, const Trace& b) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Cannot write " + path.string());
    out << "N,baseline_type,peak_index,center_nm,FWHM_nm,peak_height,area\n";
    out << std::setprecision(14);
    for (const Trace* t : {&a, &b})
        for (std::size_t i = 0; i < t->candidate->fit.peaks.size(); ++i) {
            const auto& p = t->candidate->fit.peaks[i];
            out << t->candidate->peak_count << ',' << baseline_name(t->candidate->baseline)
                << ',' << (i + 1) << ',' << p.peak_position_nm << ',' << p.fwhm_nm
                << ',' << p.peak_height << ',' << p.peak_area << '\n';
        }
}

void write_metrics(const fs::path& path, const Trace& a, const Trace& b) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Cannot write " + path.string());
    out << "N,baseline_type,R2,RMSE,SSE,BIC,baseline_b0,baseline_b1,residual_lag1,"
           "min_gap_over_mean_fwhm,elapsed_ms\n";
    out << std::setprecision(14);
    for (const Trace* t : {&a, &b}) {
        const auto& m = *t->candidate;
        out << m.peak_count << ',' << baseline_name(m.baseline) << ',' << m.fit.r_squared
            << ',' << m.fit.rmse << ',' << m.fit.sse << ',' << m.bic << ',' << m.b0
            << ',' << m.b1 << ',' << m.residual_lag1 << ',' << m.min_gap_over_mean_fwhm
            << ',' << m.elapsed_ms << '\n';
    }
}

// Small dependency-free SVG renderer. Each spectrum panel shows the measured spectrum,
// total fit, fitted baseline and INDIVIDUAL Lorentz components shifted by baseline.
// CSV exports the unshifted component functions so numerical checks are unambiguous.
std::string polyline(const std::vector<double>& xs, const std::vector<double>& ys,
                     double xleft, double xright, double ytop, double ybottom,
                     double xmin, double xmax, double ymin, double ymax,
                     const std::string& stroke, double width, double opacity,
                     const std::string& dash = "") {
    std::ostringstream o;
    o << "<polyline fill=\"none\" stroke=\"" << stroke << "\" stroke-width=\""
      << width << "\" opacity=\"" << opacity << "\"";
    if (!dash.empty()) o << " stroke-dasharray=\"" << dash << "\"";
    o << " points=\"" << std::fixed << std::setprecision(2);
    for (std::size_t i = 0; i < xs.size(); ++i) {
        const double px = xleft + (xs[i] - xmin) / (xmax - xmin) * (xright - xleft);
        const double py = ybottom - (ys[i] - ymin) / (ymax - ymin) * (ybottom - ytop);
        if (std::isfinite(px) && std::isfinite(py)) o << px << ',' << py << ' ';
    }
    o << "\"/>\n";
    return o.str();
}

void line(std::ostream& out, double x1, double y1, double x2, double y2,
          const std::string& color, double width = 1, const std::string& dash = "") {
    out << "<line x1=\"" << x1 << "\" y1=\"" << y1 << "\" x2=\"" << x2
        << "\" y2=\"" << y2 << "\" stroke=\"" << color
        << "\" stroke-width=\"" << width << "\"";
    if (!dash.empty()) out << " stroke-dasharray=\"" << dash << "\"";
    out << "/>\n";
}
void label(std::ostream& out, double x, double y, const std::string& value,
           int font = 16, const std::string& color = "#243346",
           const std::string& anchor = "start") {
    out << "<text x=\"" << x << "\" y=\"" << y << "\" font-family=\"sans-serif\" "
        << "font-size=\"" << font << "\" fill=\"" << color
        << "\" text-anchor=\"" << anchor << "\">" << value << "</text>\n";
}

void write_svg(const fs::path& path, const std::vector<SpectrumPoint>& pts,
               const Trace& a, const Trace& b) {
    const double W = 1640, H = 1070;
    const double xmin = pts.front().wavelength, xmax = pts.back().wavelength;
    double ymin = std::numeric_limits<double>::infinity();
    double ymax = -std::numeric_limits<double>::infinity();
    double abs_residual = 1.0;
    for (const auto& p : pts) { ymin = std::min(ymin, p.intensity); ymax = std::max(ymax, p.intensity); }
    for (const Trace* t : {&a, &b}) {
        for (std::size_t i = 0; i < pts.size(); ++i) {
            ymin = std::min({ymin, t->total[i], t->baseline[i]});
            ymax = std::max(ymax, t->total[i]);
            for (const auto& comp : t->components) {
                const double v = t->baseline[i] + comp[i];
                ymin = std::min(ymin, v); ymax = std::max(ymax, v);
            }
            abs_residual = std::max(abs_residual, std::abs(t->residual[i]));
        }
    }
    const double span = std::max(ymax-ymin,1.0);
    ymin -= 0.06*span; ymax += 0.08*span;
    abs_residual *= 1.1;
    std::ofstream o(path);
    if (!o) throw std::runtime_error("Cannot write SVG: " + path.string());
    o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << W
      << "\" height=\"" << H << "\" viewBox=\"0 0 " << W << ' ' << H << "\">\n";
    o << "<rect width=\"100%\" height=\"100%\" fill=\"#ffffff\"/>\n";
    label(o, 60, 48, std::to_string(a.candidate->peak_count) + " vs " +
                     std::to_string(b.candidate->peak_count) +
                     " Lorentzian components | identical baseline model", 25);
    label(o, 60, 74, "Component lines show baseline + one Lorentzian; total fit is the sum of ALL components + baseline", 14, "#66758a");
    std::vector<double> xs; xs.reserve(pts.size());
    std::vector<double> measured; measured.reserve(pts.size());
    for (const auto& p : pts) { xs.push_back(p.wavelength); measured.push_back(p.intensity); }
    const std::array<std::string, 8> colors = {
      "#15a69c", "#a06fdb", "#cf8f13", "#d86980", "#578bcc", "#b87b57", "#779b37", "#8774aa"};
    for (int column = 0; column < 2; ++column) {
        const Trace& t = column == 0 ? a : b;
        const auto& m = *t.candidate;
        const double left = column == 0 ? 92 : 912;
        const double right = left + 630;
        const double top = 188, bottom = 670;
        const double rtop = 760, rbottom = 952;
        label(o, left, 125, "N=" + std::to_string(m.peak_count) + " | " + baseline_name(m.baseline) + " baseline", 22);
        label(o, left, 151, "R2 " + format(m.fit.r_squared,6) + "   RMSE " + format(m.fit.rmse,3) + "   BIC " + format(m.bic,2), 15);
        for (int tick=0; tick<=5; ++tick) {
            const double y = top + tick * (bottom-top)/5;
            line(o,left,y,right,y,"#edf1f5");
            label(o,left-8,y+5,format(ymax-tick*(ymax-ymin)/5,0),13,"#788697","end");
        }
        const int first_tick = static_cast<int>(std::ceil(xmin/50.0)*50.0);
        for (int tx=first_tick; tx<=xmax;tx+=50) {
            const double x = left+(tx-xmin)/(xmax-xmin)*(right-left);
            line(o,x,top,x,bottom,"#eef1f6");
            line(o,x,rtop,x,rbottom,"#eef1f6");
            label(o,x,978,std::to_string(tx),13,"#788697","middle");
        }
        line(o,left,top,left,bottom,"#a7b1bf");
        line(o,left,bottom,right,bottom,"#a7b1bf");
        line(o,left,rtop,left,rbottom,"#a7b1bf");
        line(o,left,rbottom,right,rbottom,"#a7b1bf");
        const double zero = (rbottom+rtop)*0.5;
        line(o,left,zero,right,zero,"#95a0ad",1.2,"5 4");
        label(o,left-8,rtop+4,"+"+format(abs_residual,0),12,"#788697","end");
        label(o,left-8,zero+4,"0",12,"#788697","end");
        label(o,left-8,rbottom+4,"-"+format(abs_residual,0),12,"#788697","end");
        label(o,left,743,"Residual (measured - fit)",15,"#44566c");
        label(o,(left+right)/2,1013,"Wavelength (nm)",16,"#44566c","middle");
        o << polyline(xs,measured,left,right,top,bottom,xmin,xmax,ymin,ymax,"#798594",1.0,0.82);
        o << polyline(xs,t.baseline,left,right,top,bottom,xmin,xmax,ymin,ymax,"#7f8790",1.2,0.9,"6 4");
        for (std::size_t i=0; i<t.components.size();++i) {
            std::vector<double> shifted(pts.size());
            for (std::size_t j=0;j<pts.size();++j)
                shifted[j] = t.baseline[j]+t.components[i][j];
            o << polyline(xs,shifted,left,right,top,bottom,xmin,xmax,ymin,ymax,
                          colors[i%colors.size()],1.65,0.84);
        }
        o << polyline(xs,t.total,left,right,top,bottom,xmin,xmax,ymin,ymax,"#212d43",2.4,1.0);
        o << polyline(xs,t.residual,left,right,rtop,rbottom,xmin,xmax,
                      -abs_residual,abs_residual,"#9b4b67",1.3,0.95);
        label(o,left,176,"Intensity (a.u.): measured / fit / components",14,"#4c5868");
        label(o,left,1042,"Measured = gray     Total = dark     Residual = magenta",13,"#687789");
    }
    o << "</svg>\n";
}

void print_model(const BaselineCandidate& m) {
    std::cout << "N=" << m.peak_count << " " << baseline_name(m.baseline)
              << "  R2=" << format(m.fit.r_squared,6)
              << " RMSE=" << format(m.fit.rmse)
              << " SSE=" << format(m.fit.sse,2)
              << " BIC=" << format(m.bic,3)
              << " b0=" << format(m.b0,3)
              << " overlap=" << format(m.min_gap_over_mean_fwhm,3) << '\n';
    for (std::size_t i=0;i<m.fit.peaks.size();++i) {
        const auto& p=m.fit.peaks[i];
        std::cout << "   #" << (i+1) << ": center=" << format(p.peak_position_nm,3)
                  << " nm, FWHM=" << format(p.fwhm_nm,3)
                  << " nm, height=" << format(p.peak_height,2) << '\n';
    }
}
} // namespace

int main(int argc, char** argv) {
    try {
        const Config cfg = parse(argc, argv);
        const auto spectrum = load_spectrum(cfg);
        BaselineComparisonOptions options;
        options.peak_options.max_peaks = std::max(cfg.n1,cfg.n2);
        const auto comparison = compare_baselines(spectrum, options);
        const auto& a = get_model(comparison, cfg.n1, cfg.baseline);
        const auto& b = get_model(comparison, cfg.n2, cfg.baseline);
        const auto ta = make_trace(spectrum, comparison, a);
        const auto tb = make_trace(spectrum, comparison, b);
        fs::create_directories(cfg.output);
        write_curves(cfg.output / "comparison_curves.csv", spectrum, ta, tb);
        write_peaks(cfg.output / "peak_parameters.csv", ta, tb);
        write_regional_improvement(cfg.output / "regional_improvement.csv", spectrum, ta, tb);
        write_metrics(cfg.output / "comparison_metrics.csv", ta, tb);
        write_svg(cfg.output / "comparison.svg", spectrum, ta, tb);
        std::cout << "Points: " << spectrum.size() << " | baseline: "
                  << baseline_name(cfg.baseline) << " | elapsed: "
                  << format(comparison.elapsed_ms,1) << " ms\n";
        print_model(a); print_model(b);
        const double gain=100.0*(a.fit.sse-b.fit.sse)/a.fit.sse;
        std::cout << "Second vs first: SSE improvement=" << format(gain,2)
                  << "% ; BIC(second-first)=" << format(b.bic-a.bic,3) << '\n';
        std::cout << "Outputs: " << cfg.output.string() << "/comparison.svg, "
                  << "comparison_curves.csv, peak_parameters.csv, comparison_metrics.csv, regional_improvement.csv\n";
        std::cout << "CAUTION: overlapping fitted components do not prove distinct physical resonances.\n";
        return 0;
    } catch (const std::exception& err) {
        std::cerr << "Error: " << err.what() << '\n';
        return 1;
    }
}
