// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNEUtilsModelBuilder.h"
#include "NNECore.h"
#include "NNECoreAttributeMap.h"
#include "NNXRuntimeFormat.h"

#include "Misc/StringBuilder.h"
#include "Serialization/MemoryWriter.h"

#define Print(Format, ...) UE_LOG(LogNNE, Display, Format, __VA_ARGS__)

namespace UE::NNEUtils::Internal
{

class FModelPrinterNNE
{
public:

	void Visit(const FMLRuntimeFormat& Format)
	{
		for (int Idx = 0; Idx < Format.Tensors.Num(); ++Idx)
		{
			Visit(Format.Tensors[Idx]);
		}

		for (int Idx = 0; Idx < Format.Operators.Num(); ++Idx)
		{
			Visit(Format.Operators[Idx]);
		}
	}

	void Visit(const FMLFormatTensorDesc& Tensor)
	{
		FStringBuilderBase Str;

		Str << TEXT("[");
		
		for (int32 Idx = 0; Idx < Tensor.Shape.Num(); ++Idx)
		{
			Str << Tensor.Shape[Idx];

			if (Idx + 1 < Tensor.Shape.Num())
			{
				Str << TEXT(",");
			}
		}
		
		Str << TEXT("]");

		Print(TEXT("Tensor:%s %s"), *Tensor.Name, Str.ToString());
		
	}

	void Visit(const FMLFormatOperatorDesc& Op)
	{
		Print(TEXT("Op:%s in:%d out:%d"), *Op.TypeName, Op.InTensors.Num(), Op.OutTensors.Num());
	}
};

inline int NNETensorCast(IModelBuilder::HTensor& Handle)
{
	if (Handle.Type == IModelBuilder::HandleType::Tensor)
	{
		return int(reinterpret_cast<int64>(Handle.Ptr));
	}

	return -1;
}

inline int NNEOperatorCast(IModelBuilder::HOperator& Handle)
{
	if (Handle.Type == IModelBuilder::HandleType::Operator)
	{
		return int(reinterpret_cast<int64>(Handle.Ptr));
	}

	return -1;
}

/**
 * NNE format builder, create NNE format in memory
 */
class FModelBuilderNNE : public IModelBuilder
{
public:

	FModelBuilderNNE()
	{

	}

	virtual bool Begin(const FString& Name) override
	{
		return true;
	}

	virtual bool End(TArray<uint8>& Data) override
	{
		// This is for debugging purposes
		FModelPrinterNNE Printer;

		Printer.Visit(Format);

		FMemoryWriter Writer(Data);

		FMLRuntimeFormat::StaticStruct()->SerializeBin(Writer, &Format);

		return !Data.IsEmpty();
	}

	virtual HTensor AddTensor(const FString& Name, ENNETensorDataType DataType, TArrayView<const int32> Shape, const void* Data, uint64 DataSize)
	{
		TArray<int32, TInlineAllocator<NNECore::FTensorShape::MaxRank>> NNIShape;
		for (int i = 0; i < Shape.Num(); ++i)
		{
			//ORT Graph return 0 for variable dimensions, NNI use -1.
			NNIShape.Emplace(Shape[i] == 0 ? -1 : Shape[i]);
		}
		
		int Idx = AddTensor(Name, NNIShape, DataType, Data, DataSize);

		return MakeTensorHandle(reinterpret_cast<void*>((int64) Idx));
	}

	/** Add model input */
	virtual bool AddInput(HTensor Tensor) override
	{
		int Idx = NNETensorCast(Tensor);
		
		if (Idx < 0 || Idx >= Format.Tensors.Num())
		{
			UE_LOG(LogNNE, Warning, TEXT("Failed to add input tensor, invalid tensor index"));
			return false;
		}
		
		if (Format.Tensors[Idx].Type != EMLFormatTensorType::None)
		{
			UE_LOG(LogNNE, Warning, TEXT("Failed to add input tensor, tensor usage already set up"));
			return false;
		}
		
		Format.Tensors[Idx].Type = EMLFormatTensorType::Input;

		return true;
	}

