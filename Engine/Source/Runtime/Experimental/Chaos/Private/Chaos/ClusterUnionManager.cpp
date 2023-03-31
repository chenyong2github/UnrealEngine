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
	FClusterUnionIndex FClusterUnionManager::CreateNewClusterUnion(const FClusterCreationParameters& Parameters, const FClusterUnionCreationParameters& ClusterUnionParameters)
	{
		SCOPE_CYCLE_COUNTER(STAT_CreateNewClusterUnion);
		FClusterUnionIndex NewIndex = ClaimNextUnionIndex();
		check(NewIndex > 0);

		FClusterUnion NewUnion;
		NewUnion.ExplicitIndex = ClusterUnionParameters.ExplicitIndex;
		NewUnion.SharedGeometry = ForceRecreateClusterUnionSharedGeometry(NewUnion);
		NewUnion.InternalCluster = MClustering.CreateClusterParticle(-NewIndex, {}, Parameters, NewUnion.SharedGeometry, nullptr, ClusterUnionParameters.UniqueIndex);
		NewUnion.Parameters = Parameters;
		NewUnion.ClusterUnionParameters = ClusterUnionParameters;

		// Some parameters aren't relevant after creation.
		NewUnion.ClusterUnionParameters.UniqueIndex = nullptr;


		if (ensure(NewUnion.InternalCluster != nullptr))
		{
			NewUnion.InternalCluster->SetInternalCluster(true);
			if (AccelerationStructureSplitStaticAndDynamic == 1)
			{
				NewUnion.InternalCluster->SetSpatialIdx(FSpatialAccelerationIdx{ 0, 1 });
			}
			else
			{
				NewUnion.InternalCluster->SetSpatialIdx(FSpatialAccelerationIdx{ 0, 0 });
			}
			// No bounds for now since we don't have particles. When/if we do get particles later, updating the
			// geometry should switch this flag back on.
			NewUnion.InternalCluster->SetHasBounds(false);
		}
		MEvolution.DisableParticle(NewUnion.InternalCluster);

		ClusterUnions.Add(NewIndex, NewUnion);
		ParticleToClusterUnionIndex.Add(NewUnion.InternalCluster, NewIndex);
		return NewIndex;
	}

	DECLARE_CYCLE_STAT(TEXT("FClusterUnionManager::DestroyClusterUnion"), STAT_DestroyClusterUnion, STATGROUP_Chaos);
	void FClusterUnionManager::DestroyClusterUnion(FClusterUnionIndex Index)
	{
		SCOPE_CYCLE_COUNTER(STAT_DestroyClusterUnion);

		if (FClusterUnion* ClusterUnion = FindClusterUnion(Index))
		{
			// Need to actually remove the particles and set them back into a simulatable state.
			// We need a clean removal here just in case the cluster union is actually being destroyed 
			// on the game thread prior to its children (which would live on another actor).
			// 
			// Note that we need to make a copy of the array here since the children list will be modified by the HandleRemoveOperation.
			// However, the function does not expect that the input array will change.
			TArray<FPBDRigidParticleHandle*> ChildrenCopy = ClusterUnion->ChildParticles;
			HandleRemoveOperation(Index, ChildrenCopy, false);
			ClusterUnion->ChildParticles.Empty();
			MClustering.DestroyClusterParticle(ClusterUnion->InternalCluster);

			if (ClusterUnion->ExplicitIndex != INDEX_NONE)
			{
				ExplicitIndexMap.Remove(ClusterUnion->ExplicitIndex);
				PendingExplicitIndexOperations.Remove(ClusterUnion->ExplicitIndex);
			}
			ReusableIndices.Add(Index);
			PendingClusterIndexOperations.Remove(Index);
			ClusterUnions.Remove(Index);
		}
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
		Objects.Reserve(Union.ChildParticles.Num());

		for (FPBDRigidParticleHandle* Child : Union.ChildParticles)
		{
			FRigidTransform3 Frame = FRigidTransform3::Identity;
			
			if (FPBDRigidClusteredParticleHandle* ClusterChild = Child->CastToClustered(); ClusterChild && Union.Parameters.bUseExistingChildToParent)
			{
				Frame = ClusterChild->ChildToParent();
			}
			else
			{
				const FRigidTransform3 ChildWorldTM(Child->X(), Child->R());
				Frame = ChildWorldTM.GetRelativeTransform(ClusterWorldTM);
			}

			if (Child->Geometry())
			{
				Objects.Add(TUniquePtr<FImplicitObject>(new TImplicitObjectTransformed<FReal, 3>(Child->Geometry(), Frame)));
			}
		}

		return MakeShared<FImplicitObjectUnion>(MoveTemp(Objects));
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
				case EClusterUnionOperation::Remove:
					HandleRemoveOperation(OpMap.Key, Op.Value, true);
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

		// If we're adding particles to a cluster we need to first make sure they're not part of any other cluster.
		// Book-keeping might get a bit odd if we try to add a particle to a new clutser and then only later remove the particle from its old cluster.
		HandleRemoveOperationWithClusterLookup(Particles, true);

		TGuardValue_Bitfield_Cleanup<TFunction<void()>> Cleanup(
			[this, OldGenerateClusterBreaking=MClustering.GetDoGenerateBreakingData()]() {
				MClustering.SetGenerateClusterBreaking(OldGenerateClusterBreaking);
			}
		);
		MClustering.SetGenerateClusterBreaking(false);

		// If a physics proxy was set already on the cluster we want to make sure that doesn't change.
		// This is needed to eventually be able to introduce a new physics proxy that gets attached to the
		// cluster union particle so that it can communicate with the game thread.
		IPhysicsProxyBase* OldProxy = Cluster->InternalCluster->PhysicsProxy();
		EPhysicsProxyType OldProxyType = OldProxy ? OldProxy->GetType() : EPhysicsProxyType::NoneType;

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

			if (FPBDRigidClusteredParticleHandle* ClusterHandle = Handle->CastToClustered())
			{
				if (bReleaseClustersFirst)
				{
					TSet<FPBDRigidParticleHandle*> Children = MClustering.ReleaseClusterParticles(ClusterHandle, true);
					FinalParticlesToAdd.Append(Children.Array());

					for (FPBDRigidParticleHandle* Child : Children)
					{
						ChildToParentMap.Add(Child, ClusterHandle);
					}
				}
				else
				{
					FinalParticlesToAdd.Add(Handle);
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
		for (FPBDRigidParticleHandle* Particle : FinalParticlesToAdd)
		{
			ParticleToClusterUnionIndex.Add(Particle, ClusterIndex);
		}

		MClustering.AddParticlesToCluster(Cluster->InternalCluster, FinalParticlesToAdd, ChildToParentMap);

		// For all the particles that have been added to the cluster we need to set their parent proxy to the
		// cluster's proxy if it exists. We need the proxy type check because for non-cluster union proxy backed unions,
		// the cluster union's proxy will be the proxy of the most recently added particle.
		if (OldProxy && OldProxyType == EPhysicsProxyType::ClusterUnionProxy)
		{
			for (FPBDRigidParticleHandle* Particle : FinalParticlesToAdd)
			{
				if (Particle && Particle->PhysicsProxy())
				{
					Particle->PhysicsProxy()->SetParentProxy(OldProxy);
				}
			}
		}

		if (bIsNewCluster && bIsAnchored)
		{
			// The anchored flag is taken care of in UpdateKinematicProperties so it must be set before that is called.
			Cluster->InternalCluster->SetIsAnchored(true);
		}
		UpdateAllClusterUnionProperties(*Cluster, bIsNewCluster);

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
			Cluster->bNeedsXRInitialization = false;
		}

		if (Cluster->InternalCluster->Disabled())
		{
			MEvolution.EnableParticle(Cluster->InternalCluster);
		}

		MEvolution.DirtyParticle(*Cluster->InternalCluster);
		MEvolution.GetParticles().MarkTransientDirtyParticle(Cluster->InternalCluster);
	}

	DECLARE_CYCLE_STAT(TEXT("FClusterUnionManager::HandleRemoveOperation"), STAT_HandleRemoveOperation, STATGROUP_Chaos);
	void FClusterUnionManager::HandleRemoveOperation(FClusterUnionIndex ClusterIndex, const TArray<FPBDRigidParticleHandle*>& Particles, bool bUpdateClusterProperties)
	{
		SCOPE_CYCLE_COUNTER(STAT_HandleRemoveOperation);
		FClusterUnion* Cluster = ClusterUnions.Find(ClusterIndex);
		if (!Cluster || Particles.IsEmpty())
		{
			return;
		}

		TGuardValue_Bitfield_Cleanup<TFunction<void()>> Cleanup(
			[this, OldGenerateClusterBreaking = MClustering.GetDoGenerateBreakingData()]() {
				MClustering.SetGenerateClusterBreaking(OldGenerateClusterBreaking);
			}
		);
		MClustering.SetGenerateClusterBreaking(false);

		IPhysicsProxyBase* OldProxy = Cluster->InternalCluster->PhysicsProxy();
		TArray<int32> ParticleIndicesToRemove;
		ParticleIndicesToRemove.Reserve(Particles.Num());

		for (FPBDRigidParticleHandle* Handle : Particles)
		{
			const int32 ParticleIndex = Cluster->ChildParticles.Find(Handle);
			if (ParticleIndex != INDEX_NONE)
			{
				ParticleIndicesToRemove.Add(ParticleIndex);
				// Remove the parent proxy only if it's a cluster union proxy.
				if (IPhysicsProxyBase* Proxy = Handle->PhysicsProxy(); Proxy && Proxy->GetParentProxy() && Proxy->GetParentProxy()->GetType() == EPhysicsProxyType::ClusterUnionProxy)
				{
					Proxy->SetParentProxy(nullptr);
				}
			}
		}

		ParticleIndicesToRemove.Sort();
		for (int32 Index = ParticleIndicesToRemove.Num() - 1; Index >= 0; --Index)
		{
			const int32 ParticleIndex = ParticleIndicesToRemove[Index];
			ParticleToClusterUnionIndex.Remove(Cluster->ChildParticles[ParticleIndex]);
			Cluster->ChildParticles.RemoveAt(ParticleIndex);
		}

		MClustering.RemoveParticlesFromCluster(Cluster->InternalCluster, Particles);

		if (bUpdateClusterProperties)
		{
			UpdateAllClusterUnionProperties(*Cluster, false);
		}

		// Removing a particle should have no bearing on the proxy of the cluster.
		// This gets changed because we go through an internal initialization route when we update the cluster union particle's properties.
		Cluster->InternalCluster->SetPhysicsProxy(OldProxy);

		if (!Cluster->ChildParticles.IsEmpty())
		{
			MEvolution.DirtyParticle(*Cluster->InternalCluster);
		}
		else
		{
			// Note that if we have 0 child particles, our implicit object union will have an invalid bounding box.
			// We must eject from the acceleration structure otherwise we risk cashes.
			MEvolution.DisableParticle(Cluster->InternalCluster);
		}
		MEvolution.GetParticles().MarkTransientDirtyParticle(Cluster->InternalCluster);
	}

	DECLARE_CYCLE_STAT(TEXT("FClusterUnionManager::HandleRemoveOperationWithClusterLookup"), STAT_HandleRemoveOperationWithClusterLookup, STATGROUP_Chaos);
	void FClusterUnionManager::HandleRemoveOperationWithClusterLookup(const TArray<FPBDRigidParticleHandle*>& InParticles, bool bUpdateClusterProperties)
	{
		SCOPE_CYCLE_COUNTER(STAT_HandleRemoveOperationWithClusterLookup);
		TMap<FClusterUnionIndex, TSet<FPBDRigidParticleHandle*>> ParticlesPerCluster;
		for (FPBDRigidParticleHandle* Particle : InParticles)
		{
			if (const int32 Index = FindClusterUnionIndexFromParticle(Particle); Index != INDEX_NONE)
			{
				ParticlesPerCluster.FindOrAdd(Index).Add(Particle);
			}
		}

		for (const TPair<FClusterUnionIndex, TSet<FPBDRigidParticleHandle*>>& Kvp : ParticlesPerCluster)
		{
			HandleRemoveOperation(Kvp.Key, Kvp.Value.Array(), bUpdateClusterProperties);
		}
	}

	DECLARE_CYCLE_STAT(TEXT("FClusterUnionManager::UpdateClusterUnionProperties"), STAT_UpdateClusterUnionProperties, STATGROUP_Chaos);
	void FClusterUnionManager::UpdateAllClusterUnionProperties(FClusterUnion& ClusterUnion, bool bRecomputeMassOrientation)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateClusterUnionProperties);
		// Update cluster properties.
		FMatrix33 ClusterInertia(0);

		// TODO: These functions are generally just re-building the cluster from scratch. Need to figure out a way
		// to get these functions to update the already existing cluster instead.
		TSet<FPBDRigidParticleHandle*> FullChildrenSet(ClusterUnion.ChildParticles);

		const FRigidTransform3 ForceMassOrientation{ ClusterUnion.InternalCluster->X(), ClusterUnion.InternalCluster->R() };
		UpdateClusterMassProperties(ClusterUnion.InternalCluster, FullChildrenSet, ClusterInertia, (bRecomputeMassOrientation && ClusterUnion.bNeedsXRInitialization) ? nullptr : &ForceMassOrientation);
		UpdateKinematicProperties(ClusterUnion.InternalCluster, MClustering.GetChildrenMap(), MEvolution);

		MEvolution.InvalidateParticle(ClusterUnion.InternalCluster);
		// The recreation of the geometry must happen after the call to UpdateClusterMassProperties.
		// Creating the geometry requires knowing the relative frame between the parent cluster and the child clusters. The
		// parent transform is not set properly for a new empty cluster until UpdateClusterMassProperties is called for the first time.
		ClusterUnion.SharedGeometry = ForceRecreateClusterUnionSharedGeometry(ClusterUnion);
		UpdateGeometry(ClusterUnion.InternalCluster, FullChildrenSet, MClustering.GetChildrenMap(), ClusterUnion.SharedGeometry, ClusterUnion.Parameters);

		// TODO: Need to figure out how to do the mapping back to the child shape if we ever do shape simplification...
		if (!ClusterUnion.ChildParticles.IsEmpty() && ClusterUnion.ChildParticles.Num() == ClusterUnion.InternalCluster->ShapesArray().Num())
		{
			for (int32 ChildIndex = 0; ChildIndex < ClusterUnion.ChildParticles.Num(); ++ChildIndex)
			{
				// TODO: Is there a better way to do this merge?
				const TUniquePtr<Chaos::FPerShapeData>& TemplateShape = ClusterUnion.ChildParticles[ChildIndex]->ShapesArray()[0];
				const TUniquePtr<Chaos::FPerShapeData>& ShapeData = ClusterUnion.InternalCluster->ShapesArray()[ChildIndex];
				if (ShapeData && TemplateShape)
				{
					{
						FCollisionData Data = TemplateShape->GetCollisionData();
						Data.UserData = nullptr;
						ShapeData->SetCollisionData(Data);
					}

					{
						FCollisionFilterData Data = TemplateShape->GetQueryData();
						Data.Word0 = ClusterUnion.ClusterUnionParameters.ActorId;
						ShapeData->SetQueryData(Data);
					}

					{
						FCollisionFilterData Data = TemplateShape->GetSimData();
						Data.Word0 = 0;
						Data.Word2 = ClusterUnion.ClusterUnionParameters.ComponentId;
						ShapeData->SetSimData(Data);
					}
				}
			}
		}

		MClustering.ClearConnectionGraph(ClusterUnion.InternalCluster);
		MClustering.GenerateConnectionGraph(ClusterUnion.InternalCluster, ClusterUnion.Parameters);
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

		FClusterUnionIndex NewIndex = CreateNewClusterUnion(DefaultClusterCreationParameters());
		ExplicitIndexMap.Add(InIndex, NewIndex);
		return NewIndex;
	}

	FClusterCreationParameters FClusterUnionManager::DefaultClusterCreationParameters() const
	{
		 FClusterCreationParameters Parameters{ 0.3f, 100, false, FRigidClustering::ShouldUnionsHaveCollisionParticles() };
		 Parameters.ConnectionMethod = MClustering.GetClusterUnionConnectionType();
		 return Parameters;
	}

	DECLARE_CYCLE_STAT(TEXT("FClusterUnionManager::FindClusterUnionIndexFromParticle"), STAT_FindClusterUnionIndexFromParticle, STATGROUP_Chaos);
	FClusterUnionIndex FClusterUnionManager::FindClusterUnionIndexFromParticle(FPBDRigidParticleHandle* ChildParticle)
	{
		SCOPE_CYCLE_COUNTER(STAT_FindClusterUnionIndexFromParticle);
		if (!ChildParticle)
		{
			return INDEX_NONE;
		}

		if (FClusterUnionIndex* Index = ParticleToClusterUnionIndex.Find(ChildParticle))
		{
			return *Index;
		}
		return INDEX_NONE;
	}

	bool FClusterUnionManager::IsClusterUnionParticle(FPBDRigidClusteredParticleHandle* Particle)
	{
		FClusterUnionIndex UnionIndex = FindClusterUnionIndexFromParticle(Particle);
		if (FClusterUnion* Union = FindClusterUnion(UnionIndex))
		{
			return Union->InternalCluster == Particle;
		}
		return false;
	}

	DECLARE_CYCLE_STAT(TEXT("FClusterUnionManager::UpdateClusterUnionParticlesChildToParent"), STAT_UpdateClusterUnionParticlesChildToParent, STATGROUP_Chaos);
	void FClusterUnionManager::UpdateClusterUnionParticlesChildToParent(FClusterUnionIndex Index, const TArray<FPBDRigidParticleHandle*>& Particles, const TArray<FTransform>& ChildToParent)
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateClusterUnionParticlesChildToParent);

		if (FClusterUnion* ClusterUnion = FindClusterUnion(Index))
		{
			for (int32 ParticleIndex = 0; ParticleIndex < Particles.Num() && ParticleIndex < ChildToParent.Num(); ++ParticleIndex)
			{
				FPBDRigidParticleHandle* Particle = Particles[ParticleIndex];
				if (!ensure(Particle))
				{
					return;
				}

				const int32 ChildIndex = ClusterUnion->ChildParticles.Find(Particle->CastToRigidParticle());
				if (ChildIndex != INDEX_NONE && ClusterUnion->InternalCluster)
				{
					if (FPBDRigidClusteredParticleHandle* ChildHandle = ClusterUnion->ChildParticles[ChildIndex]->CastToClustered())
					{
						ChildHandle->SetChildToParent(ChildToParent[ParticleIndex]);
					}
				}
			}

			UpdateAllClusterUnionProperties(*ClusterUnion, false);
			MEvolution.GetParticles().MarkTransientDirtyParticle(ClusterUnion->InternalCluster);
			MEvolution.DirtyParticle(*ClusterUnion->InternalCluster);
		}
	}
}