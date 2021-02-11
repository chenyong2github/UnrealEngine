// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLevelEditor.h"
#include "Framework/MultiBox/MultiBoxExtender.h"
#include "ToolMenus.h"
#include "Framework/Docking/LayoutService.h"
#include "EditorModeRegistry.h"
#include "EdMode.h"
#include "Engine/Selection.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "Styling/SlateStyleRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "EditorStyleSet.h"
#include "Editor/EditorPerProjectUserSettings.h"
#include "Editor/UnrealEdEngine.h"
#include "Settings/EditorExperimentalSettings.h"
#include "LevelEditorViewport.h"
#include "EditorModes.h"
#include "UnrealEdGlobals.h"
#include "LevelEditor.h"
#include "LevelEditorMenu.h"
#include "IDetailsView.h"
#include "LevelEditorActions.h"
#include "LevelEditorModesActions.h"
#include "LevelEditorContextMenu.h"
#include "LevelEditorToolBar.h"
#include "SLevelEditorToolBox.h"
#include "SLevelEditorModeContent.h"
#include "SLevelEditorBuildAndSubmit.h"
#include "Kismet2/DebuggerCommands.h"
#include "SceneOutlinerPublicTypes.h"
#include "SceneOutlinerModule.h"
#include "Editor/Layers/Public/LayersModule.h"
#include "Editor/WorldBrowser/Public/WorldBrowserModule.h"
#include "Toolkits/ToolkitManager.h"
#include "PropertyEditorModule.h"
#include "Interfaces/IMainFrameModule.h"
#include "Editor/WorkspaceMenuStructure/Public/WorkspaceMenuStructure.h"
#include "Editor/WorkspaceMenuStructure/Public/WorkspaceMenuStructureModule.h"
#include "StatsViewerModule.h"
#include "Widgets/SToolTip.h"
#include "IDocumentation.h"
#include "TutorialMetaData.h"
#include "Widgets/Docking/SDockTab.h"
#include "SActorDetails.h"
#include "GameFramework/WorldSettings.h"
#include "Framework/Docking/LayoutExtender.h"
#include "HierarchicalLODOutlinerModule.h"
#include "EditorViewportCommands.h"
#include "IPlacementModeModule.h"
#include "Classes/EditorStyleSettings.h"
#include "Editor/EnvironmentLightingViewer/Public/EnvironmentLightingModule.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "SLevelEditor"

static const FName MainFrameModuleName("MainFrame");
static const FName LevelEditorModuleName("LevelEditor");


namespace LevelEditorConstants
{
	/** The size of the thumbnail pool */
	const int32 ThumbnailPoolSize = 32;
}

SLevelEditor::SLevelEditor()
	: World(NULL), bNeedsRefresh(false)
{
	const bool bAreRealTimeThumbnailsAllowed = false;
	ThumbnailPool = MakeShareable(new FAssetThumbnailPool(LevelEditorConstants::ThumbnailPoolSize, bAreRealTimeThumbnailsAllowed));
}

void SLevelEditor::BindCommands()
{
	LevelEditorCommands = MakeShareable( new FUICommandList );

	const FLevelEditorCommands& Actions = FLevelEditorCommands::Get();

	// Map UI commands to delegates that are executed when the command is handled by a keybinding or menu
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>( LevelEditorModuleName );

	// Append the list of the level editor commands for this instance with the global list of commands for all instances.
	LevelEditorCommands->Append( LevelEditorModule.GetGlobalLevelEditorActions() );

	// Append the list of global PlayWorld commands
	LevelEditorCommands->Append( FPlayWorldCommands::GlobalPlayWorldActions.ToSharedRef() );

	LevelEditorCommands->MapAction( 
		Actions.EditAssetNoConfirmMultiple, 
		FExecuteAction::CreateStatic( &FLevelEditorActionCallbacks::EditAsset_Clicked, EToolkitMode::Standalone, TWeakPtr< SLevelEditor >( SharedThis( this ) ), false ) );

	LevelEditorCommands->MapAction(
		Actions.EditAsset,
		FExecuteAction::CreateStatic( &FLevelEditorActionCallbacks::EditAsset_Clicked, EToolkitMode::Standalone, TWeakPtr< SLevelEditor >( SharedThis( this ) ), true ) );

	LevelEditorCommands->MapAction(
		Actions.CheckOutProjectSettingsConfig,
		FExecuteAction::CreateStatic( &FLevelEditorActionCallbacks::CheckOutProjectSettingsConfig ) );

	LevelEditorCommands->MapAction(
		Actions.OpenLevelBlueprint,
		FExecuteAction::CreateStatic< TWeakPtr< SLevelEditor > >( &FLevelEditorActionCallbacks::OpenLevelBlueprint, SharedThis( this ) ) );
	
	LevelEditorCommands->MapAction(
		Actions.CreateBlankBlueprintClass,
		FExecuteAction::CreateStatic( &FLevelEditorActionCallbacks::CreateBlankBlueprintClass ) );

	LevelEditorCommands->MapAction(
		Actions.ConvertSelectionToBlueprint,
		FExecuteAction::CreateStatic( &FLevelEditorActionCallbacks::ConvertSelectedActorsIntoBlueprintClass ),
		FCanExecuteAction::CreateStatic( &FLevelEditorActionCallbacks::CanConvertSelectedActorsIntoBlueprintClass ) );

	LevelEditorCommands->MapAction(
		Actions.OpenContentBrowser,
		FExecuteAction::CreateStatic( &FLevelEditorActionCallbacks::OpenContentBrowser ) );
	
	LevelEditorCommands->MapAction(
		Actions.OpenMarketplace,
		FExecuteAction::CreateStatic( &FLevelEditorActionCallbacks::OpenMarketplace ) );

	LevelEditorCommands->MapAction(
		Actions.ToggleVR,
		FExecuteAction::CreateStatic( &FLevelEditorActionCallbacks::ToggleVR ),
		FCanExecuteAction::CreateStatic( &FLevelEditorActionCallbacks::ToggleVR_CanExecute ),
		FIsActionChecked::CreateStatic( &FLevelEditorActionCallbacks::ToggleVR_IsChecked ),
		FIsActionButtonVisible::CreateStatic(&FLevelEditorActionCallbacks::ToggleVR_IsButtonActive));

	LevelEditorCommands->MapAction(
		Actions.WorldProperties,
		FExecuteAction::CreateStatic< TWeakPtr< SLevelEditor > >( &FLevelEditorActionCallbacks::OnShowWorldProperties, SharedThis( this ) ) );
	
	LevelEditorCommands->MapAction( 
		Actions.FocusAllViewportsToSelection, 
		FExecuteAction::CreateStatic( &FLevelEditorActionCallbacks::ExecuteExecCommand, FString( TEXT("CAMERA ALIGN") ) )
		);

	LevelEditorCommands->MapAction( 
		FEditorViewportCommands::Get().FocusViewportToSelection, 
		FExecuteAction::CreateStatic( &FLevelEditorActionCallbacks::ExecuteExecCommand, FString( TEXT("CAMERA ALIGN ACTIVEVIEWPORTONLY") ) )
		);

	if (FPlayWorldCommands::GlobalPlayWorldActions.IsValid())
	{
		FUICommandList& PlayWorldActionList = *FPlayWorldCommands::GlobalPlayWorldActions;
		PlayWorldActionList.MapAction(Actions.RecompileGameCode, *LevelEditorCommands->GetActionForCommand(Actions.RecompileGameCode));
	}
}

void SLevelEditor::RegisterMenus()
{
	FLevelEditorMenu::RegisterLevelEditorMenus();
	FLevelEditorToolBar::RegisterLevelEditorToolBar(LevelEditorCommands.ToSharedRef(), SharedThis(this));
}

void SLevelEditor::Construct( const SLevelEditor::FArguments& InArgs)
{
	// Important: We use raw bindings here because we are releasing our binding in our destructor (where a weak pointer would be invalid)
	// It's imperative that our delegate is removed in the destructor for the level editor module to play nicely with reloading.

	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked< FLevelEditorModule >( LevelEditorModuleName );
	LevelEditorModule.OnNotificationBarChanged().AddRaw( this, &SLevelEditor::ConstructNotificationBar );

	GetMutableDefault<UEditorExperimentalSettings>()->OnSettingChanged().AddRaw(this, &SLevelEditor::HandleExperimentalSettingChanged);

	BindCommands();

	RegisterMenus();

	// We need to register when modes list changes so that we can refresh the auto generated commands.
	if (GEditor != nullptr)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OnEditorModesChanged().AddRaw(this, &SLevelEditor::EditorModeCommandsChanged);
	}
	GLevelEditorModeTools().OnEditorModeIDChanged().AddSP(this, &SLevelEditor::OnEditorModeIdChanged);

	// @todo This is a hack to get this working for now. This won't work with multiple worlds
	if (GEditor != nullptr)
	{
		GEditor->GetEditorWorldContext(true).AddRef(World);

		// Set the initial preview feature level.
		World->ChangeFeatureLevel(GEditor->GetActiveFeatureLevelPreviewType());

		LevelActorOuterChangedHandle = GEditor->OnLevelActorOuterChanged().AddSP(this, &SLevelEditor::OnLevelActorOuterChanged);
	}

	// Patch into the OnPreviewFeatureLevelChanged() delegate to swap out the current feature level with a user selection.
	PreviewFeatureLevelChangedHandle = GEditor->OnPreviewFeatureLevelChanged().AddLambda([this](ERHIFeatureLevel::Type NewFeatureLevel)
		{
			// Do one recapture if atleast one ReflectionComponent is dirty
			// BuildReflectionCapturesOnly_Execute in LevelEditorActions relies on this happening on toggle between SM5->ES31. If you remove this, update that code!
			if (World->NumUnbuiltReflectionCaptures >= 1 && NewFeatureLevel == ERHIFeatureLevel::ES3_1 && GEditor != nullptr)
			{
				GEditor->BuildReflectionCaptures();
			}
			World->ChangeFeatureLevel(NewFeatureLevel);
		});

	FEditorDelegates::MapChange.AddRaw(this, &SLevelEditor::HandleEditorMapChange);
	FEditorDelegates::OnAssetsDeleted.AddRaw(this, &SLevelEditor::HandleAssetsDeleted);
	HandleEditorMapChange(MapChangeEventFlags::NewMap);
}

void SLevelEditor::Initialize( const TSharedRef<SDockTab>& OwnerTab, const TSharedRef<SWindow>& OwnerWindow )
{
	// Bind the level editor tab's label to the currently loaded level name string in the main frame
	OwnerTab->SetLabel( TAttribute<FText>( this, &SLevelEditor::GetTabTitle) );
	OwnerTab->SetTabLabelSuffix(TAttribute<FText>(this, &SLevelEditor::GetTabSuffix));

	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked< FLevelEditorModule >(LevelEditorModuleName);

	LevelEditorModule.OnActorSelectionChanged().AddSP(this, &SLevelEditor::OnActorSelectionChanged);

	TSharedRef<SWidget> Widget2 = RestoreContentArea( OwnerTab, OwnerWindow );
	TSharedRef<SWidget> Widget1 = FLevelEditorMenu::MakeLevelEditorMenu( LevelEditorCommands, SharedThis(this) );

	ChildSlot
	[
		SNew( SVerticalBox )
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew( SOverlay )

			+SOverlay::Slot()
			[
				SNew( SBox )
				.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("MainMenu")))
				[
					Widget1
				]
			]
		 
