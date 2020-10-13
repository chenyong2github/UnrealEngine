// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDListener.h"

#include "USDMemory.h"

#include "UsdWrappers/UsdStage.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"

#include "pxr/base/tf/weakBase.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/sdf/notice.h"
#include "pxr/usd/usd/notice.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/tokens.h"

#include "USDIncludesEnd.h"

#endif // USE_USD_SDK


class FUsdListenerImpl
#if USE_USD_SDK
	: public pxr::TfWeakBase
#endif // #if USE_USD_SDK
{
public:
	FUsdListenerImpl() = default;

	virtual ~FUsdListenerImpl();

	FUsdListener::FOnStageEditTargetChanged OnStageEditTargetChanged;
	FUsdListener::FOnPrimsChanged OnPrimsChanged;
	FUsdListener::FOnStageInfoChanged OnStageInfoChanged;
	FUsdListener::FOnLayersChanged OnLayersChanged;

	FThreadSafeCounter IsBlocked;

#if USE_USD_SDK
	FUsdListenerImpl( const pxr::UsdStageRefPtr& Stage );

	void Register( const pxr::UsdStageRefPtr& Stage );

protected:
	void HandleUsdNotice( const pxr::UsdNotice::ObjectsChanged& Notice, const pxr::UsdStageWeakPtr& Sender );
	void HandleStageEditTargetChangedNotice( const pxr::UsdNotice::StageEditTargetChanged& Notice, const pxr::UsdStageWeakPtr& Sender );
	void HandleLayersChangedNotice ( const pxr::SdfNotice::LayersDidChange& Notice );

private:
	pxr::TfNotice::Key RegisteredObjectsChangedKey;
	pxr::TfNotice::Key RegisteredStageEditTargetChangedKey;
	pxr::TfNotice::Key RegisteredLayersChangedKey;
#endif // #if USE_USD_SDK
};

FUsdListener::FUsdListener()
	: Impl( MakeUnique< FUsdListenerImpl >() )
{
}

FUsdListener::FUsdListener( const UE::FUsdStage& Stage )
	: FUsdListener()
{
	Register( Stage );
}

FUsdListener::~FUsdListener() = default;

void FUsdListener::Register( const UE::FUsdStage& Stage )
{
#if USE_USD_SDK
	Impl->Register( Stage );
#endif // #if USE_USD_SDK
}

void FUsdListener::Block()
{
	Impl->IsBlocked.Increment();
}

void FUsdListener::Unblock()
{
	Impl->IsBlocked.Decrement();
}

bool FUsdListener::IsBlocked() const
{
	return Impl->IsBlocked.GetValue() > 0;
}

FUsdListener::FOnStageEditTargetChanged& FUsdListener::GetOnStageEditTargetChanged()
{
	return Impl->OnStageEditTargetChanged;
}

FUsdListener::FOnPrimsChanged& FUsdListener::GetOnPrimsChanged()
{
	return Impl->OnPrimsChanged;
}

FUsdListener::FOnStageInfoChanged& FUsdListener::GetOnStageInfoChanged()
{
	return Impl->OnStageInfoChanged;
}

FUsdListener::FOnLayersChanged& FUsdListener::GetOnLayersChanged()
{
	return Impl->OnLayersChanged;
}

#if USE_USD_SDK
void FUsdListenerImpl::Register( const pxr::UsdStageRefPtr& Stage )
{
	FScopedUsdAllocs UsdAllocs;

	if ( RegisteredObjectsChangedKey.IsValid() )
	{
		pxr::TfNotice::Revoke( RegisteredObjectsChangedKey );
	}

	RegisteredObjectsChangedKey = pxr::TfNotice::Register( pxr::TfWeakPtr< FUsdListenerImpl >( this ), &FUsdListenerImpl::HandleUsdNotice, Stage );

	if ( RegisteredStageEditTargetChangedKey.IsValid() )
	{
		pxr::TfNotice::Revoke( RegisteredStageEditTargetChangedKey );
	}

	RegisteredStageEditTargetChangedKey = pxr::TfNotice::Register( pxr::TfWeakPtr< FUsdListenerImpl >( this ), &FUsdListenerImpl::HandleStageEditTargetChangedNotice, Stage );

	if (RegisteredLayersChangedKey.IsValid())
	{
		pxr::TfNotice::Revoke(RegisteredLayersChangedKey);
	}

	RegisteredLayersChangedKey = pxr::TfNotice::Register( pxr::TfWeakPtr< FUsdListenerImpl >( this ), &FUsdListenerImpl::HandleLayersChangedNotice );
}
#endif // #if USE_USD_SDK

FUsdListenerImpl::~FUsdListenerImpl()
{
#if USE_USD_SDK
	FScopedUsdAllocs UsdAllocs;
	pxr::TfNotice::Revoke( RegisteredObjectsChangedKey );
	pxr::TfNotice::Revoke( RegisteredStageEditTargetChangedKey );
	pxr::TfNotice::Revoke( RegisteredLayersChangedKey );
#endif // #if USE_USD_SDK
}

