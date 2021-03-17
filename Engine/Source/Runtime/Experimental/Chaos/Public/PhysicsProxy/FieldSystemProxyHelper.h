// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Field/FieldSystem.h"
#include "Chaos/Array.h"
#include "Chaos/ConstraintHandle.h"
#include "Chaos/ParticleHandle.h"
#include "Chaos/Defines.h"
#include "Chaos/Particles.h"

namespace Chaos
{
	/**
	 * Build the sample points positions and indices based on the resolution and filter type 
	 * @param    LocalProxy Physics proxy from which to extract the particle handles
	 * @param    RigidSolver Rigid solver owning the particles
	 * @param    FieldCommand Field command used to extract the resolution and filter meta data
	 * @param    ParticleHandles List of particle handles extracted from the field command meta data
	 * @param    SamplePositions Positions of the extracted sample points
	 * @param    SampleIndices Indices of the extracted sample points
	 * @param    PrevResolutionType Resolution of the previous command
	 * @param    PrevFilterType Filter of the previous command
	 */
	template <typename PhysicsProxy>
	FORCEINLINE bool BuildFieldSamplePoints(
		PhysicsProxy* LocalProxy,
		Chaos::FPBDRigidsSolver* RigidSolver,
		const FFieldSystemCommand& FieldCommand, 
		TArray<Chaos::FGeometryParticleHandle*>& ParticleHandles,
		TArray<FVector>& SamplePositions,
		TArray<FFieldContextIndex>& SampleIndices,
		EFieldResolutionType& PrevResolutionType, EFieldFilterType& PrevFilterType)
	{
		const EFieldResolutionType ResolutionType =
			FieldCommand.HasMetaData(FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution) ?
			FieldCommand.GetMetaDataAs<FFieldSystemMetaDataProcessingResolution>(
				FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution)->ProcessingResolution :
			EFieldResolutionType::Field_Resolution_Minimal;

		const EFieldFilterType FilterType =
			FieldCommand.HasMetaData(FFieldSystemMetaData::EMetaType::ECommandData_Filter) ?
			FieldCommand.GetMetaDataAs<FFieldSystemMetaDataFilter>(
				FFieldSystemMetaData::EMetaType::ECommandData_Filter)->FilterType :
			EFieldFilterType::Field_Filter_Max;

		if (LocalProxy && ( (PrevResolutionType != ResolutionType) || (PrevFilterType != FilterType) || ParticleHandles.Num() == 0))
		{
			if (FilterType != EFieldFilterType::Field_Filter_Max)
			{
				LocalProxy->GetFilteredParticleHandles(ParticleHandles, RigidSolver, FilterType);
			}
			else
			{
				LocalProxy->GetRelevantParticleHandles(ParticleHandles, RigidSolver, ResolutionType);
			}

			PrevResolutionType = ResolutionType;
			PrevFilterType = FilterType;

			SamplePositions.SetNum(ParticleHandles.Num());
			SampleIndices.SetNum(ParticleHandles.Num());

			for (int32 Idx = 0; Idx < ParticleHandles.Num(); ++Idx)
			{
				SamplePositions[Idx] = ParticleHandles[Idx]->X();
				SampleIndices[Idx] = FFieldContextIndex(Idx, Idx);
			}
		}
		return ParticleHandles.Num() > 0;
	}

	/**
	 * Init the dynamics state of the particle handles to be processed by the field
	 * @param    ParticleHandles Particle hadles that will be used to init the dynamic state
	 * @param    FieldContext Field context to retrieve the evaluated samples
	 * @param    LocalResults Array to store the dynamic state
	 */
	FORCEINLINE void InitDynamicStateResults(const TArray<Chaos::FGeometryParticleHandle*>& ParticleHandles, FFieldContext& FieldContext, TArray<int32>& LocalResults)
	{
		for (const FFieldContextIndex& Index : FieldContext.GetEvaluatedSamples())
		{
			const Chaos::EObjectStateType InitState = (ParticleHandles[Index.Sample]->ObjectState() != Chaos::EObjectStateType::Uninitialized) ?
				ParticleHandles[Index.Sample]->ObjectState() : Chaos::EObjectStateType::Dynamic;
			LocalResults[Index.Result] = static_cast<int32>(InitState);
		}
	}

