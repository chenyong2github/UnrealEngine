// Copyright Epic Games, Inc. All Rights Reserved.

#include "Toolkits/MediaPlateEditorToolkit.h"

#include "Editor.h"
#include "EditorStyleSet.h"
#include "EditorReimportHandler.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "MediaPlate.h"
#include "MediaPlayer.h"
#include "MediaPlaylist.h"
#include "Models/MediaPlateEditorCommands.h"
#include "SlateOptMacros.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SMediaPlayerEditorViewer.h"

#define LOCTEXT_NAMESPACE "FMediaPlateEditorToolkit"

namespace MediaPlateEditorToolkit
{
	static const FName AppIdentifier("MediaPlateEditorApp");
	static const FName DetailsTabId("Details");
	static const FName InfoTabId("Info");
	static const FName MediaTabId("Media");
	static const FName PlaylistTabId("Playlist");
	static const FName StatsTabId("Stats");
	static const FName ViewerTabId("Viewer");
}

/* FMediaPlateEditorToolkit structors
 *****************************************************************************/

FMediaPlateEditorToolkit::FMediaPlateEditorToolkit(const TSharedRef<ISlateStyle>& InStyle)
	: MediaPlate(nullptr)
	, Style(InStyle)
{
}

FMediaPlateEditorToolkit::~FMediaPlateEditorToolkit()
{
	FReimportManager::Instance()->OnPreReimport().RemoveAll(this);
	FReimportManager::Instance()->OnPostReimport().RemoveAll(this);

	GEditor->UnregisterForUndo(this);
}

/* FMediaPlateEditorToolkit interface
 *****************************************************************************/

void FMediaPlateEditorToolkit::Initialize(AMediaPlate* InMediaPlate, const EToolkitMode::Type InMode, const TSharedPtr<IToolkitHost>& InToolkitHost)
{
	MediaPlate = InMediaPlate;

	if (MediaPlate == nullptr)
	{
		return;
	}

	// support undo/redo
	MediaPlate->SetFlags(RF_Transactional);
	GEditor->RegisterForUndo(this);

	BindCommands();

	// create tab layout
	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("Standalone_MediaPlateEditor_v1")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
				->SetOrientation(Orient_Horizontal)
				->Split
				(
					FTabManager::NewSplitter()
						->SetOrientation(Orient_Vertical)
						->Split
						(
							// viewer
							FTabManager::NewStack()
								->AddTab(MediaPlateEditorToolkit::ViewerTabId, ETabState::OpenedTab)
								->SetHideTabWell(true)
								->SetSizeCoefficient(0.6f)
						)
						->Split
						(
							// media library
							FTabManager::NewStack()
								->AddTab(MediaPlateEditorToolkit::MediaTabId, ETabState::OpenedTab)
								->SetHideTabWell(true)
								->SetSizeCoefficient(0.3f)
						)
				)
				->Split
				(
					FTabManager::NewSplitter()
						->SetOrientation(Orient_Vertical)
						->SetSizeCoefficient(0.33f)
						->Split
						(
							// playlist
							FTabManager::NewStack()
								->AddTab(MediaPlateEditorToolkit::PlaylistTabId, ETabState::OpenedTab)
								->SetSizeCoefficient(0.5f)
						)
						->Split
						(
							// details, info, stats
							FTabManager::NewStack()
								->AddTab(MediaPlateEditorToolkit::DetailsTabId, ETabState::OpenedTab)
								->AddTab(MediaPlateEditorToolkit::InfoTabId, ETabState::OpenedTab)
								->AddTab(MediaPlateEditorToolkit::StatsTabId, ETabState::ClosedTab)
								->SetForegroundTab(MediaPlateEditorToolkit::DetailsTabId)
								->SetSizeCoefficient(0.5f)
						)
				)
		);

	FAssetEditorToolkit::InitAssetEditor(
		InMode,
		InToolkitHost,
		MediaPlateEditorToolkit::AppIdentifier,
		Layout,
		true,
		true,
		InMediaPlate
	);
	
	ExtendToolBar();
	RegenerateMenusAndToolbars();
}

/* FAssetEditorToolkit interface
 *****************************************************************************/

FString FMediaPlateEditorToolkit::GetDocumentationLink() const
{
	return FString(TEXT("WorkingWithMedia/IntegratingMedia/MediaFramework"));
}

