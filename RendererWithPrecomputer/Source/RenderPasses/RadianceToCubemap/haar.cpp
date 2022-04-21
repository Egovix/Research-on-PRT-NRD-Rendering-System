#include "my_haar.h"
#include "math.h"
#include <algorithm>
#include <vector>
#include <assert.h>

#define ONE_BY_SQRT2 0.70710678118654752440084436210485f
#define FUNCTION_RES 128
bool cmpAbsf(float x, float y) {
    return abs(x) > abs(y);
}

MyWavelet::MyWavelet(void)
{
    m_tmp1 = new float[FUNCTION_RES * FUNCTION_RES];
    memset(m_tmp1, 0, sizeof(float) * FUNCTION_RES * FUNCTION_RES);
    m_tmp2 = new float[FUNCTION_RES * FUNCTION_RES];
    memset(m_tmp2, 0, sizeof(float) * FUNCTION_RES * FUNCTION_RES);
    m_tmp3 = new float[FUNCTION_RES * FUNCTION_RES];
    memset(m_tmp3, 0, sizeof(float) * FUNCTION_RES * FUNCTION_RES);
}

MyWavelet::~MyWavelet(void)
{
    delete[] m_tmp1;
    m_tmp1 = NULL;
    delete[] m_tmp2;
    m_tmp2 = NULL;
    delete[] m_tmp3;
    m_tmp3 = NULL;

}

void MyWavelet::Haar1D(float* array, int len)
{
    for (int i = 0; i < len / 2; ++i)
    {
        m_tmp1[i] = (array[i * 2] + array[i * 2 + 1]) * ONE_BY_SQRT2;
        m_tmp1[i + len / 2] = (array[i * 2] - array[i * 2 + 1]) * ONE_BY_SQRT2;
    }
    memcpy(array, m_tmp1, len * sizeof(float));
}

void MyWavelet::Haar2D(float* mat, int rowNum, int colNum)
{
    for (int related = rowNum; related > 1; related /= 2)
    {
        for (int i = 0; i < related; ++i)
        {
            Haar1D(mat + i * colNum, related);
        }
        for (int i = 0; i < related; ++i)
        {
            for (int j = 0; j < related; ++j)
                m_tmp2[j] = *(mat + j * colNum + i);
            Haar1D(m_tmp2, related);
            for (int j = 0; j < related; ++j)//copy in
                *(mat + j * colNum + i) = m_tmp2[j];
        }
    }
}

void MyWavelet::reverseHaar1D(float* array, int len)
{

    for (int i = 0; i < len / 2; ++i)
    {
        m_tmp1[2 * i] = (array[i] + array[i + len / 2]) * ONE_BY_SQRT2;
        m_tmp1[2 * i + 1] = (array[i] - array[i + len / 2]) * ONE_BY_SQRT2;
    }
    memcpy(array, m_tmp1, len * sizeof(float));

}

void MyWavelet::reverseHaar2D(float* mat, int rowNum, int colNum)
{
    for (int related = 2; related <= rowNum; related *= 2)
    {
        for (int i = 0; i < related; ++i)
        {
            reverseHaar1D(mat + i * colNum, related);
        }
        for (int i = 0; i < related; ++i)
        {
            for (int j = 0; j < related; ++j)
                m_tmp3[j] = *(mat + j * colNum + i);
            reverseHaar1D(m_tmp3, related);
            for (int j = 0; j < related; ++j)
                *(mat + j * colNum + i) = m_tmp3[j];
        }
    }
}

void MyWavelet::standardHaar1D(float* array, int len)
{
    assert((len & (len - 1)) == 0);
    //memset(m_temp, 0, len * sizeof(Vec3));

    for (int related = len; related > 1; related /= 2)
    {
        for (int i = 0; i < related / 2; ++i)
        {
            m_tmp1[i] = (array[i * 2] + array[i * 2 + 1]) * ONE_BY_SQRT2;
            m_tmp1[i + related / 2] = (array[i * 2] - array[i * 2 + 1]) * ONE_BY_SQRT2;
        }
        memcpy(array, m_tmp1, len * sizeof(float));
    }
}

void MyWavelet::standardHaar2D(float* mat, int rowNum, int colNum)
{
    assert((rowNum & (rowNum - 1)) == 0);
    assert((colNum & (colNum - 1)) == 0);

    for (int i = 0; i < rowNum; ++i)
    {
        standardHaar1D(mat + i * colNum, colNum);
    }

    for (int i = 0; i < colNum; ++i)
    {
        for (int j = 0; j < rowNum; ++j)
            m_tmp2[j] = *(mat + j * colNum + i);
        standardHaar1D(m_tmp2, rowNum);
        for (int j = 0; j < rowNum; ++j)
            *(mat + j * colNum + i) = m_tmp2[j];
    }
}

void MyWavelet::standardReverseHaar1D(float* array, int len)
{
    for (int related = 2; related <= len; related *= 2)
    {
        for (int i = 0; i < related / 2; ++i)
        {
            m_tmp3[2 * i] = (array[i] + array[i + related / 2]) * ONE_BY_SQRT2;
            m_tmp3[2 * i + 1] = (array[i] - array[i + related / 2]) * ONE_BY_SQRT2;
        }
        memcpy(array, m_tmp3, related * sizeof(float));
    }
}

void MyWavelet::standardReverseHaar2D(float* mat, int rowNum, int colNum)
{
    for (int i = 0; i < rowNum; ++i)
    {
        standardReverseHaar1D(mat + i * colNum, colNum);
    }
    for (int i = 0; i < colNum; ++i)
    {
        for (int j = 0; j < rowNum; ++j)
            m_tmp3[j] = *(mat + j * colNum + i);
        standardReverseHaar1D(m_tmp3, rowNum);
        for (int j = 0; j < rowNum; ++j)
            *(mat + j * colNum + i) = m_tmp3[j];
    }
}

void MyWavelet::keepLargestN(float* mat, int* ind, float* res, int keepN) {
    int n = FUNCTION_RES * FUNCTION_RES;
    float* tmp = new float[n];
    for (int i = 0; i < n; i++)
        tmp[i] = mat[i];
    int count = 0;
    std::sort(tmp, tmp + n, cmpAbsf);
    for (int i = 0; i < n; i++) {
        /* write coeffs and indexs to buffers */
        if (count < keepN && abs(mat[i]) >= abs(tmp[keepN - 1])) {
            ind[count] = i;
            res[count++] = mat[i];
        }
        /* set coeffs to 0 */
        else {
            mat[i] = 0.0f;
        }
    }
    delete[] tmp;

}

