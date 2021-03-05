// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetTypeActions/AssetTypeActions_LensFile.h"

#include "AssetEditor/LensFileEditorToolkit.h"
#include "LensFile.h"

#define LOCTEXT_NAMESPACE "LensFileTypeActions"

FText FAssetTypeActions_LensFile::GetName() const
{
	return LOCTEXT("AssetTypeActions_LensFile", "Lens File");
}

UClass* FAssetTypeActions_LensFile::GetSupportedClass() const
{
	return ULensFile::StaticClass();
}

//void FAssetTypeActions_LensFile::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
//{
//	for (UObject* Object : InObjects)
//	{
//		if (ULensFile* Asset = Cast<ULensFile>(Object))
//		{
//			FLensFileEditorToolkit::CreateEditor(EToolkitMode::Standalone, EditWithinLevelEditor, Asset);
//		}
//	}
//}

#undef LOCTEXT_NAMESPACE
