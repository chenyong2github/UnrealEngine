// Copyright Epic Games, Inc. All Rights Reserved.

#include "Toolkits/MediaSourceEditorToolkit.h"

#include "Editor.h"
#include "EditorReimportHandler.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "MediaPlayer.h"
#include "MediaPlaylist.h"
#include "MediaSource.h"
#include "MediaTexture.h"
#include "Models/MediaPlayerEditorCommands.h"
#include "SlateOptMacros.h"
#include "Styling/AppStyle.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SMediaPlayerEditorDetails.h"
#include "Widgets/SMediaPlayerEditorMediaDetails.h"
#include "Widgets/SMediaPlayerEditorPlaylist.h"
#include "Widgets/SMediaPlayerEditorStats.h"
#include "Widgets/SMediaPlayerEditorViewer.h"
#include "Widgets/SMediaSourceEditorDetails.h"
#include "UObject/Package.h"


#define LOCTEXT_NAMESPACE "FMediaSourceEditorToolkit"


namespace MediaSourceEditorToolkit
{
	static const FName AppIdentifier("MediaSourceEditorApp");
	static const FName DetailsTabId("Details");
	static const FName MediaDetailsTabId("MediaDetails");
	static const FName PlayerDetailsTabId("PlayerDetails");
	static const FName ViewerTabId("Viewer");
}


/* FMediaSourceEditorToolkit structors
 *****************************************************************************/

FMediaSourceEditorToolkit::FMediaSourceEditorToolkit(const TSharedRef<ISlateStyle>& InStyle)
	: MediaPlayer(nullptr)
	, MediaSource(nullptr)
	, MediaTexture(nullptr)
	, Style(InStyle)
{ }


FMediaSourceEditorToolkit::~FMediaSourceEditorToolkit()
{
	FReimportManager::Instance()->OnPreReimport().RemoveAll(this);
	FReimportManager::Instance()->OnPostReimport().RemoveAll(this);

	GEditor->UnregisterForUndo(this);
}


/* FMediaSourceEditorToolkit interface
 *****************************************************************************/

void FMediaSourceEditorToolkit::Initialize(UMediaSource* InMediaSource, const EToolkitMode::Type InMode, const TSharedPtr<IToolkitHost>& InToolkitHost)
{
	MediaSource = InMediaSource;

	if (MediaSource == nullptr)
	{
		return;
	}

	// support undo/redo
	MediaSource->SetFlags(RF_Transactional);
	GEditor->RegisterForUndo(this);

	MediaPlayer = NewObject<UMediaPlayer>(GetTransientPackage());
	MediaPlayer->SetLooping(false);
	MediaPlayer->PlayOnOpen = true;

	MediaTexture = NewObject<UMediaTexture>(GetTransientPackage(), NAME_None, RF_Transient | RF_Public);
	if (MediaTexture != nullptr)
	{
		MediaTexture->AutoClear = true;
		MediaTexture->SetMediaPlayer(MediaPlayer);
		MediaTexture->UpdateResource();
	}

	BindCommands();

	// create tab layout
	const TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("Standalone_MediaSourceEditor_v0.3")
		->AddArea
		(
			FTabManager::NewPrimaryArea()
				->SetOrientation(Orient_Horizontal)
				->Split
				(
					// viewer
					FTabManager::NewStack()
						->AddTab(MediaSourceEditorToolkit::ViewerTabId, ETabState::OpenedTab)
						->SetHideTabWell(true)
						->SetSizeCoefficient(0.6f)
						
				)
				->Split
				(
					FTabManager::NewSplitter()
						->SetOrientation(Orient_Vertical)
						->SetSizeCoefficient(0.4f)
						->Split
						(
							// Media details tab.
							FTabManager::NewStack()
							->AddTab(MediaSourceEditorToolkit::MediaDetailsTabId, ETabState::OpenedTab)
							->SetSizeCoefficient(0.2f)
						)
						->Split
						(
							// Details tab.
							FTabManager::NewStack()
								->AddTab(MediaSourceEditorToolkit::DetailsTabId, ETabState::OpenedTab)
								->AddTab(MediaSourceEditorToolkit::PlayerDetailsTabId, ETabState::OpenedTab)
								->SetForegroundTab(MediaSourceEditorToolkit::DetailsTabId)
								->SetSizeCoefficient(0.8f)
						)
				)
		);

	FAssetEditorToolkit::InitAssetEditor(
		InMode,
		InToolkitHost,
		MediaSourceEditorToolkit::AppIdentifier,
		Layout,
		true /*bCreateDefaultStandaloneMenu*/,
		true /*bCreateDefaultToolbar*/,
		InMediaSource
	);
	
	ExtendToolBar();
	RegenerateMenusAndToolbars();
}


/* FAssetEditorToolkit interface
 *****************************************************************************/

