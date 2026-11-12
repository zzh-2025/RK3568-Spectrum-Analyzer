#include "baseline_compare.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
constexpr double pi = 3.14159265358979323846;
constexpr double factor = 2.0 / pi;

double clamp(double v, double lo, double hi) { return std::max(lo, std::min(v, hi)); }

bool solve(std::vector<std::vector<double>> a, std::vector<double> b,
           std::vector<double>& result)
{
    const std::size_t n = b.size();
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t pivot = i;
        for (std::size_t j = i + 1; j < n; ++j)
            if (std::abs(a[j][i]) > std::abs(a[pivot][i])) pivot = j;
        if (!std::isfinite(a[pivot][i]) || std::abs(a[pivot][i]) < 1e-19)
            return false;
        std::swap(a[i], a[pivot]);
        std::swap(b[i], b[pivot]);
        const double val = a[i][i];
        for (std::size_t j = i; j < n; ++j) a[i][j] /= val;
        b[i] /= val;
        for (std::size_t r = 0; r < n; ++r) {
            if (r == i) continue;
            const double mult = a[r][i];
            if (mult == 0.0) continue;
            for (std::size_t c = i; c < n; ++c) a[r][c] -= mult * a[i][c];
            b[r] -= mult * b[i];
        }
    }
    result = std::move(b);
    return true;
}

struct SolverState {
    std::vector<double> q, scale;
    double xmid = 0.0, halfspan = 1.0;
    double xmin = 0.0, xmax = 1.0;
    double min_width = 0.0, max_width = 0.0, min_area = 0.0;
    double baseline_bound = 0.0;
    int n = 0;
    BaselineType baseline = BaselineType::None;
    std::size_t size() const { return q.size(); }

    void bound(std::vector<double>& v) const {
        for (int i = 0; i < n; ++i) {
            const int k = i * 3;
            v[k] = clamp(v[k], xmin / scale[k], xmax / scale[k]);
            v[k+1] = clamp(v[k+1], min_width / scale[k+1],
                           max_width / scale[k+1]);
            v[k+2] = std::max(v[k+2], min_area / scale[k+2]);
        }
        if (baseline != BaselineType::None) {
            v[3*n] = clamp(v[3*n], -baseline_bound / scale[3*n],
                                      baseline_bound / scale[3*n]);
            if (baseline == BaselineType::Linear)
                v[3*n+1] = clamp(v[3*n+1], -baseline_bound / scale[3*n+1],
                                                 baseline_bound / scale[3*n+1]);
        }
    }

    double predict(double x, const std::vector<double>& p) const {
        double y = 0.0;
        for (int i = 0; i < n; ++i) {
            const int k = 3*i;
            const double c = p[k] * scale[k];
            const double w = p[k+1] * scale[k+1];
            const double area = p[k+2] * scale[k+2];
            const double d = x - c;
            y += factor * area * w / (4*d*d + w*w);
        }
        if (baseline != BaselineType::None) y += p[3*n] * scale[3*n];
        if (baseline == BaselineType::Linear)
            y += p[3*n+1] * scale[3*n+1] * ((x - xmid) / halfspan);
        return y;
    }

    double sse(const std::vector<SpectrumPoint>& spectrum,
               const std::vector<double>& p) const {
        double value = 0.0;
        for (const auto& s : spectrum) {
            const double residual = s.intensity - predict(s.wavelength, p);
            value += residual*residual;
        }
        return value;
    }
};