// For platforms without a global menu bar we can put the perf. tools in the editor window's menu bar
#if !PLATFORM_MAC
			+ SOverlay::Slot()
			.HAlign( HAlign_Right )
			.VAlign( VAlign_Center )
			[
				SAssignNew( NotificationBarBox, SHorizontalBox )
				.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("PerformanceTools")))
			]
#endif
		]

#if PLATFORM_MAC
		// Without the in-window menu bar, we need some space between the tab bar and tab contents
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew( SBox )
			.HeightOverride( 1.0f )
		]
#endif

		+SVerticalBox::Slot()
		.FillHeight( 1.0f )
		[
			Widget2
		]
	];
	
// For OS X we need to put it into the window's title bar since there's no per-window menu bar
#if PLATFORM_MAC	
	OwnerTab->SetRightContent(
		SAssignNew( NotificationBarBox, SHorizontalBox )
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("PerformanceTools")))
		);
#endif

	ConstructNotificationBar();

	OnLayoutHasChanged();
}

void SLevelEditor::ConstructNotificationBar()
{
	NotificationBarBox->ClearChildren();

	// level editor commands
	NotificationBarBox->AddSlot()
		.AutoWidth()
		.Padding(5.0f, 0.0f, 0.0f, 0.0f)
		[
			FLevelEditorMenu::MakeNotificationBar( LevelEditorCommands, SharedThis(this ) )
		];

	// developer tools
	const IMainFrameModule& MainFrameModule = FModuleManager::GetModuleChecked<IMainFrameModule>(MainFrameModuleName);
	const FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked< FLevelEditorModule >(LevelEditorModuleName);
	
	TArray<FMainFrameDeveloperTool> Tools;
	for (const TTuple<FName, FLevelEditorModule::FStatusBarItem>& Item : LevelEditorModule.GetStatusBarItems())
	{
		Tools.Add({Item.Value.Visibility, Item.Value.Label, Item.Value.Value});
	}

	NotificationBarBox->AddSlot()
		.AutoWidth()
		.Padding(5.0f, 0.0f, 0.0f, 0.0f)
		[
			MainFrameModule.MakeDeveloperTools( Tools )
		];
}


SLevelEditor::~SLevelEditor()
{
	// We're going away now, so make sure all toolkits that are hosted within this level editor are shut down
	FToolkitManager::Get().OnToolkitHostDestroyed( this );
	HostedToolkits.Reset();
	
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked< FLevelEditorModule >( LevelEditorModuleName );
	LevelEditorModule.OnNotificationBarChanged().RemoveAll( this );
	
	if(UObjectInitialized())
	{
		GetMutableDefault<UEditorExperimentalSettings>()->OnSettingChanged().RemoveAll(this);
		GetMutableDefault<UEditorPerProjectUserSettings>()->OnUserSettingChanged().RemoveAll(this);
	}

	FEditorDelegates::OnAssetsDeleted.RemoveAll(this);
	FEditorDelegates::MapChange.RemoveAll(this);

	if (GEngine)
	{
		CastChecked<UEditorEngine>(GEngine)->OnPreviewFeatureLevelChanged().Remove(PreviewFeatureLevelChangedHandle);
	}

	if (GEditor)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OnEditorModesChanged().RemoveAll(this);
		GEditor->OnLevelActorOuterChanged().Remove(LevelActorOuterChangedHandle);
		GEditor->GetEditorWorldContext(true).RemoveRef(World);

		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OnEditorModesChanged().RemoveAll(this);
	}
}

FText SLevelEditor::GetTabTitle() const
{
	const IMainFrameModule& MainFrameModule = FModuleManager::GetModuleChecked< IMainFrameModule >( MainFrameModuleName );

	return FText::FromString(MainFrameModule.GetLoadedLevelName());
}

FText SLevelEditor::GetTabSuffix() const
{
	const bool bDirtyState = World && World->GetCurrentLevel()->GetOutermost()->IsDirty();
	return bDirtyState ? FText::FromString(TEXT("*")) : FText::GetEmpty();
}

bool SLevelEditor::HasActivePlayInEditorViewport() const
{
	// Search through all current viewport layouts
	for( int32 TabIndex = 0; TabIndex < ViewportTabs.Num(); ++TabIndex )
	{
		TWeakPtr<FLevelViewportTabContent> ViewportTab = ViewportTabs[ TabIndex ];

		if (ViewportTab.IsValid())
		{
			// Get all the viewports in the layout
			const TMap< FName, TSharedPtr< IEditorViewportLayoutEntity > >* LevelViewports = ViewportTab.Pin()->GetViewports();

			if (LevelViewports != NULL)
			{
				// Search for a viewport with a pie session
				for (auto& Pair : *LevelViewports)
				{
					TSharedPtr<ILevelViewportLayoutEntity> ViewportEntity = StaticCastSharedPtr<ILevelViewportLayoutEntity>(Pair.Value);
					if (ViewportEntity.IsValid() && ViewportEntity->IsPlayInEditorViewportActive())
					{
						return true;
					}
				}
			}
		}
	}

	// Also check standalone viewports
	for( const TWeakPtr< SLevelViewport >& StandaloneViewportWeakPtr : StandaloneViewports )
	{
		const TSharedPtr< SLevelViewport > Viewport = StandaloneViewportWeakPtr.Pin();
		if( Viewport.IsValid() )
		{
			if( Viewport->IsPlayInEditorViewportActive() )
			{
				return true;
			}
		}
	}

	return false;
}


TSharedPtr<SLevelViewport> SLevelEditor::GetActiveViewport()
{
	// The first visible viewport
	TSharedPtr<SLevelViewport> FirstVisibleViewport;

	// Search through all current viewport tabs
	for( int32 TabIndex = 0; TabIndex < ViewportTabs.Num(); ++TabIndex )
	{
		TSharedPtr<FLevelViewportTabContent> ViewportTab = ViewportTabs[ TabIndex ].Pin();

		if (ViewportTab.IsValid())
		{
			// Only check the viewports in the tab if its visible
			if( ViewportTab->IsVisible() )
			{
				const TMap< FName, TSharedPtr< IEditorViewportLayoutEntity > >* LevelViewports = ViewportTab->GetViewports();

				if (LevelViewports != nullptr)
				{
					for(auto& Pair : *LevelViewports)
					{
						TSharedPtr<SLevelViewport> Viewport = StaticCastSharedPtr<ILevelViewportLayoutEntity>(Pair.Value)->AsLevelViewport();
						if( Viewport.IsValid() && Viewport->IsInForegroundTab() )
						{
							if( &Viewport->GetLevelViewportClient() == GCurrentLevelEditingViewportClient )
							{
								// If the viewport is visible and is also the current level editing viewport client
								// return it as the active viewport
								return Viewport;
							}
							else if( !FirstVisibleViewport.IsValid() )
							{
								// If there is no current first visible viewport set it now
								// We will return this viewport if the current level editing viewport client is not visible
								FirstVisibleViewport = Viewport;
							}
						}
					}
				}
			}
		}
	}

	// Also check standalone viewports
	for( const TWeakPtr< SLevelViewport >& StandaloneViewportWeakPtr : StandaloneViewports )
	{
		const TSharedPtr< SLevelViewport > Viewport = StandaloneViewportWeakPtr.Pin();
		if( Viewport.IsValid() )
		{
			if( &Viewport->GetLevelViewportClient() == GCurrentLevelEditingViewportClient )
			{
				// If the viewport is visible and is also the current level editing viewport client
				// return it as the active viewport
				return Viewport;
			}
			else if( !FirstVisibleViewport.IsValid() )
			{
				// If there is no current first visible viewport set it now
				// We will return this viewport if the current level editing viewport client is not visible
				FirstVisibleViewport = Viewport;
			}
		}
	}
	
	// Return the first visible viewport if we found one.  This can be null if we didn't find any visible viewports
	return FirstVisibleViewport;
}


TSharedRef< SWidget > SLevelEditor::GetParentWidget()
{
	return AsShared();
}

void SLevelEditor::BringToFront()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>( LevelEditorModuleName );
	TSharedPtr<SDockTab> LevelEditorTab = LevelEditorModule.GetLevelEditorInstanceTab().Pin();
	TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
	if (LevelEditorTabManager.IsValid() && LevelEditorTab.IsValid())
	{
		LevelEditorTabManager->DrawAttention( LevelEditorTab.ToSharedRef() );
	}
}

TSharedRef< SDockTabStack > SLevelEditor::GetTabSpot( const EToolkitTabSpot::Type TabSpot )
{
	ensureMsgf(false, TEXT("Unimplemented"));
	return TSharedPtr<SDockTabStack>().ToSharedRef();
}

void SLevelEditor::OnToolkitHostingStarted( const TSharedRef< class IToolkit >& Toolkit )
{
	// @todo toolkit minor: We should consider only allowing a single toolkit for a specific asset editor type hosted
	//   at once.  OR, we allow multiple to be hosted, but we only show tabs for one at a time (fast switching.)
	//   Otherwise, it's going to be a huge cluster trying to distinguish tabs for different assets of the same type
	//   of editor
	
	TSharedPtr<FTabManager> LevelEditorTabManager = GetTabManager();

	HostedToolkits.Add( Toolkit );

	Toolkit->RegisterTabSpawners( LevelEditorTabManager.ToSharedRef() );

	// @todo toolkit minor: We should clean out old invalid array entries from time to time

	// Tell all of the toolkit area widgets about the new toolkit
	for( auto ToolBoxIt = ToolBoxTabs.CreateIterator(); ToolBoxIt; ++ToolBoxIt )
	{
		if( ToolBoxIt->IsValid() )
		{
			ToolBoxIt->Pin()->OnToolkitHostingStarted( Toolkit );
		}
	}

	// Tell all of the toolkit area widgets about the new toolkit
	for( auto ToolBoxIt = ModesTabs.CreateIterator(); ToolBoxIt; ++ToolBoxIt )
	{
		if( ToolBoxIt->IsValid() )
		{
			ToolBoxIt->Pin()->OnToolkitHostingStarted( Toolkit );
		}
	}
}

void SLevelEditor::OnToolkitHostingFinished( const TSharedRef< class IToolkit >& Toolkit )
{
	TSharedPtr<FTabManager> LevelEditorTabManager = GetTabManager();

	Toolkit->UnregisterTabSpawners(LevelEditorTabManager.ToSharedRef());

	// Tell all of the toolkit area widgets that our toolkit was removed
	for( auto ToolBoxIt = ToolBoxTabs.CreateIterator(); ToolBoxIt; ++ToolBoxIt )
	{
		if( ToolBoxIt->IsValid() )
		{
			ToolBoxIt->Pin()->OnToolkitHostingFinished( Toolkit );
		}
	}

	// Tell all of the toolkit area widgets that our toolkit was removed
	for( auto ToolBoxIt = ModesTabs.CreateIterator(); ToolBoxIt; ++ToolBoxIt )
	{
		if( ToolBoxIt->IsValid() )
		{
			ToolBoxIt->Pin()->OnToolkitHostingFinished( Toolkit );
		}
	}

	HostedToolkits.Remove( Toolkit );

	// @todo toolkit minor: If user clicks X on all opened world-centric toolkit tabs, should we exit that toolkit automatically?
	//   Feel 50/50 about this.  It's totally valid to use the "Save" menu even after closing tabs, etc.  Plus, you can spawn the tabs back up using the tab area down-down menu.
}

