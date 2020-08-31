// Copyright Epic Games, Inc. All Rights Reserved.

#include "ULevelAssetEditor.h"

#include "LevelAssetEditorToolkit.h"
#include "Engine/Level.h"
#include "InteractiveToolsContext.h"

void ULevelAssetEditor::GetObjectsToEdit(TArray<UObject*>& InObjectsToEdit)
{
	InObjectsToEdit.Add(NewObject<ULevel>(this, NAME_None, RF_Transient));
}

TSharedPtr<FBaseAssetToolkit> ULevelAssetEditor::CreateToolkit()
{
	if (!InteractiveToolsContext)
	{
		InteractiveToolsContext = NewObject<UInteractiveToolsContext>(this);
	}

	return MakeShared<FLevelEditorAssetToolkit>(this, InteractiveToolsContext);
}
