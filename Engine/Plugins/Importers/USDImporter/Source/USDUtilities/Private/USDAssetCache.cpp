// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDAssetCache.h"

#include "Misc/ScopeLock.h"

#include "USDLog.h"

FUsdAssetCache::FUsdAssetCache( TMap< FString, UObject* >& InHashToAssets )
{
	HashToAssets = InHashToAssets;

	TArray<UObject*> Values;
	HashToAssets.GenerateValueArray(Values);
	OwnedAssets = TSet<UObject*>{ Values };
}

void FUsdAssetCache::AddReferencedObjects( FReferenceCollector& Collector )
{
	Collector.AddReferencedObjects( HashToAssets );
}

void FUsdAssetCache::CacheAsset( const FString& Hash, UObject* Asset, const FString& PrimPath /*= FString() */ )
{
	if ( !Asset )
	{
		UE_LOG( LogUsd, Warning, TEXT( "Attempted to add to FUsdAssetCache a nullptr asset with hash '%s' and PrimPath '%s'!" ), *Hash, *PrimPath );
		return;
	}

	FScopeLock Lock( &CriticalSection );

	HashToAssets.Add( Hash, Asset );
	OwnedAssets.Add( Asset );
	if ( !PrimPath.IsEmpty() )
	{
		PrimPathToAssets.Add( PrimPath, Asset );
	}

	ActiveAssets.Add(Asset);
}

void FUsdAssetCache::DiscardAsset( const FString& Hash )
{
	FScopeLock Lock( &CriticalSection );

	if ( UObject** FoundObject = HashToAssets.Find( Hash ) )
	{
		for ( TMap< FString, UObject* >::TIterator PrimPathToAssetIt = PrimPathToAssets.CreateIterator(); PrimPathToAssetIt; ++PrimPathToAssetIt )
		{
			if ( *FoundObject == PrimPathToAssetIt.Value() )
			{
				PrimPathToAssetIt.RemoveCurrent();
			}
		}

		ActiveAssets.Remove( *FoundObject );
		HashToAssets.Remove( Hash );
		OwnedAssets.Remove( *FoundObject );
	}
}

UObject* FUsdAssetCache::GetCachedAsset( const FString& Hash ) const
{
	FScopeLock Lock( &CriticalSection );

	if ( UObject* const* FoundObject = HashToAssets.Find( Hash ) )
	{
		ActiveAssets.Add( *FoundObject );
		return *FoundObject;
	}

	return nullptr;
}

void FUsdAssetCache::LinkAssetToPrim( const FString& PrimPath, UObject* Asset )
{
	if ( !Asset )
	{
		return;
	}

	if ( !OwnedAssets.Contains( Asset ) )
	{
		UE_LOG( LogUsd, Warning, TEXT( "Tried to set prim path '%s' to asset '%s', but it is not currently owned by the USD stage cache!" ), *PrimPath, *Asset->GetName() );
		return;
	}

	FScopeLock Lock( &CriticalSection );

	PrimPathToAssets.Add( PrimPath, Asset );
}

void FUsdAssetCache::RemoveAssetPrimLink( const FString& PrimPath )
{
	FScopeLock Lock( &CriticalSection );

	PrimPathToAssets.Remove( PrimPath );
}

UObject* FUsdAssetCache::GetAssetForPrim( const FString& PrimPath ) const
{
	FScopeLock Lock( &CriticalSection );

	if ( UObject* const* FoundObject = PrimPathToAssets.Find( PrimPath ) )
	{
		ActiveAssets.Add( *FoundObject );
		return *FoundObject;
	}

	return nullptr;
}

void FUsdAssetCache::Reset()
{
	FScopeLock Lock( &CriticalSection );

	HashToAssets.Reset();
	OwnedAssets.Reset();
	PrimPathToAssets.Reset();
	ActiveAssets.Reset();
}

void FUsdAssetCache::MarkAssetsAsStale()
{
	FScopeLock Lock( &CriticalSection );

	ActiveAssets.Reset();
}

TSet<UObject*> FUsdAssetCache::GetActiveAssets() const
{
	return ActiveAssets;
}

FArchive& operator<<( FArchive& Ar, FUsdAssetCache& AssetCache )
{
	FScopeLock Lock( &AssetCache.CriticalSection );

	Ar << AssetCache.HashToAssets;
	Ar << AssetCache.OwnedAssets;
	Ar << AssetCache.PrimPathToAssets;
	Ar << AssetCache.ActiveAssets;

	return Ar;
}