void FMediaPlateEditorToolkit::OnClose()
{
	TObjectPtr<UMediaPlayer> MediaPlayer = MediaPlate->GetMediaPlayer();
	if (MediaPlayer != nullptr)
	{
		MediaPlayer->Close();
	}
}

void FMediaPlateEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_MediaPlateEditor", "Media Plate Editor"));
	auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(MediaPlateEditorToolkit::DetailsTabId, FOnSpawnTab::CreateSP(this, &FMediaPlateEditorToolkit::HandleTabManagerSpawnTab, MediaPlateEditorToolkit::DetailsTabId))
		.SetDisplayName(LOCTEXT("DetailsTabName", "Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(MediaPlateEditorToolkit::InfoTabId, FOnSpawnTab::CreateSP(this, &FMediaPlateEditorToolkit::HandleTabManagerSpawnTab, MediaPlateEditorToolkit::InfoTabId))
		.SetDisplayName(LOCTEXT("InfoTabName", "Info"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(Style->GetStyleSetName(), "MediaPlateEditor.Tabs.Info"));

	InTabManager->RegisterTabSpawner(MediaPlateEditorToolkit::MediaTabId, FOnSpawnTab::CreateSP(this, &FMediaPlateEditorToolkit::HandleTabManagerSpawnTab, MediaPlateEditorToolkit::MediaTabId))
		.SetDisplayName(LOCTEXT("MediaTabName", "Media Library"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(Style->GetStyleSetName(), "MediaPlateEditor.Tabs.Media"));

	InTabManager->RegisterTabSpawner(MediaPlateEditorToolkit::ViewerTabId, FOnSpawnTab::CreateSP(this, &FMediaPlateEditorToolkit::HandleTabManagerSpawnTab, MediaPlateEditorToolkit::ViewerTabId))
		.SetDisplayName(LOCTEXT("PlayerTabName", "Player"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(Style->GetStyleSetName(), "MediaPlateEditor.Tabs.Player"));

	InTabManager->RegisterTabSpawner(MediaPlateEditorToolkit::PlaylistTabId, FOnSpawnTab::CreateSP(this, &FMediaPlateEditorToolkit::HandleTabManagerSpawnTab, MediaPlateEditorToolkit::PlaylistTabId))
		.SetDisplayName(LOCTEXT("PlaylistTabName", "Playlist"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(Style->GetStyleSetName(), "MediaPlateEditor.Tabs.Playlist"));

	InTabManager->RegisterTabSpawner(MediaPlateEditorToolkit::StatsTabId, FOnSpawnTab::CreateSP(this, &FMediaPlateEditorToolkit::HandleTabManagerSpawnTab, MediaPlateEditorToolkit::StatsTabId))
		.SetDisplayName(LOCTEXT("StatsTabName", "Stats"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(Style->GetStyleSetName(), "MediaPlateEditor.Tabs.Stats"));
}

void FMediaPlateEditorToolkit::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(MediaPlateEditorToolkit::ViewerTabId);
	InTabManager->UnregisterTabSpawner(MediaPlateEditorToolkit::StatsTabId);
	InTabManager->UnregisterTabSpawner(MediaPlateEditorToolkit::PlaylistTabId);
	InTabManager->UnregisterTabSpawner(MediaPlateEditorToolkit::MediaTabId);
	InTabManager->UnregisterTabSpawner(MediaPlateEditorToolkit::InfoTabId);
	InTabManager->UnregisterTabSpawner(MediaPlateEditorToolkit::DetailsTabId);
}

/* IToolkit interface
 *****************************************************************************/

FText FMediaPlateEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Media Plate Editor");
}

FName FMediaPlateEditorToolkit::GetToolkitFName() const
{
	return FName("MediaPlateEditor");
}

FLinearColor FMediaPlateEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.3f, 0.2f, 0.5f, 0.5f);
}

FString FMediaPlateEditorToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "MediaPlate ").ToString();
}

/* FGCObject interface
 *****************************************************************************/

void FMediaPlateEditorToolkit::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(MediaPlate);
}

/* FEditorUndoClient interface
*****************************************************************************/

void FMediaPlateEditorToolkit::PostUndo(bool bSuccess)
{
	// do nothing
}

void FMediaPlateEditorToolkit::PostRedo(bool bSuccess)
{
	PostUndo(bSuccess);
}

