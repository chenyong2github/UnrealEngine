// Copyright Epic Games, Inc. All Rights Reserved.

#ifdef NNE_USE_DIRECTML

#include "NNEDmlOperator.h"
#include "NNEDmlOperatorUtils.h"

namespace UE::NNERuntimeRDG::Private::Dml
{

class FOperatorDmlMaxUnpool : public FOperatorDml
{

public:

	static FOperatorDml* Create()
	{
		return new FOperatorDmlMaxUnpool();
	}

	//
	//
	//
	virtual bool Initialize(IDMLDevice* Device, TArrayView<const NNECore::Internal::FTensor> InputTensors, TArrayView<const NNECore::Internal::FTensor> OutputTensors, const NNECore::FAttributeMap& Attributes) override
	{
		check(InputTensors.Num() == 2 || InputTensors.Num() == 3);
		check(OutputTensors.Num() == 1);

		if(InputTensors.Num() == 3)
		{
			ConstantCPUInputs.Add(2);
		}

		const NNECore::Internal::FTensor& InputTensorDesc = InputTensors[0];
		const NNECore::Internal::FTensor& IndicesTensorDesc = InputTensors[1];
		const NNECore::Internal::FTensor& OutputTensorDesc = OutputTensors[0];

		DmlUtil::FTensorDesc	DmlInputTensorDesc;
		DmlUtil::FTensorDesc	DmlIndicesTensorDesc;
		DmlUtil::FTensorDesc	DmlOutputTensorDesc;

		if (!DmlInputTensorDesc.InitFromTensor(InputTensorDesc, 4))
		{
			UE_LOG(LogNNE, Warning, TEXT("Failed to initialize input tensor for DML inference"));
			return false;
		}
		if (!DmlIndicesTensorDesc.InitFromTensor(IndicesTensorDesc, 4))
		{
			UE_LOG(LogNNE, Warning, TEXT("Failed to initialize indices tensor for DML inference"));
			return false;
		}
		check(DmlIndicesTensorDesc.BuffDesc.DataType == DML_TENSOR_DATA_TYPE::DML_TENSOR_DATA_TYPE_INT64);
		// Cast IndicesTensor from int64 to uint64 due to differences in representation between ONNX and DML formats.
		DmlIndicesTensorDesc.BuffDesc.DataType = DML_TENSOR_DATA_TYPE::DML_TENSOR_DATA_TYPE_UINT64;
		if (!DmlOutputTensorDesc.InitFromTensor(OutputTensorDesc, 4))
		{
			UE_LOG(LogNNE, Warning, TEXT("Failed to initialize output tensor for DML inference"));
			return false;
		}

		DML_MAX_UNPOOLING_OPERATOR_DESC DmlMaxUnpoolOpDesc{};

		DmlMaxUnpoolOpDesc.InputTensor = &DmlInputTensorDesc.Desc;
		DmlMaxUnpoolOpDesc.IndicesTensor = &DmlIndicesTensorDesc.Desc;
		DmlMaxUnpoolOpDesc.OutputTensor = &DmlOutputTensorDesc.Desc;

		return CreateOperator(Device, DML_OPERATOR_DESC{ DML_OPERATOR_MAX_UNPOOLING, &DmlMaxUnpoolOpDesc });
	}
};

// Register MaxUnpool operator on Module startup
NNE_DML_REGISTER_OP(MaxUnpool)

} // namespace UE::NNERuntimeRDG::Private::Dml

#endif // NNE_USE_DIRECTML
