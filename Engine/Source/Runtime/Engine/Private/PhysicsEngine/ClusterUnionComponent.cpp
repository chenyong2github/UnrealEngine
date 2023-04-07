// Copyright Epic Games, Inc. All Rights Reserved.
#include "PhysicsEngine/ClusterUnionComponent.h"

#include "Engine/World.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Net/UnrealNetwork.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "PhysicsEngine/ClusterUnionReplicatedProxyComponent.h"
#include "PhysicsEngine/PhysicsObjectExternalInterface.h"
#include "PhysicsProxy/ClusterUnionPhysicsProxy.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ClusterUnionComponent)

DEFINE_LOG_CATEGORY(LogClusterUnion);

UClusterUnionComponent::UClusterUnionComponent(const FObjectInitializer& ObjectInitializer)
	: UPrimitiveComponent(ObjectInitializer)
{
	PhysicsProxy = nullptr;
	SetIsReplicatedByDefault(true);

#if WITH_EDITORONLY_DATA
	bVisualizeComponent = true;
#endif
}

FPhysScene_Chaos* UClusterUnionComponent::GetChaosScene() const
{
	if (AActor* Owner = GetOwner(); Owner && Owner->GetWorld())
	{
		return Owner->GetWorld()->GetPhysicsScene();
	}

	check(GWorld);
	return GWorld->GetPhysicsScene();
}

void UClusterUnionComponent::AddComponentToCluster(UPrimitiveComponent* InComponent, const TArray<int32>& BoneIds)
{
	if (!InComponent || !PhysicsProxy)
	{
		return;
	}

	if (!InComponent->HasValidPhysicsState())
	{
		if (!PendingComponentsToAdd.Contains(InComponent))
		{
			// Early out - defer adding the component to the cluster until the component has a valid physics state.
			FClusterUnionPendingAddData Data;
			Data.BoneIds = BoneIds;
			PendingComponentsToAdd.Add(InComponent, Data);
			InComponent->OnComponentPhysicsStateChanged.AddDynamic(this, &UClusterUnionComponent::HandleComponentPhysicsStateChange);
		}
		return;
	}

	PendingComponentsToAdd.Remove(InComponent);

	TArray<Chaos::FPhysicsObjectHandle> AllObjects = InComponent->GetAllPhysicsObjects();
	FLockedReadPhysicsObjectExternalInterface Interface = FPhysicsObjectExternalInterface::LockRead(AllObjects);

	for (Chaos::FPhysicsObjectHandle Handle : AllObjects)
	{
		TArray<Chaos::FGeometryParticle*> Particles = Interface->GetAllParticles({ &Handle, 1 });
		if (Particles.IsEmpty() || !Particles[0])
		{
			continue;
		}

		FClusterUnionParticleCandidateData Data;
		Data.Component = InComponent;
		Data.BoneId = Chaos::FPhysicsObjectInterface::GetId(Handle);
		UniqueIdxToComponent.Add(Particles[0]->UniqueIdx().Idx, Data);
	}

	TArray<Chaos::FPhysicsObjectHandle> Objects;
	if (BoneIds.IsEmpty())
	{
		Objects = AllObjects;
	}
	else
	{
		Objects.Reserve(BoneIds.Num());
		for (int32 Id : BoneIds)
		{
			Objects.Add(InComponent->GetPhysicsObjectById(Id));
		}
	}

	if (BoneIds.IsEmpty())
	{
		Objects = Objects.FilterByPredicate(
			[&Interface](Chaos::FPhysicsObjectHandle Handle)
			{
				return !Interface->AreAllDisabled({ &Handle, 1 });
			}
		);
	}

	if (Objects.IsEmpty())
	{
		UE_LOG(LogClusterUnion, Warning, TEXT("Trying to add a component [%p] with no physics objects to a cluster union...ignoring"), InComponent)
		return;
	}

	PhysicsProxy->AddPhysicsObjects_External(Objects);
}

