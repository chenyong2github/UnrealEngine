// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/pxr.h"
#include "USDIncludesEnd.h"

PXR_NAMESPACE_OPEN_SCOPE
	class SdfLayer;
	template< typename T > class TfRefPtr;

	using SdfLayerRefPtr = TfRefPtr< SdfLayer >;
PXR_NAMESPACE_CLOSE_SCOPE

#endif // #if USE_USD_SDK

namespace UE
{
	class FSdfPath;

	namespace Internal
	{
		class FSdfLayerImpl;
	}

	struct UNREALUSDWRAPPER_API FSdfLayerOffset
	{
		FSdfLayerOffset() = default;
		FSdfLayerOffset( double InOffset, double InScale )
			: Offset( InOffset )
			, Scale( InScale )
		{
		}

		double Offset = 0.0;
		double Scale = 1.0;
	};

	/**
	 * Minimal pxr::SdfLayerRefPtr wrapper for Unreal that can be used from no-rtti modules.
	 */
	class UNREALUSDWRAPPER_API FSdfLayer
	{
	public:
		FSdfLayer();

		FSdfLayer( const FSdfLayer& Other );
		FSdfLayer( FSdfLayer&& Other );

		FSdfLayer& operator=( const FSdfLayer& Other );
		FSdfLayer& operator=( FSdfLayer&& Other );

		~FSdfLayer();

		operator bool() const;

	// Auto conversion from/to pxr::UsdPrim
	public:
#if USE_USD_SDK
		explicit FSdfLayer( const pxr::SdfLayerRefPtr& InSdfLayer );
		explicit FSdfLayer( pxr::SdfLayerRefPtr&& InSdfLayer );

		FSdfLayer& operator=( const pxr::SdfLayerRefPtr& InSdfLayer );
		FSdfLayer& operator=( pxr::SdfLayerRefPtr&& InSdfLayer );

		operator pxr::SdfLayerRefPtr&();
		operator const pxr::SdfLayerRefPtr&() const;
#endif // #if USE_USD_SDK

	// Wrapped pxr::SdfLayer functions, refer to the USD SDK documentation
	public:
		static FSdfLayer FindOrOpen( const TCHAR* Identifier );

		bool Save( bool bForce = false ) const;

		FString GetRealPath() const;
		FString GetIdentifier() const;
		FString GetDisplayName() const;

		bool HasStartTimeCode() const;
		bool HasEndTimeCode() const;
		double GetStartTimeCode() const;
		double GetEndTimeCode() const;
		bool HasTimeCodesPerSecond() const;
		double GetTimeCodesPerSecond() const;

		int64 GetNumSubLayerPaths() const;
		TArray< FString > GetSubLayerPaths() const;
		TArray< FSdfLayerOffset > GetSubLayerOffsets() const;

		void SetSubLayerOffset( const FSdfLayerOffset& LayerOffset, int32 Index );

		bool HasSpec( const FSdfPath& Path ) const;

	private:
		TUniquePtr< Internal::FSdfLayerImpl > Impl;
	};

	/**
	 * Wrapper for global functions in pxr/usd/sdf/layerUtils.h
	 */
	class UNREALUSDWRAPPER_API FSdfLayerUtils
	{
	public:
		static FString SdfComputeAssetPathRelativeToLayer( const FSdfLayer& Anchor, const TCHAR* AssetPath );
	};
}