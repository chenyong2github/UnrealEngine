// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTools/ImgMediaSourceActions.h"

#include "AssetRegistry/AssetData.h"
#include "ImgMediaSource.h"
#include "MediaPlayerEditorModule.h"
#include "Toolkits/MediaSourceEditorToolkit.h"


#define LOCTEXT_NAMESPACE "ImageMediaSourceAssetTypeActions"

bool FImgMediaSourceActions::CanFilter()
{
	return true;
}

FText FImgMediaSourceActions::GetAssetDescription(const struct FAssetData& AssetData) const
{
	const UImgMediaSource* ImgMediaSource = Cast<UImgMediaSource>(AssetData.GetAsset());

	if (ImgMediaSource != nullptr)
	{
		const FString Url = ImgMediaSource->GetUrl();

		if (Url.IsEmpty())
		{
			return LOCTEXT("ImgAssetTypeActions_URLMissing", "Warning: Missing URL detected!");
		}

		if (!ImgMediaSource->Validate())
		{
			return LOCTEXT("AssetTypeActions_ImgMediaSourceInvalid", "Warning: Invalid settings detected!");
		}
	}

	return FText::GetEmpty();
}

uint32 FImgMediaSourceActions::GetCategories()
{
	return EAssetTypeCategories::Media;
}

FText FImgMediaSourceActions::GetName() const
{
	return LOCTEXT("AssetTypeActions_ImgMediaSource", "Img Media Source");
}

UClass* FImgMediaSourceActions::GetSupportedClass() const
{
	return UImgMediaSource::StaticClass();
}

FColor FImgMediaSourceActions::GetTypeColor() const
{
	return FColor::White;
}

void FImgMediaSourceActions::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid()
		? EToolkitMode::WorldCentric
		: EToolkitMode::Standalone;

	for (auto ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		auto MediaSource = Cast<UMediaSource>(*ObjIt);

		if (MediaSource != nullptr)
		{
			IMediaPlayerEditorModule* MediaPlayerEditorModule = FModuleManager::LoadModulePtr<IMediaPlayerEditorModule>("MediaPlayerEditor");
			if (MediaPlayerEditorModule != nullptr)
			{
				TSharedPtr<ISlateStyle> Style = MediaPlayerEditorModule->GetStyle();

				TSharedRef<FMediaSourceEditorToolkit> EditorToolkit = MakeShareable(new FMediaSourceEditorToolkit(Style.ToSharedRef()));
				EditorToolkit->Initialize(MediaSource, Mode, EditWithinLevelEditor);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
