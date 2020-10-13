// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUSDStage.h"

#include "SUSDLayersTreeView.h"
#include "SUSDPrimInfo.h"
#include "SUSDStageInfo.h"
#include "SUSDStageTreeView.h"
#include "UnrealUSDWrapper.h"
#include "USDErrorUtils.h"
#include "USDLayerUtils.h"
#include "USDStageActor.h"
#include "USDStageImportContext.h"
#include "USDStageImporter.h"
#include "USDStageImporterModule.h"
#include "USDStageImportOptions.h"
#include "USDStageModule.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/UsdStage.h"

#include "Dialogs/DlgPickPath.h"
#include "EditorStyleSet.h"
#include "Engine/World.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Layout/SSplitter.h"

#define LOCTEXT_NAMESPACE "SUsdStage"

#if USE_USD_SDK

namespace SUSDStageConstants
{
	static const FMargin SectionPadding( 1.f, 4.f, 1.f, 1.f );
}

void SUsdStage::Construct( const FArguments& InArgs )
{
	OnStageActorPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP( SharedThis( this ), &SUsdStage::OnStageActorPropertyChanged );
	OnActorLoadedHandle = AUsdStageActor::OnActorLoaded.AddSP( SharedThis( this ), &SUsdStage::OnStageActorLoaded );

	IUsdStageModule& UsdStageModule = FModuleManager::Get().LoadModuleChecked< IUsdStageModule >( "UsdStage" );
	ViewModel.UsdStageActor = &UsdStageModule.GetUsdStageActor( GWorld );

	UE::FUsdStage UsdStage;

	if ( ViewModel.UsdStageActor.IsValid() )
	{
		UsdStage = ViewModel.UsdStageActor->GetUsdStage();
	}

	ChildSlot
	[
		SNew( SBorder )
		.BorderImage( FEditorStyle::GetBrush("Docking.Tab.ContentAreaBrush") )
		[
			SNew( SVerticalBox )

			// Menu
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				MakeMainMenu()
			]

			// Stage Info
			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding( SUSDStageConstants::SectionPadding )
			[
				SNew(SBorder)
				.BorderImage( FEditorStyle::GetBrush(TEXT("ToolPanel.GroupBorder")) )
				[
					SAssignNew( UsdStageInfoWidget, SUsdStageInfo, ViewModel.UsdStageActor.Get() )
				]
			]

			+SVerticalBox::Slot()
			.FillHeight(1.f)
			.Padding( SUSDStageConstants::SectionPadding )
			[
				SNew( SSplitter )
				.Orientation( Orient_Vertical )

				+SSplitter::Slot()
				.Value( 0.7f )
				[
					SNew( SSplitter )
					.Orientation( Orient_Horizontal )

					// Stage tree
					+SSplitter::Slot()
					[
						SNew( SBorder )
						.BorderImage( FEditorStyle::GetBrush(TEXT("ToolPanel.GroupBorder")) )
						[
							SAssignNew( UsdStageTreeView, SUsdStageTreeView, ViewModel.UsdStageActor.Get() )
							.OnPrimSelected( this, &SUsdStage::OnPrimSelected )
						]
					]

					// Prim details
					+SSplitter::Slot()
					[
						SNew( SBorder )
						.BorderImage( FEditorStyle::GetBrush("ToolPanel.GroupBorder") )
						[
							SAssignNew( UsdPrimInfoWidget, SUsdPrimInfo, UsdStage, TEXT("/") )
						]
					]
				]

				// Layers tree
				+SSplitter::Slot()
				.Value( 0.3f )
				[
					SNew(SBorder)
					.BorderImage( FEditorStyle::GetBrush(TEXT("ToolPanel.GroupBorder")) )
					[
						SAssignNew( UsdLayersTreeView, SUsdLayersTreeView, ViewModel.UsdStageActor.Get() )
					]
				]
			]
		]
	];

	SetupStageActorDelegates();
}