SolverState make_state(const std::vector<SpectrumPoint>& sorted,
                       const MultiLorentzFitResult& seed,
                       BaselineType type, double b0, double b1)
{
    SolverState s;
    s.n = static_cast<int>(seed.peaks.size());
    s.baseline = type;
    s.xmin = sorted.front().wavelength;
    s.xmax = sorted.back().wavelength;
    const double span = s.xmax - s.xmin;
    s.xmid = 0.5 * (s.xmin + s.xmax);
    s.halfspan = 0.5 * span;
    const double max_abs_y = std::accumulate(
        sorted.begin(), sorted.end(), 0.0,
        [](double max_y, const SpectrumPoint& p) {
            return std::max(max_y, std::abs(p.intensity));
        });
    const double intensity_scale = std::max(1.0, max_abs_y);
    const double typical_width = span / std::max(1.0, 1.25 * s.n);
    const double area_scale = intensity_scale * typical_width;
    const int extras = (type == BaselineType::None) ? 0 :
                       (type == BaselineType::Constant ? 1 : 2);
    const int parameter_count = 3*s.n + extras;
    s.q.resize(parameter_count, 0.0);
    s.scale.resize(parameter_count, 1.0);
    s.min_width = std::max(span / sorted.size() * 0.5, 1e-4);
    s.max_width = 2 * span;
    s.min_area = std::max(1e-12, area_scale * 1e-12);
    s.baseline_bound = 2.0 * intensity_scale;
    for (int i = 0; i < s.n; ++i) {
        const int k = 3*i;
        const auto& p = seed.peaks.at(static_cast<std::size_t>(i));
        s.scale[k] = span;
        s.scale[k+1] = typical_width;
        s.scale[k+2] = area_scale;
        s.q[k] = p.peak_position_nm / s.scale[k];
        s.q[k+1] = p.fwhm_nm / s.scale[k+1];
        s.q[k+2] = p.peak_area / s.scale[k+2];
    }
    if (type != BaselineType::None) {
        s.scale[3*s.n] = intensity_scale;
        s.q[3*s.n] = b0 / intensity_scale;
        if (type == BaselineType::Linear) {
            s.scale[3*s.n+1] = intensity_scale;
            s.q[3*s.n+1] = b1 / intensity_scale;
        }
    }
    s.bound(s.q);
    return s;
}

// Linear-regression initial background on residuals of NO-baseline seed.
std::pair<double, double> regression_start(const std::vector<SpectrumPoint>& sorted,
                                            const MultiLorentzFitResult& seed)
{
    const double mid = 0.5 * (sorted.front().wavelength + sorted.back().wavelength);
    const double halfspan = 0.5 * (sorted.back().wavelength - sorted.front().wavelength);
    double sum_t = 0.0, sum_tt = 0.0, sum_r = 0.0, sum_tr = 0.0;
    const double n = static_cast<double>(sorted.size());
    for (const auto& pt : sorted) {
        const double t = (pt.wavelength - mid) / halfspan;
        const double r = pt.intensity - multi_lorentz(pt.wavelength, seed.peaks);
        sum_t += t; sum_tt += t*t; sum_r += r; sum_tr += t*r;
    }
    const double denom = n * sum_tt - sum_t * sum_t;
    const double b1 = denom > 1e-20 ? (n * sum_tr - sum_t * sum_r) / denom : 0.0;
    return {(sum_r - b1*sum_t) / n, b1};
}