/* FMediaPlayerEditorToolkit implementation
 *****************************************************************************/

void FMediaPlateEditorToolkit::BindCommands()
{
	const FMediaPlateEditorCommands& Commands = FMediaPlateEditorCommands::Get();

	ToolkitCommands->MapAction(
		Commands.CloseMedia,
		FExecuteAction::CreateLambda([this] { MediaPlate->GetMediaPlayer()->Close(); }),
		FCanExecuteAction::CreateLambda([this] { return !MediaPlate->GetMediaPlayer()->GetUrl().IsEmpty(); })
	);

	ToolkitCommands->MapAction(
		Commands.ForwardMedia,
		FExecuteAction::CreateLambda([this]{ MediaPlate->GetMediaPlayer()->SetRate(GetForwardRate()); }),
		FCanExecuteAction::CreateLambda([this]{
			TObjectPtr<UMediaPlayer> MediaPlayer = MediaPlate->GetMediaPlayer();
			return MediaPlayer->IsReady() && MediaPlayer->SupportsRate(GetForwardRate(), false);
		})
	);

	ToolkitCommands->MapAction(
		Commands.NextMedia,
		FExecuteAction::CreateLambda([this]{ MediaPlate->GetMediaPlayer()->Next(); }),
		FCanExecuteAction::CreateLambda([this]{ return (MediaPlate->GetMediaPlayer()->GetPlaylistRef().Num() > 1); })
	);

	ToolkitCommands->MapAction(
		Commands.OpenMedia,
		FExecuteAction::CreateLambda([this] { MediaPlate->GetMediaPlayer()->OpenSource(MediaPlate->MediaSource); }),
		FCanExecuteAction::CreateLambda([this] { return true; })
	);

	ToolkitCommands->MapAction(
		Commands.PauseMedia,
		FExecuteAction::CreateLambda([this]{ MediaPlate->GetMediaPlayer()->Pause(); }),
		FCanExecuteAction::CreateLambda([this]{
			TObjectPtr<UMediaPlayer> MediaPlayer = MediaPlate->GetMediaPlayer();
			return MediaPlayer->CanPause() && !MediaPlayer->IsPaused();
		})
	);

	ToolkitCommands->MapAction(
		Commands.PlayMedia,
		FExecuteAction::CreateLambda([this]{ MediaPlate->GetMediaPlayer()->Play(); }),
		FCanExecuteAction::CreateLambda([this]{
			TObjectPtr<UMediaPlayer> MediaPlayer = MediaPlate->GetMediaPlayer(); 
			return MediaPlayer->IsReady() && (!MediaPlayer->IsPlaying() || (MediaPlayer->GetRate() != 1.0f));
		})
	);

	ToolkitCommands->MapAction(
		Commands.PreviousMedia,
		FExecuteAction::CreateLambda([this]{ MediaPlate->GetMediaPlayer()->Previous(); }),
		FCanExecuteAction::CreateLambda([this]{ return (MediaPlate->GetMediaPlayer()->GetPlaylistRef().Num() > 1); })
	);

	ToolkitCommands->MapAction(
		Commands.ReverseMedia,
		FExecuteAction::CreateLambda([this]{ MediaPlate->GetMediaPlayer()->SetRate(GetReverseRate()); } ),
		FCanExecuteAction::CreateLambda([this]{
			TObjectPtr<UMediaPlayer> MediaPlayer = MediaPlate->GetMediaPlayer();
			return MediaPlayer->IsReady() && MediaPlayer->SupportsRate(GetReverseRate(), false);
		})
	);

	ToolkitCommands->MapAction(
		Commands.RewindMedia,
		FExecuteAction::CreateLambda([this]{ MediaPlate->GetMediaPlayer()->Rewind(); }),
		FCanExecuteAction::CreateLambda([this]{
			TObjectPtr<UMediaPlayer> MediaPlayer = MediaPlate->GetMediaPlayer(); 
			return MediaPlate->GetMediaPlayer()->IsReady() && MediaPlayer->SupportsSeeking() && MediaPlayer->GetTime() > FTimespan::Zero();
		})
	);
}

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void FMediaPlateEditorToolkit::ExtendToolBar()
{
	struct Local
	{
		static void FillToolbar(FToolBarBuilder& ToolbarBuilder, const TSharedRef<FUICommandList> ToolkitCommands)
		{
			ToolbarBuilder.BeginSection("PlaybackControls");
			{
				ToolbarBuilder.AddToolBarButton(FMediaPlateEditorCommands::Get().PreviousMedia);
				ToolbarBuilder.AddToolBarButton(FMediaPlateEditorCommands::Get().RewindMedia);
				ToolbarBuilder.AddToolBarButton(FMediaPlateEditorCommands::Get().ReverseMedia);
				ToolbarBuilder.AddToolBarButton(FMediaPlateEditorCommands::Get().PlayMedia);
				ToolbarBuilder.AddToolBarButton(FMediaPlateEditorCommands::Get().PauseMedia);
				ToolbarBuilder.AddToolBarButton(FMediaPlateEditorCommands::Get().ForwardMedia);
				ToolbarBuilder.AddToolBarButton(FMediaPlateEditorCommands::Get().NextMedia);
			}
			ToolbarBuilder.EndSection();

			ToolbarBuilder.BeginSection("MediaControls");
			{
				ToolbarBuilder.AddToolBarButton(FMediaPlateEditorCommands::Get().OpenMedia);
				ToolbarBuilder.AddToolBarButton(FMediaPlateEditorCommands::Get().CloseMedia);
			}
			ToolbarBuilder.EndSection();
		}
	};

	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);

	ToolbarExtender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateStatic(&Local::FillToolbar, GetToolkitCommands())
	);

	AddToolbarExtender(ToolbarExtender);
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

