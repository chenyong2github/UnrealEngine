// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TransformDynamicCollection.h"
//#include "GeometryCollection/GeometryCollectionSimulationTypes.h"
#include "Misc/Crc.h"

class GEOMETRYCOLLECTIONCORE_API FGeometryDynamicCollection : public FTransformDynamicCollection
{

public:
	FGeometryDynamicCollection();
	FGeometryDynamicCollection(FGeometryDynamicCollection &) = delete;
	FGeometryDynamicCollection& operator=(const FGeometryDynamicCollection &) = delete;
	FGeometryDynamicCollection(FGeometryDynamicCollection &&) = delete;
	FGeometryDynamicCollection& operator=(FGeometryDynamicCollection &&) = delete;


	typedef FTransformDynamicCollection Super;

	static const FName ActiveAttribute;
	static const FName CollisionGroupAttribute;
	static const FName DynamicStateAttribute;


	// Transform Group
	TManagedArray<bool> Active;
	TManagedArray<int32> CollisionGroup;
	TManagedArray<int32> DynamicState;
};