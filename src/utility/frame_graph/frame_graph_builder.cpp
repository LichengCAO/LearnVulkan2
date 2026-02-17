#include "frame_graph_builder.h"
#include "frame_graph.h"
#include "vulkan_struct_util.h"

FrameGraphPassBind& FrameGraphPassBind::BindInAttachment(uint32_t inAttachmentIndex, const std::string& inName)
{
    Data data{};

    data.id = inAttachmentIndex;
    data.name = inName;

    m_data.push_back(std::move(data));

    return *this;
}

FrameGraphPassBind& FrameGraphPassBind::BindInoutAttachment(uint32_t inAttachmentIndex, const std::string& inName)
{
    Data data{};

    data.id = inAttachmentIndex;
    data.name = inName;

    m_data.push_back(std::move(data));

    return *this;
}

FrameGraphPassBind& FrameGraphPassBind::BindOutAttachment(
    uint32_t inAttachmentIndex, 
    const std::string& inName, 
    const FrameGraphBufferHandle& inHandle)
{
    Data data{};

    data.id = inAttachmentIndex;
    data.name = inName;
    data.handle = inHandle;

    m_data.push_back(std::move(data));

    return *this;
}

FrameGraphPassBind& FrameGraphPassBind::BindOutAttachment(
    uint32_t inAttachmentIndex, 
    const std::string& inName, 
    const FrameGraphImageHandle& inHandle)
{
    Data data{};

    data.id = inAttachmentIndex;
    data.name = inName;
    data.handle = inHandle;

    m_data.push_back(std::move(data));

    return *this;
}

FrameGraphPassBind& FrameGraphPassBind::BindTransientAttachment(
    uint32_t inAttachmentIndex, 
    const std::string& inName, 
    const FrameGraphBufferHandle& inHandle)
{
    Data data{};

    data.id = inAttachmentIndex;
    data.name = inName;
    data.handle = inHandle;

    m_data.push_back(std::move(data));

    return *this;
}

FrameGraphPassBind& FrameGraphPassBind::BindTransientAttachment(
    uint32_t inAttachmentIndex, 
    const std::string& inName, 
    const FrameGraphImageHandle& inHandle)
{
    Data data{};

    data.id = inAttachmentIndex;
    data.name = inName;
    data.handle = inHandle;

    m_data.push_back(std::move(data));

    return *this;
}

FrameGraphPassBind& FrameGraphPassBind::SetQueueType(FrameGraphQueueType inType)
{
    m_type = inType;

    return *this;
}

FrameGraphBuilder::NodeBlueprint* FrameGraphBuilder::_GetNodeBlueprint(FrameGraphNodeHandle inHandle)
{
    return m_nodeBlueprints.at(inHandle.handle).get();
}

FrameGraphBuilder::ImageBlueprint* FrameGraphBuilder::_GetImageBlueprint(FrameGraphImageHandle inHandle)
{
    size_t blueprintIndex = m_handleToImageBlueprint.at(inHandle);
    return m_imageBlueprints.at(blueprintIndex).get();
}

FrameGraphBuilder::BufferBlueprint* FrameGraphBuilder::_GetBufferBlueprint(FrameGraphBufferHandle inHandle)
{
    size_t blueprintIndex = m_handleToBufferBlueprint.at(inHandle);
    return m_bufferBlueprints.at(blueprintIndex).get();
}

auto FrameGraphBuilder::_GetResourceState(FrameGraphImageHandle inHandle) -> FrameGraphImageResourceState*
{
    FrameGraphImageResourceState* result = nullptr;
    auto blueprint = _GetImageBlueprint(inHandle);

    if (blueprint && blueprint->HaveResourceAssigned(inHandle))
    {
        size_t index = blueprint->handleToIndex[inHandle];
        result = blueprint->states[index].get();
    }

    CHECK_TRUE(result);

    return result;
}

auto FrameGraphBuilder::_GetResourceState(FrameGraphBufferHandle inHandle) -> FrameGraphBufferResourceState*
{
    FrameGraphBufferResourceState* result = nullptr;
    auto blueprint = _GetBufferBlueprint(inHandle);

    if (blueprint && blueprint->HaveResourceAssigned(inHandle))
    {
        size_t index = blueprint->handleToIndex[inHandle];
        result = blueprint->states[index].get();
    }

    CHECK_TRUE(result);

    return result;
}