TSharedPtr<FTabManager> SLevelEditor::GetTabManager() const
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>( LevelEditorModuleName );
	TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
	return LevelEditorTabManager;
}

void SLevelEditor::AttachSequencer( TSharedPtr<SWidget> SequencerWidget, TSharedPtr<IAssetEditorInstance> NewSequencerAssetEditor )
{
	struct Local
	{
		static void OnSequencerClosed( TSharedRef<SDockTab> DockTab, TWeakPtr<IAssetEditorInstance> InSequencerAssetEditor )
		{
			TSharedPtr<IAssetEditorInstance> AssetEditorInstance = InSequencerAssetEditor.Pin();

			if (AssetEditorInstance.IsValid())
			{
				InSequencerAssetEditor.Pin()->CloseWindow();
			}
		}
	};

	static bool bIsReentrant = false;

	if( !bIsReentrant )
	{
		TSharedPtr<SDockTab> Tab = TryInvokeTab(LevelEditorTabIds::Sequencer);
		if(Tab.IsValid())
		{
			// Close the sequence editor after invoking a sequencer tab instead of before so that the existing asset editor doesn't refer to a stale sequencer.
			if (SequencerAssetEditor.IsValid())
			{
				// Closing the window will invoke this method again but we are handling reopening with a new movie scene ourselves
				TGuardValue<bool> ReentrantGuard(bIsReentrant, true);
				// Shutdown cleanly
				SequencerAssetEditor.Pin()->CloseWindow();
			}

			if (!FGlobalTabmanager::Get()->OnOverrideDockableAreaRestore_Handler.IsBound())
			{
				// Don't allow standard tab closing behavior when the override is active
				Tab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateStatic(&Local::OnSequencerClosed, TWeakPtr<IAssetEditorInstance>(NewSequencerAssetEditor)));
			}
			if (SequencerWidget.IsValid() && NewSequencerAssetEditor.IsValid())
			{
				Tab->SetContent(SequencerWidget.ToSharedRef());
				SequencerWidgetPtr = SequencerWidget;
				SequencerAssetEditor = NewSequencerAssetEditor;
				if (FGlobalTabmanager::Get()->OnOverrideDockableAreaRestore_Handler.IsBound())
				{
					// @todo vreditor: more general vr editor tab manager should handle windows instead
					// Close the original tab so we just work with the override window
					Tab->RequestCloseTab();
				}
			}
			else
			{
				Tab->SetContent(SNullWidget::NullWidget);
				SequencerAssetEditor.Reset();
			}
		}
	}
}

TSharedRef<SDockTab> SLevelEditor::SummonDetailsPanel( FName TabIdentifier )
{
	TSharedRef<SActorDetails> ActorDetails = StaticCastSharedRef<SActorDetails>( CreateActorDetails( TabIdentifier ) );

	const FText Label = NSLOCTEXT( "LevelEditor", "DetailsTabTitle", "Details" );

	TSharedRef<SDockTab> DocTab = SNew(SDockTab)
		.Icon( FEditorStyle::GetBrush( "LevelEditor.Tabs.Details" ) )
		.Label( Label )
		.ToolTip( IDocumentation::Get()->CreateToolTip( Label, nullptr, "Shared/LevelEditor", "DetailsTab" ) )
		[
			SNew( SBox )
			.AddMetaData<FTutorialMetaData>(FTutorialMetaData(TEXT("ActorDetails"), TEXT("LevelEditorSelectionDetails")))
			[
				ActorDetails
			]
		];

	return DocTab;
}
/** Method to call when a tab needs to be spawned by the FLayoutService */
TSharedRef<SDockTab> SLevelEditor::SpawnLevelEditorTab( const FSpawnTabArgs& Args, FName TabIdentifier, FString InitializationPayload )
{
	if( TabIdentifier == LevelEditorTabIds::LevelEditorViewport)
	{
		return this->BuildViewportTab( NSLOCTEXT("LevelViewportTypes", "LevelEditorViewport", "Viewport 1"), TEXT("Viewport 1"), InitializationPayload );
	}
	else if( TabIdentifier == LevelEditorTabIds::LevelEditorViewport_Clone1)
	{
		return this->BuildViewportTab( NSLOCTEXT("LevelViewportTypes", "LevelEditorViewport_Clone1", "Viewport 2"), TEXT("Viewport 2"), InitializationPayload );
	}
	else if( TabIdentifier == LevelEditorTabIds::LevelEditorViewport_Clone2)
	{
		return this->BuildViewportTab( NSLOCTEXT("LevelViewportTypes", "LevelEditorViewport_Clone2", "Viewport 3"), TEXT("Viewport 3"), InitializationPayload );
	}
	else if( TabIdentifier == LevelEditorTabIds::LevelEditorViewport_Clone3)
	{
		return this->BuildViewportTab( NSLOCTEXT("LevelViewportTypes", "LevelEditorViewport_Clone3", "Viewport 4"), TEXT("Viewport 4"), InitializationPayload );
	}
	else if( TabIdentifier == LevelEditorTabIds::LevelEditorToolBar)
	{
		return SNew( SDockTab )
			.Label( NSLOCTEXT("LevelEditor", "ToolBarTabTitle", "Toolbar") )
			.ShouldAutosize(true)
			.Icon( FEditorStyle::GetBrush("ToolBar.Icon") )
			[
				SNew(SHorizontalBox)
				.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("LevelEditorToolbar")))
				+SHorizontalBox::Slot()
				.FillWidth(1)
				.VAlign(VAlign_Bottom)
				.HAlign(HAlign_Left)
				[
					FLevelEditorToolBar::MakeLevelEditorToolBar( LevelEditorCommands.ToSharedRef(), SharedThis(this) )
				]
			];

	}
	else if (TabIdentifier == FEditorModeTools::EditorModeToolbarTabName)
	{
		return GLevelEditorModeTools().MakeModeToolbarTab();
	}
	else if( TabIdentifier == TEXT("LevelEditorSelectionDetails") || TabIdentifier == TEXT("LevelEditorSelectionDetails2") || TabIdentifier == TEXT("LevelEditorSelectionDetails3") || TabIdentifier == TEXT("LevelEditorSelectionDetails4") )
	{
		TSharedRef<SDockTab> DetailsPanel = SummonDetailsPanel( TabIdentifier );
		GUnrealEd->UpdateFloatingPropertyWindows();
		return DetailsPanel;
	}
	else if( TabIdentifier == LevelEditorTabIds::LevelEditorToolBox )
	{
		TSharedRef<SLevelEditorToolBox> NewToolBox = StaticCastSharedRef<SLevelEditorToolBox>( CreateToolBox() );

		TSharedRef<SDockTab> DockTab = SNew( SDockTab )
			.Icon( FEditorStyle::GetBrush( "LevelEditor.Tabs.Modes" ) )
			.Label( NSLOCTEXT( "LevelEditor", "ToolsTabTitle", "Toolbox" ) )
			.OnTabClosed(this, &SLevelEditor::OnToolboxTabClosed)
			[
				SNew( SBox )
				.AddMetaData<FTutorialMetaData>(FTutorialMetaData(TEXT("ToolsPanel"), TEXT("LevelEditorToolBox")))
				[
					NewToolBox
				]
			];

		NewToolBox->SetParentTab(DockTab);

		return DockTab;
	}
	else if (TabIdentifier == LevelEditorTabIds::PlacementBrowser)
	{
		if(!GetDefault<UEditorStyleSettings>()->bEnableLegacyEditorModeUI)
		{
			return
				SNew(SDockTab)
				.Icon(FEditorStyle::GetBrush("LevelEditor.Tabs.PlacementBrowser"))
				.Label(NSLOCTEXT("LevelEditor", "PlacementBrowserTitle", "Place Actors"))
				.AddMetaData<FTutorialMetaData>(FTutorialMetaData(TEXT("PlacementBrowser"), TEXT("PlacementBrowser")))
				[
					IPlacementModeModule::Get().CreatePlacementModeBrowser()
				];
		}
	}
	else if( TabIdentifier == LevelEditorTabIds::LevelEditorBuildAndSubmit )
	{
		TSharedRef<SLevelEditorBuildAndSubmit> NewBuildAndSubmit = SNew( SLevelEditorBuildAndSubmit, SharedThis( this ) );

		TSharedRef<SDockTab> NewTab = SNew( SDockTab )
			.Icon( FEditorStyle::GetBrush( "LevelEditor.Tabs.BuildAndSubmit" ) )
			.Label( NSLOCTEXT("LevelEditor", "BuildAndSubmitTabTitle", "Build and Submit") )
			[
				NewBuildAndSubmit
			];

		NewBuildAndSubmit->SetDockableTab(NewTab);

		return NewTab;
	}
	else if( TabIdentifier == LevelEditorTabIds::LevelEditorSceneOutliner)
	{
		SceneOutliner::FInitializationOptions InitOptions;
		InitOptions.bShowTransient = true;
		InitOptions.Mode = ESceneOutlinerMode::ActorBrowsing;
		{
			UToolMenus* ToolMenus = UToolMenus::Get();
			static const FName MenuName = "LevelEditor.LevelEditorSceneOutliner.ContextMenu";
			if (!ToolMenus->IsMenuRegistered(MenuName))
			{
				UToolMenu* Menu = ToolMenus->RegisterMenu(MenuName, "SceneOutliner.DefaultContextMenuBase");
				FToolMenuSection& Section = Menu->AddDynamicSection("LevelEditorContextMenu", FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
				{
					FName LevelContextMenuName = FLevelEditorContextMenu::GetContextMenuName(ELevelEditorMenuContext::SceneOutliner);
					if (LevelContextMenuName != NAME_None)
					{
						// Extend the menu even if no actors selected, as Edit menu should always exist for scene outliner
						UToolMenu* OtherMenu = UToolMenus::Get()->GenerateMenu(LevelContextMenuName, InMenu->Context);
						InMenu->Sections.Append(OtherMenu->Sections);
					}
				}));
				Section.InsertPosition = FToolMenuInsert("MainSection", EToolMenuInsertType::Before);
			}

			TWeakPtr<SLevelEditor> WeakLevelEditor = SharedThis(this);
			InitOptions.ModifyContextMenu.BindLambda([=](FName& OutMenuName, FToolMenuContext& MenuContext)
			{
				OutMenuName = MenuName;

				if (WeakLevelEditor.IsValid())
				{
					FLevelEditorContextMenu::InitMenuContext(MenuContext, WeakLevelEditor, ELevelEditorMenuContext::SceneOutliner);
				}
			});
		}


		FText Label = NSLOCTEXT( "LevelEditor", "SceneOutlinerTabTitle", "World Outliner" );

		FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::Get().LoadModuleChecked<FSceneOutlinerModule>("SceneOutliner");
		TSharedRef<ISceneOutliner> SceneOutlinerRef = SceneOutlinerModule.CreateSceneOutliner(
			InitOptions,
			FOnActorPicked() /* Not used for outliner when in browsing mode */);
		SceneOutlinerPtr = SceneOutlinerRef;

		return SNew( SDockTab )
			.Icon( FEditorStyle::GetBrush( "LevelEditor.Tabs.Outliner" ) )
			.Label( Label )
			.ToolTip( IDocumentation::Get()->CreateToolTip( Label, nullptr, "Shared/LevelEditor", "SceneOutlinerTab" ) )
			[
				SNew(SBorder)
				.Padding(4)
				.BorderImage( FEditorStyle::GetBrush("ToolPanel.GroupBorder") )
				.AddMetaData<FTutorialMetaData>(FTutorialMetaData(TEXT("SceneOutliner"), TEXT("LevelEditorSceneOutliner")))
				[
					SceneOutlinerRef
				]
			];
	}
	else if(TabIdentifier == LevelEditorTabIds::LevelEditorLayerBrowser)
	{
		FLayersModule& LayersModule = FModuleManager::LoadModuleChecked<FLayersModule>( "Layers" );
		return SNew( SDockTab )
			.Icon( FEditorStyle::GetBrush( "LevelEditor.Tabs.Layers" ) )
			.Label( NSLOCTEXT("LevelEditor", "LayersTabTitle", "Layers") )
			[
				SNew(SBorder)
				.Padding( 0 )
				.BorderImage( FEditorStyle::GetBrush("ToolPanel.GroupBorder") )
				.AddMetaData<FTutorialMetaData>(FTutorialMetaData(TEXT("LayerBrowser"), TEXT("LevelEditorLayerBrowser")))
				[
					LayersModule.CreateLayerBrowser()
				]
			];
	}
	else if (TabIdentifier == LevelEditorTabIds::LevelEditorHierarchicalLODOutliner)
	{
		FText Label = NSLOCTEXT("LevelEditor", "HLODOutlinerTabTitle", "Hierarchical LOD Outliner");

		FHierarchicalLODOutlinerModule& HLODModule = FModuleManager::LoadModuleChecked<FHierarchicalLODOutlinerModule>("HierarchicalLODOutliner");
		return SNew(SDockTab)
			.Icon(FEditorStyle::GetBrush("LevelEditor.Tabs.HLOD"))
			.Label(Label)
			.ToolTip(IDocumentation::Get()->CreateToolTip(Label, nullptr, "Shared/Editor/HLOD", "main"))
			[
				HLODModule.CreateHLODOutlinerWidget()
			];
	}
	else if( TabIdentifier == LevelEditorTabIds::WorldBrowserHierarchy)
	{
		FWorldBrowserModule& WorldBrowserModule = FModuleManager::LoadModuleChecked<FWorldBrowserModule>( "WorldBrowser" );
		return SNew( SDockTab )
			.Icon( FEditorStyle::GetBrush( "LevelEditor.Tabs.WorldBrowser" ) )
			.Label( NSLOCTEXT("LevelEditor", "WorldBrowserHierarchyTabTitle", "Levels") )
			[
				WorldBrowserModule.CreateWorldBrowserHierarchy()
			];
	}
	else if( TabIdentifier == LevelEditorTabIds::WorldBrowserDetails)
	{
		FWorldBrowserModule& WorldBrowserModule = FModuleManager::LoadModuleChecked<FWorldBrowserModule>( "WorldBrowser" );
		return SNew( SDockTab )
			.Icon( FEditorStyle::GetBrush( "LevelEditor.Tabs.WorldBrowserDetails" ) )
			.Label( NSLOCTEXT("LevelEditor", "WorldBrowserDetailsTabTitle", "Level Details") )
			[
				WorldBrowserModule.CreateWorldBrowserDetails()
			];
	}
	else if( TabIdentifier == LevelEditorTabIds::WorldBrowserComposition )
	{
		FWorldBrowserModule& WorldBrowserModule = FModuleManager::LoadModuleChecked<FWorldBrowserModule>( "WorldBrowser" );
		return SNew( SDockTab )
			.Icon( FEditorStyle::GetBrush( "LevelEditor.Tabs.WorldBrowserComposition" ) )
			.Label( NSLOCTEXT("LevelEditor", "WorldBrowserCompositionTabTitle", "World Composition") )
			[
				WorldBrowserModule.CreateWorldBrowserComposition()
			];
	}
	else if(TabIdentifier == LevelEditorTabIds::Sequencer)
	{
		if (FSlateStyleRegistry::FindSlateStyle("LevelSequenceEditorStyle"))
		{
			// @todo sequencer: remove when world-centric mode is added
			return SNew(SDockTab)
				.Icon(FSlateStyleRegistry::FindSlateStyle("LevelSequenceEditorStyle")->GetBrush("LevelSequenceEditor.Tabs.Sequencer"))
				.Label(NSLOCTEXT("Sequencer", "SequencerMainTitle", "Sequencer"))
				[
					SNullWidget::NullWidget
				];
		}
	}
	else if(TabIdentifier == LevelEditorTabIds::SequencerGraphEditor )
	{
		const FSlateIcon SequencerGraphIcon = FSlateIcon(FEditorStyle::GetStyleSetName(), "GenericCurveEditor.TabIcon");
		// @todo sequencer: remove when world-centric mode is added
		return SNew(SDockTab)
			.Icon(SequencerGraphIcon.GetIcon())
			.Label(NSLOCTEXT("Sequencer", "SequencerMainGraphEditorTitle", "Sequencer Curves"))
			[
				SNullWidget::NullWidget
			];
	}
	else if( TabIdentifier == LevelEditorTabIds::LevelEditorStatsViewer )
	{
		FStatsViewerModule& StatsViewerModule = FModuleManager::Get().LoadModuleChecked<FStatsViewerModule>( "StatsViewer" );
		return SNew( SDockTab )
			.Icon( FEditorStyle::GetBrush( "LevelEditor.Tabs.StatsViewer" ) )
			.Label( NSLOCTEXT("LevelEditor", "StatsViewerTabTitle", "Statistics") )
			[
				StatsViewerModule.CreateStatsViewer()
			];
	}
	else if (TabIdentifier == LevelEditorTabIds::WorldSettings)
	{
		FPropertyEditorModule& PropPlugin = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		FDetailsViewArgs DetailsViewArgs( false, false, true, FDetailsViewArgs::HideNameArea, false, GUnrealEd );
		DetailsViewArgs.bShowActorLabel = false;

		WorldSettingsView = PropPlugin.CreateDetailView( DetailsViewArgs );

		if (GetWorld() != NULL)
		{
			WorldSettingsView->SetObject(GetWorld()->GetWorldSettings());
		}

		return SNew( SDockTab )
			.Icon( FEditorStyle::GetBrush( "LevelEditor.WorldProperties.Tab" ) )
			.Label( NSLOCTEXT("LevelEditor", "WorldSettingsTabTitle", "World Settings") )
			.AddMetaData<FTutorialMetaData>(FTutorialMetaData(TEXT("WorldSettings"), TEXT("WorldSettingsTab")))
			[
				WorldSettingsView.ToSharedRef()
			];
	}
	else if( TabIdentifier == LevelEditorTabIds::LevelEditorEnvironmentLightingViewer)
	{
		FEnvironmentLightingViewerModule& EnvironmentLightingViewerModule = FModuleManager::Get().LoadModuleChecked<FEnvironmentLightingViewerModule>( "EnvironmentLightingViewer" );
		return SNew(SDockTab)
			.Icon(FEditorStyle::GetBrush("EditorViewport.ReflectionOverrideMode"))
			.Label(NSLOCTEXT("LevelEditor", "EnvironmentLightingViewerTitle", "Env. Light Mixer"))
			[
				EnvironmentLightingViewerModule.CreateEnvironmentLightingViewer()
			];
	}
	
	return SNew(SDockTab);
}

