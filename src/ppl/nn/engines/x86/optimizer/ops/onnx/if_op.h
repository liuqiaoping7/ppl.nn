#ifndef _ST_HPC_PPL_NN_ENGINES_X86_OPTIMIZER_OPS_ONNX_IF_OP_H_
#define _ST_HPC_PPL_NN_ENGINES_X86_OPTIMIZER_OPS_ONNX_IF_OP_H_

#include "ppl/nn/engines/x86/optimizer/opt_kernel.h"
#include "ppl/nn/engines/common/onnx/if_op.h"

namespace ppl { namespace nn { namespace x86 {

class IfOp final : public X86OptKernel {
public:
    IfOp(const ir::Node* node) : X86OptKernel(node), op_(node) {}
    ppl::common::RetCode Init(const OptKernelOptions& options) override;
    KernelImpl* CreateKernelImpl() const override;

private:
    common::IfOp op_;
};

}}} // namespace ppl::nn::x86

#endif