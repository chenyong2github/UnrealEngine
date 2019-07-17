// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/PBDRigidsEvolutionGBF.h"

#include "Chaos/Box.h"
#include "Chaos/Defines.h"
#include "Chaos/Framework/Parallel.h"
#include "Chaos/ImplicitObjectTransformed.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/PBDCollisionConstraint.h"
#include "Chaos/PBDCollisionSpringConstraints.h"
#include "Chaos/PerParticleEtherDrag.h"
#include "Chaos/PerParticleEulerStepVelocity.h"
#include "Chaos/PerParticleGravity.h"
#include "Chaos/PerParticleInitForce.h"
#include "Chaos/PerParticlePBDEulerStep.h"
#include "Chaos/PerParticlePBDGroundConstraint.h"
#include "Chaos/PerParticlePBDUpdateFromDeltaPosition.h"
#include "ChaosStats.h"

#include "ProfilingDebugging/ScopedTimers.h"
#include "Chaos/DebugDrawQueue.h"
#include "Chaos/Levelset.h"
#include "Chaos/ChaosPerfTest.h"

#define LOCTEXT_NAMESPACE "Chaos"

int32 DisableSim = 0;
FAutoConsoleVariableRef CVarDisableSim(TEXT("p.DisableSim"), DisableSim, TEXT("Disable Sim"));

using namespace Chaos;

template<class T, int d>
TPBDRigidsEvolutionGBF<T, d>::TPBDRigidsEvolutionGBF(TPBDRigidParticles<T, d>&& InParticles, int32 InNumIterations)
    : Base(MoveTemp(InParticles), InNumIterations)
	, CollisionConstraints(Particles, NonDisabledIndices, Collided, PhysicsMaterials, DefaultNumPushOutPairIterations, (T)0)
	, CollisionRule(CollisionConstraints, DefaultNumPushOutIterations)
{
	SetParticleUpdateVelocityFunction([PBDUpdateRule = TPerParticlePBDUpdateFromDeltaPosition<float, 3>(), this](TPBDRigidParticles<T, d>& ParticlesInput, const T Dt, const TArray<int32>& InActiveIndices) {
		PhysicsParallelFor(InActiveIndices.Num(), [&](int32 ActiveIndex) {
			int32 Index = InActiveIndices[ActiveIndex];
			PBDUpdateRule.Apply(ParticlesInput, Dt, Index);
		});
	});

	SetParticleUpdatePositionFunction([this](TPBDRigidParticles<T, d>& ParticlesInput, const T Dt)
	{
		const TArray<int32>& ActiveIndicesArray = GetActiveIndicesArray();
		PhysicsParallelFor(ActiveIndicesArray.Num(), [&](int32 ActiveIndex)
		{
			int32 Index = ActiveIndicesArray[ActiveIndex];
			ParticlesInput.X(Index) = ParticlesInput.P(Index);
			ParticlesInput.R(Index) = ParticlesInput.Q(Index);
		});
	});

	AddConstraintRule(&CollisionRule);
}

DECLARE_CYCLE_STAT(TEXT("Integrate"), STAT_Integrate, STATGROUP_Chaos);

float HackMaxAngularVelocity = 1000.f;
FAutoConsoleVariableRef CVarHackMaxAngularVelocity(TEXT("p.HackMaxAngularVelocity"), HackMaxAngularVelocity, TEXT("Max cap on angular velocity: rad/s. This is only a temp solution and should not be relied on as a feature. -1.f to disable"));

float HackMaxVelocity = -1.f;
FAutoConsoleVariableRef CVarHackMaxVelocity(TEXT("p.HackMaxVelocity"), HackMaxVelocity, TEXT("Max cap on velocity: cm/s. This is only a temp solution and should not be relied on as a feature. -1.f to disable"));


float HackLinearDrag = 0.f;
FAutoConsoleVariableRef CVarHackLinearDrag(TEXT("p.HackLinearDrag"), HackLinearDrag, TEXT("Linear drag used to slow down objects. This is a hack and should not be relied on as a feature."));

