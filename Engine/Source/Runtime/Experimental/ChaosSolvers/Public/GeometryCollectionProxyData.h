/// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Crc.h"

#include "GeometryCollection/ManagedArrayCollection.h"
#include "GeometryCollection/GeometryCollectionCollisionStructureManager.h"

/*
* Managed arrays for simulation data used by the GeometryCollectionProxy
*/

/**
* FTransformDynamicCollection (FManagedArrayCollection)
*
* Stores per instance data for transforms and hierarchy information
*/
class CHAOSSOLVERS_API FTransformDynamicCollection : public FManagedArrayCollection
{
public:
	typedef FManagedArrayCollection Super;

	FTransformDynamicCollection();
	FTransformDynamicCollection(FTransformDynamicCollection&) = delete;
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


/**
* FGeometryDynamicCollection (FTransformDynamicCollection)
*
* Stores per instance data for simulation level information
*/

class CHAOSSOLVERS_API FGeometryDynamicCollection : public FTransformDynamicCollection
{

public:
	FGeometryDynamicCollection();
	FGeometryDynamicCollection(FGeometryDynamicCollection&) = delete;
	FGeometryDynamicCollection& operator=(const FGeometryDynamicCollection&) = delete;
	FGeometryDynamicCollection(FGeometryDynamicCollection&&) = delete;
	FGeometryDynamicCollection& operator=(FGeometryDynamicCollection&&) = delete;

	typedef FTransformDynamicCollection Super;

	static const FName ActiveAttribute;
	static const FName CollisionGroupAttribute;
	static const FName CollisionMaskAttribute;
	static const FName DynamicStateAttribute;
	static const FName ImplicitsAttribute;
	static const FName ShapesQueryDataAttribute;
	static const FName ShapesSimDataAttribute;
	static const FName SharedGeometryAttribute;
	static const FName SharedImplicitsAttribute;
	static const FName SimplicialsAttribute;

	// Transform Group
	TManagedArray<bool> Active;
	TManagedArray<int32> CollisionGroup;
	TManagedArray<int32> CollisionMask;
	TManagedArray<int32> CollisionStructureID;
	TManagedArray<int32> DynamicState;
	TManagedArray<TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe>> Implicits;
	TManagedArray<FVector> InitialAngularVelocity;
	TManagedArray<FVector> InitialLinearVelocity;
	TManagedArray<FTransform> MassToLocal;
	//TManagedArray<TArray<FCollisionFilterData>> ShapeQueryData;
	//TManagedArray<TArray<FCollisionFilterData>> ShapeSimData;
	TManagedArray<TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe>> SharedGeometry;
	//TManagedArray<TUniquePtr<FCollisionStructureManager::FSimplicial>> Simplicials;
	TManagedArray<bool> SimulatableParticles;


};