void SUsdStage::SetupStageActorDelegates()
{
	if ( ViewModel.UsdStageActor.IsValid() )
	{
		ClearStageActorDelegates();

		OnPrimChangedHandle = ViewModel.UsdStageActor->OnPrimChanged.AddLambda(
			[ this ]( const FString& PrimPath, bool bResync )
			{
				if ( this->UsdStageTreeView )
				{
					this->UsdStageTreeView->RefreshPrim( PrimPath, bResync );
				}

				if ( this->UsdPrimInfoWidget && ViewModel.UsdStageActor.IsValid() && SelectedPrimPath.Equals( PrimPath, ESearchCase::IgnoreCase ) )
				{
					this->UsdPrimInfoWidget->SetPrimPath( ViewModel.UsdStageActor->GetUsdStage(), *PrimPath );
				}
			}
		);

		// Fired when we switch which is the currently opened stage
		OnStageChangedHandle = ViewModel.UsdStageActor->OnStageChanged.AddLambda(
			[ this ]()
			{
				if ( ViewModel.UsdStageActor.IsValid() )
				{
					if ( this->UsdPrimInfoWidget )
					{
						this->UsdPrimInfoWidget->SetPrimPath( ViewModel.UsdStageActor->GetUsdStage(), TEXT("/") );
					}
				}

				this->Refresh();
			}
		);

		// Fired when the currently opened stage changes its info (e.g. startTimeSeconds, framesPerSecond, etc.)
		OnStageInfoChangedHandle = ViewModel.UsdStageActor->GetUsdListener().GetOnStageInfoChanged().AddLambda(
			[ this ]( const TArray< FString >& ChangedFields )
			{
				if ( this->UsdStageInfoWidget )
				{
					this->UsdStageInfoWidget->RefreshStageInfos( ViewModel.UsdStageActor.Get() );
				}
			}
		);

		OnActorDestroyedHandle = ViewModel.UsdStageActor->OnActorDestroyed.AddLambda(
			[ this ]()
			{
				ClearStageActorDelegates();
				this->ViewModel.CloseStage();

				this->Refresh();
			}
		);

		OnStageEditTargetChangedHandle = ViewModel.UsdStageActor->GetUsdListener().GetOnStageEditTargetChanged().AddLambda(
			[ this ]()
			{
				if ( this->UsdLayersTreeView && ViewModel.UsdStageActor.IsValid() )
				{
					this->UsdLayersTreeView->Refresh( ViewModel.UsdStageActor.Get(), false );
				}
			}
		);
	}
}

void SUsdStage::ClearStageActorDelegates()
{
	if ( ViewModel.UsdStageActor.IsValid() )
	{
		ViewModel.UsdStageActor->OnStageChanged.Remove( OnStageChangedHandle );
		ViewModel.UsdStageActor->OnPrimChanged.Remove( OnPrimChangedHandle );
		ViewModel.UsdStageActor->OnActorDestroyed.Remove ( OnActorDestroyedHandle );

		ViewModel.UsdStageActor->GetUsdListener().GetOnStageEditTargetChanged().Remove( OnStageEditTargetChangedHandle );
		ViewModel.UsdStageActor->GetUsdListener().GetOnStageInfoChanged().Remove( OnStageInfoChangedHandle );
	}
}

SUsdStage::~SUsdStage()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove( OnStageActorPropertyChangedHandle );
	AUsdStageActor::OnActorLoaded.Remove( OnActorLoadedHandle );

	ClearStageActorDelegates();
}

