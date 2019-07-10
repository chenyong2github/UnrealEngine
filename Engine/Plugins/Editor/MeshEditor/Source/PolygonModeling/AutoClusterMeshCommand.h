// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "EngineDefines.h"
#include "MeshEditorCommands.h"
#include "EditableMesh.h"
#include "GeometryCollectionCommandCommon.h"
#include "AutoClusterMeshCommand.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogAutoClusterCommand, Log, All);

/** Performs clustering of the currently selected geometry collection bones */
UCLASS()
class UAutoClusterMeshCommand : public UMeshEditorInstantCommand, public FGeometryCollectionCommandCommon
{
public:
	GENERATED_BODY()

protected:

	// Overrides
	virtual EEditableMeshElementType GetElementType() const override
	{
		return EEditableMeshElementType::Fracture;
	}
	virtual FUIAction MakeUIAction(class IMeshEditorModeUIContract& MeshEditorMode) override;
	virtual void RegisterUICommand(class FBindingContext* BindingContext) override;
	virtual void Execute(class IMeshEditorModeEditingContract& MeshEditorMode) override;

private:
	EMeshAutoClusterMode AutoClusterGroupMode;

	void ClusterChildBonesOfASingleMesh(IMeshEditorModeEditingContract& MeshEditorMode, TArray<UEditableMesh*>& SelectedMeshes);
	void ClusterSelectedBones(int FractureLevel, int NumClusters, UEditableMesh* EditableMesh, UGeometryCollectionComponent* GeometryCollectionComponent);
	void ClusterToNearestSiteInGroup(int FractureLevel, UGeometryCollectionComponent* GeometryCollectionComponent, const TMap<int32, FVector>& Locations, const TArray<TTuple<int32, FVector>>& Sites, const TMap<int32, int32>& BoneToGroup, int32 Group, TArray<TArray<int>>& SiteToBone, TArray<int32>& BoneToSite);
	void ClusterToNearestSiteInGroup(int FractureLevel, UGeometryCollectionComponent* GeometryCollectionComponent, const TMap<int32, FVector>& Locations, const TArray<TTuple<int32, FVector>>& Sites, const TMap<int32, int32>& BoneToGroup, int32 Group, TArray<TArray<int>>& SiteToBone, TArray<int32>& BoneToSite, TMap<int32, FBox>& WorldBounds);

	static FBox GetChildVolume(const TManagedArray<TSet<int32>>& Children, const TArray<FTransform>& Transforms, const TArray<int32>& TransformToGeometry, const TManagedArray<FBox>& BoundingBoxes, int32 Element);
	static int FindNearestSitetoBone(const FVector& Location, const TArray<TTuple<int32, FVector>>& Sites);
	static int FindNearestSitetoBounds(const FBox& Bounds, const TArray<TTuple<int32, FVector>>& Sites, TMap<int32, FBox>& WorldBounds);
	static void FloodFill(int FractureLevel, int32 CurrentGroup, int32 BoneIndex, TMap<int32, int32> &BoneToGroup, const TManagedArray<int32>& Levels, const TMap<int32, FBox>& BoundingBoxes, float ExpandBounds = 0.0f);
	static void FloodProximity(int FractureLevel, int32 CurrentGroup, int32 BoneIndex, TMap<int32, int32> &ElementToGroup, const TArray<int32>& TransformToGeometry, const TManagedArray<int32>& GeometryToTransform, const TManagedArray<int32>& Levels, const TManagedArray<TSet<int32>>& Proximity);
	static bool HasPath(int32 TransformIndexStart, int32 TransformIndexGoal, const TArray<int32>& BoneToSite, const TArray<int32>& TransformToGeometry, const TManagedArray<int32>& GeometryToTransform, const TManagedArray<TSet<int32>>& Proximity);

	static float GetClosestDistance(const FBox& A, const FBox& B);
};
