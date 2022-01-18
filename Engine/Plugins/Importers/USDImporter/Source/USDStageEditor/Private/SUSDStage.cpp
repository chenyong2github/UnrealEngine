// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUSDStage.h"

#include "SUSDLayersTreeView.h"
#include "SUSDPrimInfo.h"
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

#include "ActorTreeItem.h"
#include "Async/Async.h"
#include "Dialogs/DlgPickPath.h"
#include "EditorStyleSet.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "SceneOutlinerModule.h"
#include "ScopedTransaction.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SSplitter.h"

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
		for ( TSet<USceneComponent*>::TIterator It(ComponentsToSelect); It; ++It )
		{
			if ( AActor* Owner = (*It)->GetOwner() )
			{
				// We always need the parent actor selected to select a component
				ActorsToSelect.Add( Owner );

				// If we're going to select a root component, select the actor instead. This is useful
				// because if we later press "F" to focus on the prim, having the actor selected will use the entire
				// actor's bounding box and focus on something. If all we have selected is an empty scene component
				// however, the camera won't focus on anything
				if ( *It == Owner->GetRootComponent() )
				{
					It.RemoveCurrent();
				}
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
	OnActorLoadedHandle = AUsdStageActor::OnActorLoaded.AddSP( SharedThis( this ), &SUsdStage::OnStageActorLoaded );

	OnViewportSelectionChangedHandle = USelection::SelectionChangedEvent.AddRaw( this, &SUsdStage::OnViewportSelectionChanged );

	IUsdStageModule& UsdStageModule = FModuleManager::Get().LoadModuleChecked< IUsdStageModule >( "UsdStage" );
	ViewModel.UsdStageActor = UsdStageModule.FindUsdStageActor( GWorld );

	bUpdatingViewportSelection = false;
	bUpdatingPrimSelection = false;

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
				SNew( SHorizontalBox )
				+ SHorizontalBox::Slot()
				.FillWidth( 1 )
				[
					MakeMainMenu()
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew( SBox )
					.HAlign( HAlign_Right )
					.VAlign( VAlign_Fill )
					[
						MakeActorPickerMenu()
					]
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
				// The USD notices may come from a background USD TBB thread, but we should only update slate from the main/slate threads.
				// We can't retrieve the FSlateApplication singleton here (because that can also only be used from the main/slate threads),
				// so we must use Async or core tickers here
				AsyncTask( ENamedThreads::GameThread, [this, PrimPath, bResync]()
				{
					if ( this->UsdStageTreeView )
					{
						this->UsdStageTreeView->RefreshPrim( PrimPath, bResync );
						this->UsdStageTreeView->RequestTreeRefresh();
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
				});
			}
		);

		// Fired when we switch which is the currently opened stage
		OnStageChangedHandle = ViewModel.UsdStageActor->OnStageChanged.AddLambda(
			[ this ]()
			{
				AsyncTask(ENamedThreads::GameThread, [this]()
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
				});
			}
		);

		OnActorDestroyedHandle = ViewModel.UsdStageActor->OnActorDestroyed.AddLambda(
			[ this ]()
			{
				// Refresh widgets on game thread, but close the stage right away. In some contexts this is important, for example when
				// running a Python script: If our USD Stage Editor is open and our script deletes an actor, it will trigger OnActorDestroyed.
				// If we had this CloseStage() call inside the AsyncTask, it would only take place when the script has finished running
				// (and control flow returned to the game thread) which can lead to some weird results (and break automated tests).
				// We could get around this on the Python script's side by just yielding, but there may be other scenarios
				ClearStageActorDelegates();
				this->ViewModel.CloseStage();

				AsyncTask( ENamedThreads::GameThread, [this]()
				{
					this->Refresh();
				});
			}
		);

		OnStageEditTargetChangedHandle = ViewModel.UsdStageActor->GetUsdListener().GetOnStageEditTargetChanged().AddLambda(
			[ this ]()
			{
				AsyncTask( ENamedThreads::GameThread, [this]()
				{
					if ( this->UsdLayersTreeView && ViewModel.UsdStageActor.IsValid() )
					{
						constexpr bool bResync = false;
						this->UsdLayersTreeView->Refresh( ViewModel.UsdStageActor.Get(), bResync );
					}
				});
			}
		);

		OnLayersChangedHandle = ViewModel.UsdStageActor->GetUsdListener().GetOnLayersChanged().AddLambda(
			[ this ]( const TArray< FString >& LayersNames )
			{
				AsyncTask( ENamedThreads::GameThread, [this]()
				{
					if ( this->UsdLayersTreeView && ViewModel.UsdStageActor.IsValid() )
					{
						constexpr bool bResync = false;
						this->UsdLayersTreeView->Refresh( ViewModel.UsdStageActor.Get(), bResync );
					}
				});
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
		StageActor->GetUsdListener().GetOnLayersChanged().Remove( OnLayersChangedHandle );
	}
}

SUsdStage::~SUsdStage()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove( OnStageActorPropertyChangedHandle );
	AUsdStageActor::OnActorLoaded.Remove( OnActorLoadedHandle );
	USelection::SelectionChangedEvent.Remove( OnViewportSelectionChangedHandle );

	ClearStageActorDelegates();

	ActorPickerMenu.Reset();
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

TSharedRef< SWidget > SUsdStage::MakeActorPickerMenu()
{
	return SNew( SComboButton )
		.ComboButtonStyle( FAppStyle::Get(), "SimpleComboButton" )
		.OnGetMenuContent( this, &SUsdStage::MakeActorPickerMenuContent )
		.MenuPlacement( EMenuPlacement::MenuPlacement_BelowRightAnchor )
		.ToolTipText( LOCTEXT( "ActorPicker_ToolTip", "Switch the active stage actor" ) )
		.ButtonContent()
		[
			SNew( STextBlock )
			.Text_Lambda( [this]()
			{
				if ( AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get() )
				{
					return FText::FromString( StageActor->GetActorLabel() );
				}

				return FText::FromString( "None" );
			})
		];
}

TSharedRef< SWidget > SUsdStage::MakeActorPickerMenuContent()
{
	if ( !ActorPickerMenu.IsValid() )
	{
		FSceneOutlinerInitializationOptions InitOptions;
		InitOptions.bShowHeaderRow = false;
		InitOptions.bShowSearchBox = true;
		InitOptions.bShowCreateNewFolder = false;
		InitOptions.bFocusSearchBoxWhenOpened = true;
		InitOptions.ColumnMap.Add( FSceneOutlinerBuiltInColumnTypes::Label(), FSceneOutlinerColumnInfo( ESceneOutlinerColumnVisibility::Visible, 0 ) );
		InitOptions.Filters->AddFilterPredicate<FActorTreeItem>(
			FActorTreeItem::FFilterPredicate::CreateLambda(
				[]( const AActor* Actor )
				{
					return Actor && Actor->IsA<AUsdStageActor>();
				}
			)
		);

		FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::LoadModuleChecked<FSceneOutlinerModule>( "SceneOutliner" );
		ActorPickerMenu = SceneOutlinerModule.CreateActorPicker(
			InitOptions,
			FOnActorPicked::CreateLambda(
				[this]( AActor* Actor )
				{
					if ( Actor && Actor->IsA<AUsdStageActor>() )
					{
						this->SetActor( Cast<AUsdStageActor>( Actor ) );

						FSlateApplication::Get().DismissAllMenus();
					}
				}
			)
		);
	}

	if ( ActorPickerMenu.IsValid() )
	{
		ActorPickerMenu->FullRefresh();

		return SNew(SBox)
			.Padding( FMargin( 1 ) )    // Add a small margin or else we'll get dark gray on dark gray which can look a bit confusing
			.MinDesiredWidth( 300.0f )  // Force a min width or else the tree view item text will run up right to the very edge pixel of the menu
			.HAlign( HAlign_Fill )
			[
				ActorPickerMenu.ToSharedRef()
			];
	}

	return SNullWidget::NullWidget;
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
				FCanExecuteAction::CreateLambda( [this]()
				{
					if ( const AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get() )
					{
						if ( UE::FUsdStage Stage = StageActor->GetUsdStage() )
						{
							return true;
						}
					}

					return false;
				})
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);

		MenuBuilder.AddSeparator();

		MenuBuilder.AddMenuEntry(
			LOCTEXT("Reload", "Reload"),
			LOCTEXT("Reload_ToolTip", "Reloads the stage from disk, keeping aspects of the session intact"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SUsdStage::OnReloadStage ),
				FCanExecuteAction::CreateLambda( [this]()
				{
					if ( const AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get() )
					{
						if ( UE::FUsdStage Stage = StageActor->GetUsdStage() )
						{
							if ( !Stage.GetRootLayer().IsAnonymous() )
							{
								return true;
							}
						}
					}

					return false;
				})
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);

		MenuBuilder.AddMenuEntry(
			LOCTEXT( "ResetState", "Reset state" ),
			LOCTEXT( "ResetState_ToolTip", "Resets the session layer and other options like edit target and muted layers" ),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SUsdStage::OnResetStage ),
				FCanExecuteAction::CreateLambda( [this]()
				{
					if ( const AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get() )
					{
						if ( UE::FUsdStage Stage = StageActor->GetUsdStage() )
						{
							return true;
						}
					}

					return false;
				})
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);

		MenuBuilder.AddSeparator();

		MenuBuilder.AddMenuEntry(
			LOCTEXT("Close", "Close"),
			LOCTEXT("Close_ToolTip", "Closes the opened stage"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP( this, &SUsdStage::OnClose ),
				FCanExecuteAction::CreateLambda( [this]()
				{
					if ( const AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get() )
					{
						if ( UE::FUsdStage Stage = StageActor->GetUsdStage() )
						{
							return true;
						}
					}

					return false;
				})
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
				FCanExecuteAction::CreateLambda( [this]()
				{
					if ( const AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get() )
					{
						if ( UE::FUsdStage Stage = StageActor->GetUsdStage() )
						{
							return true;
						}
					}

					return false;
				})
			),
			NAME_None,
			EUserInterfaceActionType::Button
		);
	}
	MenuBuilder.EndSection();
}

