// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GeometryCollection/GeometryCollection.h"
#include "Dataflow/DataflowSelection.h"

class FRACTUREENGINE_API FFractureEngineEdit
{
public:

	static void DeleteBranch(FGeometryCollection& GeometryCollection, const TArray<int32>& InBoneSelection);

	static void SetVisibilityInCollection(FManagedArrayCollection& InCollection, const TArray<int32>& InBoneSelection, bool bVisible);

	static void Merge(FGeometryCollection& GeometryCollection, const TArray<int32>& InBoneSelection);

};