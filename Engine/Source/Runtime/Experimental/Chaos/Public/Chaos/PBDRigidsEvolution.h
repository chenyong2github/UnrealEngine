// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/PBDCollisionConstraints.h"
#include "Chaos/PBDCollisionConstraintsPGS.h"
#include "Chaos/PBDConstraintGraph.h"
#include "Chaos/PBDRigidClustering.h"
#include "Chaos/PBDRigidParticles.h"
#include "Chaos/PBDConstraintRule.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Transform.h"
#include "Chaos/Framework/DebugSubstep.h"
#include "HAL/Event.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "Chaos/SpatialAccelerationCollection.h"


// Declaring so it can be friended for tests.
namespace ChaosTest { void TestPendingSpatialDataHandlePointerConflict(); } 

namespace Chaos
{
template<class T, int d> class TPBDRigidsEvolutionGBF;
class FChaosArchive;

template <typename TPayload, typename T, int d>
class ISpatialAccelerationCollection;

struct CHAOS_API FEvolutionStats
{
	int32 ActiveCollisionPoints;
	int32 ActiveShapes;
	int32 ShapesForAllConstraints;
	int32 CollisionPointsForAllConstraints;

	FEvolutionStats()
	{
		Reset();
	}

	void Reset()
	{
		ActiveCollisionPoints = 0;
		ActiveShapes = 0;
		ShapesForAllConstraints = 0;
		CollisionPointsForAllConstraints = 0;
	}

	FEvolutionStats& operator+=(const FEvolutionStats& Other)
	{
		ActiveCollisionPoints += Other.ActiveCollisionPoints;
		ActiveShapes += Other.ActiveShapes;
		ShapesForAllConstraints += Other.ShapesForAllConstraints;
		CollisionPointsForAllConstraints += Other.CollisionPointsForAllConstraints;
		return *this;
	}
};

template <typename T, int d>
struct TSpatialAccelerationCacheHandle;

/** The SOA cache used for a single acceleration structure */
template <typename T, int d>
class TSpatialAccelerationCache : public TArrayCollection
{
public:
	using THandleType = TSpatialAccelerationCacheHandle<T, d>;

	TSpatialAccelerationCache()
	{
		AddArray(&MHasBoundingBoxes);
		AddArray(&MBounds);
		AddArray(&MPayloads);

#if PARTICLE_ITERATOR_RANGED_FOR_CHECK
		MDirtyValidationCount = 0;
#endif
	}

	TSpatialAccelerationCache(const TSpatialAccelerationCache<T, d>&) = delete;
	TSpatialAccelerationCache(TSpatialAccelerationCache<T, d>&& Other)
		: TArrayCollection()
		, MHasBoundingBoxes(MoveTemp(Other.MHasBoundingBoxes))
		, MBounds(MoveTemp(Other.MBounds))
		, MPayloads(MoveTemp(Other.MPayloads))
	{
		ResizeHelper(Other.MSize);
		Other.MSize = 0;

		AddArray(&MHasBoundingBoxes);
		AddArray(&MBounds);
		AddArray(&MPayloads);
#if PARTICLE_ITERATOR_RANGED_FOR_CHECK
		MDirtyValidationCount = 0;
#endif
	}

	TSpatialAccelerationCache& operator=(TSpatialAccelerationCache<T, d>&& Other)
	{
		if (&Other != this)
		{
			MHasBoundingBoxes = MoveTemp(Other.MHasBoundingBoxes);
			MBounds = MoveTemp(Other.MBounds);
			MPayloads = MoveTemp(Other.MPayloads);
			ResizeHelper(Other.MSize);
			Other.MSize = 0;
#if PARTICLE_ITERATOR_RANGED_FOR_CHECK
			MDirtyValidationCount = 0;
			++Other.MDirtyValidationCount;
#endif
		}

		return *this;
	}

#if PARTICLE_ITERATOR_RANGED_FOR_CHECK
	int32 DirtyValidationCount() const { return MDirtyValidationCount; }
#endif

	void AddElements(const int32 Num)
	{
		AddElementsHelper(Num);
		IncrementDirtyValidation();
	}