float HackAngularDrag = 0.f;
FAutoConsoleVariableRef CVarHackAngularDrag(TEXT("p.HackAngularDrag"), HackAngularDrag, TEXT("Angular drag used to slow down objects. This is a hack and should not be relied on as a feature."));

int DisableThreshold = 5;
FAutoConsoleVariableRef CVarDisableThreshold(TEXT("p.DisableThreshold"), DisableThreshold, TEXT("Disable threshold frames to transition to sleeping"));

template <typename T, int d>
void TPBDRigidsEvolutionGBF<T, d>::Integrate(const TArray<int32>& InActiveIndices, T Dt)
{
	SCOPE_CYCLE_COUNTER(STAT_Integrate);
	CHAOS_SCOPED_TIMER(Integrate);
	double TimerTime = 0.0;
	FDurationTimer Timer(TimerTime);
	TPerParticleInitForce<T, d> InitForceRule;
	TPerParticleEulerStepVelocity<T, d> EulerStepVelocityRule;
	TPerParticleEtherDrag<T, d> EtherDragRule(HackLinearDrag, HackAngularDrag);
	TPerParticlePBDEulerStep<T, d> EulerStepRule;
	Timer.Stop();
	UE_LOG(LogChaos, Verbose, TEXT("Init Time is %f"), TimerTime);

	TimerTime = 0;
	Timer.Start();
	const T MaxAngularSpeed2 = HackMaxAngularVelocity * HackMaxAngularVelocity;
	const T MaxSpeed2 = HackMaxVelocity * HackMaxVelocity;
	PhysicsParallelFor(InActiveIndices.Num(), [&](int32 ActiveIndex) {
		int32 Index = InActiveIndices[ActiveIndex];
		if (ensure(!Particles.Disabled(Index) && !Particles.Sleeping(Index)))
		{
			//save off previous velocities
			Particles.PreV(Index) = Particles.V(Index);
			Particles.PreW(Index) = Particles.W(Index);

			InitForceRule.Apply(Particles, Dt, Index);
			for (FForceRule ForceRule : ForceRules)
			{
				ForceRule(Particles, Dt, Index);
			}
			EulerStepVelocityRule.Apply(Particles, Dt, Index);
			EtherDragRule.Apply(Particles, Dt, Index);

			if (HackMaxAngularVelocity >= 0.f)
			{
				const T AngularSpeed2 = Particles.W(Index).SizeSquared();
				if (AngularSpeed2 > MaxAngularSpeed2)
				{
					Particles.W(Index) = Particles.W(Index) * (HackMaxAngularVelocity / FMath::Sqrt(AngularSpeed2));
				}
			}

			if (HackMaxVelocity >= 0.f)
			{
				const T Speed2 = Particles.V(Index).SizeSquared();
				if (Speed2 > MaxSpeed2)
				{
					Particles.V(Index) = Particles.V(Index) * (HackMaxVelocity / FMath::Sqrt(Speed2));
				}
			}


			EulerStepRule.Apply(Particles, Dt, Index);
		}
	});
	Timer.Stop();
	UE_LOG(LogChaos, Verbose, TEXT("Per ParticleUpdate Time is %f"), TimerTime);
}

DECLARE_CYCLE_STAT(TEXT("AdvanceOneTimestep"), STAT_AdvanceOneTimeStep, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("UpdateContactGraph"), STAT_UpdateContactGraph, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("Apply+PushOut"), STAT_ApplyApplyPushOut, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("ParticleUpdateVelocity"), STAT_ParticleUpdateVelocity, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("SleepInactive"), STAT_SleepInactive, STATGROUP_Chaos);
DECLARE_CYCLE_STAT(TEXT("ParticleUpdatePosition"), STAT_ParticleUpdatePosition, STATGROUP_Chaos);
DECLARE_DWORD_COUNTER_STAT(TEXT("NumActiveParticles"), STAT_NumActiveParticles, STATGROUP_Chaos);
DECLARE_DWORD_COUNTER_STAT(TEXT("NumActiveConstraints"), STAT_NumActiveConstraints, STATGROUP_Chaos);