void SUsdStage::FillOptionsMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection( TEXT( "StageOptions" ), LOCTEXT( "StageOptions", "Stage options" ) );
	{
		MenuBuilder.AddSubMenu(
			LOCTEXT("Payloads", "Payloads"),
			LOCTEXT("Payloads_ToolTip", "What to do with payloads when initially opening the stage"),
			FNewMenuDelegate::CreateSP(this, &SUsdStage::FillPayloadsSubMenu));

		MenuBuilder.AddSubMenu(
			LOCTEXT("PurposesToLoad", "Purposes to load"),
			LOCTEXT("PurposesToLoad_ToolTip", "Only load prims with these specific purposes from the USD stage"),
			FNewMenuDelegate::CreateSP(this, &SUsdStage::FillPurposesToLoadSubMenu));

		MenuBuilder.AddSubMenu(
			LOCTEXT("RenderContext", "Render Context"),
			LOCTEXT("RenderContext_ToolTip", "Choose which render context to use when parsing materials"),
			FNewMenuDelegate::CreateSP(this, &SUsdStage::FillRenderContextSubMenu));

		MenuBuilder.AddSubMenu(
			LOCTEXT( "Collapsing", "Collapsing" ),
			LOCTEXT( "Collapsing_ToolTip", "Whether to try to combine individual assets and components of the same type on a Kind-per-Kind basis, like multiple Mesh prims into a single Static Mesh" ),
			FNewMenuDelegate::CreateSP( this, &SUsdStage::FillCollapsingSubMenu ) );

		MenuBuilder.AddSubMenu(
			LOCTEXT( "InterpolationType", "Interpolation type" ),
			LOCTEXT( "InterpolationType_ToolTip", "Whether to interpolate between time samples linearly or with 'held' (i.e. constant) interpolation" ),
			FNewMenuDelegate::CreateSP( this, &SUsdStage::FillInterpolationTypeSubMenu ) );
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection( TEXT( "EditorOptions" ), LOCTEXT( "EditorOptions", "Editor options" ) );
	{
		MenuBuilder.AddSubMenu(
			LOCTEXT( "SelectionText", "Selection" ),
			LOCTEXT( "SelectionText_ToolTip", "How the selection of prims, actors and components should behave" ),
			FNewMenuDelegate::CreateSP( this, &SUsdStage::FillSelectionSubMenu ) );

		MenuBuilder.AddSubMenu(
			LOCTEXT( "NaniteSettings", "Nanite" ),
			LOCTEXT( "NaniteSettings_ToolTip", "Configure how to use Nanite for generated assets" ),
			FNewMenuDelegate::CreateSP( this, &SUsdStage::FillNaniteThresholdSubMenu ),
			false,
			FSlateIcon(),
			false );
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
			EUserInterfaceActionType::ToggleButton
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
			RenderContextName = LOCTEXT("UniversalRenderContext", "universal");
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

void SUsdStage::FillCollapsingSubMenu( FMenuBuilder& MenuBuilder )
{
	auto AddKindToCollapseEntry = [&]( const EUsdDefaultKind Kind, const FText& Text, FCanExecuteAction CanExecuteAction )
	{
		MenuBuilder.AddMenuEntry(
			Text,
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateLambda( [this, Text, Kind]()
				{
					if ( AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get() )
					{
						FScopedTransaction Transaction( FText::Format(
							LOCTEXT( "ToggleCollapsingTransaction", "Toggle asset and component collapsing for kind '{0}' on USD stage actor '{1}'" ),
							Text,
							FText::FromString( StageActor->GetActorLabel() )
						) );

						int32 NewKindsToCollapse = ( int32 ) ( ( EUsdDefaultKind ) StageActor->KindsToCollapse ^ Kind );
						StageActor->SetKindsToCollapse( NewKindsToCollapse );
					}
				}),
				CanExecuteAction,
				FIsActionChecked::CreateLambda( [this, Kind]()
				{
					if ( AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get() )
					{
						return EnumHasAllFlags( ( EUsdDefaultKind ) StageActor->KindsToCollapse, Kind );
					}
					return false;
				})
			),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	};

	AddKindToCollapseEntry(
		EUsdDefaultKind::Model,
		LOCTEXT( "ModelKind", "Model" ),
		FCanExecuteAction::CreateLambda( [this]()
		{
			return ViewModel.UsdStageActor.Get() != nullptr;
		})
	);

	AddKindToCollapseEntry(
		EUsdDefaultKind::Component,
		LOCTEXT( "ModelComponent", "   Component" ),
		FCanExecuteAction::CreateLambda( [this]()
		{
			if ( AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get() )
			{
				// If we're collapsing all "model" kinds, the all "component"s should be collapsed anyway
				if ( !EnumHasAnyFlags( ( EUsdDefaultKind ) StageActor->KindsToCollapse, EUsdDefaultKind::Model ) )
				{
					return true;
				}
			}

			return false;
		})
	);

	AddKindToCollapseEntry(
		EUsdDefaultKind::Group,
		LOCTEXT( "ModelGroup", "   Group" ),
		FCanExecuteAction::CreateLambda( [this]()
		{
			if ( AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get() )
			{
				if ( !EnumHasAnyFlags( ( EUsdDefaultKind ) StageActor->KindsToCollapse, EUsdDefaultKind::Model ) )
				{
					return true;
				}
			}

			return false;
		})
	);

	AddKindToCollapseEntry(
		EUsdDefaultKind::Assembly,
		LOCTEXT( "ModelAssembly", "      Assembly" ),
		FCanExecuteAction::CreateLambda( [this]()
		{
			if ( AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get() )
			{
				if ( !EnumHasAnyFlags( ( EUsdDefaultKind ) StageActor->KindsToCollapse, EUsdDefaultKind::Model ) && !EnumHasAnyFlags( ( EUsdDefaultKind ) StageActor->KindsToCollapse, EUsdDefaultKind::Group ) )
				{
					return true;
				}
			}

			return false;
		})
	);

	AddKindToCollapseEntry(
		EUsdDefaultKind::Subcomponent,
		LOCTEXT( "ModelSubcomponent", "Subcomponent" ),
		FCanExecuteAction::CreateLambda( [this]()
		{
			return ViewModel.UsdStageActor.Get() != nullptr;
		})
	);
}

void SUsdStage::FillInterpolationTypeSubMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.AddMenuEntry(
		LOCTEXT("LinearType", "Linear"),
		LOCTEXT("LinearType_ToolTip", "Attribute values are linearly interpolated between authored values"),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]()
			{
				if(AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get())
				{
					FScopedTransaction Transaction(FText::Format(
						LOCTEXT("SetLinearInterpolationType", "Set USD stage actor '{0}' to linear interpolation"),
						FText::FromString(StageActor->GetActorLabel())
					));

					StageActor->SetInterpolationType( EUsdInterpolationType::Linear );
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
					return StageActor->InterpolationType == EUsdInterpolationType::Linear;
				}
				return false;
			})
		),
		NAME_None,
		EUserInterfaceActionType::RadioButton
	);

	MenuBuilder.AddMenuEntry(
		LOCTEXT("HeldType", "Held"),
		LOCTEXT("HeldType_ToolTip", "Attribute values are held constant between authored values. An attribute's value will be equal to the nearest preceding authored value. If there is no preceding authored value, the value will be equal to the nearest subsequent value."),
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([this]()
			{
				if(AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get())
				{
					FScopedTransaction Transaction(FText::Format(
						LOCTEXT("SetHeldInterpolationType", "Set USD stage actor '{0}' to held interpolation"),
						FText::FromString(StageActor->GetActorLabel())
					));

					StageActor->SetInterpolationType( EUsdInterpolationType::Held );
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
					return StageActor->InterpolationType == EUsdInterpolationType::Held;
				}
				return false;
			})
		),
		NAME_None,
		EUserInterfaceActionType::RadioButton
	);
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
		EUserInterfaceActionType::ToggleButton
	);
}

