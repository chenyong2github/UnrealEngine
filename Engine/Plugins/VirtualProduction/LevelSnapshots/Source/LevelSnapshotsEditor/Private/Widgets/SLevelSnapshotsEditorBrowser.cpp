// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SLevelSnapshotsEditorBrowser.h"

#include "ILevelSnapshotsEditorView.h"
#include "LevelSnapshot.h"
#include "LevelSnapshotsEditorData.h"

#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "LevelSnapshotsLog.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor/EditorStyle/Public/EditorStyleSet.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "Slate/Public/Framework/MultiBox/MultiBoxBuilder.h"
#include "Toolkits/GlobalEditorCommonCommands.h"

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
	AssetPickerConfig.SelectionMode = ESelectionMode::Single;
	AssetPickerConfig.OnAssetDoubleClicked = FOnAssetSelected::CreateSP(this, &SLevelSnapshotsEditorBrowser::OnAssetDoubleClicked);
	AssetPickerConfig.OnShouldFilterAsset = FOnShouldFilterAsset::CreateSP(this, &SLevelSnapshotsEditorBrowser::OnShouldFilterAsset);
	AssetPickerConfig.OnGetAssetContextMenu = FOnGetAssetContextMenu::CreateSP(this, &SLevelSnapshotsEditorBrowser::OnGetAssetContextMenu);

	ChildSlot
		[
			ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
		];
}

void SLevelSnapshotsEditorBrowser::SelectAsset(const FAssetData& InAssetData) const
{
	SCOPED_SNAPSHOT_EDITOR_TRACE(SelectSnapshotAsset);
	
	FScopedSlowTask SelectSnapshot(100.f, LOCTEXT("SelectSnapshotKey", "Loading snapshot"));
	SelectSnapshot.EnterProgressFrame(60.f);
	SelectSnapshot.MakeDialog();
	
	TSharedPtr<FLevelSnapshotsEditorViewBuilder> Builder = BuilderPtr.Pin();
	ULevelSnapshot* Snapshot = Cast<ULevelSnapshot>(InAssetData.GetAsset());

	SelectSnapshot.EnterProgressFrame(40.f);
	if (ensure(Builder) && ensure(Snapshot))
	{
		Builder->EditorDataPtr->SetActiveSnapshot(Snapshot);
	}
}

void SLevelSnapshotsEditorBrowser::OnAssetDoubleClicked(const FAssetData& InAssetData) const
{
	SelectAsset(InAssetData);
}

bool SLevelSnapshotsEditorBrowser::OnShouldFilterAsset(const FAssetData& InAssetData) const
{
	const FString SnapshotMapPath = InAssetData.GetTagValueRef<FString>("MapPath");

	const bool bShouldFilter = SnapshotMapPath != OwningWorldPathAttribute.Get().ToString();
	
	return bShouldFilter;
}

TSharedPtr<SWidget> SLevelSnapshotsEditorBrowser::OnGetAssetContextMenu(const TArray<FAssetData>& SelectedAssets)
{
	if (SelectedAssets.Num() <= 0)
	{
		return nullptr;
	}

	UObject* SelectedAsset = SelectedAssets[0].GetAsset();
	if (SelectedAsset == nullptr)
	{
		return nullptr;
	}
	
	FMenuBuilder MenuBuilder(true, MakeShared<FUICommandList>());

	MenuBuilder.BeginSection(TEXT("Asset"), NSLOCTEXT("ReferenceViewerSchema", "AssetSectionLabel", "Asset"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("Browse", "Browse to Asset"),
			LOCTEXT("BrowseTooltip", "Browses to the associated asset and selects it in the most recently used Content Browser (summoning one if necessary)"),
			FSlateIcon(FEditorStyle::GetStyleSetName(), "SystemWideCommands.FindInContentBrowser.Small"),
			FUIAction(
				FExecuteAction::CreateLambda([SelectedAsset] ()
				{
					if (SelectedAsset)
					{
						const TArray<FAssetData>& Assets = { SelectedAsset };
						FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
						ContentBrowserModule.Get().SyncBrowserToAssets(Assets);
					}
				}),
				FCanExecuteAction::CreateLambda([] () { return true; })
			)
		);

		MenuBuilder.AddMenuEntry(
		LOCTEXT("OpenSnapshot", "Open Snapshot in Editor"),
		LOCTEXT("OpenSnapshotToolTip", "Open this snapshot in the Level Snapshots Editor."),
		FSlateIcon(FEditorStyle::GetStyleSetName(), "SystemWideCommands.SummonOpenAssetDialog"),
			FUIAction(
				FExecuteAction::CreateLambda([this, SelectedAsset] ()
				{
					if (SelectedAsset)
					{
						SelectAsset(SelectedAsset);
					}
				}),
				FCanExecuteAction::CreateLambda([] () { return true; })
			)
	);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE
