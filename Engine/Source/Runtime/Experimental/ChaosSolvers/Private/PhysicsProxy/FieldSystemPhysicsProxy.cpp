// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PhysicsProxy/FieldSystemPhysicsProxy.h"
#include "PhysicsProxy/GeometryCollectionPhysicsProxy.h"
#include "Field/FieldSystem.h"
#include "Chaos/PBDRigidClustering.h"
#include "GeometryCollection/GeometryCollectionSimulationTypes.h"
#include "PhysicsSolver.h"
#include "ChaosStats.h"

void ResetIndicesArray(TArray<int32> & IndicesArray, int32 Size)
{
	if(IndicesArray.Num() != Size)
	{
		IndicesArray.SetNum(Size);
		for(int32 i = 0; i < IndicesArray.Num(); ++i)
		{
			IndicesArray[i] = i;
		}
	}
}

//==============================================================================
// FFieldSystemPhysicsProxy
//==============================================================================

FFieldSystemPhysicsProxy::FFieldSystemPhysicsProxy(UObject* InOwner)
	: Base(InOwner)
{}

FFieldSystemPhysicsProxy::~FFieldSystemPhysicsProxy()
{
	// Need to delete any command lists we have hanging around
	FScopeLock Lock(&CommandLock);
	for(TTuple<Chaos::FPhysicsSolver*, TArray<FFieldSystemCommand>*>& Pair : Commands)
	{
		delete Pair.Get<1>();
	}
	Commands.Reset();
}

void FFieldSystemPhysicsProxy::Initialize()
{}

bool FFieldSystemPhysicsProxy::IsSimulating() const
{
	return true; // #todo Actually start gating this?
}

