// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeometryCollection/ManagedArrayCollection.h"
#include "Misc/Crc.h"

/**
* TransformDynamicCollection (ManagedArrayCollection)
*
* Stores per instance data for transforms and hierarchy information
*/
class GEOMETRYCOLLECTIONCORE_API FTransformDynamicCollection : public FManagedArrayCollection
{
public:
	typedef FManagedArrayCollection Super;

	FTransformDynamicCollection();
	FTransformDynamicCollection(FTransformDynamicCollection &) = delete;
	FTransformDynamicCollection& operator=(const FTransformDynamicCollection&) = delete;
	FTransformDynamicCollection(FTransformDynamicCollection&&) = delete;
	FTransformDynamicCollection& operator=(FTransformDynamicCollection&&) = delete;

	// Transform Group
	TManagedArray<FTransform>   Transform;
	TManagedArray<int32>        Parent;
	TManagedArray<TSet<int32>>  Children;
	TManagedArray<int32>        SimulationType;
	TManagedArray<int32>        StatusFlags;

protected:

	/** Construct */
	void Construct();
};