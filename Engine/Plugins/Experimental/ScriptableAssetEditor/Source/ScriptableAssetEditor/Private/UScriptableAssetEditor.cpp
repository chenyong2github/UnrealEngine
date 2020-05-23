// Copyright Epic Games, Inc. All Rights Reserved.

#include "UScriptableAssetEditor.h"
#include "ExampleAssetToolkit.h"
#include "Engine/Level.h"

void UScriptableAssetEditor::GetObjectsToEdit(TArray<UObject*>& InObjectsToEdit)
{
	InObjectsToEdit.Add(NewObject<ULevel>(this, NAME_None, RF_Transient));
}

TSharedPtr<FBaseAssetToolkit> UScriptableAssetEditor::CreateToolkit()
{
	return MakeShareable(new FExampleAssetToolkit(this));
}
