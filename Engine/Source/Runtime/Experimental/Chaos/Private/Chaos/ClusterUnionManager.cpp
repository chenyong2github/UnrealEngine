// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/ClusterUnionManager.h"
#include "Chaos/ImplicitObjectUnion.h"
#include "Chaos/PBDRigidClustering.h"
#include "Chaos/PBDRigidClusteringAlgo.h"
#include "Chaos/PBDRigidsEvolution.h"
#include "Chaos/PBDRigidsEvolutionGBF.h"
#include "ChaosStats.h"

namespace Chaos
{
	FClusterUnionManager::FClusterUnionManager(FRigidClustering& InClustering, FPBDRigidsEvolutionGBF& InEvolution)
		: MClustering(InClustering)
		, MEvolution(InEvolution)
	{
	}

	DECLARE_CYCLE_STAT(TEXT("FClusterUnionManager::CreateNewClusterUnion"), STAT_CreateNewClusterUnion, STATGROUP_Chaos);
	FClusterUnionIndex FClusterUnionManager::CreateNewClusterUnion(const FClusterCreationParameters& Parameters, FClusterUnionExplicitIndex ExplicitIndex)
	{
		SCOPE_CYCLE_COUNTER(STAT_CreateNewClusterUnion);
		FClusterUnionIndex NewIndex = ClaimNextUnionIndex();
		check(NewIndex > 0);

		FClusterUnion NewUnion;
		NewUnion.ExplicitIndex = ExplicitIndex;
		NewUnion.SharedGeometry = ForceRecreateClusterUnionSharedGeometry(NewUnion);
		NewUnion.InternalCluster = MClustering.CreateClusterParticle(-NewIndex, {}, Parameters, NewUnion.SharedGeometry);
		NewUnion.Parameters = Parameters;
		if (ensure(NewUnion.InternalCluster != nullptr))
		{
			NewUnion.InternalCluster->SetInternalCluster(true);
		}

		ClusterUnions.Add(NewIndex, NewUnion);
		return NewIndex;
	}

	DECLARE_CYCLE_STAT(TEXT("FClusterUnionManager::ForceRecreateClusterUnionSharedGeometry"), STAT_ForceRecreateClusterUnionSharedGeometry, STATGROUP_Chaos);
	TSharedPtr<FImplicitObject, ESPMode::ThreadSafe> FClusterUnionManager::ForceRecreateClusterUnionSharedGeometry(const FClusterUnion& Union)
	{
		if (Union.ChildParticles.IsEmpty() || !Union.InternalCluster)
		{
			return MakeShared<FImplicitObjectUnionClustered>();
		}

		// TODO: Can we do something better than a union?

		const FRigidTransform3 ClusterWorldTM(Union.InternalCluster->X(), Union.InternalCluster->R());
		TArray<TUniquePtr<FImplicitObject>> Objects;
		TArray<FPBDRigidParticleHandle*> ChildParticleHandles;
		Objects.Reserve(Union.ChildParticles.Num());
		ChildParticleHandles.Reserve(Union.ChildParticles.Num());

		for (FPBDRigidParticleHandle* Child : Union.ChildParticles)
		{
			const FRigidTransform3 ChildWorldTM(Child->X(), Child->R());
			FRigidTransform3 Frame = ChildWorldTM.GetRelativeTransform(ClusterWorldTM);
			if (Child->Geometry())
			{
				Objects.Add(TUniquePtr<FImplicitObject>(new TImplicitObjectTransformed<FReal, 3>(Child->Geometry(), Frame)));
				ChildParticleHandles.Add(Child);
			}
		}

		return MakeShared<FImplicitObjectUnionClustered>(MoveTemp(Objects), ChildParticleHandles);
	}

	DECLARE_CYCLE_STAT(TEXT("FClusterUnionManager::ClaimNextUnionIndex"), STAT_ClaimNextUnionIndex, STATGROUP_Chaos);
	FClusterUnionIndex FClusterUnionManager::ClaimNextUnionIndex()
	{
		SCOPE_CYCLE_COUNTER(STAT_ClaimNextUnionIndex);
		if (ReusableIndices.IsEmpty())
		{
			return NextAvailableUnionIndex++;
		}
		else
		{
			return ReusableIndices.Pop(false);
		}
	}

	DECLARE_CYCLE_STAT(TEXT("FClusterUnionManager::AddPendingExplicitIndexOperation"), STAT_AddPendingExplicitIndexOperation, STATGROUP_Chaos);
	void FClusterUnionManager::AddPendingExplicitIndexOperation(FClusterUnionExplicitIndex Index, EClusterUnionOperation Op, const TArray<FPBDRigidParticleHandle*>& Particles)
	{
		SCOPE_CYCLE_COUNTER(STAT_AddPendingExplicitIndexOperation);
		AddPendingOperation(PendingExplicitIndexOperations, Index, Op, Particles);
	}

