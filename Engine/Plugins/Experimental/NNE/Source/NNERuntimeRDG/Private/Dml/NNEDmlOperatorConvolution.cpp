// Copyright Epic Games, Inc. All Rights Reserved.

#ifdef NNE_USE_DIRECTML

#include "NNEDmlOperator.h"

namespace UE::NNERuntimeRDG::Private::Dml
{
/**
 * Convolution
 */
template <DML_CONVOLUTION_DIRECTION Direction>
class FOperatorDmlConv : public FOperatorDml
{
	using FSmallArray = TArray<uint32, TInlineAllocator<NcdhwSpatialDimensionCount>>;
	using FIntArray = TArray<int32>;
	
	enum EAutoPad
	{
		NOTSET,
		SAME_UPPER,
		SAME_LOWER,
		VALID
	};

	static EAutoPad AutoPadFromString(FStringView StringVal) 
	{
		if (FCString::Stricmp(StringVal.GetData(), TEXT("NOTSET")) == 0) 
		{
			return EAutoPad::NOTSET;
		}
		else if (FCString::Stricmp(StringVal.GetData(), TEXT("SAME_UPPER")) == 0)
		{
			return EAutoPad::SAME_UPPER;
		}
		else if (FCString::Stricmp(StringVal.GetData(), TEXT("SAME_LOWER")) == 0)
		{
			return EAutoPad::SAME_LOWER;
		}
		else if (FCString::Stricmp(StringVal.GetData(), TEXT("VALID")) == 0)
		{
			return EAutoPad::VALID;
		}
		else
		{
			return EAutoPad::NOTSET;
		}
	}

	struct FConvArgs
	{
		EAutoPad		AutoPad;
		FSmallArray		StartPadding;
		FSmallArray		EndPadding;
		FIntArray		OutPadding;
		FIntArray		Dilations;
		FIntArray		Strides;
		FSmallArray		OutputShape;
		uint32			NumDimensions;
		FSmallArray		WindowSize;
		int32			Group;

		FConvArgs() = default;

		//
		//
		//
		bool Init(const NNECore::FTensorShape& InputShape, const NNECore::FTensorShape& FilterShape, const NNECore::FAttributeMap& Attributes)
		{
			check(InputShape.Rank() > NonspatialDimensionCount);
			check(FilterShape.Rank() == InputShape.Rank());
			
			NumDimensions = InputShape.Rank() - NonspatialDimensionCount;

			const FNNEAttributeValue*	AttrStrides = Attributes.GetAttributeValue(TEXT("strides"));

			if (AttrStrides)
			{
				Strides = AttrStrides->GetValue<FIntArray>();
			}
			else
			{
				Strides.Init(1, NumDimensions);
			}

			check(Strides.Num() == 0 || Strides.Num() == FilterShape.Rank() - NonspatialDimensionCount);

			const FNNEAttributeValue*	AttrDilations = Attributes.GetAttributeValue(TEXT("dilations"));

			if (AttrDilations)
			{
				Dilations = AttrDilations->GetValue<FIntArray>();
			}
			else
			{
				Dilations.Init(1, NumDimensions);
			}

			check(Dilations.Num() == 0 || Dilations.Num() == FilterShape.Rank() - NonspatialDimensionCount);
			
			for (int32 Dim = FilterShape.Rank() - NumDimensions; Dim < FilterShape.Rank(); ++Dim)
			{
				WindowSize.Add(FilterShape.GetData()[Dim]);
			}

			if (Direction == DML_CONVOLUTION_DIRECTION_FORWARD)
			{
				OutPadding.Init(0, NumDimensions);
			}
			else
			{
				const FNNEAttributeValue* AttrOutPadding = Attributes.GetAttributeValue(TEXT("output_padding"));

				if (AttrOutPadding)
				{
					OutPadding = AttrOutPadding->GetValue<TArray<int32>>();
				}
				else
				{
					OutPadding.Init(0, NumDimensions);
				}
			}

			Group = Attributes.GetValueOrDefault<int32>(TEXT("group"), 1);

			AutoPad = AutoPadFromString(*Attributes.GetValue<FString>(TEXT("auto_pad")));

			if (AutoPad == EAutoPad::NOTSET)
			{
				const FNNEAttributeValue* AttrPads = Attributes.GetAttributeValue(TEXT("pads"));
				TArray<int32>	Pads;

				if (AttrPads)
				{
					Pads = AttrPads->GetValue<TArray<int32>>();
				}
				else
				{
					Pads.Init(0, 2 * NumDimensions);
				}

				for (uint32 Dim = 0; Dim < NumDimensions; ++Dim)
				{
					StartPadding.Add(Pads[Dim]);
					EndPadding.Add(Pads[Dim + NumDimensions]);
				}
			}
			else if (AutoPad == EAutoPad::VALID)
			{
				StartPadding.Init(0, NumDimensions);
				EndPadding.Init(0, NumDimensions);
			}
			else
			{
				StartPadding.Init(0, NumDimensions);
				EndPadding.Init(0, NumDimensions);

				SetAutoPadding(InputShape.GetData(), FilterShape.GetData());
			}

			if (Direction == DML_CONVOLUTION_DIRECTION_FORWARD)
			{
				SetOutputShape(InputShape.GetData(), FilterShape.GetData());
			}
			else
			{
				const FNNEAttributeValue* AttrOutShape = Attributes.GetAttributeValue(TEXT("output_shape"));

				if (AttrOutShape)
				{
					TArray<int32> OutShapeVal = AttrOutShape->GetValue<TArray<int32>>();

					for (int32 Value : OutShapeVal)
					{
						OutputShape.Add(uint32(Value));
					}
				}
				else
				{
					SetOutputShape(InputShape.GetData(), FilterShape.GetData());
				}
			}

			return true;
		}

