// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUSDStage.h"

#include "SUSDLayersTreeView.h"
#include "SUSDPrimInfo.h"
#include "SUSDStageInfo.h"
#include "SUSDStageTreeView.h"
#include "UnrealUSDWrapper.h"
#include "USDErrorUtils.h"
#include "USDLayerUtils.h"
#include "USDSchemasModule.h"
#include "USDSchemaTranslator.h"
#include "USDStageActor.h"
#include "USDStageEditorSettings.h"
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
#include "Engine/Selection.h"

#define LOCTEXT_NAMESPACE "SUsdStage"

#if USE_USD_SDK

namespace SUSDStageConstants
{
	static const FMargin SectionPadding( 1.f, 4.f, 1.f, 1.f );
}

namespace SUSDStageImpl
{
	void SelectGeneratedComponentsAndActors( AUsdStageActor* StageActor, const TArray<FString>& PrimPaths )
	{
		if ( !StageActor )
		{
			return;
		}

		TSet<USceneComponent*> ComponentsToSelect;
		for ( const FString& PrimPath : PrimPaths )
		{
			if ( USceneComponent* GeneratedComponent = StageActor->GetGeneratedComponent( PrimPath ) )
			{
				ComponentsToSelect.Add( GeneratedComponent );
			}
		}

		TSet<AActor*> ActorsToSelect;
		for ( USceneComponent* ComponentToSelect : ComponentsToSelect )
		{
			if ( AActor* Owner = ComponentToSelect->GetOwner() )
			{
				// We always need the parent actor selected to select a component
				ActorsToSelect.Add( Owner );
			}
		}

		// Don't deselect anything if we're not going to select anything
		if ( ActorsToSelect.Num() == 0 && ComponentsToSelect.Num() == 0 )
		{
			return;
		}

		const bool bSelected = true;
		const bool bNotifySelectionChanged = true;
		const bool bDeselectBSPSurfs = true;
		GEditor->SelectNone( bNotifySelectionChanged, bDeselectBSPSurfs );

		for ( AActor* Actor : ActorsToSelect )
		{
			GEditor->SelectActor( Actor, bSelected, bNotifySelectionChanged );
		}

		for ( USceneComponent* Component : ComponentsToSelect )
		{
			GEditor->SelectComponent( Component, bSelected, bNotifySelectionChanged );
		}
	}
}

