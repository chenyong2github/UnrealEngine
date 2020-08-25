// Copyright Epic Games, Inc. All Rights Reserved.

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
class CHAOS_API FTransformDynamicCollection : public FManagedArrayCollection
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

class CHAOS_API FGeometryDynamicCollection : public FTransformDynamicCollection
{

public:
	FGeometryDynamicCollection();
	FGeometryDynamicCollection(FGeometryDynamicCollection&) = delete;
	FGeometryDynamicCollection& operator=(const FGeometryDynamicCollection&) = delete;
	FGeometryDynamicCollection(FGeometryDynamicCollection&&) = delete;
	FGeometryDynamicCollection& operator=(FGeometryDynamicCollection&&) = delete;

	typedef FTransformDynamicCollection Super;
	typedef TSharedPtr<Chaos::FImplicitObject, ESPMode::ThreadSafe> FSharedImplicit;

	static const FName ActiveAttribute;
	static const FName CollisionGroupAttribute;
	static const FName CollisionMaskAttribute;
	static const FName DynamicStateAttribute;
	static const FName ImplicitsAttribute;
	static const FName ShapesQueryDataAttribute;
	static const FName ShapesSimDataAttribute;
	static const FName SharedImplicitsAttribute;
	static const FName SimplicialsAttribute;
	static const FName SimulatableParticlesAttribute;

	// Transform Group
	TManagedArray<bool> Active;
	TManagedArray<int32> CollisionGroup;
	TManagedArray<int32> CollisionMask;
	TManagedArray<int32> CollisionStructureID;
	TManagedArray<int32> DynamicState;
	TManagedArray<FSharedImplicit> Implicits;
	TManagedArray<FVector> InitialAngularVelocity;
	TManagedArray<FVector> InitialLinearVelocity;
	TManagedArray<FTransform> MassToLocal;
	//TManagedArray<TArray<FCollisionFilterData>> ShapeQueryData;
	//TManagedArray<TArray<FCollisionFilterData>> ShapeSimData;
	TManagedArray<TUniquePtr<FCollisionStructureManager::FSimplicial>> Simplicials;
	TManagedArray<bool> SimulatableParticles;
};


/**
 * Buffer structure for communicating simulation state between game and physics
 * threads.
 */
class FGeometryCollectionResults
{
public:
	FGeometryCollectionResults();

	void Reset();

	int32 NumTransformGroup() const { return Transforms.Num(); }

	void InitArrays(const FGeometryDynamicCollection& Other)
	{
		// Managed arrays
		Transforms.Init(Other.Transform);
		DynamicState.Init(Other.DynamicState);
		Parent.Init(Other.Parent);
		Children.Init(Other.Children);
		SimulationType.Init(Other.SimulationType);

		// Arrays
		const int32 NumTransforms = Other.NumElements(FGeometryCollection::TransformGroup);
		DisabledStates.SetNumUninitialized(NumTransforms);
		GlobalTransforms.SetNumUninitialized(NumTransforms);
		ParticleToWorldTransforms.SetNumUninitialized(NumTransforms);
	}

	float SolverDt;
	int32 BaseIndex;
	int32 NumParticlesAdded;
	TArray<bool> DisabledStates;
	TArray<FMatrix> GlobalTransforms;
	TArray<FTransform> ParticleToWorldTransforms;

	TManagedArray<int32> TransformIndex;

	TManagedArray<FTransform> Transforms;
	TManagedArray<int32> BoneMap;
	TManagedArray<int32> Parent;
	TManagedArray<TSet<int32>> Children;
	TManagedArray<int32> SimulationType;
	TManagedArray<int32> DynamicState;
	TManagedArray<float> Mass;
	TManagedArray<FVector> InertiaTensor;

	TManagedArray<int32> ClusterId;

	bool IsObjectDynamic;
	bool IsObjectLoading;

	FBoxSphereBounds WorldBounds;
};