#if USE_USD_SDK
void FUsdListenerImpl::HandleUsdNotice( const pxr::UsdNotice::ObjectsChanged& Notice, const pxr::UsdStageWeakPtr& Sender )
{
	using namespace pxr;
	using PathRange = UsdNotice::ObjectsChanged::PathRange;

	if ( (!OnPrimsChanged.IsBound() && !OnStageInfoChanged.IsBound()) || IsBlocked.GetValue() > 0 )
	{
		return;
	}

	TMap< FString, bool > PrimsChangedList;
	TArray< FString > StageChangedFields;

	FScopedUsdAllocs UsdAllocs;

	PathRange PathsToUpdate = Notice.GetResyncedPaths();
	for ( PathRange::const_iterator It = PathsToUpdate.begin(); It != PathsToUpdate.end(); ++It )
	{
		constexpr bool bResync = true;
		PrimsChangedList.Add( ANSI_TO_TCHAR( It->GetAbsoluteRootOrPrimPath().GetString().c_str() ), bResync );
	}

	PathsToUpdate = Notice.GetChangedInfoOnlyPaths();
	for ( PathRange::const_iterator  PathToUpdateIt = PathsToUpdate.begin(); PathToUpdateIt != PathsToUpdate.end(); ++PathToUpdateIt )
	{
		const FString PrimPath = ANSI_TO_TCHAR( PathToUpdateIt->GetAbsoluteRootOrPrimPath().GetString().c_str() );

		if ( PrimsChangedList.Contains( PrimPath ) )
		{
			continue;
		}

		bool bResync = false;

		// If the layer reloaded, anything could have happened, so we must resync from the layer down
		const std::vector<const SdfChangeList::Entry*>& Changes = PathToUpdateIt.base()->second;
		for ( const SdfChangeList::Entry* Change : Changes )
		{
			if ( Change && Change->flags.didReloadContent )
			{
				bResync = true;
			}
		}

		// Change on the stage root
		bool bIsRootLayer = PathToUpdateIt->GetAbsoluteRootOrPrimPath() == pxr::SdfPath::AbsoluteRootPath();
		if ( bIsRootLayer )
		{
			pxr::TfTokenVector ChangedFields = PathToUpdateIt.GetChangedFields();

			for ( const pxr::TfToken& ChangedField : ChangedFields )
			{
				StageChangedFields.Add( ANSI_TO_TCHAR( ChangedField.GetString().c_str() ) );

				if ( ChangedField == UsdGeomTokens->metersPerUnit )
				{
					bResync = true; // Force a resync when changing the metersPerUnit since it affects all coordinates
					break;
				}
			}
		}

		// If the change is just about the stage updating some info like startTimeSeconds or framesPerSecond, then we don't
		// need to refresh all prims. This is important because when undo/redoing, we may resync the movie scene, which shouldn't trigger prim spawning
		if ( !bIsRootLayer || bResync )
		{
			PrimsChangedList.Add( PrimPath, bResync );
		}
	}

	if ( PrimsChangedList.Num() > 0 )
	{
		FScopedUnrealAllocs UnrealAllocs;
		OnPrimsChanged.Broadcast( PrimsChangedList );
	}

	if ( StageChangedFields.Num() > 0 )
	{
		FScopedUnrealAllocs UnrealAllocs;
		OnStageInfoChanged.Broadcast( StageChangedFields );
	}
}

void FUsdListenerImpl::HandleStageEditTargetChangedNotice( const pxr::UsdNotice::StageEditTargetChanged& Notice, const pxr::UsdStageWeakPtr& Sender )
{
	FScopedUnrealAllocs UnrealAllocs;
	OnStageEditTargetChanged.Broadcast();
}

void FUsdListenerImpl::HandleLayersChangedNotice( const pxr::SdfNotice::LayersDidChange& Notice )
{
	if ( !OnLayersChanged.IsBound() || IsBlocked.GetValue() > 0 )
	{
		return;
	}

	TArray< FString > LayersNames;

	FScopedUsdAllocs UsdAllocs;
	[&](const pxr::SdfLayerChangeListVec& ChangeVec)
	{
		// Check to see if any layer reloaded. If so, rebuild all of our animations as a single layer changing
		// might propagate timecodes through all level sequences
		for (const std::pair<pxr::SdfLayerHandle, pxr::SdfChangeList>& ChangeVecItem : ChangeVec)
		{
			const pxr::SdfChangeList::EntryList& ChangeList = ChangeVecItem.second.GetEntryList();
			for (const std::pair<pxr::SdfPath, pxr::SdfChangeList::Entry>& Change : ChangeList)
			{
				for (const pxr::SdfChangeList::Entry::SubLayerChange& SubLayerChange : Change.second.subLayerChanges)
				{
					const pxr::SdfChangeList::SubLayerChangeType ChangeType = SubLayerChange.second;
					if (ChangeType == pxr::SdfChangeList::SubLayerChangeType::SubLayerAdded ||
						ChangeType == pxr::SdfChangeList::SubLayerChangeType::SubLayerRemoved)
					{
						LayersNames.Add( ANSI_TO_TCHAR( SubLayerChange.first.c_str() ) );
					}
				}

				const pxr::SdfChangeList::Entry::_Flags& Flags = Change.second.flags;
				if (Flags.didReloadContent)
				{
					LayersNames.Add( ANSI_TO_TCHAR( Change.first.GetString().c_str() ) );
				}
			}
		}
	}( Notice.GetChangeListVec() );

	FScopedUnrealAllocs UnrealAllocs;
	OnLayersChanged.Broadcast( LayersNames );
}
#endif // #if USE_USD_SDK

FScopedBlockNotices::FScopedBlockNotices( FUsdListener& InListener )
	: Listener( InListener )
{
	Listener.Block();
}

FScopedBlockNotices::~FScopedBlockNotices()
{
	Listener.Unblock();
}
