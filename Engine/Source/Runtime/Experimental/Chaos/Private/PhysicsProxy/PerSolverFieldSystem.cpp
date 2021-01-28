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

template <typename Traits>
void FPerSolverFieldSystem::FieldParameterUpdateInternal(
	Chaos::TPBDRigidsSolver<Traits>* RigidSolver,
	Chaos::TPBDPositionConstraints<float, 3>& PositionTarget,
	TMap<int32, int32>& TargetedParticles,
	TArray<FFieldSystemCommand>& Commands, const bool IsTransient)
{
	SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_Object);

	const int32 NumCommands = Commands.Num();
	if (NumCommands && RigidSolver)
	{
		TArray<int32> CommandsToRemove;
		CommandsToRemove.Reserve(NumCommands);

		TArray<Chaos::TGeometryParticleHandle<float, 3>*> ParticleHandles;
		TArray<FVector> SamplePoints;
		TArray<ContextIndex> SampleIndices;

		EFieldResolutionType PrevResolutionType = EFieldResolutionType::Field_Resolution_Max;
		EFieldFilterType PrevFilterType = EFieldFilterType::Field_Filter_Max;

		for (int32 CommandIndex = 0; CommandIndex < NumCommands; CommandIndex++)
		{
			const FFieldSystemCommand& FieldCommand = Commands[CommandIndex];
			if( Chaos::BuildFieldSamplePoints(this, RigidSolver, FieldCommand, ParticleHandles, SamplePoints, SampleIndices, PrevResolutionType, PrevFilterType) )
			{
				const float TimeSeconds = RigidSolver->GetSolverTime() - FieldCommand.TimeCreation;

				TArrayView<FVector> SamplePointsView(&(SamplePoints[0]), SamplePoints.Num());
				TArrayView<ContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

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

template <typename Traits>
void FPerSolverFieldSystem::FieldParameterUpdateCallback(
	Chaos::TPBDRigidsSolver<Traits>* InSolver,
	Chaos::TPBDPositionConstraints<float, 3>& PositionTarget,
	TMap<int32, int32>& TargetedParticles)
{
	FieldParameterUpdateInternal(InSolver, PositionTarget, TargetedParticles, TransientCommands, true);
	FieldParameterUpdateInternal(InSolver, PositionTarget, TargetedParticles, PersistentCommands, false);
}

template <typename Traits>
void FPerSolverFieldSystem::FieldForcesUpdateInternal(
	Chaos::TPBDRigidsSolver<Traits>* RigidSolver,
	TArray<FFieldSystemCommand>& Commands, const bool IsTransient)
{
	SCOPE_CYCLE_COUNTER(STAT_ForceUpdateField_Object);

	const int32 NumCommands = Commands.Num();
	if (NumCommands && RigidSolver)
	{
		TArray<int32> CommandsToRemove;
		CommandsToRemove.Reserve(NumCommands);

		TArray<Chaos::TGeometryParticleHandle<float, 3>*> ParticleHandles;
		TArray<FVector> SamplePoints;
		TArray<ContextIndex> SampleIndices;

		EFieldResolutionType PrevResolutionType = EFieldResolutionType::Field_Resolution_Max;
		EFieldFilterType PrevFilterType = EFieldFilterType::Field_Filter_Max;

		for (int32 CommandIndex = 0; CommandIndex < NumCommands; CommandIndex++)
		{
			const FFieldSystemCommand& FieldCommand = Commands[CommandIndex];

			if (Chaos::BuildFieldSamplePoints(this, RigidSolver, FieldCommand, ParticleHandles, SamplePoints, SampleIndices, PrevResolutionType, PrevFilterType))
			{
				const float TimeSeconds = RigidSolver->GetSolverTime() - FieldCommand.TimeCreation;

				TArrayView<FVector> SamplePointsView(&(SamplePoints[0]), SamplePoints.Num());
				TArrayView<ContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

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

template <typename Traits>
void FPerSolverFieldSystem::FieldForcesUpdateCallback(
	Chaos::TPBDRigidsSolver<Traits>* InSolver)
{
	FieldForcesUpdateInternal(InSolver, TransientCommands, true);
	FieldForcesUpdateInternal(InSolver, PersistentCommands, false);
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

template <typename Traits>
void FPerSolverFieldSystem::GetRelevantParticleHandles(
	TArray<Chaos::TGeometryParticleHandle<float, 3>*>& Handles,
	const Chaos::TPBDRigidsSolver<Traits>* RigidSolver,
	const EFieldResolutionType ResolutionType)
{
	Handles.SetNum(0, false);
	const Chaos::TPBDRigidsSOAs<float, 3>& SolverParticles = RigidSolver->GetParticles();

	if (ResolutionType == EFieldResolutionType::Field_Resolution_Minimal)
	{
		const auto& Clustering = RigidSolver->GetEvolution()->GetRigidClustering();
		const auto& ClusterMap = Clustering.GetChildrenMap();

		const Chaos::TParticleView<Chaos::TGeometryParticles<float, 3>>& ParticleView =
			SolverParticles.GetNonDisabledView();
		Handles.Reserve(ParticleView.Num()); // ?? what about additional number of children added
		for (Chaos::TParticleIterator<Chaos::TGeometryParticles<float, 3>> It = ParticleView.Begin(), ItEnd = ParticleView.End();
			It != ItEnd; ++It)
		{
			const Chaos::TTransientGeometryParticleHandle<float, 3>* Handle = &(*It);
			Handles.Add(GetHandleHelper(const_cast<Chaos::TTransientGeometryParticleHandle<float, 3>*>(Handle)));

			const auto* Clustered = Handle->CastToClustered();
			if (Clustered && Clustered->ClusterIds().NumChildren)
			{
				Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = (*It).Handle()->CastToRigidParticle();
				if (ClusterMap.Contains(RigidHandle))
				{
					for (Chaos::TPBDRigidParticleHandle<float, 3> * Child : ClusterMap[RigidHandle])
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

		for (Chaos::TPBDRigidClusteredParticleHandle<float, 3> * TopLevelParent : Clustering.GetTopLevelClusterParents())
		{
			Handles.Add(TopLevelParent);
		}
	}
	else if (ResolutionType == EFieldResolutionType::Field_Resolution_Maximum)
	{
		const Chaos::TParticleView<Chaos::TGeometryParticles<float, 3>>& ParticleView =
			SolverParticles.GetAllParticlesView();
		Handles.Reserve(ParticleView.Num());

		for (Chaos::TParticleIterator<Chaos::TGeometryParticles<float, 3>> It = ParticleView.Begin(), ItEnd = ParticleView.End();
			It != ItEnd; ++It)
		{
			const Chaos::TTransientGeometryParticleHandle<float, 3>* Handle = &(*It);
			Handles.Add(GetHandleHelper(const_cast<Chaos::TTransientGeometryParticleHandle<float, 3>*>(Handle)));
		}
	}
}

template <typename Traits>
void FPerSolverFieldSystem::GetFilteredParticleHandles(
	TArray<Chaos::TGeometryParticleHandle<float, 3>*>& Handles,
	const Chaos::TPBDRigidsSolver<Traits>* RigidSolver,
	const EFieldFilterType FilterType)
{
	Handles.SetNum(0, false);
	const Chaos::TPBDRigidsSOAs<float, 3>& SolverParticles = RigidSolver->GetParticles();

	if (FilterType == EFieldFilterType::Field_Filter_Dynamic)
	{
		const Chaos::TParticleView<Chaos::TPBDRigidParticles<float, 3>>& ParticleView =
			SolverParticles.GetNonDisabledDynamicView();
		Handles.Reserve(ParticleView.Num());

		for (Chaos::TParticleIterator<Chaos::TPBDRigidParticles<float, 3>> It = ParticleView.Begin(), ItEnd = ParticleView.End();
			It != ItEnd; ++It)
		{
			const Chaos::TTransientGeometryParticleHandle<float, 3>* Handle = &(*It);
			Handles.Add(GetHandleHelper(const_cast<Chaos::TTransientGeometryParticleHandle<float, 3>*>(Handle)));
		}
	}
	else if (FilterType == EFieldFilterType::Field_Filter_Static)
	{
		const Chaos::TParticleView<Chaos::TGeometryParticles<float, 3>>& ParticleView =
			SolverParticles.GetActiveStaticParticlesView();
		Handles.Reserve(ParticleView.Num());

		for (Chaos::TParticleIterator<Chaos::TGeometryParticles<float, 3>> It = ParticleView.Begin(), ItEnd = ParticleView.End();
			It != ItEnd; ++It)
		{
			const Chaos::TTransientGeometryParticleHandle<float, 3>* Handle = &(*It);
			Handles.Add(GetHandleHelper(const_cast<Chaos::TTransientGeometryParticleHandle<float, 3>*>(Handle)));
		}
	}
	else if (FilterType == EFieldFilterType::Field_Filter_Kinematic)
	{
		const Chaos::TParticleView<Chaos::TKinematicGeometryParticles<float, 3>>& ParticleView =
			SolverParticles.GetActiveKinematicParticlesView();
		Handles.Reserve(ParticleView.Num());

		for (Chaos::TParticleIterator<Chaos::TKinematicGeometryParticles<float, 3>> It = ParticleView.Begin(), ItEnd = ParticleView.End();
			It != ItEnd; ++It)
		{
			const Chaos::TTransientGeometryParticleHandle<float, 3>* Handle = &(*It);
			Handles.Add(GetHandleHelper(const_cast<Chaos::TTransientGeometryParticleHandle<float, 3>*>(Handle)));
		}
	}
	else if (FilterType == EFieldFilterType::Field_Filter_All)
	{
		const Chaos::TParticleView<Chaos::TGeometryParticles<float, 3>>& ParticleView =
			SolverParticles.GetAllParticlesView();
		Handles.Reserve(ParticleView.Num());

		for (Chaos::TParticleIterator<Chaos::TGeometryParticles<float, 3>> It = ParticleView.Begin(), ItEnd = ParticleView.End();
			It != ItEnd; ++It)
		{
			const Chaos::TTransientGeometryParticleHandle<float, 3>* Handle = &(*It);
			Handles.Add(GetHandleHelper(const_cast<Chaos::TTransientGeometryParticleHandle<float, 3>*>(Handle)));
		}
	}
}

#define EVOLUTION_TRAIT(Traits)\
template void FPerSolverFieldSystem::FieldParameterUpdateCallback(\
		Chaos::TPBDRigidsSolver<Chaos::Traits>* InSolver, \
		Chaos::TPBDPositionConstraints<float, 3>& PositionTarget, \
		TMap<int32, int32>& TargetedParticles);\
\
template void FPerSolverFieldSystem::FieldForcesUpdateCallback(\
		Chaos::TPBDRigidsSolver<Chaos::Traits>* InSolver);\
\
template void FPerSolverFieldSystem::GetRelevantParticleHandles(\
		TArray<Chaos::TGeometryParticleHandle<float,3>*>& Handles,\
		const Chaos::TPBDRigidsSolver<Chaos::Traits>* RigidSolver,\
		const EFieldResolutionType ResolutionType);\
\
template void FPerSolverFieldSystem::GetFilteredParticleHandles(\
		TArray<Chaos::TGeometryParticleHandle<float,3>*>& Handles,\
		const Chaos::TPBDRigidsSolver<Chaos::Traits>* RigidSolver,\
		const EFieldFilterType FilterType);\

#include "Chaos/EvolutionTraits.inl"
#undef EVOLUTION_TRAIT
