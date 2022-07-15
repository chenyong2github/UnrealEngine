// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/CollisionResolutionTypes.h"
#include "Chaos/Collision/CollisionApplyType.h"
#include "Chaos/Collision/CollisionConstraintAllocator.h"
#include "Chaos/Collision/CollisionContext.h"
#include "Chaos/Collision/PBDCollisionConstraint.h"
#include "Chaos/Collision/PBDCollisionConstraintHandle.h"
#include "Chaos/Collision/SolverCollisionContainer.h"
#include "Chaos/PBDConstraintContainer.h"
#include "Framework/BufferedData.h"

#include <memory>
#include <queue>
#include <sstream>
#include "BoundingVolume.h"
#include "AABBTree.h"

namespace Chaos
{
class FImplicitObject;
class FPBDCollisionConstraints;
class FPBDRigidsSOAs;
class FPBDCollisionConstraint;
class FPBDIslandSolverData;

using FRigidBodyContactConstraintsPostComputeCallback = TFunction<void()>;
using FRigidBodyContactConstraintsPostApplyCallback = TFunction<void(const FReal Dt, const TArray<FPBDCollisionConstraintHandle*>&)>;
using FRigidBodyContactConstraintsPostApplyPushOutCallback = TFunction<void(const FReal Dt, const TArray<FPBDCollisionConstraintHandle*>&, bool)>;

namespace Collisions
{
	struct FContactParticleParameters;
	struct FContactIterationParameters;
}

/**
 * A container and solver for collision constraints.
 * 
 * @todo(chaos): remove handles array
 */
class CHAOS_API FPBDCollisionConstraints : public FPBDConstraintContainer
{
public:
	friend class FPBDCollisionConstraintHandle;

	using Base = FPBDIndexedConstraintContainer;

	// Collision constraints have intrusive pointers. An array of constraint pointers can be uased as an array of handle pointers
	using FHandles = TArrayView<FPBDCollisionConstraint* const>;
	using FConstHandles = TArrayView<const FPBDCollisionConstraint* const>;

	// For use by dependent types
	using FConstraintContainerHandle = FPBDCollisionConstraintHandle;		// Used by constraint rules
	using FConstraintSolverContainerType = FPBDCollisionSolverContainer;	// Used by constraint rules
	using FParticleHandle = TGeometryParticleHandle<FReal, 3>;

	FPBDCollisionConstraints(const FPBDRigidsSOAs& InParticles, 
		TArrayCollectionArray<bool>& Collided, 
		const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& PhysicsMaterials, 
		const TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>>& PerParticlePhysicsMaterials,
		const THandleArray<FChaosPhysicsMaterial>* const SimMaterials,
		const int32 ApplyPairIterations = 1, 
		const int32 ApplyPushOutPairIterations = 1, 
		const FReal RestitutionThreshold = 2000.0f);

	virtual ~FPBDCollisionConstraints();

	/**
	 * Whether this container provides constraint handles (simple solvers do not need them)
	 */
	bool GetHandlesEnabled() const { return bHandlesEnabled; }

	/**
	 * Put the container in "no handles" mode for use with simple solver. Must be called when empty of constraints (ideally right after creation).
	 */
	void DisableHandles();

	/**
	 * Set the solver method to use
	 */
	void SetSolverType(EConstraintSolverType InSolverType)
	{
		SolverType = InSolverType;
	}

	/**
	 * @brief Enable or disable determinism.
	 * Support for determinism requires that we sort active constraints each tick, so there is additional cost.
	*/
	void SetIsDeterministic(const bool bInIsDeterministic)
	{
		bIsDeterministic = bInIsDeterministic;
	}

	/**
	 *  Clears the list of active constraints.
	 * @todo(chaos): This is only required because of the way events work (see AdvanceOneTimeStepTask::DoWork)
	*/
	void BeginFrame();

	/**
	*  Destroy all constraints 
	*/
	void Reset();


	/**
	 * @brief Called before collision detection to reset contacts
	*/
	void BeginDetectCollisions();

	/**
	 * @brief Called after collision detection to finalize the contacts
	*/
	void EndDetectCollisions();

