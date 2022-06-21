// Copyright Epic Games, Inc. All Rights Reserved.

#include "Toolkits/MediaSourceManagerEditorToolkit.h"

#include "Editor.h"
#include "EditorReimportHandler.h"
#include "MediaSourceManager.h"
#include "MediaTexture.h"
#include "SlateOptMacros.h"
#include "Styling/AppStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SMediaPlayerEditorViewer.h"
#include "Widgets/SMediaSourceManagerSources.h"

#define LOCTEXT_NAMESPACE "FMediaSourceManagerEditorToolkit"

namespace MediaSourceManagerEditorToolkit
{
	static const FName AppIdentifier("MediaSourceManagerEditorApp");
	static const FName SourcesTabId("Sources");
	static const FName ViewerTabId("Viewer");
}

FMediaSourceManagerEditorToolkit::FMediaSourceManagerEditorToolkit(const TSharedRef<ISlateStyle>& InStyle)
	: MediaSourceManager(nullptr)
	, Style(InStyle)
{
}

FMediaSourceManagerEditorToolkit::~FMediaSourceManagerEditorToolkit()
{
	FReimportManager::Instance()->OnPreReimport().RemoveAll(this);
	FReimportManager::Instance()->OnPostReimport().RemoveAll(this);

	GEditor->UnregisterForUndo(this);
}

void FMediaSourceManagerEditorToolkit::Initialize(UMediaSourceManager* InMediaSourceManager,
	const EToolkitMode::Type InMode, const TSharedPtr<IToolkitHost>& InToolkitHost)
{
	MediaSourceManager = InMediaSourceManager;

	if (MediaSourceManager == nullptr)
	{
		return;
	}

	// support undo/redo
	MediaSourceManager->SetFlags(RF_Transactional);
	GEditor->RegisterForUndo(this);

	// create tab layout
	TSharedRef<FTabManager::FArea> PrimaryArea = FTabManager::NewPrimaryArea()
		->SetOrientation(Orient_Horizontal);
	TSharedRef<FTabManager::FSplitter> PreviewSplitter = FTabManager::NewSplitter();
	PreviewSplitter->SetOrientation(Orient_Vertical);

	// Base tab name for each preview window.
	FString TabId = MediaSourceManagerEditorToolkit::ViewerTabId.ToString();
	int32 OriginalLength = TabId.Len();

	// Add each channel.
	PreviewTabIds.SetNum(MediaSourceManager->Channels.Num());
	for (int Index = 0; Index < MediaSourceManager->Channels.Num(); ++Index)
	{
		// Construct tab name,
		TabId.LeftInline(OriginalLength, false);
		TabId.AppendInt(Index);
		PreviewTabIds[Index] = FName(*TabId);

		// Add to splitter.
		PreviewSplitter->Split
		(
			FTabManager::NewStack()
			->AddTab(FName(*TabId), ETabState::OpenedTab)
		);
	}

	PrimaryArea->Split
	(
		// Sources tab.
		FTabManager::NewStack()
		->AddTab(MediaSourceManagerEditorToolkit::SourcesTabId, ETabState::OpenedTab)
		->SetHideTabWell(true)
		->SetSizeCoefficient(0.4f)
	)
	->Split
	(
		// Channel previews.
		PreviewSplitter
	);

	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("Standalone_MediaSourceManagerEditor_v0.1d4")
		->AddArea
		(
			PrimaryArea
		);

	FAssetEditorToolkit::InitAssetEditor(
		InMode,
		InToolkitHost,
		MediaSourceManagerEditorToolkit::AppIdentifier,
		Layout,
		true,
		true,
		MediaSourceManager
	);
	
	RegenerateMenusAndToolbars();
}

FString FMediaSourceManagerEditorToolkit::GetDocumentationLink() const
{
	return FString(TEXT("WorkingWithMedia/IntegratingMedia/MediaFramework"));
}

void FMediaSourceManagerEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(
		LOCTEXT("WorkspaceMenu_MediaSourcesManager", "Media Sources Manager Editor"));
	auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	// Sources tab.
	InTabManager->RegisterTabSpawner(MediaSourceManagerEditorToolkit::SourcesTabId,
		FOnSpawnTab::CreateSP(this, &FMediaSourceManagerEditorToolkit::HandleTabManagerSpawnTab, 
			MediaSourceManagerEditorToolkit::SourcesTabId))
		.SetDisplayName(LOCTEXT("SourcesTabName", "Sources"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

	// Viewer tab.
	for (int Index = 0; Index < PreviewTabIds.Num(); Index++)
	{
		InTabManager->RegisterTabSpawner(PreviewTabIds[Index],
			FOnSpawnTab::CreateSP(this, &FMediaSourceManagerEditorToolkit::HandlePreviewTabManagerSpawnTab,
				Index))
			.SetDisplayName(FText::Format(LOCTEXT("ChannelTabName", "Channel {0}"),
				FText::AsNumber(Index)))
			.SetGroup(WorkspaceMenuCategoryRef)
			.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "MediaPlateEditor.Tabs.Player"));
	}
}

void FMediaSourceManagerEditorToolkit::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	for (int Index = 0; Index < PreviewTabIds.Num(); Index++)
	{
		InTabManager->UnregisterTabSpawner(PreviewTabIds[Index]);
	}
	InTabManager->UnregisterTabSpawner(MediaSourceManagerEditorToolkit::SourcesTabId);
}

FText FMediaSourceManagerEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Media Source Manager Editor");
}

FName FMediaSourceManagerEditorToolkit::GetToolkitFName() const
{
	return FName("MediaSourceManagerEditor");
}

FLinearColor FMediaSourceManagerEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.3f, 0.2f, 0.5f, 0.5f);
}

FString FMediaSourceManagerEditorToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "MediaSourceManager ").ToString();
}

void FMediaSourceManagerEditorToolkit::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(MediaSourceManager);
}

void FMediaSourceManagerEditorToolkit::PostUndo(bool bSuccess)
{
}

void FMediaSourceManagerEditorToolkit::PostRedo(bool bSuccess)
{
	PostUndo(bSuccess);
}

TSharedRef<SDockTab> FMediaSourceManagerEditorToolkit::HandleTabManagerSpawnTab(const FSpawnTabArgs& Args, FName TabIdentifier)
{
	TSharedPtr<SWidget> TabWidget = SNullWidget::NullWidget;

	if (TabIdentifier == MediaSourceManagerEditorToolkit::SourcesTabId)
	{
		TabWidget = SNew(SMediaSourceManagerSources, *MediaSourceManager);
	}

	return SNew(SDockTab)
		.TabRole(ETabRole::PanelTab)
		[
			TabWidget.ToSharedRef()
		];
}


TSharedRef<SDockTab> FMediaSourceManagerEditorToolkit::HandlePreviewTabManagerSpawnTab(
	const FSpawnTabArgs& Args, int32 ChannelIndex)
{
	TSharedPtr<SWidget> TabWidget = SNullWidget::NullWidget;

	if (MediaSourceManager != nullptr)
	{
		if (ChannelIndex < MediaSourceManager->Channels.Num())
		{
			UMediaSourceManagerChannel* Channel = MediaSourceManager->Channels[ChannelIndex];
			if (Channel != nullptr)
			{
				UMediaPlayer* MediaPlayer = Channel->GetMediaPlayer();
				UMediaTexture* MediaTexture = Cast<UMediaTexture>(Channel->OutTexture);
				TabWidget = SNew(SMediaPlayerEditorViewer, *MediaPlayer, MediaTexture, Style, false);
			}
		}
	}
	
	return SNew(SDockTab)
		.TabRole(ETabRole::PanelTab)
		[
			TabWidget.ToSharedRef()
		];
}

#undef LOCTEXT_NAMESPACE