TSharedRef< SWidget > SUsdStage::MakeMainMenu()
{
	FMenuBarBuilder MenuBuilder( nullptr );
	{
		// File
		MenuBuilder.AddPullDownMenu(
			LOCTEXT( "FileMenu", "File" ),
			LOCTEXT( "FileMenu_ToolTip", "Opens the file menu" ),
			FNewMenuDelegate::CreateSP( this, &SUsdStage::FillFileMenu ) );

		// Actions
		MenuBuilder.AddPullDownMenu(
			LOCTEXT( "ActionsMenu", "Actions" ),
			LOCTEXT( "ActionsMenu_ToolTip", "Opens the actions menu" ),
			FNewMenuDelegate::CreateSP( this, &SUsdStage::FillActionsMenu ) );

		// Options
		MenuBuilder.AddPullDownMenu(
			LOCTEXT( "OptionsMenu", "Options" ),
			LOCTEXT( "OptionsMenu_ToolTip", "Opens the options menu" ),
			FNewMenuDelegate::CreateSP( this, &SUsdStage::FillOptionsMenu ) );
	}

	// Create the menu bar
	TSharedRef< SWidget > MenuBarWidget = MenuBuilder.MakeWidget();
	MenuBarWidget->SetVisibility( EVisibility::Visible ); // Work around for menu bar not showing on Mac

	return MenuBarWidget;
}

void SUsdStage::FillFileMenu( FMenuBuilder& MenuBuilder )
{
	MenuBuilder.BeginSection( "File", LOCTEXT("File", "File") );
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("New", "New..."),
			LOCTEXT("New_ToolTip", "Creates a new layer and opens the stage with it at its root"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SUsdStage::OnNew ),
				FCanExecuteAction()
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("Open", "Open..."),
			LOCTEXT("Open_ToolTip", "Opens a USD file"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SUsdStage::OnOpen ),
				FCanExecuteAction()
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("Save", "Save"),
			LOCTEXT("Save_ToolTip", "Saves the stage"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SUsdStage::OnSave ),
				FCanExecuteAction()
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("Reload", "Reload"),
			LOCTEXT("Reload_ToolTip", "Reloads the stage from disk"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SUsdStage::OnReloadStage ),
				FCanExecuteAction()
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT("Close", "Close"),
			LOCTEXT("Close_ToolTip", "Closes the opened stage"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SUsdStage::OnClose ),
				FCanExecuteAction()
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);
	}
	MenuBuilder.EndSection();
}

void SUsdStage::FillActionsMenu( FMenuBuilder& MenuBuilder )
{
	MenuBuilder.BeginSection( "Actions", LOCTEXT("Actions", "Actions") );
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("Import", "Import..."),
			LOCTEXT("Import_ToolTip", "Imports the stage as Unreal assets"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SUsdStage::OnImport ),
				FCanExecuteAction()
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);
	}
	MenuBuilder.EndSection();
}

void SUsdStage::FillOptionsMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection( "Options", LOCTEXT("Options", "Options") );
	{
		MenuBuilder.AddSubMenu(
			LOCTEXT("Payloads", "Payloads"),
			LOCTEXT("Payloads_ToolTip", "What to do with payloads when initially opening the stage"),
			FNewMenuDelegate::CreateSP(this, &SUsdStage::FillPayloadsSubMenu));

		MenuBuilder.AddMenuSeparator();

		MenuBuilder.AddSubMenu(
			LOCTEXT("PurposesToLoad", "Purposes to load"),
			LOCTEXT("PurposesToLoad_ToolTip", "Only load prims with these specific purposes from the USD stage"),
			FNewMenuDelegate::CreateSP(this, &SUsdStage::FillPurposesToLoadSubMenu),
			false,
			FSlateIcon(),
			false);
	}
	MenuBuilder.EndSection();
}

