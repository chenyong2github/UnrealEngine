// Copyright Epic Games, Inc. All Rights Reserved.

#include "SmartObjectAssetEditor.h"

#include "SmartObjectAssetToolkit.h"

void USmartObjectAssetEditor::GetObjectsToEdit(TArray<UObject*>& OutObjectsToEdit)
{
	OutObjectsToEdit.Add(ObjectToEdit);
}

void USmartObjectAssetEditor::SetObjectToEdit(UObject* InObject)
{
	ObjectToEdit = InObject;
}

TSharedPtr<FBaseAssetToolkit> USmartObjectAssetEditor::CreateToolkit()
{
	return MakeShared<FSmartObjectAssetToolkit>(this);
}
