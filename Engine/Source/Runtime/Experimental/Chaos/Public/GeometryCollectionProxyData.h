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
	TManagedArray<FVector3f> InitialAngularVelocity;
	TManagedArray<FVector3f> InitialLinearVelocity;
	TManagedArray<FTransform> MassToLocal;
	//TManagedArray<TArray<FCollisionFilterData>> ShapeQueryData;
	//TManagedArray<TArray<FCollisionFilterData>> ShapeSimData;
	TManagedArray<TUniquePtr<FCollisionStructureManager::FSimplicial>> Simplicials;
	TManagedArray<bool> SimulatableParticles;
};


class FGeometryCollectioPerFrameData
{
public:
	FGeometryCollectioPerFrameData()
		: IsWorldTransformDirty(false) {}

	const FTransform& GetWorldTransform() const { return WorldTransform; }

	void SetWorldTransform(const FTransform& InWorldTransform)
	{
		if (!WorldTransform.Equals(InWorldTransform))
		{
			WorldTransform = InWorldTransform;
			IsWorldTransformDirty = true;
		}
	}

	bool GetIsWorldTransformDirty() const { return IsWorldTransformDirty; }
	void ResetIsWorldTransformDirty() { IsWorldTransformDirty = false; }

private:
	FTransform WorldTransform;
	bool IsWorldTransformDirty;
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
		const int32 NumTransforms = Other.NumElements(FGeometryCollection::TransformGroup);
		DisabledStates.SetNumUninitialized(NumTransforms);
		GlobalTransforms.SetNumUninitialized(NumTransforms);
		ParticleToWorldTransforms.SetNumUninitialized(NumTransforms);

		Transforms.SetNumUninitialized(NumTransforms);
		Parent.SetNumUninitialized(NumTransforms);
		DynamicState.SetNumUninitialized(NumTransforms);
	}

	Chaos::FReal SolverDt;
	TArray<bool> DisabledStates;
	TArray<FMatrix> GlobalTransforms;
	TArray<FTransform> ParticleToWorldTransforms;

	TArray<FTransform> Transforms;
	TArray<int32> Parent;
	TArray<int32> DynamicState;
	
	bool IsObjectDynamic;
	bool IsObjectLoading;
};