	void DestroyElement(const int32 Idx)
	{
		RemoveAtSwapHelper(Idx);
		IncrementDirtyValidation();
	}

	bool HasBounds(const int32 Idx) const { return MHasBoundingBoxes[Idx]; }
	bool& HasBounds(const int32 Idx) { return MHasBoundingBoxes[Idx]; }

	const TAABB<T,d>& Bounds(const int32 Idx) const { return MBounds[Idx]; }
	TAABB<T, d>& Bounds(const int32 Idx) { return MBounds[Idx]; }

	const TAccelerationStructureHandle<T, d>& Payload(const int32 Idx) const { return MPayloads[Idx]; }
	TAccelerationStructureHandle<T, d>& Payload(const int32 Idx) { return MPayloads[Idx]; }

private:
	void IncrementDirtyValidation()
	{
#if PARTICLE_ITERATOR_RANGED_FOR_CHECK
		++MDirtyValidationCount;
#endif
	}

	TArrayCollectionArray<bool> MHasBoundingBoxes;
	TArrayCollectionArray<TAABB<T, d>> MBounds;
	TArrayCollectionArray<TAccelerationStructureHandle<T, d>> MPayloads;

#if PARTICLE_ITERATOR_RANGED_FOR_CHECK
	int32 MDirtyValidationCount;
#endif
};

/** The handle the acceleration structure uses to access the data (similar to particle handle) */
template <typename T, int d>
struct TSpatialAccelerationCacheHandle
{
	using THandleBase = TSpatialAccelerationCacheHandle<T, d>;
	using TTransientHandle = TSpatialAccelerationCacheHandle<T, d>;

	TSpatialAccelerationCacheHandle(TSpatialAccelerationCache<T, d>* InCache = nullptr, int32 InEntryIdx = INDEX_NONE)
		: Cache(InCache)
		, EntryIdx(InEntryIdx)
	{}

	template <typename TPayloadType>
	TPayloadType GetPayload(int32 Idx) const
	{
		return Cache->Payload(EntryIdx);
	}

	bool HasBoundingBox() const
	{
		return Cache->HasBounds(EntryIdx);
	}

	const TAABB<T, d>& BoundingBox() const
	{
		return Cache->Bounds(EntryIdx);
	}

	union
	{
		TSpatialAccelerationCache<T, d>* GeometryParticles;	//using same name as particles SOA for template reuse, should probably rethink this
		TSpatialAccelerationCache<T, d>* Cache;
	};

	union
	{
		int32 ParticleIdx;	//same name for template reasons. Not really a particle idx
		int32 EntryIdx;
	};
};

template <typename T, int d>
struct CHAOS_API ISpatialAccelerationCollectionFactory
{
	//Create an empty acceleration collection with the desired buckets. Chaos enqueues acceleration structure operations per bucket
	virtual TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<T, d>, T, d>> CreateEmptyCollection() = 0;

	//Chaos creates new acceleration structures per bucket. Factory can change underlying type at runtime as well as number of buckets to AB test
	virtual TUniquePtr<ISpatialAcceleration<TAccelerationStructureHandle<T, d>, T, d>> CreateAccelerationPerBucket_Threaded(const TConstParticleView<TSpatialAccelerationCache<T, d>>& Particles, uint16 BucketIdx, bool ForceFullBuild) = 0;

	//Mask indicating which bucket is active. Spatial indices in inactive buckets fallback to bucket 0. Bit 0 indicates bucket 0 is active, Bit 1 indicates bucket 1 is active, etc...
	virtual uint8 GetActiveBucketsMask() const = 0;

	//Serialize the collection in and out
	virtual void Serialize(TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<T, d>, T, d>>& Ptr, FChaosArchive& Ar) = 0;