void FFieldSystemPhysicsProxy::FieldParameterUpdateCallback(
	Chaos::FPhysicsSolver* InSolver, 
	FParticlesType& Particles, 
	Chaos::TArrayCollectionArray<float>& Strains, 
	Chaos::TPBDPositionConstraints<float, 3>& PositionTarget, 
	TMap<int32, int32>& PositionTargetedParticles, 
	//const TArray<FKinematicProxy>& AnimatedPosition, 
	const float InTime)
{
	using namespace Chaos;
	SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_Object);
	
	Chaos::FPhysicsSolver* CurrentSolver = InSolver;

	if (Commands.Num() && InSolver)
	{
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		const Chaos::TArrayCollectionArray<Chaos::ClusterId> & ClusterIDs = CurrentSolver->GetRigidClustering().GetClusterIdsArray();
#endif // TODO_REIMPLEMENT_RIGID_CLUSTERING

		TArray<FFieldSystemCommand>* CommandListPtr = GetSolverCommandList(InSolver);
		if(!CommandListPtr)
		{
			// No command list present for this solver, bail out
			return;
		}
		const int32 NumCommands = CommandListPtr->Num();
		TArray<int32> CommandsToRemove;
		CommandsToRemove.Reserve(NumCommands);

		// @todo: This seems like a waste if we just want to get everything
		//TArray<ContextIndex> IndicesArray;
		TArray<Chaos::TGeometryParticleHandle<float,3>*> Handles;
		TArray<FVector> SamplePoints;
		TArray<ContextIndex> SampleIndices;
		TArray<ContextIndex>& IndicesArray = SampleIndices; // Do away with!
		EFieldResolutionType PrevResolutionType = static_cast<EFieldResolutionType>(0); // none
		for (int32 CommandIndex = 0; CommandIndex < NumCommands; CommandIndex++)
		{
			const FFieldSystemCommand& Command = (*CommandListPtr)[CommandIndex];
			const EFieldResolutionType ResolutionType = 
				Command.HasMetaData(FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution) ?
					Command.GetMetaDataAs<FFieldSystemMetaDataProcessingResolution>(
						FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution)->ProcessingResolution :
					EFieldResolutionType::Field_Resolution_Minimal;
			if (PrevResolutionType != ResolutionType || Handles.Num() == 0)
			{
				FFieldSystemPhysicsProxy::GetParticleHandles(Handles, CurrentSolver, ResolutionType);
				PrevResolutionType = ResolutionType;

				SamplePoints.AddUninitialized(Handles.Num());
				SampleIndices.AddUninitialized(Handles.Num());
				for (int32 Idx = 0; Idx < Handles.Num(); ++Idx)
				{
					SamplePoints[Idx] = Handles[Idx]->X();
					SampleIndices[Idx] = ContextIndex(Idx, Idx);
				}
			}

			if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_DynamicState))
			{
				SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_DynamicState);

				if (Handles.Num())
				{
					TArrayView<Chaos::TGeometryParticleHandle<float,3>*> HandlesView(&(Handles[0]), Handles.Num());
					TArrayView<FVector> SamplePointsView(&(SamplePoints[0]), SamplePoints.Num());
					TArrayView<ContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

					FFieldContext Context(
						SampleIndicesView, // @todo(chaos) important: an empty index array should evaluate everything
						SamplePointsView,
						Command.MetaData);

					//
					//  Sample the dynamic state array in the field
					//
					TArray<int32> DynamicState; // TODO: 32 bits seems rather excessive!!!
					DynamicState.AddUninitialized(Handles.Num());					
					int32 i = 0;
					for(Chaos::TGeometryParticleHandle<float,3>* Handle : Handles)
					{
						const Chaos::EObjectStateType CurrState = Handle->ObjectState();
						if(CurrState == Chaos::EObjectStateType::Kinematic)
						{
							DynamicState[i] = (int)EObjectStateTypeEnum::Chaos_Object_Kinematic;
						}
						else if (CurrState == Chaos::EObjectStateType::Static)
						{
							DynamicState[i] = (int)EObjectStateTypeEnum::Chaos_Object_Static;
						}
						else
						{
							DynamicState[i] = (int)EObjectStateTypeEnum::Chaos_Object_Dynamic;
						}
						++i;
					}

					TArrayView<int32> DynamicStateView(&(DynamicState[0]), DynamicState.Num());
					if (ensureMsgf(Command.RootNode->Type() == FFieldNode<int32>::StaticType(),
						TEXT("Field based evaluation of the simulations 'ObjectType' parameter expects int32 field inputs.")))
					{
						static_cast<const FFieldNode<int32>*>(
							Command.RootNode.Get())->Evaluate(Context, DynamicStateView);
					}

					bool StateChanged = false;
					i = 0;
					for (Chaos::TGeometryParticleHandle<float, 3>* Handle : Handles)
					{
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
						// not be dynamic.  Har.
						Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = Handle->CastToRigidParticle();
						if(RigidHandle && RigidHandle->ObjectState() == Chaos::EObjectStateType::Dynamic)
						{
							const int32 FieldState = DynamicStateView[i];
							const EObjectStateType HandleState = RigidHandle->ObjectState();
							if (FieldState == (int32)EObjectStateTypeEnum::Chaos_Object_Dynamic)
							{
								if ((HandleState == Chaos::EObjectStateType::Static ||
									 HandleState == Chaos::EObjectStateType::Kinematic) &&
									RigidHandle->M() > FLT_EPSILON)
								{
									RigidHandle->SetObjectStateLowLevel(Chaos::EObjectStateType::Dynamic);
									StateChanged = true;
								}
								else if (HandleState == Chaos::EObjectStateType::Sleeping)
								{
									RigidHandle->SetObjectStateLowLevel(Chaos::EObjectStateType::Dynamic);
									StateChanged = true;
								}
							}
							else if (FieldState == (int32)EObjectStateTypeEnum::Chaos_Object_Kinematic)
							{
								if (HandleState == Chaos::EObjectStateType::Dynamic)
								{
									RigidHandle->SetObjectStateLowLevel(Chaos::EObjectStateType::Kinematic);
									RigidHandle->SetV(Chaos::TVector<float, 3>(0));
									RigidHandle->SetW(Chaos::TVector<float, 3>(0));
									StateChanged = true;
								}
							}
							else if (FieldState == (int32)EObjectStateTypeEnum::Chaos_Object_Static)
							{
								if (HandleState == Chaos::EObjectStateType::Dynamic)
								{
									RigidHandle->SetObjectStateLowLevel(Chaos::EObjectStateType::Static);
									RigidHandle->SetV(Chaos::TVector<float, 3>(0));
									RigidHandle->SetW(Chaos::TVector<float, 3>(0));
									StateChanged = true;
								}
							}
							else if (FieldState == (int32)EObjectStateTypeEnum::Chaos_Object_Sleeping)
							{
								if (HandleState == Chaos::EObjectStateType::Dynamic)
								{
									RigidHandle->SetObjectStateLowLevel(Chaos::EObjectStateType::Sleeping);
									StateChanged = true;
								}
							}
						} // handle is dynamic
						++i;
					} // end for all handles
					if (StateChanged)
					{
						// regenerate views
						CurrentSolver->GetParticles().UpdateGeometryCollectionViews();
					}

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
					//  Update all cluster bodies based on the changes in the kinematic state.
					const Chaos::TArrayCollectionArray<Chaos::ClusterId>& ClusterIdArray = CurrentSolver->GetRigidClustering().GetClusterIdsArray();
					for (int32 ActiveParticleIndex : CurrentSolver->ActiveIndices())
					{
						if (ClusterIdArray[ActiveParticleIndex].NumChildren)
						{
							CurrentSolver->GetRigidClustering().UpdateKinematicProperties(ActiveParticleIndex);
						}
					}
#endif // TODO_REIMPLEMENT_RIGID_CLUSTERING

				}
				CommandsToRemove.Add(CommandIndex);
			}
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_ActivateDisabled))
			{
				FFieldSystemPhysicsProxy::ContiguousIndices(IndicesArray, CurrentSolver, ResolutionType, IndicesArray.Num() != Particles.Size());
				if (IndicesArray.Num())
				{
					TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());

					FVector * tptr = &(Particles.X(0));
					TArrayView<FVector> SamplesView(tptr, int32(Particles.Size()));

					FFieldContext Context{
						IndexView, // @todo(chaos) important: an empty index array should evaluate everything
						SamplesView,
						Command.MetaData
					};

					//
					//  Sample the dynamic state array in the field
					//
					TArray<int32> DynamicState;
					DynamicState.AddUninitialized(Particles.Size());
					for(const ContextIndex& Index : IndicesArray)
					{
						DynamicState[Index.Sample] = 0;	//is this needed?
					}
					
					for (const ContextIndex& Index : IndicesArray)
					{
						const int32 i = Index.Sample;
						if (Particles.Disabled(i))
						{
							DynamicState[i] = 1;
						}
						else
						{
							DynamicState[i] = 0;
						}
					}
					TArrayView<int32> DynamicStateView(&(DynamicState[0]), DynamicState.Num());

					if (ensureMsgf(Command.RootNode->Type() == FFieldNode<int32>::StaticType(),
						TEXT("Field based evaluation of the simulations 'ObjectType' parameter expects int32 field inputs.")))
					{
						static_cast<const FFieldNode<int32> *>(Command.RootNode.Get())->Evaluate(Context, DynamicStateView);
					}

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
					// transfer results to rigid system.
					int32 FloorIndex = CurrentSolver->GetFloorIndex();
					int32 NumSamples = Context.SampleIndices.Num();
					for (int32 SampleIndex = 0; SampleIndex < NumSamples; SampleIndex++)
					{
						int32 RigidBodyIndex = Context.SampleIndices[SampleIndex].Result;
						if (RigidBodyIndex != FloorIndex) // ignore the floor
						{ 
							if (DynamicStateView[RigidBodyIndex] == 0 && Particles.Disabled(RigidBodyIndex))
							{
								ensure(CurrentSolver->GetRigidClustering().GetClusterIdsArray()[RigidBodyIndex].Id == INDEX_NONE);
								CurrentSolver->GetEvolution()->EnableParticle(RigidBodyIndex, INDEX_NONE);
								Particles.SetObjectState(RigidBodyIndex, Chaos::EObjectStateType::Dynamic);
							}
						}
					}
#endif
				}
				CommandsToRemove.Add(CommandIndex);
			}
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_ExternalClusterStrain))
			{
				SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_ExternalClusterStrain);

				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<float>::StaticType(),
					TEXT("Field based evaluation of the simulations 'Strain' parameter expects float field inputs.")))
				{
					FFieldSystemPhysicsProxy::ContiguousIndices(IndicesArray, CurrentSolver, ResolutionType, IndicesArray.Num() != Particles.Size());
					if (IndicesArray.Num())
					{
						TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());

						FVector * tptr = &(Particles.X(0));
						TArrayView<FVector> SamplesView(tptr, int32(Particles.Size()));

						FFieldContext Context{
							IndexView,
							SamplesView,
							Command.MetaData
						};

						TArray<float> StrainSamples;
						StrainSamples.AddUninitialized(SamplesView.Num());
						for (const ContextIndex& Index : IndicesArray)
						{
							StrainSamples[Index.Sample] = 0.f;
						}
						TArrayView<float> FloatBuffer(&StrainSamples[0], StrainSamples.Num());
						static_cast<const FFieldNode<float> *>(Command.RootNode.Get())->Evaluate(Context, FloatBuffer);

						int32 Iterations = 1;
						if (Command.MetaData.Contains(FFieldSystemMetaData::EMetaType::ECommandData_Iteration))
						{
							Iterations = static_cast<FFieldSystemMetaDataIteration*>(Command.MetaData[FFieldSystemMetaData::EMetaType::ECommandData_Iteration].Get())->Iterations;
						}

						if (StrainSamples.Num())
						{
							CurrentSolver->GetRigidClustering().BreakingModel(StrainSamples);
						}
					}
				}
				CommandsToRemove.Add(CommandIndex);
			}