void UClusterUnionComponent::RemoveComponentFromCluster(UPrimitiveComponent* InComponent)
{
	if (!InComponent || !PhysicsProxy)
	{
		return;
	}

	const int32 NumRemoved = PendingComponentsToAdd.Remove(InComponent);
	if (NumRemoved > 0)
	{
		// We haven't actually added yet so we can early out.
		return;
	}

	TSet<Chaos::FPhysicsObjectHandle> PhysicsObjectsToRemove;

	if (FClusteredComponentData* ComponentData = ComponentToPhysicsObjects.Find(InComponent))
	{
		// We need to mark the replicated proxy as pending deletion.
		// This way anyone who tries to use the replicated proxy component knows that it
		// doesn't actually denote a meaningful cluster union relationship.
		if (IsAuthority())
		{
			if (UClusterUnionReplicatedProxyComponent* Component = ComponentData->ReplicatedProxyComponent.Get())
			{
				Component->MarkPendingDeletion();
			}
		}

		PhysicsObjectsToRemove = ComponentData->PhysicsObjects;
	}

	PhysicsProxy->RemovePhysicsObjects_External(PhysicsObjectsToRemove);
}

TArray<UPrimitiveComponent*> UClusterUnionComponent::GetPrimitiveComponents()
{
	TArray<UPrimitiveComponent*> PrimitiveComponents;
	for (auto Iter = ComponentToPhysicsObjects.CreateIterator(); Iter; ++Iter)
	{
		PrimitiveComponents.Add(Iter.Key().ResolveObjectPtr());
	}

	return PrimitiveComponents;
}

void UClusterUnionComponent::SetIsAnchored(bool bIsAnchored)
{
	if (!PhysicsProxy)
	{
		return;
	}

	PhysicsProxy->SetIsAnchored_External(bIsAnchored);
}

bool UClusterUnionComponent::IsAuthority() const
{
	ENetMode Mode = GetNetMode();
	if (Mode == ENetMode::NM_Standalone)
	{
		return true;
	}

	if (AActor* Owner = GetOwner())
	{
		return Owner->GetLocalRole() == ROLE_Authority && Mode != NM_Client;
	}

	return false;
}

void UClusterUnionComponent::OnCreatePhysicsState()
{
	USceneComponent::OnCreatePhysicsState();

	// If we've already created the physics proxy we shouldn't do this again.
	if (PhysicsProxy)
	{
		return;
	}

	// If we're not actually playing/needing this to simulate (e.g. in the editor) there should be no reason to create this proxy.
	const bool bValidWorld = GetWorld() && (GetWorld()->IsGameWorld() || GetWorld()->IsPreviewWorld());
	if (!bValidWorld)
	{
		return;
	}

	// TODO: Expose these parameters via the component.
	Chaos::FClusterCreationParameters Parameters{ 0.3f, 100, false, false };
	Parameters.ConnectionMethod = Chaos::FClusterCreationParameters::EConnectionMethod::DelaunayTriangulation;

	FChaosUserData::Set<UPrimitiveComponent>(&PhysicsUserData, this);

	Chaos::FClusterUnionInitData InitData;
	InitData.UserData = static_cast<void*>(&PhysicsUserData);
	InitData.ActorId = GetOwner()->GetUniqueID();
	InitData.ComponentId = GetUniqueID();
	InitData.bNeedsClusterXRInitialization = GetOwner()->HasAuthority();
	PhysicsProxy = new Chaos::FClusterUnionPhysicsProxy{ this, Parameters, InitData };
	PhysicsProxy->Initialize_External();
	if (FPhysScene_Chaos* Scene = GetChaosScene())
	{
		Scene->AddObject(this, PhysicsProxy);
	}

	// It's just logically easier to be consistent on the client to go through the replication route.
	if (IsAuthority())
	{
		for (const FComponentReference& ComponentReference : ClusteredComponentsReferences)
		{
			if (!ComponentReference.OtherActor.IsValid())
			{
				continue;
			}

			AddComponentToCluster(Cast<UPrimitiveComponent>(ComponentReference.GetComponent(ComponentReference.OtherActor.Get())), {});
		}
	}
}

