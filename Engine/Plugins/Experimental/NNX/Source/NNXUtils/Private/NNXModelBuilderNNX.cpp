// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXModelBuilder.h"
#include "NNXRuntimeFormat.h"
#include "NNXCore.h"

#include "Misc/StringBuilder.h"
#include "Serialization/MemoryWriter.h"

#define Print(Format, ...) UE_LOG(LogNNX, Display, Format, __VA_ARGS__)

namespace NNX
{

class FMLModelPrinterNNX
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
		
		for (uint32 Idx = 0; Idx < Tensor.Shape.Dimension; ++Idx)
		{
			Str << Tensor.Shape.Sizes[Idx];

			if (Idx + 1 < Tensor.Shape.Dimension)
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

inline int NNXTensorCast(IMLModelBuilder::HTensor& Handle)
{
	if (Handle.Type == IMLModelBuilder::HandleType::Tensor)
	{
		return int(reinterpret_cast<int64>(Handle.Ptr));
	}

	return -1;
}

inline int NNXOperatorCast(IMLModelBuilder::HOperator& Handle)
{
	if (Handle.Type == IMLModelBuilder::HandleType::Operator)
	{
		return int(reinterpret_cast<int64>(Handle.Ptr));
	}

	return -1;
}

/**
 * NNX format builder, create NNX format in memory
 */
class FMLModelBuilderNNX : public IMLModelBuilder
{
public:

	FMLModelBuilderNNX()
	{

	}

	virtual bool Begin(const FString& Name) override
	{
		return true;
	}

	virtual bool End(TArray<uint8>& Data) override
	{
		// This is for debugging purposes
		FMLModelPrinterNNX Printer;

		Printer.Visit(Format);

		FMemoryWriter Writer(Data);

		FMLRuntimeFormat::StaticStruct()->SerializeBin(Writer, &Format);

		return !Data.IsEmpty();
	}

	virtual HTensor AddTensor(const FString& Name, EMLTensorDataType DataType, TArrayView<const int32> Shape)
	{
		int Idx = AddTensor(Name, Shape, DataType, EMLFormatTensorType::None);
		
		return MakeTensorHandle(reinterpret_cast<void*>((int64) Idx));
	}

	/** Add model input */
	virtual bool AddInput(HTensor Tensor) override
	{
		int Idx = NNXTensorCast(Tensor);
		
		if (Idx < 0)
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to add input tensor, invalid tensor index"));
			return false;
		}
		
		Format.Tensors[Idx].Type = EMLFormatTensorType::Input;

		return true;
	}

	/** Add model output */
	virtual bool AddOutput(HTensor Tensor) override
	{
		int Idx = NNXTensorCast(Tensor);

		if (Idx < 0)
		{
			UE_LOG(LogNNX, Warning, TEXT("Failed to add output tensor, invalid tensor index"));
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
		int OpIdx = NNXOperatorCast(Op);
		int TensorIdx = NNXTensorCast(Tensor);
		
		// TODO: Set tensor type

		Format.Operators[OpIdx].InTensors.Emplace(TensorIdx);

		return true;
	}

	/** Add operator output */
	virtual bool AddOperatorOutput(HOperator Op, HTensor Tensor) override
	{
		int OpIdx = NNXOperatorCast(Op);
		int TensorIdx = NNXTensorCast(Tensor);

		// TODO: Set tensor type

		Format.Operators[OpIdx].OutTensors.Emplace(TensorIdx);

		return true;
	}

	/** Add operator attribute */
	virtual bool AddOperatorAttribute(HOperator Op, const FString& Name, const FMLAttributeValue& Value) override
	{
		int OpIdx = NNXOperatorCast(Op);

		FMLFormatAttributeDesc& Attribute = Format.Operators[OpIdx].Attributes.Emplace_GetRef();
		Attribute.Name = Name;
		Attribute.Value = Value;
		
		return true;
	}

private:

	int AddTensor(const FString& InName, TArrayView<const int32> InShape, EMLTensorDataType InDataType, EMLFormatTensorType InType)
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
			Desc.Type = InType;
			Desc.DataType = InDataType;
			Desc.DataOffset = 0;
			Desc.DataSize = 0;

			Format.Tensors.Add(Desc);
			Idx = Format.Tensors.Num() - 1;

			TensorMap.Add(InName, Idx);
		}

		return Idx;
	}


	FMLRuntimeFormat		Format;
	TMap<FString, int>		TensorMap;	
};

NNXUTILS_API IMLModelBuilder* CreateNNXModelBuilder()
{
	return new FMLModelBuilderNNX();
}

} // namespace NNX
