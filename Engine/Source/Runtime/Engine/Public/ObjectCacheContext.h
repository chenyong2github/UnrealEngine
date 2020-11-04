// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Set.h"
#include "Containers/Map.h"
#include "Misc/Optional.h"
#include "UObject/ObjectKey.h"

class UObject;
class UPrimitiveComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;
class UTexture;

/** 
 *  Context containing a lazy initialized ObjectIterator cache along with some useful
 *  reverse lookup tables that can be used during heavy scene updates
 *  of async asset compilation.
 */
class FObjectCacheContext
{
public:
	const TArray<UPrimitiveComponent*>&   GetPrimitiveComponents();
	const TArray<UStaticMeshComponent*>&  GetStaticMeshComponents();
	const TArray<UStaticMeshComponent*>&  GetStaticMeshComponents(UStaticMesh* InStaticMesh);
	const TSet<UMaterialInterface*>&      GetMaterialsAffectedByTexture(UTexture* InTexture);
	const TSet<UPrimitiveComponent*>&     GetPrimitivesAffectedByMaterial(UMaterialInterface* InMaterial);
	const TSet<UTexture*>&                GetUsedTextures(UMaterialInterface* InMaterial);
	const TArray<UMaterialInterface*>&    GetUsedMaterials(UPrimitiveComponent* InComponent);

private:
	friend class FObjectCacheContextScope;
	FObjectCacheContext() = default;
	TMap<TObjectKey<UPrimitiveComponent>, TArray<UMaterialInterface*>> PrimitiveComponentToMaterial;
	TMap<TObjectKey<UMaterialInterface>, TSet<UTexture*>> MaterialUsedTextures;
	TOptional<TMap<TObjectKey<UTexture>, TSet<UMaterialInterface*>>> TextureToMaterials;
	TOptional<TMap<TObjectKey<UMaterialInterface>, TSet<UPrimitiveComponent*>>> MaterialToPrimitives;
	TOptional<TMap<TObjectKey<UStaticMesh>, TArray<UStaticMeshComponent*>>> StaticMeshToComponents;
	TOptional<TArray<UStaticMeshComponent*>> StaticMeshComponents;
	TOptional<TArray<UPrimitiveComponent*>> PrimitiveComponents;
};

/**
 * A scope that can be used to maintain a FObjectCacheContext active until the scope
 * is destroyed. Should only be used during short periods when there is no new
 * objects created and no object dependency changes. (i.e. Scene update after asset compilation).
 */
class FObjectCacheContextScope
{
public:
	ENGINE_API FObjectCacheContextScope();
	ENGINE_API ~FObjectCacheContextScope();
	ENGINE_API FObjectCacheContext& GetContext();
private:
	// Scopes can be stacked over one another, but only the outermost will
	// own the actual context and destroy it at the end, all inner scopes
	// will feed off the already existing one and will not own it.
	bool bIsOwner = false;
};