void UClusterUnionComponent::OnDestroyPhysicsState()
{
	USceneComponent::OnDestroyPhysicsState();

	if (!PhysicsProxy)
	{
		return;
	}

	// We need to make sure we *immediately* disconnect on the GT side since there's no guarantee the normal flow
	// will happen once we've destroyed things.
	TSet<TObjectPtr<UPrimitiveComponent>> RemainingComponents;
	for (const TPair<TObjectKey<UPrimitiveComponent>, FClusteredComponentData>& Kvp : ComponentToPhysicsObjects)
	{
		RemainingComponents.Add(Kvp.Key.ResolveObjectPtr());
	}

	for (TObjectPtr<UPrimitiveComponent> Component : RemainingComponents)
	{
		if (Component)
		{
			HandleRemovedClusteredComponent(Component, false);
		}
	}

	if (FPhysScene_Chaos* Scene = GetChaosScene())
	{
		Scene->RemoveObject(PhysicsProxy);
	}

	PhysicsProxy = nullptr;
}

void UClusterUnionComponent::OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	USceneComponent::OnUpdateTransform(UpdateTransformFlags, Teleport);

	if (PhysicsProxy && !(UpdateTransformFlags & EUpdateTransformFlags::SkipPhysicsUpdate))
	{
		// If the component transform changes, we need to make sure this update is reflected on the physics thread as well.
		// This code path is generally used when setting the transform manually or when it's set via replication.
		const FTransform Transform = GetComponentTransform();
		PhysicsProxy->SetXR_External(Transform.GetLocation(), Transform.GetRotation());
	}
}

bool UClusterUnionComponent::ShouldCreatePhysicsState() const
{
	return true;
}

bool UClusterUnionComponent::HasValidPhysicsState() const
{
	return PhysicsProxy != nullptr;
}

Chaos::FPhysicsObject* UClusterUnionComponent::GetPhysicsObjectById(Chaos::FPhysicsObjectId Id) const
{
	if (!PhysicsProxy)
	{
		return nullptr;
	}
	return PhysicsProxy->GetPhysicsObjectHandle();
}

Chaos::FPhysicsObject* UClusterUnionComponent::GetPhysicsObjectByName(const FName& Name) const
{
	return GetPhysicsObjectById(0);
}

TArray<Chaos::FPhysicsObject*> UClusterUnionComponent::GetAllPhysicsObjects() const
{
	return { GetPhysicsObjectById(0) };
}

Chaos::FPhysicsObjectId UClusterUnionComponent::GetIdFromGTParticle(Chaos::FGeometryParticle* Particle) const
{
	return 0;
}

void UClusterUnionComponent::HandleComponentPhysicsStateChange(UPrimitiveComponent* ChangedComponent, EComponentPhysicsStateChange StateChange)
{
	// TODO: Maybe we should handle the destroyed state change too?
	if (!ChangedComponent || StateChange != EComponentPhysicsStateChange::Created)
	{
		return;
	}

	ChangedComponent->OnComponentPhysicsStateChanged.RemoveDynamic(this, &UClusterUnionComponent::HandleComponentPhysicsStateChange);

	if (FClusterUnionPendingAddData* PendingData = PendingComponentsToAdd.Find(ChangedComponent))
	{
		AddComponentToCluster(ChangedComponent, PendingData->BoneIds);
	}
}

void UClusterUnionComponent::SyncVelocitiesFromPhysics(const FVector& LinearVelocity, const FVector& AngularVelocity)
{
	if (!IsAuthority())
	{
		return;
	}

	ReplicatedRigidState.LinVel = LinearVelocity;
	ReplicatedRigidState.AngVel = AngularVelocity;
	MARK_PROPERTY_DIRTY_FROM_NAME(UClusterUnionComponent, ReplicatedRigidState, this);
}