	/**
	 * Init the enable/disable boolean array of the particle handles to be processed by the field
	 * @param    ParticleHandles Particle hadles that will be used to init the enable/disable booleans
	 * @param    FieldContext Field context to retrieve the evaluated samples
	 * @param    LocalResults Array to store the enable/disable boolean
	 */
	FORCEINLINE void InitActivateDisabledResults(const TArray<Chaos::FGeometryParticleHandle*>& ParticleHandles, FFieldContext& FieldContext, TArray<int32>& LocalResults)
	{
		for (const FFieldContextIndex& Index : FieldContext.GetEvaluatedSamples())
		{
			Chaos::FPBDRigidParticleHandle* RigidHandle = ParticleHandles[Index.Sample]->CastToRigidParticle();
			if (RigidHandle)
			{
				LocalResults[Index.Result] = RigidHandle->Disabled();
			}
		}
	}

	/**
	 * Set the dynamic state of a particle handle
	 * @param    Rigidsolver Rigid solver owning the particle handle
	 * @param    FieldState Field state that will be set on the handle
	 * @param    RigidHandle Particle hadle on which the state will be set
	 */
	FORCEINLINE void SetParticleDynamicState(Chaos::FPBDRigidsSolver* RigidSolver,
		const Chaos::EObjectStateType FieldState, Chaos::FPBDRigidParticleHandle* RigidHandle)
	{
		const bool bIsGC = (RigidHandle->GetParticleType() == Chaos::EParticleType::GeometryCollection) ||
			(RigidHandle->GetParticleType() == Chaos::EParticleType::Clustered && !RigidHandle->CastToClustered()->InternalCluster());

		if (!bIsGC)
		{
			RigidSolver->GetEvolution()->SetParticleObjectState(RigidHandle, FieldState);
		}
		else
		{
			RigidHandle->SetObjectStateLowLevel(FieldState);
		}
	}

	/**
	 * Report the dynamic state result onto the handle
	 * @param    Rigidsolver Rigid solver owning the particle handle
	 * @param    FieldState Field state that will be set on the handle
	 * @param    RigidHandle Particle hadle on which the state will be set
	 * @param    HasInitialLinearVelocity Boolean to check if we have to set the initial linear velocity 
	 * @param    InitialLinearVelocity Initial linear velocity to potentially set onto he handle
	 * @param    HasInitialAngularVelocity Boolean to check if we have to set the initial angular velocity 
	 * @param    InitialAngularVelocity Initial angular velocity to potentially set onto he handle
	 */
	FORCEINLINE bool ReportDynamicStateResult(Chaos::FPBDRigidsSolver* RigidSolver,
		const Chaos::EObjectStateType FieldState, Chaos::FPBDRigidParticleHandle* RigidHandle,
		const bool HasInitialLinearVelocity, const Chaos::FVec3& InitialLinearVelocity,
		const bool HasInitialAngularVelocity, const Chaos::FVec3& InitialAngularVelocity)
	{
		const Chaos::EObjectStateType HandleState = RigidHandle->ObjectState();

		// Do we need to be sure the mass > 0 only for the dynamic state
		const bool bHasStateChanged = ((FieldState != Chaos::EObjectStateType::Dynamic) ||
			(FieldState == Chaos::EObjectStateType::Dynamic && RigidHandle->M() > FLT_EPSILON)) && (HandleState != FieldState);

		if (bHasStateChanged)
		{
			SetParticleDynamicState(RigidSolver, FieldState, RigidHandle);

			if (FieldState == Chaos::EObjectStateType::Kinematic || FieldState == Chaos::EObjectStateType::Static)
			{
				RigidHandle->SetV(Chaos::FVec3(0));
				RigidHandle->SetW(Chaos::FVec3(0));
			}
			else if (FieldState == Chaos::EObjectStateType::Dynamic)
			{
				if (HasInitialLinearVelocity)
				{
					RigidHandle->SetV(InitialLinearVelocity);
				}
				if (HasInitialAngularVelocity)
				{
					RigidHandle->SetW(InitialAngularVelocity);
				}
			}
		}
		return bHasStateChanged;
	}

