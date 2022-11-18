// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryCollection/GeometryCollection.h"
#include "Dataflow/DataflowSelection.h"

class FRACTUREENGINE_API FFractureEngineSelection
{
public:
	static void GetRootBones(const FManagedArrayCollection& Collection, TArray<int32>& RootBonesOut);
	static void GetRootBones(const FManagedArrayCollection& Collection, FDataflowTransformSelection& TransformSelection);

	static void SelectParent(const FManagedArrayCollection& Collection, TArray<int32>& SelectedBones);
	static void SelectParent(const FManagedArrayCollection& Collection, FDataflowTransformSelection& TransformSelection);

	static void SelectChildren(const FManagedArrayCollection& Collection, TArray<int32>& SelectedBones);
	static void SelectChildren(const FManagedArrayCollection& Collection, FDataflowTransformSelection& TransformSelection);

	static void SelectSiblings(const FManagedArrayCollection& Collection, TArray<int32>& SelectedBones);
	static void SelectSiblings(const FManagedArrayCollection& Collection, FDataflowTransformSelection& TransformSelection);

	static void SelectLevel(const FManagedArrayCollection& Collection, TArray<int32>& SelectedBones);
	static void SelectLevel(const FManagedArrayCollection& Collection, FDataflowTransformSelection& TransformSelection);

	static void SelectContact(FGeometryCollection& GeometryCollection, TArray<int32>& SelectedBones);
	static void SelectContact(FGeometryCollection& GeometryCollection, FDataflowTransformSelection& TransformSelection);

	static void SelectLeaf(const FGeometryCollection& GeometryCollection, TArray<int32>& SelectedBones);
	static void SelectLeaf(const FGeometryCollection& GeometryCollection, FDataflowTransformSelection& TransformSelection);

	static void SelectCluster(const FGeometryCollection& GeometryCollection, TArray<int32>& SelectedBones);
	static void SelectCluster(const FGeometryCollection& GeometryCollection, FDataflowTransformSelection& TransformSelection);

	static void SelectByPercentage(TArray<int32>& SelectedBones, const int32 Percentage, const bool Deterministic, const float RandomSeed);
	static void SelectByPercentage(FDataflowTransformSelection& TransformSelection, const int32 Percentage, const bool Deterministic, const float RandomSeed);

	static void SelectBySize(FGeometryCollection& GeometryCollection, TArray<int32>& SelectedBones, const float SizeMin, const float SizeMax);
	static void SelectBySize(FGeometryCollection& GeometryCollection, FDataflowTransformSelection& TransformSelection, const float SizeMin, const float SizeMax);

	static void SelectByVolume(FGeometryCollection& GeometryCollection, TArray<int32>& SelectedBones, const float VolumeMin, const float VolumeMax);
	static void SelectByVolume(FGeometryCollection& GeometryCollection, FDataflowTransformSelection& TransformSelection, const float VolumeMin, const float VolumeMax);
};