bool SLevelEditor::CanSpawnEditorModeToolbarTab(const FSpawnTabArgs& Args) const
{
	return GLevelEditorModeTools().ShouldShowModeToolbar();
}

bool SLevelEditor::CanSpawnEditorModeToolboxTab(const FSpawnTabArgs& Args) const
{
	return HasAnyHostedEditorModeToolkit();
}

bool SLevelEditor::HasAnyHostedEditorModeToolkit() const
{
	for (TSharedPtr<IToolkit> Toolkit : HostedToolkits)
	{
		if (Toolkit->GetScriptableEditorMode() || Toolkit->GetEditorMode())
		{
			return true;
		}
	}
	return false;
}

TSharedPtr<SDockTab> SLevelEditor::TryInvokeTab( FName TabID )
{
	TSharedPtr<FTabManager> LevelEditorTabManager = GetTabManager();
	return LevelEditorTabManager->TryInvokeTab(TabID);
}

void SLevelEditor::SyncDetailsToSelection()
{
	static const FName DetailsTabIdentifiers[] = { 
		LevelEditorTabIds::LevelEditorSelectionDetails, 
		LevelEditorTabIds::LevelEditorSelectionDetails2, 
		LevelEditorTabIds::LevelEditorSelectionDetails3, 
		LevelEditorTabIds::LevelEditorSelectionDetails4 };

	FPropertyEditorModule& PropPlugin = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FName FirstClosedDetailsTabIdentifier;

	// First see if there is an already open details view that can handle the request
	// For instance, if "Details 3" is open, we don't want to open "Details 2" to handle this
	for(const FName& DetailsTabIdentifier : DetailsTabIdentifiers)
	{
		TSharedPtr<IDetailsView> DetailsView = PropPlugin.FindDetailView(DetailsTabIdentifier);

		if(!DetailsView.IsValid())
		{
			// Track the first closed details view in case no currently open ones can handle our request
			if(FirstClosedDetailsTabIdentifier.IsNone())
			{
				FirstClosedDetailsTabIdentifier = DetailsTabIdentifier;
			}
			continue;
		}

		if(DetailsView->IsUpdatable() && !DetailsView->IsLocked())
		{
			TryInvokeTab(DetailsTabIdentifier);
			return;
		}
	}

	// If we got this far then there were no open details views, so open the first available one
	if(!FirstClosedDetailsTabIdentifier.IsNone())
	{
		TryInvokeTab(FirstClosedDetailsTabIdentifier);
	}
}

/** Builds a viewport tab. */
TSharedRef<SDockTab> SLevelEditor::BuildViewportTab( const FText& Label, const FString LayoutId, const FString& InitializationPayload )
{
	// The tab must be created before the viewport layout because the layout needs them
	TSharedRef< SDockTab > DockableTab =
		SNew(SDockTab)
		.Label(Label)
		.Icon(FEditorStyle::GetBrush("LevelEditor.Tabs.Viewports"))
		.OnTabClosed(this, &SLevelEditor::OnViewportTabClosed);
		
	// Create a new tab
	TSharedRef<FLevelViewportTabContent> ViewportTabContent = MakeShareable(new FLevelViewportTabContent());

	// Track the viewport
	CleanupPointerArray(ViewportTabs);
	ViewportTabs.Add(ViewportTabContent);

	ViewportTabContent->Initialize(SharedThis(this), DockableTab, LayoutId);

	// Restore transient camera position
	RestoreViewportTabInfo(ViewportTabContent);

	return DockableTab;
}

