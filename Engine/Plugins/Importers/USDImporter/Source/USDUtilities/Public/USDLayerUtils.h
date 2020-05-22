// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if USE_USD_SDK

#include "USDMemory.h"

#include "USDIncludesStart.h"
	#include "pxr/pxr.h"
#include "USDIncludesEnd.h"

class SWidget;

PXR_NAMESPACE_OPEN_SCOPE
	class SdfLayer;
	class UsdStage;

	template< typename T > class TfRefPtr;

	using SdfLayerRefPtr = TfRefPtr< SdfLayer >;
	using UsdStageRefPtr = TfRefPtr< UsdStage >;
PXR_NAMESPACE_CLOSE_SCOPE

namespace UsdUtils
{
	enum class EBrowseFileMode
	{
		Open,
		Save
	};

	/** Inserts the SubLayerFile path into ParentLayer as a sublayer */
	USDUTILITIES_API bool InsertSubLayer( const TUsdStore< pxr::SdfLayerRefPtr >& ParentLayer, const TCHAR* SubLayerFile );

	/** Opens a file dialog to open or save a USD file */
	USDUTILITIES_API TOptional< FString > BrowseUsdFile( EBrowseFileMode Mode, TSharedRef< const SWidget > OriginatingWidget );

	/** Creates a new layer with a default prim */
	USDUTILITIES_API TUsdStore< pxr::SdfLayerRefPtr > CreateNewLayer( TUsdStore< pxr::UsdStageRefPtr > UsdStage, const TUsdStore< pxr::SdfLayerRefPtr >& ParentLayer, const TCHAR* LayerFilePath );
}

#endif // #if USE_USD_SDK