void SUsdStage::Construct( const FArguments& InArgs )
{
	OnStageActorPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP( SharedThis( this ), &SUsdStage::OnStageActorPropertyChanged );
	OnActorLoadedHandle = AUsdStageActor::OnActorLoaded.AddSP( SharedThis( this ), &SUsdStage::OnStageActorLoaded );

	OnViewportSelectionChangedHandle = USelection::SelectionChangedEvent.AddRaw( this, &SUsdStage::OnViewportSelectionChanged );

	IUsdStageModule& UsdStageModule = FModuleManager::Get().LoadModuleChecked< IUsdStageModule >( "UsdStage" );
	ViewModel.UsdStageActor = &UsdStageModule.GetUsdStageActor( GWorld );

	bUpdatingViewportSelection = false;

	UE::FUsdStage UsdStage;

	if ( ViewModel.UsdStageActor.IsValid() )
	{
		UsdStage = ViewModel.UsdStageActor->GetOrLoadUsdStage();
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
							.OnPrimSelectionChanged( this, &SUsdStage::OnPrimSelectionChanged )
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
	ClearStageActorDelegates();

	if ( ViewModel.UsdStageActor.IsValid() )
	{
		OnPrimChangedHandle = ViewModel.UsdStageActor->OnPrimChanged.AddLambda(
			[ this ]( const FString& PrimPath, bool bResync )
			{
				if ( this->UsdStageTreeView )
				{
					this->UsdStageTreeView->RefreshPrim( PrimPath, bResync );
					UsdStageTreeView->RequestTreeRefresh();
				}

				const bool bViewingTheUpdatedPrim = SelectedPrimPath.Equals( PrimPath, ESearchCase::IgnoreCase );
				const bool bViewingStageProperties = SelectedPrimPath.IsEmpty() || SelectedPrimPath == TEXT("/");
				const bool bStageUpdated = PrimPath == TEXT("/");

				if ( this->UsdPrimInfoWidget &&
					 ViewModel.UsdStageActor.IsValid() &&
					 ( bViewingTheUpdatedPrim || ( bViewingStageProperties && bStageUpdated ) ) )
				{
					this->UsdPrimInfoWidget->SetPrimPath( ViewModel.UsdStageActor->GetOrLoadUsdStage(), *PrimPath );
				}

				if ( PrimPath == TEXT("/") && this->UsdStageInfoWidget )
				{
					this->UsdStageInfoWidget->RefreshStageInfos( ViewModel.UsdStageActor.Get() );
				}
			}
		);

		// Fired when we switch which is the currently opened stage
		OnStageChangedHandle = ViewModel.UsdStageActor->OnStageChanged.AddLambda(
			[ this ]()
			{
				// So we can reset even if our actor is being destroyed right now
				const bool bEvenIfPendingKill = true;
				if ( ViewModel.UsdStageActor.IsValid( bEvenIfPendingKill ) )
				{
					if ( this->UsdPrimInfoWidget )
					{
						// The cast here forces us to use the const version of GetUsdStage, that won't force-load the stage in case it isn't opened yet
						const UE::FUsdStage& UsdStage = static_cast< const AUsdStageActor* >( ViewModel.UsdStageActor.Get( bEvenIfPendingKill ) )->GetUsdStage();
						this->UsdPrimInfoWidget->SetPrimPath( UsdStage, TEXT("/") );
					}
				}

				this->Refresh();
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
	const bool bEvenIfPendingKill = true;
	if ( AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get( bEvenIfPendingKill ) )
	{
		StageActor->OnStageChanged.Remove( OnStageChangedHandle );
		StageActor->OnPrimChanged.Remove( OnPrimChangedHandle );
		StageActor->OnActorDestroyed.Remove ( OnActorDestroyedHandle );

		StageActor->GetUsdListener().GetOnStageEditTargetChanged().Remove( OnStageEditTargetChangedHandle );
	}
}

SUsdStage::~SUsdStage()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove( OnStageActorPropertyChangedHandle );
	AUsdStageActor::OnActorLoaded.Remove( OnActorLoadedHandle );
	USelection::SelectionChangedEvent.Remove( OnViewportSelectionChangedHandle );

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
			LOCTEXT("New", "New"),
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

		MenuBuilder.AddSubMenu(
			LOCTEXT("PurposesToLoad", "Purposes to load"),
			LOCTEXT("PurposesToLoad_ToolTip", "Only load prims with these specific purposes from the USD stage"),
			FNewMenuDelegate::CreateSP(this, &SUsdStage::FillPurposesToLoadSubMenu),
			false,
			FSlateIcon(),
			false);

		MenuBuilder.AddSubMenu(
			LOCTEXT("RenderContext", "Render Context"),
			LOCTEXT("RenderContext_ToolTip", "Choose which render context to use when parsing materials"),
			FNewMenuDelegate::CreateSP(this, &SUsdStage::FillRenderContextSubMenu));

		MenuBuilder.AddMenuSeparator();

		MenuBuilder.AddSubMenu(
			LOCTEXT( "SelectionText", "Selection" ),
			LOCTEXT( "SelectionText_ToolTip", "How the selection of prims, actors and components should behave" ),
			FNewMenuDelegate::CreateSP( this, &SUsdStage::FillSelectionSubMenu ) );
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

void SUsdStage::FillRenderContextSubMenu( FMenuBuilder& MenuBuilder )
{
	auto AddRenderContextEntry = [&](const FName& RenderContext)
	{
		FText RenderContextName = FText::FromName( RenderContext );
		if ( RenderContext.IsNone() )
		{
			RenderContextName = LOCTEXT("UniversalRenderContext", "Universal");
		}

		MenuBuilder.AddMenuEntry(
			RenderContextName,
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda([this, RenderContext]()
				{
					if ( AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get() )
					{
						FScopedTransaction Transaction(FText::Format(
							LOCTEXT("RenderContextToLoadTransaction", "Change render context to load for USD stage actor '{0}'"),
							FText::FromString(StageActor->GetActorLabel())
						));

						StageActor->Modify();
						StageActor->RenderContext = RenderContext;

						FPropertyChangedEvent PropertyChangedEvent(
							FindFieldChecked< FProperty >( StageActor->GetClass(), GET_MEMBER_NAME_CHECKED( AUsdStageActor, RenderContext ) )
						);
						StageActor->PostEditChangeProperty(PropertyChangedEvent);
					}
				}),
				FCanExecuteAction::CreateLambda([this]()
				{
					return ViewModel.UsdStageActor.Get() != nullptr;
				}),
				FIsActionChecked::CreateLambda([this, RenderContext]()
				{
					if ( AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get() )
					{
						return StageActor->RenderContext == RenderContext;
					}
					return false;
				})
			),
			NAME_None,
			EUserInterfaceActionType::RadioButton
		);
	};

	IUsdSchemasModule& UsdSchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >( TEXT("USDSchemas") );

	for ( const FName& RenderContext : UsdSchemasModule.GetRenderContextRegistry().GetRenderContexts() )
	{
		AddRenderContextEntry( RenderContext );
	}
}

void SUsdStage::FillSelectionSubMenu( FMenuBuilder& MenuBuilder )
{
	MenuBuilder.AddMenuEntry(
		LOCTEXT( "SynchronizeText", "Synchronize with Editor" ),
		FText::GetEmpty(),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]()
			{
				if ( UUsdStageEditorSettings* Settings = GetMutableDefault<UUsdStageEditorSettings>() )
				{
					Settings->bSelectionSynced = !Settings->bSelectionSynced;
					Settings->SaveConfig();
				}

				// Immediately sync the selection, but only if the results are obvious (i.e. either our prim selection or viewport selection are empty)
				if ( GEditor )
				{
					int32 NumPrimsSelected = UsdStageTreeView->GetNumItemsSelected();
					int32 NumViewportSelected = FMath::Max( GEditor->GetSelectedComponentCount(), GEditor->GetSelectedActorCount() );

					if ( NumPrimsSelected == 0 )
					{
						USelection* Selection = GEditor->GetSelectedComponentCount() > 0
							? GEditor->GetSelectedComponents()
							: GEditor->GetSelectedActors();

						OnViewportSelectionChanged(Selection);
					}
					else if ( NumViewportSelected == 0 )
					{
						TGuardValue<bool> SelectionLoopGuard( bUpdatingViewportSelection, true );

						SUSDStageImpl::SelectGeneratedComponentsAndActors( ViewModel.UsdStageActor.Get(), UsdStageTreeView->GetSelectedPrims() );
					}
				}
			}),
			FCanExecuteAction::CreateLambda([]()
			{
				return true;
			}),
			FIsActionChecked::CreateLambda([this]()
			{
				const UUsdStageEditorSettings* Settings = GetDefault<UUsdStageEditorSettings>();
				return Settings && Settings->bSelectionSynced;
			})
		),
		NAME_None,
		EUserInterfaceActionType::Check
	);
}

