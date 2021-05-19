// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsProxy/PerSolverFieldSystem.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "Field/FieldSystem.h"
#include "Chaos/PBDRigidClustering.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"
#include "PhysicsProxy/FieldSystemProxyHelper.h"
#include "PhysicsSolver.h"
#include "ChaosStats.h"

void ResetIndicesArray(TArray<int32>& IndicesArray, int32 Size)
{
	if (IndicesArray.Num() != Size)
	{
		IndicesArray.SetNum(Size);
		for (int32 i = 0; i < IndicesArray.Num(); ++i)
		{
			IndicesArray[i] = i;
		}
	}
}

//==============================================================================
// FPerSolverFieldSystem
//==============================================================================

void FPerSolverFieldSystem::FieldParameterUpdateInternal(
	Chaos::FPBDRigidsSolver* RigidSolver,
	Chaos::FPBDPositionConstraints& PositionTarget,
	TMap<int32, int32>& TargetedParticles,
	TArray<FFieldSystemCommand>& Commands, const bool IsTransient)
{
	SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_Object);

	const int32 NumCommands = Commands.Num();
	if (NumCommands && RigidSolver)
	{
		TArray<int32> CommandsToRemove;
		CommandsToRemove.Reserve(NumCommands);

		TArray<Chaos::FGeometryParticleHandle*> ParticleHandles;

		EFieldResolutionType PrevResolutionType = EFieldResolutionType::Field_Resolution_Max;
		EFieldFilterType PrevFilterType = EFieldFilterType::Field_Filter_Max;

		for (int32 CommandIndex = 0; CommandIndex < NumCommands; CommandIndex++)
		{
			const FFieldSystemCommand& FieldCommand = Commands[CommandIndex];
			if( Chaos::BuildFieldSamplePoints(this, RigidSolver, FieldCommand, ParticleHandles, SamplePositions, SampleIndices, PrevResolutionType, PrevFilterType) )
			{
				const float TimeSeconds = RigidSolver->GetSolverTime() - FieldCommand.TimeCreation;

				TArrayView<FVector> SamplePointsView(&(SamplePositions[0]), SamplePositions.Num());
				TArrayView<FFieldContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

				FFieldContext FieldContext(
					SampleIndicesView,
					SamplePointsView,
					FieldCommand.MetaData,
					TimeSeconds);

				const EFieldOutputType FieldOutput = GetFieldTargetOutput(GetFieldPhysicsType(FieldCommand.TargetAttribute));
				if ((FieldOutput == EFieldOutputType::Field_Output_Integer) && (FieldCommand.RootNode->Type() == FFieldNodeBase::EFieldType::EField_Int32))
				{
					Chaos::FieldIntegerParameterUpdate(RigidSolver, FieldCommand, ParticleHandles, FieldContext, CommandsToRemove, PositionTarget, TargetedParticles, CommandIndex);
				}
				else if ((FieldOutput == EFieldOutputType::Field_Output_Scalar) && (FieldCommand.RootNode->Type() == FFieldNodeBase::EFieldType::EField_Float))
				{
					Chaos::FieldScalarParameterUpdate(RigidSolver, FieldCommand, ParticleHandles, FieldContext, CommandsToRemove, PositionTarget, TargetedParticles, CommandIndex);
				}
				else if ((FieldOutput == EFieldOutputType::Field_Output_Vector) && (FieldCommand.RootNode->Type() == FFieldNodeBase::EFieldType::EField_FVector))
				{
					Chaos::FieldVectorParameterUpdate(RigidSolver, FieldCommand, ParticleHandles, FieldContext, CommandsToRemove, PositionTarget, TargetedParticles, CommandIndex);
				}
				else
				{
					UE_LOG(LogChaos, Error, TEXT("Field based evaluation of the simulation %s parameter expects %s field inputs."),
						*FieldCommand.TargetAttribute.ToString(), *GetFieldOutputName(FieldOutput).ToString());
					CommandsToRemove.Add(CommandIndex);
				}
			}
		}
		if (IsTransient)
		{
			for (int32 Index = CommandsToRemove.Num() - 1; Index >= 0; --Index)
			{
				Commands.RemoveAt(CommandsToRemove[Index]);
			}
		}
	}
}

