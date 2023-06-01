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
	TManagedArray<FTransform> MassToLocal;
	TManagedArray<TUniquePtr<FCollisionStructureManager::FSimplicial>> Simplicials;
	TManagedArray<bool> SimulatableParticles;

public:
	struct FInitialVelocityFacade
	{
		FInitialVelocityFacade(FGeometryDynamicCollection& DynamicCollection);
		FInitialVelocityFacade(const FGeometryDynamicCollection& DynamicCollection);

		bool IsValid() const;
		void DefineSchema();
		void Fill(const FVector3f& InitialLinearVelocity, const FVector3f& InitialAngularVelocity);
		void CopyFrom(const FGeometryDynamicCollection& SourceCollection);

		TManagedArrayAccessor<FVector3f> InitialLinearVelocityAttribute;
		TManagedArrayAccessor<FVector3f> InitialAngularVelocityAttribute;
	};

	FInitialVelocityFacade GetInitialVelocityFacade() { return FInitialVelocityFacade(*this); }
	FInitialVelocityFacade GetInitialVelocityFacade() const { return FInitialVelocityFacade(*this); }

	void CopyInitialVelocityAttributesFrom(const FGeometryDynamicCollection& SourceCollection);
};

/**
 * Provides an API for dynamic state related attributes
 * physics state , broken state, current parent (normal or internal clusters )
 * To be used with the dynamic collection
 */
class CHAOS_API FGeometryCollectionDynamicStateFacade
{
public:
	FGeometryCollectionDynamicStateFacade(FManagedArrayCollection& InCollection);

	/** returns true if all the necessary attributes are present */
	bool IsValid() const;

	/** return true if the transform is in a dynamic or sleeping state */
	bool IsDynamicOrSleeping(int32 TransformIndex) const;

	/** return true if the transform is in a sleeping state */
	bool IsSleeping(int32 TransformIndex) const;

	/** whether there's children attached to this transfom (Cluster) */
	bool HasChildren(int32 TransformIndex) const;
	
	/** return true if the transform has broken off its parent */
	bool HasBrokenOff(int32 TransformIndex) const;

	/** return true if the transform has an internal cluster parent */
	bool HasInternalClusterParent(int32 TransformIndex) const;

	/** return true if the transform has an internal cluster parent in a dynamic state */
	bool HasDynamicInternalClusterParent(int32 TransformIndex) const;
	
private:
	/** Active state, true means that the transform is active or broken off from its parent */
	TManagedArrayAccessor<bool> ActiveAttribute;

	/** physics state of the transform (Dynamic, kinematic, static, sleeping) */
	TManagedArrayAccessor<int32> DynamicStateAttribute;

	/** currently attached children (potentially different from the initial children setup) */
	TManagedArrayAccessor<TSet<int32>> ChildrenAttribute;
	
	/** Current parent (potentially different from the initial parent) */
	TManagedArrayAccessor<int32> ParentAttribute;

	/** type of internal state parent */
	TManagedArrayAccessor<uint8> InternalClusterParentTypeAttribute;
};

class FGeometryCollectioPerFrameData
{
public:
	FGeometryCollectioPerFrameData()
		: bIsWorldTransformDirty(false)
		, bIsCollisionFilterDataDirty(false)
		, bIsNotificationDataDirty(false) 
		, bIsDamageSettingsDataDirty(false)
		, bNotifyBreakings(false)
		, bNotifyRemovals(false)
		, bNotifyCrumblings(false)
		, bCrumblingEventIncludesChildren(false)
		, bNotifyGlobalBreakings(false)
		, bNotifyGlobalRemovals(false)
		, bNotifyGlobalCrumblings(false)
		, bGlobalCrumblingEventIncludesChildren(false)
		, bEnableStrainOnCollision(false)
	{}

	const FTransform& GetWorldTransform() const { return WorldTransform; }

	void SetWorldTransform(const FTransform& InWorldTransform)
	{
		if (!WorldTransform.Equals(InWorldTransform))
		{
			WorldTransform = InWorldTransform;
			bIsWorldTransformDirty = true;
		}
	}

	bool GetIsWorldTransformDirty() const { return bIsWorldTransformDirty; }
	void ResetIsWorldTransformDirty() { bIsWorldTransformDirty = false; }

	const FCollisionFilterData& GetSimFilter() const { return SimFilter; }
	void SetSimFilter(const FCollisionFilterData& NewSimFilter)
	{
		SimFilter = NewSimFilter;
		bIsCollisionFilterDataDirty = true;
	}