	/**
	 * @brief Called after collision resolution in order to detect probes
	 */
	void DetectProbeCollisions(FReal Dt);

	/**
	 * Apply modifiers to the constraints and specify which constraints should be disabled.
	 * You would probably call this in the PostComputeCallback. Prefer this to calling RemoveConstraints in a loop,
	 * so you don't have to worry about constraint iterator/indices changing.
	 */
	void ApplyCollisionModifier(const TArray<ISimCallbackObject*>& CollisionModifiers, FReal Dt);


	/**
	* Remove the constraints associated with the ParticleHandle.
	*/
	void RemoveConstraints(const TSet<FGeometryParticleHandle*>&  ParticleHandle);

	/**
	 * @brief Remove all constraints associated with the particles - called when particles are destroyed
	*/
	virtual void DisconnectConstraints(const TSet<FGeometryParticleHandle*>& ParticleHandles) override;

	/**
	* Disable the constraints associated with the ParticleHandle.
	*/
	void DisableConstraints(const TSet<FGeometryParticleHandle*>& ParticleHandle) {}

	//
	// General Rule API
	//

	void PrepareTick() {}

	void UnprepareTick() {}

	/**
	 * Generate all contact constraints.
	 */
	void UpdatePositionBasedState(const FReal Dt);

	//
	// Simple Rule API
	//

	void PreGatherInput(const FReal Dt, FPBDIslandSolverData& SolverData);
	void GatherInput(const FReal Dt, FPBDIslandSolverData& SolverData);
	void ScatterOutput(const FReal Dt, FPBDIslandSolverData& SolverData);

	bool ApplyPhase1(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData);
	bool ApplyPhase2(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData);
	bool ApplyPhase3(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData) { return false; }

	//
	// Island Rule API
	//

	void SetNumIslandConstraints(const int32 NumIslandConstraints, FPBDIslandSolverData& SolverData);
	void PreGatherInput(const FReal Dt, FPBDCollisionConstraint& Constraint, FPBDIslandSolverData& SolverData);
	void GatherInput(const FReal Dt, FPBDCollisionConstraint& Constraint, const int32 Particle0Level, const int32 Particle1Level, FPBDIslandSolverData& SolverData);

	bool ApplyPhase1Serial(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData);
	bool ApplyPhase2Serial(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData);
	bool ApplyPhase3Serial(const FReal Dt, const int32 It, const int32 NumIts, FPBDIslandSolverData& SolverData) { return false; }

	//
	// Color Rule API
	//

	void ScatterOutput(const FReal Dt, const int32 BeginIndex, const int32 EndIndex, FPBDIslandSolverData& SolverData);

	bool ApplyPhase1Serial(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, FPBDIslandSolverData& SolverData);
	bool ApplyPhase1Parallel(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, FPBDIslandSolverData& SolverData);
	
	bool ApplyPhase2Serial(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, FPBDIslandSolverData& SolverData);
	bool ApplyPhase2Parallel(const FReal Dt, const int32 It, const int32 NumIts, const int32 BeginIndex, const int32 EndIndex, FPBDIslandSolverData& SolverData);

	//
	// Member Access
	//

	void SetCanDisableContacts(bool bInCanDisableContacts)
	{
		bCanDisableContacts = bInCanDisableContacts;
	}

	bool GetCanDisableContacts() const
	{
		return bCanDisableContacts;
	}

	void SetRestitutionThreshold(FReal InRestitutionThreshold)
	{
		RestitutionThreshold = InRestitutionThreshold;
	}

	FReal GetRestitutionThreshold() const
	{
		return RestitutionThreshold;
	}

	void SetPairIterations(int32 InPairIterations)
	{
		MApplyPairIterations = InPairIterations;
	}

	int32 GetPairIterations() const
	{
		return MApplyPairIterations;
	}

	void SetPushOutPairIterations(int32 InPairIterations)
	{
		MApplyPushOutPairIterations = InPairIterations;
	}

	int32 GetPushOutPairIterations() const
	{
		return MApplyPushOutPairIterations;
	}