	/** Add model output */
	virtual bool AddOutput(HTensor Tensor) override
	{
		int Idx = NNETensorCast(Tensor);

		if (Idx < 0 || Idx >= Format.Tensors.Num())
		{
			UE_LOG(LogNNE, Warning, TEXT("Failed to add output tensor, invalid tensor index"));
			return false;
		}

		if (Format.Tensors[Idx].Type != EMLFormatTensorType::None)
		{
			UE_LOG(LogNNE, Warning, TEXT("Failed to add output tensor, tensor usage already set up"));
			return false;
		}

		Format.Tensors[Idx].Type = EMLFormatTensorType::Output;

		return true;
	}

	/** Add operator */
	virtual HOperator AddOperator(const FString& TypeName, const FString& Name = TEXT("")) override
	{
		int Idx = Format.Operators.Num();

		FMLFormatOperatorDesc	Operator{};

		Operator.TypeName = TypeName;
		Format.Operators.Emplace(Operator);

		return MakeOperatorHandle(reinterpret_cast<void*>((int64) Idx));
	}

	/** Add operator input */
	virtual bool AddOperatorInput(HOperator Op, HTensor Tensor) override
	{
		int OpIdx = NNEOperatorCast(Op);
		int TensorIdx = NNETensorCast(Tensor);
		
		if (TensorIdx < 0 || TensorIdx >= Format.Tensors.Num())
		{
			UE_LOG(LogNNE, Warning, TEXT("Failed to add operator input tensor, invalid tensor index"));
			return false;
		}

		Format.Operators[OpIdx].InTensors.Emplace(TensorIdx);

		return true;
	}

	/** Add operator output */
	virtual bool AddOperatorOutput(HOperator Op, HTensor Tensor) override
	{
		int OpIdx = NNEOperatorCast(Op);
		int TensorIdx = NNETensorCast(Tensor);
		
		if (TensorIdx < 0 || TensorIdx >= Format.Tensors.Num())
		{
			UE_LOG(LogNNE, Warning, TEXT("Failed to add operator output tensor, invalid tensor index"));
			return false;
		}

		if (Format.Tensors[TensorIdx].Type == EMLFormatTensorType::Input)
		{
			UE_LOG(LogNNE, Warning, TEXT("Failed to add output tensor, tensor usage already set up to input"));
			return false;
		}

		if (Format.Tensors[TensorIdx].Type == EMLFormatTensorType::None)
		{
			Format.Tensors[TensorIdx].Type = EMLFormatTensorType::Intermediate;
		}
		
		Format.Operators[OpIdx].OutTensors.Emplace(TensorIdx);

		return true;
	}

	/** Add operator attribute */
	virtual bool AddOperatorAttribute(HOperator Op, const FString& Name, const FNNEAttributeValue& Value) override
	{
		int OpIdx = NNEOperatorCast(Op);

		FMLFormatAttributeDesc& Attribute = Format.Operators[OpIdx].Attributes.Emplace_GetRef();
		Attribute.Name = Name;
		Attribute.Value = Value;
		
		return true;
	}

private:

	int AddTensor(const FString& InName, TArrayView<const int32> InShape, ENNETensorDataType InDataType, const void* Data, uint64 DataSize)
	{
		int Idx = -1;

		int* Val = TensorMap.Find(InName);

		if (Val)
		{
			Idx = *Val;
		}
		else
		{
			FMLFormatTensorDesc	Desc{};

			Desc.Name = InName;
			Desc.Shape = InShape;
			Desc.Type = EMLFormatTensorType::None;
			Desc.DataType = InDataType;
			
			if (Data)
			{
				Desc.Type = EMLFormatTensorType::Initializer;
				Desc.DataOffset = Format.TensorData.AddUninitialized(DataSize);
				Desc.DataSize = DataSize;

				FMemory::Memcpy(Format.TensorData.GetData() + Desc.DataOffset, Data, DataSize);
			}
			else
			{
				Desc.DataOffset = 0;
				Desc.DataSize = 0;
			}

			Format.Tensors.Add(Desc);
			Idx = Format.Tensors.Num() - 1;

			TensorMap.Add(InName, Idx);
		}

		return Idx;
	}


	FMLRuntimeFormat		Format;
	TMap<FString, int>		TensorMap;	
};

NNEUTILS_API IModelBuilder* CreateNNEModelBuilder()
{
	return new FModelBuilderNNE();
}

} // namespace UE::NNEUtils::Internal
