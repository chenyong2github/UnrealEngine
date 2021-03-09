// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Field/FieldSystem.h"
#include "Field/FieldSystemTypes.h"
#include "Chaos/PBDPositionConstraints.h"
#include "Chaos/Defines.h"

/** Enum to specify on whjich array the intermediate fields results are going to be stored */
enum class EFieldCommandResultType : uint8
{
	FinalResult = 0,
	LeftResult = 1,
	RightResult = 2,
	NumResults = 3
};

class CHAOS_API FPerSolverFieldSystem
{
public:

	/**
	 * Services queued \c FFieldSystemCommand commands.
	 *
	 * Supported fields:
	 * EFieldPhysicsType::Field_DynamicState
	 * EFieldPhysicsType::Field_ActivateDisabled
	 * EFieldPhysicsType::Field_ExternalClusterStrain (clustering)
	 * EFieldPhysicsType::Field_Kill
	 * EFieldPhysicsType::Field_LinearVelocity
	 * EFieldPhysicsType::Field_AngularVelociy
	 * EFieldPhysicsType::Field_SleepingThreshold
	 * EFieldPhysicsType::Field_DisableThreshold
	 * EFieldPhysicsType::Field_InternalClusterStrain (clustering)
	 * EFieldPhysicsType::Field_CollisionGroup
	 * EFieldPhysicsType::Field_PositionStatic
	 * EFieldPhysicsType::Field_PositionTarget
	 * EFieldPhysicsType::Field_PositionAnimated
	 * EFieldPhysicsType::Field_DynamicConstraint
	 */
	void FieldParameterUpdateCallback(
		Chaos::FPBDRigidsSolver* InSolver, 
		Chaos::FPBDPositionConstraints& PositionTarget,
		TMap<int32, int32>& TargetedParticles);

	/**
	 * Services queued \c FFieldSystemCommand commands.
	 *
	 * Supported fields:
	 * EFieldPhysicsType::Field_LinearForce
	 * EFieldPhysicsType::Field_AngularTorque
	 */
	void FieldForcesUpdateCallback(
		Chaos::FPBDRigidsSolver* RigidSolver);

	/**
	 * Compute field linear velocity/force and angular velocity/torque given a list of samples (positions + indices)
	 *
	 * Supported fields:
	 * FieldPhysicsType::Field_LinearVelocity
	 * EFieldPhysicsType::Field_LinearForce
	 * EFieldPhysicsType::Field_AngularVelocity
	 * EFieldPhysicsType::Field_AngularrTorque
	 */
	void ComputeFieldRigidImpulse(const float SolverTime);

	/**
	 * Compute field linear velocity/force given a list of samples (positions + indices)
	 *
	 * Supported fields:
	 * EFieldPhysicsType::Field_LinearVelocity
	 * EFieldPhysicsType::Field_LinearForce
	 */
	void ComputeFieldLinearImpulse(const float SolverTime);

	/** Add the transient field command */
	void AddTransientCommand(const FFieldSystemCommand& FieldCommand);

	/** Add the persistent field command */
	void AddPersistentCommand(const FFieldSystemCommand& FieldCommand);

	/** Remove the transient field command */
	void RemoveTransientCommand(const FFieldSystemCommand& FieldCommand);

	/** Remove the persistent field command */
	void RemovePersistentCommand(const FFieldSystemCommand& FieldCommand);

	/** Get all the non const transient field commands */
	TArray<FFieldSystemCommand>& GetTransientCommands() { return TransientCommands; }

	/** Get all the const transient field commands */
	const TArray<FFieldSystemCommand>& GetTransientCommands() const { return TransientCommands; }

	/** Get all the non const persistent field commands */
	TArray<FFieldSystemCommand>& GetPersistentCommands() { return PersistentCommands; }

	/** Get all the const persistent field commands */
	const TArray<FFieldSystemCommand>& GetPersistentCommands() const { return PersistentCommands; }

	/**
	 * Generates a mapping between the Position array and the results array. 
	 *
	 * When \p ResolutionType is set to \c Maximum the complete particle mapping 
	 * is provided from the \c Particles.X to \c Particles.Attribute. 
	 * When \c Minimum is set only the ActiveIndices and the direct children of 
	 * the active clusters are set in the \p IndicesArray.
	 */

