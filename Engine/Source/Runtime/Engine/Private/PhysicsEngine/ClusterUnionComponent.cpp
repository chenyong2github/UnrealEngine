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

void UClusterUnionComponent::AddComponentToCluster(UPrimitiveComponent* InComponent, const TArray<FName>& BoneNames)
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
			Data.BoneNames = BoneNames;
			PendingComponentsToAdd.Add(InComponent, Data);
			InComponent->OnComponentPhysicsStateChanged.AddDynamic(this, &UClusterUnionComponent::HandleComponentPhysicsStateChange);
		}
		return;
	}

	PendingComponentsToAdd.Remove(InComponent);

	TArray<Chaos::FPhysicsObjectHandle> Objects;

	if (BoneNames.IsEmpty())
	{
		Objects = InComponent->GetAllPhysicsObjects();
	}
	else
	{
		Objects.Reserve(BoneNames.Num());
		for (const FName& Name : BoneNames)
		{
			Objects.Add(InComponent->GetPhysicsObjectByName(Name));
		}
	}

	FLockedReadPhysicsObjectExternalInterface Interface = FPhysicsObjectExternalInterface::LockRead(Objects);

	if (BoneNames.IsEmpty())
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

	// Force the component and its parent actor to stop replicating movement.
	// Setting the component to not replicate should be sufficient since a simulating
	// component shouldn't be doing much more than replicating its position anyway.
	if (AActor* Owner = InComponent->GetOwner())
	{
		if (FClusteredActorData* Data = ActorToComponents.Find(Owner))
		{
			Data->Components.Add(InComponent);
		}
		else
		{
			FClusteredActorData NewData;
			NewData.Components.Add(InComponent);
			NewData.bWasReplicatingMovement = Owner->IsReplicatingMovement();
			ActorToComponents.Add(Owner, NewData);

			if (IsAuthority())
			{
				Owner->SetReplicatingMovement(false);
			}
		}
	}

	FClusteredComponentData Data;
	Data.PhysicsObjects = Objects;
	Data.bWasReplicating = InComponent->GetIsReplicated();

	if (IsAuthority())
	{
		InComponent->SetIsReplicated(false);
		if (AActor* Owner = InComponent->GetOwner())
		{
			// Create a replicated proxy component and add it to the actor being added to the cluster.
			// This component will take care of replicating this addition into the cluster.
			TObjectPtr<UClusterUnionReplicatedProxyComponent> ReplicatedProxy = NewObject<UClusterUnionReplicatedProxyComponent>(Owner);
			if (ensure(ReplicatedProxy))
			{
				ReplicatedProxy->RegisterComponent();
				ReplicatedProxy->SetParentClusterUnion(this);
				ReplicatedProxy->SetChildClusteredComponent(InComponent);

				TArray<FName> PhysicsObjectBoneNames;
				PhysicsObjectBoneNames.Reserve(Objects.Num());

				for (Chaos::FPhysicsObjectHandle PhysicsObject : Objects)
				{
					PhysicsObjectBoneNames.Add(Chaos::FPhysicsObjectInterface::GetName(PhysicsObject));
				}

				ReplicatedProxy->SetParticleBoneNames(PhysicsObjectBoneNames);
				ReplicatedProxy->SetIsReplicated(true);
			}

			Data.ReplicatedProxyComponent = ReplicatedProxy;
			Owner->FlushNetDormancy();
		}
	}

	ComponentToPhysicsObjects.Add(InComponent, Data);

	for (Chaos::FPhysicsObjectHandle Handle : Objects)
	{
		PhysicsObjectToComponent.Add(Handle, InComponent);
	}
	PhysicsProxy->AddPhysicsObjects_External(Objects);
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
	PhysicsProxy = new Chaos::FClusterUnionPhysicsProxy{this, Parameters, static_cast<void*>(&PhysicsUserData), IsAuthority() ? Chaos::EThreadContext::Internal : Chaos::EThreadContext::External };
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

Chaos::FPhysicsObject* UClusterUnionComponent::GetPhysicsObjectById(int32 Id) const
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
		AddComponentToCluster(ChangedComponent, PendingData->BoneNames);
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

void UClusterUnionComponent::SyncIsAnchoredFromPhysics(bool bIsAnchored)
{
	if (!IsAuthority())
	{
		return;
	}

	ReplicatedRigidState.bIsAnchored = bIsAnchored;
	MARK_PROPERTY_DIRTY_FROM_NAME(UClusterUnionComponent, ReplicatedRigidState, this);
}

void UClusterUnionComponent::SyncChildToParentFromProxy()
{
	if (!PhysicsProxy)
	{
		return;
	}

	const TArray<Chaos::FPhysicsObjectHandle>& ChildPhysicsObjects = PhysicsProxy->GetChildPhysicsObjects_External();
	const Chaos::FClusterUnionSyncedData& SyncedData = PhysicsProxy->GetSyncedData_External();
	if (ChildPhysicsObjects.Num() != SyncedData.ChildToParent.Num())
	{
		return;
	}

	for (int32 Index = 0; Index < ChildPhysicsObjects.Num(); ++Index)
	{
		Chaos::FPhysicsObjectHandle PhysicsObject = ChildPhysicsObjects[Index];
		const FTransform& ChildToParent = SyncedData.ChildToParent[Index];

		if (UPrimitiveComponent** Component = PhysicsObjectToComponent.Find(PhysicsObject); Component && *Component)
		{
			if (FClusteredComponentData* Data = ComponentToPhysicsObjects.Find(*Component); Data && Data->ReplicatedProxyComponent.IsValid())
			{
				Data->ReplicatedProxyComponent.Get()->SetParticleChildToParent(Chaos::FPhysicsObjectInterface::GetName(PhysicsObject), ChildToParent);
			}
		}
	}
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
}

void UClusterUnionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	UPrimitiveComponent::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;

	DOREPLIFETIME_WITH_PARAMS_FAST(UClusterUnionComponent, ReplicatedRigidState, Params);
}

void UClusterUnionComponent::ForceSetChildToParent(UPrimitiveComponent* InComponent, const TArray<FName>& BoneNames, const TArray<FTransform>& ChildToParent)
{
	if (IsAuthority() || !PhysicsProxy || !ensure(InComponent) || !ensure(BoneNames.Num() == ChildToParent.Num()))
	{
		return;
	}

	for (int32 Index = 0; Index < BoneNames.Num(); ++Index)
	{
		Chaos::FPhysicsObjectHandle Handle = InComponent->GetPhysicsObjectByName(BoneNames[Index]);
		PhysicsProxy->SetChildToParent_External(Handle, ChildToParent[Index]);
	}
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