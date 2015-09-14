/*
 qe2alm.cpp

 Copyright (c) 2015 Terumasa Tadano

 This file is distributed under the terms of the MIT license.
 Please see the file 'LICENCE.txt' in the root directory 
 or http://opensource.org/licenses/mit-license.php for information.
*/

#include <iostream>
#include <fstream>
#include <stdlib.h> 
#include <boost/property_tree/xml_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/version.hpp>
#include <boost/lexical_cast.hpp>
#include "constants.h"
#include "mathfunctions.h"
#include "qe2alm.h"

using namespace std;

int main() {

    cout << " QE2ALM -- a converter of harmonic FCs" << endl;
    cout << " Input file (QE fc file generated by q2r.x) : ";
    cin >> file_fc2qe;
    cout << " Output file (xml file for ALAMODE) : ";
    cin >> file_xmlout;
    cout << " Impose acoustic sum rule ? [y/n] : ";
    cin >> flag_asr;

    ifs_fc2qe.open(file_fc2qe.c_str(), std::ios::in);
    if (!ifs_fc2qe) {
        cout << " ERROR: Cannot open file " << file_fc2qe << endl;
        exit(EXIT_FAILURE);
    }

    transform(flag_asr.begin(), flag_asr.end(), flag_asr.begin(), ::tolower);
    if (flag_asr[0] == 'y') {
        consider_asr = true;
    } else if (flag_asr[0] == 'n') {
        consider_asr = false;
    } else {
        cout << " ERROR: Please specify 'y' or 'n' for the acoustic sum rule." << endl;
        exit(EXIT_FAILURE);
    }

    // Read structural parameters

    ifs_fc2qe >> nkd >> natmin >> ibrav 
              >> celldm[0] >> celldm[1] >> celldm[2]
              >> celldm[3] >> celldm[4] >> celldm[5];

    if (ibrav == 0) {
        for (i = 0; i < 3; ++i) {
            ifs_fc2qe >> lavec[i][0] >> lavec[i][1] >> lavec[i][2];
        }
    } else {
        calc_lattice_vector(ibrav, celldm, lavec);
    }

    recips(lavec, rlavec);

    int dummy;
    string str_dummy;
    double tmp;

    allocate(kd_symbol, nkd);

    for (i = 0; i < nkd; ++i) {
        ifs_fc2qe >> dummy >> kd_symbol[i] >> str_dummy >> tmp;
        kd_symbol[i] = kd_symbol[i].substr(1);
    }

    allocate(kd, natmin);
    allocate(xcrd, natmin, 3);

    for (i = 0; i < natmin; ++i) {
        ifs_fc2qe >> dummy >> kd[i] >> xcrd[i][0] >> xcrd[i][1] >> xcrd[i][2];
    }

    std::string str_na;
    ifs_fc2qe >> str_na;

    if (str_na[0] == 'T') {
        include_na = true;
    } else if (str_na[0] == 'F') {
        include_na = false;
    } else {
        cout << " ERROR: This cannot happen " << endl;
        exit(EXIT_FAILURE);   
    }
   
    if (include_na) {
       cout << " WARNING: The given force constant file contains information of" << endl;
       cout << "          dielectric constants and Born effective charges." << endl;
       cout << "          The program skips these parts and converts the" << endl;
       cout << "          short-range terms only." << endl;
    } 

    if (include_na) {
        allocate(born, natmin, 3, 3);
        for (i = 0; i < 3; ++i) {
            for (j = 0; j < 3; ++j) {
                ifs_fc2qe >> dielec[i][j];
            }
        }
        for (i = 0; i < natmin; ++i) {
            ifs_fc2qe >> dummy;
            for (j = 0; j < 3; ++j) {
                for (k = 0; k < 3; ++k) {
                    ifs_fc2qe >> born[i][j][k];
                }
            }
        }
    }

    // Read force constant info

    ifs_fc2qe >> nq[0] >> nq[1] >> nq[2];
    nsize = nq[0] * nq[1] * nq[2];

    allocate(fc2, 3*natmin, 3*natmin, nsize);

    int icell = 0;
    int m1, m2, m3;
    int icrd, jcrd;
    int iat, jat;
   
    for (icrd = 0; icrd < 3; ++icrd) {
        for (jcrd = 0; jcrd < 3; ++jcrd) {
            for (iat = 0; iat < natmin; ++iat) {
                for (jat = 0; jat < natmin; ++jat) {
                    ifs_fc2qe >> dummy >> dummy >> dummy >> dummy;

                    icell = 0;
                    for (m3 = 0; m3 < nq[2]; ++m3) {
                        for (m2 = 0; m2 < nq[1]; ++m2) {
                            for (m1 = 0; m1 < nq[0]; ++m1) {
                                ifs_fc2qe >> dummy >> dummy >> dummy;

                                // Force constant matrix should be transposed here.
                                ifs_fc2qe >> fc2[3*jat+jcrd][3*iat+icrd][icell];

                                ++icell;
                            }
                        }
                    }
                }
            }
        }
    }

    if (consider_asr) {
        
        double sum_fc;
        
        for (icrd = 0; icrd < 3; ++icrd) {
            for (jcrd = 0; jcrd < 3; ++jcrd) {
                for (iat = 0; iat < natmin; ++iat) {
                    sum_fc = 0.0;
                    for (jat = 0; jat < natmin; ++jat) {
                        for (icell = 0; icell < nsize; ++icell) {
                            sum_fc += fc2[3*iat+icrd][3*jat+jcrd][icell];
                        }
                    }
                    fc2[3*iat+icrd][3*iat+jcrd][0] -= sum_fc;
                }
            }
        }
    }

    // Convert positions from alat to crystal basis
    for (i = 0; i < natmin; ++i) {
        rotvec(xcrd[i], xcrd[i], rlavec);
        for (j = 0; j < 3; ++j) {
            xcrd[i][j] *= celldm[0] / (2.0 * pi);
        }
    }

    // Extend the system to the supercell

    nat = natmin * nsize;

    allocate(xcrd_super, nat, 3);
    allocate(kd_super, nsize, natmin);
    allocate(map_p2s, nsize, natmin);

    icell = 0;
    int icount = 0;
    
    for (m3 = 0; m3 < nq[2]; ++m3) {
        for (m2 = 0; m2 < nq[1]; ++m2) {
            for (m1 = 0; m1 < nq[0]; ++m1) {
                for (i = 0; i < natmin; ++i) {
                    xcrd_super[icount][0] = (xcrd[i][0] + static_cast<double>(m1)) / static_cast<double>(nq[0]);
                    xcrd_super[icount][1] = (xcrd[i][1] + static_cast<double>(m2)) / static_cast<double>(nq[1]);
                    xcrd_super[icount][2] = (xcrd[i][2] + static_cast<double>(m3)) / static_cast<double>(nq[2]);
                    kd_super[icell][i] = kd[i];

                    map_p2s[icell][i] = icount;
                    ++icount;
                }
                ++icell;
            }
        }
    }

    for (i = 0; i < 3; ++i) {
        for (j = 0; j < 3; ++j) {
            lavec[i][j] *= static_cast<double>(nq[j]);
        }
    }

    allocate(mindist, natmin, nat);

    get_pairs_of_minimum_distance(natmin, nat, map_p2s, xcrd_super, mindist);

    // Write to XML file

    using boost::property_tree::ptree;

    ptree pt;
    string str_pos[3], str_tmp;

    str_tmp.clear();

//    for (i = 0; i < 3; ++i) {
//        str_tmp += " " + std::to_string(nq[i]);
//    }


    pt.put("Data.FilenameOfQE", file_fc2qe);
//    pt.put("Data.Qmesh", str_tmp);

    pt.put("Data.Structure.NumberOfAtoms", nat);
    pt.put("Data.Structure.NumberOfElements", nkd);

    for (i = 0; i < nkd; ++i) {
        ptree &child = pt.add("Data.Structure.AtomicElements.element", kd_symbol[i]);
        child.put("<xmlattr>.number", i + 1);
    }

    for (i = 0; i < 3; ++i) {
        str_pos[i].clear();
        for (j = 0; j < 3; ++j) {
            str_pos[i] += " " + double2string(lavec[j][i]);
        }
    }
    pt.put("Data.Structure.LatticeVector", "");
    pt.put("Data.Structure.LatticeVector.a1", str_pos[0]);
    pt.put("Data.Structure.LatticeVector.a2", str_pos[1]);
    pt.put("Data.Structure.LatticeVector.a3", str_pos[2]);

    pt.put("Data.Structure.Position", "");

    icount = 0;

    for (i = 0; i < nsize; ++i) {
        
        for (j = 0; j < natmin; ++j) {
            str_tmp.clear();
            for (k = 0; k < 3; ++k) str_tmp += " " + double2string(xcrd_super[map_p2s[i][j]][k]);
            ptree &child = pt.add("Data.Structure.Position.pos", str_tmp);
            child.put("<xmlattr>.index", icount + 1);
            child.put("<xmlattr>.element", kd_symbol[kd_super[i][j] - 1]);

            ++icount;
        }
    }

    pt.put("Data.Symmetry.NumberOfTranslations", nsize);
    for (i = 0; i < nsize; ++i) {
        for (j = 0; j < natmin; ++j) {
            ptree &child = pt.add("Data.Symmetry.Translations.map", map_p2s[i][j] + 1);
            child.put("<xmlattr>.tran", i + 1);
            child.put("<xmlattr>.atom", j + 1);
        }
    }


    pt.put("Data.ForceConstants", "");
    str_tmp.clear();

    int nmulti, kat;

    for (iat = 0; iat < natmin; ++iat) {
        for (icrd = 0; icrd < 3; ++icrd) {
            for (icell = 0; icell < nsize; ++icell) {
                for (jat = 0; jat < natmin; ++jat) {
                    kat = map_p2s[icell][jat];
                    nmulti = mindist[iat][kat].size();
                    for (jcrd = 0; jcrd < 3; ++jcrd) {
                        if (std::abs(fc2[3*iat+icrd][3*jat+jcrd][icell]) < eps15) continue;
                        for (i = 0; i < nmulti; ++i) {
                            ptree &child = pt.add("Data.ForceConstants.HARMONIC.FC2", 
                                                  double2string(fc2[3*iat+icrd][3*jat+jcrd][icell] / static_cast<double>(nmulti)));

                            child.put("<xmlattr>.pair1", boost::lexical_cast<std::string>(iat + 1) 
                                      + " " + boost::lexical_cast<std::string>(icrd + 1));
                            child.put("<xmlattr>.pair2", boost::lexical_cast<std::string>(kat + 1) 
                                      + " " + boost::lexical_cast<std::string>(jcrd + 1)
                                      + " " + boost::lexical_cast<std::string>(mindist[iat][kat][i].cell + 1));
                        }
                    }
                }
            }
        }
    }
    

    using namespace boost::property_tree::xml_parser;
    const int indent = 2;

#if BOOST_VERSION >= 105600
    write_xml(file_xmlout, pt, std::locale(),
              xml_writer_make_settings<ptree::key_type>(' ', indent, widen<std::string>("utf-8")));
#else
    write_xml(file_xmlout, pt, std::locale(),
              xml_writer_make_settings(' ', indent, widen<char>("utf-8")));
#endif


    deallocate(kd);
    deallocate(kd_symbol);
    deallocate(xcrd);
    deallocate(fc2);
    deallocate(xcrd_super);
    deallocate(kd_super);
    deallocate(map_p2s);

    if (include_na) {
        deallocate(born);
    }
}


