// interaction.cpp

#include "interaction.h"
#include "memory.h"
#include "system.h"
#include "error.h"
#include "symmetry.h"
#include <iostream>
#include <iomanip>
#include <Eigen/core>
#include <vector>
#include <algorithm>
#include "combination.h"
#include "listcomparison.h"

using namespace ALM_NS;

Interaction::Interaction(ALM *alm) : Pointers(alm) {
    nsize[0] = nsize[1] = nsize[2] = 1;
    maxorder = 3;
}

Interaction::~Interaction() {}

void Interaction::init()
{
    int i, j;
    int nat = system->nat;
    int nkd = system->nkd;

    std::cout << "Cutoff Radii (Bohr Unit.)" << std::endl;
    std::cout << std::setw(8) << "Species" 
        << std::setw(9) << "HARMONIC"
        << std::setw(9) << "ANHARM3"
        << std::setw(9) << "ANHARM4" << std::endl;

    for (i = 0; i < nkd; i++){
        std::cout << std::setw(8) << i + 1 
            << std::setw(9) << rcs[i][0] 
        << std::setw(9) << rcs[i][1] 
        << std::setw(9) << rcs[i][2] << std::endl;
    }

    for (i = 0; i < 3; i++){
        if(!is_periodic[i]) nsize[i] = 0;
    }

    std::cout << std::endl << "Periodicity Flags (0: Non-Periodic, else: Periodic)" << std::endl;
    std::cout << "a axis: " << std::setw(3) << is_periodic[0] << std::endl
        << "b axis: " << std::setw(3) << is_periodic[1] << std::endl
        << "c axis: " << std::setw(3) << is_periodic[2] << std::endl << std::endl;

    //   Eigen::MatrixXd xfrac(nat, 3);

    //   for (i = 0; i < nat; i++){
    //       for (j = 0; j < 3; j++){
    //           xfrac(i,j) = system->xcoord[i][j];
    //       }
    //   }
    nneib = (2 * nsize[0] + 1) * (2 * nsize[1] + 1) * (2 * nsize[2] + 1);
    memory->allocate(xcrd, nneib, nat, 3);
    memory->allocate(distlist, nat, nat);
    //  calc_distlist(nat, xfrac);
    calc_distlist(nat, system->xcoord);

    search_interactions();


}

double Interaction::distance(double *x1, double *x2)
{
    double dist;    
    dist = pow(x1[0] - x2[0], 2) + pow(x1[1] - x2[1], 2) + pow(x1[2] - x2[2], 2);
    dist = sqrt(dist);

    return dist;
}

//void Interaction::calc_distlist(int nat, Eigen::MatrixXd xf)
void Interaction::calc_distlist(int nat, double **xf)
{
    int icell = 0;
    int i, j;
    int isize, jsize, ksize;

    for (i = 0; i < nat; i++){
        for (j = 0; j < 3; j++){
            //        xcrd[0][i][j] = xf(i,j);
            xcrd[0][i][j] = xf[i][j];
        }
    }

    for (isize = -nsize[0]; isize <= nsize[0] ; isize++){
        for (jsize = -nsize[1]; jsize <= nsize[1] ; jsize++){
            for (ksize = -nsize[2]; ksize <= nsize[2] ; ksize++){
                if (isize == 0 && jsize == 0 && ksize == 0) continue;

                icell++;
                for (i = 0; i < nat; i++){
                    //            xcrd[icell][i][0] = xf(i,0) + static_cast<double>(isize);
                    //           xcrd[icell][i][1] = xf(i,1) + static_cast<double>(jsize);
                    //          xcrd[icell][i][2] = xf(i,2) + static_cast<double>(ksize);
                    xcrd[icell][i][0] = xf[i][0] + static_cast<double>(isize);
                    xcrd[icell][i][1] = xf[i][1] + static_cast<double>(jsize);
                    xcrd[icell][i][2] = xf[i][2] + static_cast<double>(ksize);
                }
            }
        }
    }

    for (icell = 0; icell < nneib; icell++) system->frac2cart(xcrd[icell]);

    double dist_tmp;

    for (i = 0; i < nat; i++){
        for (j = i; j < nat; j++){
            distlist[i][j] = distance(xcrd[0][i], xcrd[0][j]);
            distlist[j][i] = distlist[i][j];
        }
    }

    for (icell = 1; icell < nneib; icell++){
        for (i = 0; i < nat; i++){
            for (j = i; j < nat; j++){
                dist_tmp = distance(xcrd[0][i], xcrd[icell][j]);
                distlist[i][j] = std::min<double>(dist_tmp, distlist[i][j]);
                distlist[j][i] = std::min<double>(dist_tmp, distlist[j][i]);
            }
        }
    }
}

