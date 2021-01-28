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
		Chaos::TPBDPositionConstraints<float, 3>& PositionTarget,
		TMap<int32, int32>& TargetedParticles);

	/**
	 * Services queued \c FFieldSystemCommand commands.
	 *
	 * Supported fields:
	 *	* EFieldPhysicsType::Field_LinearForce
	 *	* EFieldPhysicsType::Field_AngularTorque
	 */
	template <typename Traits>
	void FieldForcesUpdateCallback(
		Chaos::TPBDRigidsSolver<Traits>* RigidSolver);

	/** Add the transient field command */
	void AddTransientCommand(const FFieldSystemCommand& InCommand);

	/** Add the persistent field command */
	void AddPersistentCommand(const FFieldSystemCommand& FieldCommand);

	/** Remove the transient field command */
	void RemoveTransientCommand(const FFieldSystemCommand& InCommand);

	/** Remove the persistent field command */
	void RemovePersistentCommand(const FFieldSystemCommand& FieldCommand);

	/**
	 * Generates a mapping between the Position array and the results array. 
	 *
	 * When \p ResolutionType is set to \c Maximum the complete particle mapping 
	 * is provided from the \c Particles.X to \c Particles.Attribute. 
	 * When \c Minimum is set only the ActiveIndices and the direct children of 
	 * the active clusters are set in the \p IndicesArray.
	 */

	template <typename Traits>
	static void GetRelevantParticleHandles(
		TArray<Chaos::TGeometryParticleHandle<float,3>*>& ParticleHandles,
		const Chaos::TPBDRigidsSolver<Traits>* RigidSolver,
		const EFieldResolutionType ResolutionType);

	/**
	 * Generates a mapping between the Position array and the results array.
	 *
	 * When \p FilterType is set to \c Active the complete particle mapping
	 * is provided from the \c Particles.X to \c Particles.Attribute.
	 */

	template <typename Traits>
	static void GetFilteredParticleHandles(
		TArray<Chaos::TGeometryParticleHandle<float, 3>*>& ParticleHandles,
		const Chaos::TPBDRigidsSolver<Traits>* RigidSolver,
		const EFieldFilterType FilterType);

private:

	/** Forces update callback implementation */
	template <typename Traits>
	void FieldForcesUpdateInternal(
		Chaos::TPBDRigidsSolver<Traits>* RigidSolver,
		TArray<FFieldSystemCommand>& Commands, 
		const bool IsTransient);

	/** Parameter update callback implementation */
	template <typename Traits>
	void FieldParameterUpdateInternal(
		Chaos::TPBDRigidsSolver<Traits>* RigidSolver,
		Chaos::TPBDPositionConstraints<float, 3>& PositionTarget,
		TMap<int32, int32>& PositionTargetedParticles,
		TArray<FFieldSystemCommand>& Commands, 
		const bool IsTransient);

	/** Transient commands to be processed by the chaos solver */
	TArray<FFieldSystemCommand> TransientCommands;

	/** Persistent commands to be processed by the chaos solver */
	TArray<FFieldSystemCommand> PersistentCommands;
};

#define EVOLUTION_TRAIT(Traits)\
extern template CHAOS_API void FPerSolverFieldSystem::FieldParameterUpdateCallback(\
		Chaos::TPBDRigidsSolver<Chaos::Traits>* InSolver, \
		Chaos::TPBDPositionConstraints<float, 3>& PositionTarget, \
		TMap<int32, int32>& TargetedParticles);\
\
extern template CHAOS_API void FPerSolverFieldSystem::FieldForcesUpdateCallback(\
		Chaos::TPBDRigidsSolver<Chaos::Traits>* InSolver);\
\
extern template CHAOS_API void FPerSolverFieldSystem::GetRelevantParticleHandles(\
		TArray<Chaos::TGeometryParticleHandle<float,3>*>& Handles,\
		const Chaos::TPBDRigidsSolver<Chaos::Traits>* RigidSolver,\
		const EFieldResolutionType ResolutionType);\
\
extern template CHAOS_API void FPerSolverFieldSystem::GetFilteredParticleHandles(\
		TArray<Chaos::TGeometryParticleHandle<float,3>*>& Handles,\
		const Chaos::TPBDRigidsSolver<Chaos::Traits>* RigidSolver,\
		const EFieldFilterType FilterType);\

#include "Chaos/EvolutionTraits.inl"
#undef EVOLUTION_TRAIT