	/**
	 * Update all the clustered particles object state to static/kinematic if one of its children state has been changed to static/kinematic
	 * @param    Rigidsolver Rigid solver owning the particle handle
	 * @param    bHasStateChanged Boolean to check before updating the handle state
	 */
	FORCEINLINE void UpdateSolverParticlesState(Chaos::FPBDRigidsSolver* RigidSolver, const bool bHasStateChanged)
	{
		if (bHasStateChanged)
		{
			RigidSolver->GetParticles().UpdateGeometryCollectionViews(true);

			const Chaos::FPBDRigidsSOAs& SolverParticles = RigidSolver->GetParticles();
			auto& Clustering = RigidSolver->GetEvolution()->GetRigidClustering();

			const Chaos::TParticleView<Chaos::FGeometryParticles>& ParticleView =
				SolverParticles.GetNonDisabledView();

			for (Chaos::TParticleIterator<Chaos::FGeometryParticles> It = ParticleView.Begin(), ItEnd = ParticleView.End();
				It != ItEnd; ++It)
			{
				const auto* Clustered = It->Handle()->CastToClustered();
				if (Clustered && Clustered->ClusterIds().NumChildren)
				{
					Chaos::FPBDRigidParticleHandle* RigidHandle = It->Handle()->CastToRigidParticle();
					check(RigidHandle);
					Clustering.UpdateKinematicProperties(RigidHandle);
				}
			}
		}
	}

	/**
	 * Update the solver breaking model based on external strain
	 * @param    Rigidsolver Rigid solver owning the breaking model
	 * @param    ExternalStrain Strain to be used to update the breaking model
	 */
	FORCEINLINE void UpdateSolverBreakingModel(Chaos::FPBDRigidsSolver* RigidSolver, TMap<Chaos::FGeometryParticleHandle*, float>& ExternalStrain)
	{
		// Capture the results from the breaking model to post-process
		TMap<Chaos::FPBDRigidClusteredParticleHandle*, TSet<Chaos::FPBDRigidParticleHandle*>> BreakResults =
			RigidSolver->GetEvolution()->GetRigidClustering().BreakingModel(&ExternalStrain);

		// If clusters broke apart then we'll have activated new particles that have no relationship to the proxy that now owns them
		// Here we attach each new particle to the proxy of the parent particle that owns it.
		for (const TPair<Chaos::TPBDRigidClusteredParticleHandle<Chaos::FReal, 3>*, TSet<Chaos::FPBDRigidParticleHandle*>> & Iter : BreakResults)
		{
			const TSet<Chaos::FPBDRigidParticleHandle*>& Activated = Iter.Value;

			for (Chaos::FPBDRigidParticleHandle* Handle : Activated)
			{
				if (!RigidSolver->GetProxies(Handle))
				{
					const TSet<IPhysicsProxyBase*>* ParentProxies = RigidSolver->GetProxies(Iter.Key);
					if (ensure(ParentProxies))
					{
						for (IPhysicsProxyBase* ParentProxy : *ParentProxies)
							RigidSolver->AddParticleToProxy(Handle, ParentProxy);
					}
				}
			}
		}
	}