void UClusterUnionComponent::SyncClusterUnionFromProxy()
{
	// NOTE THAT WE ARE ON THE GAME THREAD HERE.
	if (!PhysicsProxy)
	{
		return;
	}

	ReplicatedRigidState.bIsAnchored = PhysicsProxy->IsAnchored_External();
	ReplicatedRigidState.ObjectState = static_cast<uint8>(PhysicsProxy->GetObjectState_External());
	MARK_PROPERTY_DIRTY_FROM_NAME(UClusterUnionComponent, ReplicatedRigidState, this);
	
	const Chaos::FClusterUnionSyncedData& FullData = PhysicsProxy->GetSyncedData_External();

	// Note that at the UClusterUnionComponent level we really only want to be dealing with components.
	// Hence why we need to modify each of the particles that we synced from the game thread into a
	// component + bone id combination for identification. 
	TMap<TObjectKey<UPrimitiveComponent>, TMap<int32, FTransform>> MappedData;
	for (const Chaos::FClusterUnionChildData& ChildData : FullData.ChildParticles)
	{
		if (FClusterUnionParticleCandidateData* Data = UniqueIdxToComponent.Find(ChildData.ParticleIdx.Idx))
		{
			MappedData.FindOrAdd(Data->Component).Add(Data->BoneId, ChildData.ChildToParent);
		}
	}

	// We need to handle any additions, deletions, and modifications to any child in the cluster union here.
	// If a component lives in MappedData but not in ComponentToPhysicsObjects, new component!
	// If a component lives in both, then it's a modified component.
	for (const TPair<TObjectKey<UPrimitiveComponent>, TMap<int32, FTransform>>& Kvp : MappedData)
	{
		HandleAddOrModifiedClusteredComponent(Kvp.Key.ResolveObjectPtr(), Kvp.Value);
	}

	// If a component lives in ComponentToPhysicsObjects but not in MappedData, deleted component!
	TArray<TObjectPtr<UPrimitiveComponent>> ComponentsToRemove;
	for (const TPair<TObjectKey<UPrimitiveComponent>, FClusteredComponentData>& Kvp : ComponentToPhysicsObjects)
	{
		if (!MappedData.Contains(Kvp.Key))
		{
			ComponentsToRemove.Add(Kvp.Key.ResolveObjectPtr());
		}
	}

	for (TObjectPtr<UPrimitiveComponent> Component : ComponentsToRemove)
	{
		HandleRemovedClusteredComponent(Component, true);
	}
}

