// Copyright Epic Games, Inc. All Rights Reserved.

#include "Grid/PCGLandscapeCache.h"

#include "PCGSubsystem.h"
#include "Data/PCGPointData.h"
#include "Helpers/PCGBlueprintHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Metadata/PCGMetadataAttribute.h"
#include "Metadata/PCGMetadataAttributeTpl.h"
#include "Metadata/PCGMetadataAttributeTraits.h"

#include "UObject/WeakObjectPtr.h"

#include "LandscapeHeightfieldCollisionComponent.h"
#include "LandscapeComponent.h"
#include "LandscapeInfo.h"
#include "LandscapeInfoMap.h"
#include "LandscapeProxy.h"
#include "LandscapeDataAccess.h"
#include "Landscape.h"
#include "Kismet/GameplayStatics.h"

#if WITH_EDITOR
void FPCGLandscapeCacheEntry::BuildCacheData(ULandscapeInfo* LandscapeInfo, ULandscapeComponent* InComponent, UObject* Owner)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGLandscapeCacheEntry::BuildCacheData);
	check(Owner && !Component.Get() && InComponent && PositionsAndNormals.Num() == 0);
	Component = InComponent;

	ALandscape* Landscape = Component->GetLandscapeActor();
	if (!Landscape)
	{
		return;
	}

	// Get landscape default layer (heightmap/tangents/normal)
	{
		FLandscapeComponentDataInterface CDI(InComponent, 0);
		FVector WorldPos;
		FVector WorldTangentX;
		FVector WorldTangentY;
		FVector WorldTangentZ;

		PointHalfSize = InComponent->GetComponentTransform().GetScale3D() * 0.5;

		// The component has an extra vertex on the edge, for interpolation purposes
		const int32 ComponentSizeQuads = InComponent->ComponentSizeQuads + 1;
		const int32 NumVertices = FMath::Square(ComponentSizeQuads);

		PositionsAndNormals.Reserve(2 * NumVertices);
		for (int32 Index = 0; Index < NumVertices; ++Index)
		{
			CDI.GetWorldPositionTangents(Index, WorldPos, WorldTangentX, WorldTangentY, WorldTangentZ);
			PositionsAndNormals.Add(WorldPos);
			PositionsAndNormals.Add(WorldTangentZ);
		}
	}
	
	// Get other layers, push data into metadata attributes
	TArray<uint8> LayerCache;
	for (const FLandscapeInfoLayerSettings& Layer : LandscapeInfo->Layers)
	{
		ULandscapeLayerInfoObject* LayerInfo = Layer.LayerInfoObj;
		if (!LayerInfo)
		{
			continue;
		}

		FLandscapeComponentDataInterface CDI(InComponent, 0, /*bUseEditingLayer=*/true);
		if (CDI.GetWeightmapTextureData(LayerInfo, LayerCache, /*bUseEditingLayer=*/true))
		{
			FPCGLandscapeCacheLayer& PCGLayer = LayerData.Emplace_GetRef();
			PCGLayer.Name = Layer.LayerName;
			PCGLayer.Data = LayerCache;
		}

		LayerCache.Reset();
	}
}
#endif // WITH_EDITOR

