// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineAssetEditor.h"
#include "MoviePipelineConfigAssetEditor.h"

TSharedPtr<FBaseAssetToolkit> UMoviePipelineAssetEditor::CreateToolkit()
{
	return MakeShared<FMoviePipelineConfigAssetEditor>(this);
}

void UMoviePipelineAssetEditor::GetObjectsToEdit(TArray<UObject*>& InObjectsToEdit)
{
	InObjectsToEdit.Add(ObjectToEdit);
}

void UMoviePipelineAssetEditor::SetObjectToEdit(UObject* InObject)
{
	ObjectToEdit = InObject;
}