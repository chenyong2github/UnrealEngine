// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDListener.h"

#include "USDLog.h"
#include "USDMemory.h"

#include "UsdWrappers/UsdStage.h"
#include "UsdWrappers/VtValue.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"

#include "pxr/base/tf/weakBase.h"
#include "pxr/usd/sdf/changeList.h"
#include "pxr/usd/sdf/notice.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/notice.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/tokens.h"

#include "USDIncludesEnd.h"

#endif // USE_USD_SDK

namespace UsdToUnreal
{
#if USE_USD_SDK
	bool CollectFieldChanges( const pxr::UsdNotice::ObjectsChanged::PathRange& InfoChanges, UsdUtils::FUsdFieldValueMap& OutOldValues, UsdUtils::FUsdFieldValueMap& OutNewValues )
	{
		using namespace pxr;
		using PathRange = UsdNotice::ObjectsChanged::PathRange;
		using InfoChange = std::pair<VtValue, VtValue>;

		for ( PathRange::const_iterator It = InfoChanges.begin(); It != InfoChanges.end(); ++It )
		{
			// Something like "/Root/Prim.some_field"
			const TCHAR* FullFieldPath = ANSI_TO_TCHAR( It->GetString().c_str() );

			const std::vector<const SdfChangeList::Entry*>& Changes = It.base()->second;
			for ( const SdfChangeList::Entry* Entry : Changes )
			{
				// For most changes we'll only get one of these, but sometimes multiple changes are fired in sequence
				// (e.g. if you change framesPerSecond, it will send a notice for it but also for the matching, updated timeCodesPerSecond)
				for ( const std::pair<TfToken, InfoChange>& Change : Entry->infoChanged )
				{
					FString CombinedPath;

					// For regular properties (most common case) FullFieldPath will already carry the property and FieldToken will be "default", "timeSamples", "variability", etc.
					const TCHAR* FieldToken = ANSI_TO_TCHAR( Change.first.GetString().c_str() );
					if ( It->IsPropertyPath() )
					{
						CombinedPath = FString::Printf( TEXT( "%s.%s" ), FullFieldPath, FieldToken );
					}
					// For stage properties it seems like USD likes to send just "/" as the FullFieldPath, and the actual property name in FieldToken (e.g. "metersPerUnit", "timeCodesPerSecond", etc.)
					// We send "default" here for consistency, so that our consumer code can always strip one suffix and expect a property path
					else if ( It->IsAbsoluteRootPath() || FieldToken == FString(TEXT( "kind" )) )
					{
						CombinedPath = FString::Printf( TEXT( "%s.%s.default" ), FullFieldPath, FieldToken );
					}
					else
					{
						UE_LOG( LogUsd, Warning, TEXT( "Failed to process a USD field notice for field '%s' and path '%s'" ),
							FieldToken,
							FullFieldPath
						);
						continue;
					}

					if ( OutOldValues.Contains( CombinedPath ) )
					{
						UE_LOG( LogUsd, Warning, TEXT( "Overwriting existing old value for field '%s'!" ), *CombinedPath );
					}
					OutOldValues.Add( CombinedPath, UE::FVtValue{ Change.second.first } );

					if ( OutNewValues.Contains( CombinedPath ) )
					{
						UE_LOG( LogUsd, Warning, TEXT( "Overwriting existing new value for field '%s'!" ), *CombinedPath );
					}
					OutNewValues.Add( CombinedPath, UE::FVtValue{ Change.second.second } );
				}
			}
		}

		return true;
	}
#endif // USE_USD_SDK
}

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
	FUsdListener::FOnFieldsChanged OnFieldsChanged;

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

FUsdListener::FOnFieldsChanged& FUsdListener::GetOnFieldsChanged()
{
	return Impl->OnFieldsChanged;
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
void FUsdListenerImpl::HandleUsdNotice( const pxr::UsdNotice::ObjectsChanged & Notice, const pxr::UsdStageWeakPtr & Sender )
{
	using namespace pxr;
	using PathRange = UsdNotice::ObjectsChanged::PathRange;

	if ( !OnPrimsChanged.IsBound() && !OnStageInfoChanged.IsBound() && !OnFieldsChanged.IsBound() )
	{
		return;
	}

	// Temp: We always want to emit this one as we just use this notice to keep track of USD stage changes, for undo/redo and multi-user
	UsdUtils::FUsdFieldValueMap OldValues;
	UsdUtils::FUsdFieldValueMap NewValues;
	UsdToUnreal::CollectFieldChanges( Notice.GetChangedInfoOnlyPaths(), OldValues, NewValues );
	UsdToUnreal::CollectFieldChanges( Notice.GetResyncedPaths(), OldValues, NewValues );
	if ( OldValues.Num() > 0 || NewValues.Num() > 0 )
	{
		FScopedUnrealAllocs UnrealAllocs;
		OnFieldsChanged.Broadcast( OldValues, NewValues );
	}

	if ( IsBlocked.GetValue() > 0 )
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

				// Force a resync when changing these since it affects all coordinates (the transforms they had when collapsed, etc.)
				if ( ChangedField == UsdGeomTokens->metersPerUnit || ChangedField == UsdGeomTokens->upAxis )
				{
					bResync = true;
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
