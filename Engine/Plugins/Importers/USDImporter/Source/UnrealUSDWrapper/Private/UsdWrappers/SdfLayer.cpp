// Copyright Epic Games, Inc. All Rights Reserved.

#include "UsdWrappers/SdfLayer.h"

#include "UsdWrappers/SdfPath.h"

#include "USDMemory.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/usd/sdf/layer.h"
	#include "pxr/usd/sdf/layerUtils.h"
#include "USDIncludesEnd.h"

#endif // #if USE_USD_SDK

namespace UE
{
	namespace Internal
	{
		class FSdfLayerImpl
		{
		public:
			FSdfLayerImpl() = default;

#if USE_USD_SDK
			explicit FSdfLayerImpl( const pxr::SdfLayerRefPtr& InSdfLayer )
				: PxrSdfLayer( InSdfLayer )
			{
			}

			explicit FSdfLayerImpl( pxr::SdfLayerRefPtr&& InSdfLayer )
				: PxrSdfLayer( MoveTemp( InSdfLayer ) )
			{
			}

			TUsdStore< pxr::SdfLayerRefPtr > PxrSdfLayer;
#endif // #if USE_USD_SDK
		};
	}

	FSdfLayer::FSdfLayer()
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MakeUnique< Internal::FSdfLayerImpl >();
	}

	FSdfLayer::FSdfLayer( const FSdfLayer& Other )
	{
#if USE_USD_SDK
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MakeUnique< Internal::FSdfLayerImpl >( Other.Impl->PxrSdfLayer.Get() );
#endif // #if USE_USD_SDK
	}

	FSdfLayer::FSdfLayer( FSdfLayer&& Other ) = default;

	FSdfLayer& FSdfLayer::operator=( const FSdfLayer& Other )
	{
#if USE_USD_SDK
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MakeUnique< Internal::FSdfLayerImpl >( Other.Impl->PxrSdfLayer.Get() );
#endif // #if USE_USD_SDK
		return *this;
	}

	FSdfLayer& FSdfLayer::operator=( FSdfLayer&& Other )
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MoveTemp( Other.Impl );

		return *this;
	}

	FSdfLayer::~FSdfLayer()
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl.Reset();
	}

	bool FSdfLayer::operator==( const FSdfLayer& Other ) const
	{
#if USE_USD_SDK
		return Impl->PxrSdfLayer.Get() == Other.Impl->PxrSdfLayer.Get();
#else
		return false;
#endif // #if USE_USD_SDK
	}

	bool FSdfLayer::operator!=( const FSdfLayer& Other ) const
	{
		return !( *this == Other );
	}

	FSdfLayer::operator bool() const
	{
#if USE_USD_SDK
		return (bool)Impl->PxrSdfLayer.Get();
#else
		return false;
#endif // #if USE_USD_SDK
	}

#if USE_USD_SDK
	FSdfLayer::FSdfLayer( const pxr::SdfLayerRefPtr& InSdfLayer )
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MakeUnique< Internal::FSdfLayerImpl >( InSdfLayer );
	}

	FSdfLayer::FSdfLayer( pxr::SdfLayerRefPtr&& InSdfLayer )
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MakeUnique< Internal::FSdfLayerImpl >( MoveTemp( InSdfLayer ) );
	}

	FSdfLayer& FSdfLayer::operator=( const pxr::SdfLayerRefPtr& InSdfLayer )
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MakeUnique< Internal::FSdfLayerImpl >( InSdfLayer );
		return *this;
	}

	FSdfLayer& FSdfLayer::operator=( pxr::SdfLayerRefPtr&& InSdfLayer )
	{
		FScopedUnrealAllocs UnrealAllocs;
		Impl = MakeUnique< Internal::FSdfLayerImpl >( MoveTemp( InSdfLayer ) );
		return *this;
	}

	FSdfLayer::operator pxr::SdfLayerRefPtr&()
	{
		return Impl->PxrSdfLayer.Get();
	}

	FSdfLayer::operator const pxr::SdfLayerRefPtr&() const
	{
		return Impl->PxrSdfLayer.Get();
	}