void SUsdStage::FillPayloadsSubMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		LOCTEXT("LoadAll", "Load all"),
		LOCTEXT("LoadAll_ToolTip", "Loads all payloads initially"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]()
			{
				if(AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get())
				{
					FScopedTransaction Transaction(FText::Format(
						LOCTEXT("SetLoadAllTransaction", "Set USD stage actor '{0}' actor to load all payloads initially"),
						FText::FromString(StageActor->GetActorLabel())
					));

					StageActor->Modify();
					StageActor->InitialLoadSet = EUsdInitialLoadSet::LoadAll;
				}
			}),
			FCanExecuteAction::CreateLambda([this]()
			{
				return ViewModel.UsdStageActor.Get() != nullptr;
			}),
			FIsActionChecked::CreateLambda([this]()
			{
				if(AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get())
				{
					return StageActor->InitialLoadSet == EUsdInitialLoadSet::LoadAll;
				}
				return false;
			})
		),
		NAME_None,
		EUserInterfaceActionType::RadioButton
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("LoadNone", "Load none"),
		LOCTEXT("LoadNone_ToolTip", "Don't load any payload initially"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]()
			{
				if(AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get())
				{
					FScopedTransaction Transaction(FText::Format(
						LOCTEXT("SetLoadNoneTransaction", "Set USD stage actor '{0}' actor to load no payloads initially"),
						FText::FromString(StageActor->GetActorLabel())
					));

					StageActor->Modify();
					StageActor->InitialLoadSet = EUsdInitialLoadSet::LoadNone;
				}
			}),
			FCanExecuteAction::CreateLambda([this]()
			{
				return ViewModel.UsdStageActor.Get() != nullptr;
			}),
			FIsActionChecked::CreateLambda([this]()
			{
				if(AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get())
				{
					return StageActor->InitialLoadSet == EUsdInitialLoadSet::LoadNone;
				}
				return false;
			})
		),
		NAME_None,
		EUserInterfaceActionType::RadioButton
	);
}

void SUsdStage::FillPurposesToLoadSubMenu(FMenuBuilder& MenuBuilder)
{
	auto AddPurposeEntry = [&](const EUsdPurpose& Purpose, const FText& Text)
	{
		MenuBuilder.AddMenuEntry(
			Text,
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, Purpose]()
				{
					if(AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get())
					{
						FScopedTransaction Transaction(FText::Format(
							LOCTEXT("PurposesToLoadTransaction", "Change purposes to load for USD stage actor '{0}'"),
							FText::FromString(StageActor->GetActorLabel())
						));

						StageActor->Modify();
						StageActor->PurposesToLoad = (int32)((EUsdPurpose)StageActor->PurposesToLoad ^ Purpose);

						FPropertyChangedEvent PropertyChangedEvent(
							FindFieldChecked< FProperty >( StageActor->GetClass(), GET_MEMBER_NAME_CHECKED( AUsdStageActor, PurposesToLoad ) )
						);
						StageActor->PostEditChangeProperty(PropertyChangedEvent);
					}
				}),
				FCanExecuteAction::CreateLambda([this]()
				{
					return ViewModel.UsdStageActor.Get() != nullptr;
				}),
				FIsActionChecked::CreateLambda([this, Purpose]()
				{
					if(AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get())
					{
						return EnumHasAllFlags((EUsdPurpose)StageActor->PurposesToLoad, Purpose);
					}
					return false;
				})
			),
			NAME_None,
			EUserInterfaceActionType::Check
		);
	};

	AddPurposeEntry(EUsdPurpose::Proxy,  LOCTEXT("ProxyPurpose",  "Proxy"));
	AddPurposeEntry(EUsdPurpose::Render, LOCTEXT("RenderPurpose", "Render"));
	AddPurposeEntry(EUsdPurpose::Guide,  LOCTEXT("GuidePurpose",  "Guide"));
}

void SUsdStage::OnNew()
{
	TOptional< FString > UsdFilePath = UsdUtils::BrowseUsdFile( UsdUtils::EBrowseFileMode::Save, AsShared() );

	if ( UsdFilePath )
	{
		ViewModel.NewStage( *UsdFilePath.GetValue() );
	}
}

void SUsdStage::OnOpen()
{
	TOptional< FString > UsdFilePath = UsdUtils::BrowseUsdFile( UsdUtils::EBrowseFileMode::Open, AsShared() );

	if ( UsdFilePath )
	{
		OpenStage( *UsdFilePath.GetValue() );
	}
}

void SUsdStage::OnSave()
{
	ViewModel.SaveStage();
}