void SLevelEditor::OnViewportTabClosed(TSharedRef<SDockTab> ClosedTab)
{
	TWeakPtr<FLevelViewportTabContent>* const ClosedTabContent = ViewportTabs.FindByPredicate([&ClosedTab](TWeakPtr<FLevelViewportTabContent>& InPotentialElement) -> bool
	{
		TSharedPtr<FLevelViewportTabContent> ViewportTabContent = InPotentialElement.Pin();
		return ViewportTabContent.IsValid() && ViewportTabContent->BelongsToTab(ClosedTab);
	});

	if(ClosedTabContent)
	{
		TSharedPtr<FLevelViewportTabContent> ClosedTabContentPin = ClosedTabContent->Pin();
		if(ClosedTabContentPin.IsValid())
		{
			SaveViewportTabInfo(ClosedTabContentPin.ToSharedRef());

			// Untrack the viewport
			ViewportTabs.Remove(ClosedTabContentPin);
			CleanupPointerArray(ViewportTabs);
		}
	}
}

void SLevelEditor::OnToolboxTabClosed(TSharedRef<SDockTab> ClosedTab)
{
	GLevelEditorModeTools().ActivateDefaultMode();
}


void SLevelEditor::SaveViewportTabInfo(TSharedRef<const FLevelViewportTabContent> ViewportTabContent)
{
	const TMap<FName, TSharedPtr<IEditorViewportLayoutEntity>>* const Viewports = ViewportTabContent->GetViewports();
	if(Viewports)
	{
		const FString& LayoutId = ViewportTabContent->GetLayoutString();
		for (auto& Pair : *Viewports)
		{
			TSharedPtr<SLevelViewport> Viewport = StaticCastSharedPtr<ILevelViewportLayoutEntity>(Pair.Value)->AsLevelViewport();

			if( !Viewport.IsValid() )
			{
				continue;
			}

			//@todo there could potentially be more than one of the same viewport type.  This effectively takes the last one of a specific type
			const FLevelEditorViewportClient& LevelViewportClient = Viewport->GetLevelViewportClient();
			const FString Key = FString::Printf(TEXT("%s[%d]"), *LayoutId, static_cast<int32>(LevelViewportClient.ViewportType));
			TransientEditorViews.Add(
				Key, FLevelViewportInfo( 
					LevelViewportClient.GetViewLocation(),
					LevelViewportClient.GetViewRotation(), 
					LevelViewportClient.GetOrthoZoom()
					)
				);
		}
	}
}

void SLevelEditor::RestoreViewportTabInfo(TSharedRef<FLevelViewportTabContent> ViewportTabContent) const
{
	const TMap<FName, TSharedPtr<IEditorViewportLayoutEntity>>* const Viewports = ViewportTabContent->GetViewports();
	if(Viewports)
	{
		const FString& LayoutId = ViewportTabContent->GetLayoutString();
		for (auto& Pair : *Viewports)
		{
			TSharedPtr<SLevelViewport> Viewport = StaticCastSharedPtr<ILevelViewportLayoutEntity>(Pair.Value)->AsLevelViewport();
			if( !Viewport.IsValid() )
			{
				continue;
			}

			FLevelEditorViewportClient& LevelViewportClient = Viewport->GetLevelViewportClient();
			bool bInitializedOrthoViewport = false;
			for (int32 ViewportType = 0; ViewportType < LVT_MAX; ViewportType++)
			{
				if (ViewportType == LVT_Perspective || !bInitializedOrthoViewport)
				{
					const FString Key = FString::Printf(TEXT("%s[%d]"), *LayoutId, ViewportType);
					const FLevelViewportInfo* const TransientEditorView = TransientEditorViews.Find(Key);
					if (TransientEditorView)
					{
						LevelViewportClient.SetInitialViewTransform(
							static_cast<ELevelViewportType>(ViewportType),
							TransientEditorView->CamPosition,
							TransientEditorView->CamRotation,
							TransientEditorView->CamOrthoZoom
							);

						if (ViewportType != LVT_Perspective)
						{
							bInitializedOrthoViewport = true;
						}
					}
				}
			}
		}
	}
}

void SLevelEditor::ResetViewportTabInfo()
{
	TransientEditorViews.Reset();
}