#endif
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_Kill))
			{
				SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_Kill);

				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<float>::StaticType(),
					TEXT("Field based evaluation of the simulations 'Disabled' parameter expects float field inputs.")))
				{
					FFieldSystemPhysicsProxy::ContiguousIndices(IndicesArray, CurrentSolver, ResolutionType, IndicesArray.Num() != Particles.Size());
					if (IndicesArray.Num())
					{
						TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());

						FVector * tptr = &(Particles.X(0));
						TArrayView<FVector> SamplesView(tptr, int32(Particles.Size()));

						FFieldContext Context{
							IndexView,
							SamplesView,
							Command.MetaData
						};

						// @todo(better) : Dont copy, support native type conversion. 
						TArray<float> Results;
						Results.AddUninitialized(Particles.Size());
						for (const ContextIndex& Index : IndicesArray)
						{
							Results[Index.Sample] = 0.f;
						}
						TArrayView<float> ResultsView(&(Results[0]), Results.Num());
						static_cast<const FFieldNode<float> *>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

#if TODO_REIMPLEMENT_GETFLOORINDEX
						bool bHasFloor = false;
						int32 FloorIndex = CurrentSolver->GetFloorIndex();
						if (FloorIndex != INDEX_NONE)
						{
							bHasFloor = !Particles.Disabled(FloorIndex);
						}

						TSet<uint32> RemovedParticles;
						for (const ContextIndex& Index : IndicesArray)
						{
							const int32 i = Index.Result;
							if (!Particles.Disabled(i) && Results[i] > 0.0)
							{
								RemovedParticles.Add((uint32)i);
								CurrentSolver->GetEvolution()->DisableParticle(i);
							}
						}

						if (RemovedParticles.Num() && bHasFloor)
						{
							CurrentSolver->GetEvolution()->DisableParticle(FloorIndex);
							Particles.SetObjectState(FloorIndex, Chaos::EObjectStateType::Static);
						}
#endif
					}
				}
				CommandsToRemove.Add(CommandIndex);
			}
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_LinearVelocity))
			{
				SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_LinearVelocity);

				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<FVector>::StaticType(),
					TEXT("Field based evaluation of the simulations 'LinearVelocity' parameter expects FVector field inputs.")))
				{
					FFieldSystemPhysicsProxy::ContiguousIndices(IndicesArray, CurrentSolver, ResolutionType, IndicesArray.Num() != Particles.Size());
					if (IndicesArray.Num())
					{
						TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());

						FVector * tptr = &(Particles.X(0));
						TArrayView<FVector> SamplesView(tptr, int32(Particles.Size()));

						FFieldContext Context{
							IndexView,
							SamplesView,
							Command.MetaData
						};

						FVector * vptr = &(Particles.V(0));
						TArrayView<FVector> ResultsView(vptr, Particles.Size());
						static_cast<const FFieldNode<FVector> *>(Command.RootNode.Get())->Evaluate(Context, ResultsView);
					}
				}
				CommandsToRemove.Add(CommandIndex);
			}
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_AngularVelociy))
			{
				SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_AngularVelocity);

				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<FVector>::StaticType(),
					TEXT("Field based evaluation of the simulations 'AngularVelocity' parameter expects FVector field inputs.")))
				{
					FFieldSystemPhysicsProxy::ContiguousIndices(IndicesArray, CurrentSolver, ResolutionType, IndicesArray.Num() != Particles.Size());
					if (IndicesArray.Num())
					{
						TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());

						FVector * tptr = &(Particles.X(0));
						TArrayView<FVector> SamplesView(tptr, int32(Particles.Size()));

						FFieldContext Context{
							IndexView,
							SamplesView,
							Command.MetaData
						};

						FVector * vptr = &(Particles.W(0));
						TArrayView<FVector> ResultsView(vptr, Particles.Size());
						static_cast<const FFieldNode<FVector> *>(Command.RootNode.Get())->Evaluate(Context, ResultsView);
					}
				}
				CommandsToRemove.Add(CommandIndex);
			}
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_SleepingThreshold))
			{
				SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_SleepingThreshold);

				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<float>::StaticType(),
					TEXT("Field based evaluation of the simulations 'Disable' parameter expects scale field inputs.")))
				{
#if TODO_REIMPLEMENT_PHYSICS_PROXY_REVERSE_MAPPING
					const Chaos::TArrayCollectionArray<PhysicsProxyWrapper>& PhysicsProxyMapping = CurrentSolver->GetPhysicsProxyReverseMapping();
					
					FFieldSystemPhysicsProxy::ContiguousIndices(IndicesArray, CurrentSolver, ResolutionType, IndicesArray.Num() != Particles.Size());
					if (IndicesArray.Num())
					{
						TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());

						FVector * tptr = &(Particles.X(0));
						TArrayView<FVector> SamplesView(tptr, int32(Particles.Size()));

						FFieldContext Context{
							IndexView,
							SamplesView,
							Command.MetaData
						};

						TArray<float> Results;
						Results.AddUninitialized(Particles.Size());
						for (const ContextIndex Index : IndicesArray)
						{
							const SolverObjectWrapper& ParticleObjectWrapper = SolverObjectMapping[Index.Result];
							TSerializablePtr<Chaos::FChaosPhysicsMaterial> Material = CurrentSolver->GetPhysicsMaterial(Index.Result);
							if (ensure(Material) && ParticleObjectWrapper.SolverObject)
							{
								const TUniquePtr<Chaos::FChaosPhysicsMaterial>& InstanceMaterial = CurrentSolver->GetPerParticlePhysicsMaterial(Index.Result);
								if (InstanceMaterial)
								{
									Results[Index.Result] = InstanceMaterial->SleepingLinearThreshold;
								}
								else
								{
									Results[Index.Result] = Material->SleepingLinearThreshold;
								}
							}
							else
							{
								Results[Index.Result] = 0.0f;
							}
						}

						TArrayView<float> ResultsView(&(Results[0]), Results.Num());
						static_cast<const FFieldNode<float> *>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

						for (const ContextIndex Index : IndicesArray)
						{
							const int32 i = Index.Result;
							const PhysicsProxyWrapper& ParticleObjectWrapper = PhysicsProxyMapping[i];
							TSerializablePtr<FChaosPhysicsMaterial> Material = CurrentSolver->GetPhysicsMaterial(i);
							if (!ensure(Material) || !ParticleObjectWrapper.PhysicsProxy)	//question: do we actually need to check for solver object?
							{
								continue;
							}

							//per instance override
							if (!CurrentSolver->GetPerParticlePhysicsMaterial(Index.Result))
							{
								if (Results[i] != Material->SleepingLinearThreshold)
								{
									// value changed from shared material, make unique material.
									TUniquePtr<Chaos::FChaosPhysicsMaterial> NewMaterial = MakeUnique< Chaos::FChaosPhysicsMaterial>(*Material);
									CurrentSolver->SetPerParticlePhysicsMaterial(Index.Result, MoveTemp(NewMaterial));
									const TUniquePtr<FChaosPhysicsMaterial>& InstanceMaterial = CurrentSolver->GetPerParticlePhysicsMaterial(i);

									InstanceMaterial->SleepingLinearThreshold = Results[i];
									InstanceMaterial->SleepingAngularThreshold = Results[i];
								}
							}
							else
							{
								const TUniquePtr<FChaosPhysicsMaterial>& InstanceMaterial = CurrentSolver->GetPerParticlePhysicsMaterial(i);

								if (InstanceMaterial->SleepingLinearThreshold != Results[i])
								{
									InstanceMaterial->SleepingLinearThreshold = Results[i];
									InstanceMaterial->SleepingAngularThreshold = Results[i];
								}
							}
						}
					}
