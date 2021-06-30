#include "ppl/nn/models/onnx/parsers/parse_roialign_param.h"
#include "ppl/nn/common/logger.h"
#include "ppl/nn/models/onnx/utils.h"

namespace ppl { namespace nn { namespace onnx {

ppl::common::RetCode ParseROIAlignParam(const ::onnx::NodeProto& node, void* arg, ir::Node*, ir::GraphTopo*) {
    auto param = static_cast<ppl::nn::common::ROIAlignParam*>(arg);
    std::string mode = utils::GetNodeAttrByKey<std::string>(node, "mode", "avg");
    if (mode == "avg") {
        param->mode = ppl::nn::common::ROIAlignParam::ONNXROIAlignMode_AVG;
    } else if (mode == "max") {
        param->mode = ppl::nn::common::ROIAlignParam::ONNXROIAlignMode_MAX;
    } else {
        LOG(ERROR) << "Invalid mode " << mode << ".";
        return ppl::common::RC_INVALID_VALUE;
    }
    param->output_height = utils::GetNodeAttrByKey<int32_t>(node, "output_height", 1);
    param->output_width = utils::GetNodeAttrByKey<int32_t>(node, "output_width", 1);
    param->sampling_ratio = utils::GetNodeAttrByKey<int32_t>(node, "sampling_ratio", 0);
    param->spatial_scale = utils::GetNodeAttrByKey<float>(node, "spatial_scale", 1.0f);
    return ppl::common::RC_SUCCESS;
}

}}} // namespace ppl::nn::onnx