void FPerSolverFieldSystem::FieldParameterUpdateCallback(
	Chaos::FPBDRigidsSolver* InSolver,
	Chaos::FPBDPositionConstraints& PositionTarget,
	TMap<int32, int32>& TargetedParticles)
{
	if (InSolver && !InSolver->IsShuttingDown())
	{
		FieldParameterUpdateInternal(InSolver, PositionTarget, TargetedParticles, TransientCommands, true);
		FieldParameterUpdateInternal(InSolver, PositionTarget, TargetedParticles, PersistentCommands, false);
	}
}

void FPerSolverFieldSystem::FieldForcesUpdateInternal(
	Chaos::FPBDRigidsSolver* RigidSolver,
	TArray<FFieldSystemCommand>& Commands, const bool IsTransient)
{
	SCOPE_CYCLE_COUNTER(STAT_ForceUpdateField_Object);

	const int32 NumCommands = Commands.Num();
	if (NumCommands && RigidSolver)
	{
		TArray<int32> CommandsToRemove;
		CommandsToRemove.Reserve(NumCommands);

		TArray<Chaos::FGeometryParticleHandle*> ParticleHandles;

		EFieldResolutionType PrevResolutionType = EFieldResolutionType::Field_Resolution_Max;
		EFieldFilterType PrevFilterType = EFieldFilterType::Field_Filter_Max;

		for (int32 CommandIndex = 0; CommandIndex < NumCommands; CommandIndex++)
		{
			const FFieldSystemCommand& FieldCommand = Commands[CommandIndex];

			if (Chaos::BuildFieldSamplePoints(this, RigidSolver, FieldCommand, ParticleHandles, SamplePositions, SampleIndices, PrevResolutionType, PrevFilterType))
			{
				const float TimeSeconds = RigidSolver->GetSolverTime() - FieldCommand.TimeCreation;

				TArrayView<FVector> SamplePointsView(&(SamplePositions[0]), SamplePositions.Num());
				TArrayView<FFieldContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

				FFieldContext FieldContext(
					SampleIndicesView,
					SamplePointsView,
					FieldCommand.MetaData,
					TimeSeconds);

				if (FieldCommand.RootNode->Type() == FFieldNode<FVector>::StaticType())
				{
					Chaos::FieldVectorForceUpdate(RigidSolver, FieldCommand, ParticleHandles, FieldContext, CommandsToRemove, CommandIndex);
				}
			}
		}
		if (IsTransient)
		{
			for (int32 Index = CommandsToRemove.Num() - 1; Index >= 0; --Index)
			{
				Commands.RemoveAt(CommandsToRemove[Index]);
			}
		}
	}
}

void FPerSolverFieldSystem::FieldForcesUpdateCallback(
	Chaos::FPBDRigidsSolver* InSolver)
{
	if (InSolver && !InSolver->IsShuttingDown())
	{
		FieldForcesUpdateInternal(InSolver, TransientCommands, true);
		FieldForcesUpdateInternal(InSolver, PersistentCommands, false);
	}
}

template<typename FieldType, int32 ArraySize>
FORCEINLINE void ResetInternalArrays(const int32 FieldSize, const TArray<int32>& FieldTargets, TArray<FieldType> FieldArray[ArraySize], const FieldType DefaultValue)
{
	for (const int32& FieldTarget : FieldTargets)
	{
		if (FieldTarget < ArraySize)
		{
			FieldArray[FieldTarget].SetNum(FieldSize,false);
			for (int32 i = 0; i < FieldSize; ++i)
			{
				FieldArray[FieldTarget][i] = DefaultValue;
			}
		}
	}
}
template<typename FieldType, int32 ArraySize>
FORCEINLINE void EmptyInternalArrays(const TArray<int32>& FieldTargets, TArray<FieldType> FieldArray[ArraySize])
{
	for (const int32& FieldTarget : FieldTargets)
	{
		if (FieldTarget < ArraySize)
		{
			FieldArray[FieldTarget].SetNum(0, false);
		}
	}
}