	void SetCollisionsEnabled(bool bInEnableCollisions)
	{
		bEnableCollisions = bInEnableCollisions;
	}

	bool GetCollisionsEnabled() const
	{
		return bEnableCollisions;
	}

	void SetRestitutionEnabled(bool bInEnableRestitution)
	{
		bEnableRestitution = bInEnableRestitution;
	}

	bool GetRestitutionEnabled() const
	{
		return bEnableRestitution;
	}

	void SetGravity(const FVec3& InGravity)
	{
		GravityDirection = InGravity;
		GravitySize = GravityDirection.SafeNormalize();
	}

	FVec3 GetGravityDirection() const
	{
		return GravityDirection;
	}

	FReal GetGravitySize() const
	{
		return GravitySize;
	}

	void SetMaxPushOutVelocity(const FReal InMaxPushOutVelocity)
	{
		SolverSettings.MaxPushOutVelocity = InMaxPushOutVelocity;
	}

	void SetPositionFrictionIterations(const int32 InNumIterations)
	{
		SolverSettings.NumPositionFrictionIterations = InNumIterations;
	}

	void SetVelocityFrictionIterations(const int32 InNumIterations)
	{
		SolverSettings.NumVelocityFrictionIterations = InNumIterations;
	}

	void SetPositionShockPropagationIterations(const int32 InNumIterations)
	{
		SolverSettings.NumPositionShockPropagationIterations = InNumIterations;
	}

	void SetVelocityShockPropagationIterations(const int32 InNumIterations)
	{
		SolverSettings.NumVelocityShockPropagationIterations = InNumIterations;
	}

	int32 NumConstraints() const
	{
		return GetConstraints().Num();
	}

	TArrayView<FPBDCollisionConstraint* const> GetConstraints() const
	{
		return ConstraintAllocator.GetConstraints();
	}

	FHandles GetConstraintHandles() const;
	FConstHandles GetConstConstraintHandles() const;

	const FPBDCollisionConstraint& GetConstraint(int32 Index) const;

	FCollisionConstraintAllocator& GetConstraintAllocator() { return ConstraintAllocator; }

	void UpdateConstraintMaterialProperties(FPBDCollisionConstraint& Contact);

	// @todo(chaos): these settings are duplicated from the NarrowPhase context - fix this
	const FCollisionDetectorSettings& GetDetectorSettings() const { return DetectorSettings; }
	void SetDetectorSettings(const FCollisionDetectorSettings& InSettings) { DetectorSettings = InSettings; }

protected:
	FPBDCollisionConstraint& GetConstraint(int32 Index);
	FPBDCollisionSolverContainer& GetConstraintSolverContainer(FPBDIslandSolverData& SolverData);

	Collisions::FContactParticleParameters GetContactParticleParameters(const FReal Dt);
	Collisions::FContactIterationParameters GetContactIterationParameters(const FReal Dt, const int32 Iteration, const int32 NumIterations, const int32 NumPairIterations, bool& bNeedsAnotherIteration);

	// Call PruneParticleEdgeCollisions on all particles with ECollisionConstraintFlags::CCF_SmoothEdgeCollisions set in CollisionFlags
	void PruneEdgeCollisions();

	// The "Legacy" functions handle the older solver types (StandardPbd)
	// @todo(chaos): remove legacy methods when the new solver is fully operational and used everywhere (RBAN may still optionally use StandardPbd)
	void LegacyGatherInput(const FReal Dt, FPBDCollisionConstraint& Constraint, const int32 Particle0Level, const int32 Particle1Level, FPBDIslandSolverData& SolverData);
	void LegacyScatterOutput(const FReal Dt, const int32 BeginIndex, const int32 EndIndex, FPBDIslandSolverData& SolverData);
	bool LegacyApplyPhase1Serial(const FReal Dt, const int32 Iterations, const int32 NumIterations, const int32 BeginIndex, const int32 EndIndex, FPBDIslandSolverData& SolverData);
	bool LegacyApplyPhase1Parallel(const FReal Dt, const int32 Iterations, const int32 NumIterations, const int32 BeginIndex, const int32 EndIndex, FPBDIslandSolverData& SolverData);

private:

