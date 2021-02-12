// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"
#include "Templates/UniquePtr.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/pxr.h"
#include "USDIncludesEnd.h"

PXR_NAMESPACE_OPEN_SCOPE
	class UsdStage;
	template< typename T > class TfRefPtr;
	template< typename T > class TfWeakPtr;

	using UsdStageRefPtr = TfRefPtr< UsdStage >;
	using UsdStageWeakPtr = TfWeakPtr< UsdStage >;
PXR_NAMESPACE_CLOSE_SCOPE

#endif // #if USE_USD_SDK

namespace UE
{
	class FSdfLayer;
	class FSdfPath;
	class FUsdPrim;
	class FUsdStage;
	class FVtValue;

	namespace Internal
	{
		class FUsdStageImpl;
	}

	/**
	 * Minimal pxr::UsdStageRefPtr wrapper for Unreal that can be used from no-rtti modules.
	 */
	class UNREALUSDWRAPPER_API FUsdStage
	{
	public:
		FUsdStage();
		FUsdStage( const FUsdStage& Other );
		FUsdStage( FUsdStage&& Other );

		FUsdStage& operator=( const FUsdStage& Other );
		FUsdStage& operator=( FUsdStage&& Other );

		~FUsdStage();

		operator bool() const;

	// Auto conversion from/to pxr::UsdStageRefPtr
	public:
#if USE_USD_SDK
		explicit FUsdStage( const pxr::UsdStageRefPtr& InUsdStageRefPtr );
		explicit FUsdStage( pxr::UsdStageRefPtr&& InUsdStageRefPtr );

		operator pxr::UsdStageRefPtr&();
		operator const pxr::UsdStageRefPtr&() const;
		operator pxr::UsdStageWeakPtr() const;
#endif // #if USE_USD_SDK

	// Wrapped pxr::UsdStage functions, refer to the USD SDK documentation
	public:
		FSdfLayer GetRootLayer() const;
		FSdfLayer GetSessionLayer() const;
		bool HasLocalLayer( const FSdfLayer& Layer ) const;

		FUsdPrim GetPseudoRoot() const;
		FUsdPrim GetDefaultPrim() const;
		FUsdPrim GetPrimAtPath( const FSdfPath& Path ) const;

		bool IsEditTargetValid() const;
		void SetEditTarget( const FSdfLayer& Layer );
		FSdfLayer GetEditTarget() const;

		bool GetMetadata( const TCHAR* Key, UE::FVtValue& Value ) const;
		bool HasMetadata( const TCHAR* Key ) const;
		bool SetMetadata( const TCHAR* Key, const UE::FVtValue& Value ) const;
		bool ClearMetadata( const TCHAR* Key ) const;

		double GetStartTimeCode() const;
		double GetEndTimeCode() const;
		void SetStartTimeCode( double TimeCode );
		void SetEndTimeCode( double TimeCode );
		double GetTimeCodesPerSecond() const;
		void SetTimeCodesPerSecond( double TimeCodesPerSecond );
		double GetFramesPerSecond() const;
		void SetFramesPerSecond( double FramesPerSecond );

		void SetDefaultPrim( const FUsdPrim& Prim );
		FUsdPrim DefinePrim( const FSdfPath& Path, const TCHAR* TypeName = TEXT("") );
		bool RemovePrim( const FSdfPath& Path );

	private:
		TUniquePtr< Internal::FUsdStageImpl > Impl;
	};
}