// Copyright Epic Games, Inc. All Rights Reserved.

#ifdef NNE_USE_DIRECTML

#include "NNEDmlOperator.h"
#include "Algo/Transform.h"
#include "Algo/Count.h"
#include "Misc/EnumerateRange.h"

namespace UE::NNERuntimeRDG::Private::Dml
{

/**
 * Reshape
 */
class FOperatorDmlReshape : public FOperatorDml
{

    template<typename DataType>
    bool ReshapeTensorToDmlShape(
        const NNECore::Internal::FTensor& InputTensor, 
        const NNECore::Internal::FTensor& ShapeTensor, 
        bool bAllowZero,
        DmlUtil::FSmallUIntArray& OutShape)
    {
        
        DmlUtil::FSmallArray<DataType> ReshapedShape(ShapeTensor.GetPreparedData<DataType>());

        if(!bAllowZero)
        {
            // at most 1 dimension can be -1
            if(Algo::Count(ReshapedShape, -1) > 1)
            {
                UE_LOG(LogNNE, Error, TEXT("Shape tensor can't contain more than one '-1'."));
                return false;
            }
            for (TEnumerateRef<DataType> Elem : EnumerateRange(ReshapedShape))
            {
                if(!(*Elem != 0 || InputTensor.GetShape().Rank() > Elem.GetIndex()))
                {
                    UE_LOG(LogNNE, Error, TEXT("Shape tensor contains '0' in an invalid place."));
                    return false;
                }
                *Elem = 
                    *Elem == 0 ? 
                        InputTensor.GetShape().GetData()[Elem.GetIndex()] 
                        : 
                        *Elem;
            }
        }
        else
        {
            // no -1 is allowed if there is a 0
            if(!Algo::Count(ReshapedShape, 0) == 0 && !Algo::Count(ReshapedShape, -1) == 0)
            {
                UE_LOG(LogNNE, Error, TEXT("Shape tensor contains both '0' and '-1'. This is not allowed."));
                return false;
            }
        }

        auto PartialVolume = [](TConstArrayView<DataType> Shape) -> uint32
            {
                uint32 Volume = 1;
                for(DataType Dim : Shape)
                {
                    Volume *= Dim != -1 ? (DataType) Dim : 1;
                }
                return Volume;
            };

        OutShape.SetNumUninitialized(ReshapedShape.Num());

        for(TConstEnumerateRef<DataType> Elem : EnumerateRange(ReshapedShape))
        {
            OutShape[Elem.GetIndex()] = 
                *Elem == -1 ? 
                    (uint32) InputTensor.GetShape().Volume() / PartialVolume(ReshapedShape)
                    :
                    (uint32) *Elem;
        }

        return true;
    }

public:

	static FOperatorDml* Create()
	{
		return new FOperatorDmlReshape();
	}

	//
	//
	//
	virtual bool Initialize(IDMLDevice* Device, TArrayView<const NNECore::Internal::FTensor> InputTensors, TArrayView<const NNECore::Internal::FTensor> OutputTensors, const NNECore::FAttributeMap& Attributes) override
	{
        check(InputTensors.Num() == 2);
        check(OutputTensors.Num() == 1);

        ConstantCPUInputs.Add(1);

        // Shape tensor must be constant!
        check(InputTensors[1].HasPreparedData());

        bool bAllowZero = (bool)
			( Attributes.GetValueOrDefault<int32>(TEXT("allowzero"), 0) );

        DmlUtil::FSmallUIntArray ReshapedShape;

        switch(InputTensors[1].GetDataType())
        {
        case ENNETensorDataType::Int32:
            if(!ReshapeTensorToDmlShape<int32>(InputTensors[0], InputTensors[1], bAllowZero, ReshapedShape))
            {
                return false;
            }
            break;
        case ENNETensorDataType::Int64:
            if(!ReshapeTensorToDmlShape<int64>(InputTensors[0], InputTensors[1], bAllowZero, ReshapedShape))
            {
                return false;
            }
            break;
        case ENNETensorDataType::UInt32:
            if(!ReshapeTensorToDmlShape<uint32>(InputTensors[0], InputTensors[1], bAllowZero, ReshapedShape))
            {
                return false;
            }
            break;
        default:
            UE_LOG(LogNNE, Warning, TEXT("Shape tensor has invalid data type"));
			return false;
        };
        
        check(ReshapedShape == OutputTensors[0].GetShape().GetData());

        DmlUtil::FTensorDesc DmlInputTensorDesc;
        if (!DmlInputTensorDesc.InitFromTensor(InputTensors[0], ReshapedShape.Num(), 
			/*Broadcast =*/ MakeArrayView((uint32*) nullptr, 0), 
			/*CustomShape =*/ ReshapedShape))
		{
			UE_LOG(LogNNE, Warning, TEXT("Failed to initialize Reshape's input tensor for DML inference"));
			return false;
		}

        DmlUtil::FTensorDesc DmlOutputTensorDesc;
        if (!DmlOutputTensorDesc.InitFromTensor(OutputTensors[0], OutputTensors[0].GetShape().Rank()))
		{
			UE_LOG(LogNNE, Warning, TEXT("Failed to initialize Reshape's output tensor for DML inference"));
			return false;
		}
        
		DML_ELEMENT_WISE_IDENTITY_OPERATOR_DESC DmlIdentityOpDesc{};

        DmlIdentityOpDesc.InputTensor = &DmlInputTensorDesc.Desc;
        DmlIdentityOpDesc.OutputTensor = &DmlOutputTensorDesc.Desc;

		return CreateOperator(Device, DML_OPERATOR_DESC{ DML_OPERATOR_ELEMENT_WISE_IDENTITY, &DmlIdentityOpDesc} );

	}
};

// Register Reshape operator on Module startup
NNE_DML_REGISTER_OP(Reshape)

} // namespace UE::NNERuntimeRDG::Private::Dml

#endif // NNE_USE_DIRECTML
