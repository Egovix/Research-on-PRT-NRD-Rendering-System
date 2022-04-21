//
// Created by Starry on 2021/7/10.
//

#ifndef TRT_CLION_HAAR_TPI_H
#define TRT_CLION_HAAR_TPI_H
#include <map>

float getCoefNL(std::map<int, float> &cmap, int level, int iIdx, int jIdx, int MIdx, int res)
{
    int idx = 0;
    if (MIdx == 1)
        idx = res * jIdx + (1<<level) + iIdx;
    else if (MIdx == 2)
        idx = res * ((1<<level) + jIdx) + iIdx;
    else
        idx = res * ((1<<level) + jIdx) + (1<<level) + iIdx;

    return cmap[idx];
}


int signOfQuadrant(int MIdx, int qx, int qy)
{
    if(MIdx == 1)
        return qx == 0? 1:-1;

    if(MIdx == 2)
        return qy == 0? 1:-1;

    if(MIdx == 3)
        return qx == 0 ? (qy == 0 ? 1 : -1) : (qy == 0 ? -1 : 1);

    return 0;
}

float getCoefPSumNL(std::map<int, float> &cmap, int level, int iIdx, int jIdx, int res)
{
    if((level == 0) && (iIdx == 0) && (jIdx == 0))
        return cmap[0];
    int ol = level - 1;
    int oi = iIdx / 2;
    int oj = jIdx / 2;
    int qx = iIdx - 2 * oi;
    int qy = jIdx - 2 * oj;
    return getCoefPSumNL(cmap, ol, oi, oj, res) + (1 << ol) *
                                                  (getCoefNL(cmap, ol, oi, oj, 1, res) * signOfQuadrant(1, qx, qy) +
                                                   getCoefNL(cmap, ol, oi, oj, 2, res) * signOfQuadrant(2, qx, qy) +
                                                   getCoefNL(cmap, ol, oi, oj, 3, res) * signOfQuadrant(3, qx, qy));
}



float tripleProductIntegral(std::map<int, float> &cmap1, std::map<int, float> &cmap2, std::map<int, float> &cmap3)
{

    int res = 128;
    float Cuvw = 0;
    int levelNum = 7;

    // case 1
    float integral = cmap1[0] * cmap2[0] * cmap3[0];
    float psumL, psumP, psumT;
    for(int level = 0; level < levelNum; ++level)
        for(int iIdx = 0; iIdx < (1<<level); ++iIdx)
            for(int jIdx = 0; jIdx < (1<<level); ++jIdx)
            {
                Cuvw = float(1 << level);
                float P1 = getCoefNL(cmap1, level, iIdx, jIdx, 1, res);
                float P2 = getCoefNL(cmap1, level, iIdx, jIdx, 2, res);
                float P3 = getCoefNL(cmap1, level, iIdx, jIdx, 3, res);
                float L1 = getCoefNL(cmap2, level, iIdx, jIdx, 1, res);
                float L2 = getCoefNL(cmap2, level, iIdx, jIdx, 2, res);
                float L3 = getCoefNL(cmap2, level, iIdx, jIdx, 3, res);
                float T1 = getCoefNL(cmap3, level, iIdx, jIdx, 1, res);
                float T2 = getCoefNL(cmap3, level, iIdx, jIdx, 2, res);
                float T3 = getCoefNL(cmap3, level, iIdx, jIdx, 3, res);
                //case 2
                integral += Cuvw * (
                        P1 * L2 * T3 +
                        P1 * L3 * T2 +
                        P2 * L3 * T1 +
                        P2 * L1 * T3 +
                        P3 * L1 * T2 +
                        P3 * L2 * T1);
                //case 3
                psumL = getCoefPSumNL(cmap2, level, iIdx, jIdx, res);
                psumP = getCoefPSumNL(cmap1, level, iIdx, jIdx, res);
                psumT = getCoefPSumNL(cmap3, level, iIdx, jIdx, res);
                integral +=
                        (P1 * L1 * psumT +
                         L1 * T1 * psumP +
                         T1 * P1 * psumL);
                integral +=
                        (P2 * L2 * psumT +
                         L2 * T2 * psumP +
                         T2 * P2 * psumL);
                integral +=
                        (P3 * L3 * psumT +
                         L3 * T3 * psumP +
                         T3 * P3 * psumL);
            }
    integral /= res * res *res;
    return integral;
}



#endif //TRT_CLION_HAAR_TPI_H
