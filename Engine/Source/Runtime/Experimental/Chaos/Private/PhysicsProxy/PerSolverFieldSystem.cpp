// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsProxy/PerSolverFieldSystem.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "Field/FieldSystem.h"
#include "Chaos/PBDRigidClustering.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"
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
	Chaos::TPBDRigidsSolver<Traits>* InSolver,
	Chaos::TPBDRigidParticles<float, 3>& Particles,
	Chaos::TArrayCollectionArray<float>& Strains,
	Chaos::TPBDPositionConstraints<float, 3>& PositionTarget,
	TMap<int32, int32>& PositionTargetedParticles,
	//const TArray<FKinematicProxy>& AnimatedPosition, 
	TArray<FFieldSystemCommand>& Commands, const bool IsTransient)
{
	using namespace Chaos;
	SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_Object);

	const int32 NumCommands = Commands.Num();
	if (NumCommands && InSolver)
	{
		Chaos::TPBDRigidsSolver<Traits>* CurrentSolver = InSolver;

		TArray<int32> CommandsToRemove;
		CommandsToRemove.Reserve(NumCommands);

		// @todo: This seems like a waste if we just want to get everything
		//TArray<ContextIndex> IndicesArray;
		TArray<Chaos::TGeometryParticleHandle<float, 3>*> Handles;
		TArray<FVector> SamplePoints;
		TArray<ContextIndex> SampleIndices;
		TArray<ContextIndex>& IndicesArray = SampleIndices; // Do away with!
		EFieldResolutionType PrevResolutionType = EFieldResolutionType::Field_Resolution_Max;
		EFieldFilterType PrevFilterType = EFieldFilterType::Field_Filter_Max;
		for (int32 CommandIndex = 0; CommandIndex < NumCommands; CommandIndex++)
		{
			const FFieldSystemCommand& Command = Commands[CommandIndex];
			const float TimeSeconds = InSolver->GetSolverTime() - Command.TimeCreation;

			const EFieldResolutionType ResolutionType =
				Command.HasMetaData(FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution) ?
				Command.GetMetaDataAs<FFieldSystemMetaDataProcessingResolution>(
					FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution)->ProcessingResolution :
				EFieldResolutionType::Field_Resolution_Minimal;

			const EFieldFilterType FilterType =
				Command.HasMetaData(FFieldSystemMetaData::EMetaType::ECommandData_Filter) ?
				Command.GetMetaDataAs<FFieldSystemMetaDataFilter>(
					FFieldSystemMetaData::EMetaType::ECommandData_Filter)->FilterType :
				EFieldFilterType::Field_Filter_Max;

			if ((PrevResolutionType != ResolutionType) || (PrevFilterType != FilterType) || (Handles.Num() == 0))
			{
				if (FilterType != EFieldFilterType::Field_Filter_Max)
				{
					FPerSolverFieldSystem::FilterParticleHandles(Handles, CurrentSolver, FilterType);
				}
				else
				{
					FPerSolverFieldSystem::GetParticleHandles(Handles, CurrentSolver, ResolutionType);
				}

				PrevResolutionType = ResolutionType;
				PrevFilterType = FilterType;

				SamplePoints.SetNum(Handles.Num());
				SampleIndices.SetNum(Handles.Num());
				for (int32 Idx = 0; Idx < Handles.Num(); ++Idx)
				{
					SamplePoints[Idx] = Handles[Idx]->X();
					SampleIndices[Idx] = ContextIndex(Idx, Idx);
				}
			}
			if (Handles.Num())
			{
				TArrayView<FVector> SamplePointsView(&(SamplePoints[0]), SamplePoints.Num());
				TArrayView<ContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

				FFieldContext Context{
					SampleIndicesView, // @todo(chaos) important: an empty index array should evaluate everything
					SamplePointsView,
					Command.MetaData,
					TimeSeconds
				};

				const EFieldOutputType FieldOutput = GetFieldTargetOutput(GetFieldPhysicsType(Command.TargetAttribute));
				if (((FieldOutput == EFieldOutputType::Field_Output_Integer) && (Command.RootNode->Type() != FFieldNodeBase::EFieldType::EField_Int32)) ||
					((FieldOutput == EFieldOutputType::Field_Output_Vector) && (Command.RootNode->Type() != FFieldNodeBase::EFieldType::EField_FVector)) ||
					((FieldOutput == EFieldOutputType::Field_Output_Scalar) && (Command.RootNode->Type() != FFieldNodeBase::EFieldType::EField_Float)))
				{
					UE_LOG(LogChaos, Error, TEXT("Field based evaluation of the simulation %s parameter expects %s field inputs."),
						*Command.TargetAttribute.ToString(), *GetFieldOutputName(FieldOutput).ToString());
					CommandsToRemove.Add(CommandIndex);
				}
				else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_DynamicState))
				{
					SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_DynamicState);
					{
						// Sample the dynamic state array in the field

						// #BGTODO We're initializing every particle in the simulation here even though we're probably
						// going to cull it in the field eval - can probably be smarter about this.

						TArray<int32> DynamicState; // #BGTODO Enum class support (so we can size the underlying type to be more appropriate)
						DynamicState.AddUninitialized(Handles.Num());
						int32 i = 0;
						for (Chaos::TGeometryParticleHandle<float, 3> * Handle : Handles)
						{
							const Chaos::EObjectStateType ObjectState = Handle->ObjectState();
							switch (ObjectState)
							{
							case Chaos::EObjectStateType::Kinematic:
								DynamicState[i++] = (int)EObjectStateTypeEnum::Chaos_Object_Kinematic;
								break;
							case Chaos::EObjectStateType::Static:
								DynamicState[i++] = (int)EObjectStateTypeEnum::Chaos_Object_Static;
								break;
							case Chaos::EObjectStateType::Sleeping:
								DynamicState[i++] = (int)EObjectStateTypeEnum::Chaos_Object_Sleeping;
								break;
							case Chaos::EObjectStateType::Dynamic:
							case Chaos::EObjectStateType::Uninitialized:
							default:
								DynamicState[i++] = (int)EObjectStateTypeEnum::Chaos_Object_Dynamic;
								break;
							}
						}
						TArrayView<int32> DynamicStateView(&(DynamicState[0]), DynamicState.Num());
						static_cast<const FFieldNode<int32>*>(Command.RootNode.Get())->Evaluate(Context, DynamicStateView);

						bool StateChanged = false;
						for (const ContextIndex& Index : Context.GetEvaluatedSamples())
						{
							Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = Handles[Index.Sample]->CastToRigidParticle();

							// Lower level particle handles, like TGeometryParticleHandle and 
							// TKinematicParticleHandle, infer their dynamic state by whether or not
							// promotion to a derived c++ handle type succeeds or fails.
							//
							// THAT IS NOT WHAT WE WANT.
							//
							// PBDRigidParticles has an array of EObjectStateType, and the associated
							// handle has a getter and a setter for that data.  So, at least for now,
							// we're just going to ignore non-dynamic particles.  This has the added
							// benefit of not needing to deal with the floor, as it's pretty likely to
							// not be dynamic.
							if (RigidHandle)
							{
								auto SetParticleState = [CurrentSolver](Chaos::TPBDRigidParticleHandle<float, 3>* InHandle, EObjectStateType InState)
								{
									const bool bIsGC = (InHandle->GetParticleType() == Chaos::EParticleType::GeometryCollection) ||
										(InHandle->GetParticleType() == Chaos::EParticleType::Clustered && !InHandle->CastToClustered()->InternalCluster());

									if (!bIsGC)
									{
										CurrentSolver->GetEvolution()->SetParticleObjectState(InHandle, InState);
									}
									else
									{
										InHandle->SetObjectStateLowLevel(InState);
									}
								};

								const EObjectStateType HandleState = RigidHandle->ObjectState();

								const int32 FieldState = DynamicStateView[Index.Result];
								if (FieldState == (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic)
								{
									if ((HandleState == Chaos::EObjectStateType::Static ||
										HandleState == Chaos::EObjectStateType::Kinematic) &&
										RigidHandle->M() > FLT_EPSILON)
									{
										SetParticleState(RigidHandle, EObjectStateType::Dynamic);
										StateChanged = true;
									}
									else if (HandleState == Chaos::EObjectStateType::Sleeping)
									{
										SetParticleState(RigidHandle, EObjectStateType::Dynamic);
										StateChanged = true;
									}
								}
								else if (FieldState == (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic)
								{
									if (HandleState != Chaos::EObjectStateType::Kinematic)
									{
										SetParticleState(RigidHandle, EObjectStateType::Kinematic);
										RigidHandle->SetV(Chaos::FVec3(0));
										RigidHandle->SetW(Chaos::FVec3(0));
										StateChanged = true;
									}
								}
								else if (FieldState == (int32)EObjectStateTypeEnum::Chaos_Object_Static)
								{
									if (HandleState != Chaos::EObjectStateType::Static)
									{
										SetParticleState(RigidHandle, EObjectStateType::Static);
										RigidHandle->SetV(Chaos::FVec3(0));
										RigidHandle->SetW(Chaos::FVec3(0));
										StateChanged = true;
									}
								}
								else if (FieldState == (int32)EObjectStateTypeEnum::Chaos_Object_Sleeping)
								{
									if (HandleState != Chaos::EObjectStateType::Sleeping)
									{
										SetParticleState(RigidHandle, EObjectStateType::Sleeping);
										StateChanged = true;
									}
								}
							} // handle is dynamic
						} // end for all samples

						if (StateChanged)
						{
							// regenerate views
							CurrentSolver->GetParticles().UpdateGeometryCollectionViews(true);
						}

						const Chaos::TPBDRigidsSOAs<float, 3>& SolverParticles = CurrentSolver->GetParticles();
						auto& Clustering = CurrentSolver->GetEvolution()->GetRigidClustering();
						const auto& ClusterMap = Clustering.GetChildrenMap();

						const Chaos::TParticleView<Chaos::TGeometryParticles<float, 3>>& ParticleView =
							SolverParticles.GetNonDisabledView();

						for (Chaos::TParticleIterator<Chaos::TGeometryParticles<float, 3>> It = ParticleView.Begin(), ItEnd = ParticleView.End();
							It != ItEnd; ++It)
						{
							const auto* Clustered = It->Handle()->CastToClustered();
							if (Clustered && Clustered->ClusterIds().NumChildren)
							{
								Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = It->Handle()->CastToRigidParticle();
								check(RigidHandle);
								Clustering.UpdateKinematicProperties(RigidHandle);
							}
						}
					}
					CommandsToRemove.Add(CommandIndex);
				}
				else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_ActivateDisabled))
				{
					SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_ActivateDisabled);
					{
						TArray<int32> LocalResults;
						LocalResults.Init(false, Handles.Num());
						for (const ContextIndex& Index : Context.GetEvaluatedSamples())
						{
							Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = Handles[Index.Sample]->CastToRigidParticle();
							if (RigidHandle)
							{
								LocalResults[Index.Result] = RigidHandle->Disabled();
							}
						}
						TArrayView<int32> ResultsView(&(LocalResults[0]), LocalResults.Num());
						static_cast<const FFieldNode<int32>*>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

						for (const ContextIndex& Index : Context.GetEvaluatedSamples())
						{
							Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = Handles[Index.Sample]->CastToRigidParticle();
							if (RigidHandle && RigidHandle->Disabled() && ResultsView[Index.Result] == 0.0)
							{
								CurrentSolver->GetEvolution()->EnableParticle(RigidHandle, nullptr);
								CurrentSolver->GetEvolution()->SetParticleObjectState(RigidHandle, Chaos::EObjectStateType::Dynamic);
							}
						}

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
						// transfer results to rigid system.
						for (const ContextIndex& Index : Context.GetEvaluatedSamples())
						{
							int32 RigidBodyIndex = Index.Result;
							if (DynamicStateView[RigidBodyIndex] == 0 && Particles.Disabled(RigidBodyIndex))
							{
								ensure(CurrentSolver->GetRigidClustering().GetClusterIdsArray()[RigidBodyIndex].Id == INDEX_NONE);
								CurrentSolver->GetEvolution()->EnableParticle(RigidBodyIndex, INDEX_NONE);
								Particles.SetObjectState(RigidBodyIndex, Chaos::EObjectStateType::Dynamic);
							}
						}
#endif
					}
					CommandsToRemove.Add(CommandIndex);
				}
				else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_ExternalClusterStrain))
				{
					SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_ExternalClusterStrain);

					{
						// TODO: Chaos, Ryan
						// As we're allocating a buffer the size of all particles every iteration, 
						// I suspect this is a performance hit.  It seems like we should add a buffer
						// that's recycled rather than reallocating.  It could live on the particles
						// and its lifetime tied to an object that lives in the scope of the object 
						// that's driving the sampling of the field.

						TArray<float> LocalResults;
						// There's 2 ways to think about initializing this array...
						// Either we have a low number of indices, and the cost of iterating
						// over the indices in addition to StrainSamples is lower than the
						// cost of initializing them all, or it's cheaper and potentially 
						// more cache coherent to just initialize them all.  I'm thinking
						// the latter may be more likely...
						LocalResults.AddZeroed(Handles.Num());

						TArrayView<float> ResultsView(&LocalResults[0], LocalResults.Num());
						static_cast<const FFieldNode<float>*>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

						TMap<TGeometryParticleHandle<float, 3>*, float> Map;

						for (const ContextIndex& Index : Context.GetEvaluatedSamples())
						{
							if (ResultsView[Index.Result] > 0)
							{
								Map.Add(Handles[Index.Sample], ResultsView[Index.Result]);
							}
						}

						// Capture the results from the breaking model to post-process
						TMap<TPBDRigidClusteredParticleHandle<FReal, 3>*, TSet<TPBDRigidParticleHandle<FReal, 3>*>> BreakResults = CurrentSolver->GetEvolution()->GetRigidClustering().BreakingModel(&Map);

						// If clusters broke apart then we'll have activated new particles that have no relationship to the proxy that now owns them
						// Here we attach each new particle to the proxy of the parent particle that owns it.
						for (const TPair<TPBDRigidClusteredParticleHandle<FReal, 3>*, TSet<TPBDRigidParticleHandle<FReal, 3>*>> & Iter : BreakResults)
						{
							const TSet<TPBDRigidParticleHandle<FReal, 3>*>& Activated = Iter.Value;

							for (TPBDRigidParticleHandle<FReal, 3> * Handle : Activated)
							{
								if (!CurrentSolver->GetProxies(Handle))
								{
									const TSet<IPhysicsProxyBase*>* ParentProxies = CurrentSolver->GetProxies(Iter.Key);
									if (ensure(ParentProxies))
									{
										for (IPhysicsProxyBase* ParentProxy : *ParentProxies)
											CurrentSolver->AddParticleToProxy(Handle, ParentProxy);
									}
								}
							}
						}
					}
					CommandsToRemove.Add(CommandIndex);
				}
				else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_Kill))
				{
					SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_Kill);

					{
						TArray<float> LocalResults;
						LocalResults.AddZeroed(Handles.Num());
						TArrayView<float> ResultsView(&(LocalResults[0]), LocalResults.Num());

						static_cast<const FFieldNode<float>*>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

						for (const ContextIndex& Index : Context.GetEvaluatedSamples())
						{
							Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = Handles[Index.Sample]->CastToRigidParticle();
							if (RigidHandle && ResultsView[Index.Result] > 0.0)
							{
								CurrentSolver->GetEvolution()->DisableParticle(RigidHandle);
							}
						}
					}
					CommandsToRemove.Add(CommandIndex);
				}
				else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_LinearVelocity))
				{
					SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_LinearVelocity);

					{
						TArray<FVector> LocalResults;
						LocalResults.AddUninitialized(Handles.Num());
						TArrayView<FVector> ResultsView(&(LocalResults[0]), LocalResults.Num());

						static_cast<const FFieldNode<FVector>*>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

						for (const ContextIndex& Index : Context.GetEvaluatedSamples())
						{
							Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = Handles[Index.Sample]->CastToRigidParticle();
							if (RigidHandle && RigidHandle->ObjectState() == Chaos::EObjectStateType::Dynamic)
							{
								RigidHandle->V() += ResultsView[Index.Result];
							}
						}
					}
					CommandsToRemove.Add(CommandIndex);
				}
				else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_AngularVelociy))
				{
					SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_AngularVelocity);

					{
						TArray<FVector> LocalResults;
						LocalResults.AddUninitialized(Handles.Num());
						TArrayView<FVector> ResultsView(&(LocalResults[0]), LocalResults.Num());

						static_cast<const FFieldNode<FVector>*>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

						for (const ContextIndex& Index : Context.GetEvaluatedSamples())
						{
							Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = Handles[Index.Sample]->CastToRigidParticle();
							if (RigidHandle && RigidHandle->ObjectState() == Chaos::EObjectStateType::Dynamic)
							{
								RigidHandle->W() += ResultsView[Index.Result];
							}
						}
					}
					CommandsToRemove.Add(CommandIndex);
				}
				else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_SleepingThreshold))
				{
					SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_SleepingThreshold);

					{
						TArray<float> LocalResults;
						LocalResults.AddZeroed(Handles.Num());
						TArrayView<float> ResultsView(&(LocalResults[0]), LocalResults.Num());

						static_cast<const FFieldNode<float>*>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

						for (const ContextIndex& Index : Context.GetEvaluatedSamples())
						{
							Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = Handles[Index.Sample]->CastToRigidParticle();
							if (RigidHandle && ResultsView.Num() > 0)
							{
								// if no per particle physics material is set, make one
								if (!CurrentSolver->GetEvolution()->GetPerParticlePhysicsMaterial(RigidHandle).IsValid())
								{

									TUniquePtr<Chaos::FChaosPhysicsMaterial> NewMaterial = MakeUnique< Chaos::FChaosPhysicsMaterial>();
									NewMaterial->SleepingLinearThreshold = ResultsView[Index.Result];
									NewMaterial->SleepingAngularThreshold = ResultsView[Index.Result];


									CurrentSolver->GetEvolution()->SetPhysicsMaterial(RigidHandle, MakeSerializable(NewMaterial));
									CurrentSolver->GetEvolution()->SetPerParticlePhysicsMaterial(RigidHandle, NewMaterial);
								}
								else
								{
									const TUniquePtr<FChaosPhysicsMaterial>& InstanceMaterial = CurrentSolver->GetEvolution()->GetPerParticlePhysicsMaterial(RigidHandle);

									if (ResultsView[Index.Result] != InstanceMaterial->DisabledLinearThreshold)
									{
										InstanceMaterial->SleepingLinearThreshold = ResultsView[Index.Result];
										InstanceMaterial->SleepingAngularThreshold = ResultsView[Index.Result];
									}
								}
							}
						}
					}
					CommandsToRemove.Add(CommandIndex);
				}
				else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_DisableThreshold))
				{
					SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_DisableThreshold);

					{
						TArray<float> LocalResults;
						LocalResults.AddUninitialized(Handles.Num());
						TArrayView<float> ResultsView(&(LocalResults[0]), LocalResults.Num());

						static_cast<const FFieldNode<float>*>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

						for (const ContextIndex& Index : Context.GetEvaluatedSamples())
						{
							Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = Handles[Index.Sample]->CastToRigidParticle();

							if (RigidHandle && RigidHandle->ObjectState() == Chaos::EObjectStateType::Dynamic && ResultsView.Num() > 0)
							{
								// if no per particle physics material is set, make one
								if (!CurrentSolver->GetEvolution()->GetPerParticlePhysicsMaterial(RigidHandle).IsValid())
								{

									TUniquePtr<Chaos::FChaosPhysicsMaterial> NewMaterial = MakeUnique< Chaos::FChaosPhysicsMaterial>();
									NewMaterial->DisabledLinearThreshold = ResultsView[Index.Result];
									NewMaterial->DisabledAngularThreshold = ResultsView[Index.Result];

									CurrentSolver->GetEvolution()->SetPhysicsMaterial(RigidHandle, MakeSerializable(NewMaterial));
									CurrentSolver->GetEvolution()->SetPerParticlePhysicsMaterial(RigidHandle, NewMaterial);
								}
								else
								{
									const TUniquePtr<FChaosPhysicsMaterial>& InstanceMaterial = CurrentSolver->GetEvolution()->GetPerParticlePhysicsMaterial(RigidHandle);

									if (ResultsView[Index.Result] != InstanceMaterial->DisabledLinearThreshold)
									{
										InstanceMaterial->DisabledLinearThreshold = ResultsView[Index.Result];
										InstanceMaterial->DisabledAngularThreshold = ResultsView[Index.Result];
									}
								}
							}
						}
					}
					CommandsToRemove.Add(CommandIndex);
				}
				else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_InternalClusterStrain))
				{
					SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_InternalClusterStrain);

					{
						TArray<float> LocalResults;
						LocalResults.AddZeroed(Handles.Num());
						TArrayView<float> ResultsView(&(LocalResults[0]), LocalResults.Num());

						static_cast<const FFieldNode<float>*>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

						for (const ContextIndex& Index : Context.GetEvaluatedSamples())
						{
							Chaos::TPBDRigidClusteredParticleHandle<float, 3>* RigidHandle = Handles[Index.Sample]->CastToClustered();
							if (RigidHandle && RigidHandle->ObjectState() == Chaos::EObjectStateType::Dynamic)
							{
								RigidHandle->Strain() += ResultsView[Index.Result];
							}
						}
					}
					CommandsToRemove.Add(CommandIndex);
				}
				else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_CollisionGroup))
				{
					SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_CollisionGroup);

					{
						TArray<int32> LocalResults;
						LocalResults.AddZeroed(Handles.Num());
						TArrayView<int32> ResultsView(&(LocalResults[0]), LocalResults.Num());

						static_cast<const FFieldNode<int32>*>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

						for (const ContextIndex& Index : Context.GetEvaluatedSamples())
						{
							Chaos::TPBDRigidClusteredParticleHandle<float, 3>* RigidHandle = Handles[Index.Sample]->CastToClustered();
							if (RigidHandle)
							{
								RigidHandle->SetCollisionGroup(ResultsView[Index.Result]);
							}
						}
					}
					CommandsToRemove.Add(CommandIndex);
				}
				else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_PositionStatic))
				{
					SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_PositionStatic);

					{
						TArray<int32> LocalResults;
						LocalResults.AddZeroed(Handles.Num());
						TArrayView<int32> ResultsView(&(LocalResults[0]), LocalResults.Num());

						static_cast<const FFieldNode<int32>*>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

#if TODO_REIMPLEMENT_KINEMATIC_PROXY
						for (const ContextIndex& CIndex : Context.GetEvaluatedSamples())
						{
							const int32 i = CIndex.Result;
							if (Results[i])
							{
								if (PositionTargetedParticles.Contains(i))
								{
									int32 Index = PositionTargetedParticles[i];
									PositionTarget.Replace(Index, Particles.X(i));
								}
								else
								{
									int32 Index = PositionTarget.AddConstraint(Particles.Handle(i), Particles.X(i)); //??
									PositionTargetedParticles.Add(i, Index);
								}
							}
						}
#endif
					}

					CommandsToRemove.Add(CommandIndex);
				}
				else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_PositionTarget))
				{
					SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_PositionTarget);

					{
						TArray<FVector> LocalResults;
						LocalResults.Init(FVector(FLT_MAX), Handles.Num());
						TArrayView<FVector> ResultsView(&(LocalResults[0]), LocalResults.Num());

						static_cast<const FFieldNode<FVector>*>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

#if TODO_REIMPLEMENT_KINEMATIC_PROXY
						for (const ContextIndex& CIndex : Context.GetEvaluatedSamples())
						{
							const int32 i = CIndex.Result;
							if (Results[i] != FVector(FLT_MAX))
							{
								if (PositionTargetedParticles.Contains(i))
								{
									int32 Index = PositionTargetedParticles[i];
									PositionTarget.Replace(Index, Results[i]);
								}
								else
								{
									int32 Index = PositionTarget.Add(i, Results[i]);
									PositionTargetedParticles.Add(i, Index);
								}
							}
						}
#endif
					}

					CommandsToRemove.Add(CommandIndex);
				}
				else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_PositionAnimated))
				{
					SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_PositionAnimated);

					{
						TArray<int32> LocalResults;
						LocalResults.Init(false, Handles.Num());
						TArrayView<int32> ResultsView(&(LocalResults[0]), LocalResults.Num());

						static_cast<const FFieldNode<int32>*>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

#if TODO_REIMPLEMENT_KINEMATIC_PROXY
						for (int32 i = 0; i < AnimatedPosition.Num(); ++i)
						{
							for (int32 j = 0; j < AnimatedPosition[i].Ids.Num(); ++j)
							{
								int32 Index = AnimatedPosition[i].Ids[j];
								if (Results[Index])
								{
									if (PositionTargetedParticles.Contains(Index))
									{
										int32 PosIndex = PositionTargetedParticles[i];
										PositionTarget.Replace(PosIndex, AnimatedPosition[i].Position[j]);
									}
									else
									{
										int32 PosIndex = PositionTarget.Add(i, AnimatedPosition[i].Position[j]);
										PositionTargetedParticles.Add(i, PosIndex);
									}
								}
							}
						}
#endif
					}
					CommandsToRemove.Add(CommandIndex);
				}
				else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_DynamicConstraint))
				{
					SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_DynamicConstraint);

					{
#if TODO_REIMPLEMENT_DYNAMIC_CONSTRAINT_ACCESSORS
						Chaos::TPBDRigidDynamicSpringConstraints<float, 3>& DynamicConstraints = FPhysicsSolver::FAccessor(CurrentSolver).DynamicConstraints();
						TSet<int32>& DynamicConstraintParticles = FPhysicsSolver::FAccessor(CurrentSolver).DynamicConstraintParticles();

						FPerSolverFieldSystem::ContiguousIndices(IndicesArray, CurrentSolver, ResolutionType, IndicesArray.Num() != Particles.Size());
						if (IndicesArray.Num())
						{
							TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());

							FVector* tptr = &(Particles.X(0));
							TArrayView<FVector> SamplesView(tptr, int32(Particles.Size()));

							FFieldContext Context{
								IndexView,
								SamplesView,
								Command.MetaData,
								TimeSeconds
							};

							TArray<float> Results;
							Results.AddUninitialized(Particles.Size());
							for (const ContextIndex& CIndex : IndicesArray)
							{
								Results[CIndex.Sample] = FLT_MAX;
							}
							TArrayView<float> ResultsView(&(Results[0]), Results.Num());
							static_cast<const FFieldNode<float>*>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

							for (const ContextIndex& CIndex : Context.GetEvaluatedSamples())
							{
								const int32 i = CIndex.Result;
								if (Results[i] != FLT_MAX)
								{
									if (!DynamicConstraintParticles.Contains(i))
									{
										DynamicConstraints.SetDistance(Results[i]);
										for (const int32 Index : DynamicConstraintParticles)
										{
											DynamicConstraints.Add(Index, i);
										}
										DynamicConstraintParticles.Add(i);
									}
								}
							}
						}