void calc_lattice_vector(const int ibrav, double celldm[6], double aa[3][3])
{
    double a, b, c;
    double alpha, beta, gamma;
    double tx, ty, tz;
    double a2, b2, c2;
    double cosalpha, a_prime, u, v;

    switch (ibrav) {
    case 1:
        a = celldm[0];
        aa[0][0] = a  ; aa[0][1] = 0.0; aa[0][2] = 0.0;
        aa[1][0] = 0.0; aa[1][1] = a  ; aa[1][2] = 0.0;
        aa[2][0] = 0.0; aa[2][1] = 0.0; aa[2][2] = a  ;
        
        break;

    case 2:
        a = celldm[0] / 2.0;
        aa[0][0] = -a ; aa[0][1] = 0.0; aa[0][2] = a  ;
        aa[1][0] = 0.0; aa[1][1] = a  ; aa[1][2] = a  ;
        aa[2][0] = -a ; aa[2][1] = a  ; aa[2][2] = 0.0;
        
        break;

    case 3:
        a = celldm[0] / 2.0;
        aa[0][0] =  a; aa[0][1] =  a; aa[0][2] = a;
        aa[1][0] = -a; aa[1][1] =  a; aa[1][2] = a;
        aa[2][0] = -a; aa[2][1] = -a; aa[2][2] = a;

        break;

    case 4:
        a = celldm[0];
        c = celldm[0] * celldm[2];
        aa[0][0] = a; aa[0][1] = 0.0; aa[0][2] = 0.0;
        aa[1][0] = -0.5*a; aa[1][1] = sqrt(3.0)/2.0*a; aa[1][2] = 0.0;
        aa[2][0] = 0.0; aa[2][1] = 0.0; aa[2][2] = c;

        break;

    case 5:
    case -5:
        a= celldm[0];
        cosalpha = celldm[3];
        tx = a * sqrt((1.0 - cosalpha) / 2.0);
        ty = a * sqrt((1.0 - cosalpha) / 6.0);
        tz = a * sqrt((1.0 + 2.0 * cosalpha) / 3.0);

        if (ibrav == 5) {
            aa[0][0] = tx; aa[0][1] = -ty; aa[0][2] = tz;
            aa[1][0] = 0.0; aa[1][1] = 2.0 * ty; aa[1][2] = tz;
            aa[2][0] = -tx; aa[2][1] = -ty; aa[2][2] = tz;
        } else {
            a_prime = a / sqrt(3.0);
            u = tz - 2.0 * sqrt(2.0) * ty;
            v = tz + sqrt(2.0) * ty;
            aa[0][0] = u; aa[0][1] = v; aa[0][2] = v;
            aa[1][0] = v; aa[1][1] = u; aa[1][2] = v;
            aa[2][0] = v; aa[2][1] = v; aa[2][2] = u;
        }
        
        break;

    case 6:
        a = celldm[0];
        c = celldm[0] * celldm[2];
        aa[0][0] =   a; aa[0][1] = 0.0; aa[0][2] = 0.0;
        aa[1][0] = 0.0; aa[1][1] =   a; aa[1][2] = 0.0;
        aa[2][0] = 0.0; aa[2][1] = 0.0; aa[2][2] =   c;

        break;

    case 7:
        a = celldm[0];
        c = celldm[0] * celldm[2];
        a2 = a*0.5;
        c2 = c*0.5;
        aa[0][0] =  a2; aa[0][1] = -a2; aa[0][2] = c2;
        aa[1][0] =  a2; aa[1][1] =  a2; aa[1][2] = c2;
        aa[2][0] = -a2; aa[2][1] = -a2; aa[2][2] = c2;

        break;

    case 8:
        a = celldm[0];
        b = celldm[0] * celldm[1];
        c = celldm[0] * celldm[2];
        aa[0][0] =   a; aa[0][1] = 0.0; aa[0][2] = 0.0;
        aa[1][0] = 0.0; aa[1][1] =   b; aa[1][2] = 0.0;
        aa[2][0] = 0.0; aa[2][1] = 0.0; aa[2][2] =   c;

        break;

    case 9:
    case -9:
        a = celldm[0];
        b = celldm[0] * celldm[1];
        c = celldm[0] * celldm[2];
        a2 = a*0.5;
        b2 = b*0.5;

        if (ibrav == 9) {
            aa[0][0] =  a2; aa[0][1] =  b2; aa[0][2] = 0.0;
            aa[1][0] = -a2; aa[1][1] =  b2; aa[1][2] = 0.0;
            aa[2][0] = 0.0; aa[2][1] = 0.0; aa[2][2] =   c;
        } else {
            aa[0][0] =  a2; aa[0][1] = -b2; aa[0][2] = 0.0;
            aa[1][0] =  a2; aa[1][1] =  b2; aa[1][2] = 0.0;
            aa[2][0] = 0.0; aa[2][1] = 0.0; aa[2][2] =   c;
        }

        break;

    case 10:
        a2 = celldm[0] *  0.5;
        b2 = celldm[0] * celldm[1] * 0.5;
        c2 = celldm[0] * celldm[2] * 0.5;
        aa[0][0] =  a2; aa[0][1] = 0.0; aa[0][2] =  c2;
        aa[1][0] =  a2; aa[1][1] =  b2; aa[1][2] = 0.0;
        aa[2][0] = 0.0; aa[2][1] =  b2; aa[2][2] =  c2;

        break;

    case 11:
        a2 = celldm[0] *  0.5;
        b2 = celldm[0] * celldm[1] * 0.5;
        c2 = celldm[0] * celldm[2] * 0.5;
        aa[0][0] =  a2; aa[0][1] =  b2; aa[0][2] = c2;
        aa[1][0] = -a2; aa[1][1] =  b2; aa[1][2] = c2;
        aa[2][0] = -a2; aa[2][1] = -b2; aa[2][2] = c2;
        
        break;

    case 12:
        a = celldm[0];
        b = celldm[0] * celldm[1];
        c = celldm[0] * celldm[2];
        gamma = acos(celldm[3]);
        aa[0][0] = a; aa[0][1] = 0.0; aa[0][2] = 0.0;
        aa[1][0] = b*cos(gamma); aa[1][1] = b*sin(gamma); aa[1][2] = 0.0;
        aa[2][0] = 0.0; aa[2][1] = 0.0; aa[2][2] = c;

        break;

    case -12:
        a = celldm[0];
        b = celldm[0] * celldm[1];
        c = celldm[0] * celldm[2];
        beta = acos(celldm[4]);
        aa[0][0] = a; aa[0][1] = 0.0; aa[0][2] = 0.0;
        aa[1][0] = 0.0; aa[1][1] = b; aa[1][2] = 0.0;
        aa[2][0] = c*cos(beta); aa[2][1] = 0.0; aa[2][2] = c*sin(beta);

        break;

    case 13:
        a= celldm[0];
        b = celldm[0] * celldm[1];
        c = celldm[0] * celldm[2];
        gamma = acos(celldm[3]);
        aa[0][0] = 0.5*a; aa[0][1] = 0.0; aa[0][2] = -0.5*c;
        aa[1][0] = b*cos(gamma); aa[1][1] = b*sin(gamma); aa[1][2] = 0.0;
        aa[2][0] = 0.5*a; aa[2][1] = 0.0; aa[2][2] = 0.5*c;

        break;

    case 14:
        a = celldm[0];
        b = celldm[0] * celldm[1];
        c = celldm[0] * celldm[2];
        alpha = acos(celldm[3]);
        beta  = acos(celldm[4]);
        gamma = acos(celldm[5]);
        aa[0][0] = a; aa[0][1] = 0.0; aa[0][2] = 0.0;
        aa[1][0] = b*cos(gamma); aa[1][1] = b*sin(gamma); aa[1][2] = 0.0;
        aa[2][0] = c*cos(beta); aa[2][1] = c*(cos(alpha)-cos(beta)*cos(gamma))/sin(gamma);
        aa[2][2] = c*sqrt(1.0 + 2.0*cos(alpha)*cos(beta)*cos(gamma)
                          - cos(alpha)*cos(alpha)-cos(beta)*cos(beta)-cos(gamma)*cos(gamma))/sin(gamma);

        break;

    default:

        cout << "ERROR: Invalid ibrav." << endl;
        exit(EXIT_FAILURE);
    }


    // Transpose lavec for later use

    double tmp[3][3];

    for (i = 0; i < 3; ++i) {
        for (j = 0; j < 3; ++j) {
            tmp[i][j] = aa[i][j];
        }
    }

    for (i = 0; i < 3; ++i) {
        for (j = 0; j < 3; ++j) {
            aa[i][j] = tmp[j][i];
        }
    }
    
}


