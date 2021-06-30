#include "ppl/nn/engines/cuda/optimizer/fusions/fs_averagepool.h"

#include "ppl/nn/engines/cuda/optimizer/opt_kernel.h"
#include "ppl/nn/params/onnx/pooling_param.h"
#include "ppl/nn/params/onnx/pad_param.h"
#include "ppl/nn/common/logger.h"

using namespace ppl::common;
using namespace ppl::nn::common;

namespace ppl { namespace nn { namespace cuda {

const bool AveragePoolFusion::CanFuse(ir::Node* node, ir::Node* prenode, OptKernelOptions& options) {
    if (node->GetType().name != "AveragePool" || prenode->GetType().name != "Pad")
        return false;

    auto data = options.graph->data.get();
    auto attrs = data->attrs.find(prenode->GetId());
    if (attrs == data->attrs.end()) {
        return false;
    }

    PadParam* param = (PadParam*)(attrs->second.get());
    if (param->mode != PadParam::PAD_MODE_CONSTANT) {
        return false;
    }

    auto constants = data->constants.find(prenode->GetInput(1));
    if (constants == data->constants.end()) {
        return false;
    }

    uint64_t* pads = (uint64_t*)(constants->second.data.data());
    if (constants->second.data.length() != 64) {
        return false;
    }

    for (uint32_t i = 0; i < 8; ++i) {
        if (i % 4 < 2 && pads[i] != 0) {
            return false;
        }
    }

    attrs = data->attrs.find(node->GetId());
    if (attrs == data->attrs.end()) {
        return false;
    }

    PoolingParam* pool_param = (PoolingParam*)(attrs->second.get());
    if (param->mode != PadParam::PAD_MODE_CONSTANT) {
        return false;
    }

    if (pool_param->pads.size() < 4) {
        return false;
    }

    for (uint32_t i = 0; i < 4; ++i) {
        if (pool_param->pads[i] != 0) {
            return false;
        }
    }

    return true;
}

const RetCode AveragePoolFusion::FuseWithPreviousPad(ir::Node* node, ir::Node* prenode, OptKernelOptions& options) {
    auto topo = options.graph->topo.get();
    auto connect_edge_id = node->GetInput(0);
    auto pre_edge_id = prenode->GetInput(0);
    auto pre_edge = topo->GetEdgeById(pre_edge_id);
    auto pad_edge_id = prenode->GetInput(1);
    auto pad_edge = topo->GetEdgeById(pad_edge_id);

    pre_edge->DelConsumer(prenode->GetId());
    pre_edge->AddConsumer(node->GetId());
    pad_edge->DelConsumer(prenode->GetId());
    node->ReplaceInput(connect_edge_id, pre_edge_id);

    topo->DelEdgeById(connect_edge_id);
    topo->DelNodeById(prenode->GetId());
    return RC_SUCCESS;
}

const RetCode AveragePoolFusion::FuseNode(ir::Node* node, bool reliable, OptKernelOptions& options) {
    auto topo = options.graph->topo.get();
    auto data = options.graph->data.get();
    auto node_id = node->GetId();
    auto edge_id = node->GetInput(0);
    if (edge_id == INVALID_EDGEID) {
        return RC_UNSUPPORTED;
    }

    auto edge = topo->GetEdgeById(edge_id);
    if (edge->CalcConsumerCount() != 1) {
        return RC_UNSUPPORTED;
    }

    auto prenode_id = edge->GetProducer();
    if (prenode_id == INVALID_NODEID) {
        return RC_UNSUPPORTED;
    }

    auto prenode = topo->GetNodeById(prenode_id);
    if (node->GetInputCount() != 1 || node->GetOutputCount() != 1) {
        return RC_UNSUPPORTED;
    }

    if (topo->GetOutput(edge->GetName()) != INVALID_EDGEID) { // Can not fuse an output edge
        return RC_UNSUPPORTED;
    }

    if (CanFuse(node, prenode, options)) {
        LOG(DEBUG) << "Fuse averagepool node[" << node->GetName() << "] with pad node[" << prenode->GetName() << "]";

        int index[4] = {2, 3, 6, 7};
        auto constants = data->constants.find(prenode->GetInput(1));
        uint64_t* pads = (uint64_t*)(constants->second.data.data());

        auto kernel = options.info->kernels.find(node_id)->second.get();
        PoolingParam* param = (PoolingParam*)(((CudaOptKernel*)kernel)->GetParam());
        param->pads.resize(4);
        for (uint32_t i = 0; i < param->pads.size(); ++i) {
            param->pads[i] = pads[index[i]];
        }
        param->mode = PoolingParam::POOLING_AVERAGE_INCLUDE;
        options.info->kernels.erase(prenode_id);
        if (topo->GetEdgeById(prenode->GetInput(1))->CalcConsumerCount() == 0) {
            data->constants.erase(prenode->GetInput(1));
            topo->DelEdgeById(prenode->GetInput(1));
        }
        FuseWithPreviousPad(node, prenode, options);
    }

    return RC_SUCCESS;
}

}}} // namespace ppl::nn::cuda