	/**
	 * Update the handle sleeping linear and angular theshold
	 * @param    Rigidsolver Rigid solver owning the particle handle
	 * @param    RigidHandle Particle handle on which the threshold will be updated
	 * @param    ResultThreshold Threshoild to be set onto the handle
	 */
	FORCEINLINE void UpdateMaterialSleepingThreshold(Chaos::FPBDRigidsSolver* RigidSolver, Chaos::FPBDRigidParticleHandle* RigidHandle, const float ResultThreshold)
	{
		// if no per particle physics material is set, make one
		if (!RigidSolver->GetEvolution()->GetPerParticlePhysicsMaterial(RigidHandle).IsValid())
		{
			TUniquePtr<Chaos::FChaosPhysicsMaterial> NewMaterial = MakeUnique< Chaos::FChaosPhysicsMaterial>();
			NewMaterial->SleepingLinearThreshold = ResultThreshold;
			NewMaterial->SleepingAngularThreshold = ResultThreshold;

			RigidSolver->GetEvolution()->SetPhysicsMaterial(RigidHandle, MakeSerializable(NewMaterial));
			RigidSolver->GetEvolution()->SetPerParticlePhysicsMaterial(RigidHandle, NewMaterial);
		}
		else
		{
			const TUniquePtr<Chaos::FChaosPhysicsMaterial>& InstanceMaterial = RigidSolver->GetEvolution()->GetPerParticlePhysicsMaterial(RigidHandle);

			if (ResultThreshold != InstanceMaterial->DisabledLinearThreshold)
			{
				InstanceMaterial->SleepingLinearThreshold = ResultThreshold;
				InstanceMaterial->SleepingAngularThreshold = ResultThreshold;
			}
		}
	}

	/**
	 * Update the handle disable linear and angular theshold
	 * @param    Rigidsolver Rigid solver owning the particle handle
	 * @param    RigidHandle Particle handle on which the threshold will be updated
	 * @param    ResultThreshold Threshoild to be set onto the handle
	 */
	FORCEINLINE void UpdateMaterialDisableThreshold(Chaos::FPBDRigidsSolver* RigidSolver, Chaos::FPBDRigidParticleHandle* RigidHandle, const float ResultThreshold)
	{
		// if no per particle physics material is set, make one
		if (!RigidSolver->GetEvolution()->GetPerParticlePhysicsMaterial(RigidHandle).IsValid())
		{
			TUniquePtr<Chaos::FChaosPhysicsMaterial> NewMaterial = MakeUnique< Chaos::FChaosPhysicsMaterial>();
			NewMaterial->DisabledLinearThreshold = ResultThreshold;
			NewMaterial->DisabledAngularThreshold = ResultThreshold;

			RigidSolver->GetEvolution()->SetPhysicsMaterial(RigidHandle, MakeSerializable(NewMaterial));
			RigidSolver->GetEvolution()->SetPerParticlePhysicsMaterial(RigidHandle, NewMaterial);
		}
		else
		{
			const TUniquePtr<Chaos::FChaosPhysicsMaterial>& InstanceMaterial = RigidSolver->GetEvolution()->GetPerParticlePhysicsMaterial(RigidHandle);

			if (ResultThreshold != InstanceMaterial->DisabledLinearThreshold)
			{
				InstanceMaterial->DisabledLinearThreshold = ResultThreshold;
				InstanceMaterial->DisabledAngularThreshold = ResultThreshold;
			}
		}
	}