	DECLARE_CYCLE_STAT(TEXT("FClusterUnionManager::AddPendingClusterIndexOperation"), STAT_AddPendingClusterIndexOperation, STATGROUP_Chaos);
	void FClusterUnionManager::AddPendingClusterIndexOperation(FClusterUnionIndex Index, EClusterUnionOperation Op, const TArray<FPBDRigidParticleHandle*>& Particles)
	{
		SCOPE_CYCLE_COUNTER(STAT_AddPendingClusterIndexOperation);
		AddPendingOperation(PendingClusterIndexOperations, Index, Op, Particles);
	}

	DECLARE_CYCLE_STAT(TEXT("FClusterUnionManager::FlushPendingOperations"), STAT_FlushPendingOperations, STATGROUP_Chaos);
	void FClusterUnionManager::FlushPendingOperations()
	{
		SCOPE_CYCLE_COUNTER(STAT_FlushPendingOperations);
		if (PendingExplicitIndexOperations.IsEmpty() && PendingClusterIndexOperations.IsEmpty())
		{
			return;
		}

		// Go through every explicit index operation and convert them into a normal cluster index operation.
		// This could be made more efficient but shouldn't happen enough for it to really matter.
		for (const TPair<FClusterUnionExplicitIndex, FClusterOpMap>& OpMap : PendingExplicitIndexOperations)
		{
			const FClusterUnionIndex UnionIndex = GetOrCreateClusterUnionIndexFromExplicitIndex(OpMap.Key);
			for (const TPair<EClusterUnionOperation, TArray<FPBDRigidParticleHandle*>>& Op : OpMap.Value)
			{
				AddPendingClusterIndexOperation(UnionIndex, Op.Key, Op.Value);
			}
		}
		PendingExplicitIndexOperations.Empty();

		for (const TPair<FClusterUnionIndex, FClusterOpMap>& OpMap : PendingClusterIndexOperations)
		{
			for (const TPair<EClusterUnionOperation, TArray<FPBDRigidParticleHandle*>>& Op : OpMap.Value)
			{
				switch (Op.Key)
				{
				case EClusterUnionOperation::Add:
				case EClusterUnionOperation::AddReleased:
					HandleAddOperation(OpMap.Key, Op.Value, Op.Key == EClusterUnionOperation::AddReleased);
					break;
				}
			}
		}
		PendingClusterIndexOperations.Empty();
	}

	FClusterUnion* FClusterUnionManager::FindClusterUnionFromExplicitIndex(FClusterUnionExplicitIndex Index)
	{
		FClusterUnionIndex* ClusterIndex = ExplicitIndexMap.Find(Index);
		if (ClusterIndex == nullptr)
		{
			return nullptr;
		}

		return FindClusterUnion(*ClusterIndex);
	}

	FClusterUnion* FClusterUnionManager::FindClusterUnion(FClusterUnionIndex Index)
	{
		return ClusterUnions.Find(Index);
	}

