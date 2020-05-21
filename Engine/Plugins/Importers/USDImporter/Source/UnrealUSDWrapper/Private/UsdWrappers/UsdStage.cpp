// Copyright Epic Games, Inc. All Rights Reserved.

#include "UsdWrappers/UsdStage.h"

#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdPrim.h"

#include "USDMemory.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/usd/usd/prim.h"
	#include "pxr/usd/usd/stage.h"
#include "USDIncludesEnd.h"

#endif // #if USE_USD_SDK

namespace UE
{
	namespace Internal
	{
		class FUsdStageImpl
		{
		public:
			FUsdStageImpl() = default;

#if USE_USD_SDK
			explicit FUsdStageImpl( const pxr::UsdStageRefPtr& InUsdStageRefPtr )
				: PxrUsdStageRefPtr( InUsdStageRefPtr )
			{
			}

			explicit FUsdStageImpl( pxr::UsdStageRefPtr&& InUsdStageRefPtr )
				: PxrUsdStageRefPtr( MoveTemp( InUsdStageRefPtr ) )
			{
			}

			TUsdStore< pxr::UsdStageRefPtr > PxrUsdStageRefPtr;
#endif // #if USE_USD_SDK
		};
	}

	FUsdStage::FUsdStage()
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl =  MakeUnique< Internal::FUsdStageImpl >();
	}

	FUsdStage::FUsdStage( const FUsdStage& Other )
#if USE_USD_SDK
		: Impl( MakeUnique< Internal::FUsdStageImpl >( Other.Impl->PxrUsdStageRefPtr.Get() ) )
#endif // #if USE_USD_SDK
	{
	}

	FUsdStage::FUsdStage( FUsdStage&& Other ) = default;

	FUsdStage& FUsdStage::operator=( const FUsdStage& Other )
	{
#if USE_USD_SDK
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MakeUnique< Internal::FUsdStageImpl >( Other.Impl->PxrUsdStageRefPtr.Get() );
#endif // #if USE_USD_SDK
		return *this;
	}

	FUsdStage& FUsdStage::operator=( FUsdStage&& Other )
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MoveTemp( Other.Impl );

		return *this;
	}

	FUsdStage::operator bool() const
	{
#if USE_USD_SDK
		return (bool)Impl->PxrUsdStageRefPtr.Get();
#else
		return false;
#endif // #if USE_USD_SDK
	}

	FUsdStage::~FUsdStage()
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl.Reset();
	}

#if USE_USD_SDK
	FUsdStage::FUsdStage( const pxr::UsdStageRefPtr& InUsdStageRefPtr )
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MakeUnique< Internal::FUsdStageImpl >( InUsdStageRefPtr );
	}

	FUsdStage::FUsdStage( pxr::UsdStageRefPtr&& InUsdStageRefPtr )
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MakeUnique< Internal::FUsdStageImpl >( MoveTemp( InUsdStageRefPtr ) );
	}

	FUsdStage::operator pxr::UsdStageRefPtr&()
	{
		return Impl->PxrUsdStageRefPtr.Get();
	}

	FUsdStage::operator const pxr::UsdStageRefPtr&() const
	{
		return Impl->PxrUsdStageRefPtr.Get();
	}

	FUsdStage::operator pxr::UsdStageWeakPtr() const
	{
		return pxr::UsdStageWeakPtr( Impl->PxrUsdStageRefPtr.Get() );
	}
#endif // #if USE_USD_SDK

	FSdfLayer FUsdStage::GetRootLayer() const
	{
#if USE_USD_SDK
		return FSdfLayer( Impl->PxrUsdStageRefPtr.Get()->GetRootLayer() );
#else
		return FSdfLayer();
#endif // #if USE_USD_SDK
	}

	FSdfLayer FUsdStage::GetSessionLayer() const
	{
#if USE_USD_SDK
		return FSdfLayer( Impl->PxrUsdStageRefPtr.Get()->GetSessionLayer() );
#else
		return FSdfLayer();
#endif // #if USE_USD_SDK
	}

	FUsdPrim FUsdStage::GetPseudoRoot() const
	{
#if USE_USD_SDK
		return FUsdPrim( Impl->PxrUsdStageRefPtr.Get()->GetPseudoRoot() );
#else
		return FUsdPrim();
#endif // #if USE_USD_SDK
	}

	FUsdPrim FUsdStage::GetDefaultPrim() const
	{
#if USE_USD_SDK
		return FUsdPrim( Impl->PxrUsdStageRefPtr.Get()->GetDefaultPrim() );
#else
		return FUsdPrim();
#endif // #if USE_USD_SDK
	}

	FUsdPrim FUsdStage::GetPrimAtPath( const FSdfPath& Path ) const
	{
#if USE_USD_SDK
		return FUsdPrim( Impl->PxrUsdStageRefPtr.Get()->GetPrimAtPath( Path ) );
#else
		return FUsdPrim();
#endif // #if USE_USD_SDK
	}

	bool FUsdStage::IsEditTargetValid() const
	{
#if USE_USD_SDK
		return Impl->PxrUsdStageRefPtr.Get()->GetEditTarget().IsValid();
#else
		return false;
#endif // #if USE_USD_SDK
	}

	void FUsdStage::SetEditTarget( const FSdfLayer& Layer )
	{
#if USE_USD_SDK
		Impl->PxrUsdStageRefPtr.Get()->SetEditTarget( pxr::UsdEditTarget( Layer ) );
#endif // #if USE_USD_SDK
	}

	double FUsdStage::GetStartTimeCode() const
	{
#if USE_USD_SDK
		return Impl->PxrUsdStageRefPtr.Get()->GetStartTimeCode();
#else
		return 0.0;
#endif // #if USE_USD_SDK
	}

	double FUsdStage::GetEndTimeCode() const
	{
#if USE_USD_SDK
		return Impl->PxrUsdStageRefPtr.Get()->GetEndTimeCode();
#else
		return 0.0;
#endif // #if USE_USD_SDK
	}

	void FUsdStage::SetStartTimeCode( double TimeCode )
	{
#if USE_USD_SDK
		Impl->PxrUsdStageRefPtr.Get()->SetStartTimeCode( TimeCode );
#endif // #if USE_USD_SDK
	}

	void FUsdStage::SetEndTimeCode( double TimeCode )
	{
#if USE_USD_SDK
		Impl->PxrUsdStageRefPtr.Get()->SetEndTimeCode( TimeCode );
#endif // #if USE_USD_SDK
	}

	void FUsdStage::SetDefaultPrim( const FUsdPrim& Prim )
	{
#if USE_USD_SDK
		Impl->PxrUsdStageRefPtr.Get()->SetDefaultPrim( Prim );
#endif // #if USE_USD_SDK
	}

	FUsdPrim FUsdStage::DefinePrim( const FSdfPath& Path, const TCHAR* TypeName )
	{
#if USE_USD_SDK
		return FUsdPrim( Impl->PxrUsdStageRefPtr.Get()->DefinePrim( Path, pxr::TfToken( TCHAR_TO_ANSI( TypeName ) ) ) );
#else
		return FUsdPrim();
#endif // #if USE_USD_SDK
	}

	bool FUsdStage::RemovePrim( const FSdfPath& Path )
	{
#if USE_USD_SDK
		return Impl->PxrUsdStageRefPtr.Get()->RemovePrim( Path );
#else
		return false;
#endif // #if USE_USD_SDK
	}
}