void SUsdStage::FillNaniteThresholdSubMenu( FMenuBuilder& MenuBuilder )
{
	if ( AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get() )
	{
		CurrentNaniteThreshold = StageActor->NaniteTriangleThreshold;
	}

	TSharedRef<SSpinBox<int32>> Slider = SNew( SSpinBox<int32> )
		.MinValue( 0 )
		.ToolTipText( LOCTEXT( "TriangleThresholdTooltip", "Try enabling Nanite for static meshes that are generated with at least this many triangles" ) )
		.Value( this, &SUsdStage::GetNaniteTriangleThresholdValue )
		.OnValueChanged(this, &SUsdStage::OnNaniteTriangleThresholdValueChanged )
		.SupportDynamicSliderMaxValue( true )
		.IsEnabled_Lambda( [this]() -> bool
		{
			return ViewModel.UsdStageActor.Get() != nullptr;
		})
		.OnValueCommitted( this, &SUsdStage::OnNaniteTriangleThresholdValueCommitted );

	const bool bNoIndent = true;
	MenuBuilder.AddWidget( Slider, FText::FromString( TEXT( "Triangle threshold: " ) ), bNoIndent );
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

void SUsdStage::OnResetStage()
{
	ViewModel.ResetStage();

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
	if ( bUpdatingPrimSelection )
	{
		return;
	}

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
		SetActor( &UsdStageModule.GetUsdStageActor( GWorld ) );
	}

	ViewModel.OpenStage( FilePath );
}