	const FCollisionFilterData& GetQueryFilter() const { return QueryFilter; }
	void SetQueryFilter(const FCollisionFilterData& NewQueryFilter)
	{
		QueryFilter = NewQueryFilter;
		bIsCollisionFilterDataDirty = true;
	}

	bool GetIsCollisionFilterDataDirty() const { return bIsCollisionFilterDataDirty; }
	void ResetIsCollisionFilterDataDirty() { bIsCollisionFilterDataDirty = false; }

	void SetNotifyBreakings(bool bNotify)
	{
		bNotifyBreakings = bNotify;
		bIsNotificationDataDirty = true;
	}
	bool GetNotifyBreakings() const { return bNotifyBreakings; }

	void SetNotifyRemovals(bool bNotify)
	{
		bNotifyRemovals = bNotify;
		bIsNotificationDataDirty = true;
	}

	bool GetNotifyRemovals() const { return bNotifyRemovals; }

	void SetNotifyCrumblings(bool bNotify, bool bIncludeChildren)
	{
		bNotifyCrumblings = bNotify;
		bCrumblingEventIncludesChildren = bIncludeChildren;
		bIsNotificationDataDirty = true;
	}

	bool GetNotifyCrumblings() const { return bNotifyCrumblings; }
	bool GetCrumblingEventIncludesChildren() const { return bCrumblingEventIncludesChildren; }

	void SetNotifyGlobalBreakings(bool bNotify)
	{
		bNotifyGlobalBreakings = bNotify;
		bIsNotificationDataDirty = true;
	}
	bool GetNotifyGlobalBreakings() const { return bNotifyGlobalBreakings; }

	void SetNotifyGlobalRemovals(bool bNotify)
	{
		bNotifyGlobalRemovals = bNotify;
		bIsNotificationDataDirty = true;
	}
	bool GetNotifyGlobalRemovals() const { return bNotifyGlobalRemovals; }

	void SetNotifyGlobalCrumblings(bool bNotify, bool bIncludeChildren)
	{
		bNotifyGlobalCrumblings = bNotify;
		bGlobalCrumblingEventIncludesChildren = bIncludeChildren;
		bIsNotificationDataDirty = true;
	}

	bool GetNotifyGlobalCrumblings() const { return bNotifyGlobalCrumblings; }
	bool GetGlobalCrumblingEventIncludesChildren() const { return bGlobalCrumblingEventIncludesChildren; }


	bool GetIsNotificationDataDirty() const { return bIsNotificationDataDirty; }
	void ResetIsNotificationDataDirty() { bIsNotificationDataDirty = false; }

	void SetEnableStrainOnCollision(bool bEnable)
	{
		bEnableStrainOnCollision = bEnable;
		bIsDamageSettingsDataDirty = true;
	}

	bool GetEnableStrainOnCollision() const { return bEnableStrainOnCollision; }

	bool GetIsDamageSettingsDataDirty() const { return bIsDamageSettingsDataDirty; }
	void ResetIsDamageSettingsDataDirty() { bIsDamageSettingsDataDirty = false; }

private:
	uint16 bIsWorldTransformDirty : 1;
	uint16 bIsCollisionFilterDataDirty : 1;
	uint16 bIsNotificationDataDirty: 1;
	uint16 bIsDamageSettingsDataDirty : 1;

	/** updated when bNotificationDataDirty is set */
	uint16 bNotifyBreakings : 1;
	uint16 bNotifyRemovals : 1;
	uint16 bNotifyCrumblings : 1;
	uint16 bCrumblingEventIncludesChildren : 1;
	uint16 bNotifyGlobalBreakings : 1;
	uint16 bNotifyGlobalRemovals : 1;
	uint16 bNotifyGlobalCrumblings : 1;
	uint16 bGlobalCrumblingEventIncludesChildren : 1;

	/** updated when bDamageSettingsDataDirty is set */
	uint16 bEnableStrainOnCollision : 1;

	/** updated when bIsWorldTransformDirty is set */
	FTransform WorldTransform; 

	/** updated when bIsCollisionFilterDataDirty is set */
	FCollisionFilterData SimFilter;
	FCollisionFilterData QueryFilter;
};

/**
 * Buffer structure for communicating simulation state between game and physics
 * threads.
 */
