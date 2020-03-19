// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDListener.h"

#include "USDMemory.h"
#include "USDTypesConversion.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"

#include "pxr/usd/usdGeom/tokens.h"

#include "USDIncludesEnd.h"

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

	if (RegisteredLayersChangedKey.IsValid())
	{
		pxr::TfNotice::Revoke(RegisteredLayersChangedKey);
	}

	RegisteredLayersChangedKey = pxr::TfNotice::Register(pxr::TfWeakPtr< FUsdListener >( this ), &FUsdListener::HandleLayersChangedNotice );
}

FUsdListener::~FUsdListener()
{
	FScopedUsdAllocs UsdAllocs;
	pxr::TfNotice::Revoke( RegisteredObjectsChangedKey );
	pxr::TfNotice::Revoke( RegisteredStageEditTargetChangedKey );
	pxr::TfNotice::Revoke( RegisteredLayersChangedKey );
}

void FUsdListener::HandleUsdNotice( const pxr::UsdNotice::ObjectsChanged& Notice, const pxr::UsdStageWeakPtr& Sender )
{
	using namespace pxr;
	using PathRange = UsdNotice::ObjectsChanged::PathRange;

	if ( !OnPrimsChanged.IsBound() || IsBlocked.GetValue() > 0 )
	{
		return;
	}

	TMap< FString, bool > PrimsChangedList;

	FScopedUsdAllocs UsdAllocs;

	PathRange PathsToUpdate = Notice.GetResyncedPaths();

	for ( PathRange::const_iterator It = PathsToUpdate.begin(); It != PathsToUpdate.end(); ++It )
	{
		constexpr bool bResync = true;
		PrimsChangedList.Add( UsdToUnreal::ConvertPath( It->GetAbsoluteRootOrPrimPath() ), bResync );
	}

	PathsToUpdate = Notice.GetChangedInfoOnlyPaths();

	for ( PathRange::const_iterator  PathToUpdateIt = PathsToUpdate.begin(); PathToUpdateIt != PathsToUpdate.end(); ++PathToUpdateIt )
	{
		const FString PrimPath = UsdToUnreal::ConvertPath( PathToUpdateIt->GetAbsoluteRootOrPrimPath() );

		if ( !PrimsChangedList.Contains( PrimPath ) )
		{
			bool bResync = false;

			if ( PathToUpdateIt->GetAbsoluteRootOrPrimPath() == pxr::SdfPath::AbsoluteRootPath() )
			{
				pxr::TfTokenVector ChangedFields = PathToUpdateIt.GetChangedFields();

				for ( const pxr::TfToken& ChangeField : ChangedFields )
				{
					if ( ChangeField == UsdGeomTokens->metersPerUnit )
					{
						bResync = true; // Force a resync when changing the metersPerUnit since it affects all coordinates
						break;
					}
				}
			}

			PrimsChangedList.Add( PrimPath, bResync );
		}
	}

	if ( PrimsChangedList.Num() > 0 )
	{
		FScopedUnrealAllocs UnrealAllocs;
		OnPrimsChanged.Broadcast( PrimsChangedList );
	}
}

void FUsdListener::HandleStageEditTargetChangedNotice( const pxr::UsdNotice::StageEditTargetChanged& Notice, const pxr::UsdStageWeakPtr& Sender )
{
	FScopedUnrealAllocs UnrealAllocs;
	OnStageEditTargetChanged.Broadcast();
}

void FUsdListener::HandleLayersChangedNotice(const pxr::SdfNotice::LayersDidChange& Notice)
{
	if ( !OnLayersChanged.IsBound() || IsBlocked.GetValue() > 0 )
	{
		return;
	}

	OnLayersChanged.Broadcast(Notice.GetChangeListVec());
}

#endif // #if USE_USD_SDK