	DECLARE_CYCLE_STAT(TEXT("FClusterUnionManager::HandleAddOperation"), STAT_HandleAddOperation, STATGROUP_Chaos);
	void FClusterUnionManager::HandleAddOperation(FClusterUnionIndex ClusterIndex, const TArray<FPBDRigidParticleHandle*>& Particles, bool bReleaseClustersFirst)
	{
		SCOPE_CYCLE_COUNTER(STAT_HandleAddOperation);
		FClusterUnion* Cluster = ClusterUnions.Find(ClusterIndex);
		if (!Cluster)
		{
			return;
		}

		TGuardValue_Bitfield_Cleanup<TFunction<void()>> Cleanup(
			[this, OldGenerateClusterBreaking=MClustering.GetDoGenerateBreakingData()]() {
				MClustering.SetGenerateClusterBreaking(OldGenerateClusterBreaking);
			}
		);
		MClustering.SetGenerateClusterBreaking(false);

		// If we're a new cluster, we need to determine whether to start the cluster in a sleeping or dynamic state.
		// Only stay sleeping if all the particles we add are also sleeping.
		const bool bIsNewCluster = Cluster->ChildParticles.IsEmpty();
		bool bIsSleeping = true;
		bool bIsAnchored = false;

		TArray<FPBDRigidParticleHandle*> FinalParticlesToAdd;
		FinalParticlesToAdd.Reserve(Particles.Num());

		// This is only relevant when bReleaseClustersFirst=true. This is used to be able to
		// properly notify the parent cluster about its child proxies.
		TMap<FPBDRigidParticleHandle*, FPBDRigidParticleHandle*> ChildToParentMap;

		for (FPBDRigidParticleHandle* Handle : Particles)
		{
			if (!Handle || Handle->Disabled())
			{
				continue;
			}

			bIsSleeping &= Handle->ObjectState() == EObjectStateType::Sleeping;

			if (FPBDRigidClusteredParticleHandle* ClusterHandle = Handle->CastToClustered(); ClusterHandle && bReleaseClustersFirst)
			{
				TSet<FPBDRigidParticleHandle*> Children = MClustering.ReleaseClusterParticles(ClusterHandle, true);
				FinalParticlesToAdd.Append(Children.Array());

				for (FPBDRigidParticleHandle* Child : Children)
				{
					ChildToParentMap.Add(Child, ClusterHandle);
				}

				bIsAnchored |= ClusterHandle->IsAnchored();
			}
			else
			{
				FinalParticlesToAdd.Add(Handle);
			}
		}

		if (FinalParticlesToAdd.IsEmpty())
		{
			return;
		}

		Cluster->ChildParticles.Append(FinalParticlesToAdd);

		// If a physics proxy was set already on the cluster we want to make sure that doesn't change.
		// This is needed to eventually be able to introduce a new physics proxy that gets attached to the
		// cluster union particle so that it can communicate with the game thread.
		IPhysicsProxyBase* OldProxy = Cluster->InternalCluster->PhysicsProxy();

		MClustering.AddParticlesToCluster(Cluster->InternalCluster, FinalParticlesToAdd, ChildToParentMap);

		// Update cluster properties.
		FMatrix33 ClusterInertia(0);

		// TODO: These functions are generally just re-building the cluster from scratch. Need to figure out a way
		// to get these functions to update the already existing cluster instead.
		TSet<FPBDRigidParticleHandle*> FullChildrenSet(FinalParticlesToAdd);

		const FRigidTransform3 ForceMassOrientation{ Cluster->InternalCluster->X(), Cluster->InternalCluster->R() };
		UpdateClusterMassProperties(Cluster->InternalCluster, FullChildrenSet, ClusterInertia, bIsNewCluster ? nullptr : &ForceMassOrientation);

		if (bIsNewCluster && bIsAnchored)
		{
			// The anchored flag is taken care of in UpdateKinematicProperties so it must be set before that is called.
			Cluster->InternalCluster->SetIsAnchored(true);
		}
		UpdateKinematicProperties(Cluster->InternalCluster, MClustering.GetChildrenMap(), MEvolution);

		// The recreation of the geometry must happen after the call to UpdateClusterMassProperties.
		// Creating the geometry requires knowing the relative frame between the parent cluster and the child clusters. The
		// parent transform is not set properly for a new empty cluster until UpdateClusterMassProperties is called for the first time.
		Cluster->SharedGeometry = ForceRecreateClusterUnionSharedGeometry(*Cluster);

		UpdateGeometry(Cluster->InternalCluster, FullChildrenSet, MClustering.GetChildrenMap(), Cluster->SharedGeometry, Cluster->Parameters);
		MClustering.GenerateConnectionGraph(Cluster->InternalCluster, Cluster->Parameters);

		if (OldProxy)
		{
			Cluster->InternalCluster->SetPhysicsProxy(OldProxy);
		}

		if (bIsNewCluster)
		{
			if (bIsSleeping)
			{
				MEvolution.SetParticleObjectState(Cluster->InternalCluster, Chaos::EObjectStateType::Sleeping);
			}

			MEvolution.SetPhysicsMaterial(Cluster->InternalCluster, MEvolution.GetPhysicsMaterial(FinalParticlesToAdd[0]));
		}
	}

	DECLARE_CYCLE_STAT(TEXT("FClusterUnionManager::GetOrCreateClusterUnionIndexFromExplicitIndex"), STAT_GetOrCreateClusterUnionIndexFromExplicitIndex, STATGROUP_Chaos);
	FClusterUnionIndex FClusterUnionManager::GetOrCreateClusterUnionIndexFromExplicitIndex(FClusterUnionExplicitIndex InIndex)
	{
		SCOPE_CYCLE_COUNTER(STAT_GetOrCreateClusterUnionIndexFromExplicitIndex);
		FClusterUnionIndex* OutIndex = ExplicitIndexMap.Find(InIndex);
		if (OutIndex != nullptr)
		{
			return *OutIndex;
		}

		FClusterUnionIndex NewIndex = CreateNewClusterUnion(DefaultClusterCreationParameters(), InIndex);
		ExplicitIndexMap.Add(InIndex, NewIndex);
		return NewIndex;
	}

	FClusterCreationParameters FClusterUnionManager::DefaultClusterCreationParameters() const
	{
		 FClusterCreationParameters Parameters{ 0.3f, 100, false, FRigidClustering::ShouldUnionsHaveCollisionParticles() };
		 Parameters.ConnectionMethod = MClustering.GetClusterUnionConnectionType();
		 return Parameters;
	}
}