void SUsdStage::OnReloadStage()
{
	FScopedTransaction Transaction( LOCTEXT( "ReloadTransaction", "Reload USD stage" ) );

	ViewModel.ReloadStage();

	if ( UsdLayersTreeView )
	{
		UsdLayersTreeView->Refresh( ViewModel.UsdStageActor.Get(), true );
	}
}

void SUsdStage::OnClose()
{
	FScopedTransaction Transaction(LOCTEXT("CloseTransaction", "Close USD stage"));

	ViewModel.CloseStage();
	Refresh();
}

void SUsdStage::OnImport()
{
	ViewModel.ImportStage();
}

void SUsdStage::OnPrimSelected( FString PrimPath )
{
	if ( UsdPrimInfoWidget )
	{
		UE::FUsdStage UsdStage;

		if ( ViewModel.UsdStageActor.IsValid() )
		{
			UsdStage = ViewModel.UsdStageActor->GetUsdStage();
		}

		SelectedPrimPath = PrimPath;
		UsdPrimInfoWidget->SetPrimPath( UsdStage, *PrimPath );
	}
}

void SUsdStage::OpenStage( const TCHAR* FilePath )
{
	// This scope is important so that we can resume monitoring the level sequence after our scoped transaction is finished
	{
		// Create the transaction before calling UsdStageModule.GetUsdStageActor as that may create the actor, and we want
		// the actor spawning to be part of the transaction
		FScopedTransaction Transaction( FText::Format(
			LOCTEXT( "OpenStageTransaction", "Open USD stage '{0}'" ),
			FText::FromString( FilePath )
		) );

		if ( !ViewModel.UsdStageActor.IsValid() )
		{
			IUsdStageModule& UsdStageModule = FModuleManager::Get().LoadModuleChecked< IUsdStageModule >( "UsdStage" );
			ViewModel.UsdStageActor = &UsdStageModule.GetUsdStageActor( GWorld );

			SetupStageActorDelegates();
		}

		// Block writing level sequence changes back to the USD stage until we finished this transaction, because once we do
		// the movie scene and tracks will all trigger OnObjectTransacted. We listen for those on FUsdLevelSequenceHelperImpl::OnObjectTransacted,
		// and would otherwise end up writing all of the data we just loaded back to the USD stage
		ViewModel.UsdStageActor->StopMonitoringLevelSequence();

		ViewModel.OpenStage( FilePath );
	}

	if ( AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get() )
	{
		StageActor->ResumeMonitoringLevelSequence();
	}
}

void SUsdStage::Refresh()
{
	// May be nullptr, but that is ok. Its how the widgets are reset
	AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get();

	if (UsdLayersTreeView)
	{
		UsdLayersTreeView->Refresh( StageActor, true );
	}

	if (UsdStageInfoWidget)
	{
		UsdStageInfoWidget->RefreshStageInfos( StageActor );
	}

	if (UsdStageTreeView)
	{
		UsdStageTreeView->Refresh( StageActor );
		UsdStageTreeView->RequestTreeRefresh();
	}
}

void SUsdStage::OnStageActorLoaded( AUsdStageActor* InUsdStageActor )
{
	if ( ViewModel.UsdStageActor == InUsdStageActor )
	{
		return;
	}

	ClearStageActorDelegates();
	ViewModel.UsdStageActor = InUsdStageActor;
	SetupStageActorDelegates();

	// Refresh here because we may be receiving an actor that has a stage already loaded,
	// like during undo/redo
	Refresh();
}

void SUsdStage::OnStageActorPropertyChanged( UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent )
{
	if ( ObjectBeingModified == ViewModel.UsdStageActor )
	{
		if ( UsdStageInfoWidget )
		{
			UsdStageInfoWidget->RefreshStageInfos( ViewModel.UsdStageActor.Get() );
		}
	}
}

#endif // #if USE_USD_SDK

#undef LOCTEXT_NAMESPACE
