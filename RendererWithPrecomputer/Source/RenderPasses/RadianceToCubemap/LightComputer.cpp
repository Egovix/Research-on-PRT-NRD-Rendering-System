#include "LightComputer.h"
#include "QuadTree.h"
#include <omp.h>


LightComputer::LightComputer()
{

}

std::vector<uint8_t> captureToSystemRAM(uint32_t mipLevel, uint32_t arraySlice, const Texture& pTexture)
{
    RenderContext* pContext = gpDevice->getRenderContext();
    std::vector<uint8_t> textureData;
    uint32_t subresource = pTexture.getSubresourceIndex(arraySlice, mipLevel);
    textureData = pContext->readTextureSubresource(&pTexture, subresource);
    return textureData;
}

void LightComputer::StoreColorLT(const Texture& mpBRDFPerMaterial, uint id)
{
    uint perBatchId = id % 4;

    for (size_t i = 0; i < 1024; i++) // pointId
    {
        if (perBatchId * 1024u + i >= 4096)
        {
            return;
        }
        Texture tempTex = mpBRDFPerMaterial;

        std::vector<uint8_t> textureData;
        // Prepare each wo's origin BRDF data organized by TextureArray2D.
        textureData = captureToSystemRAM(0, uint32_t(i), tempTex); // Use vector<uint_t> to avoid destruction of point.

        void* pData = (void*)textureData.data();
        std::vector<float> rgbValue;
        int rgbLength = (int)(textureData.size() / 4 / 3);

        for (int j = 0; j < rgbLength * 3; j += 4)
        {
            rgbValue.push_back(((float*)pData)[j]);
            rgbValue.push_back(((float*)pData)[j + 1]);
            rgbValue.push_back(((float*)pData)[j + 2]);
        }

        std::ofstream ofs("LT_Cloud_Local_Color_HWcarLatestPT_4096_mesh" + std::to_string(id / 4) + "_test.dat", std::ios::binary | std::ios::app | std::ios::out);
        ofs.write((const char*)rgbValue.data(), sizeof(float) * rgbValue.size());
        ofs.close();
    }
}

void LightComputer::StoreColorLTWithDiffuse(const Texture& mpBRDFPerMaterial, uint id, const float cosine[49152])
{
    for (size_t i = 0; i < 1024; i++) // pointId
    {
        if (id * 1024u + i >= 16384)
        {
            return;
        }
        Texture tempTex = mpBRDFPerMaterial;

        std::vector<uint8_t> textureData;
        // Prepare each wo's origin BRDF data organized by TextureArray2D.
        textureData = captureToSystemRAM(0, uint32_t(i), tempTex); // Use vector<uint_t> to avoid destruction of point.

        void* pData = (void*)textureData.data();
        std::vector<float> rgbValue;
        int rgbLength = (int)(textureData.size() / 4 / 3);

        float3 sumIr = float3(0.f);

        for (int j = 0; j < rgbLength * 3; j += 4)
        {
            rgbValue.push_back(((float*)pData)[j]);
            rgbValue.push_back(((float*)pData)[j + 1]);
            rgbValue.push_back(((float*)pData)[j + 2]);

            sumIr += float3(((float*)pData)[j], ((float*)pData)[j + 1], ((float*)pData)[j + 2]) * cosine[j / 4];
        }

        std::ofstream ofs("LT_Cloud_Local_Color_HWcar_Seat_NewTest_128_16384.dat", std::ios::binary | std::ios::app | std::ios::out);
        ofs.write((const char*)rgbValue.data(), sizeof(float) * rgbValue.size());
        ofs.close();

        std::ofstream ofs_ir("IrradianceSum_Seat_16384.txt", std::ios::app | std::ios::out);
        ofs_ir << sumIr.x << " " << sumIr.y << " " << sumIr.z << std::endl;
        ofs_ir.close();

    }
}

void LightComputer::StoreColorLT_Batch(const Texture& mpBRDFPerMaterial_128, const Texture& mpBRDFPerMaterial_64, const Texture& mpBRDFPerMaterial_32, const Texture& mpBRDFPerMaterial_16, uint id, std::vector<uint2> ResId)
{
    for (size_t i = 0; i < 1024; i++) // pointId
    {
        if (id * 1024u + i >= 65536)
        {
            return;
        }

        Texture tempTex = mpBRDFPerMaterial_128;
        if(ResId[id * 1024u + i][0] == 64) tempTex = mpBRDFPerMaterial_64;
        else if(ResId[id * 1024u + i][0] == 32) tempTex = mpBRDFPerMaterial_32;
        else if(ResId[id * 1024u + i][0] == 16) tempTex = mpBRDFPerMaterial_16;

        std::vector<uint8_t> textureData;
        // Prepare each wo's origin BRDF data organized by TextureArray2D.
        textureData = captureToSystemRAM(0, uint32_t(i), tempTex); // Use vector<uint_t> to avoid destruction of point.

        void* pData = (void*)textureData.data();
        std::vector<float> rgbValue;
        int rgbLength = (int)(textureData.size() / 4 / 3);

        for (int j = 0; j < rgbLength * 3; j += 4)
        {
            rgbValue.push_back(((float*)pData)[j]);
            rgbValue.push_back(((float*)pData)[j + 1]);
            rgbValue.push_back(((float*)pData)[j + 2]);
        }

        std::ofstream ofs("LT_Cloud_Local_Color_HWcarNewPT_adaptiveRes_65536.dat", std::ios::binary | std::ios::app | std::ios::out);
        ofs.write((const char*)rgbValue.data(), sizeof(float) * rgbValue.size());
        ofs.close();
    }
}