void Interaction::search_interactions()
{

    int icell;
    int i, j, k;
    int iat, jat;
    int order;

    double dist;

    int natmin = symmetry->natmin;
    int nat = system->nat;

    int ***countint;
    int ***intpairs;
    int **ninter;

    memory->allocate(countint, natmin, nat, maxorder);
    memory->allocate(intpairs, natmin, maxorder, nat);
    memory->allocate(ninter, natmin, maxorder);

    // initialize arrays
    for (i = 0; i < natmin; ++i){
        for (j = 0; j < nat; ++j){
            for (order = 0; order < maxorder; ++order){
                countint[i][j][order] = 0;
            }
        }
    }

    for (i = 0; i < natmin; ++i){
        for (order = 0; order < maxorder; ++order){
            for (j = 0; j < nat; ++j){
                intpairs[i][order][j] = 0;
            }
        }
    }

    for (i = 0; i < natmin; i++){
        for (order = 0; order < maxorder; ++order){
            ninter[i][order] = 0;
        }
    }
    ///

    for (icell = 0; icell < nneib; icell++){
        for (i = 0; i < natmin; i++){

            iat = symmetry->map_p2s[i][0] - 1; //index of an atom in the primitive cell

            for (jat = 0; jat < nat; jat++){

                dist = distance(xcrd[0][iat], xcrd[icell][jat]);

                for (order = 0; order < 3; order++){

                    if(dist < rcs[system->kd[iat] - 1][order] + rcs[system->kd[jat] - 1][order]) {

                        if(!countint[i][jat][order]) {
                            intpairs[i][order][ninter[i][order]] = jat;
                            ++ninter[i][order];
                        }
                        ++countint[i][jat][order];
                    }
                }
            }
        }
    }
    std::cout << "OK" << std::endl;
    if(maxval(natmin, nat, order, countint) > 1) {
        error->warn("search_interactions", "Duplicate interaction exits\nThis will be a critical problem for a large cell MD.");
    }

#ifdef _DEBUG
    for (i = 0; i < natmin; i++){
        iat = symmetry->map_p2s[i][0];
        for (j = 0; j < nat; j++){
            std::cout << std::setw(5) << iat << std::setw(5) << j + 1; 
            for (order = 0; order < maxorder; ++order){
                std::cout << std::setw(3) << countint[i][j][order];
            }
            std::cout << std::endl;
        }
    }
#endif

    std::vector<int> intlist;
    std::vector<int> intnew;
    std::string str_order[3];

    str_order[0] = "HARMONIC";
    str_order[1] = "ANHARM3 ";
    str_order[2] = "ANHARM4 ";

    intlist.clear();
    for(order = 0; order < maxorder; ++order){
        std::set<IntList> listset;
        for(i = 0; i < natmin; ++i){

            iat = symmetry->map_p2s[i][0] - 1;

            for(j = 0; j < ninter[i][order]; ++j){
                intlist.push_back(intpairs[i][order][j]);
            }
            std::sort(intlist.begin(), intlist.end());

#ifdef _DEBUG
            for(std::vector<int>::iterator it = intlist.begin(); it != intlist.end(); ++it){
                std::cout << std::setw(5) << iat + 1 << std::setw(7) << *it + 1<< std::endl;
            }
#endif
            int id = 0;
            std::cout << "Atom " << std::setw(5) << iat + 1 << std::endl;
            std::cout << "Order: " << str_order[order] << " interact with atoms ..." << std::endl;
            for(std::vector<int>::iterator it = intlist.begin(); it != intlist.end(); ++it){
                std::cout << std::setw(5) << *it + 1;
                if(!(++id%15)) std::cout << std::endl;
            }
            std::cout << std::endl;
            std::cout << "Number of total interaction pairs (duplication allowed) = " << ninter[i][order] << std::endl;

            int *intarr;        
            intarr = new int [order + 2];

            if(order == 0){
                for(unsigned int ielem = 0; ielem < intlist.size(); ++ielem){
                    intarr[0] = iat;
                    intarr[1] = intlist[ielem];
                    insort(order+2, intarr);

                    //        list_tmp.clean();
                    //       list_tmp.iarray.push_back(intarr[0]);
                    //      list_tmp.iarray.push_back(intarr[1]);
                    //       std::cout << "TEST " << list_tmp.iarray[0] << " " << list_tmp.iarray[1] ;
                    listset.insert(IntList(order+2, intarr));
                    std::cout << "SIZE " <<  listset.size() << std::endl;
                }
            } else if (order > 0) {
                CombinationWithRepetition<int> g(intlist.begin(), intlist.end(), order + 1);
                do {
                    std::vector<int> data = g.now();
                    intarr[0] = iat;
                    intarr[1] = data[0];
                    //                    std::cout << std::setw(5) << data[0];
                    for(unsigned int isize = 1; isize < data.size() ; ++isize){
                        intarr[isize + 1] = data[isize];
                        //                       std::cout << std::setw(5) << data[isize];
                    }
                    //std::cout << std::endl;
                    if(!is_incutoff(order+2, intarr)) continue;
                    insort(order+2, intarr);
                    //                    for(int mm = 0; mm < order + 2; ++mm){
                    //                        std::cout << std::setw(5) << intarr[mm];
                    //                    }
                    //                    std::cout << std::setw(3) << is_incutoff(order+2, intarr);
                    //                    std::cout << std::endl;

                } while(g.next());
            }
            intlist.clear();
            delete intarr;


        }

        for (std::set<IntList>::iterator p = listset.begin(); p != listset.end(); ++p){
            std::cout << *p;
            std::cout << "OK" << std::endl;
        }
    }
}


