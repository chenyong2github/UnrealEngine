// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDStageViewModel.h"

#include "UnrealUSDWrapper.h"
#include "USDConversionUtils.h"
#include "USDErrorUtils.h"
#include "USDLayerUtils.h"
#include "USDLog.h"
#include "USDStageActor.h"
#include "USDStageImportContext.h"
#include "USDStageImporterModule.h"
#include "USDStageImportOptions.h"
#include "USDStageModule.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/UsdStage.h"

#include "Engine/World.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "UObject/GCObjectScopeGuard.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/usd/kind/registry.h"
	#include "pxr/usd/usd/common.h"
	#include "pxr/usd/usd/modelAPI.h"
	#include "pxr/usd/usd/stage.h"
	#include "pxr/usd/usdGeom/xform.h"
#include "USDIncludesEnd.h"

#endif // #if USE_USD_SDK

#define LOCTEXT_NAMESPACE "UsdStageViewModel"

#if USE_USD_SDK
namespace UsdViewModelImpl
{
	/**
	 * Saves the UE-state session layer for the given stage.
	 * We use this instead of pxr::UsdStage::SaveSessionLayers because that function
	 * will emit a warning about the main session layer not being saved every time it is used
	 */
	void SaveUEStateLayer( const UE::FUsdStage& UsdStage )
	{
		const bool bCreateIfNeeded = false;
		if ( UE::FSdfLayer UEStateLayer = UsdUtils::GetUEPersistentStateSublayer( UsdStage, bCreateIfNeeded ) )
		{
			UEStateLayer.Export( *UEStateLayer.GetRealPath() );
		}
	}
}
#endif // #if USE_USD_SDK

void FUsdStageViewModel::NewStage( const TCHAR* FilePath )
{
	FScopedTransaction Transaction( FText::Format(
		LOCTEXT("NewStageTransaction", "Created new USD stage '{0}'"),
		FText::FromString( FilePath )
	));

	UE::FUsdStage UsdStage = FilePath ? UnrealUSDWrapper::NewStage( FilePath ) : UnrealUSDWrapper::NewStage();
	if ( !UsdStage )
	{
		return;
	}

	// If we pass in nullptr, we'll create an in-memory stage, and so the "RootLayer" path we'll send down to the
	// stage actor will be a magic path that is guaranteed to never exist in a filesystem due to invalid characters.
	// The stage actor will interpret that, and try to load the stage from the stage cache
	FString StagePath = FilePath;
	if ( FilePath == nullptr )
	{
		UE::FSdfLayer Layer = UsdStage.GetRootLayer();
		if ( Layer )
		{
			StagePath = FString( UnrealIdentifiers::IdentifierPrefix ) + Layer.GetIdentifier();
		}
	}

#if USE_USD_SDK
	{
		FScopedUsdAllocs UsdAllocs;

		// Create default prim
		pxr::UsdGeomXform RootPrim = pxr::UsdGeomXform::Define( UsdStage, UnrealToUsd::ConvertPath( TEXT("/Root") ).Get() );
		pxr::UsdModelAPI( RootPrim ).SetKind( pxr::KindTokens->assembly );

		// Set default prim
		( (pxr::UsdStageRefPtr&)UsdStage )->SetDefaultPrim( RootPrim.GetPrim() );
	}
#endif // #if USE_USD_SDK

	OpenStage( *StagePath );
}

void FUsdStageViewModel::OpenStage( const TCHAR* FilePath )
{
	if ( !UsdStageActor.IsValid() )
	{
		IUsdStageModule& UsdStageModule = FModuleManager::GetModuleChecked< IUsdStageModule >( TEXT("USDStage") );
		UsdStageActor = &UsdStageModule.GetUsdStageActor( GWorld );
	}

	if ( AUsdStageActor* StageActor = UsdStageActor.Get() )
	{
		StageActor->Modify();
		StageActor->SetRootLayer( FilePath );
	}
	else
	{
		UE_LOG(LogUsd, Error, TEXT("Failed to find a AUsdStageActor that could open stage '%s'!"), FilePath);
	}
}

void FUsdStageViewModel::ReloadStage()
{
	if ( !UsdStageActor.IsValid() )
	{
		return;
	}

#if USE_USD_SDK
	UE::FUsdStage Stage = UsdStageActor->GetOrLoadUsdStage();
	pxr::UsdStageRefPtr UsdStage = pxr::UsdStageRefPtr( Stage );

	if ( UsdStage )
	{
		UsdUtils::StartMonitoringErrors();
		{
			FScopedUsdAllocs Allocs;
			const std::vector<pxr::SdfLayerHandle>& HandleVec = UsdStage->GetUsedLayers();

			const bool bForce = true;
			pxr::SdfLayer::ReloadLayers( { HandleVec.begin(), HandleVec.end() }, bForce );

			// When reloading our UEState layer is closed but there is nothing on the root layer
			// that would automatically pull the UEState session layer and cause it to be reloaded, so we need to try
			// to load it back again
			const bool bCreateIfNeeded = false;
			UsdUtils::GetUEPersistentStateSublayer( Stage, bCreateIfNeeded );
		}

		if ( UsdUtils::ShowErrorsAndStopMonitoring() )
		{
			return;
		}

		// If we were editing an unsaved layer, when we reload the edit target will be cleared.
		// We need to make sure we're always editing something or else UsdEditContext might trigger some errors
		const pxr::UsdEditTarget& EditTarget = UsdStage->GetEditTarget();
		if ( !EditTarget.IsValid() || EditTarget.IsNull() )
		{
			UsdStage->SetEditTarget( UsdStage->GetEditTargetForLocalLayer( UsdStage->GetRootLayer() ) );
		}
	}
#endif // #if USE_USD_SDK
}