void LightComputer::StoreImg(const Texture& mpBRDFPerMaterial, uint batchId, uint id)
{
    Texture tempTex = mpBRDFPerMaterial;

    std::vector<uint8_t> textureData;
    // Prepare each wo's origin BRDF data organized by TextureArray2D.
    textureData = captureToSystemRAM(0, uint32_t(id), tempTex); // Use vector<uint_t> to avoid destruction of point.

    Bitmap::saveImage("./TestImg/r011/img_HWCar_128_" + std::to_string(batchId) + "_" + std::to_string(batchId * 1024 + id) + ".exr", 128, 128, Bitmap::FileFormat::ExrFile, Bitmap::ExportFlags::None, ResourceFormat::RGBA32Float, true, (void*)textureData.data());
}

// For parallel
void LightComputer::calculateLightWaveletCoefficients_QuadTree_Parallel(std::vector<std::vector<uint8_t>> textureData, int start, std::vector<uint>& brdfWoFaceCoeStartIndex, std::vector<float>& root_arr_brdf)
{
    // only use when parallel (otherwise namespace)
    uint lightWoFaceIndex = 0;

    uint startId = start * 1024;

    for (size_t i = 0; i < 1024; i++)
    {
        void* pData = (void*)(textureData[i].data());

        // save origin light values.
        int rgbLength = (int)(textureData[i].size()) / 4 / 3;
        for (size_t k = 0; k < 3; k++) // three faces.
        {
            std::vector<float3> mBRDFToSave;
            for (size_t j = 0; j < rgbLength; j += 4)
            {
                mBRDFToSave.push_back(
                    float3(
                        ((float*)pData)[k * rgbLength + j],
                        ((float*)pData)[k * rgbLength + j + 1],
                        ((float*)pData)[k * rgbLength + j + 2])
                );
            }

            std::vector<QuadTree::Cube_Cell> cell_list_brdf;
            std::vector<int> level_index_brdf;
            create_cell_list(cell_list_brdf, level_index_brdf, mBRDFToSave, 128);
            // build quad tree.
            int node_num_brdf = 0;
            float discard_brdf = 1.f;
            QuadTree::Coefficient_Node* root_brdf = process_cell(cell_list_brdf[0], cell_list_brdf, discard_brdf, node_num_brdf);

            if (root_brdf == NULL)
            {
                QuadTree::Coefficient_Node* root_brdf_temp = process_cell(cell_list_brdf[0], cell_list_brdf, 0.f, node_num_brdf);
                QuadTree::Coefficient_Node* child_index[4];
                for (size_t m = 0; m < 4; m++)
                {
                    child_index[m] = NULL;
                }
                root_brdf = new QuadTree::Coefficient_Node(root_brdf_temp->coeffs[0], root_brdf_temp->coeffs[1], root_brdf_temp->coeffs[2], child_index, root_brdf_temp->cell_global_index, root_brdf_temp->cell_avg);

                QuadTree::releaseQuadTree(root_brdf_temp);
            }

            // // rebuild image to test if true
            // QuadTree::Buffer_Static_Data data_brdf;
            // data_brdf.init(cell_list_brdf);
            // std::vector<float3> img_brdf;
            // int level = data_brdf.max_level;
            // node_decode(root_brdf, img_brdf, data_brdf, 0, level, cell_list_brdf[root_brdf->cell_global_index].color, cell_list_brdf[root_brdf->cell_global_index].img_index, cell_list_brdf[root_brdf->cell_global_index].len, cell_list_brdf[root_brdf->cell_global_index].level);
            // Bitmap::saveImage("D:/LabProject/CarExhibition/Falcor4_3/Source/Mogwai/QuadTreeBrdfCoes/level_img_" + std::to_string(k) + ".exr", data_brdf.img_len, data_brdf.img_len, Bitmap::FileFormat::ExrFile, Bitmap::ExportFlags::None, ResourceFormat::RGB32Float, true, (void*)img_brdf.data());

            if (root_brdf != NULL)
            {
                transform_into_array(root_brdf, root_arr_brdf, 16);
            }

            brdfWoFaceCoeStartIndex.push_back(lightWoFaceIndex);
            lightWoFaceIndex += (uint)(root_arr_brdf.size());

            // delete root_brdf;
            QuadTree::releaseQuadTree(root_brdf);
        }

    }

    // (only use when parallel)
    brdfWoFaceCoeStartIndex.push_back(lightWoFaceIndex);
}

