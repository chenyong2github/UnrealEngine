// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTools/MediaSourceManagerActions.h"

#include "AssetRegistry/AssetData.h"
#include "MediaSourceManager.h"
#include "Toolkits/MediaSourceManagerEditorToolkit.h"

#define LOCTEXT_NAMESPACE "MediaSourceManagerActions"

/* FMediaSourceManagerActions constructors
 *****************************************************************************/

FMediaSourceManagerActions::FMediaSourceManagerActions(const TSharedRef<ISlateStyle>& InStyle)
	: Style(InStyle)
{
}

/* FAssetTypeActions_Base interface
 *****************************************************************************/

bool FMediaSourceManagerActions::CanFilter()
{
	return true;
}

FText FMediaSourceManagerActions::GetAssetDescription(const struct FAssetData& AssetData) const
{
	return FText::GetEmpty();
}

uint32 FMediaSourceManagerActions::GetCategories()
{
	return EAssetTypeCategories::Media;
}

FText FMediaSourceManagerActions::GetName() const
{
	return LOCTEXT("MediaSourceManager", "Media Source Manager");
}

UClass* FMediaSourceManagerActions::GetSupportedClass() const
{
	return UMediaSourceManager::StaticClass();
}

FColor FMediaSourceManagerActions::GetTypeColor() const
{
	return FColor::White;
}

void FMediaSourceManagerActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<class IToolkitHost> EditWithinLevelEditor)
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid()
		? EToolkitMode::WorldCentric
		: EToolkitMode::Standalone;

	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		UMediaSourceManager* MediaSourceManager = Cast<UMediaSourceManager>(*ObjIt);

		if (MediaSourceManager != nullptr)
		{
			TSharedRef<FMediaSourceManagerEditorToolkit> EditorToolkit = MakeShareable(new FMediaSourceManagerEditorToolkit(Style));
			EditorToolkit->Initialize(MediaSourceManager, Mode, EditWithinLevelEditor);
		}
	}
}

#undef LOCTEXT_NAMESPACE