void UClusterUnionComponent::HandleAddOrModifiedClusteredComponent(UPrimitiveComponent* ChangedComponent, const TMap<int32, FTransform>& PerBoneChildToParent)
{
	if (!ChangedComponent)
	{
		return;
	}

	const bool bIsNew = !ComponentToPhysicsObjects.Contains(ChangedComponent);
	FClusteredComponentData& ComponentData = ComponentToPhysicsObjects.FindOrAdd(ChangedComponent);

	// If this is a *new* component that we're keeping track of then there's additional book-keeping
	// we need to do to make sure we don't forget what exactly we're tracking. Additionally, we need to
	// modify the component and its parent actor to ensure their replication stops.
	if (bIsNew)
	{
		// Force the component and its parent actor to stop replicating movement.
		// Setting the component to not replicate should be sufficient since a simulating
		// component shouldn't be doing much more than replicating its position anyway.
		if (AActor* Owner = ChangedComponent->GetOwner())
		{
			if (FClusteredActorData* Data = ActorToComponents.Find(Owner))
			{
				Data->Components.Add(ChangedComponent);
			}
			else
			{
				FClusteredActorData NewData;
				NewData.Components.Add(ChangedComponent);
				NewData.bWasReplicatingMovement = Owner->IsReplicatingMovement();
				ActorToComponents.Add(Owner, NewData);

				if (IsAuthority())
				{
					Owner->SetReplicatingMovement(false);
				}
			}
		}

		ComponentData.bWasReplicating = ChangedComponent->GetIsReplicated();
		if (IsAuthority())
		{
			ChangedComponent->SetIsReplicated(false);
			if (AActor* Owner = ChangedComponent->GetOwner())
			{
				// Create a replicated proxy component and add it to the actor being added to the cluster.
				// This component will take care of replicating this addition into the cluster.
				TObjectPtr<UClusterUnionReplicatedProxyComponent> ReplicatedProxy = NewObject<UClusterUnionReplicatedProxyComponent>(Owner);
				if (ensure(ReplicatedProxy))
				{
					ReplicatedProxy->RegisterComponent();
					ReplicatedProxy->SetParentClusterUnion(this);
					ReplicatedProxy->SetChildClusteredComponent(ChangedComponent);
					ReplicatedProxy->SetIsReplicated(true);
				}

				ComponentData.ReplicatedProxyComponent = ReplicatedProxy;
			}
		}
	}

	if (IsAuthority() && ComponentData.ReplicatedProxyComponent.IsValid())
	{
		// We really only need to do modifications on the server since that's where we're changing the replicated proxy to broadcast this data change.
		TSet<int32> BoneIds;
		PerBoneChildToParent.GetKeys(BoneIds);

		TObjectPtr<UClusterUnionReplicatedProxyComponent> ReplicatedProxy = ComponentData.ReplicatedProxyComponent.Get();
		ReplicatedProxy->SetParticleBoneIds(BoneIds.Array());
		for (const TPair<int32, FTransform>& Kvp : PerBoneChildToParent)
		{
			ReplicatedProxy->SetParticleChildToParent(Kvp.Key, Kvp.Value);
		}

		if (AActor* Owner = ChangedComponent->GetOwner())
		{
			Owner->FlushNetDormancy();
		}
	}

	// One more loop to ensure that our sets of physics objects are valid and up to date.
	// This needs to happen on both the client and the server.
	for (const TPair<int32, FTransform>& Kvp : PerBoneChildToParent)
	{
		Chaos::FPhysicsObjectHandle PhysicsObject = ChangedComponent->GetPhysicsObjectById(Kvp.Key);
		ComponentData.PhysicsObjects.Add(PhysicsObject);
	}

	ComponentData.AllPhysicsObjects.Empty();
	ComponentData.AllPhysicsObjects.Append(ChangedComponent->GetAllPhysicsObjects());
}

void UClusterUnionComponent::HandleRemovedClusteredComponent(UPrimitiveComponent* ChangedComponent, bool bDestroyReplicatedProxy)
{
	if (!ChangedComponent)
	{
		return;
	}

	// At this point the component's particles are no longer a part of the cluster union. So we just need
	// to get our book-keeping and game thread state to match that.
	AActor* Owner = ChangedComponent->GetOwner();
	if (!ensure(Owner))
	{
		return;
	}

	if (FClusteredComponentData* ComponentData = ComponentToPhysicsObjects.Find(ChangedComponent))
	{
		FLockedReadPhysicsObjectExternalInterface Interface = FPhysicsObjectExternalInterface::LockRead(ComponentData->AllPhysicsObjects);
		for (Chaos::FGeometryParticle* Particle : Interface->GetAllParticles(ComponentData->AllPhysicsObjects))
		{
			if (!Particle)
			{
				continue;
			}

			UniqueIdxToComponent.Remove(Particle->UniqueIdx().Idx);
		}

		if (IsAuthority())
		{
			ChangedComponent->SetIsReplicated(ComponentData->bWasReplicating);

			if (bDestroyReplicatedProxy && ensure(ComponentData->ReplicatedProxyComponent.IsValid()))
			{
				UClusterUnionReplicatedProxyComponent* ProxyComponent = ComponentData->ReplicatedProxyComponent.Get();
				ProxyComponent->DestroyComponent();
			}
		}

		ComponentToPhysicsObjects.Remove(ChangedComponent);
	}


	if (FClusteredActorData* ActorData = ActorToComponents.Find(Owner))
	{
		ActorData->Components.Remove(ChangedComponent);

		if (ActorData->Components.IsEmpty())
		{
			if (IsAuthority())
			{
				Owner->SetReplicatingMovement(ActorData->bWasReplicatingMovement);
			}
			ActorToComponents.Remove(Owner);
		}
	}

	Owner->FlushNetDormancy();
}

