// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUSDStage.h"

#include "IUSDImporterModule.h"
#include "SUSDLayersTreeView.h"
#include "SUSDPrimInfo.h"
#include "SUSDStageInfo.h"
#include "SUSDStageTreeView.h"
#include "USDErrorUtils.h"
#include "USDImportOptions.h"
#include "USDImporter.h"
#include "USDLayerUtils.h"
#include "USDStageActor.h"
#include "USDStageModule.h"
#include "USDTypesConversion.h"
#include "UnrealUSDWrapper.h"

#include "Dialogs/DlgPickPath.h"
#include "EditorStyleSet.h"
#include "Engine/World.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "UObject/GCObjectScopeGuard.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Layout/SSplitter.h"

#define LOCTEXT_NAMESPACE "SUsdStage"

#if USE_USD_SDK

#include <string>

#include "USDIncludesStart.h"

#include "pxr/pxr.h"
#include "pxr/usd/kind/registry.h"
#include "pxr/usd/sdf/copyUtils.h"
#include "pxr/usd/sdf/schema.h"
#include "pxr/usd/usd/editTarget.h"
#include "pxr/usd/usd/modelAPI.h"
#include "pxr/usd/usd/stageCache.h"
#include "pxr/usd/usd/stageCacheContext.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdUtils/authoring.h"
#include "pxr/usd/usdUtils/stitch.h"

#include "USDIncludesEnd.h"

namespace SUSDStageConstants
{
	static const FMargin SectionPadding( 1.f, 4.f, 1.f, 1.f );
}

void SUsdStage::Construct( const FArguments& InArgs )
{
	OnStageActorPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddSP( SharedThis( this ), &SUsdStage::OnStageActorPropertyChanged );
	OnActorLoadedHandle = AUsdStageActor::OnActorLoaded.AddSP( SharedThis( this ), &SUsdStage::OnStageActorLoaded );

	IUsdStageModule& UsdStageModule = FModuleManager::Get().LoadModuleChecked< IUsdStageModule >( "UsdStage" );
	UsdStageActor = &UsdStageModule.GetUsdStageActor( GWorld );

	TUsdStore< pxr::UsdStageRefPtr > UsdStage;

	if ( UsdStageActor.IsValid() )
	{
		UsdStage = UsdStageActor->GetUsdStage();
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
					SAssignNew( UsdStageInfoWidget, SUsdStageInfo, UsdStageActor.Get() )
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
							SAssignNew( UsdStageTreeView, SUsdStageTreeView, UsdStageActor.Get() )
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
						SAssignNew( UsdLayersTreeView, SUsdLayersTreeView, UsdStageActor.Get() )
					]
				]
			]
		]
	];

	SetupStageActorDelegates();
}

void SUsdStage::SetupStageActorDelegates()
{
	if ( UsdStageActor.IsValid() )
	{
		ClearStageActorDelegates();

		OnPrimChangedHandle = UsdStageActor->OnPrimChanged.AddLambda(
			[ this ]( const FString& PrimPath, bool bResync )
			{
				if ( this->UsdStageTreeView )
				{
					this->UsdStageTreeView->RefreshPrim( PrimPath, bResync );
				}

				if ( this->UsdPrimInfoWidget && UsdStageActor.IsValid() && SelectedPrimPath.Equals( PrimPath, ESearchCase::IgnoreCase ) )
				{
					this->UsdPrimInfoWidget->SetPrimPath( UsdStageActor->GetUsdStage(), *PrimPath );
				}
			}
		);

		OnStageChangedHandle = UsdStageActor->OnStageChanged.AddLambda(
			[ this ]()
			{
				if ( UsdStageActor.IsValid() )
				{
					if ( this->UsdPrimInfoWidget )
					{
						this->UsdPrimInfoWidget->SetPrimPath( UsdStageActor->GetUsdStage(), TEXT("/") );
					}
				}

				this->Refresh();
			}
		);

		OnActorDestroyedHandle = UsdStageActor->OnActorDestroyed.AddLambda(
			[ this ]()
			{
				ClearStageActorDelegates();
				this->CloseStage();
			}
		);

		OnStageEditTargetChangedHandle = UsdStageActor->GetUsdListener().OnStageEditTargetChanged.AddLambda(
			[ this ]()
			{
				if ( this->UsdLayersTreeView && UsdStageActor.IsValid() )
				{
					this->UsdLayersTreeView->Refresh( UsdStageActor.Get(), false );
				}
			}
		);
	}
}