BaselineCandidate optimize(const std::vector<SpectrumPoint>& sorted,
                            const MultiLorentzFitResult& seed,
                            BaselineType type, int max_iterations,
                            double initial_b0, double initial_b1)
{
    BaselineCandidate out;
    out.peak_count = static_cast<int>(seed.peaks.size());
    out.baseline = type;
    out.parameter_count = 3*out.peak_count + (type == BaselineType::None ? 0 :
                                             (type == BaselineType::Constant ? 1 : 2));
    SolverState state = make_state(sorted, seed, type, initial_b0, initial_b1);
    std::vector<double> q = state.q;
    const std::size_t pcount = state.size();
    double sse = state.sse(sorted, q);
    double lambda = 1e-3;
    bool converged = false;
    int iterations = 0;
    for (int iteration = 0; iteration < max_iterations; ++iteration) {
        iterations = iteration + 1;
        std::vector<std::vector<double>> hess(pcount, std::vector<double>(pcount, 0.0));
        std::vector<double> grad(pcount, 0.0), jac(pcount, 0.0);
        for (const auto& pt : sorted) {
            const double x = pt.wavelength;
            double pred = 0.0;
            for (int i = 0; i < state.n; ++i) {
                const int k = i*3;
                const double xc = q[k] * state.scale[k];
                const double w = q[k+1] * state.scale[k+1];
                const double a = q[k+2] * state.scale[k+2];
                const double d = x-xc;
                const double denom = 4*d*d + w*w;
                const double dsq = denom*denom;
                pred += factor*a*w/denom;
                jac[k] = factor*a*w*8*d/dsq*state.scale[k];
                jac[k+1] = factor*a*(4*d*d - w*w)/dsq*state.scale[k+1];
                jac[k+2] = factor*w/denom*state.scale[k+2];
            }
            if (type != BaselineType::None) {
                pred += q[3*state.n] * state.scale[3*state.n];
                jac[3*state.n] = state.scale[3*state.n];
            }
            if (type == BaselineType::Linear) {
                const double t = (x - state.xmid) / state.halfspan;
                pred += q[3*state.n+1] * state.scale[3*state.n+1] * t;
                jac[3*state.n+1] = state.scale[3*state.n+1] * t;
            }
            const double residual = pt.intensity - pred;
            for (std::size_t i = 0; i < pcount; ++i) {
                grad[i] += jac[i]*residual;
                for (std::size_t j = i; j < pcount; ++j)
                    hess[i][j] += jac[i]*jac[j];
            }
        }
        for (std::size_t i = 0; i < pcount; ++i)
            for (std::size_t j = i+1; j < pcount; ++j)
                hess[j][i] = hess[i][j];

        std::vector<std::vector<double>> damped = hess;
        for (std::size_t i = 0; i < pcount; ++i)
            damped[i][i] += lambda * std::max(hess[i][i], 1e-12);
        std::vector<double> delta;
        if (!solve(damped, grad, delta)) {
            lambda *= 10.0;
            if (lambda > 1e15) break;
            continue;
        }
        auto proposal = q;
        double norm = 0.0;
        for (std::size_t i = 0; i < pcount; ++i) {
            proposal[i] += delta[i];
            norm += delta[i]*delta[i];
        }
        state.bound(proposal);
        const double trial = state.sse(sorted, proposal);
        if (std::isfinite(trial) && trial < sse) {
            const double gain = sse - trial;
            q = std::move(proposal);
            sse = trial;
            lambda = std::max(lambda/3.0, 1e-12);
            if (std::sqrt(norm) < 1e-8 || gain < 1e-11*(1.0+sse)) {
                converged = true;
                break;
            }
        } else {
            // At a numerical stationary point, an appropriately tiny LM step
            // with unchanged objective is convergence, not automatic failure.
            if (std::sqrt(norm) < 1e-9 && std::isfinite(trial) &&
                trial <= sse * (1.0 + 1e-13)) {
                converged = true;
                break;
            }
            lambda *= 10.0;
            if (lambda > 1e15) break;
        }
    }
    out.converged = converged;
    out.fit.iterations = iterations;
    out.fit.converged = converged;
    out.fit.sse = sse;
    out.fit.rmse = std::sqrt(sse/static_cast<double>(sorted.size()));
    const double mean = std::accumulate(sorted.begin(), sorted.end(), 0.0,
        [](double acc, const SpectrumPoint& v) { return acc + v.intensity; }) / sorted.size();
    double ss_tot = 0.0;
    for (const auto& v : sorted) {
        const double d = v.intensity - mean;
        ss_tot += d*d;
    }
    out.fit.r_squared = ss_tot > 0 ? 1.0 - sse/ss_tot :
                        std::numeric_limits<double>::quiet_NaN();
    out.fit.peaks.reserve(static_cast<std::size_t>(state.n));
    for (int i = 0; i < state.n; ++i) {
        const int k = i*3;
        const double xc = q[k]*state.scale[k];
        const double w = q[k+1]*state.scale[k+1];
        const double a = q[k+2]*state.scale[k+2];
        out.fit.peaks.push_back({xc,w,2*a/(pi*w),a});
    }
    std::sort(out.fit.peaks.begin(), out.fit.peaks.end(),
              [](const MultiLorentzPeak& a, const MultiLorentzPeak& b) {
                  return a.peak_position_nm < b.peak_position_nm;
              });
    if (type != BaselineType::None) out.b0 = q[3*state.n]*state.scale[3*state.n];
    if (type == BaselineType::Linear) out.b1 = q[3*state.n+1]*state.scale[3*state.n+1];
    return out;
}

std::string check_fit(const BaselineCandidate& c,
                      const std::vector<SpectrumPoint>& sorted,
                      const ModelSelectionOptions& options)
{
    if (!c.converged) return "not converged";
    if (c.fit.peaks.size() != static_cast<std::size_t>(c.peak_count) ||
        !std::isfinite(c.fit.sse) || c.fit.sse < 0 || !std::isfinite(c.fit.rmse))
        return "invalid fit";
    const double xmin = sorted.front().wavelength, xmax = sorted.back().wavelength;
    const double span = xmax - xmin;
    const double step = span / (sorted.size()-1);
    double max_y = 0.0;
    for (const auto& pt : sorted) max_y = std::max(max_y, std::abs(pt.intensity));
    for (std::size_t i = 0; i < c.fit.peaks.size(); ++i) {
        const auto& p = c.fit.peaks[i];
        if (!std::isfinite(p.peak_position_nm) || !std::isfinite(p.fwhm_nm) ||
            !std::isfinite(p.peak_height) || !std::isfinite(p.peak_area))
            return "non-finite peak";
        if (p.fwhm_nm < options.min_width_steps*step ||
            p.fwhm_nm > options.max_width_span_fraction*span)
            return "unresolved or excessive width";
        if (p.peak_height <= options.min_height_fraction*max_y || p.peak_area <= 0.0)
            return "negligible or negative peak";
        if (p.peak_position_nm <= xmin + options.edge_margin_steps*step ||
            p.peak_position_nm >= xmax - options.edge_margin_steps*step)
            return "peak at boundary";
    }
    for (std::size_t i = 1; i < c.fit.peaks.size(); ++i)
        if (c.fit.peaks[i].peak_position_nm - c.fit.peaks[i-1].peak_position_nm <
            options.min_separation_steps*step)
            return "unresolved peak centers";
    return {};
}

