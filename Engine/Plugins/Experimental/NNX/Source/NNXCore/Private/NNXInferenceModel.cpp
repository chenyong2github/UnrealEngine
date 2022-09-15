// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXInferenceModel.h"

UMLInferenceModel* UMLInferenceModel::CreateFromData(EMLInferenceFormat Format, TArrayView<uint8> Data)
{
	UMLInferenceModel* Model = NewObject<UMLInferenceModel>((UObject*)GetTransientPackage(), UMLInferenceModel::StaticClass());

	if (!Model)
	{
		ensureMsgf(false, TEXT("Failed to create UMLInferenceModel from data"));
		return nullptr;
	}

	Model->Format = Format;
	Model->Data.SetNum(Data.Num());
	FMemory::Memcpy(Model->Data.GetData(), Data.GetData(), Data.Num());

	return Model;
}

UMLInferenceModel::UMLInferenceModel()
	: Format(EMLInferenceFormat::Invalid)
{
}

EMLInferenceFormat UMLInferenceModel::GetFormat() const
{
	return Format;
}

// Return model data
const TArray<uint8>& UMLInferenceModel::GetData() const
{
	return Data;
}

// Return data size in bytes
uint32 UMLInferenceModel::GetDataSize() const
{
	return Data.Num();
}
