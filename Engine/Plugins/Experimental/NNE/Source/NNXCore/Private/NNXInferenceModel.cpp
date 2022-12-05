// Copyright Epic Games, Inc. All Rights Reserved.

#include "NNXInferenceModel.h"

UMLInferenceModel* UMLInferenceModel::CreateFromFormatDesc(const FNNIModelRaw& FormatDesc)
{
	UMLInferenceModel* Model = NewObject<UMLInferenceModel>((UObject*)GetTransientPackage(), UMLInferenceModel::StaticClass());

	if (!Model)
	{
		ensureMsgf(false, TEXT("Failed to create UMLInferenceModel from data"));
		return nullptr;
	}

	Model->FormatDesc = FormatDesc;
	return Model;
}

const FNNIModelRaw& UMLInferenceModel::GetFormatDesc() const
{
	return FormatDesc;
}