void FrameGraphBuilder::_TopologicalSort(std::vector<std::set<NodeBlueprint*>>& outBatches)
{
    std::vector<size_t> indegree(m_nodeBlueprints.size(), 0);
    std::queue<size_t> processQueue;
    std::unordered_map<NodeBlueprint*, size_t> ptrToIndex;

    for (size_t i = 0; i < m_nodeBlueprints.size(); ++i)
    {
        auto currentNode = m_nodeBlueprints[i].get();
        std::set<NodeBlueprint*> prevs{};

        ptrToIndex[currentNode] = i;
        currentNode->GetFullPrev(prevs);
        indegree[i] = prevs.size();

        if (indegree[i] == 0)
        {
            processQueue.push(i);
        }
    }

    while (!processQueue.empty())
    {
        size_t n = processQueue.size();
        
        outBatches.push_back({});
        
        std::set<NodeBlueprint*>& currentBatch = outBatches.back();
        for (size_t i = 0; i < n; ++i)
        {
            auto currentNode = m_nodeBlueprints[processQueue.front()].get();
            std::set<NodeBlueprint*> nexts{};
            
            currentBatch.insert(currentNode);
            processQueue.pop();
            currentNode->GetFullNext(nexts);
            for (auto nextNode : nexts)
            {
                size_t index = ptrToIndex.at(nextNode);

                --indegree[index];
                if (indegree[index] == 0)
                {
                    processQueue.push(index);
                }
            }
        }
    }

}

