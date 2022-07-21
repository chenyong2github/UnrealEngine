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
#include "ProfilingDebugging/CsvProfiler.h"
#include "RewindData.h"

extern int32 ChaosRigidsEvolutionApplyAllowEarlyOutCVar;
extern int32 ChaosRigidsEvolutionApplyPushoutAllowEarlyOutCVar;
extern int32 ChaosNumPushOutIterationsOverride;
extern int32 ChaosNumContactIterationsOverride;
extern int32 ChaosNonMovingKinematicUpdateOptimization;

namespace Chaos
{
extern CHAOS_API int32 ForceNoCollisionIntoSQ;

namespace Collisions
{
	void CHAOS_API ResetChaosCollisionCounters();
}

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

	bool LightWeightDisabled() const { return false; }

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
	virtual TUniquePtr<ISpatialAcceleration<FAccelerationStructureHandle, FReal, 3>> CreateAccelerationPerBucket_Threaded(const TConstParticleView<FSpatialAccelerationCache>& Particles, uint16 BucketIdx, bool ForceFullBuild, bool bDynamicTree) = 0;

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

	CHAOS_API FPBDRigidsEvolutionBase(FPBDRigidsSOAs& InParticles, THandleArray<FChaosPhysicsMaterial>& InSolverPhysicsMaterials, bool InIsSingleThreaded = false);
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

	CHAOS_API void SetNumPositionIterations(int32 InNumIterations)
	{
		NumPositionIterations = InNumIterations;
	}

	CHAOS_API int32 GetNumPositionIterations() const
	{
		return NumPositionIterations;
	}

	CHAOS_API void SetNumVelocityIterations(int32 InNumIterations)
	{
		NumVelocityIterations = InNumIterations;
	}

	CHAOS_API int32 GetNumVelocityIterations() const
	{
		return NumVelocityIterations;
	}

	CHAOS_API void SetNumProjectionIterations(int32 InNumIterations)
	{
		NumProjectionIterations = InNumIterations;
	}

	CHAOS_API int32 GetNumProjectionIterations() const
	{
		return NumProjectionIterations;
	}

	CHAOS_API void SetParticleKinematicTarget(FKinematicGeometryParticleHandle* KinematicHandle, const FKinematicTarget& NewKinematicTarget)
	{
		if (KinematicHandle)
		{
			if (ChaosNonMovingKinematicUpdateOptimization)
			{
				// optimization : we keep track of moving kinematic targets ( list gets clear every frame )
				if (NewKinematicTarget.GetMode() != EKinematicTargetMode::None)
				{
					// move particle from "non-moving" kinematics to "moving" kinematics
					Particles.MarkMovingKinematic(KinematicHandle);
				}
			}
			KinematicHandle->SetKinematicTarget(NewKinematicTarget);
		}
	}
	



	CHAOS_API void EnableParticle(FGeometryParticleHandle* Particle, const FGeometryParticleHandle* ParentParticle)
	{
		Particles.EnableParticle(Particle);
		EnableConstraints(Particle);
		ConstraintGraph.EnableParticle(Particle, ParentParticle);
		DirtyParticle(*Particle);
	}

	CHAOS_API void DisableParticle(FGeometryParticleHandle* Particle)
	{
		RemoveParticleFromAccelerationStructure(*Particle);
		Particles.DisableParticle(Particle);
		// disable constraint before disabling the particle in the graph because of dependencies 
		DisableConstraints(Particle);
		ConstraintGraph.DisableParticle(Particle);
	}

	CHAOS_API void InvalidateParticle(FGeometryParticleHandle* Particle)
	{
		// Remove the particle from the constraint graph. This should remove all the constraints too
		ConstraintGraph.DisableParticle(Particle);

		// Destroy all the transient constraints (collisions) because the particle has changed somehow (e.g. new shapes) and they may have cached the previous state
		DestroyTransientConstraints(Particle);
	}
	
	CHAOS_API void FlushExternalAccelerationQueue(FAccelerationStructure& Acceleration,FPendingSpatialDataQueue& ExternalQueue);