		//
		//
		//
		void SetAutoPadding(TConstArrayView<uint32> InputShape, TConstArrayView<uint32> FilterShape)
		{
			const uint32 DimOffset = NonspatialDimensionCount;

			for (uint32 Dim = 0; Dim < NumDimensions; ++Dim)
			{
				uint32 Padding;

				if (Direction == DML_CONVOLUTION_DIRECTION_FORWARD)
				{
					uint32 InputLen = uint32(InputShape[Dim + DimOffset]);
					uint32 StridedOutLen = (InputLen + Strides[Dim] - 1) / Strides[Dim];
					uint32 KernelLen = 1 + (WindowSize[Dim] - 1) * Dilations[Dim];
					uint32 Len = Strides[Dim] * (StridedOutLen - 1) + KernelLen;
					
					Padding = (Len <= InputLen) ? 0 : (Len - InputLen);
				}
				else
				{
					Padding = (InputShape[Dim + DimOffset] - 1) * Dilations[Dim] - Strides[Dim] + OutPadding[Dim] + 1;
				}

				if (AutoPad == EAutoPad::SAME_LOWER)
				{
					StartPadding[Dim] = (Padding + 1) / 2;
				}
				else
				{
					StartPadding[Dim] = Padding / 2;
				}

				EndPadding[Dim] = Padding - StartPadding[Dim];
			}
		}

		//
		//
		//
		void SetOutputShape(TConstArrayView<uint32> InputShape, TConstArrayView<uint32> FilterShape)
		{
			const uint32 DimOffset = NonspatialDimensionCount;

			OutputShape.SetNumUninitialized(InputShape.Num());

			if (Direction == DML_CONVOLUTION_DIRECTION_FORWARD)
			{
				OutputShape[0] = InputShape[0];
				OutputShape[1] = FilterShape[0];

				for (uint32 Dim = 0; Dim < NumDimensions; ++Dim)
				{
					uint32 InputLen = InputShape[Dim + DimOffset];
					uint32 PaddedLen = InputLen + StartPadding[Dim] + EndPadding[Dim];
					uint32 KernelLen = 1 + (WindowSize[Dim] - 1) * Dilations[Dim];

					checkf(KernelLen <= PaddedLen, TEXT("KernelLen must < PaddedLen"));
					checkf(Strides[Dim] != 0, TEXT("Strides must be != 0"));

					uint32 StridableOutLen = PaddedLen - KernelLen;
					uint32 OutLen = 1 + (StridableOutLen / Strides[Dim]);

					OutputShape[Dim + DimOffset] = OutLen;
				}
			}
			else
			{
				OutputShape[0] = InputShape[0];
				OutputShape[1] = FilterShape[1] * Group;

				for (uint32 Dim = 0; Dim < NumDimensions; ++Dim)
				{
					uint32 Padding = StartPadding[Dim] + EndPadding[Dim];
					uint32 KernelLen = 1 + (WindowSize[Dim] - 1) * Dilations[Dim];

					OutputShape[Dim + DimOffset] = (InputShape[Dim + DimOffset] - 1) * Strides[Dim] + KernelLen + OutPadding[Dim] - Padding;
				}
			}
		}
	};

public:

	static FOperatorDml* Create()
	{
		return new FOperatorDmlConv();
	}