#endif
				}
				CommandsToRemove.Add(CommandIndex);
			}
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_DisableThreshold))
			{
				SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_DisableThreshold);

				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<float>::StaticType(),
					TEXT("Field based evaluation of the simulations 'Disable' parameter expects scale field inputs.")))
				{
#if TODO_REIMPLEMENT_PHYSICS_PROXY_REVERSE_MAPPING
					const Chaos::TArrayCollectionArray<PhysicsProxyWrapper>& PhysicsProxyMapping = CurrentSolver->GetPhysicsProxyReverseMapping();
					
					FFieldSystemPhysicsProxy::ContiguousIndices(IndicesArray, CurrentSolver, ResolutionType, IndicesArray.Num() != Particles.Size());
					if (IndicesArray.Num())
					{
						TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());

						FVector * tptr = &(Particles.X(0));
						TArrayView<FVector> SamplesView(tptr, int32(Particles.Size()));

						FFieldContext Context{
							IndexView,
							SamplesView,
							Command.MetaData
						};

						TArray<float> Results;
						Results.AddUninitialized(Particles.Size());
						for (const ContextIndex Index : IndicesArray)
						{
							const SolverObjectWrapper& ParticleObjectWrapper = SolverObjectMapping[Index.Result];
							TSerializablePtr<Chaos::FChaosPhysicsMaterial> Material = CurrentSolver->GetPhysicsMaterial(Index.Result);
							if (ensure(Material) && ParticleObjectWrapper.SolverObject)
							{
								const TUniquePtr<Chaos::FChaosPhysicsMaterial>& InstanceMaterial = CurrentSolver->GetPerParticlePhysicsMaterial(Index.Result);
								if (InstanceMaterial)
								{
									Results[Index.Result] = InstanceMaterial->DisabledLinearThreshold;
								}
								else
								{
									Results[Index.Result] = Material->DisabledLinearThreshold;
								}
							}
							else
							{
								Results[Index.Result] = 0.0f;
							}
						}

						TArrayView<float> ResultsView(&(Results[0]), Results.Num());
						static_cast<const FFieldNode<float> *>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

						for (const ContextIndex Index : IndicesArray)
						{
							const int32 i = Index.Result;
							const PhysicsProxyWrapper& ParticleObjectWrapper = PhysicsProxyMapping[i];
							TSerializablePtr<Chaos::FChaosPhysicsMaterial> Material = CurrentSolver->GetPhysicsMaterial(i);
							if (!ensure(Material) || !ParticleObjectWrapper.PhysicsProxy)	//question: do we actually need to check for solver object?
							{
								continue;
							}

							//per instance override
							if (!CurrentSolver->GetPerParticlePhysicsMaterial(Index.Result))
							{
								if (Results[i] != Material->DisabledLinearThreshold)
								{
									// value changed from shared material, make unique material.
									TUniquePtr<Chaos::FChaosPhysicsMaterial> NewMaterial = MakeUnique< Chaos::FChaosPhysicsMaterial>(*Material);
									CurrentSolver->SetPerParticlePhysicsMaterial(Index.Result, MoveTemp(NewMaterial));
									const TUniquePtr<FChaosPhysicsMaterial>& InstanceMaterial = CurrentSolver->GetPerParticlePhysicsMaterial(i);

									InstanceMaterial->DisabledLinearThreshold = Results[i];
									InstanceMaterial->DisabledAngularThreshold = Results[i];
								}
							}
							else
							{
								const TUniquePtr<FChaosPhysicsMaterial>& InstanceMaterial = CurrentSolver->GetPerParticlePhysicsMaterial(i);

								if (InstanceMaterial->DisabledLinearThreshold != Results[i])
								{
									InstanceMaterial->DisabledLinearThreshold = Results[i];
									InstanceMaterial->DisabledAngularThreshold = Results[i];
								}
							}
						}
					}