void FrameGraphBuilder::_GenerateResourceCreationTask(const std::vector<std::set<NodeBlueprint*>>& inBatches)
{
    // first we will do device object creation for each internal device resource
    for (const auto& nodeBatch : inBatches)
    {
        // Here, we check resource handle generate inside, if we already assign device object,
        // we're good. If not, we check if there is a no longer referenced device object,
        // if there is one, we assign this object to the handle, and attach a prologue barrier to the node;
        // if there is not, we create a new device object for the handle
        for (auto node : nodeBatch)
        {
            std::vector<FrameGraphBufferHandle> bufferToCreate; // could have duplicate handles
            std::vector<FrameGraphImageHandle> imageToCreate;
            auto funcCollectCreationRequests =
                [=, this](
                    const FRAME_GRAPH_RESOURCE_HANDLE& inHandle,
                    std::vector<FrameGraphBufferHandle>& outBufRequests,
                    std::vector<FrameGraphImageHandle>& outImgRequests)
                {
                    if (std::holds_alternative<FrameGraphBufferHandle>(inHandle))
                    {
                        auto handle = std::get<FrameGraphBufferHandle>(inHandle);
                        auto blueprint = _GetBufferBlueprint(handle);

                        // should be first time the handle appear in our view
                        CHECK_TRUE(!blueprint->HaveResourceAssigned(handle) || blueprint->external);

                        outBufRequests.emplace_back(handle);
                    }
                    else if (std::holds_alternative<FrameGraphImageHandle>(inHandle))
                    {
                        auto handle = std::get<FrameGraphImageHandle>(inHandle);
                        auto blueprint = _GetImageBlueprint(handle);

                        // should be first time the handle appear in our view
                        CHECK_TRUE(!blueprint->HaveResourceAssigned(handle) || blueprint->external);

                        outImgRequests.emplace_back(handle);
                    }
                    else
                    {
                        CHECK_TRUE(false);
                    }
                };

            // we will need to create for transient, and pure output resource
            for (auto& transient : node->transients)
            {
                auto& currentHandle = transient->handle;
                funcCollectCreationRequests(currentHandle, bufferToCreate, imageToCreate);
            }
            for (auto& output : node->outputs)
            {
                // not pure output
                if (output->prev != nullptr) continue;

                auto& currentHandle = output->handle;
                funcCollectCreationRequests(currentHandle, bufferToCreate, imageToCreate);
            }

            // now we have resource should be create, find out if we really need to create a new one
            for (auto handle : bufferToCreate)
            {
                auto bufferBlueprint = _GetBufferBlueprint(handle);
                
                if (bufferBlueprint->HaveResourceAssigned(handle))
                {
                    auto index = bufferBlueprint->handleToIndex[handle];

                    bufferBlueprint->refCounts[index]++;
                    
                    continue;
                }

                bool haveFreeResource = false;
                auto& refCounts = bufferBlueprint->refCounts;
                for (size_t i = 0; i < refCounts.size(); ++i)
                {
                    if (refCounts[i] == 0)
                    {
                        haveFreeResource = true;
                        refCounts[i] = 1;
                        bufferBlueprint->handleToIndex[handle] = i;

                        continue;
                    }
                }
                if (haveFreeResource)
                {
                    continue;
                }

                // ok, we need to create a new resource, presage it to frame graph
                // 1. update blueprint
                // 2. create new buffer
                {
                    size_t resourceLocation = refCounts.size();
                    bufferBlueprint->handleToIndex[handle] = resourceLocation;
                    bufferBlueprint->refCounts.push_back(1);
                    bufferBlueprint->states.emplace_back(std::make_unique<FrameGraphBufferResourceState>(*bufferBlueprint->initialState));
                    auto funcCreateBuffer = [=, this](FrameGraph* toInit)
                        {
                            std::unique_ptr<Buffer> newBuffer = std::make_unique<Buffer>();
                            std::set<FrameGraphBufferHandle> handleShareThisResource{};
                            
                            newBuffer->Init(bufferBlueprint->initializer.get());
                            
                            // find out handles that point to the new resource, associate them with it
                            for (auto& p : bufferBlueprint->handleToIndex)
                            {
                                if (p.second == resourceLocation)
                                {
                                    _RegisterHandleToResource(toInit, p.first, newBuffer.get());
                                }
                            }

                            _AddInternalBufferToGraph(toInit, std::move(newBuffer));
                        };

                    m_initResourceProcesses.push_back(std::move(funcCreateBuffer));
                }
            }
            for (auto handle : imageToCreate)
            {
                auto imageBlueprint = _GetImageBlueprint(handle);

                if (imageBlueprint->HaveResourceAssigned(handle))
                {
                    auto index = imageBlueprint->handleToIndex[handle];

                    imageBlueprint->refCounts[index]++;

                    continue;
                }

                bool haveFreeResource = false;
                auto& refCounts = imageBlueprint->refCounts;
                for (size_t i = 0; i < refCounts.size(); ++i)
                {
                    if (refCounts[i] == 0)
                    {
                        haveFreeResource = true;
                        refCounts[i] = 1;
                        imageBlueprint->handleToIndex[handle] = i;

                        continue;
                    }
                }

                // ok, we need to create a new resource, presage it to frame graph
                // 1. update blueprint
                // 2. create new image
                {
                    size_t resourceLocation = refCounts.size();
                    imageBlueprint->handleToIndex[handle] = resourceLocation;
                    imageBlueprint->refCounts.push_back(1);
                    imageBlueprint->states.emplace_back(std::make_unique<FrameGraphImageResourceState>(*imageBlueprint->initialState));
                    auto funcCreateImage = [=, this](FrameGraph* toInit)
                        {
                            std::unique_ptr<Image> newImage = std::make_unique<Image>();

                            newImage->Init(imageBlueprint->initializer.get());

                            // find out handles that point to the new resource, associate them with it
                            for (auto& p : imageBlueprint->handleToIndex)
                            {
                                if (p.second == resourceLocation)
                                {
                                    _RegisterHandleToResource(toInit, p.first, newImage.get());
                                }
                            }

                            _AddInternalImageToGraph(toInit, std::move(newImage));
                        };

                    m_initResourceProcesses.push_back(std::move(funcCreateImage));
                }
            }
        }

        // Here, we decrease reference count for input (release), and increase reference count for output (hold)
        for (auto node : nodeBatch)
        {            
            auto funcUpdateRef = [=, this](FRAME_GRAPH_RESOURCE_HANDLE inHandle, bool release, uint32_t amt = 1)
                {
                    if (std::holds_alternative<FrameGraphBufferHandle>(inHandle))
                    {
                        auto handle = std::get<FrameGraphBufferHandle>(inHandle);
                        auto blueprint = _GetBufferBlueprint(handle);
                        CHECK_TRUE(blueprint->HaveResourceAssigned(handle));
                        auto index = blueprint->handleToIndex[handle];

                        if (release)
                        {
                            CHECK_TRUE(blueprint->refCounts[index] > 0);
                            --blueprint->refCounts[index];
                        }
                        else
                        {
                            ++blueprint->refCounts[index];
                        }
                    }
                    else if (std::holds_alternative<FrameGraphImageHandle>(inHandle))
                    {
                        auto handle = std::get<FrameGraphImageHandle>(inHandle);
                        auto blueprint = _GetImageBlueprint(handle);
                        CHECK_TRUE(blueprint->HaveResourceAssigned(handle));
                        auto index = blueprint->handleToIndex[handle];
                        
                        if (release)
                        {
                            CHECK_TRUE(blueprint->refCounts[index] >= amt);
                            blueprint->refCounts[index] -= amt;
                        }
                        else
                        {
                            blueprint->refCounts[index] += amt;
                        }
                    }
                    else
                    {
                        CHECK_TRUE(false);
                    }
                };

            // ================== release ======================
            for (auto& transient : node->transients)
            {
                auto& handle = transient->handle;

                funcUpdateRef(handle, true);
            }
            for (auto& input : node->inputs)
            {
                auto& handle = input->handle;

                funcUpdateRef(handle, true);
            }
            for (auto& output : node->outputs)
            {
                if (output->prev != nullptr) continue;
                // for pure output, we also need to release by 1 to compensate what we do in resource creation
                auto& handle = output->handle;

                funcUpdateRef(handle, true);
            }
            
            // =================== hold ========================
            for (auto& output : node->outputs)
            {
                auto& nexts = output->nexts;
                auto& handle = output->handle;

                // presage ref count for next batches
                funcUpdateRef(handle, false, static_cast<uint32_t>(nexts.size()));
            }
        }
    }
}