void UClusterUnionComponent::OnRep_RigidState()
{
	if (!PhysicsProxy)
	{
		return;
	}

	PhysicsProxy->SetLinearVelocity_External(ReplicatedRigidState.LinVel);
	PhysicsProxy->SetAngularVelocity_External(ReplicatedRigidState.AngVel);
	PhysicsProxy->SetIsAnchored_External(ReplicatedRigidState.bIsAnchored);
	PhysicsProxy->SetObjectState_External(static_cast<Chaos::EObjectStateType>(ReplicatedRigidState.ObjectState));
}

void UClusterUnionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	UPrimitiveComponent::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;

	DOREPLIFETIME_WITH_PARAMS_FAST(UClusterUnionComponent, ReplicatedRigidState, Params);
}

void UClusterUnionComponent::ForceSetChildToParent(UPrimitiveComponent* InComponent, const TArray<int32>& BoneIds, const TArray<FTransform>& ChildToParent)
{
	if (IsAuthority() || !PhysicsProxy || !ensure(InComponent) || !ensure(BoneIds.Num() == ChildToParent.Num()))
	{
		return;
	}

	TArray< Chaos::FPhysicsObjectHandle> Objects;
	Objects.Reserve(BoneIds.Num());

	for (int32 Index = 0; Index < BoneIds.Num(); ++Index)
	{
		Chaos::FPhysicsObjectHandle Handle = InComponent->GetPhysicsObjectById(BoneIds[Index]);
		Objects.Add(Handle);
	}

	// If we're on the client we want to lock the child to parent transform for this particle as soon as we get a server authoritative value.
	PhysicsProxy->BulkSetChildToParent_External(Objects, ChildToParent, !IsAuthority());
}

void UClusterUnionComponent::SetSimulatePhysics(bool bSimulate)
{
	if (!PhysicsProxy)
	{
		return;
	}

	PhysicsProxy->SetObjectState_External(bSimulate ? Chaos::EObjectStateType::Dynamic : Chaos::EObjectStateType::Kinematic);
}

bool UClusterUnionComponent::LineTraceComponent(FHitResult& OutHit, const FVector Start, const FVector End, const FCollisionQueryParams& Params)
{
	bool bHasHit = false;
	OutHit.Distance = TNumericLimits<float>::Max();
	for (const TPair<TObjectKey<UPrimitiveComponent>, FClusteredComponentData>& Kvp : ComponentToPhysicsObjects)
	{
		if (UPrimitiveComponent* Component = Kvp.Key.ResolveObjectPtr())
		{
			FHitResult ComponentHit;
			if (Component->LineTraceComponent(ComponentHit, Start, End, Params) && ComponentHit.Distance < OutHit.Distance)
			{
				bHasHit = true;
				OutHit = ComponentHit;
			}
		}
	}
	return bHasHit;
}

bool UClusterUnionComponent::SweepComponent(FHitResult& OutHit, const FVector Start, const FVector End, const FQuat& ShapeWorldRotation, const FCollisionShape& CollisionShape, bool bTraceComplex)
{
	bool bHasHit = false;
	OutHit.Distance = TNumericLimits<float>::Max();
	for (const TPair<TObjectKey<UPrimitiveComponent>, FClusteredComponentData>& Kvp : ComponentToPhysicsObjects)
	{
		if (UPrimitiveComponent* Component = Kvp.Key.ResolveObjectPtr())
		{
			FHitResult ComponentHit;
			if (Component->SweepComponent(ComponentHit, Start, End, ShapeWorldRotation, CollisionShape, bTraceComplex) && ComponentHit.Distance < OutHit.Distance)
			{
				bHasHit = true;
				OutHit = ComponentHit;
			}
		}
	}
	return bHasHit;
}