	virtual ~ISpatialAccelerationCollectionFactory() = default;
};

template<class FPBDRigidsEvolution, class FPBDCollisionConstraint, class T, int d>
class TPBDRigidsEvolutionBase
{
  public:
	typedef TFunction<void(TTransientPBDRigidParticleHandle<T,d>& Particle, const T)> FForceRule;
	typedef TFunction<void(const TArray<TGeometryParticleHandle<T, d>*>&, const T)> FUpdateVelocityRule;
	typedef TFunction<void(const TParticleView<TPBDRigidParticles<T, d>>&, const T)> FUpdatePositionRule;
	typedef TFunction<void(TPBDRigidParticles<T, d>&, const T, const T, const int32)> FKinematicUpdateRule;

	friend void ChaosTest::TestPendingSpatialDataHandlePointerConflict();

	CHAOS_API TPBDRigidsEvolutionBase(TPBDRigidsSOAs<T, d>& InParticles, int32 InNumIterations = 1, int32 InNumPushOutIterations = 1, bool InIsSingleThreaded = false);
	CHAOS_API virtual ~TPBDRigidsEvolutionBase();

	CHAOS_API TArray<TGeometryParticleHandle<T, d>*> CreateStaticParticles(int32 NumParticles, const TGeometryParticleParameters<T, d>& Params = TGeometryParticleParameters<T, d>())
	{
		auto NewParticles = Particles.CreateStaticParticles(NumParticles, Params);
		for (auto& Particle : NewParticles)
		{
			DirtyParticle(*Particle);
		}
		return NewParticles;
	}

	CHAOS_API TArray<TKinematicGeometryParticleHandle<T, d>*> CreateKinematicParticles(int32 NumParticles, const TKinematicGeometryParticleParameters<T, d>& Params = TKinematicGeometryParticleParameters<T, d>())
	{
		auto NewParticles = Particles.CreateKinematicParticles(NumParticles, Params);
		for (auto& Particle : NewParticles)
		{
			DirtyParticle(*Particle);
		}
		return NewParticles;
	}

	CHAOS_API TArray<TPBDRigidParticleHandle<T, d>*> CreateDynamicParticles(int32 NumParticles, const TPBDRigidParticleParameters<T, d>& Params = TPBDRigidParticleParameters<T, d>())
	{
		auto NewParticles = Particles.CreateDynamicParticles(NumParticles, Params);
		for (auto& Particle : NewParticles)
		{
			DirtyParticle(*Particle);
		}
		return NewParticles;
	}

	CHAOS_API TArray<TPBDRigidClusteredParticleHandle<T, d>*> CreateClusteredParticles(int32 NumParticles, const TPBDRigidParticleParameters<T, d>& Params = TPBDRigidParticleParameters<T, d>())
	{
		auto NewParticles = Particles.CreateClusteredParticles(NumParticles, Params);
		for (auto& Particle : NewParticles)
		{
			DirtyParticle(*Particle);
		}
		return NewParticles;
	}

	CHAOS_API void AddForceFunction(FForceRule ForceFunction) { ForceRules.Add(ForceFunction); }
	CHAOS_API void SetParticleUpdateVelocityFunction(FUpdateVelocityRule ParticleUpdate) { ParticleUpdateVelocity = ParticleUpdate; }
	CHAOS_API void SetParticleUpdatePositionFunction(FUpdatePositionRule ParticleUpdate) { ParticleUpdatePosition = ParticleUpdate; }

	CHAOS_API TGeometryParticleHandles<T, d>& GetParticleHandles() { return Particles.GetParticleHandles(); }
	CHAOS_API const TGeometryParticleHandles<T, d>& GetParticleHandles() const { return Particles.GetParticleHandles(); }

	CHAOS_API TPBDRigidsSOAs<T,d>& GetParticles() { return Particles; }
	CHAOS_API const TPBDRigidsSOAs<T, d>& GetParticles() const { return Particles; }

	CHAOS_API void AddConstraintRule(FPBDConstraintGraphRule* ConstraintRule)
	{
		uint32 ContainerId = (uint32)ConstraintRules.Num();
		ConstraintRules.Add(ConstraintRule);
		ConstraintRule->BindToGraph(ConstraintGraph, ContainerId);
	}

	CHAOS_API void SetNumIterations(int32 InNumIterations)
	{
		NumIterations = InNumIterations;
	}