void FrameGraphBuilder::_GenerateSyncTask(const std::vector<std::set<NodeBlueprint*>>& inBatches)
{
    size_t wave = 0;

    for (const auto& nodeBatch : inBatches)
    {
        std::vector<BufferMemoryBarrierBlueprint> bufBarriers;
        std::vector<ImageMemoryBarrierBlueprint> imgBarriers;

        for (const NodeBlueprint* node : nodeBatch)
        {
            // collect prologue barriers
            for (const auto& curInput : node->inputs)
            {
                if (std::holds_alternative<FrameGraphBufferHandle>(curInput->handle))
                {
                    std::vector<FrameGraphBufferSubResourceState> curStates;
                    FrameGraphBufferHandle curHandle = std::get<FrameGraphBufferHandle>(curInput->handle);
                    auto pResourceState = _GetResourceState(curHandle);
                    auto aimState = std::get<FrameGraphBufferSubResourceState>(curInput->state);
                    
                    pResourceState->GetSubResourceState(
                        aimState.offset, 
                        aimState.size, 
                        curStates);
                    for (const auto& curState : curStates)
                    {
                        bool queueTransfer = curState.queueFamily == ~0 ? false : curState.queueFamily != aimState.queueFamily;
                        BufferBarrierBuilder builder{};
                        BufferMemoryBarrierBlueprint barrierBlueprint{};
                        
                        builder.CustomizeOffsetAndSize(curState.offset, curState.size);
                        if (queueTransfer)
                        {
                            builder.CustomizeQueueFamilyTransfer(curState.queueFamily, aimState.queueFamily);
                        }
                        barrierBlueprint.barrier = builder.Build(
                            VK_NULL_HANDLE, 
                            curState.access, 
                            aimState.access);
                        barrierBlueprint.resourceHandle = curHandle;
                        barrierBlueprint.srcStage = curState.stage;
                        barrierBlueprint.dstStage = aimState.stage;

                        bufBarriers.emplace_back(barrierBlueprint);
                    }
                }
                else if (std::holds_alternative<FrameGraphImageHandle>(curInput->handle))
                {
                    std::vector<FrameGraphImageSubResourceState> curStates;
                    FrameGraphImageHandle curHandle = std::get<FrameGraphImageHandle>(curInput->handle);
                    auto pResourceState = _GetResourceState(curHandle);
                    auto aimState = std::get<FrameGraphImageSubResourceState>(curInput->state);

                    pResourceState->GetSubResourceState(aimState.range, curStates);
                    for (const auto& curState : curStates)
                    {
                        ImageMemoryBarrierBlueprint barrierBlueprint{};
                        ImageBarrierBuilder builder{};
                        bool queueTransfer = curState.queueFamily == ~0 ? false : curState.queueFamily != aimState.queueFamily;

                        builder.CustomizeImageAspect(curState.range.aspectMask);
                        builder.CustomizeArrayLayerRange(curState.range.baseArrayLayer, curState.range.layerCount);
                        builder.CustomizeMipLevelRange(curState.range.baseMipLevel, curState.range.levelCount);
                        if (queueTransfer)
                        {
                            builder.CustomizeQueueFamilyTransfer(curState.queueFamily, aimState.queueFamily);
                        }
                        barrierBlueprint.barrier = builder.Build(
                            VK_NULL_HANDLE, 
                            curState.layout, 
                            aimState.layout, 
                            curState.access, 
                            aimState.access);
                        barrierBlueprint.srcStage = curState.stage;
                        barrierBlueprint.dstStage = aimState.stage;
                        barrierBlueprint.resourceHandle = curHandle;

                        imgBarriers.emplace_back(barrierBlueprint);
                    }
                }
                else
                {
                    CHECK_TRUE(false);
                }
            }
        }

        wave++;
    }
}

