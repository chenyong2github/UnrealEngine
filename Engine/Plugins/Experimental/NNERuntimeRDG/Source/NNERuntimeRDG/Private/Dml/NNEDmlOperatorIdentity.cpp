// Copyright Epic Games, Inc. All Rights Reserved.

#ifdef NNE_USE_DIRECTML

#include "NNEDmlOperator.h"
#include "NNEDmlOperatorUtils.h"

namespace UE::NNERuntimeRDG::Private::Dml
{

class FOperatorDmlIdentity : public FOperatorDml
{

public:

	static FOperatorDml* Create()
	{
		return new FOperatorDmlIdentity();
	}

	//
	//
	//
	virtual bool Initialize(IDMLDevice* Device, TArrayView<const NNECore::Internal::FTensor> InputTensors, TArrayView<const NNECore::Internal::FTensor> OutputTensors, const NNECore::FAttributeMap& Attributes) override
	{
		check(InputTensors.Num() == 1);
		check(OutputTensors.Num() == 1);

		const NNECore::Internal::FTensor& InputTensor = InputTensors[0];
		const NNECore::Internal::FTensor& OutputTensor = OutputTensors[0];

		FTensorDescDml	DmlInputTensorDesc;
		FTensorDescDml	DmlOutputTensorDesc;

		if (!DmlInputTensorDesc
				.SetFromTensor(InputTensor)
				.Validate())
		{
			UE_LOG(LogNNE, Error, TEXT("Failed to initialize input tensor for DML inference"));
			return false;
		}
		
		if (!DmlOutputTensorDesc
				.SetFromTensor(OutputTensor)
				.Validate())
		{
			UE_LOG(LogNNE, Error, TEXT("Failed to initialize output tensor for DML inference"));
			return false;
		}

		DML_ELEMENT_WISE_IDENTITY_OPERATOR_DESC DmlIdentityOpDesc{};

		DmlIdentityOpDesc.InputTensor = DmlInputTensorDesc.GetDmlDesc();
		DmlIdentityOpDesc.OutputTensor = DmlOutputTensorDesc.GetDmlDesc();

		return CreateOperator(Device, DML_OPERATOR_DESC{ DML_OPERATOR_ELEMENT_WISE_IDENTITY, &DmlIdentityOpDesc });
	}
};

// Register Reshape operator on Module startup
NNE_DML_REGISTER_OP(Identity)

} // namespace UE::NNERuntimeRDG::Private::Dml

#endif // NNE_USE_DIRECTML
