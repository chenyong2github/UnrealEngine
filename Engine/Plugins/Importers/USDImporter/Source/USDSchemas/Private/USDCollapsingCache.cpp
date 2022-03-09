// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDCollapsingCache.h"

#include "USDSchemasModule.h"
#include "USDSchemaTranslator.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdPrim.h"

#include "Async/ParallelFor.h"
#include "Modules/ModuleManager.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"
	#include "pxr/usd/sdf/path.h"
	#include "pxr/usd/usd/prim.h"
#include "USDIncludesEnd.h"
#endif // USE_USD_SDK

bool FUsdCollapsingCache::Serialize( FArchive& Ar )
{
	FWriteScopeLock ScopeLock( Lock );
	Ar << AssetPathsToCollapsedRoot;
	Ar << ComponentPathsToCollapsedRoot;

	return true;
}

bool FUsdCollapsingCache::IsPathCollapsed( const UE::FSdfPath& Path, ECollapsingType CollapsingType ) const
{
	FReadScopeLock ScopeLock( Lock );
	const TMap< UE::FSdfPath, UE::FSdfPath >* MapToUse =
		CollapsingType == ECollapsingType::Assets
			? &AssetPathsToCollapsedRoot
			: &ComponentPathsToCollapsedRoot;

	if ( const UE::FSdfPath* FoundResult = MapToUse->Find( Path ) )
	{
		// A non-empty path to another prim means this prim is collapsed into that one
		return !FoundResult->IsEmpty() && ( *FoundResult ) != Path;
	}

	// This should never happen: We should have cached the entire tree
	ensureMsgf( false, TEXT( "Prim path '%s' has not been cached!" ), *Path.GetString() );
	return false;
}

bool FUsdCollapsingCache::DoesPathCollapseChildren( const UE::FSdfPath& Path, ECollapsingType CollapsingType ) const
{
	FReadScopeLock ScopeLock( Lock );
	const TMap< UE::FSdfPath, UE::FSdfPath >* MapToUse =
		CollapsingType == ECollapsingType::Assets
			? &AssetPathsToCollapsedRoot
			: &ComponentPathsToCollapsedRoot;

	if ( const UE::FSdfPath* FoundResult = MapToUse->Find( Path ) )
	{
		// We store our own Path in there when we collapse children. Otherwise we hold the path of our collapse root, or empty (in case nothing is collapsed up to here)
		return (*FoundResult) == Path;
	}

	// This should never happen: We should have cached the entire tree
	ensureMsgf( false, TEXT( "Prim path '%s' has not been cached!" ), *Path.GetString() );
	return false;
}

UE::FSdfPath FUsdCollapsingCache::UnwindToNonCollapsedPath( const UE::FSdfPath& Path, ECollapsingType CollapsingType ) const
{
	FReadScopeLock ScopeLock( Lock );
	const TMap< UE::FSdfPath, UE::FSdfPath >* MapToUse =
		CollapsingType == ECollapsingType::Assets
			? &AssetPathsToCollapsedRoot
			: &ComponentPathsToCollapsedRoot;

	if ( const UE::FSdfPath* FoundResult = MapToUse->Find( Path ) )
	{
		// An empty path here means that we are not collapsed at all
		if ( FoundResult->IsEmpty() )
		{
			return Path;
		}
		// Otherwise we have our own path in there (in case we collapse children) or the path to the prim that collapsed us
		else
		{
			return *FoundResult;
		}
	}

	// This should never happen: We should have cached the entire tree
	ensureMsgf( false, TEXT( "Prim path '%s' has not been cached!" ), *Path.GetString() );
	return Path;
}

namespace UE::USDCollapsingCacheImpl::Private
{
#if USE_USD_SDK
	void RecursiveRebuildCache(
		const pxr::UsdPrim& UsdPrim,
		FUsdSchemaTranslationContext& Context,
		FUsdSchemaTranslatorRegistry& Registry,
		TMap< UE::FSdfPath, UE::FSdfPath >& AssetPathsToCollapsedRoot,
		TMap< UE::FSdfPath, UE::FSdfPath >& ComponentPathsToCollapsedRoot,
		FRWLock& Lock,
		const pxr::SdfPath& AssetCollapsedRoot = pxr::SdfPath::EmptyPath(),
		const pxr::SdfPath& ComponentCollapsedRoot = pxr::SdfPath::EmptyPath()
	)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE( RecursiveRebuildCache );
		FScopedUsdAllocs Allocs;

		pxr::SdfPath UsdPrimPath = UsdPrim.GetPrimPath();