void SUsdStage::ClearStageActorDelegates()
{
	if ( UsdStageActor.IsValid() )
	{
		UsdStageActor->OnStageChanged.Remove( OnStageChangedHandle );
		UsdStageActor->OnPrimChanged.Remove( OnPrimChangedHandle );
		UsdStageActor->OnActorDestroyed.Remove ( OnActorDestroyedHandle );

		UsdStageActor->GetUsdListener().OnStageEditTargetChanged.Remove( OnStageEditTargetChangedHandle );
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
				if(AUsdStageActor* StageActor = UsdStageActor.Get())
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
				return UsdStageActor.Get() != nullptr;
			}),
			FIsActionChecked::CreateLambda([this]()
			{
				if(AUsdStageActor* StageActor = UsdStageActor.Get())
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
				if(AUsdStageActor* StageActor = UsdStageActor.Get())
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
				return UsdStageActor.Get() != nullptr;
			}),
			FIsActionChecked::CreateLambda([this]()
			{
				if(AUsdStageActor* StageActor = UsdStageActor.Get())
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
					if(AUsdStageActor* StageActor = UsdStageActor.Get())
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
					return UsdStageActor.Get() != nullptr;
				}),
				FIsActionChecked::CreateLambda([this, Purpose]()
				{
					if(AUsdStageActor* StageActor = UsdStageActor.Get())
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
		FScopedTransaction Transaction(FText::Format(
			LOCTEXT("NewStageTransaction", "Created new USD stage '{0}'"),
			FText::FromString(UsdFilePath.GetValue())
		));

		{
			FScopedUsdAllocs UsdAllocs;

			pxr::UsdStageCacheContext UsdStageCacheContext( UnrealUSDWrapper::GetUsdStageCache() );
			pxr::UsdStageRefPtr UsdStage = pxr::UsdStage::CreateNew( UnrealToUsd::ConvertString( *UsdFilePath.GetValue() ).Get() );

			if ( !UsdStage )
			{
				return;
			}

			// Create default prim
			pxr::UsdGeomXform RootPrim = pxr::UsdGeomXform::Define( UsdStage, UnrealToUsd::ConvertPath( TEXT("/Root") ).Get() );
			pxr::UsdModelAPI( RootPrim ).SetKind( pxr::KindTokens->assembly );

			// Set default prim
			UsdStage->SetDefaultPrim( RootPrim.GetPrim() );

			// Set up axis
			UsdUtils::SetUsdStageAxis( UsdStage, pxr::UsdGeomTokens->z );
		}

		OpenStage( *UsdFilePath.GetValue() );
	}
}

void SUsdStage::OnOpen()
{
	TOptional< FString > UsdFilePath = UsdUtils::BrowseUsdFile( UsdUtils::EBrowseFileMode::Open, AsShared() );

	if ( UsdFilePath )
	{
		FScopedTransaction Transaction(FText::Format(
			LOCTEXT("OpenStageTransaction", "Open USD stage '{0}'"),
			FText::FromString(UsdFilePath.GetValue())
		));

		UsdUtils::StartMonitoringErrors();

		OpenStage( *UsdFilePath.GetValue() );

		UsdUtils::ShowErrorsAndStopMonitoring(FText::Format(LOCTEXT("USDOpenError", "Encountered some errors opening USD file at path '{0}!\nCheck the Output Log for details."), FText::FromString(UsdFilePath.GetValue())));
	}
}

void SUsdStage::OnSave()
{
	if ( UsdStageActor.IsValid() )
	{
		const pxr::UsdStageRefPtr& UsdStage = UsdStageActor->GetUsdStage();

		if ( UsdStage )
		{
			FScopedUsdAllocs UsdAllocs;

			UsdUtils::StartMonitoringErrors();

			UsdStage->Save();

			UsdUtils::ShowErrorsAndStopMonitoring(LOCTEXT("USDSaveError", "Failed to save current USD Stage!\nCheck the Output Log for details."));
		}
	}
}

void SUsdStage::OnReloadStage()
{
	if ( UsdStageActor.IsValid() )
	{
		const pxr::UsdStageRefPtr& UsdStage = UsdStageActor->GetUsdStage();

		if ( UsdStage )
		{
			UsdUtils::StartMonitoringErrors();
			{
				FScopedUsdAllocs Allocs;
				const std::vector<pxr::SdfLayerHandle>& HandleVec = UsdStage->GetUsedLayers();
				bool bForce = true;
				pxr::SdfLayer::ReloadLayers({HandleVec.begin(), HandleVec.end()}, bForce);
			}
			if (UsdUtils::ShowErrorsAndStopMonitoring())
			{
				return;
			}

			// If we were editing an unsaved layer, when we reload the edit target will be cleared.
			// We need to make sure we're always editing something or else UsdEditContext might trigger some errors
			const pxr::UsdEditTarget& EditTarget = UsdStage->GetEditTarget();
			if (!EditTarget.IsValid() || EditTarget.IsNull())
			{
				UsdStage->SetEditTarget(UsdStage->GetRootLayer());
			}

			if ( UsdLayersTreeView )
			{
				UsdLayersTreeView->Refresh( UsdStageActor.Get(), true );
			}
		}
	}
}

void SUsdStage::OnClose()
{
	FScopedTransaction Transaction(LOCTEXT("CloseTransaction", "Close USD stage"));

	CloseStage();
}