void FrameGraphBuilder::_AddInternalBufferToGraph(FrameGraph* inGraph, std::unique_ptr<Buffer> inBufferToOwn) const
{
    inGraph->m_internalBuffers.push_back(std::move(inBufferToOwn));
}

void FrameGraphBuilder::_AddExternalBufferToGraph(FrameGraph* inGraph, Buffer* inBuffer) const
{
    inGraph->m_externalBuffers.push_back(inBuffer);
}

void FrameGraphBuilder::_AddInternalImageToGraph(FrameGraph* inGraph, std::unique_ptr<Image> inImageToOwn) const
{
    inGraph->m_internalImages.push_back(std::move(inImageToOwn));
}

void FrameGraphBuilder::_AddExternalImageToGraph(FrameGraph* inGraph, Image* inImage) const
{
    inGraph->m_externalImages.push_back(inImage);
}

void FrameGraphBuilder::_RegisterHandleToResource(FrameGraph* inGraph, FrameGraphBufferHandle inHandle, Buffer* inResource) const
{
    inGraph->m_handleToBuffer[inHandle] = inResource;
}

void FrameGraphBuilder::_RegisterHandleToResource(FrameGraph* inGraph, FrameGraphImageHandle inHandle, Image* inResource) const
{
    inGraph->m_handleToImage[inHandle] = inResource;
}

