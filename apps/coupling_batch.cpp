#include "baseline_compare.h"
#include "coupling_analysis.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace {
struct Options {
    fs::path input_dir = "data";
    fs::path results_root = "results";
    int min_peaks = 4, max_peaks = 4;
    int max_iterations = 350;
    BaselineType baseline = BaselineType::Constant;
    bool automatic_baseline = false;
    CouplingPairSelection selection;
    bool use_wavelength_cut = false;
    double wavelength_low = 0.0, wavelength_high = 0.0;
    double review_r2_below = 0.90;
};

std::string csv(const std::string& input) {
    std::string result = "\"";
    for (char c : input) { if (c == '"') result += "\"\""; else result += c; }
    return result + '"';
}
std::string num(double x) {
    if (!std::isfinite(x)) return "";
    std::ostringstream o; o.imbue(std::locale::classic());
    o << std::setprecision(12) << x;
    return o.str();
}
void tag(std::string& s, const std::string& v) {
    if (!s.empty()) s += ";";
    s += v;
}
int arg_int(const char* text, int low, int high) {
    const std::string s(text);
    std::size_t idx=0;
    const int value=std::stoi(s,&idx);
    if (idx != s.size() || value < low || value > high)
        throw std::invalid_argument("Invalid integer: " + s);
    return value;
}
double arg_double(const char* text) {
    const std::string s(text);
    std::size_t idx=0;
    const double value=std::stod(s,&idx);
    if (idx != s.size() || !std::isfinite(value))
        throw std::invalid_argument("Invalid number: " + s);
    return value;
}
std::string lower(std::string s) {
    std::transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return s;
}
Options parse(int argc, char** argv) {
    if (argc < 2) throw std::invalid_argument(
        "Usage: coupling_batch <ASC_folder> [--out results] [--peaks 4|auto] "
        "[--max-peaks 5] [--baseline none|constant|linear|auto] "
        "[--pair-index 2 4 | --up-target 612 --lp-target 705 | "
        "--up-window 590 660 --lp-window 680 750] [--range 450 750] "
        "[--max-iterations 350]");
    Options cfg;
    cfg.input_dir=argv[1];
    bool set_up_target=false,set_lp_target=false,set_up_window=false,set_lp_window=false,set_pair=false;
    for (int i=2;i<argc;++i) {
        const std::string a=argv[i];
        if (a=="--out" && i+1<argc) cfg.results_root=argv[++i];
        else if (a=="--peaks" && i+1<argc) {
            const std::string value=argv[++i];
            if (value=="auto") {cfg.min_peaks=1;cfg.max_peaks=5;}
            else {cfg.min_peaks=cfg.max_peaks=arg_int(value.c_str(),1,8);}
        }
        else if (a=="--max-peaks" && i+1<argc) {
            cfg.max_peaks=arg_int(argv[++i],2,8);
            cfg.min_peaks=1;
        }
        else if (a=="--max-iterations" && i+1<argc)
            cfg.max_iterations=arg_int(argv[++i],20,5000);
        else if (a=="--baseline" && i+1<argc) {
            const std::string value=argv[++i];
            cfg.automatic_baseline = value=="auto";
            if (value=="constant" || value=="auto") cfg.baseline=BaselineType::Constant;
            else if (value=="none") cfg.baseline=BaselineType::None;
            else if (value=="linear") cfg.baseline=BaselineType::Linear;
            else throw std::invalid_argument("Unknown baseline mode: "+value);
        }
        else if (a=="--pair-index" && i+2<argc) {
            cfg.selection.mode=CouplingPairSelection::Mode::Indices;
            cfg.selection.index_a=arg_int(argv[++i],1,8);
            cfg.selection.index_b=arg_int(argv[++i],1,8);
            set_pair=true;
        }
        else if (a=="--up-target" && i+1<argc) {
            cfg.selection.up_target_nm=arg_double(argv[++i]); set_up_target=true;
        }
        else if (a=="--lp-target" && i+1<argc) {
            cfg.selection.lp_target_nm=arg_double(argv[++i]); set_lp_target=true;
        }
        else if (a=="--up-window" && i+2<argc) {
            cfg.selection.up_min_nm=arg_double(argv[++i]);
            cfg.selection.up_max_nm=arg_double(argv[++i]); set_up_window=true;
        }
        else if (a=="--lp-window" && i+2<argc) {
            cfg.selection.lp_min_nm=arg_double(argv[++i]);
            cfg.selection.lp_max_nm=arg_double(argv[++i]); set_lp_window=true;
        }
        else if (a=="--range" && i+2<argc) {
            cfg.wavelength_low=arg_double(argv[++i]);
            cfg.wavelength_high=arg_double(argv[++i]);
            cfg.use_wavelength_cut=true;
            if (!(cfg.wavelength_low<cfg.wavelength_high))
                throw std::invalid_argument("Range lower must be < upper");
        }
        else if (a=="--min-r2" && i+1<argc) {
            cfg.review_r2_below=arg_double(argv[++i]);
            if (cfg.review_r2_below<0 || cfg.review_r2_below>1)
                throw std::invalid_argument("--min-r2 must be within [0,1]");
        }
        else throw std::invalid_argument("Unrecognized/incomplete argument: " + a);
    }
    const bool target=set_up_target||set_lp_target;
    const bool windows=set_up_window||set_lp_window;
    if ((target && (set_pair||windows)) || (windows && set_pair) ||
        (target && !(set_up_target&&set_lp_target)) ||
        (windows && !(set_up_window&&set_lp_window)))
        throw std::invalid_argument("Specify one complete pair-selection mode only");
    if (target) cfg.selection.mode=CouplingPairSelection::Mode::Targets;
    if (windows) cfg.selection.mode=CouplingPairSelection::Mode::Windows;
    if (cfg.selection.mode==CouplingPairSelection::Mode::Targets &&
        !(cfg.selection.up_target_nm>0 && cfg.selection.up_target_nm<cfg.selection.lp_target_nm))
        throw std::invalid_argument("Targets must satisfy 0 < UP < LP (nm)");
    if (cfg.selection.mode==CouplingPairSelection::Mode::Windows &&
        !(cfg.selection.up_min_nm<cfg.selection.up_max_nm &&
          cfg.selection.lp_min_nm<cfg.selection.lp_max_nm))
        throw std::invalid_argument("Both peak windows must have lower < upper");
    return cfg;
}
std::vector<SpectrumPoint> read_asc(const fs::path& path, const Options& cfg) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open ASC");
    std::vector<SpectrumPoint> pts;
    std::string line;
    while (std::getline(f,line)) {
        for (char& c : line) if (c==',' || c==';' || c=='\t') c=' ';
        std::istringstream in(line);
        double x,y;
        if (in>>x>>y && std::isfinite(x)&&std::isfinite(y) &&
            (!cfg.use_wavelength_cut || (x>=cfg.wavelength_low&&x<=cfg.wavelength_high)))
            pts.push_back({x,y});
    }
    std::sort(pts.begin(),pts.end(),[](const SpectrumPoint& a,const SpectrumPoint& b){
        return a.wavelength<b.wavelength;
    });
    if (pts.size()<15) throw std::runtime_error("Fewer than 15 valid points");
    for (std::size_t i=1;i<pts.size();++i) if (pts[i].wavelength<=pts[i-1].wavelength)
        throw std::runtime_error("Duplicate/non-increasing wavelengths");
    return pts;
}
std::string timestamp() {
    const auto now=std::chrono::system_clock::now();
    const std::time_t time=std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&time,&tm);
    std::ostringstream o; o<<std::put_time(&tm,"%Y%m%d_%H%M%S");
    return o.str();
}
std::string id_for(int index) {
    std::ostringstream o; o<<std::setw(4)<<std::setfill('0')<<index;
    return o.str();
}
std::string model_warning(const BaselineCandidate& m, const Options& cfg) {
    std::string warn;
    if (m.fit.r_squared<cfg.review_r2_below) tag(warn,"LOW_R2");
    if (m.substantially_negative_baseline) tag(warn,"NEGATIVE_FITTED_BASELINE");
    if (std::isfinite(m.residual_lag1) && std::abs(m.residual_lag1)>0.25)
        tag(warn,"CORRELATED_RESIDUAL");
    if (std::isfinite(m.min_gap_over_mean_fwhm) && m.min_gap_over_mean_fwhm<0.60)
        tag(warn,"OVERLAPPING_COMPONENTS");
    return warn;
}
const BaselineCandidate* chosen_model(const BaselineComparisonResult& result,
                                      const Options& cfg) {
    const BaselineCandidate* winner=nullptr;
    for (const auto& m : result.models) {
        if (!m.acceptable || !std::isfinite(m.bic)) continue;
        if (!cfg.automatic_baseline && m.baseline!=cfg.baseline) continue;
        if (winner==nullptr || m.bic<winner->bic) winner=&m;
    }
    return winner;
}
void write_curves(const fs::path& path, const std::vector<SpectrumPoint>& pts,
                  const BaselineComparisonResult& comp, const BaselineCandidate& m) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Cannot write curves file");
    out<<"wavelength_nm,measured,fit_total,fit_baseline,residual";
    for (std::size_t p=0;p<m.fit.peaks.size();++p) out<<",component_"<<(p+1);
    out<<'\n'<<std::setprecision(12);
    for (const auto& pt:pts) {
        const double base=baseline_value(pt.wavelength,comp.xmid_nm,comp.halfspan_nm,
                                         m.baseline,m.b0,m.b1);
        const double fit=base+multi_lorentz(pt.wavelength,m.fit.peaks);
        out<<num(pt.wavelength)<<','<<num(pt.intensity)<<','<<num(fit)<<','<<num(base)
           <<','<<num(pt.intensity-fit);
        for (const auto& peak:m.fit.peaks)
            out<<','<<num(lorentz(pt.wavelength,peak.peak_position_nm,
                                 peak.fwhm_nm,peak.peak_area));
        out<<'\n';
    }
    if (!out) throw std::runtime_error("Failed writing curves file");
}
void write_summary_header(std::ostream& out) {
    out<<"id,filename,status,points,n,baseline,r2,rmse,sse,bic,b0,b1,";
    out<<"up_index,lp_index,up_nm,lp_nm,up_fwhm_nm,lp_fwhm_nm,";
    out<<"up_e_ev,lp_e_ev,splitting_mev,apparent_g_mev,up_gamma_mev,";
    out<<"lp_gamma_mev,avg_gamma_mev,splitting_width_ratio,linewidth_criterion,";
    out<<"fit_time_ms,warnings,error\n";
}
void write_row(std::ostream& out, const std::string& id, const std::string& filename,
               const std::string& status, std::size_t size,
               const BaselineCandidate* m, const CouplingPairMetrics& pair,
               double elapsed_ms, const std::string& warnings, const std::string& error) {
    out<<csv(id)<<','<<csv(filename)<<','<<status<<','<<size<<',';
    if (m) out<<m->peak_count<<','<<baseline_name(m->baseline)<<','
              <<num(m->fit.r_squared)<<','<<num(m->fit.rmse)<<','<<num(m->fit.sse)
              <<','<<num(m->bic)<<','<<num(m->b0)<<','<<num(m->b1);
    else { for (int k=0; k<8; ++k) { if (k) out << ','; } } // n through b1: 8 columns
    out<<',';
    if (pair.valid) {
        out<<pair.up_index<<','<<pair.lp_index<<','<<num(pair.up_nm)<<','<<num(pair.lp_nm)
           <<','<<num(pair.up_fwhm_nm)<<','<<num(pair.lp_fwhm_nm)
           <<','<<num(pair.up_e_ev)<<','<<num(pair.lp_e_ev)
           <<','<<num(pair.splitting_mev)<<','<<num(pair.apparent_g_mev)
           <<','<<num(pair.up_gamma_mev)<<','<<num(pair.lp_gamma_mev)
           <<','<<num(pair.avg_gamma_mev)<<','<<num(pair.splitting_to_width_ratio)
           <<','<<(pair.linewidth_criterion_pass?"PASS":"FAIL");
    } else {
        for (int k=0; k<15; ++k) { if (k) out << ','; } // 15 empty pair fields
    }
    out<<','<<num(elapsed_ms)<<','<<csv(warnings)<<','<<csv(error)<<'\n';
    if (!out) throw std::runtime_error("Cannot write summary CSV");
}
void write_peaks(std::ostream& out,const std::string& id,const std::string& filename,
                 const BaselineCandidate& m) {
    for (std::size_t i=0;i<m.fit.peaks.size();++i) {
        const auto& p=m.fit.peaks[i];
        out<<csv(id)<<','<<csv(filename)<<','<<(i+1)<<','<<num(p.peak_position_nm)
           <<','<<num(p.fwhm_nm)<<','<<num(p.peak_height)<<','<<num(p.peak_area)<<'\n';
    }
    if (!out) throw std::runtime_error("Cannot write all_peaks CSV");
}
} // namespace

