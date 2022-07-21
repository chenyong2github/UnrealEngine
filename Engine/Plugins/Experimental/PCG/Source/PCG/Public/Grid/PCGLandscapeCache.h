// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGLandscapeCache.generated.h"

class ALandscapeProxy;
class FLandscapeProxyComponentDataChangedParams;
class ULandscapeComponent;
class ULandscapeInfo;
class UPCGPointData;
class UPCGMetadata;
struct FPCGPoint;

USTRUCT()
struct FPCGLandscapeCacheLayer
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Cache")
	FName Name;

	UPROPERTY()
	TArray<uint8> Data;
};

USTRUCT()
struct FPCGLandscapeCacheEntry
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	void BuildCacheData(ULandscapeInfo* LandscapeInfo, ULandscapeComponent* InComponent, UObject* Owner);
#endif

	void GetPoint(int32 PointIndex, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const;
	void GetPointHeightOnly(int32 PointIndex, FPCGPoint& OutPoint) const;
	void GetInterpolatedPoint(const FVector2D& LocalPoint, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const;
	void GetInterpolatedPointHeightOnly(const FVector2D& LocalPoint, FPCGPoint& OutPoint) const;

	// TODO: this isn't really needed
	UPROPERTY()
	TWeakObjectPtr<const ULandscapeComponent> Component = nullptr;

	UPROPERTY()
	TArray<FVector> PositionsAndNormals;

	UPROPERTY()
	TArray<FPCGLandscapeCacheLayer> LayerData;

	UPROPERTY()
	FVector PointHalfSize = FVector::One();

	UPROPERTY()
	int32 Stride = 0;
};

USTRUCT()
struct FPCGLandscapeCache
{
	GENERATED_BODY()
public:
	FPCGLandscapeCache() = default;
	~FPCGLandscapeCache();
	FPCGLandscapeCache(UObject* InOwner);

	void SetOwner(UObject* InOwner);
	void PrimeCache();
	void ClearCache();

	const FPCGLandscapeCacheEntry* GetCacheEntry(ULandscapeComponent* LandscapeComponent, const FIntPoint& ComponentKey);
	TArray<FName> GetLayerNames(ALandscapeProxy* Landscape);

private:
#if WITH_EDITOR
	void SetupLandscapeCallbacks();
	void TeardownLandscapeCallbacks();
	void OnLandscapeChanged(ALandscapeProxy* Landscape, const FLandscapeProxyComponentDataChangedParams& ChangeParams);
	void CacheLayerNames(ALandscapeProxy* Landscape);
	void CacheLayerNames();
#endif

	// TODO: need to have an indirection for multiple landscape support
	UPROPERTY(VisibleAnywhere, Category = "Cache")
	TMap<FIntPoint, FPCGLandscapeCacheEntry> CachedData;

	//TODO: separate by landscape
	UPROPERTY(VisibleAnywhere, Category = "Cache")
	TSet<FName> CachedLayerNames;

	// Transient by design
	UObject* Owner = nullptr;

#if WITH_EDITOR
	TSet<TWeakObjectPtr<ALandscapeProxy>> Landscapes;
#endif

#if WITH_EDITOR
	FRWLock CacheLock;
#endif
};

template<>
struct TStructOpsTypeTraits<FPCGLandscapeCache> : public TStructOpsTypeTraitsBase2< FPCGLandscapeCache>
{
	enum
	{
		WithCopy = false
	};
};