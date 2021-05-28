// Copyright Epic Games, Inc. All Rights Reserved.
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
#include "Chaos/PBDJointConstraints.h"
#include "Chaos/PBDRigidsSOAs.h"
#include "Chaos/SpatialAccelerationCollection.h"
#include "Chaos/PBDRigidsEvolutionFwd.h"
#include "Chaos/Defines.h"
#include "Chaos/PendingSpatialData.h"


extern int32 ChaosRigidsEvolutionApplyAllowEarlyOutCVar;
extern int32 ChaosRigidsEvolutionApplyPushoutAllowEarlyOutCVar;
extern int32 ChaosNumPushOutIterationsOverride;
extern int32 ChaosNumContactIterationsOverride;

namespace Chaos
{

extern CHAOS_API int32 FixBadAccelerationStructureRemoval;

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

struct FSpatialAccelerationCacheHandle;

/** The SOA cache used for a single acceleration structure */
class FSpatialAccelerationCache : public TArrayCollection
{
public:
	using THandleType = FSpatialAccelerationCacheHandle;

	FSpatialAccelerationCache()
	{
		AddArray(&MHasBoundingBoxes);
		AddArray(&MBounds);
		AddArray(&MPayloads);

#if PARTICLE_ITERATOR_RANGED_FOR_CHECK
		MDirtyValidationCount = 0;
#endif
	}

	FSpatialAccelerationCache(const FSpatialAccelerationCache&) = delete;
	FSpatialAccelerationCache(FSpatialAccelerationCache&& Other)
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

	FSpatialAccelerationCache& operator=(FSpatialAccelerationCache&& Other)
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

	const FAABB3& Bounds(const int32 Idx) const { return MBounds[Idx]; }
	FAABB3& Bounds(const int32 Idx) { return MBounds[Idx]; }

	const FAccelerationStructureHandle& Payload(const int32 Idx) const { return MPayloads[Idx]; }
	FAccelerationStructureHandle& Payload(const int32 Idx) { return MPayloads[Idx]; }

private:
	void IncrementDirtyValidation()
	{
#if PARTICLE_ITERATOR_RANGED_FOR_CHECK
		++MDirtyValidationCount;
#endif
	}

	TArrayCollectionArray<bool> MHasBoundingBoxes;
	TArrayCollectionArray<FAABB3> MBounds;
	TArrayCollectionArray<FAccelerationStructureHandle> MPayloads;

#if PARTICLE_ITERATOR_RANGED_FOR_CHECK
	int32 MDirtyValidationCount;
#endif
};

/** The handle the acceleration structure uses to access the data (similar to particle handle) */
struct FSpatialAccelerationCacheHandle
{
	using THandleBase = FSpatialAccelerationCacheHandle;
	using TTransientHandle = FSpatialAccelerationCacheHandle;

	FSpatialAccelerationCacheHandle(FSpatialAccelerationCache* InCache = nullptr, int32 InEntryIdx = INDEX_NONE)
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

	const FAABB3& BoundingBox() const
	{
		return Cache->Bounds(EntryIdx);
	}

	union
	{
		FSpatialAccelerationCache* GeometryParticles;	//using same name as particles SOA for template reuse, should probably rethink this
		FSpatialAccelerationCache* Cache;
	};

	union
	{
		int32 ParticleIdx;	//same name for template reasons. Not really a particle idx
		int32 EntryIdx;
	};
};

struct CHAOS_API ISpatialAccelerationCollectionFactory
{
	//Create an empty acceleration collection with the desired buckets. Chaos enqueues acceleration structure operations per bucket
	virtual TUniquePtr<ISpatialAccelerationCollection<FAccelerationStructureHandle, FReal, 3>> CreateEmptyCollection() = 0;

	// Determines if bucket implements time slicing.
	virtual bool IsBucketTimeSliced(uint16 BucketIdx) const = 0;

	//Chaos creates new acceleration structures per bucket. Factory can change underlying type at runtime as well as number of buckets to AB test
	virtual TUniquePtr<ISpatialAcceleration<FAccelerationStructureHandle, FReal, 3>> CreateAccelerationPerBucket_Threaded(const TConstParticleView<FSpatialAccelerationCache>& Particles, uint16 BucketIdx, bool ForceFullBuild) = 0;