		// Prevents allocation by referencing instead of copying
		const pxr::SdfPath* AssetCollapsedRootOverride = &AssetCollapsedRoot;
		const pxr::SdfPath* ComponentCollapsedRootOverride = &ComponentCollapsedRoot;

		bool bIsAssetCollapsed = !AssetCollapsedRoot.IsEmpty();
		bool bIsComponentCollapsed = !ComponentCollapsedRoot.IsEmpty();

		if ( !bIsAssetCollapsed || !bIsComponentCollapsed )
		{
			if ( TSharedPtr< FUsdSchemaTranslator > SchemaTranslator = Registry.CreateTranslatorForSchema( Context.AsShared(), UE::FUsdTyped( UsdPrim ) ) )
			{
				if ( !bIsAssetCollapsed )
				{
					if ( SchemaTranslator->CollapsesChildren( ECollapsingType::Assets ) )
					{
						AssetCollapsedRootOverride = &UsdPrimPath;
					}
				}

				if ( !bIsComponentCollapsed )
				{
					if ( SchemaTranslator->CollapsesChildren( ECollapsingType::Components ) )
					{
						ComponentCollapsedRootOverride = &UsdPrimPath;
					}
				}
			}
		}

		// These paths will be still empty in case nothing has collapsed yet, hold UsdPrimPath in case UsdPrim collapses that type, or hold the path to the
		// collapsed root passed in via our caller, in case we're collapsed
		{
			FWriteScopeLock ScopeLock(Lock);
			AssetPathsToCollapsedRoot.Emplace( UsdPrimPath, *AssetCollapsedRootOverride );
			ComponentPathsToCollapsedRoot.Emplace( UsdPrimPath, *ComponentCollapsedRootOverride );
		}

		pxr::UsdPrimSiblingRange PrimChildren = UsdPrim.GetFilteredChildren( pxr::UsdTraverseInstanceProxies( pxr::UsdPrimAllPrimsPredicate ) );
		
		TArray<pxr::UsdPrim> Prims;
		for ( pxr::UsdPrim Child : PrimChildren )
		{
			Prims.Emplace( Child );
		}

		ParallelFor(
			TEXT("RecursiveRebuildCache"),
			Prims.Num(), 1 /* MinBatchSize */,
			[&Prims, &Context, &Registry, &AssetPathsToCollapsedRoot, &ComponentPathsToCollapsedRoot, &Lock, &AssetCollapsedRoot, &ComponentCollapsedRoot](int32 Index)
			{
				RecursiveRebuildCache( Prims[Index], Context, Registry, AssetPathsToCollapsedRoot, ComponentPathsToCollapsedRoot, Lock, AssetCollapsedRoot, ComponentCollapsedRoot );
			}
		);
	}
#endif // USE_USD_SDK
}

void FUsdCollapsingCache::RebuildCacheForSubtree( const UE::FUsdPrim& Prim, FUsdSchemaTranslationContext& Context )
{
#if USE_USD_SDK
	TRACE_CPUPROFILER_EVENT_SCOPE( FUsdCollapsingCache::RebuildCacheForSubtree );

	// We can't deallocate our collapsing cache pointer with the Usd allocator
	FScopedUnrealAllocs UEAllocs;

	// We don't want the translation context to try using its collapsing cache during the rebuild process, as that's the entire point
	TSharedPtr<FUsdCollapsingCache> Pin = Context.CollapsingCache;
	Context.CollapsingCache = nullptr;
	{
		FScopedUsdAllocs Allocs;

		pxr::UsdPrim UsdPrim{Prim};
		if ( !UsdPrim )
		{
			return;
		}

		IUsdSchemasModule& UsdSchemasModule = FModuleManager::Get().LoadModuleChecked< IUsdSchemasModule >( TEXT( "USDSchemas" ) );
		FUsdSchemaTranslatorRegistry& Registry = UsdSchemasModule.GetTranslatorRegistry();

		UE::USDCollapsingCacheImpl::Private::RecursiveRebuildCache( UsdPrim, Context, Registry, AssetPathsToCollapsedRoot, ComponentPathsToCollapsedRoot, Lock );
	}
	Context.CollapsingCache = Pin;
#endif // USE_USD_SDK
}

void FUsdCollapsingCache::Clear()
{
	FWriteScopeLock ScopeLock( Lock );
	AssetPathsToCollapsedRoot.Empty();
	ComponentPathsToCollapsedRoot.Empty();
}

bool FUsdCollapsingCache::IsEmpty()
{
	FReadScopeLock ScopeLock( Lock );
	return AssetPathsToCollapsedRoot.IsEmpty() && ComponentPathsToCollapsedRoot.IsEmpty();
}