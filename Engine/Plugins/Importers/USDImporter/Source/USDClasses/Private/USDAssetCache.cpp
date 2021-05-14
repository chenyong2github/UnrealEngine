// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDAssetCache.h"

#include "Misc/ScopeLock.h"

//#include "USDLog.h"

UUsdAssetCache::UUsdAssetCache()
	: bAllowPersistentStorage( true )
{
}

#if WITH_EDITOR
void UUsdAssetCache::PostEditChangeProperty( FPropertyChangedEvent& PropertyChangedEvent )
{
	if ( PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED( UUsdAssetCache, bAllowPersistentStorage ) )
	{
		if ( !bAllowPersistentStorage )
		{
			TransientStorage.Append( PersistentStorage );
			PersistentStorage.Empty();
		}
	}
}
#endif // #if WITH_EDITOR

void UUsdAssetCache::CacheAsset( const FString& Hash, UObject* Asset, const FString& PrimPath /*= FString() */ )
{
	if ( !Asset )
	{
		//UE_LOG( LogUsd, Warning, TEXT( "Attempted to add a null asset to USD Asset Cache with hash '%s' and PrimPath '%s'!" ), *Hash, *PrimPath );
		return;
	}

	FScopeLock Lock( &CriticalSection );

	if ( !bAllowPersistentStorage || Asset->HasAnyFlags( RF_Transient ) || Asset->GetOutermost() == GetTransientPackage() )
	{
		TransientStorage.Add( Hash, Asset );
	}
	else
	{
		Modify();
		PersistentStorage.Add( Hash, Asset );
	}

	OwnedAssets.Add( Asset );
	if ( !PrimPath.IsEmpty() )
	{
		PrimPathToAssets.Add( PrimPath, Asset );
	}

	ActiveAssets.Add(Asset);
}

void UUsdAssetCache::DiscardAsset( const FString& Hash )
{
	FScopeLock Lock( &CriticalSection );

	UObject** FoundObject = TransientStorage.Find( Hash );

	if ( !FoundObject )
	{
		FoundObject = PersistentStorage.Find( Hash );

		if ( FoundObject )
		{
			Modify();
		}
	}

	if ( FoundObject )
	{
		for ( TMap< FString, UObject* >::TIterator PrimPathToAssetIt = PrimPathToAssets.CreateIterator(); PrimPathToAssetIt; ++PrimPathToAssetIt )
		{
			if ( *FoundObject == PrimPathToAssetIt.Value() )
			{
				PrimPathToAssetIt.RemoveCurrent();
			}
		}

		ActiveAssets.Remove( *FoundObject );
		TransientStorage.Remove( Hash );
		PersistentStorage.Remove( Hash );
		OwnedAssets.Remove( *FoundObject );
	}
}

UObject* UUsdAssetCache::GetCachedAsset( const FString& Hash ) const
{
	FScopeLock Lock( &CriticalSection );

	UObject* const* FoundObject = TransientStorage.Find( Hash );

	if ( !FoundObject )
	{
		FoundObject = PersistentStorage.Find( Hash );
	}

	if ( FoundObject )
	{
		ActiveAssets.Add( *FoundObject );
		return *FoundObject;
	}

	return nullptr;
}

TMap< FString, UObject* > UUsdAssetCache::GetCachedAssets() const
{
	TMap< FString, UObject* > CachedAssets( TransientStorage );
	CachedAssets.Append( PersistentStorage );

	return CachedAssets;
}

void UUsdAssetCache::LinkAssetToPrim( const FString& PrimPath, UObject* Asset )
{
	if ( !Asset )
	{
		return;
	}

	if ( !OwnedAssets.Contains( Asset ) )
	{
		//UE_LOG( LogUsd, Warning, TEXT( "Tried to set prim path '%s' to asset '%s', but it is not currently owned by the USD stage cache!" ), *PrimPath, *Asset->GetName() );
		return;
	}

	FScopeLock Lock( &CriticalSection );

	PrimPathToAssets.Add( PrimPath, Asset );
}

void UUsdAssetCache::RemoveAssetPrimLink( const FString& PrimPath )
{
	FScopeLock Lock( &CriticalSection );

	PrimPathToAssets.Remove( PrimPath );
}

UObject* UUsdAssetCache::GetAssetForPrim( const FString& PrimPath ) const
{
	FScopeLock Lock( &CriticalSection );

	if ( UObject* const* FoundObject = PrimPathToAssets.Find( PrimPath ) )
	{
		ActiveAssets.Add( *FoundObject );
		return *FoundObject;
	}

	return nullptr;
}

void UUsdAssetCache::Reset()
{
	FScopeLock Lock( &CriticalSection );

	Modify();

	TransientStorage.Reset();
	OwnedAssets.Reset();
	PrimPathToAssets.Reset();
	ActiveAssets.Reset();
	PersistentStorage.Reset();
}

void UUsdAssetCache::MarkAssetsAsStale()
{
	FScopeLock Lock( &CriticalSection );

	ActiveAssets.Reset();
}

TSet<UObject*> UUsdAssetCache::GetActiveAssets() const
{
	return ActiveAssets;
}

void UUsdAssetCache::Serialize( FArchive& Ar )
{
	FScopeLock Lock( &CriticalSection );

	Super::Serialize( Ar );

	if ( Ar.GetPortFlags() & PPF_DuplicateForPIE )
	{
		Ar << TransientStorage;
		Ar << ActiveAssets;
	}
}