FString FMediaSourceEditorToolkit::GetDocumentationLink() const
{
	return FString(TEXT("WorkingWithMedia/IntegratingMedia/MediaFramework"));
}


void FMediaSourceEditorToolkit::OnClose()
{
	if (MediaPlayer != nullptr)
	{
		MediaPlayer->Close();
	}
}


void FMediaSourceEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_MediaSourceEditor", "Media Source Editor"));
	auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(MediaSourceEditorToolkit::DetailsTabId, FOnSpawnTab::CreateSP(this, &FMediaSourceEditorToolkit::HandleTabManagerSpawnTab, MediaSourceEditorToolkit::DetailsTabId))
		.SetDisplayName(LOCTEXT("DetailsTabName", "Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(MediaSourceEditorToolkit::MediaDetailsTabId, FOnSpawnTab::CreateSP(this, &FMediaSourceEditorToolkit::HandleTabManagerSpawnTab, MediaSourceEditorToolkit::MediaDetailsTabId))
		.SetDisplayName(LOCTEXT("MediaDetailsTabName", "Media Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(Style->GetStyleSetName(), "MediaPlayerEditor.Tabs.Info"));

	InTabManager->RegisterTabSpawner(MediaSourceEditorToolkit::PlayerDetailsTabId, FOnSpawnTab::CreateSP(this, &FMediaSourceEditorToolkit::HandleTabManagerSpawnTab, MediaSourceEditorToolkit::PlayerDetailsTabId))
		.SetDisplayName(LOCTEXT("PlayerDetailsTabName", "Player Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(MediaSourceEditorToolkit::ViewerTabId, FOnSpawnTab::CreateSP(this, &FMediaSourceEditorToolkit::HandleTabManagerSpawnTab, MediaSourceEditorToolkit::ViewerTabId))
		.SetDisplayName(LOCTEXT("PlayerTabName", "Player"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(Style->GetStyleSetName(), "MediaPlayerEditor.Tabs.Player"));
}


void FMediaSourceEditorToolkit::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(MediaSourceEditorToolkit::ViewerTabId);
	InTabManager->UnregisterTabSpawner(MediaSourceEditorToolkit::PlayerDetailsTabId);
	InTabManager->UnregisterTabSpawner(MediaSourceEditorToolkit::MediaDetailsTabId);
	InTabManager->UnregisterTabSpawner(MediaSourceEditorToolkit::DetailsTabId);
}


/* IToolkit interface
 *****************************************************************************/

FText FMediaSourceEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Media Source Editor");
}


FName FMediaSourceEditorToolkit::GetToolkitFName() const
{
	return FName("MediaSourceEditor");
}


FLinearColor FMediaSourceEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.3f, 0.2f, 0.5f, 0.5f);
}


FString FMediaSourceEditorToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "MediaSource ").ToString();
}


/* FGCObject interface
 *****************************************************************************/

void FMediaSourceEditorToolkit::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(MediaPlayer);
	Collector.AddReferencedObject(MediaSource);
	Collector.AddReferencedObject(MediaTexture);
}


/* FEditorUndoClient interface
*****************************************************************************/

void FMediaSourceEditorToolkit::PostUndo(bool bSuccess)
{
	// do nothing
}


void FMediaSourceEditorToolkit::PostRedo(bool bSuccess)
{
	PostUndo(bSuccess);
}


/* FMediaSourceEditorToolkit implementation
 *****************************************************************************/