	CHAOS_API void SetNumPushOutIterations(int32 InNumIterations)
	{
		NumPushOutIterations = InNumIterations;
	}

	CHAOS_API void EnableParticle(TGeometryParticleHandle<T,d>* Particle, const TGeometryParticleHandle<T, d>* ParentParticle)
	{
		DirtyParticle(*Particle);
		Particles.EnableParticle(Particle);
		ConstraintGraph.EnableParticle(Particle, ParentParticle);
	}

	CHAOS_API void DisableParticle(TGeometryParticleHandle<T,d>* Particle)
	{
		RemoveParticleFromAccelerationStructure(*Particle);
		Particles.DisableParticle(Particle);
		ConstraintGraph.DisableParticle(Particle);

		RemoveConstraints(TSet<TGeometryParticleHandle<T,d>*>({ Particle }));
	}

	template <bool bPersistent>
	FORCEINLINE_DEBUGGABLE void DirtyParticle(TGeometryParticleHandleImp<T, d, bPersistent>& Particle)
	{
		FPendingSpatialData& SpatialData = InternalAccelerationQueue.FindOrAdd(Particle.Handle());
		SpatialData.UpdateAccelerationHandle = TAccelerationStructureHandle<T,d>(Particle);
		SpatialData.bUpdate = true;
		SpatialData.UpdatedSpatialIdx = Particle.SpatialIdx();

		auto& AsyncSpatialData = AsyncAccelerationQueue.FindOrAdd(Particle.Handle());
		AsyncSpatialData.UpdateAccelerationHandle = TAccelerationStructureHandle<T, d>(Particle);
		AsyncSpatialData.bUpdate = true;
		AsyncSpatialData.UpdatedSpatialIdx = Particle.SpatialIdx();

		auto& ExternalSpatialData = ExternalAccelerationQueue.FindOrAdd(Particle.Handle());
		ExternalSpatialData.UpdateAccelerationHandle = TAccelerationStructureHandle<T, d>(Particle);
		ExternalSpatialData.bUpdate = true;
		ExternalSpatialData.UpdatedSpatialIdx = Particle.SpatialIdx();
	}

	void DestroyParticle(TGeometryParticleHandle<T, d>* Particle)
	{
		RemoveParticleFromAccelerationStructure(*Particle);
		ConstraintGraph.RemoveParticle(Particle);
		RemoveConstraints(TSet<TGeometryParticleHandle<T, d>*>({ Particle }));
		Particles.DestroyParticle(Particle);
	}

	CHAOS_API void CreateParticle(TGeometryParticleHandle<T, d>* ParticleAdded)
	{
		ConstraintGraph.AddParticle(ParticleAdded);
		DirtyParticle(*ParticleAdded);
	}

	CHAOS_API void SetParticleObjectState(TPBDRigidParticleHandle<T, d>* Particle, EObjectStateType ObjectState)
	{
		Particle->SetObjectStateLowLevel(ObjectState);
		Particles.SetDynamicParticleSOA(Particle);
	}

	CHAOS_API void DisableParticles(const TSet<TGeometryParticleHandle<T,d>*>& InParticles)
	{
		for (TGeometryParticleHandle<T, d>* Particle : InParticles)
		{
			Particles.DisableParticle(Particle);
			RemoveParticleFromAccelerationStructure(*Particle);
		}

		ConstraintGraph.DisableParticles(InParticles);

		RemoveConstraints(InParticles);
	}

	CHAOS_API void WakeIsland(const int32 Island)
	{
		ConstraintGraph.WakeIsland(Island);
		//Update Particles SOAs
		/*for (auto Particle : ContactGraph.GetIslandParticles(Island))
		{
			ActiveIndices.Add(Particle);
		}*/
	}

	// @todo(ccaulfield): Remove the uint version
	CHAOS_API void RemoveConstraints(const TSet<TGeometryParticleHandle<T, d>*>& RemovedParticles)
	{
		for (FPBDConstraintGraphRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->RemoveConstraints(RemovedParticles);
		}
	}