#endif
				}
				CommandsToRemove.Add(CommandIndex);
			}
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_InternalClusterStrain))
			{
				SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_InternalClusterStrain);
				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<float>::StaticType(),
					TEXT("Field based evaluation of the simulations 'ExternalClusterStrain' parameter expects scalar field inputs.")))
				{
					FFieldSystemPhysicsProxy::ContiguousIndices(IndicesArray, CurrentSolver, ResolutionType, IndicesArray.Num() != Particles.Size());
					if (IndicesArray.Num())
					{
						TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());

						FVector * tptr = &(Particles.X(0));
						TArrayView<FVector> SamplesView(tptr, int32(Particles.Size()));

						FFieldContext Context{
							IndexView,
							SamplesView,
							Command.MetaData
						};

						float * vptr = &(Strains[0]);
						TArrayView<float> ResultsView(vptr, Particles.Size());
						static_cast<const FFieldNode<float> *>(Command.RootNode.Get())->Evaluate(Context, ResultsView);
					}
				}
				CommandsToRemove.Add(CommandIndex);
			}
#endif
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_CollisionGroup))
			{
				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<int32>::StaticType(),
					TEXT("Field based evaluation of the simulations 'CollisionGroup' parameter expects int field inputs.")))
				{
					FFieldSystemPhysicsProxy::ContiguousIndices(IndicesArray, CurrentSolver, ResolutionType, IndicesArray.Num() != Particles.Size());
					if (IndicesArray.Num())
					{
						TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());

						FVector * tptr = &(Particles.X(0));
						TArrayView<FVector> SamplesView(tptr, int32(Particles.Size()));

						FFieldContext Context{
							IndexView,
							SamplesView,
							Command.MetaData
						};

						int32 * cptr = &(Particles.CollisionGroup(0));
						TArrayView<int32> ResultsView(cptr, Particles.Size());
						static_cast<const FFieldNode<int32> *>(Command.RootNode.Get())->Evaluate(Context, ResultsView);
					}
				}
				CommandsToRemove.Add(CommandIndex);
			}
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_PositionStatic))
			{
				SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_PositionStatic);

#if TODO_REIMPLEMENT_FIELDS_TO_USE_PARTICLEHANDLES
				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<int32>::StaticType(),
					TEXT("Field based evaluation of the simulations 'Position' parameter expects integer field inputs.")))
				{
					FFieldSystemPhysicsProxy::ContiguousIndices(IndicesArray, CurrentSolver, ResolutionType, IndicesArray.Num() != Particles.Size());
					if (IndicesArray.Num())
					{
						TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());

						FVector * tptr = &(Particles.X(0));
						TArrayView<FVector> SamplesView(tptr, int32(Particles.Size()));

						FFieldContext Context{
							IndexView,
							SamplesView,
							Command.MetaData
						};

						TArray<int32> Results;
						Results.AddUninitialized(Particles.Size());
						for (const ContextIndex& Index : IndicesArray)
						{
							Results[Index.Sample] = false;
						}
						TArrayView<int32> ResultsView(&(Results[0]), Results.Num());
						static_cast<const FFieldNode<int32> *>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

						for (const ContextIndex& CIndex : IndicesArray)
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
									int32 Index = PositionTarget.Add(i, Particles.X(i));
									PositionTargetedParticles.Add(i, Index);
								}
							}
						}
					}
				}
#endif
				CommandsToRemove.Add(CommandIndex);
			}
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_PositionTarget))
			{
				SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_PositionTarget);