void FUsdStageViewModel::CloseStage()
{
	if ( AUsdStageActor* StageActor = UsdStageActor.Get() )
	{
		StageActor->Reset();
	}
}

void FUsdStageViewModel::SaveStage()
{
#if USE_USD_SDK
	if ( UsdStageActor.IsValid() )
	{
		if ( UE::FUsdStage UsdStage = UsdStageActor->GetOrLoadUsdStage() )
		{
			FScopedUsdAllocs UsdAllocs;

			UsdUtils::StartMonitoringErrors();

			pxr::UsdStageRefPtr( UsdStage )->Save();
			UsdViewModelImpl::SaveUEStateLayer( UsdStage );

			UsdUtils::ShowErrorsAndStopMonitoring(LOCTEXT("USDSaveError", "Failed to save current USD Stage!\nCheck the Output Log for details."));
		}
	}
#endif // #if USE_USD_SDK
}

void FUsdStageViewModel::SaveStageAs( const TCHAR* FilePath )
{
#if USE_USD_SDK
	FScopedTransaction Transaction( FText::Format(
		LOCTEXT( "SaveAsTransaction", "Saved USD stage as '{0}'" ),
		FText::FromString( FilePath )
	) );

	if ( UsdStageActor.IsValid() )
	{
		if ( UE::FUsdStage UsdStage = UsdStageActor->GetOrLoadUsdStage() )
		{
			UsdUtils::StartMonitoringErrors();

			if ( UE::FSdfLayer RootLayer = UsdStage.GetRootLayer() )
			{
				if ( pxr::SdfLayerRefPtr( RootLayer )->Export( TCHAR_TO_ANSI( FilePath ) ) )
				{
					FScopedUnrealAllocs UEAllocs;

					OpenStage( FilePath );

					UsdViewModelImpl::SaveUEStateLayer( UsdStage );
				}
			}

			UsdUtils::ShowErrorsAndStopMonitoring( LOCTEXT( "USDSaveAsError", "Failed to SaveAs current USD Stage!\nCheck the Output Log for details." ) );
		}
	}
#endif // #if USE_USD_SDK
}

void FUsdStageViewModel::ImportStage()
{
#if USE_USD_SDK
	AUsdStageActor* StageActor = UsdStageActor.Get();
	if ( !StageActor )
	{
		return;
	}

	const UE::FUsdStage UsdStage = StageActor->GetOrLoadUsdStage();
	if ( !UsdStage )
	{
		return;
	}

	// Import directly from stage
	{
		FUsdStageImportContext ImportContext;

		// Preload some settings according to USDStage options. These will overwrite whatever is loaded from config
		ImportContext.ImportOptions->PurposesToImport = StageActor->PurposesToLoad;
		ImportContext.ImportOptions->RenderContextToImport = StageActor->RenderContext;
		ImportContext.ImportOptions->ImportTime = StageActor->GetTime();
		ImportContext.ImportOptions->StageOptions.MetersPerUnit = UsdUtils::GetUsdStageMetersPerUnit( UsdStage );
		ImportContext.ImportOptions->StageOptions.UpAxis = UsdUtils::GetUsdStageUpAxisAsEnum( UsdStage );
		ImportContext.bReadFromStageCache = true; // So that we import whatever the user has open right now, even if the file has changes

		const FString RootPath = UsdStage.GetRootLayer().GetRealPath();
		const FString StageName = FPaths::GetBaseFilename( RootPath );

		const bool bIsAutomated = false;
		if ( ImportContext.Init( StageName, RootPath, TEXT("/Game/"), RF_Public | RF_Transactional, bIsAutomated ) )
		{
			FScopedTransaction Transaction( FText::Format(LOCTEXT("ImportTransaction", "Import USD stage '{0}'"), FText::FromString(StageName)));

			// Let the importer reuse our assets, but force it to spawn new actors and components always
			// This allows a different setting for asset/component collapsing, and doesn't require modifying the PrimTwins
			ImportContext.AssetCache = StageActor->GetAssetCache();
			ImportContext.MaterialToPrimvarToUVIndex = StageActor->GetMaterialToPrimvarToUVIndex();

			UUsdStageImporter* USDImporter = IUsdStageImporterModule::Get().GetImporter();
			USDImporter->ImportFromFile(ImportContext);

			// Note that our ImportContext can't keep strong references to the assets in AssetsCache, and when
			// we CloseStage(), the stage actor will stop referencing them. The only thing keeping them alive at this point is
			// the transaction buffer, but it should be enough at least until this import is complete
			CloseStage();
		}
	}

#endif // #if USE_USD_SDK
}

#undef LOCTEXT_NAMESPACE