bool UClusterUnionComponent::OverlapComponentWithResult(const FVector& Pos, const FQuat& Rot, const FCollisionShape& CollisionShape, TArray<FOverlapResult>& OutOverlap) const
{
	bool bHasOverlap = false;
	for (const TPair<TObjectKey<UPrimitiveComponent>, FClusteredComponentData>& Kvp : ComponentToPhysicsObjects)
	{
		if (UPrimitiveComponent* Component = Kvp.Key.ResolveObjectPtr())
		{
			TArray<FOverlapResult> SubOverlaps;
			if (Component->OverlapComponentWithResult(Pos, Rot, CollisionShape, SubOverlaps))
			{
				bHasOverlap = true;
				OutOverlap.Append(SubOverlaps);
			}
		}
	}
	return bHasOverlap;
}

bool UClusterUnionComponent::ComponentOverlapComponentWithResultImpl(const class UPrimitiveComponent* const PrimComp, const FVector& Pos, const FQuat& Rot, const FCollisionQueryParams& Params, TArray<FOverlapResult>& OutOverlap) const
{
	bool bHasOverlap = false;

	TSet<uint32> IgnoredActors;
	IgnoredActors.Reserve(Params.GetIgnoredActors().Num());
	for (uint32 Id : Params.GetIgnoredActors())
	{
		IgnoredActors.Add(Id);
	}

	TSet<uint32> IgnoredComponents;
	IgnoredComponents.Reserve(Params.GetIgnoredComponents().Num());
	for (uint32 Id : Params.GetIgnoredActors())
	{
		IgnoredComponents.Add(Id);
	}

	for (const TPair<TObjectKey<UPrimitiveComponent>, FClusteredComponentData>& Kvp : ComponentToPhysicsObjects)
	{
		if (UPrimitiveComponent* Component = Kvp.Key.ResolveObjectPtr())
		{
			if (IgnoredComponents.Contains(Component->GetUniqueID()))
			{
				continue;
			}

			if (AActor* Owner = Component->GetOwner())
			{
				if (IgnoredActors.Contains(Owner->GetUniqueID()))
				{
					continue;
				}
			}

			TArray<FOverlapResult> SubOverlaps;
			if (Component->ComponentOverlapComponentWithResult(PrimComp, Pos, Rot, Params, SubOverlaps))
			{
				bHasOverlap = true;
				OutOverlap.Append(SubOverlaps);
			}
		}
	}
	return bHasOverlap;
}

void UClusterUnionComponent::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UPrimitiveComponent::AddReferencedObjects(InThis, Collector);

	UClusterUnionComponent* This = CastChecked<UClusterUnionComponent>(InThis);

	{
		const UScriptStruct* ScriptStruct = FClusteredComponentData::StaticStruct();
		for (TPair<TObjectKey<UPrimitiveComponent>, FClusteredComponentData>& Kvp : This->ComponentToPhysicsObjects)
		{
			Collector.AddReferencedObjects(ScriptStruct, reinterpret_cast<void*>(&Kvp.Value), This, nullptr);
		}
	}

	{
		const UScriptStruct* ScriptStruct = FClusteredActorData::StaticStruct();
		for (TPair<TObjectKey<AActor>, FClusteredActorData>& Kvp : This->ActorToComponents)
		{
			Collector.AddReferencedObjects(ScriptStruct, reinterpret_cast<void*>(&Kvp.Value), This, nullptr);
		}
	}

	{
		const UScriptStruct* ScriptStruct = FClusterUnionPendingAddData::StaticStruct();
		for (TPair<TObjectKey<UPrimitiveComponent>, FClusterUnionPendingAddData>& Kvp : This->PendingComponentsToAdd)
		{
			Collector.AddReferencedObjects(ScriptStruct, reinterpret_cast<void*>(&Kvp.Value), This, nullptr);
		}
	}
}