class FGeometryCollectionResults: public FRefCountedObject
{
public:
	FGeometryCollectionResults();

	int32 GetNumEntries() const { return Transforms.Num(); }

	void Reset();

	void InitArrays(const FGeometryDynamicCollection& Collection)
	{
		const int32 NumTransforms = Collection.NumElements(FGeometryCollection::TransformGroup);
		ModifiedTransformIndices.Init(false, NumTransforms);
#if WITH_EDITORONLY_DATA
		if (Damages.Num() != NumTransforms)
		{
			Damages.SetNumUninitialized(NumTransforms);
		}
#endif	
	}

	using FEntryIndex = int32;

	struct FState
	{
		uint16 DynamicState: 8; // need to fit EObjectStateTypeEnum
		uint16 DisabledState: 1;
		uint16 HasInternalClusterParent: 1;
		uint16 DynamicInternalClusterParent: 1;
		// 5 bits left
	};

	struct FStateData
	{
		int32  TransformIndex;
		int32  ParentTransformIndex;
		int32  InternalClusterUniqueIdx;
		FState State;
	};

	struct FPositionData
	{
		Chaos::FVec3 ParticleX;
		Chaos::FRotation3 ParticleR;
	};

	struct FVelocityData
	{
		Chaos::FVec3f ParticleV;
		Chaos::FVec3f ParticleW;
	};

#if WITH_EDITORONLY_DATA
	struct FDamageData
	{
		float Damage = 0;
		float DamageThreshold = 0;
	};

	void SetDamages(int32 TransformIndex, const FDamageData& DamageData)
	{
		Damages[TransformIndex] = DamageData;
	}

	const FDamageData& GetDamages(int32 TransformIndex) const
	{
		return Damages[TransformIndex];
	}
#endif

	inline FEntryIndex GetEntryIndexByTransformIndex(int32 TransformIndex) const
	{
		if (ModifiedTransformIndices[TransformIndex])
		{
			return ModifiedTransformIndices.CountSetBits(0, TransformIndex + 1) - 1;
		}
		return INDEX_NONE;
	}

	inline const FStateData& GetState(FEntryIndex EntryIndex) const
	{
		return States[EntryIndex];
	}

	inline const FPositionData& GetPositions(FEntryIndex EntryIndex) const
	{
		return Positions[EntryIndex];
	}

	inline const FVelocityData& GetVelocities(FEntryIndex EntryIndex) const
	{
		return Velocities[EntryIndex];
	}

	inline const FTransform& GetTransform(FEntryIndex EntryIndex) const
	{
		return Transforms[EntryIndex];
	}

	inline void SetSolverDt(const Chaos::FReal SolverDtIn)
	{
		SolverDt = SolverDtIn;
	}

	inline void SetState(int32 EntryIndex, const FStateData& StateData)
	{
		States[EntryIndex] = StateData;
	}

	inline FEntryIndex AddEntry(int32 TransformIndex)
	{
		ModifiedTransformIndices[TransformIndex] = true;
		const FEntryIndex EntryIndex = States.AddDefaulted();
		ensure(GetEntryIndexByTransformIndex(TransformIndex) == EntryIndex);
		Positions.AddDefaulted();
		Velocities.AddDefaulted();
		Transforms.AddDefaulted();
		return EntryIndex;
	}

	inline void SetPositions(FEntryIndex EntryIndex, const FPositionData& PositionData)
	{
		Positions[EntryIndex] = PositionData;
	}

	inline void SetVelocities(FEntryIndex EntryIndex, const FVelocityData& VelocityData)
	{
		Velocities[EntryIndex] = VelocityData;
	}

	inline void SetTransform(FEntryIndex EntryIndex, const FTransform& Transform)
	{
		Transforms[EntryIndex] = Transform;
	}

private:
	Chaos::FReal SolverDt;

	// we only store the data for modified transforms
	// ModifiedTransformIndices contains which transform has been set 
	// use the API to retrieve the entry Index matching a specific transform index
	TBitArray<> ModifiedTransformIndices;
	TArray<FStateData> States;
	TArray<FPositionData> Positions;
	TArray<FVelocityData> Velocities;
	TArray<FTransform> Transforms;

#if WITH_EDITORONLY_DATA
	// use to display impulse statistics in editor
	// this is indexed on the transform index
	TArray<FDamageData> Damages;
#endif

public:
	uint8 IsObjectDynamic: 1;
	uint8 IsObjectLoading: 1;
};
