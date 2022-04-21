/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
#pragma once
#include "Falcor.h"
#include "FalcorExperimental.h"
#include "Utils/Debug/PixelDebug.h"
#include "my_haar.h"

using namespace Falcor;

class LightComputer
{
public:

    void StoreColorLT(const Texture& mpBRDFPerMaterial, uint id);
    void StoreColorLTWithDiffuse(const Texture& mpBRDFPerMaterial, uint id, const float cosine[49152]);
    void StoreColorLT_Batch(const Texture& mpBRDFPerMaterial_128, const Texture& mpBRDFPerMaterial_64, const Texture& mpBRDFPerMaterial_32, const Texture& mpBRDFPerMaterial_16, uint id, std::vector<uint2> ResId);
    void StoreImg(const Texture& mpBRDFPerMaterial, uint batchId, uint id);
    void calculateLightWaveletCoefficients_QuadTree_Parallel(std::vector<std::vector<uint8_t>> textureData, int start, std::vector<uint>& brdfWoFaceCoeStartIndex, std::vector<float>& root_arr_brdf);

    LightComputer();
};