	//Mask indicating which bucket is active. Spatial indices in inactive buckets fallback to bucket 0. Bit 0 indicates bucket 0 is active, Bit 1 indicates bucket 1 is active, etc...
	virtual uint8 GetActiveBucketsMask() const = 0;

	//Serialize the collection in and out
	virtual void Serialize(TUniquePtr<ISpatialAccelerationCollection<FAccelerationStructureHandle, FReal, 3>>& Ptr, FChaosArchive& Ar) = 0;

	virtual ~ISpatialAccelerationCollectionFactory() = default;
};

class FPBDRigidsEvolutionBase
{
public:
	using FAccelerationStructure = ISpatialAccelerationCollection<FAccelerationStructureHandle,FReal,3>;

	typedef TFunction<void(TTransientPBDRigidParticleHandle<FReal, 3>& Particle, const FReal)> FForceRule;
	typedef TFunction<void(const TArray<FGeometryParticleHandle*>&, const FReal)> FUpdateVelocityRule;
	typedef TFunction<void(const TParticleView<FPBDRigidParticles>&, const FReal)> FUpdatePositionRule;
	typedef TFunction<void(FPBDRigidParticles&, const FReal, const FReal, const int32)> FKinematicUpdateRule;
	typedef TFunction<void(TParticleView<FPBDRigidParticles>&)> FCaptureRewindRule;

	CHAOS_API FPBDRigidsEvolutionBase(FPBDRigidsSOAs& InParticles, THandleArray<FChaosPhysicsMaterial>& InSolverPhysicsMaterials, int32 InNumIterations = 1, int32 InNumPushOutIterations = 1, bool InIsSingleThreaded = false);
	CHAOS_API virtual ~FPBDRigidsEvolutionBase();

	CHAOS_API TArray<FGeometryParticleHandle*> CreateStaticParticles(int32 NumParticles, const FUniqueIdx* ExistingIndices = nullptr, const FGeometryParticleParameters& Params = FGeometryParticleParameters())
	{
		auto NewParticles = Particles.CreateStaticParticles(NumParticles, ExistingIndices, Params);
		for (auto& Particle : NewParticles)
		{
			DirtyParticle(*Particle);
		}
		return NewParticles;
	}

	CHAOS_API TArray<FKinematicGeometryParticleHandle*> CreateKinematicParticles(int32 NumParticles, const FUniqueIdx* ExistingIndices = nullptr, const FKinematicGeometryParticleParameters& Params = FKinematicGeometryParticleParameters())
	{
		auto NewParticles = Particles.CreateKinematicParticles(NumParticles, ExistingIndices, Params);
		for (auto& Particle : NewParticles)
		{
			DirtyParticle(*Particle);
		}
		return NewParticles;
	}

	CHAOS_API TArray<FPBDRigidParticleHandle*> CreateDynamicParticles(int32 NumParticles, const FUniqueIdx* ExistingIndices = nullptr, const FPBDRigidParticleParameters& Params = FPBDRigidParticleParameters())
	{
		auto NewParticles = Particles.CreateDynamicParticles(NumParticles, ExistingIndices, Params);
		for (auto& Particle : NewParticles)
		{
			DirtyParticle(*Particle);
		}
		return NewParticles;
	}

	CHAOS_API TArray<TPBDRigidClusteredParticleHandle<FReal, 3>*> CreateClusteredParticles(int32 NumParticles,const FUniqueIdx* ExistingIndices = nullptr,  const FPBDRigidParticleParameters& Params = FPBDRigidParticleParameters())
	{
		auto NewParticles = Particles.CreateClusteredParticles(NumParticles, ExistingIndices, Params);
		for (auto& Particle : NewParticles)
		{
			DirtyParticle(*Particle);
		}
		return NewParticles;
	}

	CHAOS_API TArray<TPBDGeometryCollectionParticleHandle<FReal, 3>*> CreateGeometryCollectionParticles(int32 NumParticles,const FUniqueIdx* ExistingIndices = nullptr,  const FPBDRigidParticleParameters& Params = FPBDRigidParticleParameters())
	{
		auto NewParticles = Particles.CreateGeometryCollectionParticles(NumParticles, ExistingIndices, Params);
		for (auto& Particle : NewParticles)
		{
			DirtyParticle(*Particle);
		}
		return NewParticles;
	}