int main(int argc, char** argv) {
    try {
        const Options cfg=parse(argc,argv);
        if (!fs::is_directory(cfg.input_dir))
            throw std::runtime_error("ASC input folder does not exist");
        std::vector<fs::path> files;
        for (const auto& item:fs::directory_iterator(cfg.input_dir)) {
            if (item.is_regular_file() && lower(item.path().extension().string())==".asc")
                files.push_back(item.path());
        }
        std::sort(files.begin(),files.end());
        if (files.empty()) throw std::runtime_error("No ASC files found");
        fs::create_directories(cfg.results_root);
        fs::path batch=cfg.results_root/("coupling_batch_"+timestamp());
        for (int n=1; fs::exists(batch); ++n)
            batch=cfg.results_root/("coupling_batch_"+timestamp()+"_"+std::to_string(n));
        fs::create_directories(batch/"curves");
        std::ofstream summary(batch/"summary.csv"), peaks(batch/"all_peaks.csv"),log(batch/"run_log.txt");
        if (!summary || !peaks || !log)
            throw std::runtime_error("Cannot open output files");
        write_summary_header(summary);
        peaks<<"id,filename,peak_index,center_nm,fwhm_nm,height,area\n";
        int ok=0, review=0, fail=0;
        std::cout<<"Coupling batch analysis | files="<<files.size()
                 <<" | peaks="<<cfg.min_peaks<<".."<<cfg.max_peaks
                 <<" | baseline="<<(cfg.automatic_baseline?"auto":baseline_name(cfg.baseline))<<'\n';
        for (std::size_t i=0;i<files.size();++i) {
            const auto& file=files[i];
            const std::string id=id_for(static_cast<int>(i+1));
            const auto start=std::chrono::steady_clock::now();
            std::size_t points_count=0;
            CouplingPairMetrics pair;
            const BaselineCandidate* model=nullptr;
            std::string warning,error,status="FIT_FAILED";
            try {
                const auto data=read_asc(file,cfg);
                points_count=data.size();
                BaselineComparisonOptions opts;
                opts.peak_options.min_peaks=cfg.min_peaks;
                opts.peak_options.max_peaks=cfg.max_peaks;
                opts.peak_options.max_iterations=cfg.max_iterations;
                const auto comp=compare_baselines(data,opts);
                model=chosen_model(comp,cfg);
                if (!model) {
                    status="FIT_FAILED";
                    error="No acceptable model for selected peak count/baseline";
                } else {
                    // IMPORTANT: comp and model are alive until writing the row below.
                    pair=calculate_coupling_pair(model->fit.peaks,cfg.selection);
                    warning=model_warning(*model,cfg);
                    if (cfg.min_peaks!=cfg.max_peaks && model->peak_count==cfg.max_peaks)
                        tag(warning,"N_AT_SEARCH_LIMIT");
                    if (!pair.warning.empty()) tag(warning,pair.warning);
                    if (!pair.valid) {
                        status="NO_PAIR";
                        tag(warning,"COUPLING_NOT_COMPUTED");
                    } else {
                        status=warning.empty()?"OK":"REVIEW";
                    }
                    write_curves(batch/"curves"/(id+"_fit.csv"),data,comp,*model);
                    write_peaks(peaks,id,file.filename().string(),*model);
                }
                const double elapsed=std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-start).count();
                write_row(summary,id,file.filename().string(),status,points_count,
                          model,pair,elapsed,warning,error);
                // Model pointer will not be used beyond the lifetime of `comp`.
            } catch (const std::exception& e) {
                status="INPUT_ERROR"; error=e.what();
                const double elapsed=std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-start).count();
                write_row(summary,id,file.filename().string(),status,points_count,
                          nullptr,CouplingPairMetrics{},elapsed,"",error);
            }
            if (status=="OK") ++ok;
            else if (status=="REVIEW") ++review;
            else ++fail;
            std::cout<<'['<<(i+1)<<'/'<<files.size()<<"] "<<file.filename().string()
                     <<" => "<<status;
            if (pair.valid)
                std::cout<<" ("<<num(pair.up_nm)<<" / "<<num(pair.lp_nm)
                         <<" nm, DeltaE="<<num(pair.splitting_mev)<<" meV)";
            if (!error.empty()) std::cout<<" | "<<error;
            std::cout<<'\n';
            log<<file.filename().string()<<" : "<<status
               <<" : "<<warning<<" : "<<error<<'\n';
            summary.flush(); peaks.flush();
        }
        summary.close(); peaks.close(); log.close();
        std::ofstream latest(cfg.results_root/"latest_coupling_batch.txt");
        if (latest) latest<<fs::absolute(batch).generic_string()<<'\n';
        std::cout<<"Finished: OK="<<ok<<" REVIEW="<<review<<" failed/no_pair="<<fail<<'\n'
                 <<"Batch: "<<batch<<'\n'
                 <<"Files: summary.csv, all_peaks.csv, curves/*_fit.csv, run_log.txt\n"
                 <<"CAUTION: DeltaE is apparent spectral separation. g_app=DeltaE/2 assumes zero detuning;"
                    " linewidth PASS is a screening test, not proof of strong coupling.\n";
        return fail==0?0:2;
    } catch (const std::exception& e) {
        std::cerr<<"Error: "<<e.what()<<'\n';
        return 1;
    }
}