FORCEINLINE void EvaluateImpulseField(
	const FFieldSystemCommand& FieldCommand,
	FFieldContext& FieldContext,
	TArrayView<FVector>& ResultsView,
	TArray<FVector>& OutputImpulse)
{
	static_cast<const FFieldNode<FVector>*>(FieldCommand.RootNode.Get())->Evaluate(FieldContext, ResultsView);
	if (OutputImpulse.Num() == 0)
	{
		OutputImpulse.SetNum(ResultsView.Num(), false);
		for (const FFieldContextIndex& Index : FieldContext.GetEvaluatedSamples())
		{
			if (Index.Sample < OutputImpulse.Num() && Index.Result < ResultsView.Num())
			{
				OutputImpulse[Index.Sample] = ResultsView[Index.Result];
			}
		}
	}
	else
	{
		for (const FFieldContextIndex& Index : FieldContext.GetEvaluatedSamples())
		{
			if (Index.Sample < OutputImpulse.Num() && Index.Result < ResultsView.Num())
			{
				OutputImpulse[Index.Sample] += ResultsView[Index.Result];
			}
		}
	}
}

void ComputeFieldRigidImpulseInternal(
	TArray<FVector>& SamplePositions,
	TArray<FFieldContextIndex>& SampleIndices,
	const float SolverTime,
	TArray<FVector>& FinalResults,
	TArray<FVector>& LinearVelocities,
	TArray<FVector>& LinearForces,
	TArray<FVector>& AngularVelocities,
	TArray<FVector>& AngularTorques,
	TArray<FFieldSystemCommand>& Commands, const bool IsTransient)
{
	const int32 NumCommands = Commands.Num();
	if (NumCommands)
	{
		TArray<int32> CommandsToRemove;
		CommandsToRemove.Reserve(NumCommands);

		for (int32 CommandIndex = 0; CommandIndex < NumCommands; CommandIndex++)
		{
			const FFieldSystemCommand& FieldCommand = Commands[CommandIndex];
			const float TimeSeconds = SolverTime - FieldCommand.TimeCreation;

			const TArrayView<FVector> SamplePointsView(&(SamplePositions[0]), SamplePositions.Num());
			const TArrayView<FFieldContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

			FFieldContext FieldContext(
				SampleIndicesView,
				SamplePointsView,
				FieldCommand.MetaData,
				TimeSeconds);

			if (FieldCommand.RootNode->Type() == FFieldNode<FVector>::StaticType())
			{
				TArrayView<FVector> ResultsView(&(FinalResults[0]), FinalResults.Num());

				if (FieldCommand.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_LinearVelocity))
				{
					SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_LinearVelocity);
					
					EvaluateImpulseField(FieldCommand, FieldContext, ResultsView, LinearVelocities);
				}
				else if (FieldCommand.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_LinearForce))
				{
					SCOPE_CYCLE_COUNTER(STAT_ForceUpdateField_LinearForce);

					EvaluateImpulseField(FieldCommand, FieldContext, ResultsView, LinearForces);
				}
				if (FieldCommand.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_AngularVelociy))
				{
					SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_AngularVelocity);

					EvaluateImpulseField(FieldCommand, FieldContext, ResultsView, AngularVelocities);
				}
				else if (FieldCommand.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_AngularTorque))
				{
					SCOPE_CYCLE_COUNTER(STAT_ForceUpdateField_AngularTorque);

					EvaluateImpulseField(FieldCommand, FieldContext, ResultsView, AngularTorques);
				}
			}
			CommandsToRemove.Add(CommandIndex);
		}
		if (IsTransient)
		{
			for (int32 Index = CommandsToRemove.Num() - 1; Index >= 0; --Index)
			{
				Commands.RemoveAt(CommandsToRemove[Index]);
			}
		}
	}
}