float FMediaPlateEditorToolkit::GetForwardRate() const
{
	TObjectPtr<UMediaPlayer> MediaPlayer = MediaPlate->GetMediaPlayer();
	float Rate = MediaPlayer->GetRate();

	if (Rate < 1.0f)
	{
		Rate = 1.0f;
	}

	return 2.0f * Rate;
}

float FMediaPlateEditorToolkit::GetReverseRate() const
{
	TObjectPtr<UMediaPlayer> MediaPlayer = MediaPlate->GetMediaPlayer();
	float Rate = MediaPlayer->GetRate();

	if (Rate > -1.0f)
	{
		return -1.0f;
	}

	return 2.0f * Rate;
}

/* FMediaPlayerEditorToolkit callbacks
 *****************************************************************************/

TSharedRef<SDockTab> FMediaPlateEditorToolkit::HandleTabManagerSpawnTab(const FSpawnTabArgs& Args, FName TabIdentifier)
{
	UMediaPlayer* MediaPlayer = MediaPlate->GetMediaPlayer();
	TSharedPtr<SWidget> TabWidget = SNullWidget::NullWidget;
	if (TabIdentifier == MediaPlateEditorToolkit::ViewerTabId)
	{
		TabWidget = SNew(SMediaPlayerEditorViewer, *MediaPlayer, Style);
	}
#if false
	if (TabIdentifier == MediaPlayerEditorToolkit::DetailsTabId)
	{
		TabWidget = SNew(SMediaPlayerEditorDetails, *MediaPlayer, Style);
	}
	else if (TabIdentifier == MediaPlayerEditorToolkit::InfoTabId)
	{
		TabWidget = SNew(SMediaPlayerEditorInfo, *MediaPlayer, Style);
	}
	else if (TabIdentifier == MediaPlayerEditorToolkit::MediaTabId)
	{
		TabWidget = SNew(SMediaPlayerEditorMedia, *MediaPlayer, Style);
	}
	else if (TabIdentifier == MediaPlayerEditorToolkit::PlaylistTabId)
	{
		TabWidget = SNew(SMediaPlayerEditorPlaylist, *MediaPlayer, Style);
	}
	else if (TabIdentifier == MediaPlayerEditorToolkit::StatsTabId)
	{
		TabWidget = SNew(SMediaPlayerEditorStats, *MediaPlayer, Style);
	}
	else if (TabIdentifier == MediaPlayerEditorToolkit::ViewerTabId)
	{
		TabWidget = SNew(SMediaPlayerEditorViewer, *MediaPlayer, Style);
	}
#endif
	return SNew(SDockTab)
		.TabRole(ETabRole::PanelTab)
		[
			TabWidget.ToSharedRef()
		];
}

#undef LOCTEXT_NAMESPACE
