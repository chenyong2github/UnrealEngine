// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Chaos/PhysicsObject.h"
#include "Components/PrimitiveComponent.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Engine/EngineTypes.h"
#include "Logging/LogMacros.h"
#include "PhysicsInterfaceTypesCore.h"
#include "PhysicsProxy/ClusterUnionPhysicsProxy.h"
#include "UObject/ObjectKey.h"

#include "ClusterUnionComponent.generated.h"

class AActor;
class FPhysScene_Chaos;
class UClusterUnionReplicatedProxyComponent;

DECLARE_LOG_CATEGORY_EXTERN(LogClusterUnion, Log, All);

namespace Chaos
{
	class FPBDRigidsSolver;
}

USTRUCT()
struct FClusteredComponentData
{
	GENERATED_BODY()

	TArray<Chaos::FPhysicsObjectHandle> PhysicsObjects;

	// Using a TWeakObjectPtr here because the UClusterUnionReplicatedProxyComponent will have a pointer back
	// and we don't want to get into a situation where a circular reference occurs.
	UPROPERTY()
	TWeakObjectPtr<UClusterUnionReplicatedProxyComponent> ReplicatedProxyComponent;

	UPROPERTY()
	bool bWasReplicating = true;
};

USTRUCT()
struct FClusteredActorData
{
	GENERATED_BODY()

	UPROPERTY()
	TSet<TObjectPtr<UPrimitiveComponent>> Components;

	UPROPERTY()
	bool bWasReplicatingMovement = true;
};

USTRUCT()
struct FClusterUnionReplicatedData
{
	GENERATED_BODY()

	UPROPERTY()
	FVector_NetQuantize100 LinVel;

	UPROPERTY()
	FVector_NetQuantize100 AngVel;

	UPROPERTY()
	bool bIsAnchored = false;
};

USTRUCT()
struct FClusterUnionPendingAddData
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FName> BoneNames;
};

/**
 * This does the bulk of the work exposing a physics cluster union to the game thread.
 * This component needs to be a primitive component primarily because of how physics
 * proxies need to be registered with the solver with an association with a primitive component.
 * This component can be used as part of AClusterUnionActor or on its own as its list of
 * clustered components/actors can be specified dynamically at runtime and/or statically
 * on asset creation.
 */
UCLASS()
class ENGINE_API UClusterUnionComponent : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	UClusterUnionComponent(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category="Cluster Union")
	void AddComponentToCluster(UPrimitiveComponent* InComponent, const TArray<FName>& BoneNames);

	UFUNCTION(BlueprintCallable, Category = "Cluster Union")
	void SetIsAnchored(bool bIsAnchored);

	// The SyncVelocitiesFromPhysics will set replicated state using data from the physics thread. 
	void SyncVelocitiesFromPhysics(const FVector& LinearVelocity, const FVector& AngularVelocity);
	// The SyncIsAnchoredFromPhysics will set replicated state using data from the physics thread.
	void SyncIsAnchoredFromPhysics(bool bIsAnchored);
	// SyncChildToParentFromProxy will take the ChildToParent transform data from the proxy and make sure that gets replicated to the correct replicated proxy components.
	void SyncChildToParentFromProxy();

	UFUNCTION()
	bool IsComponentAdded(UPrimitiveComponent* Component) { return ComponentToPhysicsObjects.Contains(Component); }

	friend class UClusterUnionReplicatedProxyComponent;
protected:

	// This should only be called on the client when replication happens.
	UFUNCTION()
	void ForceSetChildToParent(UPrimitiveComponent* InComponent, const TArray<FName>& BoneNames, const TArray<FTransform>& ChildToParent);

private:
	// These are the statically clustered components. These should
	// be specified in the editor and never change.
	UPROPERTY(EditAnywhere, Category = "Cluster Union")
	TArray<FComponentReference> ClusteredComponentsReferences;
	
	// We need to keep track of the mapping of primitive components to physics objects.
	// This way we know the right physics objects to pass when removing the component (because
	// it's possible to get a different list of physics objects when we get to removal). A
	// side benefit here is being able to track which components are clustered.
	TMap<TObjectKey<UPrimitiveComponent>, FClusteredComponentData> ComponentToPhysicsObjects;

	// Similarly, we need to keep track of the mapping from physics object handles to primitive components.
	// The physics proxy will only have data about these handles, so when we get an update from the proxy
	// about a particular handle we will want to map it back to a component here.
	TMap<Chaos::FPhysicsObjectHandle, UPrimitiveComponent*> PhysicsObjectToComponent;

	// Also keep track of which actors we are clustering and their components. We make modifications on
	// actors that get clustered so we need to make sure we undo those changes only once all its clustered
	// components are removed from the cluster.
	TMap<TObjectKey<AActor>, FClusteredActorData> ActorToComponents;

	// Sometimes we might be in the process of waiting for a component to create it physics state before adding to the cluster.
	// Make sure we don't try to add the component multiples times while the add is pending.
	TMap<TObjectKey<UPrimitiveComponent>, FClusterUnionPendingAddData> PendingComponentsToAdd;

	// Data that can be changed at runtime to keep state about the cluster union consistent between the server and client.
	UPROPERTY(ReplicatedUsing=OnRep_RigidState)
	FClusterUnionReplicatedData ReplicatedRigidState;

	// Handles changes to ReplicatedRigidState. Note that this function does not handle replication of X/R since we make use
	// of the scene component's default replication for that.
	UFUNCTION()
	void OnRep_RigidState();

	FPhysScene_Chaos* GetChaosScene() const;

	Chaos::FClusterUnionPhysicsProxy* PhysicsProxy;

	// User data to be able to tie the cluster particle back to this component.
	FChaosUserData PhysicsUserData;

	// Need to handle the fact that this component may or may not be initialized prior to the components referenced in
	// ClusteredComponentsReferences. This function lets us listen to OnComponentPhysicsStateChanged on the incoming
	// primitive component so that once the physics state is properly created we can begin the process of adding it.
	UFUNCTION()
	void HandleComponentPhysicsStateChange(UPrimitiveComponent* ChangedComponent, EComponentPhysicsStateChange StateChange);

	// Whether or not this code is running on the server.
	UFUNCTION()
	bool IsAuthority() const;

	//~ Begin UActorComponent Interface
public:
	virtual void OnCreatePhysicsState() override;
	virtual void OnDestroyPhysicsState() override;
	virtual bool ShouldCreatePhysicsState() const override;
	virtual bool HasValidPhysicsState() const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent Interface

	//~ Begin USceneComponent Interface
public:
	virtual void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport = ETeleportType::None) override;
	//~ End USceneComponent Interface

	//~ Begin IPhysicsComponent Interface.
public:
	virtual Chaos::FPhysicsObject* GetPhysicsObjectById(int32 Id) const override;
	virtual Chaos::FPhysicsObject* GetPhysicsObjectByName(const FName& Name) const override;
	virtual TArray<Chaos::FPhysicsObject*> GetAllPhysicsObjects() const override;
	//~ End IPhysicsComponent Interface.

	//~ Begin UObject Interface.
public:
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	//~ End UObject Interface.
};