bool FPCGLandscapeCacheEntry::GetPoint(int32 PointIndex, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const
{
	//TRACE_CPUPROFILER_EVENT_SCOPE(FPCGLandscapeCacheEntry::GetPoint);
	if (PointIndex < 0 || 2 * PointIndex >= PositionsAndNormals.Num())
	{
		return false;
	}

	const FVector& Position = PositionsAndNormals[2 * PointIndex];
	const FVector& Normal = PositionsAndNormals[2 * PointIndex + 1];
	
	FVector TangentX;
	FVector TangentY;
	TangentX = FVector(Normal.Z, 0.f, -Normal.X);
	TangentY = Normal ^ TangentX;

	OutPoint.Transform = FTransform(TangentX, TangentY, Normal, Position);
	OutPoint.BoundsMin = -PointHalfSize;
	OutPoint.BoundsMax = PointHalfSize;
	OutPoint.Seed = UPCGBlueprintHelpers::ComputeSeedFromPosition(Position);
	
	if (OutMetadata && !LayerData.IsEmpty())
	{
		OutPoint.MetadataEntry = OutMetadata->AddEntry();

		for (const FPCGLandscapeCacheLayer& Layer : LayerData)
		{
			if (FPCGMetadataAttributeBase* Attribute = OutMetadata->GetMutableAttribute(Layer.Name))
			{
				check(Attribute->GetTypeId() == PCG::Private::MetadataTypes<float>::Id);
				static_cast<FPCGMetadataAttribute<float>*>(Attribute)->SetValue(OutPoint.MetadataEntry, (float)Layer.Data[PointIndex] / 255.0f);
			}
		}
	}

	return true;
}

bool FPCGLandscapeCacheEntry::GetInterpolatedPoint(int32 BasePointIndex, int32 PointLineStride, float XFactor, float YFactor, FPCGPoint& OutPoint, UPCGMetadata* OutMetadata) const
{
	if (BasePointIndex < 0 || 2 * BasePointIndex >= PositionsAndNormals.Num())
	{
		return false;
	}

	const int32 X0Y0 = BasePointIndex;
	const int32 X1Y0 = X0Y0 + 1;
	const int32 X0Y1 = X0Y0 + PointLineStride;
	const int32 X1Y1 = X0Y1 + 1;

	const FVector& PositionX0Y0 = PositionsAndNormals[2 * X0Y0];
	const FVector& NormalX0Y0 = PositionsAndNormals[2 * X0Y0 + 1];
	const FVector& PositionX1Y0 = PositionsAndNormals[2 * X1Y0];
	const FVector& NormalX1Y0 = PositionsAndNormals[2 * X1Y0 + 1];
	const FVector& PositionX0Y1 = PositionsAndNormals[2 * X0Y1];
	const FVector& NormalX0Y1 = PositionsAndNormals[2 * X0Y1 + 1];
	const FVector& PositionX1Y1 = PositionsAndNormals[2 * X1Y1];
	const FVector& NormalX1Y1 = PositionsAndNormals[2 * X1Y1 + 1];

	FVector LerpPositionY0 = FMath::Lerp(PositionX0Y0, PositionX1Y0, XFactor);
	FVector LerpPositionY1 = FMath::Lerp(PositionX0Y1, PositionX1Y1, XFactor);
	FVector Position = FMath::Lerp(LerpPositionY0, LerpPositionY1, YFactor);

	FVector LerpNormalY0 = FMath::Lerp(NormalX0Y0, NormalX1Y0, XFactor);
	FVector LerpNormalY1 = FMath::Lerp(NormalX0Y1, NormalX1Y1, XFactor);
	FVector Normal = FMath::Lerp(LerpNormalY0, LerpNormalY1, YFactor);

	// TODO: we could preserve normal length by lerping this too?
	Normal.Normalize();

	FVector TangentX;
	FVector TangentY;
	TangentX = FVector(Normal.Z, 0.f, -Normal.X);
	TangentY = Normal ^ TangentX;

	OutPoint.Transform = FTransform(TangentX, TangentY, Normal, Position);
	OutPoint.BoundsMin = -PointHalfSize;
	OutPoint.BoundsMax = PointHalfSize;
	OutPoint.Seed = UPCGBlueprintHelpers::ComputeSeedFromPosition(Position);

	if (OutMetadata && !LayerData.IsEmpty())
	{
		OutPoint.MetadataEntry = OutMetadata->AddEntry();

		for (const FPCGLandscapeCacheLayer& Layer : LayerData)
		{
			if (FPCGMetadataAttributeBase* Attribute = OutMetadata->GetMutableAttribute(Layer.Name))
			{
				check(Attribute->GetTypeId() == PCG::Private::MetadataTypes<float>::Id);
				float Y0Data = FMath::Lerp((float)Layer.Data[X0Y0] / 255.0f, (float)Layer.Data[X1Y0] / 255.0f, XFactor);
				float Y1Data = FMath::Lerp((float)Layer.Data[X0Y1] / 255.0f, (float)Layer.Data[X1Y1] / 255.0f, XFactor);
				float Data = FMath::Lerp(Y0Data, Y1Data, YFactor);

				static_cast<FPCGMetadataAttribute<float>*>(Attribute)->SetValue(OutPoint.MetadataEntry, Data);
			}
		}
	}

	return true;
}

FPCGLandscapeCache::FPCGLandscapeCache(UObject* InOwner)
	: Owner(InOwner)
{
	check(Owner);
#if WITH_EDITOR
	SetupLandscapeCallbacks();
	CacheLayerNames();
#endif
}

void FPCGLandscapeCache::SetOwner(UObject* InOwner)
{
	check(!Owner || Owner == InOwner || InOwner == nullptr);
	Owner = InOwner;
#if WITH_EDITOR
	if (InOwner)
	{
		SetupLandscapeCallbacks();
	}
	else
	{
		TeardownLandscapeCallbacks();
	}

	CacheLayerNames();
#endif
}

FPCGLandscapeCache::~FPCGLandscapeCache()
{
#if WITH_EDITOR
	TeardownLandscapeCallbacks();
#endif
}

void FPCGLandscapeCache::PrimeCache()
{
#if WITH_EDITOR
	if (!Owner)
	{
		return;
	}

	UWorld* World = Owner->GetWorld();
	if (!World)
	{
		return;
	}

	for (auto It = ULandscapeInfoMap::GetLandscapeInfoMap(World).Map.CreateIterator(); It; ++It)
	{
		ULandscapeInfo* LandscapeInfo = It.Value();
		if (IsValid(LandscapeInfo))
		{
			// Build per-component information
			LandscapeInfo->ForAllLandscapeProxies([this, LandscapeInfo](const ALandscapeProxy* LandscapeProxy)
			{
				for (ULandscapeComponent* LandscapeComponent : LandscapeProxy->LandscapeComponents)
				{
					if (!LandscapeComponent)
					{
						continue;
					}

					FIntPoint ComponentKey(LandscapeComponent->SectionBaseX / LandscapeComponent->ComponentSizeQuads, LandscapeComponent->SectionBaseY / LandscapeComponent->ComponentSizeQuads);

					if (!CachedData.Contains(ComponentKey))
					{
						CachedData.Add(ComponentKey).BuildCacheData(LandscapeInfo, LandscapeComponent, Owner);
					}
				}
			});
		}
	}

	CacheLayerNames();
#endif
}

void FPCGLandscapeCache::ClearCache()
{
	CachedData.Reset();
}

const FPCGLandscapeCacheEntry* FPCGLandscapeCache::GetCacheEntry(ULandscapeComponent* LandscapeComponent, const FIntPoint& ComponentKey)
{
	const FPCGLandscapeCacheEntry* FoundEntry = nullptr;

	{
#if WITH_EDITOR
		FReadScopeLock ScopeLock(CacheLock);
#endif
		FoundEntry = CachedData.Find(ComponentKey);
	}

#if WITH_EDITOR
	if (!FoundEntry && LandscapeComponent && LandscapeComponent->GetLandscapeInfo())
	{
		check(LandscapeComponent->SectionBaseX / LandscapeComponent->ComponentSizeQuads == ComponentKey.X && LandscapeComponent->SectionBaseY / LandscapeComponent->ComponentSizeQuads == ComponentKey.Y);
		// Create entry
		FPCGLandscapeCacheEntry NewEntry;
		NewEntry.BuildCacheData(LandscapeComponent->GetLandscapeInfo(), LandscapeComponent, Owner);

		// Try to store it
		{
			FWriteScopeLock ScopeLock(CacheLock);
			CachedData.FindOrAdd(ComponentKey, NewEntry);
			FoundEntry = CachedData.Find(ComponentKey);
		}
	}
#endif
	
	return FoundEntry;
}

TArray<FName> FPCGLandscapeCache::GetLayerNames(ALandscapeProxy* Landscape)
{
#if WITH_EDITOR
	FReadScopeLock ScopeLock(CacheLock);
#endif
	return CachedLayerNames.Array();
}

#if WITH_EDITOR
void FPCGLandscapeCache::SetupLandscapeCallbacks()
{
	// Remove previous callbacks, if any
	TeardownLandscapeCallbacks();

	if (!Owner)
	{
		return;
	}

	UWorld* World = Owner->GetWorld();
	if (!World)
	{
		return;
	}

	// Gather landspace actors
	TArray<AActor*> FoundLandscapes;
	UGameplayStatics::GetAllActorsOfClass(World, ALandscapeProxy::StaticClass(), FoundLandscapes);

	for (AActor* FoundLandscape : FoundLandscapes)
	{
		ALandscapeProxy* Landscape = CastChecked<ALandscapeProxy>(FoundLandscape);

		Landscapes.Add(Landscape);
		Landscape->OnComponentDataChanged.AddRaw(this, &FPCGLandscapeCache::OnLandscapeChanged);
	}
}

void FPCGLandscapeCache::TeardownLandscapeCallbacks()
{
	for (TWeakObjectPtr<ALandscapeProxy> LandscapeWeakPtr : Landscapes)
	{
		if (ALandscapeProxy* Landscape = LandscapeWeakPtr.Get())
		{
			Landscape->OnComponentDataChanged.RemoveAll(this);
		}
	}
}

void FPCGLandscapeCache::OnLandscapeChanged(ALandscapeProxy* Landscape, const FLandscapeProxyComponentDataChangedParams& ChangeParams)
{
	if (!Landscapes.Contains(Landscape))
	{
		return;
	}

	CacheLock.WriteLock();

	// Just remove these from the cache, they'll be added back on demand
	ChangeParams.ForEachComponent([this](const ULandscapeComponent* LandscapeComponent)
	{
		if (LandscapeComponent)
		{
			FIntPoint ComponentKey(LandscapeComponent->SectionBaseX / LandscapeComponent->ComponentSizeQuads, LandscapeComponent->SectionBaseY / LandscapeComponent->ComponentSizeQuads);
			CachedData.Remove(ComponentKey);
		}
	});

	CacheLayerNames(Landscape);

	CacheLock.WriteUnlock();
}

void FPCGLandscapeCache::CacheLayerNames()
{
	CachedLayerNames.Reset();

	for (TWeakObjectPtr<ALandscapeProxy> Landscape : Landscapes)
	{
		if (Landscape.Get())
		{
			CacheLayerNames(Landscape.Get());
		}
	}
}

void FPCGLandscapeCache::CacheLayerNames(ALandscapeProxy* Landscape)
{
	check(Landscape);

	if (ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo())
	{
		for (const FLandscapeInfoLayerSettings& Layer : LandscapeInfo->Layers)
		{
			ULandscapeLayerInfoObject* LayerInfo = Layer.LayerInfoObj;
			if (!LayerInfo)
			{
				continue;
			}

			CachedLayerNames.Add(Layer.LayerName);
		}
	}
}

#endif // WITH_EDITOR