void add_diagnostics(BaselineCandidate& c, const std::vector<SpectrumPoint>& sorted,
                     double mid, double halfspan)
{
    const std::size_t n = sorted.size();
    std::vector<double> r;
    r.reserve(n);
    double mean = 0.0;
    for (const auto& v : sorted) {
        const double predicted = multi_lorentz(v.wavelength, c.fit.peaks) +
                                 baseline_value(v.wavelength,mid,halfspan,c.baseline,c.b0,c.b1);
        r.push_back(v.intensity - predicted);
        mean += r.back();
    }
    mean /= n;
    double variance = 0.0, cov = 0.0;
    for (std::size_t i=0; i<n; ++i) {
        const double di = r[i]-mean;
        variance += di*di;
        if (i>0) cov += di*(r[i-1]-mean);
    }
    if (variance > 1e-20) c.residual_lag1 = cov / variance;
    const std::size_t edge = std::max<std::size_t>(1,n/20);
    double left=0.0,right=0.0;
    for (std::size_t i=0;i<edge;++i) {
        left += r[i]; right += r[n-1-i];
    }
    if (c.fit.rmse>0) {
        c.edge_left_rmse = left / (edge*c.fit.rmse);
        c.edge_right_rmse = right / (edge*c.fit.rmse);
    }
    if (c.baseline != BaselineType::None) {
        double max_signal = 0.0;
        for (const auto& pt : sorted)
            max_signal = std::max(max_signal, std::abs(pt.intensity));
        c.substantially_negative_baseline =
            std::min(c.b0 - c.b1, c.b0 + c.b1) < -0.02 * max_signal;
    }
    for (std::size_t i=1; i<c.fit.peaks.size(); ++i) {
        const auto& a = c.fit.peaks[i-1];
        const auto& b = c.fit.peaks[i];
        const double ratio = (b.peak_position_nm-a.peak_position_nm)/
                             (0.5*(a.fwhm_nm+b.fwhm_nm));
        if (!std::isfinite(c.min_gap_over_mean_fwhm) ||
            ratio<c.min_gap_over_mean_fwhm)
            c.min_gap_over_mean_fwhm=ratio;
    }
}

void add_information_criteria(BaselineCandidate& c, std::size_t samples)
{
    if (!std::isfinite(c.fit.sse) || c.fit.sse < 0) return;
    const double n = static_cast<double>(samples);
    const double k = c.parameter_count;
    const double base = n*std::log(std::max(c.fit.sse,
                                std::numeric_limits<double>::min())/n);
    c.aic=base+2.0*k;
    c.bic=base+k*std::log(n);
}
} // namespace

const char* baseline_name(BaselineType baseline)
{
    switch (baseline) {
        case BaselineType::None: return "none";
        case BaselineType::Constant: return "constant";
        case BaselineType::Linear: return "linear";
    }
    return "unknown";
}

double baseline_value(double x, double mid, double halfspan, BaselineType type,
                      double b0, double b1)
{
    if (type == BaselineType::None) return 0.0;
    if (type == BaselineType::Constant) return b0;
    return b0 + b1*(x-mid)/halfspan;
}

