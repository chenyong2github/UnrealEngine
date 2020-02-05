// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDLayerUtils.h"

#include "USDTypesConversion.h"
#include "USDErrorUtils.h"

#include "DesktopPlatformModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/Paths.h"
#include "Widgets/SWidget.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"

#include "pxr/usd/usd/editContext.h"
#include "pxr/usd/usd/modelAPI.h"
#include "pxr/usd/usdGeom/xform.h"

#include "USDIncludesEnd.h"

#define LOCTEXT_NAMESPACE "USDLayerUtils"

bool UsdUtils::InsertSubLayer( const TUsdStore< pxr::SdfLayerRefPtr >& ParentLayer, const TCHAR* SubLayerFile )
{
	if ( !ParentLayer.Get() )
	{
		return false;
	}

	FScopedUsdAllocs UsdAllocs;

	std::string UsdLayerFilePath = ParentLayer.Get()->GetRealPath();
	FString LayerFilePath = UsdToUnreal::ConvertString( UsdLayerFilePath );

	FString SubLayerFilePath = FPaths::ConvertRelativePathToFull( SubLayerFile );
	FPaths::MakePathRelativeTo( SubLayerFilePath, *LayerFilePath );

	ParentLayer.Get()->InsertSubLayerPath( UnrealToUsd::ConvertString( *SubLayerFilePath ).Get() );

	return true;
}

TOptional< FString > UsdUtils::BrowseUsdFile( EBrowseFileMode Mode, TSharedRef< const SWidget > OriginatingWidget )
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();

	if ( DesktopPlatform == nullptr )
	{
		return {};
	}

	// show the file browse dialog
	TSharedPtr< SWindow > ParentWindow = FSlateApplication::Get().FindWidgetWindow( OriginatingWidget );
	void* ParentWindowHandle = ( ParentWindow.IsValid() && ParentWindow->GetNativeWindow().IsValid() )
		? ParentWindow->GetNativeWindow()->GetOSWindowHandle()
		: nullptr;

	TArray< FString > OutFiles;

	switch ( Mode )
	{
		case EBrowseFileMode::Open :

			if ( DesktopPlatform->OpenFileDialog( ParentWindowHandle, LOCTEXT( "ChooseFile", "Choose file").ToString(), TEXT(""), TEXT(""), TEXT("usd files (*.usd; *.usda; *.usdc)|*.usd; *.usda; *.usdc"), EFileDialogFlags::None, OutFiles ) )
			{
				return FPaths::ConvertRelativePathToFull(OutFiles[0]);
			}
			break;

		case EBrowseFileMode::Save :
			if ( DesktopPlatform->SaveFileDialog( ParentWindowHandle, LOCTEXT( "ChooseFile", "Choose file").ToString(), TEXT(""), TEXT(""), TEXT("usd files (*.usd; *.usda; *.usdc)|*.usd; *.usda; *.usdc"), EFileDialogFlags::None, OutFiles ) )
			{
				return FPaths::ConvertRelativePathToFull(OutFiles[0]);
			}
			break;

		default:
			break;
	}

	return {};
}

TUsdStore< pxr::SdfLayerRefPtr > UsdUtils::CreateNewLayer( TUsdStore< pxr::UsdStageRefPtr > UsdStage, const TUsdStore<pxr::SdfLayerRefPtr>& ParentLayer, const TCHAR* LayerFilePath )
{
	FScopedUsdAllocs UsdAllocs;

	std::string UsdLayerFilePath = UnrealToUsd::ConvertString( *FPaths::ConvertRelativePathToFull( LayerFilePath ) ).Get();

	pxr::SdfLayerRefPtr LayerRef = pxr::SdfLayer::CreateNew( UsdLayerFilePath );

	if ( !LayerRef )
	{
		return {};
	}

	// New layer needs to be created and in the stage layer stack before we can edit it
	UsdUtils::InsertSubLayer(ParentLayer, LayerFilePath);

	UsdUtils::StartMonitoringErrors();
	pxr::UsdEditContext UsdEditContext( UsdStage.Get(), LayerRef );
	bool bHadErrors = UsdUtils::ShowErrorsAndStopMonitoring();

	if (bHadErrors)
	{
		return {};
	}

	// Create default prim
	FString PrimPath = TEXT("/") + FPaths::GetBaseFilename( UsdToUnreal::ConvertString( LayerRef->GetDisplayName() ) );

	pxr::SdfPath UsdPrimPath = UnrealToUsd::ConvertPath( *PrimPath ).Get();
	pxr::UsdGeomXform DefaultPrim = pxr::UsdGeomXform::Define( UsdStage.Get(), UsdPrimPath );

	if ( DefaultPrim)
	{
		// Set default prim
		LayerRef->SetDefaultPrim( DefaultPrim.GetPrim().GetName() );
	}

	// Set up axis
	UsdUtils::SetUsdStageAxis( UsdStage.Get(), pxr::UsdGeomTokens->z );

	return TUsdStore< pxr::SdfLayerRefPtr >( LayerRef );
}

#undef LOCTEXT_NAMESPACE

#endif // #if USE_USD_SDK
