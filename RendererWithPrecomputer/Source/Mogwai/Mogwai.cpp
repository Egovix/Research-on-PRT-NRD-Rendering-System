/***************************************************************************
 # Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.
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
#include "stdafx.h"
#include "Mogwai.h"
#include "MogwaiSettings.h"

#include <args.hxx>

#include <filesystem>
#include <algorithm>

#define SE

namespace Mogwai
{
    namespace
    {
        std::map<std::string, Extension::CreateFunc>* gExtensions; // Map ensures ordering

        const std::string kEditorExecutableName = "RenderGraphEditor";
        const std::string kEditorSwitch = "--editor";
        const std::string kGraphFileSwitch = "--graph-file";
        const std::string kGraphNameSwitch = "--graph-name";

        const std::string kAppDataPath = getAppDataDirectory() + "/NVIDIA/Falcor/Mogwai.json";
    }

    size_t Renderer::DebugWindow::index = 0;

    Renderer::Renderer(const Options& options)
        : mOptions(options)
        , mAppData(kAppDataPath)
    {}

    void Renderer::extend(Extension::CreateFunc func, const std::string& name)
    {
        if (!gExtensions) gExtensions = new std::map<std::string, Extension::CreateFunc>();
        if (gExtensions->find(name) != gExtensions->end())
        {
            logError("Extension " + name + " already registered. If you continue the new extension will be discarded");
            return;
        }
        (*gExtensions)[name] = func;
    }

    void Renderer::onShutdown()
    {
        resetEditor();
        gpDevice->flushAndSync(); // Need to do that because clearing the graphs will try to release some state objects which might be in use
        mGraphs.clear();
    }

    void Renderer::onLoad(RenderContext* pRenderContext)
    {
        mpExtensions.push_back(MogwaiSettings::create(this));
        if (gExtensions)
        {
            for (auto& f : (*gExtensions)) mpExtensions.push_back(f.second(this));
            safe_delete(gExtensions);
        }

        auto regBinding = [this](pybind11::module& m) {this->registerScriptBindings(m); };
        ScriptBindings::registerBinding(regBinding);

        // Load script provided via command line.
        if (!mOptions.scriptFile.empty())
        {
            loadScript(mOptions.scriptFile);
            // Add script to recent files only if not in silent mode (which is used during image tests).
            if (!mOptions.silentMode) mAppData.addRecentScript(mOptions.scriptFile);
        }

        Scene::nullTracePass(pRenderContext, uint2(1024));
    }

    RenderGraph* Renderer::getActiveGraph() const
    {
        return mGraphs.size() ? mGraphs[mActiveGraph].pGraph.get() : nullptr;
    }

    void Renderer::onGuiRender(Gui* pGui)
    {
        for (auto& pe : mpExtensions)  pe->renderUI(pGui);
    }

    bool isInVector(const std::vector<std::string>& strVec, const std::string& str)
    {
        return std::find(strVec.begin(), strVec.end(), str) != strVec.end();
    }

    Gui::DropdownList createDropdownFromVec(const std::vector<std::string>& strVec, const std::string& currentLabel)
    {
        Gui::DropdownList dropdown;
        for (size_t i = 0; i < strVec.size(); i++) dropdown.push_back({ (uint32_t)i, strVec[i] });
        return dropdown;
    }

    void Renderer::addDebugWindow()
    {
        DebugWindow window;
        window.windowName = "Debug Window " + std::to_string(DebugWindow::index++);
        window.currentOutput = mGraphs[mActiveGraph].mainOutput;
        markOutput(window.currentOutput);
        mGraphs[mActiveGraph].debugWindows.push_back(window);
    }

    void Renderer::unmarkOutput(const std::string& name)
    {
        auto& graphData = mGraphs[mActiveGraph];
        // Skip the original outputs
        if (isInVector(graphData.originalOutputs, name)) return;

        // Decrease the reference counter
        auto& ref = graphData.graphOutputRefs.at(name);
        ref--;
        if (ref == 0)
        {
            graphData.graphOutputRefs.erase(name);
            graphData.pGraph->unmarkOutput(name);
        }
    }

    void Renderer::markOutput(const std::string& name)
    {
        auto& graphData = mGraphs[mActiveGraph];
        // Skip the original outputs
        if (isInVector(graphData.originalOutputs, name)) return;
        auto& refVec = mGraphs[mActiveGraph].graphOutputRefs;
        refVec[name]++;
        if (refVec[name] == 1) mGraphs[mActiveGraph].pGraph->markOutput(name);
    }

    void Renderer::renderOutputUI(Gui::Widgets& widget, const Gui::DropdownList& dropdown, std::string& selectedOutput)
    {
        uint32_t activeOut = -1;
        for (size_t i = 0; i < dropdown.size(); i++)
        {
            if (dropdown[i].label == selectedOutput)
            {
                activeOut = (uint32_t)i;
                break;
            }
        }

        // This can happen when `showAllOutputs` changes to false, and the chosen output is not an original output. We will force an output change
        bool forceOutputChange = activeOut == -1;
        if (forceOutputChange) activeOut = 0;

        if (widget.dropdown("Output", dropdown, activeOut) || forceOutputChange)
        {
            // Unmark old output, set new output, mark new output
            unmarkOutput(selectedOutput);
            selectedOutput = dropdown[activeOut].label;
            markOutput(selectedOutput);
        }
    }

    bool Renderer::renderDebugWindow(Gui::Widgets& widget, const Gui::DropdownList& dropdown, DebugWindow& data, const uint2& winSize)
    {
        // Get the current output, in case `renderOutputUI()` unmarks it
        Texture::SharedPtr pTex = std::dynamic_pointer_cast<Texture>(mGraphs[mActiveGraph].pGraph->getOutput(data.currentOutput));
        std::string label = data.currentOutput + "##" + mGraphs[mActiveGraph].pGraph->getName();
        if (!pTex) { logError("Invalid output resource. Is not a texture."); }

        uint2 debugSize = (uint2)(float2(winSize) * float2(0.4f, 0.55f));
        uint2 debugPos = winSize - debugSize;
        debugPos -= 10;

        // Display the dropdown
        Gui::Window debugWindow(widget.gui(), data.windowName.c_str(), debugSize, debugPos);
        if (debugWindow.gui())
        {
            if (debugWindow.button("Save To File", true)) Bitmap::saveImageDialog(pTex.get());
            renderOutputUI(widget, dropdown, data.currentOutput);
            debugWindow.separator();

            debugWindow.image(label.c_str(), pTex);
            debugWindow.release();
            return true;
        }

        return false;
    }

    void Renderer::eraseDebugWindow(size_t id)
    {
        unmarkOutput(mGraphs[mActiveGraph].debugWindows[id].currentOutput);
        mGraphs[mActiveGraph].debugWindows.erase(mGraphs[mActiveGraph].debugWindows.begin() + id);
    }

    void Renderer::graphOutputsGui(Gui::Widgets& widget)
    {
        RenderGraph::SharedPtr pGraph = mGraphs[mActiveGraph].pGraph;
        if (mGraphs[mActiveGraph].debugWindows.size()) mGraphs[mActiveGraph].showAllOutputs = true;
        auto strVec = mGraphs[mActiveGraph].showAllOutputs ? pGraph->getAvailableOutputs() : mGraphs[mActiveGraph].originalOutputs;
        Gui::DropdownList graphOuts = createDropdownFromVec(strVec, mGraphs[mActiveGraph].mainOutput);

        widget.checkbox("List All Outputs", mGraphs[mActiveGraph].showAllOutputs);
        widget.tooltip("Display every possible output in the render-graph, even if it wasn't explicitly marked as one. If there's a debug window open, you won't be able to uncheck this");

        if (graphOuts.size())
        {
            uint2 dims(gpFramework->getTargetFbo()->getWidth(), gpFramework->getTargetFbo()->getHeight());

            for (size_t i = 0; i < mGraphs[mActiveGraph].debugWindows.size();)
            {
                if (renderDebugWindow(widget, graphOuts, mGraphs[mActiveGraph].debugWindows[i], dims) == false)
                {
                    eraseDebugWindow(i);
                }
                else i++;
            }

            renderOutputUI(widget, graphOuts, mGraphs[mActiveGraph].mainOutput);

            // Render the debug windows *before* adding/removing debug windows
            if (widget.button("Show In Debug Window")) addDebugWindow();
            if (mGraphs[mActiveGraph].debugWindows.size())
            {
                if (widget.button("Close all debug windows"))
                {
                    while (mGraphs[mActiveGraph].debugWindows.size()) eraseDebugWindow(0);
                }
            }
        }
    }

    void Renderer::onDroppedFile(const std::string& filename)
    {
        std::string ext = getExtensionFromFile(filename);
        if (ext == "py")
        {
            loadScript(filename);
            mAppData.addRecentScript(filename);
        }
        else if (std::any_of(Scene::getFileExtensionFilters().begin(), Scene::getFileExtensionFilters().end(), [&ext](FileDialogFilter f) {return f.ext == ext; }))
        {
            loadScene(filename);
            mAppData.addRecentScene(filename);
        }
        else
        {
            logWarning("RenderGraphViewer::onDroppedFile() - Unknown file extension '" + ext + "'");
        }
    }

    void Renderer::editorFileChangeCB()
    {
        mEditorScript = readFile(mEditorTempFile);
    }

    void Renderer::openEditor()
    {
        bool unmarkOut = (isInVector(mGraphs[mActiveGraph].originalOutputs, mGraphs[mActiveGraph].mainOutput) == false);
        // If the current graph output is not an original output, unmark it
        if (unmarkOut) mGraphs[mActiveGraph].pGraph->unmarkOutput(mGraphs[mActiveGraph].mainOutput);

        mEditorTempFile = getTempFilename();

        // Save the graph
        RenderGraphExporter::save(mGraphs[mActiveGraph].pGraph, mEditorTempFile);

        // Register an update callback
        monitorFileUpdates(mEditorTempFile, std::bind(&Renderer::editorFileChangeCB, this));

        // Run the process
        std::string commandLineArgs = kEditorSwitch + " " + kGraphFileSwitch + " " + mEditorTempFile + " " + kGraphNameSwitch + " " + mGraphs[mActiveGraph].pGraph->getName();
        mEditorProcess = executeProcess(kEditorExecutableName, commandLineArgs);

        // Mark the output if it's required
        if (unmarkOut) mGraphs[mActiveGraph].pGraph->markOutput(mGraphs[mActiveGraph].mainOutput);
    }

    void Renderer::resetEditor()
    {
        if (mEditorProcess)
        {
            closeSharedFile(mEditorTempFile);
            std::remove(mEditorTempFile.c_str());
            if (mEditorProcess != kInvalidProcessId)
            {
                terminateProcess(mEditorProcess);
                mEditorProcess = 0;
            }
        }
    }

    void Renderer::setActiveGraph(uint32_t active)
    {
        RenderGraph* pOld = getActiveGraph();
        mActiveGraph = active;
        RenderGraph* pNew = getActiveGraph();
        if (pOld != pNew)
        {
            for (auto& e : mpExtensions) e->activeGraphChanged(pNew, pOld);
        }
    }

    void Renderer::removeGraph(const RenderGraph::SharedPtr& pGraph)
    {
        for (auto& e : mpExtensions) e->removeGraph(pGraph.get());
        size_t i = 0;
        for (; i < mGraphs.size(); i++) if (mGraphs[i].pGraph == pGraph) break;
        assert(i < mGraphs.size());
        mGraphs.erase(mGraphs.begin() + i);
        if (mActiveGraph >= i && mActiveGraph > 0) mActiveGraph--;
        setActiveGraph(mActiveGraph);
    }

    void Renderer::removeGraph(const std::string& graphName)
    {
        auto pGraph = getGraph(graphName);
        if (pGraph) removeGraph(pGraph);
        else logError("Can't find a graph named '" + graphName + "'. There's nothing to remove.");
    }

    RenderGraph::SharedPtr Renderer::getGraph(const std::string& graphName) const
    {
        for (const auto& g : mGraphs)
        {
            if (g.pGraph->getName() == graphName) return g.pGraph;
        }
        return nullptr;
    }

    void Renderer::removeActiveGraph()
    {
        if (mGraphs.size()) removeGraph(mGraphs[mActiveGraph].pGraph);
    }

    std::vector<std::string> Renderer::getGraphOutputs(const RenderGraph::SharedPtr& pGraph)
    {
        std::vector<std::string> outputs;
        for (size_t i = 0; i < pGraph->getOutputCount(); i++) outputs.push_back(pGraph->getOutputName(i));
        return outputs;
    }

    void Renderer::initGraph(const RenderGraph::SharedPtr& pGraph, GraphData* pData)
    {
        if (!pData)
        {
            mGraphs.push_back({});
            pData = &mGraphs.back();
        }

        GraphData& data = *pData;
        // Set input image if it exists
        data.pGraph = pGraph;
        data.pGraph->setScene(mpScene);
        if (data.pGraph->getOutputCount() != 0) data.mainOutput = data.pGraph->getOutputName(0);

        // Store the original outputs
        data.originalOutputs = getGraphOutputs(pGraph);

        for (auto& e : mpExtensions) e->addGraph(pGraph.get());
    }

    void Renderer::loadScriptDialog()
    {
        std::string filename;
        if (openFileDialog(Scripting::kFileExtensionFilters, filename))
        {
            loadScriptDeferred(filename);
            mAppData.addRecentScript(filename);
        }
    }

    void Renderer::loadScriptDeferred(const std::string& filename)
    {
        mScriptFilename = filename;
    }

    void Renderer::loadScript(const std::string& filename)
    {
        assert(filename.size());

        try
        {
            if (ProgressBar::isActive()) ProgressBar::show("Loading Configuration");

            // Add script directory to search paths (add it to the front to make it highest priority).
            const std::string directory = getDirectoryFromFile(filename);
            addDataDirectory(directory, true);

            Scripting::runScriptFromFile(filename);

            removeDataDirectory(directory);
        }
        catch (const std::exception& e)
        {
            logError("Error when loading configuration file: " + filename + "\n" + std::string(e.what()));
        }
    }

    void Renderer::saveConfigDialog()
    {
        std::string filename;
        if (saveFileDialog(Scripting::kFileExtensionFilters, filename))
        {
            saveConfig(filename);
            mAppData.addRecentScript(filename);
        }
    }

    void Renderer::addGraph(const RenderGraph::SharedPtr& pGraph)
    {
        if (pGraph == nullptr)
        {
            logError("Can't add an empty graph");
            return;
        }

        // If a graph with the same name already exists, remove it
        GraphData* pGraphData = nullptr;
        for (size_t i = 0; i < mGraphs.size(); i++)
        {
            if (mGraphs[i].pGraph->getName() == pGraph->getName())
            {
                logWarning("Replacing existing graph '" + pGraph->getName() + "' with new graph.");
                pGraphData = &mGraphs[i];
                break;
            }
        }
        initGraph(pGraph, pGraphData);
    }

    void Renderer::loadSceneDialog()
    {
        std::string filename;
        if (openFileDialog(Scene::getFileExtensionFilters(), filename))
        {
            loadScene(filename);
            mAppData.addRecentScene(filename);
        }
    }

    void Renderer::loadScene(std::string filename, SceneBuilder::Flags buildFlags)
    {
        TimeReport timeReport;

#ifdef SE
        buildFlags |= SceneBuilder::Flags::Force32BitIndices;
#endif

        SceneBuilder::SharedPtr pBuilder = SceneBuilder::create(filename, buildFlags);
        if (!pBuilder) return;

#ifdef SE
        // int input_num = 81930;
        // int output_num = 16386; // Steering wheel
        // int input_num = 81930;
        // int output_num = 16386; // One Seat
        // int input_num = 40970;
        // int output_num = 8194;
        // int input_num = 163850;
        // int output_num = 32770;
        //int input_num = 65600*5;
        //int output_num = 65600;
        // int input_num = 82000 * 5;
        // int output_num = 82000;
        
        // int input_num = 5120;
        // int output_num = 1024;
        // int input_num = 81920;
        // int output_num = 16384;
        int input_num = 20480;
        int output_num = 4096;
        // int input_num = 163840;
        // int output_num = 32768;
        // int input_num = 40960;
        // int output_num = 8192;
        // int input_num = 163840;
        // int output_num = 32768;
        // int input_num = 327680;
        // int output_num = 65536;

        uint32_t mesh_num = pBuilder->getMeshNum();
        std::vector<std::vector<float>> Area(mesh_num);//triangle area

        std::vector<std::vector<uint32_t>> index_data(mesh_num);
        std::vector<std::vector<StaticVertexData>> vertex_data(mesh_num);
        pBuilder->getData(index_data, vertex_data);

        //Calculate triangle area.
        for (uint32_t i = 0; i < mesh_num; i++) {
            uint32_t index_num = (uint32_t)index_data[i].size();
            std::vector<float> m_area(index_num / 3);
            for (uint32_t j = 0; j < index_num; j += 3) {
                auto v1 = vertex_data[i][index_data[i][j]].position - vertex_data[i][index_data[i][j + 1]].position;
                auto v2 = vertex_data[i][index_data[i][j]].position - vertex_data[i][index_data[i][j + 2]].position;
                float vx = v1.y * v2.z - v1.z * v2.y;
                float vy = v1.z * v2.x - v1.x * v2.z;
                float vz = v1.x * v2.y - v1.y * v2.x;
                m_area[j / 3] = sqrt(vx * vx + vy * vy + vz * vz);

                // // adaptive increase
                // if (i == 2)
                // {
                //     m_area[j / 3] *= 5;
                // }
            }
            Area[i] = std::move(m_area);
        }



        std::vector<std::vector<size_t>> faceidx(mesh_num);
        std::vector<std::vector<cy::Point3f>> barycentric(mesh_num);
        std::vector<uint2> res_id;
        // uint numPerMesh = 4096;
        
        // for (uint mesh_id = 0; mesh_id < mesh_num; mesh_id++)
        for (uint mesh_id = 42; mesh_id < 45; mesh_id++)
        {
            srand(1);
            if (mesh_id >= 79 && mesh_id <= 100) continue;

            std::vector<std::vector<float>> area(Area);

            //Calculate sample number of each mesh.
            std::vector<std::vector<cy::Point3f>> inputPoints(mesh_num);
            std::vector<std::vector<cy::Point3f>> outputPoints(mesh_num);
            std::vector<float> total_area(mesh_num);
            for (uint32_t i = 0; i < mesh_num; i++) {
                total_area[i] = 0.;
                // car01 37  24      78     78 and 79  69   10
                // if (!(i == 8 || i == 9 || i == 10 || i == 11 || i == 12 || i == 28 || i == 74))
                // if(i != 69)
                // 
                // if(i != 42 && i != 43 && i != 44)
                // {
                //     continue;
                // }
                 
                if (i != mesh_id)
                // if (i != 44)
                {
                    continue;
                }

                // if (i < 6 || i > 11)
                // {
                //     continue;
                // }
                // if (i >= 79 && i <= 100)
                // {
                //     continue;
                // }
                for (auto a : area[i]) {
                    total_area[i] += a;
                }
            }
            std::vector<size_t> in_num_perMesh(mesh_num), out_num_perMesh(mesh_num);
            auto total_area_total = 0.;
            for (auto ta : total_area) {
                total_area_total += ta;
            }
            for (uint32_t i = 0; i < mesh_num; i++) {
                in_num_perMesh[i] = (size_t)std::ceil(input_num * total_area[i] / total_area_total);
                out_num_perMesh[i] = (size_t)std::floor(output_num * total_area[i] / total_area_total);
                inputPoints[i].resize(in_num_perMesh[i]);
                outputPoints[i].resize(out_num_perMesh[i]);
            }

            //Allocate weights according to area.
            std::vector<float> max_area;
            for (auto& a : area) {
                max_area.push_back(*max_element(a.begin(), a.end()));
            }
            for (int i = 0; i < area.size(); i++) {
                std::transform(area[i].begin(), area[i].end(), area[i].begin(), [&max_area, &i](float a) {return ceil(a * 100 / max_area[i]); });
            }

            std::vector<std::vector<int>> face_weights(mesh_num);
            for (uint32_t i = 0; i < mesh_num; i++) {
                std::vector<int> m_weights;
                for (auto j = 0; j < area[i].size(); j++) {
                    for (int k = 0; k < (int)area[i][j]; k++) {
                        m_weights.push_back(j);
                    }
                }
                face_weights[i] = std::move(m_weights);
            }

            //cySampleElim for every mesh.
            std::vector<std::vector<cy::Point3f>> in_barycentric(inputPoints);
            std::vector<std::vector<cy::Point3f>> out_barycentric(outputPoints);
            std::vector<std::vector<size_t>> in_faceidx(mesh_num), out_faceidx(mesh_num);
            for (uint32_t i = 0; i < mesh_num; i++) {
                in_faceidx[i].resize(in_num_perMesh[i]);
                out_faceidx[i].resize(out_num_perMesh[i]);
                for (size_t j = 0; j < in_num_perMesh[i]; j++) {
                    size_t face_idx = face_weights[i][int((float)rand() * (face_weights[i].size() - 1) / RAND_MAX)];
                    float u = (float)rand() / RAND_MAX;
                    float v = (float)rand() / RAND_MAX;
                    if (u + v > 1) {
                        u = 1 - u;
                        v = 1 - v;
                    }
                    in_faceidx[i][j] = face_idx;
                    in_barycentric[i][j].x = u;
                    in_barycentric[i][j].y = v;
                    in_barycentric[i][j].z = 1 - u - v;

                    inputPoints[i][j].x = u * vertex_data[i][index_data[i][face_idx * 3]].position.x + v * vertex_data[i][index_data[i][face_idx * 3 + 1]].position.x + (1 - u - v) * vertex_data[i][index_data[i][face_idx * 3 + 2]].position.x;
                    inputPoints[i][j].y = u * vertex_data[i][index_data[i][face_idx * 3]].position.y + v * vertex_data[i][index_data[i][face_idx * 3 + 1]].position.y + (1 - u - v) * vertex_data[i][index_data[i][face_idx * 3 + 2]].position.y;
                    inputPoints[i][j].z = u * vertex_data[i][index_data[i][face_idx * 3]].position.z + v * vertex_data[i][index_data[i][face_idx * 3 + 1]].position.z + (1 - u - v) * vertex_data[i][index_data[i][face_idx * 3 + 2]].position.z;
                }

                cy::WeightedSampleElimination<cy::Point3f, float, 3> wse;

                float d_max = 2 * wse.GetMaxPoissonDiskRadius(2, static_cast<int>(outputPoints[i].size()), total_area[i]);
                wse.Eliminate(inputPoints[i].data(), static_cast<int>(inputPoints[i].size()), in_barycentric[i].data(), in_faceidx[i].data(),
                    outputPoints[i].data(), static_cast<int>(outputPoints[i].size()), out_barycentric[i].data(), out_faceidx[i].data(),
                    false, d_max, 2);

            }

            // uint res16_id = 0;
            // uint res128_id = 0;
            // std::vector<uint2> id_res;

            faceidx[mesh_id].assign(out_faceidx[mesh_id].begin(), out_faceidx[mesh_id].end());
            barycentric[mesh_id].assign(out_barycentric[mesh_id].begin(), out_barycentric[mesh_id].end());

            // std::vector<uint2> res_id;
            // uint curId = numPerMesh * meshCount;
            uint curId = 0;
            for (uint32_t i = 0; i < mesh_num; i++)
            {
                // if (pBuilder->meshIsDiffuse(i))
                // {
                //     for (uint32_t j = 0; j < out_num_perMesh[i]; j++)
                //     {
                //         id_res.push_back(uint2(16, res16_id));
                //         res16_id++;
                //     }
                // }
                // else
                // {
                //     for (uint32_t j = 0; j < out_num_perMesh[i]; j++)
                //     {
                //         id_res.push_back(uint2(128, res128_id));
                //         res128_id++;
                //     }
                // }

                uint curRes = pBuilder->meshNeedRes(i);
                // if (curRes != 256)
                if (curRes == 128)
                {
                    for (uint32_t j = 0; j < out_num_perMesh[i]; j++)
                    {
                        res_id.push_back(uint2(128, curId));
                        curId++;
                    }
                }
                else if (curRes == 64)
                {
                    for (uint32_t j = 0; j < out_num_perMesh[i]; j++)
                    {
                        res_id.push_back(uint2(64, curId));
                        curId++;
                    }
                }
                else if (curRes == 32)
                {
                    for (uint32_t j = 0; j < out_num_perMesh[i]; j++)
                    {
                        res_id.push_back(uint2(32, curId));
                        curId++;
                    }
                }
                else
                {
                    for (uint32_t j = 0; j < out_num_perMesh[i]; j++)
                    {
                        res_id.push_back(uint2(16, curId));
                        curId++;
                    }
                }
            }

            // std::fstream f;
            // f.open("pointsLocation_HuaWeiCar_Seat_16384.txt", std::ios::out);
            // uint count = 0;
            // //f << res16_count;
            // for (uint32_t i = 0; i < mesh_num; i++) {
            //     for (size_t j = 0; j < out_num_perMesh[i]; j++) {
            //         //a b c: vertex of triangle containing this sample
            //         auto& a = vertex_data[i][index_data[i][out_faceidx[i][j] * 3]];
            //         auto& b = vertex_data[i][index_data[i][out_faceidx[i][j] * 3 + 1]];
            //         auto& c = vertex_data[i][index_data[i][out_faceidx[i][j] * 3 + 2]];
            //         //out_barycentric[i][j]: barycentric coordinate of this sample
            //         auto x = a.position.x * out_barycentric[i][j].x + b.position.x * out_barycentric[i][j].y + c.position.x * out_barycentric[i][j].z;
            //         auto y = a.position.y * out_barycentric[i][j].x + b.position.y * out_barycentric[i][j].y + c.position.y * out_barycentric[i][j].z;
            //         auto z = a.position.z * out_barycentric[i][j].x + b.position.z * out_barycentric[i][j].y + c.position.z * out_barycentric[i][j].z;
            // 
            //         auto normal_x = a.normal.x * out_barycentric[i][j].x + b.normal.x * out_barycentric[i][j].y + c.normal.x * out_barycentric[i][j].z;
            //         auto normal_y = a.normal.y * out_barycentric[i][j].x + b.normal.y * out_barycentric[i][j].y + c.normal.y * out_barycentric[i][j].z;
            //         auto normal_z = a.normal.z * out_barycentric[i][j].x + b.normal.z * out_barycentric[i][j].y + c.normal.z * out_barycentric[i][j].z;
            // 
            //         // f   << i << ' ' << out_faceidx[i][j] << ' ' << id_res[count].x << ' ' << id_res[count].y << ' '
            //         //     << out_barycentric[i][j].x << ' ' << out_barycentric[i][j].y << ' ' << out_barycentric[i][j].z << ' '
            //         //     << x << ' ' << y << ' ' << z << std::endl;
            //         f << x << ' ' << y << ' ' << z << ' ' << normal_x << ' ' << normal_y << ' ' << normal_z << std::endl;
            //         count++;
            //     }
            // }
            // 
            // f.close();

        }

        std::fstream f;
        f.open("pointsInfo_HuaWeiCar_4096_allMesh.txt", std::ios::out);
        uint count = 0;
        //f << res16_count;
        for (uint32_t i = 42; i < 45; i++) {
            for (size_t j = 0; j < 4096; j++) {
                //a b c: vertex of triangle containing this sample
                auto& a = vertex_data[i][index_data[i][faceidx[i][j] * 3]];
                auto& b = vertex_data[i][index_data[i][faceidx[i][j] * 3 + 1]];
                auto& c = vertex_data[i][index_data[i][faceidx[i][j] * 3 + 2]];
                //out_barycentric[i][j]: barycentric coordinate of this sample
                auto x = a.position.x * barycentric[i][j].x + b.position.x * barycentric[i][j].y + c.position.x * barycentric[i][j].z;
                auto y = a.position.y * barycentric[i][j].x + b.position.y * barycentric[i][j].y + c.position.y * barycentric[i][j].z;
                auto z = a.position.z * barycentric[i][j].x + b.position.z * barycentric[i][j].y + c.position.z * barycentric[i][j].z;
        
                auto normal_x = a.normal.x * barycentric[i][j].x + b.normal.x * barycentric[i][j].y + c.normal.x * barycentric[i][j].z;
                auto normal_y = a.normal.y * barycentric[i][j].x + b.normal.y * barycentric[i][j].y + c.normal.y * barycentric[i][j].z;
                auto normal_z = a.normal.z * barycentric[i][j].x + b.normal.z * barycentric[i][j].y + c.normal.z * barycentric[i][j].z;
        
                // f   << i << ' ' << out_faceidx[i][j] << ' ' << id_res[count].x << ' ' << id_res[count].y << ' '
                //     << out_barycentric[i][j].x << ' ' << out_barycentric[i][j].y << ' ' << out_barycentric[i][j].z << ' '
                //     << x << ' ' << y << ' ' << z << std::endl;
                f << i << ' ' << faceidx[i][j] << ' ' << res_id[count].x << ' ' << res_id[count].y << ' '
                    << barycentric[i][j].x << ' ' << barycentric[i][j].y << ' ' << barycentric[i][j].z << ' '
                    << x << ' ' << y << ' ' << z << std::endl;
                // f << x << ' ' << y << ' ' << z << ' ' << normal_x << ' ' << normal_y << ' ' << normal_z << std::endl;
                count++;
            }
        }
        
        f.close();



        // setScene(pBuilder->getScene(), out_faceidx, out_barycentric, res_id);
        setScene(pBuilder->getScene(), faceidx, barycentric, res_id);
#else
        setScene(pBuilder->getScene());
#endif

        timeReport.measure("Loading scene (total)");
        timeReport.printToLog();
    }

    void Renderer::unloadScene()
    {
        setScene(nullptr);
    }

    void Renderer::setScene(const Scene::SharedPtr& pScene)
    {
        mpScene = pScene;

        if (mpScene)
        {
            const auto& pFbo = gpFramework->getTargetFbo();
            float ratio = float(pFbo->getWidth()) / float(pFbo->getHeight());
            mpScene->setCameraAspectRatio(ratio);

            if (mpSampler == nullptr)
            {
                // create common texture sampler
                Sampler::Desc desc;
                desc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear);
                desc.setMaxAnisotropy(8);
                mpSampler = Sampler::create(desc);
            }
            mpScene->bindSamplerToMaterials(mpSampler);
        }

        for (auto& g : mGraphs) g.pGraph->setScene(mpScene);
        gpFramework->getGlobalClock().setTime(0);
    }

    void Renderer::setScene(const Scene::SharedPtr& pScene, std::vector<std::vector<size_t>> face, std::vector<std::vector<cy::Point3f>> bary, std::vector<uint2> id_res)
    {
        mpScene = pScene;

        if (mpScene)
        {
            const auto& pFbo = gpFramework->getTargetFbo();
            float ratio = float(pFbo->getWidth()) / float(pFbo->getHeight());
            mpScene->setCameraAspectRatio(ratio);

            if (mpSampler == nullptr)
            {
                // create common texture sampler
                Sampler::Desc desc;
                desc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Linear);
                desc.setMaxAnisotropy(8);
                mpSampler = Sampler::create(desc);
            }
            mpScene->bindSamplerToMaterials(mpSampler);
        }

        for (auto& g : mGraphs) g.pGraph->setScene(mpScene, face, bary, id_res);
        gpFramework->getGlobalClock().setTime(0);
    }

    Scene::SharedPtr Renderer::getScene() const
    {
        return mpScene;
    }

    void Renderer::applyEditorChanges()
    {
        if (!mEditorProcess) return;
        // If the editor was closed, reset the handles
        if ((mEditorProcess != kInvalidProcessId) && isProcessRunning(mEditorProcess) == false) resetEditor();

        if (mEditorScript.empty()) return;

        // Unmark the current output if it wasn't originally marked
        auto pActiveGraph = mGraphs[mActiveGraph].pGraph;
        bool hasUnmarkedOut = (isInVector(mGraphs[mActiveGraph].originalOutputs, mGraphs[mActiveGraph].mainOutput) == false);
        if (hasUnmarkedOut) pActiveGraph->unmarkOutput(mGraphs[mActiveGraph].mainOutput);

        // Run the scripting
        // TODO: Rendergraph scripts should be executed in an isolated scripting context.
        Scripting::getDefaultContext().setObject("g", pActiveGraph);
        Scripting::runScript(mEditorScript);

        // Update the list of marked outputs
        mGraphs[mActiveGraph].originalOutputs = getGraphOutputs(pActiveGraph);

        // If the output before the update was not initially marked but still exists, re-mark it.
        // If it no longer exists, mark a new output from the list of currently marked outputs.
        if (hasUnmarkedOut && isInVector(pActiveGraph->getAvailableOutputs(), mGraphs[mActiveGraph].mainOutput))
        {
            pActiveGraph->markOutput(mGraphs[mActiveGraph].mainOutput);
        }
        else if (isInVector(mGraphs[mActiveGraph].originalOutputs, mGraphs[mActiveGraph].mainOutput) == false)
        {
            mGraphs[mActiveGraph].mainOutput = mGraphs[mActiveGraph].originalOutputs[0];
        }

        mEditorScript.clear();
    }

    void Renderer::executeActiveGraph(RenderContext* pRenderContext)
    {
        if (mGraphs.empty()) return;
        auto& pGraph = mGraphs[mActiveGraph].pGraph;

        // Execute graph.
        (*pGraph->getPassesDictionary())[kRenderPassRefreshFlags] = RenderPassRefreshFlags::None;
        pGraph->execute(pRenderContext);
    }

    void Renderer::beginFrame(RenderContext* pRenderContext, const Fbo::SharedPtr& pTargetFbo)
    {
        for (auto& pe : mpExtensions)  pe->beginFrame(pRenderContext, pTargetFbo);
    }

    void Renderer::endFrame(RenderContext* pRenderContext, const Fbo::SharedPtr& pTargetFbo)
    {
        for (auto& pe : mpExtensions) pe->endFrame(pRenderContext, pTargetFbo);
    }

    void Renderer::onFrameRender(RenderContext* pRenderContext, const Fbo::SharedPtr& pTargetFbo)
    {
        if(mScriptFilename.size())
        {
            std::string s = mScriptFilename;
            mScriptFilename.clear();
            loadScript(s);
        }

        applyEditorChanges();

        if (mActiveGraph < mGraphs.size())
        {
            auto& pGraph = mGraphs[mActiveGraph].pGraph;
            pGraph->compile(pRenderContext);
        }

        beginFrame(pRenderContext, pTargetFbo);

        // Clear frame buffer.
        const float4 clearColor(0.38f, 0.52f, 0.10f, 1);
        pRenderContext->clearFbo(pTargetFbo.get(), clearColor, 1.0f, 0, FboAttachmentType::All);

        if (mActiveGraph < mGraphs.size())
        {
            auto& pGraph = mGraphs[mActiveGraph].pGraph;

            // Update scene and camera.
            if (mpScene)
            {
                mpScene->update(pRenderContext, gpFramework->getGlobalClock().getTime());
            }

            executeActiveGraph(pRenderContext);

            // Blit main graph output to frame buffer.
            if (mGraphs[mActiveGraph].mainOutput.size())
            {
                Texture::SharedPtr pOutTex = std::dynamic_pointer_cast<Texture>(pGraph->getOutput(mGraphs[mActiveGraph].mainOutput));
                assert(pOutTex);
                pRenderContext->blit(pOutTex->getSRV(), pTargetFbo->getRenderTargetView(0));
            }
        }

        endFrame(pRenderContext, pTargetFbo);
    }

    bool Renderer::onMouseEvent(const MouseEvent& mouseEvent)
    {
        for (auto& pe : mpExtensions)
        {
            if (pe->mouseEvent(mouseEvent)) return true;
        }

        if (mGraphs.size()) mGraphs[mActiveGraph].pGraph->onMouseEvent(mouseEvent);
        return mpScene ? mpScene->onMouseEvent(mouseEvent) : false;
    }

    bool Renderer::onKeyEvent(const KeyboardEvent& keyEvent)
    {
        for (auto& pe : mpExtensions)
        {
            if (pe->keyboardEvent(keyEvent)) return true;
        }
        if (mGraphs.size()) mGraphs[mActiveGraph].pGraph->onKeyEvent(keyEvent);
        return mpScene ? mpScene->onKeyEvent(keyEvent) : false;
    }

    void Renderer::onResizeSwapChain(uint32_t width, uint32_t height)
    {
        for (auto& g : mGraphs)
        {
            g.pGraph->onResize(gpFramework->getTargetFbo().get());
            Scene::SharedPtr graphScene = g.pGraph->getScene();
            if (graphScene) graphScene->setCameraAspectRatio((float)width / (float)height);
        }
        if (mpScene) mpScene->setCameraAspectRatio((float)width / (float)height);
    }

    void Renderer::onHotReload(HotReloadFlags reloaded)
    {
        RenderPassLibrary::instance().reloadLibraries(gpFramework->getRenderContext());
        RenderGraph* pActiveGraph = getActiveGraph();
        if (pActiveGraph) pActiveGraph->onHotReload(reloaded);
    }

    size_t Renderer::findGraph(std::string_view name)
    {
        for (size_t i = 0; i < mGraphs.size(); i++)
        {
            if (mGraphs[i].pGraph->getName() == name) return i;
        };
        return -1;
    }

    std::string Renderer::getVersionString()
    {
        return "Mogwai " + std::to_string(kMajorVersion) + "." + std::to_string(kMinorVersion);
    }
}

int main(int argc, char** argv)
{
    args::ArgumentParser parser("Mogwai render application.");
    parser.helpParams.programName = "Mogwai";
    args::HelpFlag helpFlag(parser, "help", "Display this help menu.", {'h', "help"});
    args::ValueFlag<std::string> scriptFlag(parser, "path", "Python script file to run.", {'s', "script"});
    args::ValueFlag<std::string> logfileFlag(parser, "path", "File to write log into.", {'l', "logfile"});
    args::ValueFlag<int32_t> verbosityFlag(parser, "verbosity", "Logging verbosity (0=disabled, 1=fatal errors, 2=errors, 3=warnings, 4=infos, 5=debugging)", { 'v', "verbosity" }, 4);
    args::Flag silentFlag(parser, "", "Starts Mogwai with a minimized window and disables mouse/keyboard input as well as error message dialogs.", {"silent"});
    args::ValueFlag<uint32_t> widthFlag(parser, "pixels", "Initial window width.", {"width"});
    args::ValueFlag<uint32_t> heightFlag(parser, "pixels", "Initial window height.", {"height"});
    args::CompletionFlag completionFlag(parser, {"complete"});

    try
    {
        parser.ParseCLI(argc, argv);
    }
    catch (const args::Completion& e)
    {
        std::cout << e.what();
        return 0;
    }
    catch (const args::Help&)
    {
        std::cout << parser;
        return 0;
    }
    catch (const args::ParseError& e)
    {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    }
    catch (const args::RequiredError& e)
    {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    }

    int32_t verbosity = args::get(verbosityFlag);

    if (verbosity < 0 || verbosity >= (int32_t)Logger::Level::Count)
    {
        std::cerr << argv[0] << ": invalid verbosity level " << verbosity << std::endl;
        return 1;
    }

    Logger::setVerbosity((Logger::Level)verbosity);
    Logger::logToConsole(true);

    if (logfileFlag)
    {
        std::string logfile = args::get(logfileFlag);
        Logger::setLogFilePath(logfile);
    }

    Mogwai::Renderer::Options options;

    if (scriptFlag) options.scriptFile = args::get(scriptFlag);
    if (silentFlag) options.silentMode = true;

    try
    {
        msgBoxTitle("Mogwai");

        IRenderer::UniquePtr pRenderer = std::make_unique<Mogwai::Renderer>(options);
        SampleConfig config;
        config.windowDesc.title = "Mogwai";

        if (silentFlag)
        {
            config.suppressInput = true;
            config.showMessageBoxOnError = false;
            config.windowDesc.mode = Window::WindowMode::Minimized;

            // Set early to not show message box on errors that occur before setting the sample configuration.
            Logger::showBoxOnError(false);
        }

        if (widthFlag) config.windowDesc.width = args::get(widthFlag);
        if (heightFlag) config.windowDesc.height = args::get(heightFlag);

        Sample::run(config, pRenderer, 0, nullptr);
    }
    catch (const std::exception& e)
    {
        // Note: This can only trigger from the setup code above. Sample::run() handles all exceptions internally.
        logFatal("Mogwai crashed unexpectedly...\n" + std::string(e.what()));
    }
    return 0;
}
