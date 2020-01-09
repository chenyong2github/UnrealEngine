// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SUSDStage.h"

#include "IUSDImporterModule.h"
#include "SUSDLayersTreeView.h"
#include "SUSDPrimInfo.h"
#include "SUSDStageInfo.h"
#include "SUSDStageTreeView.h"
#include "USDErrorUtils.h"
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
#include "pxr/usd/sdf/copyUtils.h"
#include "pxr/usd/sdf/schema.h"
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

		if ( !UsdStage.Get() )
		{
			UsdStageActor.Reset();
		}
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
					.OnInitialLoadSetChanged( this, &SUsdStage::OnInitialLoadSetChanged )
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

		if ( !OnStageChangedHandle.IsValid() )
		{
			OnStageChangedHandle = UsdStageActor->OnStageChanged.AddLambda(
				[ this ]()
				{
					if ( UsdStageActor.IsValid() )
					{
						if ( this->UsdStageInfoWidget )
						{
							this->UsdStageInfoWidget->RefreshStageInfos( UsdStageActor.Get() );
						}

						if ( this->UsdStageTreeView )
						{
							this->UsdStageTreeView->Refresh( UsdStageActor.Get() );
							this->UsdStageTreeView->RequestTreeRefresh();
						}

						if ( this->UsdPrimInfoWidget )
						{
							this->UsdPrimInfoWidget->SetPrimPath( UsdStageActor->GetUsdStage(), TEXT("/") );
						}

						if ( this->UsdLayersTreeView )
						{
							this->UsdLayersTreeView->Refresh( UsdStageActor.Get(), true );
						}
					}
				}
			);
		}

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

SUsdStage::~SUsdStage()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove( OnStageActorPropertyChangedHandle );
	AUsdStageActor::OnActorLoaded.Remove( OnActorLoadedHandle );

	if ( UsdStageActor.IsValid() )
	{
		UsdStageActor->OnStageChanged.Remove( OnStageChangedHandle );
		UsdStageActor->OnPrimChanged.Remove( OnPrimChangedHandle );

		UsdStageActor->GetUsdListener().OnStageEditTargetChanged.Remove( OnStageEditTargetChangedHandle );
	}
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

void SUsdStage::OnNew()
{
	TOptional< FString > UsdFilePath = UsdUtils::BrowseUsdFile( UsdUtils::EBrowseFileMode::Save, AsShared() );

	if ( UsdFilePath )
	{
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
			pxr::UsdModelAPI( RootPrim ).SetKind( pxr::TfToken("component") );

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
			UsdStage->Reload();

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

void SUsdStage::OnImport()
{
	if ( !UsdStageActor.IsValid() )
	{
		return;
	}

	const pxr::UsdStageRefPtr& UsdStage = UsdStageActor->GetUsdStage();

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

void SUsdStage::OnInitialLoadSetChanged( EUsdInitialLoadSet InitialLoadSet )
{
	if ( UsdStageActor.IsValid() && UsdStageActor->InitialLoadSet != InitialLoadSet )
	{
		const FScopedTransaction Transaction( LOCTEXT("EditInitialLoadSetTransaction", "Edit Initial Load Set") );
		UsdStageActor->Modify();
		UsdStageActor->InitialLoadSet = InitialLoadSet;
	}
}

void SUsdStage::OpenStage( const TCHAR* FilePath )
{
	if ( !UsdStageActor.IsValid() )
	{
		IUsdStageModule& UsdStageModule = FModuleManager::Get().LoadModuleChecked< IUsdStageModule >( "UsdStage" );
		UsdStageActor = &UsdStageModule.GetUsdStageActor( GWorld );

		if ( UsdStageInfoWidget )
		{
			UsdStageActor->InitialLoadSet = UsdStageInfoWidget->GetInitialLoadSet();
		}

		SetupStageActorDelegates();
	}

	check( UsdStageActor.IsValid() );
	UsdStageActor->RootLayer.FilePath = FilePath;

	FPropertyChangedEvent RootLayerPropertyChangedEvent( FindFieldChecked< UProperty >( UsdStageActor->GetClass(), FName("RootLayer") ) );
	UsdStageActor->PostEditChangeProperty( RootLayerPropertyChangedEvent );
}

void SUsdStage::OnStageActorLoaded( AUsdStageActor* InUsdStageActor )
{
	if ( UsdStageActor == InUsdStageActor )
	{
		return;
	}

	UsdStageActor = InUsdStageActor;
	SetupStageActorDelegates();
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