auto FrameGraphBuilder::AddFrameGraphPass(const FrameGraphPassBind* inPassBind) -> FrameGraphNodeHandle
{
    FrameGraphNodeHandle handle{};
    std::unique_ptr<NodeBlueprint> newNode = std::make_unique<NodeBlueprint>();
    auto pPass = inPassBind->m_pass;
    std::vector<std::vector<std::function<void(NodeInput*, NodeOutput*, NodeTransient*)>>> updates;

    updates.resize(pPass->attachments.size());

    // store all attachment updates
    for (auto& upd : inPassBind->m_data)
    {
        auto& updateQueue = updates.at(upd.id);
        auto updateCmd = [&](NodeInput* input, NodeOutput* output, NodeTransient* transient)
            {
                if (input != nullptr)
                {
                    input->handle = upd.handle;
                    input->name = upd.name;
                }
                if (output != nullptr)
                {
                    output->handle = upd.handle;
                    output->name = upd.name;
                }
                if (transient != nullptr)
                {
                    transient->handle = upd.handle;
                    transient->name = upd.name;
                }
            };

        updateQueue.push_back(updateCmd);
    }
     
    size_t i = 0;
    for (const auto& att : pPass->attachments)
    {
        NodeInput* curAttInput = nullptr;
        NodeOutput* curAttOutput = nullptr;
        NodeTransient* curAttTransient = nullptr;

        // create inputs outputs for current attachment
        if (att.hasInput)
        {
            std::unique_ptr<NodeInput> newInput = std::make_unique<NodeInput>();
            curAttInput = newInput.get();
            newInput->owner = newNode.get();
            if (att.bufferState.has_value())
            {
                newInput->state = att.bufferState.value();
            }
            else if (att.imageState.has_value())
            {
                newInput->state = att.imageState.value();
            }
            else
            {
                CHECK_TRUE(false);
            }
            newNode->inputs.push_back(std::move(newInput));
        }
        if (att.hasOutput)
        {
            std::unique_ptr<NodeOutput> newOutput = std::make_unique<NodeOutput>();
            curAttOutput = newOutput.get();
            newOutput->owner = newNode.get();
            if (att.bufferState.has_value())
            {
                newOutput->state = att.bufferState2.has_value() ? att.bufferState2.value() : att.bufferState.value();
            }
            else if (att.imageState.has_value())
            {
                newOutput->state = att.imageState2.has_value() ? att.imageState2.value() : att.imageState.value();
            }
            else
            {
                CHECK_TRUE(false);
            }
            newNode->outputs.push_back(std::move(newOutput));
        }
        if (!att.hasInput && !att.hasOutput)
        {
            std::unique_ptr<NodeTransient> newTransient = std::make_unique<NodeTransient>();

            curAttTransient = newTransient.get();
            newTransient->owner = newNode.get();
            if (att.bufferState.has_value())
            {
                newTransient->initialState = att.bufferState.value();
                newTransient->finalState = att.bufferState2.value();
            }
            else if (att.imageState.has_value())
            {
                newTransient->initialState = att.imageState.value();
                newTransient->finalState = att.imageState2.value();
            }
            else
            {
                CHECK_TRUE(false);
            }
            newNode->transients.push_back(std::move(newTransient));
        }

        // apply attachment update to input output
        for (auto& cmd : updates[i])
        {
            cmd(curAttInput, curAttOutput, curAttTransient);
        }

        // try to connect
        if (curAttInput != nullptr)
        {
            CHECK_TRUE(m_nameToOutput.find(curAttInput->name) != m_nameToOutput.end());
            auto outputToConnect = m_nameToOutput.at(curAttInput->name);
            outputToConnect->nexts.insert(curAttInput);
            curAttInput->prev = outputToConnect;
            curAttInput->handle = outputToConnect->handle;
        }

        if (att.hasInput && att.hasOutput)
        {
            curAttInput->next = curAttOutput;
            curAttOutput->prev = curAttInput;
            curAttOutput->handle = curAttInput->handle;
        }

        if (att.hasOutput)
        {
            m_nameToOutput[curAttOutput->name] = curAttOutput;
        }

        ++i;
    }

    handle.handle = static_cast<uint32_t>(m_nodeBlueprints.size());
    m_nodeBlueprints.push_back(std::move(newNode));

    return handle;
}

void FrameGraphBuilder::AddExtraDependency(FrameGraphNodeHandle inSooner, FrameGraphNodeHandle inLater)
{
    NodeBlueprint* first = _GetNodeBlueprint(inSooner);
    NodeBlueprint* second = _GetNodeBlueprint(inLater);

    first->extraNexts.insert(second);
    second->extraPrevs.insert(first);
}

void FrameGraphBuilder::ArrangePasses()
{
    std::vector<std::set<NodeBlueprint*>> nodeBatches;

    _TopologicalSort(nodeBatches);

    _GenerateResourceCreationTask(nodeBatches);
}

void FrameGraphBuilder::NodeBlueprint::GetFullNext(std::set<NodeBlueprint*>& output)
{
    output.clear();
    output.insert(extraNexts.begin(), extraNexts.end());

    for (auto& curOutput : outputs)
    {
        for (auto nextInput : curOutput->nexts)
        {
            output.insert(nextInput->owner);
        }        
    }
}

void FrameGraphBuilder::NodeBlueprint::GetFullPrev(std::set<NodeBlueprint*>& output)
{
    output.clear();
    output.insert(extraPrevs.begin(), extraPrevs.end());

    for (auto& curInput : inputs)
    {
        auto prevOutput = curInput->prev;
        if (prevOutput)
        {
            output.insert(prevOutput->owner);
        }
    }
}

bool FrameGraphBuilder::ImageBlueprint::HaveResourceAssigned(FrameGraphImageHandle inHandle)
{
    return handleToIndex.find(inHandle) != handleToIndex.end();
}
bool FrameGraphBuilder::BufferBlueprint::HaveResourceAssigned(FrameGraphBufferHandle inHandle)
{
    return handleToIndex.find(inHandle) != handleToIndex.end();
}