int32 SelectedParticle = 1;
FAutoConsoleVariableRef CVarSelectedParticle(TEXT("p.SelectedParticle"), SelectedParticle, TEXT("Debug render for a specific particle"));
int32 ShowCollisionParticles = 0;
FAutoConsoleVariableRef CVarShowCollisionParticles(TEXT("p.ShowCollisionParticles"), ShowCollisionParticles, TEXT("Debug render the collision particles (can be very slow)"));
int32 ShowCenterOfMass = 1;
FAutoConsoleVariableRef CVarShowCenterOfMass(TEXT("p.ShowCenterOfMass"), ShowCenterOfMass, TEXT("Debug render of the center of mass, you will likely need wireframe mode on"));
int32 ShowClusterConnections = 1;
FAutoConsoleVariableRef CVarShowClusterConnections(TEXT("p.ShowClusterConnections"), ShowClusterConnections, TEXT("Debug render of the cluster connections"));
int32 ShowBounds = 1;
FAutoConsoleVariableRef CVarShowBounds(TEXT("p.ShowBounds"), ShowBounds, TEXT(""));
int32 ShowLevelSet = 0;
FAutoConsoleVariableRef CVarShowLevelSet(TEXT("p.ShowLevelSet"), ShowLevelSet, TEXT(""));
float MaxVisualizePhiDistance = 10.f;
FAutoConsoleVariableRef CVarMaxPhiDistance(TEXT("p.MaxVisualizePhiDistance"), MaxVisualizePhiDistance, TEXT(""));
float CullPhiVisualizeDistance = 0.f;
FAutoConsoleVariableRef CVarCullPhiDistance(TEXT("p.CullPhiVisualizeDistance"), CullPhiVisualizeDistance, TEXT(""));

int32 GatherVerbosePhysicsStats = 0;
FAutoConsoleVariableRef CVarGatherVerbosePhysicsStats(TEXT("p.GatherVerbosePhysicsStats"), GatherVerbosePhysicsStats, TEXT("If enabled, stat ChaosDedicated will show detailed stats that are more expensive to gather"));

template<class T, int d>
void TPBDRigidsEvolutionGBF<T, d>::AdvanceOneTimeStep(const T Dt)
{
	if (DisableSim)
	{
		return;
	}
	SCOPE_CYCLE_COUNTER(STAT_AdvanceOneTimeStep);

	UE_LOG(LogChaos, Verbose, TEXT("START FRAME with Dt %f"), Dt);
	Integrate(GetActiveIndicesArray(), Dt);
	GetDebugSubstep().Add(TEXT("TPBDRigidsEvolutionGBF::AdvanceOneTimeStep(): After Integrate()"));

	SET_DWORD_STAT(STAT_NumActiveParticles, ActiveIndices.Num());

	UpdateConstraintPositionBasedState(Dt);
	CreateConstraintGraph();
	CreateIslands();

	TArray<bool> SleepedIslands;
	SleepedIslands.SetNum(ConstraintGraph.NumIslands());
	TArray<TArray<int32>> DisabledParticles;
	DisabledParticles.SetNum(ConstraintGraph.NumIslands());
	{
		SCOPE_CYCLE_COUNTER(STAT_ApplyApplyPushOut);
		CHAOS_SCOPED_TIMER(ApplyApplyPushOut);
		PhysicsParallelFor(ConstraintGraph.NumIslands(), [&](int32 Island) {
			const TArray<int32>& IslandParticleIndices = ConstraintGraph.GetIslandParticles(Island);

			ApplyConstraints(Dt, Island);

			UpdateVelocities(Dt, Island);
			
			ApplyPushOut(Dt, Island);

			for (const int32 Index : IslandParticleIndices)
			{
				// If a dynamic particle is moving slowly enough for long enough, disable it.
				// @todo(mlentine): Find a good way of not doing this when we aren't using this functionality

				// increment the disable count for the particle
				if (Particles.ObjectState(Index) != EObjectStateType::Kinematic && Particles.ObjectState(Index) != EObjectStateType::Static && PhysicsMaterials[Index] && Particles.V(Index).SizeSquared() < PhysicsMaterials[Index]->DisabledLinearThreshold && Particles.W(Index).SizeSquared() < PhysicsMaterials[Index]->DisabledAngularThreshold)
				{
					++ParticleDisableCount[Index];
				}

				// check if we're over the disable count threshold
				if (ParticleDisableCount[Index] > DisableThreshold)
				{
					ParticleDisableCount[Index] = 0;
					Particles.SetDisabledLowLevel(Index, true);
					DisabledParticles[Island].Add(Index);
					Particles.V(Index) = TVector<T, d>(0);
					Particles.W(Index) = TVector<T, d>(0);
				}

				if (!(ensure(!FMath::IsNaN(Particles.P(Index)[0])) && ensure(!FMath::IsNaN(Particles.P(Index)[1])) && ensure(!FMath::IsNaN(Particles.P(Index)[2]))))
				{
					Particles.SetDisabledLowLevel(Index, true);
					DisabledParticles[Island].Add(Index);
				}
			}

			// Turn off if not moving
			SleepedIslands[Island] = ConstraintGraph.SleepInactive(Particles, Island, PhysicsMaterials);
		});

		GatherStats();

		for (int32 Island = 0; Island < ConstraintGraph.NumIslands(); ++Island)
		{
			if (SleepedIslands[Island])
			{
				for (const int32 Index : ConstraintGraph.GetIslandParticles(Island))
				{
					ActiveIndices.Remove(Index);
				}
			}
			for (const int32 Index : DisabledParticles[Island])
			{
				ActiveIndices.Remove(Index);
				NonDisabledIndices.Remove(Index);
			}
		}

		GetDebugSubstep().Add(TEXT("TPBDRigidsEvolutionGBF::AdvanceOneTimeStep(): Before AdvanceClustering"));
		Clustering.AdvanceClustering(Dt, GetCollisionConstraints());
		GetDebugSubstep().Add(TEXT("TPBDRigidsEvolutionGBF::AdvanceOneTimeStep(): After AdvanceClustering"));

		{
			SCOPE_CYCLE_COUNTER(STAT_ParticleUpdatePosition);
			ParticleUpdatePosition(Particles, Dt);
		}

		Time += Dt;
	}
}