bool Interaction::is_incutoff(int n, int *atomnumlist)
{
    int i, j;
    double **dist_tmp;
    double tmp;
    int *min_neib;
    int ncheck = n - 1;

    memory->allocate(dist_tmp, nneib, ncheck);
    min_neib = new int [ncheck];

    // distance from a reference atom[0] in the original cell
    // to atom number atom[j](j > 0) in the neighboring cells.
    for (i = 0; i < nneib; ++i){
        for (j = 0; j < ncheck; ++j){
            dist_tmp[i][j] = distance(xcrd[0][atomnumlist[0]], xcrd[i][atomnumlist[j+1]]);
        }
    }

    // find the indecis of neighboring cells which minimize the distances.
    for (i = 0; i < ncheck; ++i){

        tmp = dist_tmp[0][i];
        min_neib[i] = 0;

        for (j = 1; j < nneib; ++j){
            if(dist_tmp[j][i] < tmp) {
                tmp = dist_tmp[j][i];
                min_neib[i] = j;
            }
        }
    }

    // judge whether or not the given atom list interact with each other
    for (i = 0; i < ncheck; ++i){
        for (j =  i + 1; j < ncheck; ++j){

            tmp = distance(xcrd[min_neib[i]][atomnumlist[i + 1]], xcrd[min_neib[j]][atomnumlist[j + 1]]);
            if(tmp > rcs[system->kd[atomnumlist[i + 1]] - 1][ncheck - 1] + rcs[system->kd[atomnumlist[j + 1]] - 1][ncheck - 1]){
                memory->deallocate(dist_tmp);
                delete min_neib;
                return false;
            }
        }
    }

    memory->deallocate(dist_tmp);
    delete min_neib;
    return true;
}


template <typename T>
T Interaction::maxval(int n, T *arr)
{
    T tmp;
    tmp = arr[0];

    for (int i = 0; i < n; i++) {
        tmp = std::max<T>(tmp, arr[i]);
    }
    return tmp;
}

template <typename T>
T Interaction::maxval(int n1, int n2, T **arr)
{
    T tmp;
    tmp = arr[0][0];

    for (int i = 0; i < n1; i++) {
        for (int j = 0; j < n2; j++){
            tmp = std::max<T>(tmp, arr[i][j]);
        } 
    }
    return tmp;
}

template <typename T>
T Interaction::maxval(int n1, int n2, int n3, T ***arr)
{
    T tmp;
    tmp = arr[0][0][0];

    for (int i = 0; i < n1; i++) {
        for (int j = 0; j < n2; j++){
            for (int k = 0; k < n3; k++){
                tmp = std::max<T>(tmp, arr[i][j][k]);
            } 
        }
    }
    return tmp;
}

template <typename T>
void Interaction::insort(int n, T *arr)
{
    int i, j;
    T tmp;

    for (i = 1; i < n; ++i){
        tmp = arr[i];
        for (j = i - 1; j >= 0 && arr[j] > tmp; --j){
            arr[j + 1] = arr[j];
        }
        arr[j + 1] = tmp;
    }
}