	/**
	 * Update the particle handles integer parameters based on the field evaluation
	 * @param    RigidSolver Rigid solver owning the particles
	 * @param    FieldCommand Field command to be used for the parameter field evaluatation
	 * @param    ParticleHandles List of particle handles extracted from the field command meta data
	 * @param	 FieldContext Field context that will be used for field evaluation
	 * @param    CommandsToRemove List of commands that will be removed after evaluation
	 * @param    PositionTarget Chaos position contraint in which each target will be added
	 * @param    TargetedParticles List of particles (source/target) that will be filled by the PositionTarget/Static parameter 
	 * @param    CommandIndex Command index that we are evaluating
	 */
	FORCEINLINE void FieldIntegerParameterUpdate(Chaos::FPBDRigidsSolver* RigidSolver, const FFieldSystemCommand& FieldCommand,
		TArray<Chaos::FGeometryParticleHandle*>& ParticleHandles, FFieldContext& FieldContext, TArray<int32>& CommandsToRemove,
		Chaos::FPBDPositionConstraints& PositionTarget,
		TMap<int32, int32>& TargetedParticles, const int32 CommandIndex)
	{
		TArray<int32> LocalResults;
		LocalResults.AddZeroed(ParticleHandles.Num());
		TArrayView<int32> ResultsView(&(LocalResults[0]), LocalResults.Num());

		if (FieldCommand.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_DynamicState))
		{
			SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_DynamicState);
			{
				bool bHasStateChanged = false;
				InitDynamicStateResults(ParticleHandles, FieldContext, LocalResults);

				static_cast<const FFieldNode<int32>*>(FieldCommand.RootNode.Get())->Evaluate(FieldContext, ResultsView);
				for (const FFieldContextIndex& Index : FieldContext.GetEvaluatedSamples())
				{
					Chaos::FPBDRigidParticleHandle* RigidHandle = ParticleHandles[Index.Sample]->CastToRigidParticle();
					if (RigidHandle)
					{
						const int8 ResultState = ResultsView[Index.Result];
						bHasStateChanged |= ReportDynamicStateResult(RigidSolver, static_cast<Chaos::EObjectStateType>(ResultState), RigidHandle,
							false, Chaos::FVec3(0), false, Chaos::FVec3(0));
					}
				}
				UpdateSolverParticlesState(RigidSolver, bHasStateChanged);
			}
			CommandsToRemove.Add(CommandIndex);
		}
		else if (FieldCommand.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_ActivateDisabled))
		{
			SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_ActivateDisabled);
			{
				InitActivateDisabledResults(ParticleHandles, FieldContext, LocalResults);

				static_cast<const FFieldNode<int32>*>(FieldCommand.RootNode.Get())->Evaluate(FieldContext, ResultsView);
				for (const FFieldContextIndex& Index : FieldContext.GetEvaluatedSamples())
				{
					Chaos::FPBDRigidParticleHandle* RigidHandle = ParticleHandles[Index.Sample]->CastToRigidParticle();
					if (RigidHandle && RigidHandle->Disabled() && ResultsView[Index.Result] == 0)
					{
						RigidSolver->GetEvolution()->EnableParticle(RigidHandle, nullptr);
						SetParticleDynamicState(RigidSolver, Chaos::EObjectStateType::Dynamic, RigidHandle);
					}
				}
			}
			CommandsToRemove.Add(CommandIndex);
		}
		else if (FieldCommand.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_CollisionGroup))
		{
			SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_CollisionGroup);
			{
				static_cast<const FFieldNode<int32>*>(FieldCommand.RootNode.Get())->Evaluate(FieldContext, ResultsView);
				for (const FFieldContextIndex& Index : FieldContext.GetEvaluatedSamples())
				{
					Chaos::FPBDRigidClusteredParticleHandle* RigidHandle = ParticleHandles[Index.Sample]->CastToClustered();
					if (RigidHandle)
					{
						RigidHandle->SetCollisionGroup(ResultsView[Index.Result]);
					}
				}
			}
			CommandsToRemove.Add(CommandIndex);
		}
		else if (FieldCommand.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_PositionStatic))
		{
			SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_PositionStatic);
			{
				static_cast<const FFieldNode<int32>*>(FieldCommand.RootNode.Get())->Evaluate(FieldContext, ResultsView);
				for (const FFieldContextIndex& Index : FieldContext.GetEvaluatedSamples())
				{
					Chaos::FPBDRigidClusteredParticleHandle* RigidHandle = ParticleHandles[Index.Sample]->CastToClustered();
					if (RigidHandle && ResultsView[Index.Result])
					{
						if (TargetedParticles.Contains(Index.Sample))
						{
							const int32 ConstraintIndex = TargetedParticles[Index.Sample];
							PositionTarget.Replace(ConstraintIndex, ParticleHandles[Index.Sample]->X());
						}
						else
						{
							const int32 ConstraintIndex = PositionTarget.NumConstraints();
							PositionTarget.AddConstraint(RigidHandle, RigidHandle->X());
							TargetedParticles.Add(Index.Sample, ConstraintIndex);
						}
					}
				}
			}
			CommandsToRemove.Add(CommandIndex);
		}
		else if (FieldCommand.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_DynamicConstraint))
		{
			SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_PositionStatic);
			{
				UE_LOG(LogChaos, Error, TEXT("Dynamic constraint target currently not supported by chaos"));
			}
			CommandsToRemove.Add(CommandIndex);
		}
	}

	/**
	 * Update the particle handles scalar parameters based on the field evaluation
	 * @param    RigidSolver Rigid solver owning the particles
	 * @param    FieldCommand Field command to be used for the parameter field evaluatation
	 * @param    ParticleHandles List of particle handles extracted from the field command meta data
	 * @param	 FieldContext Field context that will be used for field evaluation
	 * @param    CommandsToRemove List of commands that will be removed after evaluation
	 * @param    PositionTarget Chaos position contraint in which each target will be added
	 * @param    TargetedParticles List of particles (source/target) that will be filled by the PositionTarget/Static parameter
	 * @param    CommandIndex Command index that we are evaluating
	 */
	FORCEINLINE void FieldScalarParameterUpdate(Chaos::FPBDRigidsSolver* RigidSolver, const FFieldSystemCommand& FieldCommand,
		TArray<Chaos::FGeometryParticleHandle*>& ParticleHandles, FFieldContext& FieldContext, TArray<int32>& CommandsToRemove,
		Chaos::FPBDPositionConstraints& PositionTarget,
		TMap<int32, int32>& TargetedParticles, const int32 CommandIndex)
	{
		TArray<float> LocalResults;
		LocalResults.AddZeroed(ParticleHandles.Num());
		TArrayView<float> ResultsView(&(LocalResults[0]), LocalResults.Num());

		if (FieldCommand.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_ExternalClusterStrain))
		{
			SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_ExternalClusterStrain);
			{
				TMap<Chaos::FGeometryParticleHandle*, float> ExternalStrain;

				static_cast<const FFieldNode<float>*>(FieldCommand.RootNode.Get())->Evaluate(FieldContext, ResultsView);
				for (const FFieldContextIndex& Index : FieldContext.GetEvaluatedSamples())
				{
					if (ResultsView[Index.Result] > 0)
					{
						ExternalStrain.Add(ParticleHandles[Index.Sample], ResultsView[Index.Result]);
					}
				}
				UpdateSolverBreakingModel(RigidSolver, ExternalStrain);
			}
			CommandsToRemove.Add(CommandIndex);
		}
		else if (FieldCommand.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_Kill))
		{
			SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_Kill);
			{
				static_cast<const FFieldNode<float>*>(FieldCommand.RootNode.Get())->Evaluate(FieldContext, ResultsView);
				for (const FFieldContextIndex& Index : FieldContext.GetEvaluatedSamples())
				{
					Chaos::FPBDRigidParticleHandle* RigidHandle = ParticleHandles[Index.Sample]->CastToRigidParticle();
					if (RigidHandle && ResultsView[Index.Result] > 0.0)
					{
						RigidSolver->GetEvolution()->DisableParticle(RigidHandle);
					}
				}
			}
			CommandsToRemove.Add(CommandIndex);
		}
		else if (FieldCommand.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_SleepingThreshold))
		{
			SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_SleepingThreshold);
			{
				static_cast<const FFieldNode<float>*>(FieldCommand.RootNode.Get())->Evaluate(FieldContext, ResultsView);
				for (const FFieldContextIndex& Index : FieldContext.GetEvaluatedSamples())
				{
					Chaos::FPBDRigidParticleHandle* RigidHandle = ParticleHandles[Index.Sample]->CastToRigidParticle();
					if (RigidHandle && ResultsView.Num() > 0)
					{
						UpdateMaterialSleepingThreshold(RigidSolver, RigidHandle, ResultsView[Index.Result]);
					}
				}
			}
			CommandsToRemove.Add(CommandIndex);
		}
		else if (FieldCommand.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_DisableThreshold))
		{
			SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_DisableThreshold);
			{
				static_cast<const FFieldNode<float>*>(FieldCommand.RootNode.Get())->Evaluate(FieldContext, ResultsView);
				for (const FFieldContextIndex& Index : FieldContext.GetEvaluatedSamples())
				{
					Chaos::FPBDRigidParticleHandle* RigidHandle = ParticleHandles[Index.Sample]->CastToRigidParticle();

					if (RigidHandle && RigidHandle->ObjectState() == Chaos::EObjectStateType::Dynamic && ResultsView.Num() > 0)
					{
						UpdateMaterialDisableThreshold(RigidSolver, RigidHandle, ResultsView[Index.Result]);
					}
				}
			}
			CommandsToRemove.Add(CommandIndex);
		}
		else if (FieldCommand.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_InternalClusterStrain))
		{
			SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_InternalClusterStrain);
			{
				static_cast<const FFieldNode<float>*>(FieldCommand.RootNode.Get())->Evaluate(FieldContext, ResultsView);
				for (const FFieldContextIndex& Index : FieldContext.GetEvaluatedSamples())
				{
					Chaos::FPBDRigidClusteredParticleHandle* RigidHandle = ParticleHandles[Index.Sample]->CastToClustered();
					if (RigidHandle && RigidHandle->ObjectState() == Chaos::EObjectStateType::Dynamic)
					{
						RigidHandle->Strain() += ResultsView[Index.Result];
					}
				}
			}
			CommandsToRemove.Add(CommandIndex);
		}
	}

	/**
	 * Update the particle handles vector parameters based on the field evaluation
	 * @param    RigidSolver Rigid solver owning the particles
	 * @param    FieldCommand Field command to be used for the parameter field evaluatation
	 * @param    ParticleHandles List of particle handles extracted from the field command meta data
	 * @param	 FieldContext Field context that will be used for field evaluation
	 * @param    CommandsToRemove List of commands that will be removed after evaluation
	 * @param    PositionTarget Chaos position contraint in which each target will be added
	 * @param    TargetedParticles List of particles (source/target) that will be filled by the PositionTarget/Static parameter
	 * @param    CommandIndex Command index that we are evaluating
	 */
	FORCEINLINE void FieldVectorParameterUpdate(Chaos::FPBDRigidsSolver* RigidSolver, const FFieldSystemCommand& FieldCommand,
		TArray<Chaos::FGeometryParticleHandle*>& ParticleHandles, FFieldContext& FieldContext, TArray<int32>& CommandsToRemove,
		Chaos::FPBDPositionConstraints& PositionTarget,
		TMap<int32, int32>& TargetedParticles, const int32 CommandIndex)
	{
		TArray<FVector> LocalResults;
		LocalResults.AddZeroed(ParticleHandles.Num());
		TArrayView<FVector> ResultsView(&(LocalResults[0]), LocalResults.Num());

		if (FieldCommand.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_LinearVelocity))
		{
			SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_LinearVelocity);
			{
				static_cast<const FFieldNode<FVector>*>(FieldCommand.RootNode.Get())->Evaluate(FieldContext, ResultsView);
				for (const FFieldContextIndex& Index : FieldContext.GetEvaluatedSamples())
				{
					Chaos::FPBDRigidParticleHandle* RigidHandle = ParticleHandles[Index.Sample]->CastToRigidParticle();
					if (RigidHandle && RigidHandle->ObjectState() == Chaos::EObjectStateType::Dynamic)
					{
						RigidHandle->V() += ResultsView[Index.Result];
					}
				}
			}
			CommandsToRemove.Add(CommandIndex);
		}
		else if (FieldCommand.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_AngularVelociy))
		{
			SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_AngularVelocity);
			{
				static_cast<const FFieldNode<FVector>*>(FieldCommand.RootNode.Get())->Evaluate(FieldContext, ResultsView);

				for (const FFieldContextIndex& Index : FieldContext.GetEvaluatedSamples())
				{
					Chaos::FPBDRigidParticleHandle* RigidHandle = ParticleHandles[Index.Sample]->CastToRigidParticle();
					if (RigidHandle && RigidHandle->ObjectState() == Chaos::EObjectStateType::Dynamic)
					{
						RigidHandle->W() += ResultsView[Index.Result];
					}
				}
			}
			CommandsToRemove.Add(CommandIndex);
		}
		else if (FieldCommand.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_PositionTarget))
		{
			SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_PositionTarget);
			{
				static_cast<const FFieldNode<FVector>*>(FieldCommand.RootNode.Get())->Evaluate(FieldContext, ResultsView);
				for (const FFieldContextIndex& Index : FieldContext.GetEvaluatedSamples())
				{
					Chaos::FPBDRigidClusteredParticleHandle* RigidHandle = ParticleHandles[Index.Sample]->CastToClustered();
					if (RigidHandle && ResultsView[Index.Result] != FVector(FLT_MAX))
					{
						if (TargetedParticles.Contains(Index.Sample))
						{
							const int32 ConstraintIndex = TargetedParticles[Index.Sample];
							PositionTarget.Replace(ConstraintIndex, ResultsView[Index.Result]);
						}
						else
						{
							const int32 ConstraintIndex = PositionTarget.NumConstraints();
							PositionTarget.AddConstraint(RigidHandle, ResultsView[Index.Result]);
							TargetedParticles.Add(Index.Sample, ConstraintIndex);
						}
					}
				}
			}
			CommandsToRemove.Add(CommandIndex);
		}
		else if (FieldCommand.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_PositionAnimated))
		{
			SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_PositionAnimated);
			{
				UE_LOG(LogChaos, Error, TEXT("Position Animated target currently not supported by chaos"));
			}
			CommandsToRemove.Add(CommandIndex);
		}
	}


	FORCEINLINE void FieldVectorForceUpdate(Chaos::FPBDRigidsSolver* RigidSolver, const FFieldSystemCommand& FieldCommand,
		TArray<Chaos::FGeometryParticleHandle*>& ParticleHandles, FFieldContext& FieldContext, TArray<int32>& CommandsToRemove,
		const int32 CommandIndex)
	{
		TArray<FVector> LocalResults;
		LocalResults.AddZeroed(ParticleHandles.Num());
		TArrayView<FVector> ResultsView(&(LocalResults[0]), LocalResults.Num());

		static_cast<const FFieldNode<FVector>*>(FieldCommand.RootNode.Get())->Evaluate(FieldContext, ResultsView);

		if (FieldCommand.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_LinearForce))
		{
			SCOPE_CYCLE_COUNTER(STAT_ForceUpdateField_LinearForce);
			{
				for (const FFieldContextIndex& Index : FieldContext.GetEvaluatedSamples())
				{
					Chaos::FPBDRigidParticleHandle* RigidHandle = ParticleHandles[Index.Sample]->CastToRigidParticle();
					if (RigidHandle && !RigidHandle->Disabled() && (RigidHandle->ObjectState() == Chaos::EObjectStateType::Dynamic || RigidHandle->ObjectState() == Chaos::EObjectStateType::Sleeping))
					{
						if (RigidHandle->Sleeping())
						{
							RigidHandle->SetObjectStateLowLevel(Chaos::EObjectStateType::Dynamic);
						}
						RigidHandle->F() += ResultsView[Index.Result];
					}
				}
			}
			CommandsToRemove.Add(CommandIndex);
		}
		else if (FieldCommand.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_AngularTorque))
		{
			SCOPE_CYCLE_COUNTER(STAT_ForceUpdateField_AngularTorque);
			{
				for (const FFieldContextIndex& Index : FieldContext.GetEvaluatedSamples())
				{
					Chaos::FPBDRigidParticleHandle* RigidHandle = ParticleHandles[Index.Sample]->CastToRigidParticle();
					if (RigidHandle && (RigidHandle->ObjectState() == Chaos::EObjectStateType::Dynamic || RigidHandle->ObjectState() == Chaos::EObjectStateType::Sleeping))
					{
						if (RigidHandle->Sleeping())
						{
							RigidHandle->SetObjectStateLowLevel(Chaos::EObjectStateType::Dynamic);
						}
						RigidHandle->Torque() += ResultsView[Index.Result];
					}
				}
			}
			CommandsToRemove.Add(CommandIndex);
		}
	}
}
	
