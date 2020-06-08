// Copyright Epic Games, Inc. All Rights Reserved.

#include "UScriptableAssetEditor.h"

#include "ExampleAssetToolkit.h"
#include "Engine/Level.h"
#include "InteractiveToolsContext.h"

void UScriptableAssetEditor::GetObjectsToEdit(TArray<UObject*>& InObjectsToEdit)
{
	InObjectsToEdit.Add(NewObject<ULevel>(this, NAME_None, RF_Transient));
}

TSharedPtr<FBaseAssetToolkit> UScriptableAssetEditor::CreateToolkit()
{
	if (!InteractiveToolsContext)
	{
		InteractiveToolsContext = NewObject<UInteractiveToolsContext>(this);
	}

	return MakeShared<FExampleAssetToolkit>(this, InteractiveToolsContext);
}
