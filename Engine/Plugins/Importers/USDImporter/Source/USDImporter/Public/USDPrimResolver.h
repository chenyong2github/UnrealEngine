// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "USDMemory.h"
#include "UObject/Object.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"

#include "pxr/usd/usd/prim.h"

#include "USDIncludesEnd.h"
#endif // #if USE_USD_SDK

#include "USDPrimResolver.generated.h"

class IUsdPrim;
class IAssetRegistry;
class UActorFactory;
struct FUsdImportContext;
struct FUsdGeomData;
struct FUSDSceneImportContext;

#if USE_USD_SDK
struct FUsdAssetPrimToImport
{
	FUsdAssetPrimToImport()
		: NumLODs(1)
		, CustomPrimTransform(FMatrix::Identity)
	{}

	/** The prim that represents the root most prim of the mesh asset being created */
	TUsdStore< pxr::UsdPrim > Prim;

	/** Each prim in this list represents a list of prims which have LODs at a specific lod index */
	TArray< TUsdStore< pxr::UsdPrim > > MeshPrims;
	int32 NumLODs;
	FMatrix CustomPrimTransform;
	FString AssetPath;
};

struct FActorSpawnData
{
	FTransform WorldTransform;
	/** The prim that represents this actor */
	TUsdStore< pxr::UsdPrim > ActorPrim;
	/** The prim that represents the parent of this actor for attachment (not necessarily the parent of this prim) */
	TUsdStore< pxr::UsdPrim > AttachParentPrim;
	/** List of assets under this actor to create */
	TArray<FUsdAssetPrimToImport> AssetsToImport;
	FString ActorClassName;
	FString AssetPath;
	FName ActorName;
};
#endif // #if USE_USD_SDK

/** Base class for all evaluation of prims for geometry and actors */
UCLASS(transient, MinimalAPI)
class UUSDPrimResolver : public UObject
{
	GENERATED_BODY()

public:
#if USE_USD_SDK
	virtual void Init();

	virtual void FindMeshAssetsToImport(FUsdImportContext& ImportContext, const TUsdStore< pxr::UsdPrim >& StartPrim, const TUsdStore< pxr::UsdPrim >& ModelPrim, TArray<FUsdAssetPrimToImport>& OutAssetsToImport, bool bRecursive = true) const;

	/**
	 * Finds any mesh children of a parent prim
	 *
	 * @param ImportContext		Contextual information about the current import
	 * @param ParentPrim		The parent prim to find children from
	 * @param bOnlyLODRoots		Only return prims which are parents of LOD meshes (i.e the prim has an LOD variant set)
	 * @param OutMeshChilden	Flattened list of descendant prims with geometry
	 */
	virtual void FindMeshChildren(FUsdImportContext& ImportContext, const TUsdStore< pxr::UsdPrim >& ParentPrim, bool bOnlyLODRoots, TArray< TUsdStore< pxr::UsdPrim > >& OutMeshChildren) const;

	virtual void FindActorsToSpawn(FUSDSceneImportContext& ImportContext, TArray<FActorSpawnData>& OutActorSpawnDatas) const;

	
	virtual AActor* SpawnActor(FUSDSceneImportContext& ImportContext, const FActorSpawnData& SpawnData);

	virtual TSubclassOf<AActor> FindActorClass(FUSDSceneImportContext& ImportContext, const FActorSpawnData& SpawnData) const;

protected:
	virtual void FindActorsToSpawn_Recursive(FUSDSceneImportContext& ImportContext, const TUsdStore< pxr::UsdPrim >& Prim, const TUsdStore< pxr::UsdPrim >& ParentPrim, TArray<FActorSpawnData>& OutSpawnDatas) const;
	bool IsValidPathForImporting(const FString& TestPath) const;
protected:
	IAssetRegistry* AssetRegistry;
	TMap<FString, AActor*> PrimToActorMap;
#endif // #if USE_USD_SDK
};