	//TEMP: this is only needed while clustering continues to use indices directly
	const auto& GetActiveClusteredArray() const { return Particles.GetActiveClusteredArray(); }
	const auto& GetNonDisabledClusteredArray() const { return Particles.GetNonDisabledClusteredArray(); }

	CHAOS_API TSerializablePtr<FChaosPhysicsMaterial> GetPhysicsMaterial(const TGeometryParticleHandle<T, d>* Particle) const { return Particle->AuxilaryValue(PhysicsMaterials); }
	CHAOS_API void SetPhysicsMaterial(TGeometryParticleHandle<T,d>* Particle, TSerializablePtr<FChaosPhysicsMaterial> InMaterial)
	{
		check(!Particle->AuxilaryValue(PerParticlePhysicsMaterials)); //shouldn't be setting non unique material if a unique one already exists
		Particle->AuxilaryValue(PhysicsMaterials) = InMaterial;
	}

	CHAOS_API const TArray<TGeometryParticleHandle<T,d>*>& GetIslandParticles(const int32 Island) const { return ConstraintGraph.GetIslandParticles(Island); }
	CHAOS_API int32 NumIslands() const { return ConstraintGraph.NumIslands(); }

	void InitializeAccelerationStructures()
	{
		ConstraintGraph.InitializeGraph(Particles.GetNonDisabledView());

		for (FPBDConstraintGraphRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->AddToGraph();
		}

		ConstraintGraph.ResetIslands(Particles.GetNonDisabledDynamicView());

		for (FPBDConstraintGraphRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->InitializeAccelerationStructures();
		}
	}

	void UpdateAccelerationStructures(int32 Island)
	{
		for (FPBDConstraintGraphRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->UpdateAccelerationStructures(Island);
		}
	}

	void ApplyConstraints(const T Dt, int32 Island)
	{
		UpdateAccelerationStructures(Island);

		// @todo(ccaulfield): track whether we are sufficiently solved and can early-out
		for (int i = 0; i < NumIterations; ++i)
		{
			for (FPBDConstraintGraphRule* ConstraintRule : PrioritizedConstraintRules)
			{
				ConstraintRule->ApplyConstraints(Dt, Island, i, NumIterations);
			}
		}
	}

	void ApplyKinematicTargets(const T Dt, const T StepFraction)
	{
		check(StepFraction > (T)0);
		check(StepFraction <= (T)1);

		// @todo(ccaulfield): optimize. Depending on the number of kinematics relative to the number that have 
		// targets set, it may be faster to process a command list rather than iterate over them all each frame. 
		const T MinDt = 1e-6f;
		for (auto& Particle : Particles.GetActiveKinematicParticlesView())
		{
			TKinematicTarget<T, d>& KinematicTarget = Particle.KinematicTarget();
			switch (KinematicTarget.GetMode())
			{
			case EKinematicTargetMode::None:
				// Nothing to do
				break;

			case EKinematicTargetMode::Zero:
			{
				// Reset velocity and then switch to do-nothing mode
				Particle.V() = TVector<T, d>(0, 0, 0);
				Particle.W() = TVector<T, d>(0, 0, 0);
				KinematicTarget.SetMode(EKinematicTargetMode::None);
				break;
			}

			case EKinematicTargetMode::Position:
			{
				// Move to kinematic target and update velocities to match
				// Target positions only need to be processed once, and we reset the velocity next frame (if no new target is set)
				TVector<T, d> TargetPos;
				TRotation<T, d> TargetRot;
				if (FMath::IsNearlyEqual(StepFraction, (T)1, KINDA_SMALL_NUMBER))
				{
					TargetPos = KinematicTarget.GetTarget().GetLocation();
					TargetRot = KinematicTarget.GetTarget().GetRotation();
					KinematicTarget.SetMode(EKinematicTargetMode::Zero);
				}
				else
				{
					TargetPos = TVector<T, d>::Lerp(Particle.X(), KinematicTarget.GetTarget().GetLocation(), StepFraction);
					TargetRot = TRotation<T, d>::Slerp(Particle.R(), KinematicTarget.GetTarget().GetRotation(), StepFraction);
				}
				if (Dt > MinDt)
				{
					TVector<float, 3> V = TVector<float, 3>::CalculateVelocity(Particle.X(), TargetPos, Dt);
					Particle.V() = V;

					TVector<float, 3> W = TRotation<float, 3>::CalculateAngularVelocity(Particle.R(), TargetRot, Dt);
					Particle.W() = W;
				}
				Particle.X() = TargetPos;
				Particle.R() = TargetRot;
				break;
			}

			case EKinematicTargetMode::Velocity:
			{
				// Move based on velocity
				Particle.X() = Particle.X() + Particle.V() * Dt;
				Particle.R() = TRotation<T, d>::IntegrateRotationWithAngularVelocity(Particle.R(), Particle.W(), Dt);
				break;
			}
			}
		}
	}