	CHAOS_API void AddForceFunction(FForceRule ForceFunction) { ForceRules.Add(ForceFunction); }
	CHAOS_API void AddImpulseFunction(FForceRule ImpulseFunction) { ImpulseRules.Add(ImpulseFunction); }
	CHAOS_API void SetParticleUpdateVelocityFunction(FUpdateVelocityRule ParticleUpdate) { ParticleUpdateVelocity = ParticleUpdate; }
	CHAOS_API void SetParticleUpdatePositionFunction(FUpdatePositionRule ParticleUpdate) { ParticleUpdatePosition = ParticleUpdate; }
	CHAOS_API void SetCaptureRewindDataFunction(FCaptureRewindRule Rule){ CaptureRewindData = Rule; }

	CHAOS_API TGeometryParticleHandles<FReal, 3>& GetParticleHandles() { return Particles.GetParticleHandles(); }
	CHAOS_API const TGeometryParticleHandles<FReal, 3>& GetParticleHandles() const { return Particles.GetParticleHandles(); }

	CHAOS_API FPBDRigidsSOAs& GetParticles() { return Particles; }
	CHAOS_API const FPBDRigidsSOAs& GetParticles() const { return Particles; }

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

	CHAOS_API int32 GetNumIterations() const
	{
		return NumIterations;
	}

	CHAOS_API void SetNumPushOutIterations(int32 InNumIterations)
	{
		NumPushOutIterations = InNumIterations;
	}

	CHAOS_API int32 GetNumPushOutIterations() const
	{
		return NumPushOutIterations;
	}

	CHAOS_API void EnableParticle(FGeometryParticleHandle* Particle, const FGeometryParticleHandle* ParentParticle)
	{
		Particles.EnableParticle(Particle);
		ConstraintGraph.EnableParticle(Particle, ParentParticle);
		DirtyParticle(*Particle);
	}

	CHAOS_API void DisableParticle(FGeometryParticleHandle* Particle)
	{
		RemoveParticleFromAccelerationStructure(*Particle);
		Particles.DisableParticle(Particle);
		ConstraintGraph.DisableParticle(Particle);
		DisableConstraints(TSet<FGeometryParticleHandle*>({ Particle }));
	}

	CHAOS_API void FlushExternalAccelerationQueue(FAccelerationStructure& Acceleration,FPendingSpatialDataQueue& ExternalQueue);

	CHAOS_API void DisableParticles(TSet<FGeometryParticleHandle*> &ParticlesIn)
	{
		for (FGeometryParticleHandle* Particle : ParticlesIn)
		{
			RemoveParticleFromAccelerationStructure(*Particle);
			Particles.DisableParticle(Particle);
			ConstraintGraph.DisableParticle(Particle);
		}
		DisableConstraints(ParticlesIn);
	}

	template <bool bPersistent>
	FORCEINLINE_DEBUGGABLE void DirtyParticle(TGeometryParticleHandleImp<FReal, 3, bPersistent>& Particle)
	{
		const TPBDRigidParticleHandleImp<FReal, 3, bPersistent>* AsRigid = Particle.CastToRigidParticle();
		if(AsRigid && AsRigid->Disabled())
		{
			TPBDRigidClusteredParticleHandleImp<FReal, 3, bPersistent>* AsClustered = Particle.CastToClustered();

			if(AsClustered)
			{
				// For clustered particles, they may appear disabled but they're being driven by an internal (solver-owned) cluster parent.
				// If this is the case we let the spatial data update with those particles, otherwise skip.
				// #BGTODO consider converting MDisabled into a bitfield for multiple disable types (Disabled, DisabledDriven, etc.)
				if(FPBDRigidParticleHandle* ClusterParentBase = AsClustered->ClusterIds().Id)
				{
					if(Chaos::TPBDRigidClusteredParticleHandle<FReal, 3>* ClusterParent = ClusterParentBase->CastToClustered())
					{
						if(!ClusterParent->InternalCluster())
						{
							return;
						}
					}
				}
			}
			else
			{
				// Disabled particles take no immediate part in sim or query so shouldn't be added to the acceleration
				return;
			}
		}

		//only add to acceleration structure if it has collision
		if (Particle.HasCollision())
		{
			//TODO: distinguish between new particles and dirty particles
			const FUniqueIdx UniqueIdx = Particle.UniqueIdx();
			FPendingSpatialData& SpatialData = InternalAccelerationQueue.FindOrAdd(UniqueIdx);
			ensure(SpatialData.bDelete == false);
			SpatialData.AccelerationHandle = FAccelerationStructureHandle(Particle);
			SpatialData.SpatialIdx = Particle.SpatialIdx();

			auto& AsyncSpatialData = AsyncAccelerationQueue.FindOrAdd(UniqueIdx);
			ensure(SpatialData.bDelete == false);
			AsyncSpatialData = SpatialData;
		}
	}

