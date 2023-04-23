// Copyright Epic Games, Inc. All Rights Reserved.

#ifdef NNE_USE_DIRECTML

#include "NNEDmlOperator.h"
#include "NNEDmlOperatorUtils.h"

namespace UE::NNERuntimeRDG::Private::Dml
{

class FOperatorDmlDepthToSpace : public FOperatorDml
{

	static DML_DEPTH_SPACE_ORDER SpaceOrderFromModeString(FStringView StringVal)
	{
		if (FCString::Stricmp(StringVal.GetData(), TEXT("CRD")) == 0)
		{
			return DML_DEPTH_SPACE_ORDER_COLUMN_ROW_DEPTH;
		}
		else
		{
			return DML_DEPTH_SPACE_ORDER_DEPTH_COLUMN_ROW;
		}
	}

public:

	static FOperatorDml* Create()
	{
		return new FOperatorDmlDepthToSpace();
	}

	//
	//
	//
	virtual bool Initialize(IDMLDevice* Device, TArrayView<const NNECore::Internal::FTensor> InputTensors, TArrayView<const NNECore::Internal::FTensor> OutputTensors, const NNECore::FAttributeMap& Attributes) override
	{
		check(InputTensors.Num() == 1);
		check(OutputTensors.Num() == 1);

		int32 BlockSize;

		const FNNEAttributeValue* BlockSizeAttr = Attributes.GetAttributeValue(TEXT("blocksize"));
		if (BlockSizeAttr)
		{
			BlockSize = BlockSizeAttr->GetValue<int32>();
		}
		else
		{
			UE_LOG(LogNNE, Error, TEXT("blocksize attribute is required"));
			return false;
		}


		const NNECore::Internal::FTensor& InputTensorDesc = InputTensors[0];
		const NNECore::Internal::FTensor& OutputTensorDesc = OutputTensors[0];

		FTensorDescDml	DmlInputTensorDesc;
		FTensorDescDml	DmlOutputTensorDesc;

		if (!DmlInputTensorDesc
				.SetTensorRank(4, 4)
				.SetFromTensor(InputTensorDesc)
				.Validate())
		{
			UE_LOG(LogNNE, Warning, TEXT("Failed to initialize input tensor for DML inference"));
			return false;
		}

		if (!DmlOutputTensorDesc
				.SetTensorRank(4, 4)
				.SetFromTensor(OutputTensorDesc)
				.Validate())
		{
			UE_LOG(LogNNE, Warning, TEXT("Failed to initialize output tensor for DML inference"));
			return false;
		}

		DML_DEPTH_TO_SPACE1_OPERATOR_DESC DmlDepthToSpaceOpDesc{};

		DmlDepthToSpaceOpDesc.InputTensor = DmlInputTensorDesc.GetDmlDesc();
		DmlDepthToSpaceOpDesc.OutputTensor = DmlOutputTensorDesc.GetDmlDesc();
		DmlDepthToSpaceOpDesc.BlockSize = BlockSize;
		DmlDepthToSpaceOpDesc.Order = SpaceOrderFromModeString(Attributes.GetValueOrDefault<FString>(TEXT("mode"), FString(TEXT("DCR"))));

		return CreateOperator(Device, DML_OPERATOR_DESC{ DML_OPERATOR_DEPTH_TO_SPACE1, &DmlDepthToSpaceOpDesc });
	}
};

// Register DepthToSpace operator on Module startup
NNE_DML_REGISTER_OP(DepthToSpace)

} // namespace UE::NNERuntimeRDG::Private::Dml

#endif // NNE_USE_DIRECTML
