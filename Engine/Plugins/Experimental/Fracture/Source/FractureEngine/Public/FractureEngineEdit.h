// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryCollection/GeometryCollection.h"
#include "Dataflow/DataflowSelection.h"

class FRACTUREENGINE_API FFractureEngineEdit
{
public:

	static void DeleteBranch(FGeometryCollection& GeometryCollection, const TArray<int32>& InBoneSelection);

	static void SetVisibilityInCollectionFromTransformSelection(FManagedArrayCollection& InCollection, const TArray<int32>& InTransformSelection, bool bVisible);

	static void SetVisibilityInCollectionFromFaceSelection(FManagedArrayCollection& InCollection, const TArray<int32>& InFaceSelection, bool bVisible);

	static void Merge(FGeometryCollection& GeometryCollection, const TArray<int32>& InBoneSelection);

};