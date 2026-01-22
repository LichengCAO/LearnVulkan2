#include "frame_graph_builder.h"

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

FrameGraphPassBind& FrameGraphPassBind::BindOutAttachment(uint32_t inAttachmentIndex, const std::string& inName, const FrameGraphBufferHandle& inHandle)
{
    Data data{};

    data.id = inAttachmentIndex;
    data.name = inName;
    data.handle = inHandle;

    m_data.push_back(std::move(data));

    return *this;
}

FrameGraphPassBind& FrameGraphPassBind::BindOutAttachment(uint32_t inAttachmentIndex, const std::string& inName, const FrameGraphImageHandle& inHandle)
{
    Data data{};

    data.id = inAttachmentIndex;
    data.name = inName;
    data.handle = inHandle;

    m_data.push_back(std::move(data));

    return *this;
}

FrameGraphPassBind& FrameGraphPassBind::BindTransientAttachment(uint32_t inAttachmentIndex, const std::string& inName, const FrameGraphBufferHandle& inHandle)
{
    Data data{};

    data.id = inAttachmentIndex;
    data.name = inName;
    data.handle = inHandle;

    m_data.push_back(std::move(data));

    return *this;
}

FrameGraphPassBind& FrameGraphPassBind::BindTransientAttachment(uint32_t inAttachmentIndex, const std::string& inName, const FrameGraphImageHandle& inHandle)
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

auto FrameGraphBuilder::AddFrameGraphPass(const FrameGraphPassBind* inPassBind) -> FrameGraphNodeHandle
{
    FrameGraphNodeHandle handle{};
    std::unique_ptr<NodeBlueprint> newNode = std::make_unique<NodeBlueprint>();
    auto pPass = inPassBind->m_pass;
    std::vector<std::vector<std::function<void(NodeInput*, NodeOutput*)>>> updates;

    updates.resize(pPass->attachments.size());

    // store all attachment updates
    for (auto& upd : inPassBind->m_data)
    {
        auto& updateQueue = updates.at(upd.id);
        auto updateCmd = [&](NodeInput* input, NodeOutput* output)
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
            };

        updateQueue.push_back(updateCmd);

        // TODO: process transient
    }
     
    size_t i = 0;
    for (const auto& att : pPass->attachments)
    {
        NodeInput* curAttInput = nullptr;
        NodeOutput* curAttOutput = nullptr;

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

        // apply attachment update to input output
        for (auto& cmd : updates[i])
        {
            cmd(curAttInput, curAttOutput);
        }

        // try to connect
        if (curAttInput != nullptr)
        {
            CHECK_TRUE(m_nameToOutput.find(curAttInput->name) != m_nameToOutput.end());
            auto outputToConnect = m_nameToOutput.at(curAttInput->name);
            outputToConnect->nexts.push_back(curAttInput);
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

    handle.index = static_cast<uint32_t>(m_nodeBlueprints.size());
    m_nodeBlueprints.push_back(std::move(newNode));

    return handle;
}
