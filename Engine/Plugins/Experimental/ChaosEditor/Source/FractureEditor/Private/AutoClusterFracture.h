// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "EngineDefines.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionComponent.h"
#include "AutoClusterFracture.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAutoClusterCommand, Log, All);

UENUM()
enum class EFractureAutoClusterMode : uint8
{
	/** Overlapping bounding box*/
	BoundingBox UMETA(DisplayName = "Bounding Box"),

	/** GC connectivity */
	Proximity UMETA(DisplayName = "Proximity"),

	/** Distance */
	Distance UMETA(DisplayName = "Distance"),
};

/** Performs clustering of the currently selected geometry collection bones */
UCLASS()
class UAutoClusterFractureCommand : public UObject
{
public:
	GENERATED_BODY()
public:

	static void ClusterChildBonesOfASingleMesh(UGeometryCollectionComponent* GeometryCollectionComponent, EFractureAutoClusterMode ClusterMode, int32 ClusterSiteCount);
 	static void ClusterSelectedBones(int FractureLevel, EFractureAutoClusterMode ClusterMode, int NumClusters, UGeometryCollectionComponent* GeometryCollectionComponent);
	static void ClusterToNearestSiteInGroup(int FractureLevel, UGeometryCollectionComponent* GeometryCollectionComponent, const TMap<int32, FVector>& Locations, const TArray<TTuple<int32, FVector>>& Sites, const TMap<int32, int32>& BoneToGroup, int32 Group, TArray<TArray<int>>& SiteToBone, TArray<int32>& BoneToSite);
	static void ClusterToNearestSiteInGroup(int FractureLevel, UGeometryCollectionComponent* GeometryCollectionComponent, const TMap<int32, FVector>& Locations, const TArray<TTuple<int32, FVector>>& Sites, const TMap<int32, int32>& BoneToGroup, int32 Group, TArray<TArray<int>>& SiteToBone, TArray<int32>& BoneToSite, TMap<int32, FBox>& WorldBounds);

	static FBox GetChildVolume(const TManagedArray<TSet<int32>>& Children, const TArray<FTransform>& Transforms, const TArray<int32>& TransformToGeometry, const TManagedArray<FBox>& BoundingBoxes, int32 Element);
	static int FindNearestSitetoBone(const FVector& Location, const TArray<TTuple<int32, FVector>>& Sites);
	static int FindNearestSitetoBounds(const FBox& Bounds, const TArray<TTuple<int32, FVector>>& Sites, TMap<int32, FBox>& WorldBounds);
	static void FloodFill(int FractureLevel, int32 CurrentGroup, int32 BoneIndex, TMap<int32, int32> &BoneToGroup, const TManagedArray<int32>& Levels, const TMap<int32, FBox>& BoundingBoxes, float ExpandBounds = 0.0f);
	static void FloodProximity(int FractureLevel, int32 CurrentGroup, int32 BoneIndex, TMap<int32, int32> &ElementToGroup, const TArray<int32>& TransformToGeometry, const TManagedArray<int32>& GeometryToTransform, const TManagedArray<int32>& Levels, const TManagedArray<TSet<int32>>& Proximity);
	static bool HasPath(int32 TransformIndexStart, int32 TransformIndexGoal, const TArray<int32>& BoneToSite, const TArray<int32>& TransformToGeometry, const TManagedArray<int32>& GeometryToTransform, const TManagedArray<TSet<int32>>& Proximity);

	static float GetClosestDistance(const FBox& A, const FBox& B);
};