	static void GetRelevantParticleHandles(
		TArray<Chaos::FGeometryParticleHandle*>& ParticleHandles,
		const Chaos::FPBDRigidsSolver* RigidSolver,
		const EFieldResolutionType ResolutionType);

	/**
	 * Generates a mapping between the Position array and the results array.
	 *
	 * When \p FilterType is set to \c Active the complete particle mapping
	 * is provided from the \c Particles.X to \c Particles.Attribute.
	 */

	static void GetFilteredParticleHandles(
		TArray<Chaos::FGeometryParticleHandle*>& ParticleHandles,
		const Chaos::FPBDRigidsSolver* RigidSolver,
		const EFieldFilterType FilterType);

	/** Check if a per solver field system has no commands. */
	bool IsEmpty() const { return (TransientCommands.Num() == 0) && (PersistentCommands.Num() == 0); }

	/** Get the non const array of sample positions */
	const TArray<FVector>& GetSamplePositions() const { return SamplePositions; }

	/** Get the const array of sample positions */
	TArray<FVector>& GetSamplePositions() { return SamplePositions; }

	/** Get the const array of sample indices */
	const TArray<FFieldContextIndex>& GetSampleIndices() const { return SampleIndices; }

	/** Get the non const array of sample indices */
	TArray<FFieldContextIndex>& GetSampleIndices() { return SampleIndices; }

	/** Get the non const array of the vector results given a vector type*/
	TArray<FVector>& GetVectorResults(const EFieldVectorType ResultType) { return VectorResults[ResultType]; }

	/** Get the non const array of the vector results given a vector type*/
	const TArray<FVector>& GetVectorResults(const EFieldVectorType ResultType) const { return VectorResults[ResultType]; }

	/** Get the non const array of the scalar results given a scalar type*/
	TArray<float>& GetScalarResults(const EFieldScalarType ResultType) { return ScalarResults[ResultType]; }

	/** Get the non const array of the scalar results given a scalar type*/
	const TArray<float>& GetScalarResults(const EFieldScalarType ResultType) const { return ScalarResults[ResultType]; }

	/** Get the non const array of the integer results given a integer type*/
	TArray<int32>& GetIntegerResults(const EFieldIntegerType ResultType) { return IntegerResults[ResultType]; }

	/** Get the non const array of the integer results given a integer type*/
	const TArray<int32>& GetIntegerrResults(const EFieldIntegerType ResultType) const { return IntegerResults[ResultType]; }

private:

	/** Forces update callback implementation */
	void FieldForcesUpdateInternal(
		Chaos::FPBDRigidsSolver* RigidSolver,
		TArray<FFieldSystemCommand>& Commands, 
		const bool IsTransient);

	/** Parameter update callback implementation */
	void FieldParameterUpdateInternal(
		Chaos::FPBDRigidsSolver* RigidSolver,
		Chaos::FPBDPositionConstraints& PositionTarget,
		TMap<int32, int32>& PositionTargetedParticles,
		TArray<FFieldSystemCommand>& Commands, 
		const bool IsTransient);

	/** Sample positions to be used to build the context */
	TArray<FVector> SamplePositions;

	/** Sample indices to be used to build the context  */
	TArray<FFieldContextIndex> SampleIndices;

	/** Field vector targets results */
	TArray<FVector> VectorResults[EFieldVectorType::Vector_TargetMax + (uint8)(EFieldCommandResultType::NumResults)];

	/** Field scalar targets results */
	TArray<float> ScalarResults[EFieldScalarType::Scalar_TargetMax + (uint8)(EFieldCommandResultType::NumResults)];

	/** Field integer targets results */
	TArray<int32> IntegerResults[EFieldIntegerType::Integer_TargetMax + (uint8)(EFieldCommandResultType::NumResults)];

	/** Transient commands to be processed by the chaos solver */
	TArray<FFieldSystemCommand> TransientCommands;

	/** Persistent commands to be processed by the chaos solver */
	TArray<FFieldSystemCommand> PersistentCommands;
};
