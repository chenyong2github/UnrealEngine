// Copyright Epic Games, Inc. All Rights Reserved.

#include "UExampleAssetEditor.h"

#include "ExampleAssetToolkit.h"
#include "Engine/Level.h"
#include "InteractiveToolsContext.h"

void UExampleAssetEditor::GetObjectsToEdit(TArray<UObject*>& InObjectsToEdit)
{
	InObjectsToEdit.Add(NewObject<ULevel>(this, NAME_None, RF_Transient));
}

TSharedPtr<FBaseAssetToolkit> UExampleAssetEditor::CreateToolkit()
{
	if (!InteractiveToolsContext)
	{
		InteractiveToolsContext = NewObject<UInteractiveToolsContext>(this);
	}

	return MakeShared<FExampleAssetToolkit>(this, InteractiveToolsContext);
}