	CHAOS_API void DestroyParticle(FGeometryParticleHandle* Particle)
	{
		RemoveParticleFromAccelerationStructure(*Particle);
		UniqueIndicesPendingRelease.Add(Particle->UniqueIdx());
		DisableConstraints(TSet<FGeometryParticleHandle*>({ Particle }));
		ConstraintGraph.RemoveParticle(Particle);
		Particles.DestroyParticle(Particle);
	}

	/**
	 * Preallocate buffers for creating \p Num particles.
	 */
	CHAOS_API void ReserveParticles(const int32 Num)
	{
		if (const int32 NumNew = ConstraintGraph.ReserveParticles(Num))
		{
			InternalAccelerationQueue.PendingData.Reserve(InternalAccelerationQueue.Num() + NumNew);
			AsyncAccelerationQueue.PendingData.Reserve(AsyncAccelerationQueue.Num() + NumNew);
		}
	}

	CHAOS_API void CreateParticle(FGeometryParticleHandle* ParticleAdded)
	{
		ConstraintGraph.AddParticle(ParticleAdded);
		DirtyParticle(*ParticleAdded);
	}

	CHAOS_API void SetParticleObjectState(FPBDRigidParticleHandle* Particle, EObjectStateType ObjectState)
	{
		EObjectStateType InitialState = Particle->ObjectState();

		Particle->SetObjectStateLowLevel(ObjectState);
		Particles.SetDynamicParticleSOA(Particle);

		if(InitialState != ObjectState)
		{
			if (InitialState == EObjectStateType::Sleeping)
			{
				if (Particle->Island() != INDEX_NONE)
				{
					// GT has forced a wake so have to wake everything in the island
					IslandsToWake.Enqueue(Particle->Island());
				}
			}
			else if(ObjectState != EObjectStateType::Dynamic)
			{
				// even though we went to sleep, we should still report info back to GT
				Particles.MarkTransientDirtyParticle(Particle);
			}
		}
	}

	CHAOS_API void DisableParticles(const TSet<FGeometryParticleHandle*>& InParticles)
	{
		for (FGeometryParticleHandle* Particle : InParticles)
		{
			Particles.DisableParticle(Particle);
			RemoveParticleFromAccelerationStructure(*Particle);
		}

		ConstraintGraph.DisableParticles(InParticles);
		DisableConstraints(InParticles);
	}

	CHAOS_API void WakeIslands()
	{
		TArray<int32> UniqueIslands;

		// there could easily be duplicates, best to remove these since WakeIsland is potentially expensive call
		int32 IslandIdx = 0;
		while (!IslandsToWake.IsEmpty())
		{
			IslandsToWake.Dequeue(IslandIdx);
			UniqueIslands.AddUnique(IslandIdx);
		}

		for (int32 Island : UniqueIslands)
		{
			WakeIsland(Island);
		}
	}

	CHAOS_API void WakeIsland(const int32 Island)
	{
		ConstraintGraph.WakeIsland(Particles, Island);
		//Update Particles SOAs
		/*for (auto Particle : ContactGraph.GetIslandParticles(Island))
		{
			ActiveIndices.Add(Particle);
		}*/
	}