template<class T, int d>
void TPBDRigidsEvolutionGBF<T, d>::GatherStats()
{
	EvolutionStats.Reset();
	if (GatherVerbosePhysicsStats)
	{
		auto GatherLambda = [&](const TArray<int32>& Indices, const TArray<int32>& Count, int32& NumCollisionParticles, int32& NumShapes)
		{
			for (int32 i = 0; i < Indices.Num(); ++i)
			{
				const int32 Index = Indices[i];
				if (Particles.Geometry(Index))
				{
					if (Particles.Geometry(Index)->IsUnderlyingUnion())
					{
						const bool bUseSubCollision = Particles.CollisionParticlesSize(Index) == 0;
						const TImplicitObjectUnion<T, d>* Union = static_cast<const TImplicitObjectUnion<T, d>*>(Particles.Geometry(Index).Get());
						const TArray<TUniquePtr<TImplicitObject<T, d>>>& SubObjects = Union->GetObjects();
						NumShapes += SubObjects.Num();
						if (bUseSubCollision)
						{
							for (const TUniquePtr <TImplicitObject<T, d>>& Obj : SubObjects)
							{
								if (ensure(Obj->GetType() == ImplicitObjectType::Transformed))
								{
									const TImplicitObjectTransformed<T, d>* Transformed = static_cast<const TImplicitObjectTransformed<T, d>*>(Obj.Get());

									const int32 OriginalIdx = Union->MCollisionParticleLookupHack[Transformed->GetTransformedObject()];
									NumCollisionParticles += Particles.CollisionParticlesSize(OriginalIdx) * Count[i];
								}
							}
						}
						else
						{
							NumCollisionParticles += Particles.CollisionParticlesSize(Index) * Count[i];
						}
					}
					else
					{
						NumShapes++;
						NumCollisionParticles += Particles.CollisionParticlesSize(Index) * Count[i];
					}
				}
				else
				{
					NumCollisionParticles += Particles.CollisionParticlesSize(Index) * Count[i];
				}
			}
		};

		const TArray<int32>& ActiveIndicesArray = GetActiveIndicesArray();
		TArray<int32> Ones;
		Ones.Init(1, ActiveIndicesArray.Num());
		GatherLambda(ActiveIndicesArray, Ones, EvolutionStats.ActiveCollisionPoints, EvolutionStats.ActiveShapes);
	}
}

