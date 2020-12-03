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
	class UsdAttribute;
	class UsdPrim;
	class UsdStage;

	template< typename T > class TfRefPtr;

	using SdfLayerRefPtr = TfRefPtr< SdfLayer >;
	using UsdStageRefPtr = TfRefPtr< UsdStage >;
PXR_NAMESPACE_CLOSE_SCOPE

namespace UE
{
	class FSdfLayer;
	struct FSdfLayerOffset;
}

namespace UsdUtils
{
	enum class EBrowseFileMode
	{
		Open,
		Save
	};

	/** Inserts the SubLayerFile path into ParentLayer as a sublayer */
	USDUTILITIES_API bool InsertSubLayer( const TUsdStore< pxr::SdfLayerRefPtr >& ParentLayer, const TCHAR* SubLayerFile );

#if WITH_EDITOR
	/** Opens a file dialog to open or save a USD file */
	USDUTILITIES_API TOptional< FString > BrowseUsdFile( EBrowseFileMode Mode, TSharedRef< const SWidget > OriginatingWidget );
#endif // #if WITH_EDITOR

	/** Creates a new layer with a default prim */
	USDUTILITIES_API TUsdStore< pxr::SdfLayerRefPtr > CreateNewLayer( TUsdStore< pxr::UsdStageRefPtr > UsdStage, const TUsdStore< pxr::SdfLayerRefPtr >& ParentLayer, const TCHAR* LayerFilePath );

	/** Finds which layer introduced the prim in the stage local layer stack */
	USDUTILITIES_API UE::FSdfLayer FindLayerForPrim( const pxr::UsdPrim& Prim );

	/** Finds the strongest layer contributing to an attribute */
	USDUTILITIES_API UE::FSdfLayer FindLayerForAttribute( const pxr::UsdAttribute& Attribute, double TimeCode );

	/** Finds the layer for a sublayer path of a given root layer */
	USDUTILITIES_API UE::FSdfLayer FindLayerForSubLayerPath( const UE::FSdfLayer& RootLayer, const FStringView& SubLayerPath );

	/** Sets the layer offset for the strongest reference or payload in this prim composition arcs */
	USDUTILITIES_API bool SetRefOrPayloadLayerOffset( pxr::UsdPrim& Prim, const UE::FSdfLayerOffset& LayerOffset );

	/** Finds the layer offset that converts the Attribute local times to stage times */
	USDUTILITIES_API UE::FSdfLayerOffset GetLayerToStageOffset( const pxr::UsdAttribute& Attribute );
}

#endif // #if USE_USD_SDK