	CHAOS_API void DisableConstraints(const TSet<FGeometryParticleHandle*>& RemovedParticles)
	{
		for (FGeometryParticleHandle* ParticleHandle : RemovedParticles)
		{
			for (FConstraintHandle* BaseConstraintHandle : ParticleHandle->ParticleConstraints())
			{
				if (FPBDJointConstraintHandle* ConstraintHandle = BaseConstraintHandle->As<FPBDJointConstraintHandle>())
				{
					ConstraintGraph.RemoveConstraint(ConstraintHandle->GetConstraintIndex(), ConstraintHandle, ConstraintHandle->GetConstrainedParticles());
				}
			}
		}


		for (FPBDConstraintGraphRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->DisableConstraints(RemovedParticles);
		}
	}

	CHAOS_API void ResetConstraints()
	{
		for(FPBDConstraintGraphRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->ResetConstraints();
		}
	}

	//TEMP: this is only needed while clustering continues to use indices directly
	const auto& GetActiveClusteredArray() const { return Particles.GetActiveClusteredArray(); }
	const auto& GetNonDisabledClusteredArray() const { return Particles.GetNonDisabledClusteredArray(); }

	CHAOS_API TSerializablePtr<FChaosPhysicsMaterial> GetPhysicsMaterial(const FGeometryParticleHandle* Particle) const { return Particle->AuxilaryValue(PhysicsMaterials); }
	
	CHAOS_API const TUniquePtr<FChaosPhysicsMaterial> &GetPerParticlePhysicsMaterial(const FGeometryParticleHandle* Particle) const { return Particle->AuxilaryValue(PerParticlePhysicsMaterials); }

	CHAOS_API void SetPerParticlePhysicsMaterial(FGeometryParticleHandle* Particle, TUniquePtr<FChaosPhysicsMaterial> &InMaterial)
	{
		Particle->AuxilaryValue(PerParticlePhysicsMaterials) = MoveTemp(InMaterial);
	}

	CHAOS_API void SetPhysicsMaterial(FGeometryParticleHandle* Particle, TSerializablePtr<FChaosPhysicsMaterial> InMaterial)
	{
		check(!Particle->AuxilaryValue(PerParticlePhysicsMaterials)); //shouldn't be setting non unique material if a unique one already exists
		Particle->AuxilaryValue(PhysicsMaterials) = InMaterial;
	}

	CHAOS_API const TArray<FGeometryParticleHandle*>& GetIslandParticles(const int32 Island) const { return ConstraintGraph.GetIslandParticles(Island); }
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

	void PrepareTick()
	{
		for (FPBDConstraintGraphRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->PrepareTick();
		}
	}

	void UnprepareTick()
	{
		for (FPBDConstraintGraphRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->UnprepareTick();
		}
	}

	void PrepareIteration(const FReal Dt)
	{
		for (FPBDConstraintGraphRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->PrepareIteration(Dt);
		}
	}

	void UnprepareIteration(const FReal Dt)
	{
		for (FPBDConstraintGraphRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->UnprepareIteration(Dt);
		}
	}

	void UpdateAccelerationStructures(int32 Island)
	{
		for (FPBDConstraintGraphRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->UpdateAccelerationStructures(Island);
		}
	}

	void ApplyConstraints(const FReal Dt, int32 Island)
	{
		UpdateAccelerationStructures(Island);

		int32 LocalNumIterations = ChaosNumContactIterationsOverride >= 0 ? ChaosNumContactIterationsOverride : NumIterations;
		// @todo(ccaulfield): track whether we are sufficiently solved and can early-out
		for (int i = 0; i < LocalNumIterations; ++i)
		{
			bool bNeedsAnotherIteration = false;
			for (FPBDConstraintGraphRule* ConstraintRule : PrioritizedConstraintRules)
			{
				bNeedsAnotherIteration |= ConstraintRule->ApplyConstraints(Dt, Island, i, LocalNumIterations);
			}

			if (ChaosRigidsEvolutionApplyAllowEarlyOutCVar && !bNeedsAnotherIteration)
			{
				break;
			}
		}
	}