void FPerSolverFieldSystem::ComputeFieldRigidImpulse(
	const float SolverTime)
{
	static const TArray<int32> EmptyTargets = { EFieldVectorType::Vector_LinearVelocity,
												EFieldVectorType::Vector_LinearForce,
												EFieldVectorType::Vector_AngularVelocity,
												EFieldVectorType::Vector_AngularTorque };
	static const TArray<int32> ResetTargets = { EFieldVectorType::Vector_TargetMax + (uint8)EFieldCommandResultType::FinalResult };

	EmptyInternalArrays < FVector, EFieldVectorType::Vector_TargetMax + (uint8)EFieldCommandResultType::NumResults > (EmptyTargets, VectorResults);
	ResetInternalArrays < FVector, EFieldVectorType::Vector_TargetMax + (uint8)EFieldCommandResultType::NumResults > (SamplePositions.Num(), ResetTargets, VectorResults, FVector::ZeroVector);

	ComputeFieldRigidImpulseInternal(SamplePositions, SampleIndices, SolverTime, VectorResults[EFieldVectorType::Vector_TargetMax + (uint8)EFieldCommandResultType::FinalResult],
		VectorResults[EFieldVectorType::Vector_LinearVelocity], VectorResults[EFieldVectorType::Vector_LinearForce], 
		VectorResults[EFieldVectorType::Vector_AngularVelocity], VectorResults[EFieldVectorType::Vector_AngularTorque], TransientCommands, true);
	ComputeFieldRigidImpulseInternal(SamplePositions, SampleIndices, SolverTime, VectorResults[EFieldVectorType::Vector_TargetMax + (uint8)EFieldCommandResultType::FinalResult],
		VectorResults[EFieldVectorType::Vector_LinearVelocity], VectorResults[EFieldVectorType::Vector_LinearForce],
		VectorResults[EFieldVectorType::Vector_AngularVelocity], VectorResults[EFieldVectorType::Vector_AngularTorque], PersistentCommands, false);
}

void ComputeFieldLinearImpulseInternal(
	TArray<FVector>& SamplePositions,
	TArray<FFieldContextIndex>& SampleIndices,
	const float SolverTime,
	TArray<FVector>& FinalResults,
	TArray<FVector>& LinearVelocities,
	TArray<FVector>& LinearForces,
	TArray<FFieldSystemCommand>& Commands, const bool IsTransient)
{
	const int32 NumCommands = Commands.Num();
	if (NumCommands)
	{
		TArray<int32> CommandsToRemove;
		CommandsToRemove.Reserve(NumCommands);

		for (int32 CommandIndex = 0; CommandIndex < NumCommands; CommandIndex++)
		{
			const FFieldSystemCommand& FieldCommand = Commands[CommandIndex];
			const float TimeSeconds = SolverTime - FieldCommand.TimeCreation;

			const TArrayView<FVector> SamplePointsView(&(SamplePositions[0]), SamplePositions.Num());
			const TArrayView<FFieldContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

			FFieldContext FieldContext(
				SampleIndicesView,
				SamplePointsView,
				FieldCommand.MetaData,
				TimeSeconds);

			TArrayView<FVector> ResultsView(&(FinalResults[0]), FinalResults.Num());

			if (FieldCommand.RootNode->Type() == FFieldNode<FVector>::StaticType())
			{
				if (FieldCommand.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_LinearVelocity))
				{
					SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_LinearVelocity);

					EvaluateImpulseField(FieldCommand, FieldContext, ResultsView, LinearVelocities);
				}
				else if (FieldCommand.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_LinearForce))
				{
					SCOPE_CYCLE_COUNTER(STAT_ForceUpdateField_LinearForce);

					EvaluateImpulseField(FieldCommand, FieldContext, ResultsView, LinearForces);
				}
			}
			CommandsToRemove.Add(CommandIndex);
		}
		if (IsTransient)
		{
			for (int32 Index = CommandsToRemove.Num() - 1; Index >= 0; --Index)
			{
				Commands.RemoveAt(CommandsToRemove[Index]);
			}
		}
	}
}