void recips(double aa[3][3], double bb[3][3])
{
    /*
    Calculate Reciprocal Lattice Vectors

    Here, BB is just the inverse matrix of AA (multiplied by factor 2 Pi)

    BB = 2 Pi AA^{-1},
    = t(b1, b2, b3)

    (b11 b12 b13)
    = (b21 b22 b23)
    (b31 b32 b33),

    b1 = t(b11, b12, b13) etc.
    */

    double det;
    det = aa[0][0] * aa[1][1] * aa[2][2] 
    + aa[1][0] * aa[2][1] * aa[0][2] 
    + aa[2][0] * aa[0][1] * aa[1][2]
    - aa[0][0] * aa[2][1] * aa[1][2] 
    - aa[2][0] * aa[1][1] * aa[0][2]
    - aa[1][0] * aa[0][1] * aa[2][2];

    if (std::abs(det) < eps12) {
        cout << " ERROR: Lattice vector is singular" << endl;
        exit(EXIT_FAILURE);
    }

    double factor = 2.0 * pi / det;

    bb[0][0] = (aa[1][1] * aa[2][2] - aa[1][2] * aa[2][1]) * factor;
    bb[0][1] = (aa[0][2] * aa[2][1] - aa[0][1] * aa[2][2]) * factor;
    bb[0][2] = (aa[0][1] * aa[1][2] - aa[0][2] * aa[1][1]) * factor;

    bb[1][0] = (aa[1][2] * aa[2][0] - aa[1][0] * aa[2][2]) * factor;
    bb[1][1] = (aa[0][0] * aa[2][2] - aa[0][2] * aa[2][0]) * factor;
    bb[1][2] = (aa[0][2] * aa[1][0] - aa[0][0] * aa[1][2]) * factor;

    bb[2][0] = (aa[1][0] * aa[2][1] - aa[1][1] * aa[2][0]) * factor;
    bb[2][1] = (aa[0][1] * aa[2][0] - aa[0][0] * aa[2][1]) * factor;
    bb[2][2] = (aa[0][0] * aa[1][1] - aa[0][1] * aa[1][0]) * factor;
}