	/** Make a copy of the acceleration structure to allow for external modification. This is needed for supporting sync operations on SQ structure from game thread */
	CHAOS_API void UpdateExternalAccelerationStructure(TUniquePtr<ISpatialAccelerationCollection<TAccelerationStructureHandle<T, d>, T, d>>& ExternalStructure);
	ISpatialAccelerationCollection<TAccelerationStructureHandle<T, d>, T, d>* GetSpatialAcceleration() { return InternalAcceleration.Get(); }

	/** Perform a blocking flush of the spatial acceleration structure for situations where we aren't simulating but must have an up to date structure */
	CHAOS_API void FlushSpatialAcceleration();

	/** Rebuilds the spatial acceleration from scratch. This should only be used for perf testing */
	CHAOS_API void RebuildSpatialAccelerationForPerfTest();

	const auto& GetRigidClustering() const { return Clustering; }
	auto& GetRigidClustering() { return Clustering; }

	CHAOS_API const FPBDConstraintGraph& GetConstraintGraph() const { return ConstraintGraph; }
	CHAOS_API FPBDConstraintGraph& GetConstraintGraph() { return ConstraintGraph; }

	void Serialize(FChaosArchive& Ar);

protected:
	int32 NumConstraints() const
	{
		int32 NumConstraints = 0;
		for (const FPBDConstraintGraphRule* ConstraintRule : ConstraintRules)
		{
			NumConstraints += ConstraintRule->NumConstraints();
		}
		return NumConstraints;
	}

	template <bool bPersistent>
	FORCEINLINE_DEBUGGABLE void RemoveParticleFromAccelerationStructure(TGeometryParticleHandleImp<T, d, bPersistent>& ParticleHandle)
	{
		auto Particle = ParticleHandle.Handle();
		FPendingSpatialData& AsyncSpatialData = AsyncAccelerationQueue.FindOrAdd(Particle);

		if (!AsyncSpatialData.bDelete)
		{
			//There are three cases to consider:
			//Simple single delete happens, in that case just use the index you see (the first and only index)
			//Delete followed by any number deletes and updates and finally an update, in that case we must delete the first particle and add the final (so use first index for delete)
			//Delete followed by multiple updates and or deletes and a final delete. In that case we still only delete the first particle since the final delete is not really needed (add will be cancelled)
			AsyncSpatialData.DeletedSpatialIdx = ParticleHandle.SpatialIdx();

			// We cannot overwrite this handle if delete is already pending. (If delete is pending, that means this is the third case, cancelling update is sufficient),
			AsyncSpatialData.DeleteAccelerationHandle = TAccelerationStructureHandle<T, d>(ParticleHandle);
		}

		AsyncSpatialData.bUpdate = false;
		AsyncSpatialData.bDelete = true;

		// Delete data should match async, and update is not set, so this operation should be safe.
		ExternalAccelerationQueue.FindOrAdd(Particle) = AsyncSpatialData;

		//remove particle immediately for intermediate structure
		InternalAccelerationQueue.Remove(Particle);
		InternalAcceleration->RemoveElementFrom(AsyncSpatialData.DeleteAccelerationHandle, AsyncSpatialData.DeletedSpatialIdx);	//even though we remove immediately, future adds are still pending
	}

	void UpdateConstraintPositionBasedState(T Dt)
	{
		for (FPBDConstraintGraphRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->UpdatePositionBasedState(Dt);
		}
	}