void SUsdStage::OnImport()
{
	AUsdStageActor* StageActor = UsdStageActor.Get();
	if ( !StageActor )
	{
		return;
	}

	const pxr::UsdStageRefPtr& UsdStage = StageActor->GetUsdStage();

	if ( !UsdStage )
	{
		return;
	}

	// Select content browser destination
	UPackage* DestinationFolder = nullptr;
	FString StageName;
	{
		FString Path = "/Game/"; // Trailing '/' is needed to set the default path

		TSharedRef< SDlgPickPath > PickContentPathDlg =
		SNew( SDlgPickPath )
		.Title(LOCTEXT("ChooseImportRootContentPath", "Choose a location to import the USD assets to"))
		.DefaultPath( FText::FromString( Path ) );

		if ( PickContentPathDlg->ShowModal() == EAppReturnType::Cancel )
		{
			return;
		}

		Path = PickContentPathDlg->GetPath().ToString() + "/";

		DestinationFolder = CreatePackage( nullptr, *Path );

		if ( !DestinationFolder )
		{
			return;
		}

		FString PathPart;
		FString ExtensionPart;

		FPaths::Split( UsdToUnreal::ConvertString( UsdStage->GetRootLayer()->GetDisplayName() ), PathPart, StageName, ExtensionPart );
	}

	// Import directly from stage
	bool bCanceled = false;
	{
		UUsdSceneImportContextContainer* ImportContextContainer = NewObject< UUsdSceneImportContextContainer >();
		FGCObjectScopeGuard ImportContextContainerGuard( ImportContextContainer );

		ImportContextContainer->ImportContext.Init( DestinationFolder, StageName, UsdStage );
		ImportContextContainer->ImportContext.bIsAutomated = false;
		ImportContextContainer->ImportContext.bApplyWorldTransformToGeometry = false;

		if (UUSDSceneImportOptions* SceneOptions = Cast<UUSDSceneImportOptions>(ImportContextContainer->ImportContext.ImportOptions))
		{
			SceneOptions->PurposesToImport = StageActor->PurposesToLoad;
		}

		UUSDImporter* UsdImporter = IUSDImporterModule::Get().GetImporter();

		bCanceled = !UsdImporter->ShowImportOptions( ImportContextContainer->ImportContext );

		if ( !bCanceled )
		{
			UsdImporter->ImportUsdStage( ImportContextContainer->ImportContext );
		}
	}

	// Clear USD Stage Actor
	if ( !bCanceled )
	{
		OpenStage( TEXT("") );
	}
}

void SUsdStage::OnPrimSelected( FString PrimPath )
{
	if ( UsdPrimInfoWidget )
	{
		TUsdStore< pxr::UsdStageRefPtr > UsdStage;

		if ( UsdStageActor.IsValid() )
		{
			UsdStage = UsdStageActor->GetUsdStage();
		}

		SelectedPrimPath = PrimPath;
		UsdPrimInfoWidget->SetPrimPath( UsdStage, *PrimPath );
	}
}

void SUsdStage::OpenStage( const TCHAR* FilePath )
{
	if ( !UsdStageActor.IsValid() )
	{
		IUsdStageModule& UsdStageModule = FModuleManager::Get().LoadModuleChecked< IUsdStageModule >( "UsdStage" );
		UsdStageActor = &UsdStageModule.GetUsdStageActor( GWorld );

		SetupStageActorDelegates();
	}

	check( UsdStageActor.IsValid() );
	UsdStageActor->Modify();

	UsdStageActor->RootLayer.FilePath = FilePath;
	FPropertyChangedEvent RootLayerPropertyChangedEvent( FindFieldChecked< FProperty >( UsdStageActor->GetClass(), FName("RootLayer") ) );
	UsdStageActor->PostEditChangeProperty( RootLayerPropertyChangedEvent );
}

void SUsdStage::CloseStage()
{
	if ( AUsdStageActor* StageActor = UsdStageActor.Get() )
	{
		StageActor->Reset();
	}

	Refresh();
}

void SUsdStage::Refresh()
{
	// May be nullptr, but that is ok. Its how the widgets are reset
	AUsdStageActor* StageActor = UsdStageActor.Get();

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
	if ( UsdStageActor == InUsdStageActor )
	{
		return;
	}

	ClearStageActorDelegates();
	UsdStageActor = InUsdStageActor;
	SetupStageActorDelegates();

	// Refresh here because we may be receiving an actor that has a stage already loaded,
	// like during undo/redo
	Refresh();
}

void SUsdStage::OnStageActorPropertyChanged( UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent )
{
	if ( ObjectBeingModified == UsdStageActor )
	{
		if ( this->UsdStageInfoWidget )
		{
			this->UsdStageInfoWidget->RefreshStageInfos( UsdStageActor.Get() );
		}
	}
}

#endif // #if USE_USD_SDK

#undef LOCTEXT_NAMESPACE