	CHAOS_API void DisableParticles(TSet<FGeometryParticleHandle*> &ParticlesIn)
	{
		for (FGeometryParticleHandle* Particle : ParticlesIn)
		{
			DisableParticle(Particle);
		}
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
		if (Particle.HasCollision() || ForceNoCollisionIntoSQ)
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
		if (MRewindData)
		{
			MRewindData->RemoveObject(Particle);
		}

		RemoveParticleFromAccelerationStructure(*Particle);
		DisconnectConstraints(TSet<FGeometryParticleHandle*>({ Particle }));
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

	CHAOS_API void SetParticleObjectState(FPBDRigidParticleHandle* Particle, EObjectStateType ObjectState);

	CHAOS_API void SetParticleSleepType(FPBDRigidParticleHandle* Particle, ESleepType InSleepType);

	CHAOS_API void DisableParticles(const TSet<FGeometryParticleHandle*>& InParticles);

	/** remove a constraint from the constraint graph (see AddConstraintsToConstraintGraph) */
	CHAOS_API void RemoveConstraintFromConstraintGraph(FConstraintHandle* ConstraintHandle)
	{
		if (ConstraintHandle->IsInConstraintGraph())
		{
			ConstraintGraph.RemoveConstraint(ConstraintHandle->GetContainerId(), ConstraintHandle);
		}
	}

	/** remove a list of constraints from the constraint graph (see AddConstraintsToConstraintGraph) */
	CHAOS_API void RemoveConstraintsFromConstraintGraph(const FConstraintHandleArray& Constraints)
	{
		for (FConstraintHandle* BaseConstraintHandle : Constraints)
		{
			if (FPBDJointConstraintHandle* ConstraintHandle = BaseConstraintHandle->As<FPBDJointConstraintHandle>())
			{
				RemoveConstraintFromConstraintGraph(ConstraintHandle);
			}
		}
	}

	/** Add a list of constraints to the constraint graph (see RemoveConstraintsFromConstraintGraph) */
	CHAOS_API void AddConstraintsToConstraintGraph(const FConstraintHandleArray& Constraints)
	{
		for (FConstraintHandle* BaseConstraintHandle : Constraints)
		{
			if (FPBDJointConstraintHandle* ConstraintHandle = BaseConstraintHandle->As<FPBDJointConstraintHandle>())
			{
				if (!ensure(ConstraintHandle->IsInConstraintGraph()))
				{
					ConstraintGraph.AddConstraint(ConstraintHandle->GetContainerId(), ConstraintHandle, ConstraintHandle->GetConstrainedParticles());
				}
			}
		}
	}



	/** Disconnect constraints from a set of particles to be destroyed. 
	* this will set the constraints to Enbaled = false and set their respective bodies handles to nullptr.
	* Once this is done, the constraints cannot be re-enabled.
	* @note This only applies to persistent constraints (joints etc), not transient constraints (collisons)
	*/
	CHAOS_API void DisconnectConstraints(const TSet<FGeometryParticleHandle*>& RemovedParticles)
	{
		for (FGeometryParticleHandle* ParticleHandle : RemovedParticles)
		{
			RemoveConstraintsFromConstraintGraph(ParticleHandle->ParticleConstraints());
		}

		for (FPBDConstraintGraphRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->DisconnectConstraints(RemovedParticles);
		}
	}

	/** Disconnect constraints from a particle to be removed (or destroyed)
	* this will set the constraints to Enabled = false, but leave connections to the particles to support
	* re-enabling at a later time.
	* @note This only applies to persistent constraints (joints etc), not transient constraints (collisons)
	*/
	CHAOS_API void DisableConstraints(FGeometryParticleHandle* ParticleHandle)
	{
		RemoveConstraintsFromConstraintGraph(ParticleHandle->ParticleConstraints());

		for (FPBDConstraintGraphRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->SetConstraintsEnabled(ParticleHandle, false);
		}
	}

	CHAOS_API void DisableConstraints(const TSet<FGeometryParticleHandle*>& DisabledParticles)
	{
		for (FGeometryParticleHandle* ParticleHandle : DisabledParticles)
		{
			DisableConstraints(ParticleHandle);
		}
	}

	/** Enable constraints from the enabled particles; constraints will only become enabled if their particle end points are valid */
	CHAOS_API void EnableConstraints(FGeometryParticleHandle* ParticleHandle)
	{
		for (FPBDConstraintGraphRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->SetConstraintsEnabled(ParticleHandle, true);
		}
	}

	CHAOS_API void EnableConstraints(const TSet<FGeometryParticleHandle*>& EnabledParticles)
	{
		for (FGeometryParticleHandle* ParticleHandle : EnabledParticles)
		{
			EnableConstraints(ParticleHandle);
		}
	}

	CHAOS_API void ResetConstraints()
	{
		// Remove all the constraints from the graph
		GetConstraintGraph().RemoveConstraints();
		
		// Clear all particle lists of collisions and constraints
		// (this could be performed by the constraint containers
		// but it would be unnecessarily expensive to remove them
		// one by one)
		for (auto& Particle : Particles.GetAllParticlesView())
		{
			Particle.ParticleConstraints().Reset();
			Particle.ParticleCollisions().Reset();
		}

		// Remove all constraints from the containers
		for(FPBDConstraintGraphRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->ResetConstraints();
		}
	}

	virtual void DestroyTransientConstraints(FGeometryParticleHandle* Particle) {}

	const TParticleView<FPBDRigidClusteredParticles>& GetNonDisabledClusteredView() const { return Particles.GetNonDisabledClusteredView(); }

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

	CHAOS_API int32 NumIslands() const { return ConstraintGraph.NumIslands(); }

	void InitializeAccelerationStructures()
	{
		ConstraintGraph.InitializeGraph(Particles.GetNonDisabledDynamicView());

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
		Collisions::ResetChaosCollisionCounters();

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

	void UpdateAccelerationStructures(const FReal Dt, const int32 Island)
	{
		CSV_SCOPED_TIMING_STAT(Chaos, UpdateAccelerationStructures);

		for (FPBDConstraintGraphRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->UpdateAccelerationStructures(Dt, Island);
		}
	}

	void ApplyKinematicTargets(const FReal Dt, const FReal StepFraction)
	{
		check(StepFraction > (FReal)0);
		check(StepFraction <= (FReal)1);

		const bool IsLastStep = (FMath::IsNearlyEqual(StepFraction, (FReal)1, (FReal)UE_KINDA_SMALL_NUMBER));

		const FReal MinDt = 1e-6f;
		auto GetKinematicView = [this]()
		{
			if (ChaosNonMovingKinematicUpdateOptimization)
			{
				return Particles.GetActiveMovingKinematicParticlesView();
			}
			return Particles.GetActiveKinematicParticlesView();
		};

		for (auto& Particle : GetKinematicView())
		{
			TKinematicTarget<FReal, 3>& KinematicTarget = Particle.KinematicTarget();
			const FVec3 CurrentX = Particle.X();
			const FRotation3 CurrentR = Particle.R();

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
				FVec3 NewX;
				FRotation3 NewR;
				if (IsLastStep)
				{
					NewX = KinematicTarget.GetTarget().GetLocation();
					NewR = KinematicTarget.GetTarget().GetRotation();
					KinematicTarget.SetMode(EKinematicTargetMode::Reset);
				}
				else
				{
					// as a reminder, stepfraction is the remaing fraction of the step from the remaining steps
					// for total of 4 steps and current step of 2, this will be 1/3 ( 1 step passed, 3 steps remains )
					NewX = FVec3::Lerp(CurrentX, KinematicTarget.GetTarget().GetLocation(), StepFraction);
					NewR = FRotation3::Slerp(CurrentR, KinematicTarget.GetTarget().GetRotation(), decltype(FQuat::X)(StepFraction));
				}
				if (Dt > MinDt)
				{
					FVec3 V = FVec3::CalculateVelocity(CurrentX, NewX, Dt);
					Particle.V() = V;

					FVec3 W = FRotation3::CalculateAngularVelocity(CurrentR, NewR, Dt);
					Particle.W() = W;
				}
				Particle.X() = NewX;
				Particle.R() = NewR;
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
			
			// Set positions and previous velocities if we can
			// Note: At present kininematics are in fact rigid bodies
			auto* Rigid = Particle.CastToRigidParticle();
			if (Rigid)
			{
				Rigid->P() = Rigid->X();
				Rigid->Q() = Rigid->R();
				Rigid->PreV() = Rigid->V();
				Rigid->PreW() = Rigid->W();
				if (!Rigid->CCDEnabled())
				{
					Rigid->UpdateWorldSpaceState(FRigidTransform3(Rigid->P(), Rigid->Q()), FVec3(0));
				}
				else
				{
					Rigid->UpdateWorldSpaceStateSwept(FRigidTransform3(Rigid->P(), Rigid->Q()), FVec3(0), -Rigid->V() * Dt);
				}
			}
		}

		// done with update, let's clear the tracking structures
		if (IsLastStep && ChaosNonMovingKinematicUpdateOptimization)
		{
			Particles.UpdateAllMovingKinematic();
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

	void SetResim(bool bInResim) { bIsResim = bInResim; }
	const bool IsResimming() const { return bIsResim; }

	void Serialize(FChaosArchive& Ar);

	FUniqueIdx GenerateUniqueIdx()
	{
		//NOTE: this should be thread safe since evolution has already been initialized on GT
		return Particles.GetUniqueIndices().GenerateUniqueIdx();
	}

	void ReleaseUniqueIdx(FUniqueIdx UniqueIdx)
	{
		UniqueIndicesPendingRelease.Add(UniqueIdx);
	}

	bool IsUniqueIndexPendingRelease(FUniqueIdx UniqueIdx) const
	{
		return UniqueIndicesPendingRelease.Contains(UniqueIdx) || PendingReleaseIndices.Contains(UniqueIdx);
	}

	bool AreAnyTasksPending() const
	{
		return (AccelerationStructureTaskComplete.GetReference() && !AccelerationStructureTaskComplete->IsComplete());
	}

	void SetCanStartAsyncTasks(bool bInCanStartAsyncTasks)
	{
		bCanStartAsyncTasks = bInCanStartAsyncTasks;
	}

	void SetRewindData(FRewindData* RewindData)
	{
		MRewindData = RewindData;
	}

	CHAOS_API void DisableParticleWithRemovalEvent(FGeometryParticleHandle* Particle);
	const TArray<FRemovalData>& GetAllRemovals() { return MAllRemovals; }
	void ResetAllRemovals() { MAllRemovals.Reset(); }

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
		ConstraintGraph.InitializeGraph(Particles.GetNonDisabledDynamicView());

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
		ConstraintGraph.UpdateIslands(Particles.GetNonDisabledDynamicView(), Particles, ConstraintRules.Num());

		for (FPBDConstraintGraphRule* ConstraintRule : ConstraintRules)
		{
			ConstraintRule->InitializeAccelerationStructures();
		}
	}
	
	/** Sort constraints if necessary */
	void SortConstraints()
	{
		bool bNeedsSorting = false;
		for (FPBDConstraintGraphRule* ConstraintRule : ConstraintRules)
		{
			bNeedsSorting |= ConstraintRule->IsSortingEnabled();
		}
		if(bNeedsSorting)
		{
			ConstraintGraph.GetIslandGraph()->InitSorting();
			for (FPBDConstraintGraphRule* ConstraintRule : ConstraintRules)
			{
				ConstraintRule->SortConstraints();
			}
		}
	}
	
	void FlushInternalAccelerationQueue();
	void FlushAsyncAccelerationQueue();
	void WaitOnAccelerationStructure();
	static void CopyUnBuiltDynamicAccelerationStructures(const TMap<FSpatialAccelerationIdx, TUniquePtr<FSpatialAccelerationCache>>& SpatialAccelerationCache, FAccelerationStructure* InternalAcceleration, FAccelerationStructure* AsyncInternalAcceleration, FAccelerationStructure* AsyncExternalAcceleration);
	static void CopyPristineAccelerationStructures(const TMap<FSpatialAccelerationIdx, TUniquePtr<FSpatialAccelerationCache>>& SpatialAccelerationCache, FAccelerationStructure* FromStructure, FAccelerationStructure* ToStructure, bool CheckPristine);

	TArray<FForceRule> ForceRules;
	TArray<FForceRule> ImpulseRules;
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
	FRewindData* MRewindData = nullptr;

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

	TArray<FUniqueIdx> UniqueIndicesPendingRelease;

	TArray<FRemovalData> MAllRemovals;
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

	FORCEINLINE_DEBUGGABLE void ApplyParticlePendingData(const FPendingSpatialData& PendingData, FAccelerationStructure& SpatialAcceleration, bool bUpdateCache, bool bUpdateDynamicTrees);

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
		void UpdateStructure(FAccelerationStructure* AccelerationStructure, FAccelerationStructure* CopyToAccelerationStructure = nullptr);
	};
	FGraphEventRef AccelerationStructureTaskComplete;

	int32 NumPositionIterations;
	int32 NumVelocityIterations;
	int32 NumProjectionIterations;
	TUniquePtr<ISpatialAccelerationCollectionFactory> SpatialCollectionFactory;

	FAccelerationStructure* GetFreeSpatialAcceleration_Internal();
	void FreeSpatialAcceleration_External(FAccelerationStructure* Structure);

	void ReleaseIdx(FUniqueIdx Idx);
	void ReleasePendingIndices();

	TArray<FUniqueIdx> PendingReleaseIndices;	//for now just assume a one frame delay, but may need something more general
	bool bIsResim = false; 
};


}