void FPerSolverFieldSystem::ComputeFieldLinearImpulse(const float SolverTime)
{
	static const TArray<int32> EmptyTargets = { EFieldVectorType::Vector_LinearVelocity,
												EFieldVectorType::Vector_LinearForce };
	static const TArray<int32> ResetTargets = { EFieldVectorType::Vector_TargetMax + (uint8)EFieldCommandResultType::FinalResult };

	EmptyInternalArrays < FVector, EFieldVectorType::Vector_TargetMax + (uint8)EFieldCommandResultType::NumResults > (EmptyTargets, VectorResults);
	ResetInternalArrays < FVector, EFieldVectorType::Vector_TargetMax + (uint8)EFieldCommandResultType::NumResults > (SamplePositions.Num(), ResetTargets, VectorResults, FVector::ZeroVector);

	ComputeFieldLinearImpulseInternal(SamplePositions, SampleIndices, SolverTime, VectorResults[EFieldVectorType::Vector_TargetMax + (uint8)EFieldCommandResultType::FinalResult], 
		VectorResults[EFieldVectorType::Vector_LinearVelocity], VectorResults[EFieldVectorType::Vector_LinearForce], TransientCommands, true);
	ComputeFieldLinearImpulseInternal(SamplePositions, SampleIndices, SolverTime, VectorResults[EFieldVectorType::Vector_TargetMax + (uint8)EFieldCommandResultType::FinalResult], 
		VectorResults[EFieldVectorType::Vector_LinearVelocity], VectorResults[EFieldVectorType::Vector_LinearForce], PersistentCommands, false);
}

void FPerSolverFieldSystem::AddTransientCommand(const FFieldSystemCommand& FieldCommand)
{
	TransientCommands.Add(FieldCommand);
}

void FPerSolverFieldSystem::AddPersistentCommand(const FFieldSystemCommand& FieldCommand)
{
	PersistentCommands.Add(FieldCommand);
}

void FPerSolverFieldSystem::RemoveTransientCommand(const FFieldSystemCommand& FieldCommand)
{
	TransientCommands.Remove(FieldCommand);
}

void FPerSolverFieldSystem::RemovePersistentCommand(const FFieldSystemCommand& FieldCommand)
{
	PersistentCommands.Remove(FieldCommand);
}

void FPerSolverFieldSystem::GetRelevantParticleHandles(
	TArray<Chaos::FGeometryParticleHandle*>& Handles,
	const Chaos::FPBDRigidsSolver* RigidSolver,
	const EFieldResolutionType ResolutionType)
{
	Handles.SetNum(0, false);
	const Chaos::FPBDRigidsSOAs& SolverParticles = RigidSolver->GetParticles();

	if (ResolutionType == EFieldResolutionType::Field_Resolution_Minimal)
	{
		const auto& Clustering = RigidSolver->GetEvolution()->GetRigidClustering();
		const auto& ClusterMap = Clustering.GetChildrenMap();

		const Chaos::TParticleView<Chaos::FGeometryParticles>& ParticleView =
			SolverParticles.GetNonDisabledView();
		Handles.Reserve(ParticleView.Num()); // ?? what about additional number of children added
		for (Chaos::TParticleIterator<Chaos::FGeometryParticles> It = ParticleView.Begin(), ItEnd = ParticleView.End();
			It != ItEnd; ++It)
		{
			const Chaos::TTransientGeometryParticleHandle<Chaos::FReal,3>* Handle = &(*It);
			Handles.Add(GetHandleHelper(const_cast<Chaos::TTransientGeometryParticleHandle<Chaos::FReal,3>*>(Handle)));

			const auto* Clustered = Handle->CastToClustered();
			if (Clustered && Clustered->ClusterIds().NumChildren)
			{
				Chaos::FPBDRigidParticleHandle* RigidHandle = (*It).Handle()->CastToRigidParticle();
				if (ClusterMap.Contains(RigidHandle))
				{
					for (Chaos::FPBDRigidParticleHandle * Child : ClusterMap[RigidHandle])
					{
						Handles.Add(Child);
					}
				}
			}
		}
	}
	else if (ResolutionType == EFieldResolutionType::Field_Resolution_DisabledParents)
	{
		const auto& Clustering = RigidSolver->GetEvolution()->GetRigidClustering();
		const auto& ClusterMap = Clustering.GetChildrenMap();
		Handles.Reserve(Clustering.GetTopLevelClusterParents().Num());

		for (Chaos::TPBDRigidClusteredParticleHandle<Chaos::FReal, 3> * TopLevelParent : Clustering.GetTopLevelClusterParents())
		{
			Handles.Add(TopLevelParent);
		}
	}
	else if (ResolutionType == EFieldResolutionType::Field_Resolution_Maximum)
	{
		const Chaos::TParticleView<Chaos::FGeometryParticles>& ParticleView =
			SolverParticles.GetAllParticlesView();
		Handles.Reserve(ParticleView.Num());

		for (Chaos::TParticleIterator<Chaos::FGeometryParticles> It = ParticleView.Begin(), ItEnd = ParticleView.End();
			It != ItEnd; ++It)
		{
			const Chaos::TTransientGeometryParticleHandle<Chaos::FReal,3>* Handle = &(*It);
			Handles.Add(GetHandleHelper(const_cast<Chaos::TTransientGeometryParticleHandle<Chaos::FReal,3>*>(Handle)));
		}
	}
}

