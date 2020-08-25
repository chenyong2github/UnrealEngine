// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Field/FieldSystem.h"
#include "Chaos/PBDPositionConstraints.h"
#include "Chaos/Defines.h"
#include "Chaos/EvolutionTraits.h"


class CHAOS_API FPerSolverFieldSystem
{
public:

	/**
	 * Services queued \c FFieldSystemCommand commands.
	 *
	 * Supported fields:
	 *	* EFieldPhysicsType::Field_DynamicState
	 *	* EFieldPhysicsType::Field_ActivateDisabled
	 *	* EFieldPhysicsType::Field_ExternalClusterStrain (clustering)
	 *	* EFieldPhysicsType::Field_Kill
	 *	* EFieldPhysicsType::Field_LinearVelocity
	 *	* EFieldPhysicsType::Field_AngularVelociy
	 *	* EFieldPhysicsType::Field_SleepingThreshold
	 *	* EFieldPhysicsType::Field_DisableThreshold
	 *	* EFieldPhysicsType::Field_InternalClusterStrain (clustering)
	 *	* EFieldPhysicsType::Field_CollisionGroup
	 *	* EFieldPhysicsType::Field_PositionStatic
	 *	* EFieldPhysicsType::Field_PositionTarget
	 *	* EFieldPhysicsType::Field_PositionAnimated
	 *	* EFieldPhysicsType::Field_DynamicConstraint
	 */
	template <typename Traits>
	void FieldParameterUpdateCallback(
		Chaos::TPBDRigidsSolver<Traits>* InSolver, 
		Chaos::TPBDRigidParticles<float, 3>& InParticles, 
		Chaos::TArrayCollectionArray<float>& Strains, 
		Chaos::TPBDPositionConstraints<float, 3>& PositionTarget, 
		TMap<int32, int32>& PositionTargetedParticles, 
		//const TArray<FKinematicProxy>& AnimatedPositions, 
		const float InTime);

	/**
	 * Services queued \c FFieldSystemCommand commands.
	 *
	 * Supported fields:
	 *	* EFieldPhysicsType::Field_LinearForce
	 *	* EFieldPhysicsType::Field_AngularTorque
	 */
	template <typename Traits>
	void FieldForcesUpdateCallback(
		Chaos::TPBDRigidsSolver<Traits>* InSolver, 
		Chaos::TPBDRigidParticles<float, 3>& Particles, 
		Chaos::TArrayCollectionArray<FVector> & Force, 
		Chaos::TArrayCollectionArray<FVector> & Torque, 
		const float Time);

	void BufferCommand(const FFieldSystemCommand& InCommand);

	/**
	 * Generates a mapping between the Position array and the results array. 
	 *
	 * When \p ResolutionType is set to \c Maximum the complete particle mapping 
	 * is provided from the \c Particles.X to \c Particles.Attribute. 
	 * When \c Minimum is set only the ActiveIndices and the direct children of 
	 * the active clusters are set in the \p IndicesArray.
	 */

	template <typename Traits>
	static void GetParticleHandles(
		TArray<Chaos::TGeometryParticleHandle<float,3>*>& Handles,
		const Chaos::TPBDRigidsSolver<Traits>* RigidSolver,
		const EFieldResolutionType ResolutionType,
		const bool bForce = true);

private:

	TArray<FFieldSystemCommand> Commands;
};

#define EVOLUTION_TRAIT(Traits)\
extern template CHAOS_API void FPerSolverFieldSystem::FieldParameterUpdateCallback(\
		Chaos::TPBDRigidsSolver<Chaos::Traits>* InSolver, \
		Chaos::TPBDRigidParticles<float, 3>& InParticles, \
		Chaos::TArrayCollectionArray<float>& Strains, \
		Chaos::TPBDPositionConstraints<float, 3>& PositionTarget, \
		TMap<int32, int32>& PositionTargetedParticles, \
		const float InTime);\
\
extern template CHAOS_API void FPerSolverFieldSystem::FieldForcesUpdateCallback(\
		Chaos::TPBDRigidsSolver<Chaos::Traits>* InSolver, \
		Chaos::TPBDRigidParticles<float, 3>& Particles, \
		Chaos::TArrayCollectionArray<FVector> & Force, \
		Chaos::TArrayCollectionArray<FVector> & Torque, \
		const float Time);\
\
extern template CHAOS_API void FPerSolverFieldSystem::GetParticleHandles(\
		TArray<Chaos::TGeometryParticleHandle<float,3>*>& Handles,\
		const Chaos::TPBDRigidsSolver<Chaos::Traits>* RigidSolver,\
		const EFieldResolutionType ResolutionType,\
		const bool bForce);\

#include "Chaos/EvolutionTraits.inl"
#undef EVOLUTION_TRAIT