	void ApplyKinematicTargets(const FReal Dt, const FReal StepFraction)
	{
		check(StepFraction > (FReal)0);
		check(StepFraction <= (FReal)1);

		// @todo(ccaulfield): optimize. Depending on the number of kinematics relative to the number that have 
		// targets set, it may be faster to process a command list rather than iterate over them all each frame. 
		const FReal MinDt = 1e-6f;
		for (auto& Particle : Particles.GetActiveKinematicParticlesView())
		{
			TKinematicTarget<FReal, 3>& KinematicTarget = Particle.KinematicTarget();
			const TRigidTransform<FReal, 3>& Previous = KinematicTarget.GetPrevious();
			const FVec3 PrevX = Previous.GetTranslation();
			const FRotation3 PrevR = Previous.GetRotation();

			switch (KinematicTarget.GetMode())
			{
			case EKinematicTargetMode::None:
				// Nothing to do
				break;

			case EKinematicTargetMode::Reset:
			{
				// Reset velocity and then switch to do-nothing mode
				Particle.V() = FVec3(0, 0, 0);
				Particle.W() = FVec3(0, 0, 0);
				KinematicTarget.SetMode(EKinematicTargetMode::None);
				Particles.MarkTransientDirtyParticle(Particle.Handle());
				break;
			}

			case EKinematicTargetMode::Position:
			{
				// Move to kinematic target and update velocities to match
				// Target positions only need to be processed once, and we reset the velocity next frame (if no new target is set)
				FVec3 TargetPos;
				FRotation3 TargetRot;
				if (FMath::IsNearlyEqual(StepFraction, (FReal)1, KINDA_SMALL_NUMBER))
				{
					TargetPos = KinematicTarget.GetTarget().GetLocation();
					TargetRot = KinematicTarget.GetTarget().GetRotation();
					KinematicTarget.SetMode(EKinematicTargetMode::Reset);
				}
				else
				{
					TargetPos = FVec3::Lerp(PrevX, KinematicTarget.GetTarget().GetLocation(), StepFraction);
					TargetRot = FRotation3::Slerp(PrevR, KinematicTarget.GetTarget().GetRotation(), StepFraction);
				}
				if (Dt > MinDt)
				{
					FVec3 V = FVec3::CalculateVelocity(PrevX, TargetPos, Dt);
					Particle.V() = V;

					FVec3 W = FRotation3::CalculateAngularVelocity(PrevR, TargetRot, Dt);
					Particle.W() = W;
				}
				Particle.X() = TargetPos;
				Particle.R() = TargetRot;
				Particles.MarkTransientDirtyParticle(Particle.Handle());
				break;
			}

			case EKinematicTargetMode::Velocity:
			{
				// Move based on velocity
				Particle.X() = Particle.X() + Particle.V() * Dt;
				Particle.R() = FRotation3::IntegrateRotationWithAngularVelocity(Particle.R(), Particle.W(), Dt);
				Particles.MarkTransientDirtyParticle(Particle.Handle());
				break;
			}
			}
			
			// Set previous velocities if we can
			// Note: At present kininematics are in fact rigid bodies
			auto* Rigid = Particle.CastToRigidParticle();
			if (Rigid)
			{
				Rigid->PreV() = Rigid->V();
				Rigid->PreW() = Rigid->W();

				// Update the world bounds
				if (Rigid->HasBounds())
				{
					const FAABB3& LocalBounds = Rigid->LocalBounds();
					FAABB3 WorldSpaceBounds = LocalBounds.TransformedAABB(FRigidTransform3(Rigid->X(), Rigid->R()));
					if (Rigid->CCDEnabled())
					{
						WorldSpaceBounds.ThickenSymmetrically(Rigid->V() * Dt);
					}
					Rigid->SetWorldSpaceInflatedBounds(WorldSpaceBounds);
				}
			}
		}
	}

	/** Make a copy of the acceleration structure to allow for external modification.
	    This is needed for supporting sync operations on SQ structure from game thread. You probably want to go through solver which maintains PendingExternal */
	CHAOS_API void UpdateExternalAccelerationStructure_External(ISpatialAccelerationCollection<FAccelerationStructureHandle, FReal, 3>*& ExternalStructure, FPendingSpatialDataQueue& PendingExternal);

	ISpatialAccelerationCollection<FAccelerationStructureHandle, FReal, 3>* GetSpatialAcceleration() { return InternalAcceleration; }

	/** Perform a blocking flush of the spatial acceleration structure for situations where we aren't simulating but must have an up to date structure */
	CHAOS_API void FlushSpatialAcceleration();

	/** Rebuilds the spatial acceleration from scratch. This should only be used for perf testing */
	CHAOS_API void RebuildSpatialAccelerationForPerfTest();

