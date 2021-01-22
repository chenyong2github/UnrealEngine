// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SLevelSnapshotsEditorBrowser.h"

#include "ILevelSnapshotsEditorView.h"
#include "LevelSnapshot.h"

#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

void SLevelSnapshotsEditorBrowser::Construct(const FArguments& InArgs, const TSharedRef<FLevelSnapshotsEditorViewBuilder>& InBuilder)
{
	OwningWorldPathAttribute = InArgs._OwningWorldPath;
	BuilderPtr = InBuilder;

	check(OwningWorldPathAttribute.IsSet());

	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	FARFilter ARFilter;
	ARFilter.ClassNames.Add(ULevelSnapshot::StaticClass()->GetFName());

	FAssetPickerConfig AssetPickerConfig;
	AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
	AssetPickerConfig.bFocusSearchBoxWhenOpened = true;
	AssetPickerConfig.bAllowNullSelection = false;
	AssetPickerConfig.bShowBottomToolbar = false;
	AssetPickerConfig.bAutohideSearchBar = false;
	AssetPickerConfig.bAllowDragging = false;
	AssetPickerConfig.bCanShowClasses = false;
	AssetPickerConfig.bShowPathInColumnView = true;
	AssetPickerConfig.bShowTypeInColumnView = false;
	AssetPickerConfig.bSortByPathInColumnView = false;
	AssetPickerConfig.SaveSettingsName = TEXT("GlobalAssetPicker");
	AssetPickerConfig.ThumbnailScale = 0.8f;
	AssetPickerConfig.Filter = ARFilter;
	AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &SLevelSnapshotsEditorBrowser::OnAssetSelected);
	AssetPickerConfig.OnShouldFilterAsset = FOnShouldFilterAsset::CreateSP(this, &SLevelSnapshotsEditorBrowser::OnShouldFilterAsset);

	ChildSlot
		[
			ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
		];
}

void SLevelSnapshotsEditorBrowser::OnAssetSelected(const FAssetData& InAssetData)
{
	TSharedPtr<FLevelSnapshotsEditorViewBuilder> Builder = BuilderPtr.Pin();
	check(Builder.IsValid());

	Builder->OnSnapshotSelected.Broadcast(Cast<ULevelSnapshot>(InAssetData.GetAsset()));
}

bool SLevelSnapshotsEditorBrowser::OnShouldFilterAsset(const FAssetData& InAssetData) const
{
	const FString SnapshotMapPath = InAssetData.GetTagValueRef<FString>("MapPath");

	const bool bShouldFilter = SnapshotMapPath != OwningWorldPathAttribute.Get().ToString();
	
	return bShouldFilter;
}

#undef LOCTEXT_NAMESPACE