	//
	//
	//
	virtual bool Initialize(IDMLDevice* Device, TArrayView<const NNECore::Internal::FTensor> InputTensors, TArrayView<const NNECore::Internal::FTensor> OutputTensors, const NNECore::FAttributeMap& Attributes) override
	{
		const NNECore::Internal::FTensor& InputTensor = InputTensors[0];
		const NNECore::Internal::FTensor& FilterTensor = InputTensors[1];
		
		FConvArgs	Args;
		
		if (!Args.Init(InputTensor.GetShape(), FilterTensor.GetShape(), Attributes))
		{
			return false;
		}

		NNECore::Internal::FTensor OutputTensor = OutputTensors[0];

		if (Direction == DML_CONVOLUTION_DIRECTION_FORWARD)
		{
			OutputTensor.SetShape(NNECore::FTensorShape::Make(Args.OutputShape));
		}

		// Initialize tensor descriptors
		DmlUtil::FTensorDesc	DmlInputTensor{};
		DmlUtil::FTensorDesc	DmlFilterTensor{};
		DmlUtil::FTensorDesc	DmlBiasTensor{};
		DmlUtil::FTensorDesc	DmlOutputTensor{};

		if (!DmlInputTensor.InitFromTensor(InputTensor, 3))
		{
			UE_LOG(LogNNE, Warning, TEXT("Failed to initialize tensor(s) for DML inference"));
			return false;
		}

		if (!DmlFilterTensor.InitFromTensor(FilterTensor, 3))
		{
			UE_LOG(LogNNE, Warning, TEXT("Failed to initialize tensor(s) for DML inference"));
			return false;
		}

		if (InputTensors.Num() > 2)
		{
			const NNECore::Internal::FTensor& BiasTensor = InputTensors[2];

			FSmallArray Shape;

			if (BiasTensor.GetShape().Rank() < (int32) Args.NumDimensions)
			{
				Shape.Add(1);
				Shape.Add(BiasTensor.GetShape().GetData()[0]);
				Shape.Add(1);

				for (int32 Dim = 3; Dim < InputTensor.GetShape().Rank(); ++Dim)
				{
					Shape.Add(1);
				}
			}
			else
			{
				Shape = BiasTensor.GetShape().GetData();
			}

			if (!DmlBiasTensor.InitFromTensor(BiasTensor, 3, MakeEmptyArrayView<uint32>(), Shape))
			{
				UE_LOG(LogNNE, Warning, TEXT("Failed to initialize tensor(s) for DML inference"));
				return false;
			}
		}

		if (!DmlOutputTensor.InitFromTensor(OutputTensor, 3))
		{
			UE_LOG(LogNNE, Warning, TEXT("Failed to initialize tensor(s) for DML inference"));
			return false;
		}

		DML_CONVOLUTION_OPERATOR_DESC	DmlConvOpDesc{};

		DmlConvOpDesc.InputTensor = &DmlInputTensor.Desc;
		DmlConvOpDesc.FilterTensor = &DmlFilterTensor.Desc;
		DmlConvOpDesc.BiasTensor = InputTensors.Num() > 2 ? &DmlBiasTensor.Desc : nullptr;
		DmlConvOpDesc.OutputTensor = &DmlOutputTensor.Desc;
		DmlConvOpDesc.Mode = DML_CONVOLUTION_MODE_CROSS_CORRELATION;
		DmlConvOpDesc.Direction = Direction;
		DmlConvOpDesc.DimensionCount = Args.NumDimensions;
		DmlConvOpDesc.Strides = (uint32*) Args.Strides.GetData();
		DmlConvOpDesc.Dilations = (uint32*) Args.Dilations.GetData();
		DmlConvOpDesc.StartPadding = Args.StartPadding.GetData();
		DmlConvOpDesc.EndPadding = Args.EndPadding.GetData();
		DmlConvOpDesc.OutputPadding = (uint32*) Args.OutPadding.GetData();
		DmlConvOpDesc.GroupCount = Args.Group;

		DML_OPERATOR_DESC DmlOpDesc{};

		DmlOpDesc.Type = DML_OPERATOR_CONVOLUTION;
		DmlOpDesc.Desc = &DmlConvOpDesc;

		return CreateOperator(Device, DmlOpDesc);
	}
};

void RegisterConvOperator()
{
	FOperatorRegistryDml::Get()->OpAdd(TEXT("Conv"), FOperatorDmlConv<DML_CONVOLUTION_DIRECTION_FORWARD>::Create);
}

void RegisterConvTransposeOperator()
{
	FOperatorRegistryDml::Get()->OpAdd(TEXT("ConvTranspose"), FOperatorDmlConv<DML_CONVOLUTION_DIRECTION_BACKWARD>::Create);
}

struct FDmlOperatorConvRegistrator
{
	FDmlOperatorConvRegistrator()
	{
		RegisterConvOperator();
		RegisterConvTransposeOperator();
	}
};

static FDmlOperatorConvRegistrator RegisterDmlOperatorConv;

} // namespace UE::NNERuntimeRDG::Private::Dml

#endif // NNE_USE_DIRECTML
