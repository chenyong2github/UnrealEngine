// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "USDListener.h"

#include "USDMemory.h"
#include "USDTypesConversion.h"

#if USE_USD_SDK

FUsdListener::FUsdListener( const pxr::UsdStageRefPtr& Stage )
{
	Register( Stage );
}

void FUsdListener::Register( const pxr::UsdStageRefPtr& Stage )
{
	FScopedUsdAllocs UsdAllocs;

	if ( RegisteredObjectsChangedKey.IsValid() )
	{
		pxr::TfNotice::Revoke( RegisteredObjectsChangedKey );
	}

	RegisteredObjectsChangedKey = pxr::TfNotice::Register( pxr::TfWeakPtr< FUsdListener >( this ), &FUsdListener::HandleUsdNotice, Stage );

	if ( RegisteredStageEditTargetChangedKey.IsValid() )
	{
		pxr::TfNotice::Revoke( RegisteredStageEditTargetChangedKey );
	}

	RegisteredStageEditTargetChangedKey = pxr::TfNotice::Register( pxr::TfWeakPtr< FUsdListener >( this ), &FUsdListener::HandleStageEditTargetChangedNotice, Stage );
}

FUsdListener::~FUsdListener()
{
	FScopedUsdAllocs UsdAllocs;
	pxr::TfNotice::Revoke( RegisteredObjectsChangedKey );
	pxr::TfNotice::Revoke( RegisteredStageEditTargetChangedKey );
}

void FUsdListener::HandleUsdNotice( const pxr::UsdNotice::ObjectsChanged& Notice, const pxr::UsdStageWeakPtr& Sender )
{
	using namespace pxr;
	using PathRange = UsdNotice::ObjectsChanged::PathRange;

	if ( !OnPrimChanged.IsBound() || IsBlocked.GetValue() > 0 )
	{
		return;
	}

	PathRange PathsToUpdate = Notice.GetResyncedPaths();

	for ( PathRange::const_iterator It = PathsToUpdate.begin(); It != PathsToUpdate.end(); ++It )
	{
		//FPlatformMisc::LowLevelOutputDebugStringf( TEXT("Prim %s changed (resync)\n"), *UsdToUnreal::ConvertPath( *It ) );

		FScopedUnrealAllocs UnrealAllocs;

		constexpr bool bResync = true;
		OnPrimChanged.Broadcast( UsdToUnreal::ConvertPath( *It ), bResync );
	}

	PathsToUpdate = Notice.GetChangedInfoOnlyPaths();

	for ( PathRange::const_iterator It = PathsToUpdate.begin(); It != PathsToUpdate.end(); ++It )
	{
		//FPlatformMisc::LowLevelOutputDebugStringf( TEXT("Prim %s changed (update)\n"), *UsdToUnreal::ConvertPath( *It ) );

		FScopedUnrealAllocs UnrealAllocs;

		constexpr bool bResync = false;
		OnPrimChanged.Broadcast( UsdToUnreal::ConvertPath( *It ), bResync );
	}
}

void FUsdListener::HandleStageEditTargetChangedNotice( const pxr::UsdNotice::StageEditTargetChanged& Notice, const pxr::UsdStageWeakPtr& Sender )
{
	FScopedUnrealAllocs UnrealAllocs;
	OnStageEditTargetChanged.Broadcast();
}

#endif // #if USE_USD_SDK
