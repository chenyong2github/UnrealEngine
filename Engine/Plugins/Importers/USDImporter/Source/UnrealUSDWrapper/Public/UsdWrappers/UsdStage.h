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
		UE::FSdfLayer GetRootLayer() const;
		UE::FSdfLayer GetSessionLayer() const;

		FUsdPrim GetPseudoRoot() const;
		FUsdPrim GetDefaultPrim() const;
		FUsdPrim GetPrimAtPath( const FSdfPath& Path ) const;

		bool IsEditTargetValid() const;
		void SetEditTarget( const FSdfLayer& Layer );

		double GetStartTimeCode() const;
		double GetEndTimeCode() const;
		void SetStartTimeCode( double TimeCode );
		void SetEndTimeCode( double TimeCode );

		void SetDefaultPrim( const FUsdPrim& Prim );
		FUsdPrim DefinePrim( const FSdfPath& Path, const TCHAR* TypeName = TEXT("") );
		bool RemovePrim( const FSdfPath& Path );

	private:
		TUniquePtr< Internal::FUsdStageImpl > Impl;
	};
}