#endif // #if USE_USD_SDK

	FSdfLayer FSdfLayer::FindOrOpen( const TCHAR* Identifier )
	{
#if USE_USD_SDK
		return FSdfLayer( pxr::SdfLayer::FindOrOpen( TCHAR_TO_ANSI( Identifier ) ) );
#else
		return FSdfLayer();
#endif // #if USE_USD_SDK
	}

	bool FSdfLayer::Save( bool bForce ) const
	{
#if USE_USD_SDK
		return Impl->PxrSdfLayer.Get()->Save( bForce );
#else
		return false;
#endif // #if USE_USD_SDK
	}

	FString FSdfLayer::GetRealPath() const
	{
#if USE_USD_SDK
		FScopedUsdAllocs UsdAllocs;
		return FString( ANSI_TO_TCHAR( Impl->PxrSdfLayer.Get()->GetRealPath().c_str() ) );
#else
		return FString();
#endif // #if USE_USD_SDK
	}

	FString FSdfLayer::GetIdentifier() const
	{
#if USE_USD_SDK
		FScopedUsdAllocs UsdAllocs;
		return FString( ANSI_TO_TCHAR( Impl->PxrSdfLayer.Get()->GetIdentifier().c_str() ) );
#else
		return FString();
#endif // #if USE_USD_SDK
	}

	FString FSdfLayer::GetDisplayName() const
	{
#if USE_USD_SDK
		FScopedUsdAllocs UsdAllocs;
		return FString( ANSI_TO_TCHAR( Impl->PxrSdfLayer.Get()->GetDisplayName().c_str() ) );
#else
		return FString();
#endif // #if USE_USD_SDK
	}

	bool FSdfLayer::HasStartTimeCode() const
	{
#if USE_USD_SDK
		return Impl->PxrSdfLayer.Get()->HasStartTimeCode();
#else
		return false;
#endif // #if USE_USD_SDK
	}

	bool FSdfLayer::HasEndTimeCode() const
	{
#if USE_USD_SDK
		return Impl->PxrSdfLayer.Get()->HasEndTimeCode();
#else
		return false;
#endif // #if USE_USD_SDK
	}

	int64 FSdfLayer::GetNumSubLayerPaths() const
	{
#if USE_USD_SDK
		return (int64)Impl->PxrSdfLayer.Get()->GetNumSubLayerPaths();
#else
		return 0;
#endif // #if USE_USD_SDK
	}

	double FSdfLayer::GetStartTimeCode() const
	{
#if USE_USD_SDK
		return Impl->PxrSdfLayer.Get()->GetStartTimeCode();
#else
		return 0.0;
#endif // #if USE_USD_SDK
	}

	double FSdfLayer::GetEndTimeCode() const
	{
#if USE_USD_SDK
		return Impl->PxrSdfLayer.Get()->GetEndTimeCode();
#else
		return 0.0;
#endif // #if USE_USD_SDK
	}

	void FSdfLayer::SetStartTimeCode( double TimeCode )
	{
#if USE_USD_SDK
		if ( !Impl->PxrSdfLayer.Get()->HasStartTimeCode() || !FMath::IsNearlyEqual( TimeCode, Impl->PxrSdfLayer.Get()->GetStartTimeCode() ) )
		{
			Impl->PxrSdfLayer.Get()->SetStartTimeCode( TimeCode );
		}
#endif // #if USE_USD_SDK
	}

	void FSdfLayer::SetEndTimeCode( double TimeCode )
	{
#if USE_USD_SDK
		if ( !Impl->PxrSdfLayer.Get()->HasEndTimeCode() || !FMath::IsNearlyEqual( TimeCode, Impl->PxrSdfLayer.Get()->GetEndTimeCode() ) )
		{
			Impl->PxrSdfLayer.Get()->SetEndTimeCode( TimeCode );
		}
#endif // #if USE_USD_SDK
	}

	bool FSdfLayer::HasTimeCodesPerSecond() const
	{
#if USE_USD_SDK
		return Impl->PxrSdfLayer.Get()->HasTimeCodesPerSecond();
#else
		return false;
#endif // #if USE_USD_SDK
	}

	double FSdfLayer::GetTimeCodesPerSecond() const
	{
#if USE_USD_SDK
		return Impl->PxrSdfLayer.Get()->GetTimeCodesPerSecond();
#else
		return 0.0;
#endif // #if USE_USD_SDK
	}

	void FSdfLayer::SetTimeCodesPerSecond( double TimeCodesPerSecond )
	{
#if USE_USD_SDK
		return Impl->PxrSdfLayer.Get()->SetTimeCodesPerSecond( TimeCodesPerSecond );
#endif // #if USE_USD_SDK
	}

	bool FSdfLayer::HasFramesPerSecond() const
	{
#if USE_USD_SDK
		return Impl->PxrSdfLayer.Get()->HasFramesPerSecond();
#else
		return false;
#endif // #if USE_USD_SDK
	}

	double FSdfLayer::GetFramesPerSecond() const
	{
#if USE_USD_SDK
		return Impl->PxrSdfLayer.Get()->GetFramesPerSecond();
#else
		return 0.0;
#endif // #if USE_USD_SDK
	}

	void FSdfLayer::SetFramesPerSecond( double FramesPerSecond )
	{
#if USE_USD_SDK
		return Impl->PxrSdfLayer.Get()->SetFramesPerSecond( FramesPerSecond );
#endif // #if USE_USD_SDK
	}

	TArray< FString > FSdfLayer::GetSubLayerPaths() const
	{
		TArray< FString > SubLayerPaths;

#if USE_USD_SDK
		FScopedUsdAllocs UsdAllocs;

		for ( const std::string& SubLayerPath : Impl->PxrSdfLayer.Get()->GetSubLayerPaths() )
		{
			SubLayerPaths.Emplace( ANSI_TO_TCHAR( SubLayerPath.c_str() ) );
		}
#endif // #if USE_USD_SDK

		return SubLayerPaths;
	}

	TArray< FSdfLayerOffset > FSdfLayer::GetSubLayerOffsets() const
	{
		TArray< FSdfLayerOffset > SubLayerOffsets;

#if USE_USD_SDK
		FScopedUsdAllocs UsdAllocs;

		for ( const pxr::SdfLayerOffset& SubLayerOffset : Impl->PxrSdfLayer.Get()->GetSubLayerOffsets() )
		{
			if ( SubLayerOffset.IsValid() )
			{
				SubLayerOffsets.Emplace( SubLayerOffset.GetOffset(), SubLayerOffset.GetScale() );
			}
			else
			{
				SubLayerOffsets.AddDefaulted();
			}
		}
#endif // #if USE_USD_SDK

		return SubLayerOffsets;
	}

	void FSdfLayer::SetSubLayerOffset( const FSdfLayerOffset& LayerOffset, int32 Index )
	{
#if USE_USD_SDK
		FScopedUsdAllocs UsdAllocs;

		pxr::SdfLayerOffset UsdLayerOffset( LayerOffset.Offset, LayerOffset.Scale );
		Impl->PxrSdfLayer.Get()->SetSubLayerOffset( MoveTemp( UsdLayerOffset ), Index );
#endif // #if USE_USD_SDK
	}

	bool FSdfLayer::HasSpec( const FSdfPath& Path ) const
	{
#if USE_USD_SDK
		return Impl->PxrSdfLayer.Get()->HasSpec( Path );
#else
		return false;
#endif // #if USE_USD_SDK
	}

	TSet< double > FSdfLayer::ListTimeSamplesForPath( const FSdfPath& Path ) const
	{
		TSet< double > TimeSamples;

#if USE_USD_SDK
		FScopedUsdAllocs UsdAllocs;

		std::set< double > UsdTimeSamples = Impl->PxrSdfLayer.Get()->ListTimeSamplesForPath( Path );

		for ( double UsdTimeSample : UsdTimeSamples )
		{
			TimeSamples.Add( UsdTimeSample );
		}
#endif // #if USE_USD_SDK

		return TimeSamples;
	}

	void FSdfLayer::EraseTimeSample( const FSdfPath& Path, double Time )
	{
#if USE_USD_SDK
		return Impl->PxrSdfLayer.Get()->EraseTimeSample( Path, Time );
#endif // #if USE_USD_SDK
	}

	bool FSdfLayer::IsEmpty() const
	{
#if USE_USD_SDK
		return Impl->PxrSdfLayer.Get()->IsEmpty();
#else
		return false;
#endif // #if USE_USD_SDK
	}

	bool FSdfLayer::IsAnonymous() const
	{
#if USE_USD_SDK
		return Impl->PxrSdfLayer.Get()->IsAnonymous();
#else
		return false;
#endif // #if USE_USD_SDK
	}

	bool FSdfLayer::Export( const TCHAR* Filename ) const
	{
#if USE_USD_SDK
		return Impl->PxrSdfLayer.Get()->Export( TCHAR_TO_ANSI( Filename ) );
#else
		return false;
#endif // #if USE_USD_SDK
	}

	FString FSdfLayerUtils::SdfComputeAssetPathRelativeToLayer( const FSdfLayer& Anchor, const TCHAR* AssetPath )
	{
#if USE_USD_SDK
		FScopedUsdAllocs UsdAllocs;
		return FString( ANSI_TO_TCHAR( pxr::SdfComputeAssetPathRelativeToLayer( pxr::SdfLayerRefPtr( Anchor ), TCHAR_TO_ANSI( AssetPath ) ).c_str() ) );
#else
		return FString();
#endif // #if USE_USD_SDK
	}

	bool FSdfLayer::IsMuted() const
	{
#if USE_USD_SDK
		return Impl->PxrSdfLayer.Get()->IsMuted();
#else
		return false;
#endif // #if USE_USD_SDK
	}

	void FSdfLayer::SetMuted( bool bMuted )
	{
#if USE_USD_SDK
		return Impl->PxrSdfLayer.Get()->SetMuted( bMuted );
#endif // #if USE_USD_SDK
	}
}
