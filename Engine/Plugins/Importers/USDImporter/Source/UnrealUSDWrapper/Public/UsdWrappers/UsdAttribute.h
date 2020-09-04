// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/UniquePtr.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/pxr.h"
#include "USDIncludesEnd.h"

PXR_NAMESPACE_OPEN_SCOPE
	class UsdAttribute;
PXR_NAMESPACE_CLOSE_SCOPE

#endif // #if USE_USD_SDK

namespace UE
{
	class FSdfPath;
	class FUsdPrim;

	namespace Internal
	{
		class FUsdAttributeImpl;
	}

	/**
	 * Minimal pxr::UsdAttribute wrapper for Unreal that can be used from no-rtti modules.
	 */
	class UNREALUSDWRAPPER_API FUsdAttribute
	{
	public:
		FUsdAttribute();

		FUsdAttribute( const FUsdAttribute& Other );
		FUsdAttribute( FUsdAttribute&& Other );
		~FUsdAttribute();

		FUsdAttribute& operator=( const FUsdAttribute& Other );
		FUsdAttribute& operator=( FUsdAttribute&& Other );

		bool operator==( const FUsdAttribute& Other ) const;
		bool operator!=( const FUsdAttribute& Other ) const;

		operator bool() const;

	// Auto conversion from/to pxr::UsdAttribute
	public:
#if USE_USD_SDK
		explicit FUsdAttribute( const pxr::UsdAttribute& InUsdAttribute);
		explicit FUsdAttribute( pxr::UsdAttribute&& InUsdAttribute );
		FUsdAttribute& operator=( const pxr::UsdAttribute& InUsdAttribute );
		FUsdAttribute& operator=( pxr::UsdAttribute&& InUsdAttribute );

		operator pxr::UsdAttribute&();
		operator const pxr::UsdAttribute&() const;
#endif // #if USE_USD_SDK

	// Wrapped pxr::UsdAttribute functions, refer to the USD SDK documentation
	public:
		FName GetName() const;
		FName GetBaseName() const;
		FName GetTypeName() const;

		bool ValueMightBeTimeVarying() const;

		FSdfPath GetPath() const;
		FUsdPrim GetPrim() const;

	private:
		TUniquePtr< Internal::FUsdAttributeImpl > Impl;
	};
}