	/* Ticks computation of acceleration structures. Normally handled by Advance, but if not advancing can be called to incrementally build structures.*/
	CHAOS_API void ComputeIntermediateSpatialAcceleration(bool bBlock = false);

	CHAOS_API const FPBDConstraintGraph& GetConstraintGraph() const { return ConstraintGraph; }
	CHAOS_API FPBDConstraintGraph& GetConstraintGraph() { return ConstraintGraph; }

	void Serialize(FChaosArchive& Ar);

	FUniqueIdx GenerateUniqueIdx()
	{
		//NOTE: this should be thread safe since evolution has already been initialized on GT
		return Particles.GetUniqueIndices().GenerateUniqueIdx();
	}

	bool AreAnyTasksPending() const
	{
		return (AccelerationStructureTaskComplete.GetReference() && !AccelerationStructureTaskComplete->IsComplete());
	}

	void SetCanStartAsyncTasks(bool bInCanStartAsyncTasks)
	{
		bCanStartAsyncTasks = bInCanStartAsyncTasks;
	}

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

public:
	template <bool bPersistent>
	FORCEINLINE_DEBUGGABLE void RemoveParticleFromAccelerationStructure(TGeometryParticleHandleImp<FReal, 3, bPersistent>& ParticleHandle)
	{
		//TODO: at the moment we don't distinguish between the first time a particle is created and when it's just moved
		// If we had this distinction we could simply remove the entry for the async queue
		const FUniqueIdx UniqueIdx = ParticleHandle.UniqueIdx();
		FPendingSpatialData& SpatialData = AsyncAccelerationQueue.FindOrAdd(UniqueIdx);

		SpatialData.bDelete = true;
		SpatialData.SpatialIdx = ParticleHandle.SpatialIdx();
		SpatialData.AccelerationHandle = FAccelerationStructureHandle(ParticleHandle);

		//Internal acceleration has all moves pending, so cancel them all now
		InternalAccelerationQueue.Remove(UniqueIdx);

		//remove particle immediately for intermediate structure
		//TODO: if we distinguished between first time adds we could avoid this. We could also make the RemoveElementFrom more strict and ensure when it fails
		InternalAcceleration->RemoveElementFrom(SpatialData.AccelerationHandle, SpatialData.SpatialIdx);
	}

protected:

	void UpdateConstraintPositionBasedState(FReal Dt)
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
	
	void UpdateVelocities(const FReal Dt, int32 Island)
	{
		ParticleUpdateVelocity(ConstraintGraph.GetIslandParticles(Island), Dt);
	}

	void ApplyPushOut(const FReal Dt, int32 Island)
	{
		int32 LocalNumPushOutIterations = ChaosNumPushOutIterationsOverride >= 0 ? ChaosNumPushOutIterationsOverride : NumPushOutIterations;
		bool bNeedsAnotherIteration = true;
		for (int32 It = 0; It < LocalNumPushOutIterations; ++It)
		{
			bNeedsAnotherIteration = false;
			for (FPBDConstraintGraphRule* ConstraintRule : PrioritizedConstraintRules)
			{
				bNeedsAnotherIteration |= ConstraintRule->ApplyPushOut(Dt, Island, It, LocalNumPushOutIterations);
			}

			if (ChaosRigidsEvolutionApplyPushoutAllowEarlyOutCVar && !bNeedsAnotherIteration)
			{
				break;
			}
		}
	}

	void FlushInternalAccelerationQueue();
	void FlushAsyncAccelerationQueue();
	void WaitOnAccelerationStructure();

	TArray<FForceRule> ForceRules;
	TArray<FForceRule> ImpulseRules;
	FUpdateVelocityRule ParticleUpdateVelocity;
	FUpdatePositionRule ParticleUpdatePosition;
	FKinematicUpdateRule KinematicUpdate;
	FCaptureRewindRule CaptureRewindData;
	TArray<FPBDConstraintGraphRule*> ConstraintRules;
	TArray<FPBDConstraintGraphRule*> PrioritizedConstraintRules;
	FPBDConstraintGraph ConstraintGraph;
	TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>> PhysicsMaterials;
	TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>> PerParticlePhysicsMaterials;
	TArrayCollectionArray<int32> ParticleDisableCount;
	TArrayCollectionArray<bool> Collided;