TSharedRef<SWidget> SLevelEditor::RestoreContentArea( const TSharedRef<SDockTab>& OwnerTab, const TSharedRef<SWindow>& OwnerWindow )
{
	const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();

	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>( LevelEditorModuleName );
	LevelEditorModule.SetLevelEditorTabManager(OwnerTab);
	
	TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();

	// Register Level Editor tab spawners
	{
		{
			const FText ViewportTooltip = NSLOCTEXT("LevelEditorTabs", "LevelEditorViewportTooltip", "Open a Viewport tab. Use this to view and edit the current level.");
			const FSlateIcon ViewportIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Viewports");

			LevelEditorTabManager->RegisterTabSpawner(LevelEditorTabIds::LevelEditorViewport, FOnSpawnTab::CreateSP(this, &SLevelEditor::SpawnLevelEditorTab, LevelEditorTabIds::LevelEditorViewport, FString()) )
				.SetDisplayName(NSLOCTEXT("LevelEditorTabs", "LevelEditorViewport", "Viewport 1"))
				.SetTooltipText(ViewportTooltip)
				.SetGroup( MenuStructure.GetLevelEditorViewportsCategory() )
				.SetIcon(ViewportIcon);

			LevelEditorTabManager->RegisterTabSpawner(LevelEditorTabIds::LevelEditorViewport_Clone1, FOnSpawnTab::CreateSP(this, &SLevelEditor::SpawnLevelEditorTab, LevelEditorTabIds::LevelEditorViewport_Clone1, FString()) )
				.SetDisplayName(NSLOCTEXT("LevelEditorTabs", "LevelEditorViewport_Clone1", "Viewport 2"))
				.SetTooltipText(ViewportTooltip)
				.SetGroup( MenuStructure.GetLevelEditorViewportsCategory() )
				.SetIcon(ViewportIcon);

			LevelEditorTabManager->RegisterTabSpawner(LevelEditorTabIds::LevelEditorViewport_Clone2, FOnSpawnTab::CreateSP(this, &SLevelEditor::SpawnLevelEditorTab, LevelEditorTabIds::LevelEditorViewport_Clone2, FString()) )
				.SetDisplayName(NSLOCTEXT("LevelEditorTabs", "LevelEditorViewport_Clone2", "Viewport 3"))
				.SetTooltipText(ViewportTooltip)
				.SetGroup( MenuStructure.GetLevelEditorViewportsCategory() )
				.SetIcon(ViewportIcon);

				LevelEditorTabManager->RegisterTabSpawner(LevelEditorTabIds::LevelEditorViewport_Clone3, FOnSpawnTab::CreateSP(this, &SLevelEditor::SpawnLevelEditorTab, LevelEditorTabIds::LevelEditorViewport_Clone3, FString()) )
				.SetDisplayName(NSLOCTEXT("LevelEditorTabs", "LevelEditorViewport_Clone3", "Viewport 4"))
				.SetTooltipText(ViewportTooltip)
				.SetGroup( MenuStructure.GetLevelEditorViewportsCategory() )
				.SetIcon(ViewportIcon);
		}

		{
			const FSlateIcon ToolbarIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Toolbar");
			LevelEditorTabManager->RegisterTabSpawner(LevelEditorTabIds::LevelEditorToolBar, FOnSpawnTab::CreateSP<SLevelEditor, FName, FString>(this, &SLevelEditor::SpawnLevelEditorTab, LevelEditorTabIds::LevelEditorToolBar, FString()))
				.SetDisplayName(NSLOCTEXT("LevelEditorTabs", "LevelEditorToolBar", "Toolbar"))
				.SetTooltipText(NSLOCTEXT("LevelEditorTabs", "LevelEditorToolBarTooltipText", "Open the Toolbar tab, which provides access to the most common / important actions."))
				.SetGroup( MenuStructure.GetLevelEditorCategory() )
				.SetIcon( ToolbarIcon );
		}

		{
			const FText DetailsTooltip = NSLOCTEXT("LevelEditorTabs", "LevelEditorSelectionDetailsTooltip", "Open a Details tab. Use this to view and edit properties of the selected object(s).");
			const FSlateIcon DetailsIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details");

			LevelEditorTabManager->RegisterTabSpawner(LevelEditorTabIds::LevelEditorSelectionDetails, FOnSpawnTab::CreateSP<SLevelEditor, FName, FString>(this, &SLevelEditor::SpawnLevelEditorTab, LevelEditorTabIds::LevelEditorSelectionDetails, FString()) )
				.SetDisplayName(NSLOCTEXT("LevelEditorTabs", "LevelEditorSelectionDetails", "Details 1"))
				.SetTooltipText(DetailsTooltip)
				.SetGroup( MenuStructure.GetLevelEditorDetailsCategory() )
				.SetIcon( DetailsIcon );

			LevelEditorTabManager->RegisterTabSpawner(LevelEditorTabIds::LevelEditorSelectionDetails2, FOnSpawnTab::CreateSP<SLevelEditor, FName, FString>(this, &SLevelEditor::SpawnLevelEditorTab, LevelEditorTabIds::LevelEditorSelectionDetails2, FString()) )
				.SetDisplayName(NSLOCTEXT("LevelEditorTabs", "LevelEditorSelectionDetails2", "Details 2"))
				.SetTooltipText(DetailsTooltip)
				.SetGroup( MenuStructure.GetLevelEditorDetailsCategory() )
				.SetIcon( DetailsIcon );

			LevelEditorTabManager->RegisterTabSpawner(LevelEditorTabIds::LevelEditorSelectionDetails3, FOnSpawnTab::CreateSP<SLevelEditor, FName, FString>(this, &SLevelEditor::SpawnLevelEditorTab, LevelEditorTabIds::LevelEditorSelectionDetails3, FString()) )
				.SetDisplayName(NSLOCTEXT("LevelEditorTabs", "LevelEditorSelectionDetails3", "Details 3"))
				.SetTooltipText(DetailsTooltip)
				.SetGroup( MenuStructure.GetLevelEditorDetailsCategory() )
				.SetIcon( DetailsIcon );

			LevelEditorTabManager->RegisterTabSpawner(LevelEditorTabIds::LevelEditorSelectionDetails4, FOnSpawnTab::CreateSP<SLevelEditor, FName, FString>(this, &SLevelEditor::SpawnLevelEditorTab, LevelEditorTabIds::LevelEditorSelectionDetails4, FString()) )
				.SetDisplayName(NSLOCTEXT("LevelEditorTabs", "LevelEditorSelectionDetails4", "Details 4"))
				.SetTooltipText(DetailsTooltip)
				.SetGroup( MenuStructure.GetLevelEditorDetailsCategory() )
				.SetIcon( DetailsIcon );
		}

		{

			FCanSpawnTab CanSpawnTabDelegate = FCanSpawnTab::CreateSP(this, &SLevelEditor::CanSpawnEditorModeToolboxTab);
			const FSlateIcon ToolsIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Modes");
			LevelEditorTabManager->RegisterTabSpawner(LevelEditorTabIds::LevelEditorToolBox, FOnSpawnTab::CreateSP<SLevelEditor, FName, FString>(this, &SLevelEditor::SpawnLevelEditorTab, LevelEditorTabIds::LevelEditorToolBox, FString()), CanSpawnTabDelegate)
				.SetDisplayName(NSLOCTEXT("LevelEditorTabs", "LevelEditorModesToolboxTab", "Active Mode Toolbox"))
				.SetTooltipText(NSLOCTEXT("LevelEditorTabs", "LevelEditorModesToolboxTabTooltipText", "Open the Modes tab, which contains the active editor mode's settings."))
				.SetGroup(MenuStructure.GetLevelEditorModesCategory())
				.SetIcon(ToolsIcon);
		}

		{
			const FSlateIcon ToolsIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.PlacementBrowser");
			FCanSpawnTab CanSpawnTabDelegate = FCanSpawnTab::CreateSP(this, &SLevelEditor::CanSpawnEditorModeToolbarTab);
			LevelEditorTabManager->RegisterTabSpawner(FEditorModeTools::EditorModeToolbarTabName, FOnSpawnTab::CreateSP<SLevelEditor, FName, FString>(this, &SLevelEditor::SpawnLevelEditorTab, FEditorModeTools::EditorModeToolbarTabName, FString()), CanSpawnTabDelegate)
				.SetDisplayName(NSLOCTEXT("LevelEditorTabs", "LevelEditorModesToolbarTab", "Active Mode Toolbar"))
				.SetTooltipText(NSLOCTEXT("LevelEditorTabs", "LevelEditorModesToolbarTabTooltipText", "Opens a toolbar for the active editor mode"))
				.SetGroup(MenuStructure.GetLevelEditorModesCategory())
				.SetIcon(ToolsIcon);
		}

		{
			const FSlateIcon ToolsIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.PlacementBrowser");
			LevelEditorTabManager->RegisterTabSpawner(LevelEditorTabIds::PlacementBrowser, FOnSpawnTab::CreateSP<SLevelEditor, FName, FString>(this, &SLevelEditor::SpawnLevelEditorTab, LevelEditorTabIds::PlacementBrowser, FString()))
				.SetDisplayName(NSLOCTEXT("LevelEditorTabs", "PlacementBrowser", "Place Actors"))
				.SetTooltipText(NSLOCTEXT("LevelEditorTabs", "PlacementBrowserTooltipText", "Actor Placement Browser"))
				.SetGroup(MenuStructure.GetLevelEditorCategory())
				.SetIcon(ToolsIcon);
		}

		{
			const FSlateIcon OutlinerIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Outliner");
		    LevelEditorTabManager->RegisterTabSpawner(LevelEditorTabIds::LevelEditorSceneOutliner, FOnSpawnTab::CreateSP<SLevelEditor, FName, FString>(this, &SLevelEditor::SpawnLevelEditorTab, LevelEditorTabIds::LevelEditorSceneOutliner, FString()) )
				.SetDisplayName(NSLOCTEXT("LevelEditorTabs", "LevelEditorSceneOutliner", "World Outliner"))
				.SetTooltipText(NSLOCTEXT("LevelEditorTabs", "LevelEditorSceneOutlinerTooltipText", "Open the World Outliner tab, which provides a searchable and filterable list of all actors in the world."))
				.SetGroup( MenuStructure.GetLevelEditorCategory() )	
				.SetIcon( OutlinerIcon );	
		}

		{
			const FSlateIcon LayersIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Layers");
			LevelEditorTabManager->RegisterTabSpawner(LevelEditorTabIds::LevelEditorLayerBrowser, FOnSpawnTab::CreateSP<SLevelEditor, FName, FString>(this, &SLevelEditor::SpawnLevelEditorTab, LevelEditorTabIds::LevelEditorLayerBrowser, FString()) )
				.SetDisplayName(NSLOCTEXT("LevelEditorTabs", "LevelEditorLayerBrowser", "Layers"))
				.SetTooltipText(NSLOCTEXT("LevelEditorTabs", "LevelEditorLayerBrowserTooltipText", "Open the Layers tab. Use this to manage which actors in the world belong to which layers."))
				.SetGroup( MenuStructure.GetLevelEditorCategory() )
				.SetIcon( LayersIcon );
		}

		{
			const FSlateIcon LayersIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.HLOD");
			LevelEditorTabManager->RegisterTabSpawner(LevelEditorTabIds::LevelEditorHierarchicalLODOutliner, FOnSpawnTab::CreateSP<SLevelEditor, FName, FString>(this, &SLevelEditor::SpawnLevelEditorTab, LevelEditorTabIds::LevelEditorHierarchicalLODOutliner, FString()))
				.SetDisplayName(NSLOCTEXT("LevelEditorTabs", "LevelEditorHierarchicalLODOutliner", "Hierarchical LOD Outliner"))
				.SetTooltipText(NSLOCTEXT("LevelEditorTabs", "LevelEditorHierarchicalLODOutlinerTooltipText", "Open the Hierarchical LOD Outliner."))
				.SetGroup(MenuStructure.GetLevelEditorCategory())
				.SetIcon(LayersIcon);
		}
		
		{
			LevelEditorTabManager->RegisterTabSpawner(LevelEditorTabIds::WorldBrowserHierarchy, FOnSpawnTab::CreateSP<SLevelEditor, FName, FString>(this, &SLevelEditor::SpawnLevelEditorTab, LevelEditorTabIds::WorldBrowserHierarchy, FString()) )
				.SetDisplayName(NSLOCTEXT("LevelEditorTabs", "WorldBrowserHierarchy", "Levels"))
				.SetTooltipText(NSLOCTEXT("LevelEditorTabs", "WorldBrowserHierarchyTooltipText", "Open the Levels tab. Use this to manage the levels in the current project."))
				.SetGroup( WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory() )
				.SetIcon( FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.WorldBrowser") );
			
			LevelEditorTabManager->RegisterTabSpawner(LevelEditorTabIds::WorldBrowserDetails, FOnSpawnTab::CreateSP<SLevelEditor, FName, FString>(this, &SLevelEditor::SpawnLevelEditorTab, LevelEditorTabIds::WorldBrowserDetails, FString()) )
				.SetMenuType( ETabSpawnerMenuType::Hidden )
				.SetDisplayName(NSLOCTEXT("LevelEditorTabs", "WorldBrowserDetails", "Level Details"))
				.SetGroup( WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory() )
				.SetIcon( FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.WorldBrowserDetails") );
		
			LevelEditorTabManager->RegisterTabSpawner(LevelEditorTabIds::WorldBrowserComposition, FOnSpawnTab::CreateSP<SLevelEditor, FName, FString>(this, &SLevelEditor::SpawnLevelEditorTab, LevelEditorTabIds::WorldBrowserComposition, FString()) )
				.SetMenuType( ETabSpawnerMenuType::Hidden )
				.SetDisplayName(NSLOCTEXT("LevelEditorTabs", "WorldBrowserComposition", "World Composition"))
				.SetGroup( WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory() )
				.SetIcon( FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.WorldBrowserComposition") );
		}

		{
			const FSlateIcon StatsViewerIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.StatsViewer");
			LevelEditorTabManager->RegisterTabSpawner(LevelEditorTabIds::LevelEditorStatsViewer, FOnSpawnTab::CreateSP<SLevelEditor, FName, FString>(this, &SLevelEditor::SpawnLevelEditorTab, LevelEditorTabIds::LevelEditorStatsViewer, FString()))
				.SetDisplayName(NSLOCTEXT("LevelEditorTabs", "LevelEditorStatsViewer", "Statistics"))
				.SetTooltipText(NSLOCTEXT("LevelEditorTabs", "LevelEditorStatsViewerTooltipText", "Open the Statistics tab, in order to see data pertaining to lighting, textures and primitives."))
				.SetGroup(MenuStructure.GetLevelEditorCategory())
				.SetIcon(StatsViewerIcon);
		}

		{
			// @todo remove when world-centric mode is added
			const FSlateIcon SequencerIcon("LevelSequenceEditorStyle", "LevelSequenceEditor.Tabs.Sequencer" );
			LevelEditorTabManager->RegisterTabSpawner(LevelEditorTabIds::Sequencer, FOnSpawnTab::CreateSP<SLevelEditor, FName, FString>(this, &SLevelEditor::SpawnLevelEditorTab, LevelEditorTabIds::Sequencer, FString()) )
				.SetDisplayName(NSLOCTEXT("LevelEditorTabs", "Sequencer", "Sequencer"))
				.SetGroup( MenuStructure.GetLevelEditorCinematicsCategory() )
				.SetIcon( SequencerIcon );
		}

		{
			// @todo remove when world-centric mode is added
			const FSlateIcon SequencerGraphIcon = FSlateIcon(FEditorStyle::GetStyleSetName(), "GenericCurveEditor.TabIcon");
			LevelEditorTabManager->RegisterTabSpawner(LevelEditorTabIds::SequencerGraphEditor, FOnSpawnTab::CreateSP<SLevelEditor, FName, FString>(this, &SLevelEditor::SpawnLevelEditorTab, LevelEditorTabIds::SequencerGraphEditor, FString()))
				.SetMenuType(ETabSpawnerMenuType::Type::Hidden);
		}

		{
			const FSlateIcon WorldPropertiesIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.WorldProperties.Tab");
			LevelEditorTabManager->RegisterTabSpawner(LevelEditorTabIds::WorldSettings, FOnSpawnTab::CreateSP<SLevelEditor, FName, FString>(this, &SLevelEditor::SpawnLevelEditorTab, LevelEditorTabIds::WorldSettings, FString()) )
				.SetDisplayName(NSLOCTEXT("LevelEditorTabs", "WorldSettings", "World Settings"))
				.SetTooltipText(NSLOCTEXT("LevelEditorTabs", "WorldSettingsTooltipText", "Open the World Settings tab, in which global properties of the level can be viewed and edited."))
				.SetGroup( MenuStructure.GetLevelEditorCategory() )
				.SetIcon( WorldPropertiesIcon );
		}

		{
			const FSlateIcon EnvironmentLightingViewerIcon(FEditorStyle::GetStyleSetName(), "EditorViewport.ReflectionOverrideMode");
			LevelEditorTabManager->RegisterTabSpawner(LevelEditorTabIds::LevelEditorEnvironmentLightingViewer, FOnSpawnTab::CreateSP<SLevelEditor, FName, FString>(this, &SLevelEditor::SpawnLevelEditorTab, LevelEditorTabIds::LevelEditorEnvironmentLightingViewer, FString()))
				.SetDisplayName(NSLOCTEXT("LevelEditorTabs", "EnvironmentLightingViewer", "Env. Light Mixer"))
				.SetTooltipText(NSLOCTEXT("LevelEditorTabs", "LevelEditorEnvironmentLightingViewerTooltipText", "Open the Environmment Lighting tab to edit all the entities important for world lighting."))
				.SetGroup(MenuStructure.GetLevelEditorCategory())
				.SetIcon(EnvironmentLightingViewerIcon);
		}

		FTabSpawnerEntry& BuildAndSubmitEntry = LevelEditorTabManager->RegisterTabSpawner(LevelEditorTabIds::LevelEditorBuildAndSubmit, FOnSpawnTab::CreateSP<SLevelEditor, FName, FString>(this, &SLevelEditor::SpawnLevelEditorTab, LevelEditorTabIds::LevelEditorBuildAndSubmit, FString()));
		BuildAndSubmitEntry.SetAutoGenerateMenuEntry(false);

		LevelEditorModule.OnRegisterTabs().Broadcast(LevelEditorTabManager);
	}

	// Rebuild the editor mode commands and their tab spawners before we restore the layout,
	// or there wont be any tab spawners for the modes.
	RefreshEditorModeCommands();

	// IMPORTANT: If you want to change the default value of "LevelEditor_Layout_v1.1" or "UnrealEd_Layout_v1.4" (even if you only change their version numbers), these are the steps to follow:
	// 1. Check out Engine\Config\Layouts\DefaultLayout.ini in Perforce.
	// 2. Change the code below as you wish and compile the code.
	// 3. (Optional:) Save your current layout so you can load it later.
	// 4. Close the editor.
	// 5. Manually remove Engine\Saved\Config\Windows\EditorLayout.ini
	// 6. Open the Editor, which will auto-regenerate a default EditorLayout.ini that uses your new code below.
	// 7. "Window" --> "Save Layout" --> "Save Layout As..."
	//     - Name: Default Editor Layout
	//     - Description: Default layout that the Unreal Editor automatically generates
	// 8. Either click on the toast generated by Unreal that would open the saving path or manually open Engine\Saved\Config\Layouts\ in your explorer
	// 9. Move and rename the new file (Engine\Saved\Config\Layouts\Default_Editor_Layout.ini) into Engine\Config\Layouts\DefaultLayout.ini. You might also have to modify:
	//     9.1. QAGame/Config/DefaultEditorLayout.ini
	//     9.2. Engine/Config/BaseEditorLayout.ini
	//     9.3. Etc
	// 10. Push the new "DefaultLayout.ini" together with your new code.
	// 11. Also update these instructions if you change the version number (e.g., from "UnrealEd_Layout_v1.4" to "UnrealEd_Layout_v1.5").
	const FName LayoutName = TEXT("LevelEditor_Layout_v1.2");
	const TSharedRef<FTabManager::FLayout> DefaultLayout = FTabManager::NewLayout(LayoutName)
		->AddArea
		(
			FTabManager::NewPrimaryArea()
			->SetOrientation( Orient_Horizontal )
			->SetExtensionId( "TopLevelArea" )
			->Split
			(
				FTabManager::NewSplitter()
				->SetOrientation( Orient_Vertical )
				->SetSizeCoefficient( 1 )
				->Split
				(
					FTabManager::NewSplitter()
					->SetSizeCoefficient( .75f )
					->SetOrientation(Orient_Horizontal)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient( 0.3f )
						->AddTab(LevelEditorTabIds::PlacementBrowser, ETabState::OpenedTab)
						->AddTab(LevelEditorTabIds::LevelEditorToolBox, ETabState::ClosedTab)
						->SetForegroundTab(LevelEditorTabIds::PlacementBrowser)
					)
					->Split
					(
						FTabManager::NewSplitter()
						->SetOrientation(Orient_Vertical)
						->SetSizeCoefficient( 1.15f )
						->Split
						(
							FTabManager::NewStack()
							->SetHideTabWell(true)
							->AddTab(LevelEditorTabIds::LevelEditorToolBar, ETabState::OpenedTab)
						)
						->Split
						(
							FTabManager::NewStack()
							->SetHideTabWell(true)
							->AddTab(FEditorModeTools::EditorModeToolbarTabName, ETabState::ClosedTab)
						)
						->Split
						(
							FTabManager::NewStack()
							->SetHideTabWell(true)
							->SetSizeCoefficient( 1.0f )
							->AddTab(LevelEditorTabIds::LevelEditorViewport, ETabState::OpenedTab)
						)
					)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(.4)
					->AddTab("ContentBrowserTab1", ETabState::OpenedTab)
					->AddTab(LevelEditorTabIds::OutputLog, ETabState::ClosedTab)
				)
			)
			->Split
			(
				FTabManager::NewSplitter()
				->SetSizeCoefficient(0.25f)
				->SetOrientation(Orient_Vertical)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.4f)
					->AddTab(LevelEditorTabIds::LevelEditorSceneOutliner, ETabState::OpenedTab)
					->AddTab(LevelEditorTabIds::LevelEditorLayerBrowser, ETabState::ClosedTab)

				)
				->Split
				(
					FTabManager::NewStack()
					->AddTab(LevelEditorTabIds::LevelEditorSelectionDetails, ETabState::OpenedTab)
					->AddTab(LevelEditorTabIds::WorldSettings, ETabState::ClosedTab)
					->SetForegroundTab(LevelEditorTabIds::LevelEditorSelectionDetails)
				)
			)
		);
	const EOutputCanBeNullptr OutputCanBeNullptr = EOutputCanBeNullptr::IfNoTabValid;
	TArray<FString> RemovedOlderLayoutVersions;
	const TSharedRef<FTabManager::FLayout> Layout = FLayoutSaveRestore::LoadFromConfig(GEditorLayoutIni,
		DefaultLayout, OutputCanBeNullptr, RemovedOlderLayoutVersions);

	// If older fields of the layout name (i.e., lower versions than "LevelEditor_Layout_v1.2") were found
	if (RemovedOlderLayoutVersions.Num() > 0)
	{
		// FMessageDialog - Notify the user that the layout version was updated and the current layout uses a deprecated one
		const FText TextTitle = LOCTEXT("LevelEditorVersionErrorTitle", "Unreal Level Editor Layout Version Mismatch");
		const FText TextBody = FText::Format(LOCTEXT("LevelEditorVersionErrorBody", "The expected Unreal Level Editor layout version is \"{0}\", while only version \"{1}\" was found. I.e., the current layout was created with a previous version of Unreal that is deprecated and no longer compatible.\n\nUnreal will continue with the default layout for its current version, the deprecated one has been removed.\n\nYou can create and save your custom layouts with \"Window\"->\"Save Layout\"->\"Save Layout As...\"."),
			FText::FromString(LayoutName.ToString()), FText::FromString(RemovedOlderLayoutVersions[0]));
		FMessageDialog::Open(EAppMsgType::Ok, TextBody, &TextTitle);
	}

	FLayoutExtender LayoutExtender;

	LevelEditorModule.OnRegisterLayoutExtensions().Broadcast(LayoutExtender);
	Layout->ProcessExtensions(LayoutExtender);

	const bool bEmbedTitleAreaContent = false;
	TSharedPtr<SWidget> ContentAreaWidget = LevelEditorTabManager->RestoreFrom(Layout, OwnerWindow, bEmbedTitleAreaContent, OutputCanBeNullptr);
	// ContentAreaWidget will only be nullptr if its main area contains invalid tabs (probably some layout bug). If so, reset layout to avoid potential crashes
	if (!ContentAreaWidget.IsValid())
	{
		// Try to load default layout to avoid nullptr.ToSharedRef() crash
		ContentAreaWidget = LevelEditorTabManager->RestoreFrom(DefaultLayout, OwnerWindow, bEmbedTitleAreaContent, EOutputCanBeNullptr::Never);
		// Warn user/developer
		const FString WarningMessage = FString::Format(TEXT("Level editor layout could not be loaded from the config file {0}, trying to reset this config file to the default one."), { *GEditorLayoutIni });
		UE_LOG(LogTemp, Warning, TEXT("%s"), *WarningMessage);
		ensureMsgf(false, TEXT("%s Some additional testing of that layout file should be done."));
	}
	check(ContentAreaWidget.IsValid());
	return ContentAreaWidget.ToSharedRef();
}