	void CreateConstraintGraph()
	{
		ConstraintGraph.InitializeGraph(Particles.GetNonDisabledView());

		for (FPBDConstraintGraphRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->AddToGraph();
		}

		// Apply rules in priority order
		// @todo(ccaulfield): only really needed when list or priorities change
		PrioritizedConstraintRules = ConstraintRules;
		PrioritizedConstraintRules.StableSort();
	}

	void CreateIslands()
	{
		ConstraintGraph.UpdateIslands(Particles.GetNonDisabledDynamicView(), Particles);

		for (FPBDConstraintGraphRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->InitializeAccelerationStructures();
		}
	}
	
	void UpdateVelocities(const T Dt, int32 Island)
	{
		ParticleUpdateVelocity(ConstraintGraph.GetIslandParticles(Island), Dt);
	}

	void ApplyPushOut(const T Dt, int32 Island)
	{
		bool bNeedsAnotherIteration = true;
		for (int32 It = 0; bNeedsAnotherIteration && (It < NumPushOutIterations); ++It)
		{
			bNeedsAnotherIteration = false;
			for (FPBDConstraintGraphRule* ConstraintRule : PrioritizedConstraintRules)
			{
				if (ConstraintRule->ApplyPushOut(Dt, Island, It, NumPushOutIterations))
				{
					bNeedsAnotherIteration = true;
				}
			}
		}
	}

	using FAccelerationStructure = ISpatialAccelerationCollection<TAccelerationStructureHandle<T, d>, T, d>;

	void ComputeIntermediateSpatialAcceleration(bool bBlock = false);
	void FlushInternalAccelerationQueue();
	void FlushAsyncAccelerationQueue();
	void FlushExternalAccelerationQueue(FAccelerationStructure& Acceleration);
	void WaitOnAccelerationStructure();

	TArray<FForceRule> ForceRules;
	FUpdateVelocityRule ParticleUpdateVelocity;
	FUpdatePositionRule ParticleUpdatePosition;
	FKinematicUpdateRule KinematicUpdate;
	TArray<FPBDConstraintGraphRule*> ConstraintRules;
	TArray<FPBDConstraintGraphRule*> PrioritizedConstraintRules;
	FPBDConstraintGraph ConstraintGraph;
	TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>> PhysicsMaterials;
	TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>> PerParticlePhysicsMaterials;
	TArrayCollectionArray<int32> ParticleDisableCount;
	TArrayCollectionArray<bool> Collided;

	TPBDRigidsSOAs<T, d>& Particles;
	TUniquePtr<FAccelerationStructure> InternalAcceleration;
	TUniquePtr<FAccelerationStructure> AsyncInternalAcceleration;
	TUniquePtr<FAccelerationStructure> AsyncExternalAcceleration;
	TUniquePtr<FAccelerationStructure> ScratchExternalAcceleration;
	bool bExternalReady;
	bool bIsSingleThreaded;

	TPBDRigidClustering<FPBDRigidsEvolution, FPBDCollisionConstraint, T, d> Clustering;

	/** Used for updating intermediate spatial structures when they are finished */
	struct FPendingSpatialData
	{
		TAccelerationStructureHandle<T, d> UpdateAccelerationHandle;
		TAccelerationStructureHandle<T, d> DeleteAccelerationHandle;
		FSpatialAccelerationIdx UpdatedSpatialIdx;
		FSpatialAccelerationIdx DeletedSpatialIdx;	//need both updated and deleted in case memory is reused but a different idx is neede
		bool bUpdate;
		bool bDelete;

		FPendingSpatialData()
			: bUpdate(false)
			, bDelete(false)
		{}

		void Serialize(FChaosArchive& Ar)
		{
			Ar.UsingCustomVersion(FExternalPhysicsCustomObjectVersion::GUID);
			if (Ar.CustomVer(FExternalPhysicsCustomObjectVersion::GUID) >= FExternalPhysicsCustomObjectVersion::SerializeHashResult)
			{
				Ar << UpdateAccelerationHandle;
				Ar << DeleteAccelerationHandle;
			}
			else
			{
				Ar << UpdateAccelerationHandle;
				DeleteAccelerationHandle = UpdateAccelerationHandle;
			}

			Ar << bUpdate;
			Ar << bDelete;

			Ar << UpdatedSpatialIdx;
			Ar << DeletedSpatialIdx;
		}
	};