template<class T, int d>
void TPBDRigidsEvolutionGBF<T, d>::DebugDraw()
{
#if CHAOS_DEBUG_DRAW
	if (FDebugDrawQueue::IsDebugDrawingEnabled())
	{
		if (1)
		{
			if (ShowClusterConnections)
			{
				for (const typename FRigidClustering::FClusterMap::ElementType& Cluster : Clustering.GetChildrenMap())
				{
					if (!Particles.Disabled(Cluster.Key))
					{
						for (const int32 ChildIdx : *Cluster.Value)
						{
							const FString Text = FString::Printf(TEXT("%d"), ChildIdx);
							for (const TConnectivityEdge<T>& Edge : Clustering.GetConnectivityEdges()[ChildIdx])
							{
								FDebugDrawQueue::GetInstance().DrawDebugLine(Particles.X(ChildIdx), Particles.X(Edge.Sibling), FColor::Blue, false, 1e-4, 0, 2.f);
							}
						}
					}
				}
			}

			for (uint32 Idx = 0; Idx < Particles.Size(); ++Idx)
			{
				if (Particles.Disabled(Idx)) { continue; }
				if (ShowCollisionParticles && (SelectedParticle == Idx || ShowCollisionParticles == -1))
				{
					if (Particles.CollisionParticles(Idx))
					{
						bool bDrawUnionCollision = Particles.Geometry(Idx) && Particles.Geometry(Idx)->IsUnderlyingUnion() && Particles.CollisionParticlesSize(Idx) == 0;
						if (bDrawUnionCollision)
						{
							const TImplicitObjectUnion<T, d>* Union = Particles.Geometry(Idx)->template GetObject<const TImplicitObjectUnion<T, d>>();
							const TArray<TUniquePtr<TImplicitObject<T, d>>>& SubObjects = Union->GetObjects();
							for (const TUniquePtr <TImplicitObject<T, d>>& Obj : SubObjects)
							{
								if (ensure(Obj->GetType() == ImplicitObjectType::Transformed))
								{
									const TImplicitObjectTransformed<T, d>* Transformed = static_cast<const TImplicitObjectTransformed<T, d>*>(Obj.Get());

									const int32 OriginalIdx = Union->MCollisionParticleLookupHack[Transformed->GetTransformedObject()];
									for (uint32 CollisionIdx = 0; CollisionIdx < Particles.CollisionParticles(OriginalIdx)->Size(); ++CollisionIdx)
									{
										const TVector<T, d>& X = Particles.CollisionParticles(OriginalIdx)->X(CollisionIdx);
										const TVector<T, d> WorldX = (Transformed->GetTransform()* TRigidTransform<T, d>(Particles.X(Idx), Particles.R(Idx))).TransformPosition(X);
										FDebugDrawQueue::GetInstance().DrawDebugPoint(WorldX, FColor::Purple, false, 1e-4, 0, 10.f);
									}
								}
							}
						}
						else
						{
							for (uint32 CollisionIdx = 0; CollisionIdx < Particles.CollisionParticles(Idx)->Size(); ++CollisionIdx)
							{
								const TVector<T, d>& X = Particles.CollisionParticles(Idx)->X(CollisionIdx);
								const TVector<T, d> WorldX = TRigidTransform<T, d>(Particles.X(Idx), Particles.R(Idx)).TransformPosition(X);
								FDebugDrawQueue::GetInstance().DrawDebugPoint(WorldX, FColor::Purple, false, 1e-4, 0, 10.f);
							}
						}
					}
				}

				if (ShowCenterOfMass && (SelectedParticle == Idx || ShowCenterOfMass == -1))
				{
					FColor AxisColors[] = { FColor::Red, FColor::Green, FColor::Blue };
					T MaxInertia = KINDA_SMALL_NUMBER;
					for (int i = 0; i < d; ++i)
					{
						MaxInertia = FMath::Max(Particles.I(Idx).M[i][i], MaxInertia);
					}

					for (int i = 0; i < d; ++i)
					{
						const TVector<T, d> WorldDir = Particles.R(Idx) * TVector<T, d>::AxisVector(i) * 100 * Particles.I(Idx).M[i][i] / MaxInertia;
						FDebugDrawQueue::GetInstance().DrawDebugDirectionalArrow(Particles.X(Idx), Particles.X(Idx) + WorldDir, 3, AxisColors[i], false, 1e-4, 0, 2.f);

					}
					FDebugDrawQueue::GetInstance().DrawDebugSphere(Particles.X(Idx), 20.f, 16, FColor::Yellow, false, 1e-4);
				}

				if (ShowBounds && (SelectedParticle == Idx || ShowBounds == -1) && Particles.Geometry(Idx)->HasBoundingBox())
				{
					const TBox<T, d>& Bounds = Particles.Geometry(Idx)->BoundingBox();
					const TRigidTransform<T, d> TM(Particles.X(Idx), Particles.R(Idx));
					const TVector<T, d> Center = TM.TransformPosition(Bounds.Center());
					FDebugDrawQueue::GetInstance().DrawDebugBox(Center, Bounds.Extents() * 0.5f, TM.GetRotation(), FColor::Yellow, false, 1e-4, 0, 2.f);
				}

				if (ShowLevelSet && (SelectedParticle == Idx || ShowLevelSet == -1))
				{
					auto RenderLevelSet = [](const TRigidTransform<T, d>& LevelSetToWorld, const TLevelSet<T, d>& LevelSet)
					{
						const TUniformGrid<T, d>& Grid = LevelSet.GetGrid();
						const int32 NumCells = Grid.GetNumCells();
						const TArrayND<T, 3>& PhiArray = LevelSet.GetPhiArray();
						for (int32 CellIdx = 0; CellIdx < NumCells; ++CellIdx)
						{
							const TVector<T, d> GridSpaceLocation = Grid.Center(CellIdx);
							const TVector<T, d> WorldSpaceLocation = LevelSetToWorld.TransformPosition(GridSpaceLocation);
							const T Phi = PhiArray(Grid.GetIndex(CellIdx));
							//FDebugDrawQueue::GetInstance().DrawDebugPoint(WorldSpaceLocation, Phi > 0 ? FColor::Purple : FColor::Green, false, 1e-4, 0, 10.f);
							//FDebugDrawQueue::GetInstance().DrawDebugSphere(WorldSpaceLocation, Grid.Dx().GetAbsMin(), 16, Phi > 0 ? FColor::Purple : FColor::Green, false, 1e-4);
							if (Phi <= CullPhiVisualizeDistance)
							{
								const T LocalPhi = Phi - CullPhiVisualizeDistance;
								const T MaxPhi = (-LocalPhi / MaxVisualizePhiDistance) * 255;
								const uint8 MaxPhiInt = MaxPhi > 255 ? 255 : MaxPhi;
								FDebugDrawQueue::GetInstance().DrawDebugPoint(WorldSpaceLocation, FColor(255, MaxPhiInt, 255, 255), false, 1e-4, 0, 30.f);
							}
						}
					};

					if (const TLevelSet<T, d>* LevelSet = Particles.Geometry(Idx)->template GetObject<TLevelSet<T, d>>())
					{
						RenderLevelSet(TRigidTransform<T, d>(Particles.X(Idx), Particles.R(Idx)), *LevelSet);
					}
					else if (const TImplicitObjectTransformed<T, d>* Transformed = Particles.Geometry(Idx)->template GetObject<TImplicitObjectTransformed<T, d>>())
					{
						if (const TLevelSet<T, d>* InnerLevelSet = Transformed->GetTransformedObject()->template GetObject<TLevelSet<T, d>>())
						{
							RenderLevelSet(Transformed->GetTransform() * TRigidTransform<T, d>(Particles.X(Idx), Particles.R(Idx)), *InnerLevelSet);
						}
					}
				}
			}
		}
		FDebugDrawQueue::GetInstance().Flush();
	}
#endif
}

template class Chaos::TPBDRigidsEvolutionGBF<float, 3>;

#undef LOCTEXT_NAMESPACE