void FPerSolverFieldSystem::GetFilteredParticleHandles(
	TArray<Chaos::FGeometryParticleHandle*>& Handles,
	const Chaos::FPBDRigidsSolver* RigidSolver,
	const EFieldFilterType FilterType)
{
	Handles.SetNum(0, false);
	const Chaos::FPBDRigidsSOAs& SolverParticles = RigidSolver->GetParticles();

	if (FilterType == EFieldFilterType::Field_Filter_Dynamic)
	{
		const Chaos::TParticleView<Chaos::TPBDRigidParticles<Chaos::FReal, 3>>& ParticleView =
			SolverParticles.GetNonDisabledDynamicView();
		Handles.Reserve(ParticleView.Num());

		for (Chaos::TParticleIterator<Chaos::TPBDRigidParticles<Chaos::FReal, 3>> It = ParticleView.Begin(), ItEnd = ParticleView.End();
			It != ItEnd; ++It)
		{
			const Chaos::TTransientGeometryParticleHandle<Chaos::FReal,3>* Handle = &(*It);
			Handles.Add(GetHandleHelper(const_cast<Chaos::TTransientGeometryParticleHandle<Chaos::FReal,3>*>(Handle)));
		}
	}
	else if (FilterType == EFieldFilterType::Field_Filter_Static)
	{
		const Chaos::TParticleView<Chaos::FGeometryParticles>& ParticleView =
			SolverParticles.GetActiveStaticParticlesView();
		Handles.Reserve(ParticleView.Num());

		for (Chaos::TParticleIterator<Chaos::FGeometryParticles> It = ParticleView.Begin(), ItEnd = ParticleView.End();
			It != ItEnd; ++It)
		{
			const Chaos::TTransientGeometryParticleHandle<Chaos::FReal,3>* Handle = &(*It);
			Handles.Add(GetHandleHelper(const_cast<Chaos::TTransientGeometryParticleHandle<Chaos::FReal,3>*>(Handle)));
		}
	}
	else if (FilterType == EFieldFilterType::Field_Filter_Kinematic)
	{
		const Chaos::TParticleView<Chaos::FKinematicGeometryParticles>& ParticleView =
			SolverParticles.GetActiveKinematicParticlesView();
		Handles.Reserve(ParticleView.Num());

		for (Chaos::TParticleIterator<Chaos::FKinematicGeometryParticles> It = ParticleView.Begin(), ItEnd = ParticleView.End();
			It != ItEnd; ++It)
		{
			const Chaos::TTransientGeometryParticleHandle<Chaos::FReal,3>* Handle = &(*It);
			Handles.Add(GetHandleHelper(const_cast<Chaos::TTransientGeometryParticleHandle<Chaos::FReal,3>*>(Handle)));
		}
	}
	else if (FilterType == EFieldFilterType::Field_Filter_All)
	{
		const Chaos::TParticleView<Chaos::FGeometryParticles>& ParticleView =
			SolverParticles.GetAllParticlesView();
		Handles.Reserve(ParticleView.Num());

		for (Chaos::TParticleIterator<Chaos::FGeometryParticles> It = ParticleView.Begin(), ItEnd = ParticleView.End();
			It != ItEnd; ++It)
		{
			const Chaos::TTransientGeometryParticleHandle<Chaos::FReal,3>* Handle = &(*It);
			Handles.Add(GetHandleHelper(const_cast<Chaos::TTransientGeometryParticleHandle<Chaos::FReal,3>*>(Handle)));
		}
	}
}