void SUsdStage::SetActor( AUsdStageActor* InUsdStageActor )
{
	// Call this first so that we clear all of our delegates from the previous actor before we switch actors
	ClearStageActorDelegates();

	ViewModel.UsdStageActor = InUsdStageActor;

	SetupStageActorDelegates();

	if ( this->UsdPrimInfoWidget && InUsdStageActor )
	{
		// Just reset to the pseudoroot for now
		this->UsdPrimInfoWidget->SetPrimPath( ViewModel.UsdStageActor->GetOrLoadUsdStage(), TEXT( "/" ) );
	}

	Refresh();
}

void SUsdStage::Refresh()
{
	// May be nullptr, but that is ok. Its how the widgets are reset
	AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get();

	if (UsdLayersTreeView)
	{
		UsdLayersTreeView->Refresh( StageActor, true );
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

	TGuardValue<bool> SelectionLoopGuard( bUpdatingPrimSelection, true );

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

int32 SUsdStage::GetNaniteTriangleThresholdValue() const
{
	return CurrentNaniteThreshold;
}

void SUsdStage::OnNaniteTriangleThresholdValueChanged( int32 InValue )
{
	CurrentNaniteThreshold = InValue;
}

void SUsdStage::OnNaniteTriangleThresholdValueCommitted( int32 InValue, ETextCommit::Type InCommitType )
{
	AUsdStageActor* StageActor = ViewModel.UsdStageActor.Get();
	if ( !StageActor )
	{
		return;
	}

	FScopedTransaction Transaction( FText::Format(
		LOCTEXT( "NaniteTriangleThresholdCommittedTransaction", "Change Nanite triangle threshold for USD stage actor '{0}'" ),
		FText::FromString( StageActor->GetActorLabel() )
	) );

	StageActor->SetNaniteTriangleThreshold( InValue );
	CurrentNaniteThreshold = InValue;
}

#endif // #if USE_USD_SDK

#undef LOCTEXT_NAMESPACE