BaselineComparisonResult compare_baselines(
    const std::vector<SpectrumPoint>& spectrum,
    const BaselineComparisonOptions& options)
{
    if (options.refinement_iterations < 1)
        throw std::invalid_argument("refinement_iterations must be >= 1");
    std::vector<SpectrumPoint> sorted = spectrum;
    std::sort(sorted.begin(), sorted.end(), [](const SpectrumPoint& a,
                                               const SpectrumPoint& b) {
        return a.wavelength < b.wavelength;
    });
    if (sorted.size() < 15) throw std::invalid_argument("Not enough spectrum points");
    BaselineComparisonResult result;
    const auto start=std::chrono::steady_clock::now();
    result.xmid_nm=0.5*(sorted.front().wavelength+sorted.back().wavelength);
    result.halfspan_nm=0.5*(sorted.back().wavelength-sorted.front().wavelength);
    // Stage 4 result is read-only input here: old solver and CLI are unchanged.
    const auto base=select_peak_count(sorted,options.peak_options);
    result.models.reserve(base.candidates.size()*3);
    for (const auto& original : base.candidates) {
        BaselineCandidate no;
        no.peak_count=original.peak_count;
        no.baseline=BaselineType::None;
        no.parameter_count=3*no.peak_count;
        no.fit=original.run.fit;
        no.converged=original.converged;
        no.acceptable=original.acceptable;
        no.note=original.note;
        no.elapsed_ms=original.elapsed_ms;
        add_information_criteria(no,sorted.size());
        if (std::isfinite(no.fit.sse) && !no.fit.peaks.empty())
            add_diagnostics(no,sorted,result.xmid_nm,result.halfspan_nm);
        result.models.push_back(no);

        for (BaselineType type : {BaselineType::Constant, BaselineType::Linear}) {
            BaselineCandidate best;
            best.peak_count=original.peak_count;
            best.baseline=type;
            best.parameter_count=3*best.peak_count+(type==BaselineType::Constant?1:2);
            const auto t0=std::chrono::steady_clock::now();
            if (original.run.fit.peaks.empty() || !std::isfinite(original.run.fit.sse)) {
                best.note="no valid no-baseline initial seed";
            } else {
                const auto regression=regression_start(sorted,original.run.fit);
                const int starters=(type==BaselineType::Constant?2:3);
                for (int initial=0;initial<starters;++initial) {
                    const double b0=initial==0?0.0:regression.first;
                    const double b1=(type==BaselineType::Linear && initial==2)?
                                        regression.second:0.0;
                    auto trial=optimize(sorted,original.run.fit,type,
                                        options.refinement_iterations,b0,b1);
                    trial.note=check_fit(trial,sorted,options.peak_options);
                    trial.acceptable=trial.note.empty();
                    if (initial==0 || (trial.acceptable && !best.acceptable) ||
                        (trial.acceptable==best.acceptable &&
                         trial.fit.sse<best.fit.sse)) {
                        best=std::move(trial);
                    }
                }
            }
            best.elapsed_ms=std::chrono::duration<double,std::milli>(
                std::chrono::steady_clock::now()-t0).count();
            add_information_criteria(best,sorted.size());
            if (std::isfinite(best.fit.sse) && !best.fit.peaks.empty())
                add_diagnostics(best,sorted,result.xmid_nm,result.halfspan_nm);
            result.models.push_back(std::move(best));
        }
    }
    std::vector<int> valid;
    for (int i=0;i<static_cast<int>(result.models.size());++i)
        if (result.models[i].acceptable && std::isfinite(result.models[i].bic))
            valid.push_back(i);
    std::sort(valid.begin(),valid.end(),[&](int a,int b) {
        const auto& x=result.models[a]; const auto& y=result.models[b];
        if (x.bic!=y.bic) return x.bic<y.bic;
        if (x.peak_count!=y.peak_count) return x.peak_count<y.peak_count;
        return static_cast<int>(x.baseline)<static_cast<int>(y.baseline);
    });
    if (!valid.empty()) {
        result.best_index=valid.front();
        if (valid.size()>1) result.second_index=valid[1];
        for (auto& m : result.models)
            if (std::isfinite(m.bic)) m.delta_bic=m.bic-result.models[result.best_index].bic;
        const auto& m=result.models[result.best_index];
        const auto& op=options.peak_options;
        result.requires_review=m.peak_count==op.max_peaks ||
            m.substantially_negative_baseline ||
            (std::isfinite(m.residual_lag1)&&std::abs(m.residual_lag1)>=op.residual_correlation_review)||
            (std::isfinite(m.edge_left_rmse)&&std::abs(m.edge_left_rmse)>=op.edge_bias_review_rmse)||
            (std::isfinite(m.edge_right_rmse)&&std::abs(m.edge_right_rmse)>=op.edge_bias_review_rmse)||
            (std::isfinite(m.min_gap_over_mean_fwhm)&&
             m.min_gap_over_mean_fwhm<op.overlap_review_ratio);
        result.explanation="BIC comparison across N and baseline types. "
            "This is exploratory: baseline optimizations use only the no-baseline "
            "peak solution as their starting point; local minima are possible. "
            "Correlated residuals can undermine the standard BIC assumptions. "
            "A negative fitted baseline should be checked against dark subtraction "
            "and background measurements.";
    } else {
        result.explanation="No converged, numerically acceptable fit; review manually.";
    }
    result.elapsed_ms=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-start).count();
    return result;
}