void SLevelEditor::HandleExperimentalSettingChanged(FName PropertyName)
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
	LevelEditorTabManager->UpdateMainMenu(true);
}

FName SLevelEditor::GetEditorModeTabId( FEditorModeID ModeID )
{
	return FName(*(FString("EditorMode.Tab.") + ModeID.ToString()));
}

void SLevelEditor::ToggleEditorMode( FEditorModeID ModeID )
{
	// Prompt the user if Matinee must be closed before activating new mode
	if (ModeID != FBuiltinEditorModes::EM_InterpEdit)
	{
		FEdMode* MatineeMode = GLevelEditorModeTools().GetActiveMode(FBuiltinEditorModes::EM_InterpEdit);
		if (MatineeMode && !MatineeMode->IsCompatibleWith(ModeID))
		{
			FEditorModeInfo MatineeModeInfo;
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorModeInfo(ModeID, MatineeModeInfo);
			FFormatNamedArguments Args;
			Args.Add(TEXT("ModeName"), MatineeModeInfo.Name);
			FText Msg = FText::Format(NSLOCTEXT("LevelEditor", "ModeSwitchCloseMatineeQ", "Activating '{ModeName}' editor mode will close UnrealMatinee.  Continue?"), Args);
			
			if (EAppReturnType::Yes != FMessageDialog::Open(EAppMsgType::YesNo, Msg))
			{
				return;
			}
		}
	}

	// Abort viewport tracking when switching editor mode
	if (GCurrentLevelEditingViewportClient)
	{
		GCurrentLevelEditingViewportClient->AbortTracking();
	}
		
	// *Important* - activate the mode first since FEditorModeTools::DeactivateMode will
	// activate the default mode when the stack becomes empty, resulting in multiple active visible modes.
	GLevelEditorModeTools().ActivateMode( ModeID );

	// Find and disable any other 'visible' modes since we only ever allow one of those active at a time.
	GLevelEditorModeTools().DeactivateOtherVisibleModes(ModeID);
}

bool SLevelEditor::IsModeActive( FEditorModeID ModeID )
{
	// The level editor changes the default mode to placement
	if ( ModeID == FBuiltinEditorModes::EM_Placement )
	{
		if (!GLevelEditorModeTools().IsOnlyVisibleActiveMode(ModeID))
		{
			return false;
		}
	}
	return GLevelEditorModeTools().IsModeActive( ModeID );
}

void SLevelEditor::EditorModeCommandsChanged()
{
	if (FLevelEditorModesCommands::IsRegistered())
	{
		FLevelEditorModesCommands::Unregister();
	}

	RefreshEditorModeCommands();
}

void SLevelEditor::OnEditorModeIdChanged(const FEditorModeID& ModeChangedID, bool bIsEnteringMode)
{
	if(bIsEnteringMode)
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();

		if (!HasAnyHostedEditorModeToolkit())
		{
			TSharedPtr<SDockTab> ToolboxTab = LevelEditorTabManager->FindExistingLiveTab(LevelEditorTabIds::LevelEditorToolBox);
			if (ToolboxTab.IsValid())
			{
				ToolboxTab->RequestCloseTab();
			}
		}
		else if (!GetDefault<UEditorStyleSettings>()->bEnableLegacyEditorModeUI)
		{
			LevelEditorTabManager->TryInvokeTab(LevelEditorTabIds::LevelEditorToolBox);
		}
	}
}