#if TODO_REIMPLEMENT_FIELDS_TO_USE_PARTICLEHANDLES

				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<FVector>::StaticType(),
					TEXT("Field based evaluation of the simulations 'PositionTarget' parameter expects vector field inputs.")))
				{
					FFieldSystemPhysicsProxy::ContiguousIndices(IndicesArray, CurrentSolver, ResolutionType, IndicesArray.Num() != Particles.Size());
					if (IndicesArray.Num())
					{
						TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());

						FVector * tptr = &(Particles.X(0));
						TArrayView<FVector> SamplesView(tptr, int32(Particles.Size()));

						FFieldContext Context{
							IndexView,
							SamplesView,
							Command.MetaData
						};

						TArray<FVector> Results;
						Results.AddUninitialized(Particles.Size());
						for (const ContextIndex& Index : IndicesArray)
						{
							Results[Index.Sample] = FVector(FLT_MAX);
						}
						TArrayView<FVector> ResultsView(&(Results[0]), Results.Num());
						static_cast<const FFieldNode<FVector> *>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

						for (const ContextIndex& CIndex : IndicesArray)
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
					}
				}
#endif

				CommandsToRemove.Add(CommandIndex);
			}
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_PositionAnimated))
			{
				SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_PositionAnimated);

				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<int32>::StaticType(),
					TEXT("Field based evaluation of the simulations 'Position' parameter expects integer field inputs.")))
				{
					FFieldSystemPhysicsProxy::ContiguousIndices(IndicesArray, CurrentSolver, ResolutionType, IndicesArray.Num() != Particles.Size());
					if (IndicesArray.Num())
					{
						TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());

						FVector * tptr = &(Particles.X(0));
						TArrayView<FVector> SamplesView(tptr, int32(Particles.Size()));

						FFieldContext Context{
							IndexView,
							SamplesView,
							Command.MetaData
						};

						TArray<int32> Results;
						Results.AddUninitialized(Particles.Size());
						for (const ContextIndex& Index : IndicesArray)
						{
							Results[Index.Sample] = false;
						}
						TArrayView<int32> ResultsView(&(Results[0]), Results.Num());
						static_cast<const FFieldNode<int32> *>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

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
				}
				CommandsToRemove.Add(CommandIndex);
			}
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_DynamicConstraint))
			{
				SCOPE_CYCLE_COUNTER(STAT_ParamUpdateField_DynamicConstraint);

				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<float>::StaticType(),
					TEXT("Field based evaluation of the simulations 'DynamicConstraint' parameter expects scalar field inputs.")))
				{
#if TODO_REIMPLEMENT_DYNAMIC_CONSTRAINT_ACCESSORS
					Chaos::TPBDRigidDynamicSpringConstraints<float, 3>& DynamicConstraints = FPhysicsSolver::FAccessor(CurrentSolver).DynamicConstraints();
					TSet<int32>& DynamicConstraintParticles = FPhysicsSolver::FAccessor(CurrentSolver).DynamicConstraintParticles();

					FFieldSystemPhysicsProxy::ContiguousIndices(IndicesArray, CurrentSolver, ResolutionType, IndicesArray.Num() != Particles.Size());
					if (IndicesArray.Num())
					{
						TArrayView<ContextIndex> IndexView(&(IndicesArray[0]), IndicesArray.Num());

						FVector * tptr = &(Particles.X(0));
						TArrayView<FVector> SamplesView(tptr, int32(Particles.Size()));

						FFieldContext Context{
							IndexView,
							SamplesView,
							Command.MetaData
						};

						TArray<float> Results;
						Results.AddUninitialized(Particles.Size());
						for (const ContextIndex& CIndex : IndicesArray)
						{
							Results[CIndex.Sample] = FLT_MAX;
						}
						TArrayView<float> ResultsView(&(Results[0]), Results.Num());
						static_cast<const FFieldNode<float> *>(Command.RootNode.Get())->Evaluate(Context, ResultsView);

						for (const ContextIndex& CIndex : IndicesArray)
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
		for (int32 Index = CommandsToRemove.Num() - 1; Index >= 0; --Index)
		{
			CommandListPtr->RemoveAt(CommandsToRemove[Index]);
		}
	}
}

