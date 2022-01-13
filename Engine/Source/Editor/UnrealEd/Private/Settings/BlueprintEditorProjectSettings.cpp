// Copyright Epic Games, Inc. All Rights Reserved.

#include "Settings/BlueprintEditorProjectSettings.h"
#include "UObject/UnrealType.h"
#include "Toolkits/ToolkitManager.h"
#include "BlueprintEditor.h"

/* UBlueprintEditorProjectSettings */

UBlueprintEditorProjectSettings::UBlueprintEditorProjectSettings(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, DefaultChildActorTreeViewMode(EChildActorComponentTreeViewVisualizationMode::ComponentOnly)
{
}

void UBlueprintEditorProjectSettings::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName Name = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;
	if (Name == GET_MEMBER_NAME_CHECKED(UBlueprintEditorProjectSettings, bEnableChildActorExpansionInTreeView))
	{
		if (!GEditor)
		{
			return;
		}

		// Find open blueprint editors and refresh them
		if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
			for (UObject* Asset : EditedAssets)
			{
				if (Asset && Asset->IsA<UBlueprint>())
				{
					TSharedPtr<IToolkit> AssetEditorPtr = FToolkitManager::Get().FindEditorForAsset(Asset);
					if (AssetEditorPtr.IsValid() && AssetEditorPtr->IsBlueprintEditor())
					{
						TSharedPtr<IBlueprintEditor> BlueprintEditorPtr = StaticCastSharedPtr<IBlueprintEditor>(AssetEditorPtr);
						BlueprintEditorPtr->RefreshEditors();
					}
				}
			}
		}

		// Deselect actors so we are forced to clear the current tree view
		// @todo - Figure out how to update the tree view directly instead?
		if (GEditor->GetSelectedActorCount() > 0)
		{
			const bool bNoteSelectionChange = true;
			const bool bDeselectBSPSurfaces = true;
			GEditor->SelectNone(bNoteSelectionChange, bDeselectBSPSurfaces);
		}
	}
}