	/** Pending operations for the internal acceleration structure */
	TMap<TGeometryParticleHandle<T, d>*, FPendingSpatialData> InternalAccelerationQueue;

	/** Pending operations for the acceleration structures being rebuilt asynchronously */
	TMap<TGeometryParticleHandle<T, d>*, FPendingSpatialData> AsyncAccelerationQueue;

	/** Pending operations for the external acceleration structure*/
	TMap<TGeometryParticleHandle<T, d>*, FPendingSpatialData> ExternalAccelerationQueue;

	void SerializePendingMap(FChaosArchive& Ar, TMap<TGeometryParticleHandle<T, d>*, FPendingSpatialData>& Map)
	{
		TArray<TGeometryParticleHandle<T, d>*> Keys;
		if (!Ar.IsLoading())
		{
			Map.GenerateKeyArray(Keys);
		}
		Ar << AsAlwaysSerializableArray(Keys);
		for (auto Key : Keys)
		{
			FPendingSpatialData& PendingData = Map.FindOrAdd(Key);
			PendingData.Serialize(Ar);
		}
	}

	/** Used for async acceleration rebuild */
	TMap<TGeometryParticleHandle<T, d>*, uint32> ParticleToCacheInnerIdx;

	TMap<FSpatialAccelerationIdx, TUniquePtr<TSpatialAccelerationCache<T, d>>> SpatialAccelerationCache;

	FORCEINLINE_DEBUGGABLE void ApplyParticlePendingData(const FPendingSpatialData& PendingData, FAccelerationStructure& SpatialAcceleration, bool bUpdateCache);

	class FChaosAccelerationStructureTask
	{
	public:
		FChaosAccelerationStructureTask(ISpatialAccelerationCollectionFactory<T,d>& InSpatialCollectionFactory
			, const TMap<FSpatialAccelerationIdx, TUniquePtr<TSpatialAccelerationCache<T,d>>>& InSpatialAccelerationCache
			, TUniquePtr<FAccelerationStructure>& InAccelerationStructure
			, TUniquePtr<FAccelerationStructure>& InAccelerationStructureCopy
			, bool InForceFullBuild
			, bool InIsSingleThreaded);
		static FORCEINLINE TStatId GetStatId();
		static FORCEINLINE ENamedThreads::Type GetDesiredThread();
		static FORCEINLINE ESubsequentsMode::Type GetSubsequentsMode();
		void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent);

		ISpatialAccelerationCollectionFactory<T, d>& SpatialCollectionFactory;
		const TMap<FSpatialAccelerationIdx, TUniquePtr<TSpatialAccelerationCache<T, d>>>& SpatialAccelerationCache;
		TUniquePtr<FAccelerationStructure>& AccelerationStructure;
		TUniquePtr<FAccelerationStructure>& AccelerationStructureCopy;
		bool IsForceFullBuild;
		bool bIsSingleThreaded;
	};
	FGraphEventRef AccelerationStructureTaskComplete;

	int32 NumIterations;
	int32 NumPushOutIterations;
	TUniquePtr<ISpatialAccelerationCollectionFactory<T, d>> SpatialCollectionFactory;
};

}

// Only way to make this compile at the moment due to visibility attribute issues. TODO: Change this once a fix for this problem is applied.
#if PLATFORM_MAC || PLATFORM_LINUX
extern template class CHAOS_API Chaos::TPBDRigidsEvolutionBase<Chaos::TPBDRigidsEvolutionGBF<float, 3>, Chaos::TPBDCollisionConstraints<float,3>, float, 3>;
#else
extern template class Chaos::TPBDRigidsEvolutionBase<Chaos::TPBDRigidsEvolutionGBF<float, 3>, Chaos::TPBDCollisionConstraints<float,3>, float, 3>;
#endif