void FFieldSystemPhysicsProxy::FieldForcesUpdateCallback(
	Chaos::FPhysicsSolver* InSolver, 
	FParticlesType& Particles, 
	Chaos::TArrayCollectionArray<FVector> & Force, 
	Chaos::TArrayCollectionArray<FVector> & Torque, 
	const float Time)
{
	if (Commands.Num() && InSolver)
	{
		Chaos::FPhysicsSolver* CurrentSolver = InSolver;
		TArray<ContextIndex> IndicesArray;

		TArray<FFieldSystemCommand>* CommandListPtr = GetSolverCommandList(InSolver);
		if(!CommandListPtr)
		{
			return;
		}

		TArray<int32> CommandsToRemove;
		const int32 NumCommands = CommandListPtr->Num();
		for(int32 CommandIndex = 0; CommandIndex < NumCommands; CommandIndex++)
		{
			const FFieldSystemCommand & Command = (*CommandListPtr)[CommandIndex];
			const EFieldResolutionType ResolutionType = 
				Command.HasMetaData(FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution) ?
					Command.GetMetaDataAs<FFieldSystemMetaDataProcessingResolution>(
						FFieldSystemMetaData::EMetaType::ECommandData_ProcessingResolution)->ProcessingResolution :
					EFieldResolutionType::Field_Resolution_Minimal;

			if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_LinearForce))
			{
				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<FVector>::StaticType(),
					TEXT("Field based evaluation of the simulations 'Force' parameter expects FVector field inputs.")))
				{
					TArray<Chaos::TGeometryParticleHandle<float,3>*> Handles;
					FFieldSystemPhysicsProxy::GetParticleHandles(Handles, CurrentSolver, ResolutionType);
					if (Handles.Num())
					{
						TArray<FVector> SamplePoints;
						TArray<ContextIndex> SampleIndices;
						SamplePoints.AddUninitialized(Handles.Num());
						SampleIndices.AddUninitialized(Handles.Num());
						for (int32 Idx = 0; Idx < Handles.Num(); ++Idx)
						{
							SamplePoints[Idx] = Handles[Idx]->X();
							SampleIndices[Idx] = ContextIndex(Idx, Idx);
						}

						TArrayView<FVector> SamplePointsView(&(SamplePoints[0]), SamplePoints.Num());
						TArrayView<ContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

						FFieldContext Context(
							SampleIndicesView,
							SamplePointsView,
							Command.MetaData);

						TArray<FVector> LocalForce;
						LocalForce.AddUninitialized(Handles.Num());					
						TArrayView<FVector> ForceView(&(LocalForce[0]), LocalForce.Num());
						static_cast<const FFieldNode<FVector> *>(Command.RootNode.Get())->Evaluate(Context, ForceView);
		
						int32 i = 0;
						for (Chaos::TGeometryParticleHandle<float, 3>* Handle : Handles)
						{
							Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = Handle->CastToRigidParticle();
							if(RigidHandle && RigidHandle->ObjectState() == Chaos::EObjectStateType::Dynamic)
							{
								RigidHandle->ExternalForce() += ForceView[i];
							}
							++i;
						}

#if TODO_REIMPLEMENT_WAKE_ISLANDS
						// @todo(ccaulfield): encapsulation: add WakeParticles (and therefore islands) functionality to Evolution
						TSet<int32> IslandsToActivate;
						for (const ContextIndex& CIndex : IndicesArray)
						{
							const int32 i = CIndex.Result;
							if (ForceView[i] != FVector(0) && Particles.ObjectState(i) == Chaos::EObjectStateType::Sleeping && !Particles.Disabled(i) && IslandsToActivate.Find(Particles.Island(i)) == nullptr)
							{
								IslandsToActivate.Add(Particles.Island(i));
							}
						}
						InSolver->WakeIslands(IslandsToActivate);
#endif
					}
				}
				CommandsToRemove.Add(CommandIndex);
			}
			else if (Command.TargetAttribute == GetFieldPhysicsName(EFieldPhysicsType::Field_AngularTorque))
			{
				if (ensureMsgf(Command.RootNode->Type() == FFieldNode<FVector>::StaticType(),
					TEXT("Field based evaluation of the simulations 'Torque' parameter expects FVector field inputs.")))
				{
					TArray<Chaos::TGeometryParticleHandle<float,3>*> Handles;
					FFieldSystemPhysicsProxy::GetParticleHandles(Handles, CurrentSolver, ResolutionType);
					if (Handles.Num())
					{
						TArray<FVector> SamplePoints;
						TArray<ContextIndex> SampleIndices;
						SamplePoints.AddUninitialized(Handles.Num());
						SampleIndices.AddUninitialized(Handles.Num());
						for (int32 Idx = 0; Idx < Handles.Num(); ++Idx)
						{
							SamplePoints[Idx] = Handles[Idx]->X();
							SampleIndices[Idx] = ContextIndex(Idx, Idx);
						}

						TArrayView<FVector> SamplePointsView(&(SamplePoints[0]), SamplePoints.Num());
						TArrayView<ContextIndex> SampleIndicesView(&(SampleIndices[0]), SampleIndices.Num());

						FFieldContext Context(
							SampleIndicesView,
							SamplePointsView,
							Command.MetaData);

						TArray<FVector> LocalTorque;
						LocalTorque.AddUninitialized(Handles.Num());					
						TArrayView<FVector> TorqueView(&(LocalTorque[0]), LocalTorque.Num());
						static_cast<const FFieldNode<FVector> *>(Command.RootNode.Get())->Evaluate(Context, TorqueView);
		
						int32 i = 0;
						for (Chaos::TGeometryParticleHandle<float, 3>* Handle : Handles)
						{
							Chaos::TPBDRigidParticleHandle<float, 3>* RigidHandle = Handle->CastToRigidParticle();
							if(RigidHandle && RigidHandle->ObjectState() == Chaos::EObjectStateType::Dynamic)
							{
								RigidHandle->ExternalTorque() += TorqueView[i];
							}
							++i;
						}

#if TODO_REIMPLEMENT_WAKE_ISLANDS
						// @todo(ccaulfield): encapsulation: add WakeParticles (and therefore islands) functionality to Evolution
						TSet<int32> IslandsToActivate;
						for (const ContextIndex& CIndex : IndicesArray)
						{
							const int32 i = CIndex.Result;
							if (TorqueView[i] != FVector(0) && Particles.ObjectState(i) == Chaos::EObjectStateType::Sleeping && !Particles.Disabled(i) && IslandsToActivate.Find(Particles.Island(i)) == nullptr)
							{
								IslandsToActivate.Add(Particles.Island(i));
							}
						}
						InSolver->WakeIslands(IslandsToActivate);
#endif
					}
				}
				CommandsToRemove.Add(CommandIndex);
			}
		}
		for (int32 Index = CommandsToRemove.Num() - 1; Index >= 0; --Index)
		{
			CommandListPtr->RemoveAt(CommandsToRemove[Index]);
		}
	}
}

void FFieldSystemPhysicsProxy::BufferCommand(Chaos::FPhysicsSolver* InSolver, const FFieldSystemCommand& InCommand)
{
	// TODO: Consider inspecting InCommand and bucketing according to which evaluation 
	// path it requires; FieldParameterUpdateCallback() or FieldForcesUpdateCallback().
	// TODO: Consider using a lock free triple buffer.

	TArray<FFieldSystemCommand>** ExistingList = nullptr;
	{
		FScopeLock Lock(&CommandLock);
		ExistingList = Commands.Find(InSolver);
		if(!ExistingList)
		{
			ExistingList = &Commands.Add(InSolver);
			//Commands.Add(InSolver);
			//ExistingList = Commands.Find(InSolver);
			check(ExistingList);
			(*ExistingList) = new TArray<FFieldSystemCommand>();
		}
	}
	(*ExistingList)->Add(InCommand);
}

