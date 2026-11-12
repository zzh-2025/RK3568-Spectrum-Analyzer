#include "coupling_analysis.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}
int main() {
    try {
        std::vector<MultiLorentzPeak> peaks = {
            {566.585,120.78,530,50000},
            {612.283,82.405,510,50000},
            {644.950,43.83,283,50000},
            {704.745,93.907,659,50000}
        };
        CouplingPairSelection s;
        s.mode=CouplingPairSelection::Mode::Indices;
        s.index_a=2; s.index_b=4;
        const auto r=calculate_coupling_pair(peaks,s);
        check(r.valid,"Expected valid pair");
        check(r.up_index==2 && r.lp_index==4,"Wrong peak indices");
        check(std::abs(r.splitting_mev-265.67)<0.05,"Wrong energy separation");
        check(std::abs(r.apparent_g_mev-r.splitting_mev/2)<1e-8,"Wrong apparent g");
        check(std::abs(r.up_gamma_mev-273.77)<0.1,"Wrong energy FWHM");
        check(r.linewidth_criterion_pass,"Expected linewidth condition PASS");

        s.index_b=99;
        check(!calculate_coupling_pair(peaks,s).valid,"Invalid index must be rejected");
        s.mode=CouplingPairSelection::Mode::Windows;
        s.up_min_nm=595;s.up_max_nm=630;
        s.lp_min_nm=690;s.lp_max_nm=725;
        check(calculate_coupling_pair(peaks,s).valid,"Expected window match");
        s.up_min_nm=900;s.up_max_nm=1000;
        check(!calculate_coupling_pair(peaks,s).valid,"Empty window must be rejected");
        std::cout<<"Coupling metric tests PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr<<"Coupling metric tests FAILED: "<<e.what()<<'\n';
        return 1;
    }
}