void SLevelEditor::RefreshEditorModeCommands()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>( "LevelEditor" );

	if(!FLevelEditorModesCommands::IsRegistered())
	{
		FLevelEditorModesCommands::Register();
	}
	const IWorkspaceMenuStructure& MenuStructure = WorkspaceMenu::GetMenuStructure();
	TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();

	// We need to remap all the actions to commands.
	const FLevelEditorModesCommands& Commands = FLevelEditorModesCommands::Get();

	int32 CommandIndex = 0;
	for( const FEditorModeInfo& Mode : GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->GetEditorModeInfoOrderedByPriority() )
	{
		// If the mode isn't visible don't create a menu option for it.
		if( !Mode.bVisible )
		{
			continue;
		}

		FName EditorModeTabName = GetEditorModeTabId( Mode.ID );
		FName EditorModeCommandName = FName(*(FString("EditorMode.") + Mode.ID.ToString()));

		TSharedPtr<FUICommandInfo> EditorModeCommand = 
			FInputBindingManager::Get().FindCommandInContext(Commands.GetContextName(), EditorModeCommandName);

		// If a command isn't yet registered for this mode, we need to register one.
		if ( EditorModeCommand.IsValid() && !LevelEditorCommands->IsActionMapped(Commands.EditorModeCommands[CommandIndex]) )
		{
			LevelEditorCommands->MapAction(
				Commands.EditorModeCommands[CommandIndex],
				FExecuteAction::CreateStatic( &SLevelEditor::ToggleEditorMode, Mode.ID ),
				FCanExecuteAction(),
				FIsActionChecked::CreateStatic( &SLevelEditor::IsModeActive, Mode.ID ));
		}

		CommandIndex++;
	}

	for( const auto& ToolBoxTab : ToolBoxTabs )
	{
		auto Tab = ToolBoxTab.Pin();
		if( Tab.IsValid() )
		{
			Tab->OnEditorModeCommandsChanged();
		}
	}
}

FReply SLevelEditor::OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent )
{
	// Check to see if any of the actions for the level editor can be processed by the current event
	// If we are in debug mode do not process commands
	if (FSlateApplication::Get().IsNormalExecution())
	{
		for (const auto& ActiveToolkit : HostedToolkits)
		{
			// A toolkit is active, so direct all command processing to it
			if (ActiveToolkit->ProcessCommandBindings(InKeyEvent))
			{
				return FReply::Handled();
			}
		}

		// No toolkit processed the key, so let the level editor have a chance at the keystroke
		if (LevelEditorCommands->ProcessCommandBindings(InKeyEvent))
		{
			return FReply::Handled();
		}
	}
	
	return FReply::Unhandled();
}

FReply SLevelEditor::OnKeyDownInViewport( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent )
{
	// Check to see if any of the actions for the level editor can be processed by the current keyboard from a viewport
	if( LevelEditorCommands->ProcessCommandBindings( InKeyEvent ) )
	{
		return FReply::Handled();
	}

	// NOTE: Currently, we don't bother allowing toolkits to get a chance at viewport keys

	return FReply::Unhandled();
}

/** Callback for when the level editor layout has changed */
void SLevelEditor::OnLayoutHasChanged()
{
	// ...
}

void SLevelEditor::SummonLevelViewportContextMenu()
{
	FLevelEditorContextMenu::SummonMenu( SharedThis( this ), ELevelEditorMenuContext::Viewport );
}

void SLevelEditor::SummonLevelViewportViewOptionMenu(const ELevelViewportType ViewOption)
{
	FLevelEditorContextMenu::SummonViewOptionMenu( SharedThis( this ), ViewOption);
}

const TArray< TSharedPtr< IToolkit > >& SLevelEditor::GetHostedToolkits() const
{
	return HostedToolkits;
}

TArray< TSharedPtr< IAssetViewport > > SLevelEditor::GetViewports() const
{
	TArray< TSharedPtr<IAssetViewport> > OutViewports;

	for( int32 TabIndex = 0; TabIndex < ViewportTabs.Num(); ++TabIndex )
	{
		TSharedPtr<FLevelViewportTabContent> ViewportTab = ViewportTabs[ TabIndex ].Pin();
		
		if (ViewportTab.IsValid())
		{
			const TMap< FName, TSharedPtr< IEditorViewportLayoutEntity > >* LevelViewports = ViewportTab->GetViewports();

			if (LevelViewports != NULL)
			{
				for (auto& Pair : *LevelViewports)
				{
					TSharedPtr<SLevelViewport> Viewport = StaticCastSharedPtr<ILevelViewportLayoutEntity>(Pair.Value)->AsLevelViewport();
					if( Viewport.IsValid() )
					{
						OutViewports.Add(Viewport);
					}
				}
			}
		}
	}

	// Also add any standalone viewports
	{
		for( const TWeakPtr< SLevelViewport >& StandaloneViewportWeakPtr : StandaloneViewports )
		{
			const TSharedPtr< SLevelViewport > Viewport = StandaloneViewportWeakPtr.Pin();
			if( Viewport.IsValid() )
			{
				OutViewports.Add( Viewport );
			}
		}
	}

	return OutViewports;
}
 
TSharedPtr<IAssetViewport> SLevelEditor::GetActiveViewportInterface()
{
	return GetActiveViewport();
}

TSharedPtr< class FAssetThumbnailPool > SLevelEditor::GetThumbnailPool() const
{
	return ThumbnailPool;
}

void SLevelEditor::AppendCommands( const TSharedRef<FUICommandList>& InCommandsToAppend )
{
	LevelEditorCommands->Append(InCommandsToAppend);
}

UWorld* SLevelEditor::GetWorld() const
{
	return World;
}

void SLevelEditor::HandleEditorMapChange( uint32 MapChangeFlags )
{
	ResetViewportTabInfo();

	if (WorldSettingsView.IsValid())
	{
		WorldSettingsView->SetObject(GetWorld()->GetWorldSettings(), true);
	}
}

void SLevelEditor::HandleAssetsDeleted(const TArray<UClass*>& DeletedClasses)
{
	bool bDeletedMaterials = false;
	for (UClass* AssetClass : DeletedClasses)
	{
		if (AssetClass->IsChildOf<UMaterialInterface>())
		{
			bDeletedMaterials = true;
			break;
		}
	}

	if (bDeletedMaterials)
	{
		// If a material asset has been deleted, it may be being referenced by the BSP model.
		// In case this is the case, invalidate the surface and immediately commit it (rather than waiting until the next tick as is usual),
		// to ensure that it is rebuilt prior to the viewport being redrawn.
		GetWorld()->InvalidateModelSurface(false);
		GetWorld()->CommitModelSurfaces();
	}
}

void SLevelEditor::OnActorSelectionChanged(const TArray<UObject*>& NewSelection, bool bForceRefresh)
{
	for (TSharedRef<SActorDetails> ActorDetails : GetAllActorDetails())
	{
		ActorDetails->SetObjects(NewSelection, bForceRefresh || bNeedsRefresh);
	}

	bNeedsRefresh = false;
}

void SLevelEditor::OnLevelActorOuterChanged(AActor* InActor, UObject* InOldOuter)
{
	bNeedsRefresh = true;
}

void SLevelEditor::AddStandaloneLevelViewport( const TSharedRef<SLevelViewport>& LevelViewport )
{
	CleanupPointerArray( StandaloneViewports );
	StandaloneViewports.Add( LevelViewport );
}


TSharedRef<SWidget> SLevelEditor::CreateActorDetails( const FName TabIdentifier )
{
	TSharedRef<SActorDetails> ActorDetails = SNew( SActorDetails, TabIdentifier, LevelEditorCommands, GetTabManager() );

	// Immediately update it (otherwise it will appear empty)
	{
		TArray<UObject*> SelectedActors;
		for( FSelectionIterator It( GEditor->GetSelectedActorIterator() ); It; ++It )
		{
			AActor* Actor = static_cast<AActor*>( *It );
			checkSlow( Actor->IsA( AActor::StaticClass() ) );

			if( !Actor->IsPendingKill() )
			{
				SelectedActors.Add( Actor );
			}
		}

		const bool bForceRefresh = true;
		ActorDetails->SetObjects( SelectedActors, bForceRefresh );
	}

	ActorDetails->SetActorDetailsRootCustomization(ActorDetailsObjectFilter, ActorDetailsRootCustomization);
	ActorDetails->SetSCSEditorUICustomization(ActorDetailsSCSEditorUICustomization);

	AllActorDetailPanels.Add( ActorDetails );
	return ActorDetails;
}

TArray<TSharedRef<SActorDetails>> SLevelEditor::GetAllActorDetails() const
{
	TArray<TSharedRef<SActorDetails>> AllValidActorDetails;
	AllValidActorDetails.Reserve(AllActorDetailPanels.Num());

	for (TWeakPtr<SActorDetails> ActorDetails : AllActorDetailPanels)
	{
		if (TSharedPtr<SActorDetails> ActorDetailsPinned = ActorDetails.Pin())
		{
			AllValidActorDetails.Add(ActorDetailsPinned.ToSharedRef());
		}
	}

	if (AllActorDetailPanels.Num() > AllValidActorDetails.Num())
	{
		TArray<TWeakPtr<SActorDetails>>& AllActorDetailPanelsNonConst = const_cast<TArray<TWeakPtr<SActorDetails>>&>(AllActorDetailPanels);
		AllActorDetailPanelsNonConst.Reset(AllValidActorDetails.Num());
		for (const TSharedRef<SActorDetails>& ValidActorDetails : AllValidActorDetails)
		{
			AllActorDetailPanelsNonConst.Add(ValidActorDetails);
		}
	}

	return AllValidActorDetails;
}

void SLevelEditor::SetActorDetailsRootCustomization(TSharedPtr<FDetailsViewObjectFilter> InActorDetailsObjectFilter, TSharedPtr<IDetailRootObjectCustomization> InActorDetailsRootCustomization)
{
	ActorDetailsObjectFilter = InActorDetailsObjectFilter;
	ActorDetailsRootCustomization = InActorDetailsRootCustomization;

	for (TSharedRef<SActorDetails> ActorDetails : GetAllActorDetails())
	{
		ActorDetails->SetActorDetailsRootCustomization(ActorDetailsObjectFilter, ActorDetailsRootCustomization);
	}
}

void SLevelEditor::SetActorDetailsSCSEditorUICustomization(TSharedPtr<ISCSEditorUICustomization> InActorDetailsSCSEditorUICustomization)
{
	ActorDetailsSCSEditorUICustomization = InActorDetailsSCSEditorUICustomization;

	for (TSharedRef<SActorDetails> ActorDetails : GetAllActorDetails())
	{
		ActorDetails->SetSCSEditorUICustomization(ActorDetailsSCSEditorUICustomization);
	}
}

TSharedRef<SWidget> SLevelEditor::CreateToolBox()
{
	TSharedRef<SLevelEditorToolBox> NewToolBox =
		SNew( SLevelEditorToolBox, SharedThis( this ) )
		.IsEnabled( FSlateApplication::Get().GetNormalExecutionAttribute() );

	ToolBoxTabs.Add( NewToolBox );

	return NewToolBox;
}

#undef LOCTEXT_NAMESPACE