	const FPBDRigidsSOAs& Particles;

	FCollisionConstraintAllocator ConstraintAllocator;
	int32 NumActivePointConstraints;

	TArrayCollectionArray<bool>& MCollided;
	const TArrayCollectionArray<TSerializablePtr<FChaosPhysicsMaterial>>& MPhysicsMaterials;
	const TArrayCollectionArray<TUniquePtr<FChaosPhysicsMaterial>>& MPerParticlePhysicsMaterials;
	const THandleArray<FChaosPhysicsMaterial>* const SimMaterials;

	// @todo(chaos): remove pair iteration counts when legacy PBD solver is removed (RBAN-only)
	int32 MApplyPairIterations;
	int32 MApplyPushOutPairIterations;

	FReal RestitutionThreshold;
	bool bEnableCollisions;
	bool bEnableRestitution;
	bool bHandlesEnabled;
	bool bEnableEdgePruning;
	bool bIsDeterministic;

	// This is passed to IterationParameters. If true, then an iteration can cull a contact
	// permanently (ie, for the remaining iterations) if it is ignored due to culldistance.
	// This improves performance, but can decrease stability if contacts are culled prematurely.
	bool bCanDisableContacts;

	// Used to determine constraint directions
	FVec3 GravityDirection;
	FReal GravitySize;

	// Settings for the collision detector
	FCollisionDetectorSettings DetectorSettings;

	// Settings for the low-level collision solvers
	FPBDCollisionSolverSettings SolverSettings;

	EConstraintSolverType SolverType;
};

//
//
// Inlined FPBDCollisionConstraintHandle functions. Here to avoid circular deps
//
//

inline const FPBDCollisionConstraints* FPBDCollisionConstraintHandle::ConcreteContainer() const
{
	return static_cast<FPBDCollisionConstraints*>(ConstraintContainer);
}

inline FPBDCollisionConstraints* FPBDCollisionConstraintHandle::ConcreteContainer()
{
	return static_cast<FPBDCollisionConstraints*>(ConstraintContainer);
}

inline const FPBDCollisionConstraint& FPBDCollisionConstraintHandle::GetContact() const
{
	return *GetConstraint();
}

inline FPBDCollisionConstraint& FPBDCollisionConstraintHandle::GetContact()
{
	return *GetConstraint();
}

inline ECollisionCCDType FPBDCollisionConstraintHandle::GetCCDType() const
{
	return GetContact().GetCCDType();
}

inline void FPBDCollisionConstraintHandle::SetEnabled(bool InEnabled)
{
	GetContact().SetDisabled(!InEnabled);
}

inline bool FPBDCollisionConstraintHandle::IsEnabled() const
{
	return !GetContact().GetDisabled();
}

inline bool FPBDCollisionConstraintHandle::IsProbe() const
{
	return GetContact().GetIsProbe();
}

inline FVec3 FPBDCollisionConstraintHandle::GetAccumulatedImpulse() const
{
	return GetContact().AccumulatedImpulse;
}

inline FParticlePair FPBDCollisionConstraintHandle::GetConstrainedParticles() const
{
	return { GetContact().GetParticle0(), GetContact().GetParticle1() };
}

inline void FPBDCollisionConstraintHandle::PreGatherInput(const FReal Dt, FPBDIslandSolverData& SolverData)
{
	ConcreteContainer()->PreGatherInput(Dt, GetContact(), SolverData);
}

inline void FPBDCollisionConstraintHandle::GatherInput(FReal Dt, const int32 Particle0Level, const int32 Particle1Level, FPBDIslandSolverData& SolverData)
{
	ConcreteContainer()->GatherInput(Dt, GetContact(), Particle0Level, Particle1Level, SolverData);
}

inline FSolverBody* FPBDCollisionConstraintHandle::GetSolverBody0()
{
	return GetContact().GetSolverBody0();
}

inline FSolverBody* FPBDCollisionConstraintHandle::GetSolverBody1()
{
	return GetContact().GetSolverBody1();
}

}