	FPBDRigidsSOAs& Particles;
	THandleArray<FChaosPhysicsMaterial>& SolverPhysicsMaterials;
	FAccelerationStructure* InternalAcceleration;
	FAccelerationStructure* AsyncInternalAcceleration;
	FAccelerationStructure* AsyncExternalAcceleration;

	//internal thread will push into this and external thread will consume
	TQueue<FAccelerationStructure*,EQueueMode::Spsc> ExternalStructuresQueue;

	//external thread will push into this when done with structure
	//internal thread will pop from this to generate new structure
	TQueue<FAccelerationStructure*,EQueueMode::Spsc> ExternalStructuresPool;

	//the backing buffer for all acceleration structures
	TArray<TUniquePtr<FAccelerationStructure>> AccelerationBackingBuffer;
	bool bIsSingleThreaded;

	// Allows us to tell evolution to stop starting async tasks if we are trying to cleanup solver/evo.
	bool bCanStartAsyncTasks;
	TQueue<int32, EQueueMode::Mpsc> IslandsToWake;

	TArray<FUniqueIdx> UniqueIndicesPendingRelease;
public:
	//The latest external timestamp we consumed inputs from, assigned to evolution when solver task executes, is used to stamp output data.
	int32 LatestExternalTimestampConsumed_Internal;	

protected:

	/** Pending operations for the internal acceleration structure */
	FPendingSpatialDataQueue InternalAccelerationQueue;

	/** Pending operations for the acceleration structures being rebuilt asynchronously */
	FPendingSpatialDataQueue AsyncAccelerationQueue;

	/*void SerializePendingMap(FChaosArchive& Ar, TMap<FGeometryParticleHandle*, FPendingSpatialData>& Map)
	{
		TArray<FGeometryParticleHandle*> Keys;
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
		//TODO: fix serialization
	}*/

	/** Used for async acceleration rebuild */
	TArrayAsMap<FUniqueIdx, uint32> ParticleToCacheInnerIdx;

	TMap<FSpatialAccelerationIdx, TUniquePtr<FSpatialAccelerationCache>> SpatialAccelerationCache;

	FORCEINLINE_DEBUGGABLE void ApplyParticlePendingData(const FPendingSpatialData& PendingData, FAccelerationStructure& SpatialAcceleration, bool bUpdateCache);

	class FChaosAccelerationStructureTask
	{
	public:
		FChaosAccelerationStructureTask(ISpatialAccelerationCollectionFactory& InSpatialCollectionFactory
			, const TMap<FSpatialAccelerationIdx, TUniquePtr<FSpatialAccelerationCache>>& InSpatialAccelerationCache
			, FAccelerationStructure* InInternalAccelerationStructure
			, FAccelerationStructure* InExternalAccelerationStructure
			, bool InForceFullBuild
			, bool InIsSingleThreaded
			, bool bNeedsReset);
		static FORCEINLINE TStatId GetStatId();
		static FORCEINLINE ENamedThreads::Type GetDesiredThread();
		static FORCEINLINE ESubsequentsMode::Type GetSubsequentsMode();
		void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent);

		ISpatialAccelerationCollectionFactory& SpatialCollectionFactory;
		const TMap<FSpatialAccelerationIdx, TUniquePtr<FSpatialAccelerationCache>>& SpatialAccelerationCache;
		FAccelerationStructure* InternalStructure;
		FAccelerationStructure* ExternalStructure;
		bool IsForceFullBuild;
		bool bIsSingleThreaded;
		bool bNeedsReset;

	private:
		void UpdateStructure(FAccelerationStructure* AccelerationStructure);
	};
	FGraphEventRef AccelerationStructureTaskComplete;

	int32 NumIterations;
	int32 NumPushOutIterations;
	TUniquePtr<ISpatialAccelerationCollectionFactory> SpatialCollectionFactory;

	FAccelerationStructure* GetFreeSpatialAcceleration_Internal();
	void FreeSpatialAcceleration_External(FAccelerationStructure* Structure);

	void ReleaseIdx(FUniqueIdx Idx);
	void ReleasePendingIndices();

	TArray<FUniqueIdx> PendingReleaseIndices;	//for now just assume a one frame delay, but may need something more general
};


}