void SUsdStage::OnNew()
{
	ViewModel.NewStage( nullptr );
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
	UE::FUsdStage UsdStage;
	if ( ViewModel.UsdStageActor.IsValid() )
	{
		UsdStage = ViewModel.UsdStageActor->GetOrLoadUsdStage();
	}

	if ( UsdStage )
	{
		if ( UE::FSdfLayer RootLayer = UsdStage.GetRootLayer() )
		{
			FString RealPath = RootLayer.GetRealPath();
			if ( FPaths::FileExists( RealPath ) )
			{
				ViewModel.SaveStage();
			}
			else
			{
				TOptional< FString > UsdFilePath = UsdUtils::BrowseUsdFile( UsdUtils::EBrowseFileMode::Save, AsShared() );
				if ( UsdFilePath )
				{
					ViewModel.SaveStageAs( *UsdFilePath.GetValue() );
				}
			}
		}
	}
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

void SUsdStage::OnPrimSelectionChanged( const TArray<FString>& PrimPaths )
{
	AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get();
	if ( !StageActor )
	{
		return;
	}

	if ( UsdPrimInfoWidget )
	{
		UE::FUsdStage UsdStage = StageActor->GetOrLoadUsdStage();

		SelectedPrimPath = PrimPaths.Num() == 1 ? PrimPaths[ 0 ] : TEXT( "" );
		UsdPrimInfoWidget->SetPrimPath( UsdStage, *SelectedPrimPath );
	}

	const UUsdStageEditorSettings* Settings = GetDefault<UUsdStageEditorSettings>();
	if ( Settings && Settings->bSelectionSynced && GEditor )
	{
		TGuardValue<bool> SelectionLoopGuard( bUpdatingViewportSelection, true );

		SUSDStageImpl::SelectGeneratedComponentsAndActors( StageActor, PrimPaths );
	}
}

void SUsdStage::OpenStage( const TCHAR* FilePath )
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
	ViewModel.UsdStageActor->BlockMonitoringLevelSequenceForThisTransaction();

	ViewModel.OpenStage( FilePath );
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

void SUsdStage::OnViewportSelectionChanged( UObject* NewSelection )
{
	// This may be called when first opening a project, before the our widgets are fully initialized
	if ( !UsdStageTreeView )
	{
		return;
	}

	const UUsdStageEditorSettings* Settings = GetDefault<UUsdStageEditorSettings>();
	if ( !Settings || !Settings->bSelectionSynced || bUpdatingViewportSelection )
	{
		return;
	}

	AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get();
	USelection* Selection = Cast<USelection>( NewSelection );
	if ( !Selection || !StageActor )
	{
		return;
	}

	TArray<USceneComponent*> SelectedComponents;
	{
		Selection->GetSelectedObjects<USceneComponent>( SelectedComponents );

		TArray<AActor*> SelectedActors;
		Selection->GetSelectedObjects<AActor>( SelectedActors );

		for ( AActor* SelectedActor : SelectedActors )
		{
			if ( SelectedActor )
			{
				SelectedComponents.Add( SelectedActor->GetRootComponent() );
			}
		}
	}

	TArray<FString> PrimPaths;
	for ( USceneComponent* Component : SelectedComponents )
	{
		FString FoundPrimPath = StageActor->GetSourcePrimPath( Component );
		if ( !FoundPrimPath.IsEmpty() )
		{
			PrimPaths.Add( FoundPrimPath );
		}
	}

	if ( PrimPaths.Num() > 0 )
	{
		UsdStageTreeView->SelectPrims( PrimPaths );
	}
}

#endif // #if USE_USD_SDK

#undef LOCTEXT_NAMESPACE