void FMediaSourceEditorToolkit::BindCommands()
{
	const FMediaPlayerEditorCommands& Commands = FMediaPlayerEditorCommands::Get();

	ToolkitCommands->MapAction(
		Commands.CloseMedia,
		FExecuteAction::CreateLambda([this] { MediaPlayer->Close(); }),
		FCanExecuteAction::CreateLambda([this] { return !MediaPlayer->GetUrl().IsEmpty(); })
	);

	ToolkitCommands->MapAction(
		Commands.ForwardMedia,
		FExecuteAction::CreateLambda([this]{ MediaPlayer->SetRate(GetForwardRate()); }),
		FCanExecuteAction::CreateLambda([this]{ return MediaPlayer->IsReady() && MediaPlayer->SupportsRate(GetForwardRate(), false); })
	);

	ToolkitCommands->MapAction(
		Commands.NextMedia,
		FExecuteAction::CreateLambda([this]{ MediaPlayer->Next(); }),
		FCanExecuteAction::CreateLambda([this]{ return (MediaPlayer->GetPlaylistRef().Num() > 1); })
	);

	ToolkitCommands->MapAction(
		Commands.OpenMedia,
		FExecuteAction::CreateLambda([this] { MediaPlayer->OpenSource(MediaSource); }),
		FCanExecuteAction::CreateLambda([this] { return true; })
	);

	ToolkitCommands->MapAction(
		Commands.PauseMedia,
		FExecuteAction::CreateLambda([this]{ MediaPlayer->Pause(); }),
		FCanExecuteAction::CreateLambda([this]{ return MediaPlayer->CanPause() && !MediaPlayer->IsPaused(); })
	);

	ToolkitCommands->MapAction(
		Commands.PlayMedia,
		FExecuteAction::CreateLambda([this]{ MediaPlayer->Play(); }),
		FCanExecuteAction::CreateLambda([this]{ return MediaPlayer->IsReady() && (!MediaPlayer->IsPlaying() || (MediaPlayer->GetRate() != 1.0f)); })
	);

	ToolkitCommands->MapAction(
		Commands.PreviousMedia,
		FExecuteAction::CreateLambda([this]{ MediaPlayer->Previous(); }),
		FCanExecuteAction::CreateLambda([this]{ return (MediaPlayer->GetPlaylistRef().Num() > 1); })
	);

	ToolkitCommands->MapAction(
		Commands.ReverseMedia,
		FExecuteAction::CreateLambda([this]{ MediaPlayer->SetRate(GetReverseRate()); } ),
		FCanExecuteAction::CreateLambda([this]{ return MediaPlayer->IsReady() && MediaPlayer->SupportsRate(GetReverseRate(), false); })
	);

	ToolkitCommands->MapAction(
		Commands.RewindMedia,
		FExecuteAction::CreateLambda([this]{ MediaPlayer->Rewind(); }),
		FCanExecuteAction::CreateLambda([this]{ return MediaPlayer->IsReady() && MediaPlayer->SupportsSeeking() && MediaPlayer->GetTime() > FTimespan::Zero(); })
	);
}


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void FMediaSourceEditorToolkit::ExtendToolBar()
{
	struct Local
	{
		static void FillToolbar(FToolBarBuilder& ToolbarBuilder, const TSharedRef<FUICommandList> ToolkitCommands)
		{
			ToolbarBuilder.BeginSection("PlaybackControls");
			{
				ToolbarBuilder.AddToolBarButton(FMediaPlayerEditorCommands::Get().PreviousMedia);
				ToolbarBuilder.AddToolBarButton(FMediaPlayerEditorCommands::Get().RewindMedia);
				ToolbarBuilder.AddToolBarButton(FMediaPlayerEditorCommands::Get().ReverseMedia);
				ToolbarBuilder.AddToolBarButton(FMediaPlayerEditorCommands::Get().PlayMedia);
				ToolbarBuilder.AddToolBarButton(FMediaPlayerEditorCommands::Get().PauseMedia);
				ToolbarBuilder.AddToolBarButton(FMediaPlayerEditorCommands::Get().ForwardMedia);
				ToolbarBuilder.AddToolBarButton(FMediaPlayerEditorCommands::Get().NextMedia);
			}
			ToolbarBuilder.EndSection();

			ToolbarBuilder.BeginSection("MediaControls");
			{
				ToolbarBuilder.AddToolBarButton(FMediaPlayerEditorCommands::Get().OpenMedia);
				ToolbarBuilder.AddToolBarButton(FMediaPlayerEditorCommands::Get().CloseMedia);
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


float FMediaSourceEditorToolkit::GetForwardRate() const
{
	float Rate = MediaPlayer->GetRate();

	if (Rate < 1.0f)
	{
		Rate = 1.0f;
	}

	return 2.0f * Rate;
}


float FMediaSourceEditorToolkit::GetReverseRate() const
{
	float Rate = MediaPlayer->GetRate();

	if (Rate > -1.0f)
	{
		return -1.0f;
	}

	return 2.0f * Rate;
}


/* FMediaSourceEditorToolkit callbacks
 *****************************************************************************/

TSharedRef<SDockTab> FMediaSourceEditorToolkit::HandleTabManagerSpawnTab(const FSpawnTabArgs& Args, FName TabIdentifier)
{
	TSharedPtr<SWidget> TabWidget = SNullWidget::NullWidget;

	if (TabIdentifier == MediaSourceEditorToolkit::DetailsTabId)
	{
		TabWidget = SNew(SMediaSourceEditorDetails, *MediaSource, Style);
	}
	else if (TabIdentifier == MediaSourceEditorToolkit::MediaDetailsTabId)
	{
		TabWidget = SNew(SMediaPlayerEditorMediaDetails, MediaPlayer, MediaTexture);
	}
	else if (TabIdentifier == MediaSourceEditorToolkit::PlayerDetailsTabId)
	{
		TabWidget = SNew(SMediaPlayerEditorDetails, *MediaPlayer, Style);
	}
	else if (TabIdentifier == MediaSourceEditorToolkit::ViewerTabId)
	{
		TabWidget = SNew(SMediaPlayerEditorViewer, *MediaPlayer, MediaTexture, Style, true)
			.bShowUrl(false);
	}

	return SNew(SDockTab)
		.TabRole(ETabRole::PanelTab)
		[
			TabWidget.ToSharedRef()
		];
}


#undef LOCTEXT_NAMESPACE
