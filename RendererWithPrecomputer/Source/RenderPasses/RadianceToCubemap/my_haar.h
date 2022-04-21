//
// Created by Starry on 2021/7/10.
//

#ifndef TRT_CLION_MY_HAAR_HPP
#define TRT_CLION_MY_HAAR_HPP

class MyWavelet
{
public:
    MyWavelet(void);
    ~MyWavelet(void);
    /* Non standard Haar compression, we use these methods. */
    void Haar1D(float* array, int len);//haar1D
    void Haar2D(float* mat, int rowNum, int colNum);
    void reverseHaar2D(float* mat, int rowNum, int colNum);
    void reverseHaar1D(float* array, int len);

    /* Standard Haar, we don't use */
    void standardHaar1D(float* array, int len);
    void standardReverseHaar1D(float* array, int len);
    void standardHaar2D(float* mat, int rowNum, int colNum);
    void standardReverseHaar2D(float* mat, int rowNum, int colNum);
    void keepLargestN(float* mat, int* ind, float* res, int keepN);

private:
    float* m_tmp1;
    float* m_tmp2;
    float* m_tmp3;
};





#endif //TRT_CLION_MY_HAAR_HPP
