// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDLayerUtils.h"

#include "USDTypesConversion.h"

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
				return OutFiles[0];
			}
			break;

		case EBrowseFileMode::Save :
			if ( DesktopPlatform->SaveFileDialog( ParentWindowHandle, LOCTEXT( "ChooseFile", "Choose file").ToString(), TEXT(""), TEXT(""), TEXT("usd files (*.usd; *.usda; *.usdc)|*.usd; *.usda; *.usdc"), EFileDialogFlags::None, OutFiles ) )
			{
				return OutFiles[0];
			}
			break;

		default:
			break;
	}

	return {};
}

TUsdStore< pxr::SdfLayerRefPtr > UsdUtils::CreateNewLayer( TUsdStore< pxr::UsdStageRefPtr > UsdStage, const TCHAR* LayerFilePath )
{
	FScopedUsdAllocs UsdAllocs;

	std::string UsdLayerFilePath = UnrealToUsd::ConvertString( *FPaths::ConvertRelativePathToFull( LayerFilePath ) ).Get();

	pxr::SdfLayerRefPtr LayerRef = pxr::SdfLayer::CreateNew( UsdLayerFilePath );

	if ( !LayerRef )
	{
		return {};
	}

	pxr::UsdEditContext UsdEditContext( UsdStage.Get(), LayerRef );

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