void FFieldSystemPhysicsProxy::GetParticleHandles(
	TArray<Chaos::TGeometryParticleHandle<float,3>*>& Handles,
	const Chaos::FPhysicsSolver* RigidSolver,
	const EFieldResolutionType ResolutionType,
	const bool bForce)
{
	Handles.SetNum(0, false);
	if (!bForce)
		return;

	const Chaos::TPBDRigidsSOAs<float, 3>& SolverParticles = RigidSolver->GetParticles(); // const?

	if (ResolutionType == EFieldResolutionType::Field_Resolution_Maximum)
	{
		// const TParticleView<TGeometryParticles<T, d>>& TPBDRigidSOAs<T,d>::GetAllParticlesView()
		const Chaos::TParticleView<Chaos::TGeometryParticles<float, 3>> &ParticleView = 
			SolverParticles.GetAllParticlesView();
		Handles.Reserve(ParticleView.Num());

		// TParticleIterator<TSOA> Begin() const, TSOA = TGeometryParticles<T, d>
		for (Chaos::TParticleIterator<Chaos::TGeometryParticles<float, 3>> It = ParticleView.Begin(), ItEnd = ParticleView.End();
			It != ItEnd; ++It)
		{
			const Chaos::TTransientGeometryParticleHandle<float,3> *Handle = &(*It);
			// PBDRigidsSOAs.h only has a const version of GetAllParticlesView() - is that wrong?
			Handles.Add(GetHandleHelper(const_cast<Chaos::TTransientGeometryParticleHandle<float,3>*>(Handle)));
		}
	}
	else if (ResolutionType == EFieldResolutionType::Field_Resolution_Minimal)
	{
		const Chaos::TParticleView<Chaos::TGeometryParticles<float, 3>> &ParticleView = SolverParticles.GetNonDisabledView();
		Handles.Reserve(ParticleView.Num());
		for (Chaos::TParticleIterator<Chaos::TGeometryParticles<float, 3>> It = ParticleView.Begin(), ItEnd = ParticleView.End();
			It != ItEnd; ++It)
		{
			const Chaos::TTransientGeometryParticleHandle<float,3> *Handle = &(*It);
			Handles.Add(GetHandleHelper(const_cast<Chaos::TTransientGeometryParticleHandle<float,3>*>(Handle)));

		}
	}
	else if (ResolutionType == EFieldResolutionType::Field_Resolution_DisabledParents)
	{
		check(false); // unimplemented
	}
}

void FFieldSystemPhysicsProxy::ContiguousIndices(
	TArray<ContextIndex>& Array, 
	const Chaos::FPhysicsSolver* RigidSolver, 
	const EFieldResolutionType ResolutionType, 
	const bool bForce)
{
#if TODO_REIMPLEMENT_RIGID_CLUSTERING
	if (!bForce)
	{
		return;
	}

	if (ResolutionType == EFieldResolutionType::Field_Resolution_Minimal)
	{
		Array.SetNum(0, false);

		int32 IndexCount = 0;
		int32 FloorIndex = RigidSolver->GetFloorIndex();

#if TODO_REIMPLEMENT_RIGID_CLUSTERING
		const Chaos::FPhysicsSolver::FClusteringType& Clustering = RigidSolver->GetRigidClustering();
		const Chaos::TArrayCollectionArray<Chaos::ClusterId>& ClusterIdArray = Clustering.GetClusterIdsArray();
		const Chaos::FPhysicsSolver::FClusteringType::FClusterMap &  ClusterMap = Clustering.GetChildrenMap();
#endif // TODO_REIMPLEMENT_RIGID_CLUSTERING

		for (int32 ActiveParticleIndex : RigidSolver->NonDisabledIndices())
		{
			if (ClusterIdArray[ActiveParticleIndex].NumChildren)
			{
				for (uint32 ClusterChild : *ClusterMap[ActiveParticleIndex])
				{
					Array.Add(ContextIndex(ClusterChild, ClusterChild));
				}
			}

			if( ActiveParticleIndex != FloorIndex )
			{
				Array.Add(ContextIndex(ActiveParticleIndex, ActiveParticleIndex));
			}
		}
	}
	else if (ResolutionType == EFieldResolutionType::Field_Resolution_DisabledParents)
	{
		Array.SetNum(0, false);

		int32 IndexCount = 0;
		int32 FloorIndex = RigidSolver->GetFloorIndex();
		const Chaos::FPhysicsSolver::FClusteringType& Clustering = RigidSolver->GetRigidClustering();
		const Chaos::TArrayCollectionArray<Chaos::ClusterId>& ClusterIdArray = Clustering.GetClusterIdsArray();

		for (int32 TopLevelParent : Clustering.GetTopLevelClusterParents())
		{
			if( TopLevelParent != FloorIndex )
			{
				Array.Add(ContextIndex(TopLevelParent, TopLevelParent));
			}
		}
	}
	else if (ResolutionType == EFieldResolutionType::Field_Resolution_Maximum)
	{
		const Chaos::FPhysicsSolver::FParticlesType & Particles = RigidSolver->GetRigidParticles();
		Array.SetNum(Particles.Size());
		for (int32 i = 0; i < Array.Num(); ++i)
		{
			Array[i].Sample = i;
			Array[i].Result = i;
		}
	}
#endif
}

TArray<FFieldSystemCommand>* FFieldSystemPhysicsProxy::GetSolverCommandList(const Chaos::FPhysicsSolver* InSolver)
{
	FScopeLock Lock(&CommandLock);
	TArray<FFieldSystemCommand>** ExistingList = Commands.Find(InSolver);
	return ExistingList ? *ExistingList : nullptr;
}

void FFieldSystemPhysicsProxy::OnRemoveFromScene()
{
}