#endif
					}
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
	Chaos::TPBDRigidParticles<float, 3>& Particles,
	Chaos::TArrayCollectionArray<float>& Strains,
	Chaos::TPBDPositionConstraints<float, 3>& PositionTarget,
	TMap<int32, int32>& PositionTargetedParticles)
	//const TArray<FKinematicProxy>& AnimatedPosition)
{
	FieldParameterUpdateInternal(InSolver, Particles, Strains, PositionTarget, PositionTargetedParticles, TransientCommands, true);
	FieldParameterUpdateInternal(InSolver, Particles, Strains, PositionTarget, PositionTargetedParticles, PersistentCommands, false);
}

template <typename Traits>
void FPerSolverFieldSystem::FieldForcesUpdateInternal(
	Chaos::TPBDRigidsSolver<Traits>* InSolver,
	Chaos::TPBDRigidParticles<float, 3>& Particles,
	Chaos::TArrayCollectionArray<FVector>& Force,
	Chaos::TArrayCollectionArray<FVector>& Torque,
	TArray<FFieldSystemCommand>& Commands, const bool IsTransient)
{
	using namespace Chaos;
	SCOPE_CYCLE_COUNTER(STAT_ForceUpdateField_Object);

	const int32 NumCommands = Commands.Num();
	if (NumCommands && InSolver)
	{
		Chaos::TPBDRigidsSolver<Traits>* CurrentSolver = InSolver;

		TArray<int32> CommandsToRemove;
		CommandsToRemove.Reserve(NumCommands);

		TArray<Chaos::TGeometryParticleHandle<float, 3>*> Handles;
		TArray<FVector> SamplePoints;
		TArray<ContextIndex> SampleIndices;
		EFieldResolutionType PrevResolutionType = EFieldResolutionType::Field_Resolution_Max;
		EFieldFilterType PrevFilterType = EFieldFilterType::Field_Filter_Max;
		for (int32 CommandIndex = 0; CommandIndex < NumCommands; CommandIndex++)
		{
			const FFieldSystemCommand& Command = Commands[CommandIndex];
			const float TimeSeconds = InSolver->GetSolverTime() - Command.TimeCreation;

			const EFieldResolutionType ResolutionType =
				Command.HasMetaData(FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution) ?
				Command.GetMetaDataAs<FFieldSystemMetaDataProcessingResolution>(
					FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution)->ProcessingResolution :
				EFieldResolutionType::Field_Resolution_Minimal;

			const EFieldFilterType FilterType =
				Command.HasMetaData(FFieldSystemMetaData::EMetaType::ECommandData_Filter) ?
				Command.GetMetaDataAs<FFieldSystemMetaDataFilter>(
					FFieldSystemMetaData::EMetaType::ECommandData_Filter)->FilterType :
				EFieldFilterType::Field_Filter_Dynamic;

			if ((PrevResolutionType != ResolutionType) || (PrevFilterType != FilterType) || Handles.Num() == 0)
			{
				if (FilterType != EFieldFilterType::Field_Filter_Max)
				{
					FPerSolverFieldSystem::FilterParticleHandles(Handles, CurrentSolver, FilterType);
				}
				else
				{
					FPerSolverFieldSystem::GetParticleHandles(Handles, CurrentSolver, ResolutionType);
				}

				PrevResolutionType = ResolutionType;
				PrevFilterType = FilterType;

				SamplePoints.SetNum(Handles.Num());
				SampleIndices.SetNum(Handles.Num());
				for (int32 Idx = 0; Idx < Handles.Num(); ++Idx)
				{
					SamplePoints[Idx] = Handles[Idx]->X();
					SampleIndices[Idx] = ContextIndex(Idx, Idx);
				}
			}

			if (Handles.Num())
			{
				TArrayView<FVector> SamplePointsView(&(SamplePoints[0]), SamplePoints.Num());
				TArrayView<ContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

				FFieldContext Context{
					SampleIndicesView,
					SamplePointsView,
					Command.MetaData,
					TimeSeconds
				};
				if (Command.RootNode->Type() == FFieldNode<FVector>::StaticType())
				{
					TArray<FVector> LocalResults;
					LocalResults.AddZeroed(Handles.Num());
					TArrayView<FVector> ResultsView(&(LocalResults[0]), LocalResults.Num());

					static_cast<const FFieldNode<FVector>*>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

					if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_LinearForce))
					{
						SCOPE_CYCLE_COUNTER(STAT_ForceUpdateField_LinearForce);

						for (const ContextIndex& Index : Context.GetEvaluatedSamples())
						{
							Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = Handles[Index.Sample]->CastToRigidParticle();
							if (RigidHandle && !RigidHandle->Disabled() && (RigidHandle->ObjectState() == Chaos::EObjectStateType::Dynamic || RigidHandle->ObjectState() == Chaos::EObjectStateType::Sleeping))
							{
								if (RigidHandle->Sleeping())
								{
									RigidHandle->SetObjectState(Chaos::EObjectStateType::Dynamic);
								}
								RigidHandle->F() += ResultsView[Index.Result];
							}
						}

#if TODO_REIMPLEMENT_WAKE_ISLANDS
						// @todo(ccaulfield): encapsulation: add WakeParticles (and therefore islands) functionality to Evolution
						TSet<int32> IslandsToActivate;
						for (const ContextIndex& CIndex : Context.GetEvaluatedSamples())
						{
							const int32 i = CIndex.Result;
							if (ForceView[i] != FVector(0) && Particles.ObjectState(i) == Chaos::EObjectStateType::Sleeping && !Particles.Disabled(i) && IslandsToActivate.Find(Particles.Island(i)) == nullptr)
							{
								IslandsToActivate.Add(Particles.Island(i));
							}
						}
						InSolver->WakeIslands(IslandsToActivate);
#endif
						CommandsToRemove.Add(CommandIndex);
					}
					else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_AngularTorque))
					{
						SCOPE_CYCLE_COUNTER(STAT_ForceUpdateField_AngularTorque);

						for (const ContextIndex& Index : Context.GetEvaluatedSamples())
						{
							Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = Handles[Index.Sample]->CastToRigidParticle();
							if (RigidHandle && (RigidHandle->ObjectState() == Chaos::EObjectStateType::Dynamic || RigidHandle->ObjectState() == Chaos::EObjectStateType::Sleeping))
							{
								if (RigidHandle->Sleeping())
								{
									RigidHandle->SetObjectState(Chaos::EObjectStateType::Dynamic);
								}
								RigidHandle->Torque() += ResultsView[Index.Result];
							}
						}

#if TODO_REIMPLEMENT_WAKE_ISLANDS
						// @todo(ccaulfield): encapsulation: add WakeParticles (and therefore islands) functionality to Evolution
						TSet<int32> IslandsToActivate;
						for (const ContextIndex& CIndex : Context.GetEvaluatedSamples())
						{
							const int32 i = CIndex.Result;
							if (TorqueView[i] != FVector(0) && Particles.ObjectState(i) == Chaos::EObjectStateType::Sleeping && !Particles.Disabled(i) && IslandsToActivate.Find(Particles.Island(i)) == nullptr)
							{
								IslandsToActivate.Add(Particles.Island(i));
							}
						}
						InSolver->WakeIslands(IslandsToActivate);
#endif
						CommandsToRemove.Add(CommandIndex);
					}
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
	Chaos::TPBDRigidsSolver<Traits>* InSolver,
	Chaos::TPBDRigidParticles<float, 3>& Particles,
	Chaos::TArrayCollectionArray<FVector>& Force,
	Chaos::TArrayCollectionArray<FVector>& Torque)
{
	FieldForcesUpdateInternal(InSolver, Particles, Force, Torque, TransientCommands, true);
	FieldForcesUpdateInternal(InSolver, Particles, Force, Torque, PersistentCommands, false);
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

//void FPerSolverFieldSystem::GetParticleHandles(
//	TArray<Chaos::TGeometryParticleHandle<float,3>*>& Handles,
//	const Chaos::FPhysicsSolver* RigidSolver,
//	const EFieldResolutionType ResolutionType,
//	const bool bForce)
//{
//	Handles.SetNum(0, false);
//	if (!bForce)
//		return;
//
//	const Chaos::TPBDRigidsSOAs<float, 3>& SolverParticles = RigidSolver->GetParticles();
//
//	if (ResolutionType == EFieldResolutionType::Field_Resolution_Maximum)
//	{
//		// const TParticleView<TGeometryParticles<T, d>>& TPBDRigidSOAs<T,d>::GetAllParticlesView()
//		const Chaos::TParticleView<Chaos::TGeometryParticles<float, 3>> &ParticleView = 
//			SolverParticles.GetAllParticlesView();
//		Handles.Reserve(ParticleView.Num());
//
//		// TParticleIterator<TSOA> Begin() const, TSOA = TGeometryParticles<T, d>
//		for (Chaos::TParticleIterator<Chaos::TGeometryParticles<float, 3>> It = ParticleView.Begin(), ItEnd = ParticleView.End();
//			It != ItEnd; ++It)
//		{
//			const Chaos::TTransientGeometryParticleHandle<float,3> *Handle = &(*It);
//			// PBDRigidsSOAs.h only has a const version of GetAllParticlesView() - is that wrong?
//			Handles.Add(GetHandleHelper(const_cast<Chaos::TTransientGeometryParticleHandle<float,3>*>(Handle)));
//		}
//	}
//	else if (ResolutionType == EFieldResolutionType::Field_Resolution_Minimal)
//	{
//		const Chaos::TParticleView<Chaos::TGeometryParticles<float, 3>> &ParticleView = 
//			SolverParticles.GetNonDisabledView();
//		Handles.Reserve(ParticleView.Num());
//		for (Chaos::TParticleIterator<Chaos::TGeometryParticles<float, 3>> It = ParticleView.Begin(), ItEnd = ParticleView.End();
//			It != ItEnd; ++It)
//		{
//			const Chaos::TTransientGeometryParticleHandle<float,3> *Handle = &(*It);
//			Handles.Add(GetHandleHelper(const_cast<Chaos::TTransientGeometryParticleHandle<float,3>*>(Handle)));
//		}
//	}
//	else if (ResolutionType == EFieldResolutionType::Field_Resolution_DisabledParents)
//	{
//		check(false); // unimplemented
//	}
//}

template <typename Traits>
void FPerSolverFieldSystem::GetParticleHandles(
	TArray<Chaos::TGeometryParticleHandle<float, 3>*>& Handles,
	const Chaos::TPBDRigidsSolver<Traits>* RigidSolver,
	const EFieldResolutionType ResolutionType,
	const bool bForce)
{
	Handles.SetNum(0, false);
	if (!bForce)
	{
		return;
	}

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
void FPerSolverFieldSystem::FilterParticleHandles(
	TArray<Chaos::TGeometryParticleHandle<float, 3>*>& Handles,
	const Chaos::TPBDRigidsSolver<Traits>* RigidSolver,
	const EFieldFilterType FilterType,
	const bool bForce)
{
	Handles.SetNum(0, false);
	if (!bForce)
	{
		return;
	}

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
		Chaos::TPBDRigidParticles<float, 3>& InParticles, \
		Chaos::TArrayCollectionArray<float>& Strains, \
		Chaos::TPBDPositionConstraints<float, 3>& PositionTarget, \
		TMap<int32, int32>& PositionTargetedParticles);\
\
template void FPerSolverFieldSystem::FieldForcesUpdateCallback(\
		Chaos::TPBDRigidsSolver<Chaos::Traits>* InSolver, \
		Chaos::TPBDRigidParticles<float, 3>& Particles, \
		Chaos::TArrayCollectionArray<FVector> & Force, \
		Chaos::TArrayCollectionArray<FVector> & Torque);\
\
template void FPerSolverFieldSystem::GetParticleHandles(\
		TArray<Chaos::TGeometryParticleHandle<float,3>*>& Handles,\
		const Chaos::TPBDRigidsSolver<Chaos::Traits>* RigidSolver,\
		const EFieldResolutionType ResolutionType,\
		const bool bForce);\
\
template void FPerSolverFieldSystem::FilterParticleHandles(\
		TArray<Chaos::TGeometryParticleHandle<float,3>*>& Handles,\
		const Chaos::TPBDRigidsSolver<Chaos::Traits>* RigidSolver,\
		const EFieldFilterType FilterType,\
		const bool bForce);\

#include "Chaos/EvolutionTraits.inl"
#undef EVOLUTION_TRAIT
