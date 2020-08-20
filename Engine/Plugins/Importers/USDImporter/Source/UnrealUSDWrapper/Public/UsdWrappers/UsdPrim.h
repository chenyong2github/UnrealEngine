// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/UniquePtr.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/pxr.h"
#include "USDIncludesEnd.h"

PXR_NAMESPACE_OPEN_SCOPE
	class UsdPrim;
PXR_NAMESPACE_CLOSE_SCOPE

#endif // #if USE_USD_SDK

namespace UE
{
	class FSdfPath;
	class FUsdAttribute;
	class FUsdStage;

	namespace Internal
	{
		class FUsdPrimImpl;
	}

	/**
	 * Minimal pxr::UsdPrim wrapper for Unreal that can be used from no-rtti modules.
	 */
	class UNREALUSDWRAPPER_API FUsdPrim
	{
	public:
		FUsdPrim();

		FUsdPrim( const FUsdPrim& Other );
		FUsdPrim( FUsdPrim&& Other );
		~FUsdPrim();

		FUsdPrim& operator=( const FUsdPrim& Other );
		FUsdPrim& operator=( FUsdPrim&& Other );

		bool operator==( const FUsdPrim& Other ) const;
		bool operator!=( const FUsdPrim& Other ) const;

		operator bool() const;

	// Auto conversion from/to pxr::UsdPrim
	public:
#if USE_USD_SDK
		explicit FUsdPrim( const pxr::UsdPrim& InUsdPrim );
		explicit FUsdPrim( pxr::UsdPrim&& InUsdPrim );
		FUsdPrim& operator=( const pxr::UsdPrim& InUsdPrim );
		FUsdPrim& operator=( pxr::UsdPrim&& InUsdPrim );

		operator pxr::UsdPrim&();
		operator const pxr::UsdPrim&() const;
#endif // #if USE_USD_SDK

	// Wrapped pxr::UsdPrim functions, refer to the USD SDK documentation
	public:
		bool IsValid() const;
		bool IsPseudoRoot() const;
		bool IsModel() const;
		bool IsGroup() const;

		const FSdfPath GetPrimPath() const;
		FUsdStage GetStage() const;

		FName GetName() const;
		FName GetTypeName() const;

		FUsdPrim GetParent() const;

		TArray< FUsdPrim > GetChildren() const;
		TArray< FUsdPrim > GetFilteredChildren( bool bTraverseInstanceProxies ) const;

		bool HasVariantSets() const;
		bool HasAuthoredReferences() const;

		bool HasPayload() const;
		bool IsLoaded() const;
		void Load();
		void Unload();

		TArray< FUsdAttribute > GetAttributes() const;
		FUsdAttribute GetAttribute(const TCHAR* AttrName) const;

	private:
		TUniquePtr< Internal::FUsdPrimImpl > Impl;
	};
}