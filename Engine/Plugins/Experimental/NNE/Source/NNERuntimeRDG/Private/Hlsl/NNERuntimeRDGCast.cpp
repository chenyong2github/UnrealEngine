// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNERuntimeRDGCast.h"
#include "NNERuntimeRDGHelperCast.h"
#include "NNECoreTensor.h"
#include "NNECoreTypes.h"

namespace UE::NNERuntimeRDG::Private::Hlsl
{
	/**
	 * Cast operator implementation
	 */
	class FCast : public FOperatorHlsl
	{
	public:

		FCast() {}
		virtual ~FCast() = default;

		virtual int PrepareOutputs(TConstArrayView<NNECore::Internal::FTensorRef> InputTensors, TArrayView<NNECore::Internal::FTensorRef> OutputTensors) const override
		{
			check(InputTensors.Num() == 1);
			check(OutputTensors.Num() == 1);
			OutputTensors[0]->SetShape(InputTensors[0]->GetShape());

			const NNECore::Internal::FTensor& X = *InputTensors[0];

			Internal::CPUHelper::Cast::Apply(X, *OutputTensors[0]);

			if (!OutputTensors[0]->HasPreparedData())
			{
				UE_LOG(LogNNE, Warning, TEXT("Cast: Output could not be computed as a constant tensor, however Cast is not implemented on GPU at the moment."));
				return -1;
			}

			return 0;
		}

		virtual bool Initialize(TConstArrayView<NNECore::FTensorDesc> InputTensorDescs, TConstArrayView<NNECore::FTensorDesc> OutputTensorDescs, const NNECore::FAttributeMap& Attributes) override
		{
			check(InputTensorDescs.Num() == 1);
			check(OutputTensorDescs.Num() == 1);

			ENNETensorDataType ToFromAttribute = (ENNETensorDataType)Attributes.GetValue<int>(TEXT("to"));
			ENNETensorDataType ToFromTensor = OutputTensorDescs[0].GetDataType();

			if (ToFromAttribute != ToFromTensor)
			{
				UE_LOG(LogNNE, Warning, TEXT("Cast should output a tensor of type %d but was of type %d."), ToFromAttribute, ToFromTensor);
				return false;
			}
			
			return true;
		}

		virtual void Dispatch(FRDGBuilder& GraphBuilder, TConstArrayView<FTensorRDGRef> InputTensors, TConstArrayView<FTensorRDGRef> OutputTensors) override
		{
			UE_LOG(LogNNE, Warning, TEXT("Cast: Output should be constant and already uploaded to GPU memory. Dispatch should not need to be called."));
		}
	};

	bool ValidateCastOperator(const NNECore::FAttributeMap& AttributeMap, TConstArrayView<ENNETensorDataType> InputTypes, TConstArrayView<NNECore::FSymbolicTensorShape> InputShapes)
	{
		bool bIsValid = true;

		//This match version 13 of the Cast operator
		//https://github.com/onnx/onnx/blob/main/docs/Operators.md#Cast
		FAttributeValidator AttributeValidator;
		AttributeValidator.AddRequired(TEXT("to"), ENNEAttributeDataType::Int32);
		bIsValid &= AttributeValidator.Validate(AttributeMap);

		if (bIsValid)
		{
			//In ONNX "To" is assumed to be DataType from TensorProto. Here it is a ENNETensorDataType however no conversion was needed as both enum match.
			//See ENNETensorDataType GetDataTypeFromGraphTensor(Ort::GraphTensorDataType TensorDataType) in NNEUtilsModelOptimizer.cpp
			//If a conversion is needed in the future it should be done when converting the model from ONNX to RDG format in the model builder.
			ENNETensorDataType To = (ENNETensorDataType)AttributeMap.GetValue<int>(TEXT("to"));
			switch (To)
			{
				case ENNETensorDataType::None:
					UE_LOG(LogNNE, Warning, TEXT("Cast: Invalid target type 'None' not supported."));
					bIsValid = false;
					break;
				case ENNETensorDataType::Char:
					UE_LOG(LogNNE, Warning, TEXT("Cast: Invalid target type 'Char' not supported."));
					bIsValid = false;
					break;
				case ENNETensorDataType::Boolean:
					UE_LOG(LogNNE, Warning, TEXT("Cast: Invalid target type 'Boolean' not supported."));
					bIsValid = false;
					break;
				case ENNETensorDataType::Half:
					UE_LOG(LogNNE, Warning, TEXT("Cast: Invalid target type 'Half' not supported."));
					bIsValid = false;
					break;
				case ENNETensorDataType::Float:
					break;
				case ENNETensorDataType::Double:
					UE_LOG(LogNNE, Warning, TEXT("Cast: Invalid target type 'Double' not supported."));
					bIsValid = false;
					break;
				case ENNETensorDataType::Int8:
					UE_LOG(LogNNE, Warning, TEXT("Cast: Invalid target type 'Int8' not supported."));
					bIsValid = false;
					break;
				case ENNETensorDataType::Int16:
					UE_LOG(LogNNE, Warning, TEXT("Cast: Invalid target type 'Int16' not supported."));
					bIsValid = false;
					break;
				case ENNETensorDataType::Int32:
					break;
				case ENNETensorDataType::Int64:
					break;
				case ENNETensorDataType::UInt8:
					UE_LOG(LogNNE, Warning, TEXT("Cast: Invalid target type 'UInt8' not supported."));
					bIsValid = false;
					break;
				case ENNETensorDataType::UInt16:
					UE_LOG(LogNNE, Warning, TEXT("Cast: Invalid target type 'UInt16' not supported."));
					bIsValid = false;
					break;
				case ENNETensorDataType::UInt32:
					UE_LOG(LogNNE, Warning, TEXT("Cast: Invalid target type 'UInt32' not supported."));
					bIsValid = false;
					break;
				case ENNETensorDataType::UInt64:
					UE_LOG(LogNNE, Warning, TEXT("Cast: Invalid target type 'UInt64' not supported."));
					bIsValid = false;
					break;
				case ENNETensorDataType::Complex64:
					UE_LOG(LogNNE, Warning, TEXT("Cast: Invalid target type 'Complex64' not supported."));
					bIsValid = false;
					break;
				case ENNETensorDataType::Complex128:
					UE_LOG(LogNNE, Warning, TEXT("Cast: Invalid target type 'Complex128' not supported."));
					bIsValid = false;
					break;
				case ENNETensorDataType::BFloat16:
					UE_LOG(LogNNE, Warning, TEXT("Cast: Invalid target type 'BFloat16' not supported."));
					bIsValid = false;
					break;
				default:
					UE_LOG(LogNNE, Warning, TEXT("Cast: Invalid target type %d not supported."), To);
					bIsValid = false;
			}
		}
		
		FInputValidator InputValidator;
		InputValidator.AddSupportedType(ENNETensorDataType::Float);
		InputValidator.AddSupportedType(ENNETensorDataType::Int32);
		InputValidator.AddSupportedType(ENNETensorDataType::Int64);
		InputValidator.AddRequired();
		bIsValid &= InputValidator.Validate(InputTypes);

		return bIsValid;
	}

	FOperatorHlsl* CreateCastOperator()
	{
		return new FCast();
	}

	bool RegisterCastOperator(FOperatorRegistryHlsl& Registry)
	{
		Registry.OpAdd(TEXT("Cast"), CreateCastOperator, ValidateCastOperator);
		return true;
	}
} // UE::NNERuntimeRDG::Private::Hlsl