string double2string(const double d)
{
    std::string rt;
    std::stringstream ss;

    ss << std::scientific << std::setprecision(15) << d;
    ss >> rt;
    return rt;
}


void get_pairs_of_minimum_distance(const int natmin, const int nat, int **map_p2s,
                                   double **xf, std::vector<DistInfo> **mindist_pairs)
{
    int icell = 0;
    int i, j, k;
    int isize, jsize, ksize;
    int iat;
    double dist_tmp;
    double vec[3];
    std::vector<DistInfo> **distall;
    int nneib = 27;

    double ***xcrd;

    allocate(distall, natmin, nat);
    allocate(xcrd, nneib, nat, 3);

    for (i = 0; i < nat; ++i) {
        for (j = 0; j < 3; ++j) {
            xcrd[0][i][j] = xf[i][j];
        }
    }

    for (isize = -1; isize <= 1; ++isize) {
        for (jsize = -1; jsize <= 1; ++jsize) {
            for (ksize = -1; ksize <= 1; ++ksize) {

                if (isize == 0 && jsize == 0 && ksize == 0) continue;

                ++icell;
                for (i = 0; i < nat; ++i) {
                    xcrd[icell][i][0] = xf[i][0] + static_cast<double>(isize);
                    xcrd[icell][i][1] = xf[i][1] + static_cast<double>(jsize);
                    xcrd[icell][i][2] = xf[i][2] + static_cast<double>(ksize);
                }
            }
        }
    }

    for (icell = 0; icell < nneib; ++icell) {
        for (i = 0; i < nat; ++i) {
            rotvec(xcrd[icell][i], xcrd[icell][i], lavec);
        }
    }


    for (i = 0; i < natmin; ++i) {
        iat = map_p2s[0][i];
        for (j = 0; j < nat; ++j) {
            for (icell = 0; icell < nneib; ++icell) {

                dist_tmp = distance(xcrd[0][iat], xcrd[icell][j]);

                for (k = 0; k < 3; ++k) vec[k] = xcrd[icell][j][k] - xcrd[0][iat][k];

                distall[i][j].push_back(DistInfo(icell, dist_tmp, vec));
            }
            std::sort(distall[i][j].begin(), distall[i][j].end());
        }
/*
        for (j = 0; j < nat; ++j) {
            for (k = 0; k < distall[i][j].size(); ++k) {
                std::cout << std::setw(5) << i + 1;
                std::cout << std::setw(5) << j + 1;
                std::cout << std::setw(5) << k + 1;
                std::cout << std::setw(15) << distall[i][j][k].dist << endl;
            }
        }
*/
    }

    // Construct pairs of minimum distance.

    double dist_min;
    for (i = 0; i < natmin; ++i) {
        for (j = 0; j < nat; ++j) {
            mindist_pairs[i][j].clear();

            dist_min = distall[i][j][0].dist;
            for (std::vector<DistInfo>::const_iterator it = distall[i][j].begin(); it != distall[i][j].end(); ++it) {
                if (std::abs((*it).dist - dist_min) < eps6) {
                    mindist_pairs[i][j].push_back(DistInfo(*it));
                }
            }
        }
    }

    deallocate(distall);
    deallocate(xcrd);
}


double distance(double *x1, double *x2)
{
    double dist;    
    dist = std::pow(x1[0] - x2[0], 2) + std::pow(x1[1] - x2[1], 2) + std::pow(x1[2] - x2[2], 2);
    dist = std::sqrt(dist);

    return dist;
}
