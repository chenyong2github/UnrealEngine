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

	// Set of physics objects that we actually added into the cluster union.
	TSet<Chaos::FPhysicsObjectHandle> PhysicsObjects;

	// Every physics object associated with this particular component.
	TArray<Chaos::FPhysicsObjectHandle> AllPhysicsObjects;

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
	uint8 ObjectState = 0;

	UPROPERTY()
	bool bIsAnchored = false;
};

USTRUCT()
struct FClusterUnionPendingAddData
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<int32> BoneIds;
};

/**
 * For every possible particle that could ever possibly be added into the cluster union,
 * keep track of its component and its bone id.
 */
USTRUCT()
struct FClusterUnionParticleCandidateData
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UPrimitiveComponent> Component;

	UPROPERTY()
	int32 BoneId = INDEX_NONE;
};

/**
 * This does the bulk of the work exposing a physics cluster union to the game thread.
 * This component needs to be a primitive component primarily because of how physics
 * proxies need to be registered with the solver with an association with a primitive component.
 * This component can be used as part of AClusterUnionActor or on its own as its list of
 * clustered components/actors can be specified dynamically at runtime and/or statically
 * on asset creation.
 * 
 * The cluster union component needs to not only maintain a game thread representation of what's happening on the
 * physics thread but it also needs to make sure this data gets replicated to every client. A general model of how
 * the data flow happens is as follows:
 * 
 *  [Server GT Command] -> [Server PT Command] -> [Server Modifies PT Data] -> [Server Sync PT Data back to GT Data].
 * 
 * This enables GT control over what happens to the cluster union BUT ALSO maintains a physics-first approach
 * to the cluster union where a physics event can possibly cause the cluster union to break.
 * 
 * The GT data is replicated from the server to the clients either via the FClusterUnionReplicatedData on the cluster union component
 * or per-child component data is replicated via the UClusterUnionReplicatedProxyComponent. Generally, the same flow is
 * replicated on the client. The only exception is for replicating the X/R/V/W properties on the cluster union particle which does
 * a GT -> PT data sync. There's no particula reason this happens...it just mirrors the single particle physics proxy here.
 *
 */
UCLASS()
class ENGINE_API UClusterUnionComponent : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	UClusterUnionComponent(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category="Cluster Union")
	void AddComponentToCluster(UPrimitiveComponent* InComponent, const TArray<int32>& BoneIds);

	UFUNCTION(BlueprintCallable, Category = "Cluster Union")
	void RemoveComponentFromCluster(UPrimitiveComponent* InComponent);

	UFUNCTION(BlueprintCallable, Category = "Cluster Union")
	void SetIsAnchored(bool bIsAnchored);

	// The SyncVelocitiesFromPhysics will set replicated state using data from the physics thread. 
	void SyncVelocitiesFromPhysics(const FVector& LinearVelocity, const FVector& AngularVelocity);
	// SyncClusterUnionFromProxy will examine the make up of the cluster union (particles, child to parent, etc.) and do whatever is needed on the GT in terms of bookkeeping.
	void SyncClusterUnionFromProxy();

	UFUNCTION()
	bool IsComponentAdded(UPrimitiveComponent* Component) { return ComponentToPhysicsObjects.Contains(Component); }

	friend class UClusterUnionReplicatedProxyComponent;
protected:

	// This should only be called on the client when replication happens.
	UFUNCTION()
	void ForceSetChildToParent(UPrimitiveComponent* InComponent, const TArray<int32>& BoneIds, const TArray<FTransform>& ChildToParent);

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

	// Also keep track of which actors we are clustering and their components. We make modifications on
	// actors that get clustered so we need to make sure we undo those changes only once all its clustered
	// components are removed from the cluster.
	TMap<TObjectKey<AActor>, FClusteredActorData> ActorToComponents;

	// Sometimes we might be in the process of waiting for a component to create it physics state before adding to the cluster.
	// Make sure we don't try to add the component multiples times while the add is pending.
	TMap<TObjectKey<UPrimitiveComponent>, FClusterUnionPendingAddData> PendingComponentsToAdd;

	// Given a unique index of a particle that we're adding to the cluster union - map it back to the component that owns it.
	// This works decently because we assume that when we're using a cluster union component, we will only try to add to the
	// cluster union via the GT so we can guarantee to have a decent mapping here.
	UPROPERTY()
	TMap<int32, FClusterUnionParticleCandidateData> UniqueIdxToComponent;

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

	// These functions only get called when the physics thread syncs to the game thread thereby enforcing a physics thread authoritative view of
	// what particles are currently contained within the cluster union.
	void HandleAddOrModifiedClusteredComponent(UPrimitiveComponent* ChangedComponent, const TMap<int32, FTransform>& PerBoneChildToParent);
	void HandleRemovedClusteredComponent(UPrimitiveComponent* ChangedComponent, bool bDestroyReplicatedProxy);

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

	//~ Begin UPrimitiveComponent Interface
public:
	virtual FBodyInstance* GetBodyInstance(FName BoneName, bool bGetWelded, int32 Index) const override { return nullptr; }
	virtual void SetSimulatePhysics(bool bSimulate) override;
	virtual bool CanEditSimulatePhysics() override { return true; }
	virtual bool LineTraceComponent(FHitResult& OutHit, const FVector Start, const FVector End, const FCollisionQueryParams& Params) override;
	virtual bool SweepComponent(FHitResult& OutHit, const FVector Start, const FVector End, const FQuat& ShapeWorldRotation, const FCollisionShape& CollisionShape, bool bTraceComplex) override;
	virtual bool OverlapComponentWithResult(const FVector& Pos, const FQuat& Rot, const FCollisionShape& CollisionShape, TArray<FOverlapResult>& OutOverlap) const override;
	virtual bool ComponentOverlapComponentWithResultImpl(const class UPrimitiveComponent* const PrimComp, const FVector& Pos, const FQuat& Rot, const FCollisionQueryParams& Params, TArray<FOverlapResult>& OutOverlap) const override;
	//~ End UPrimitiveComponent Interface

	//~ Begin USceneComponent Interface
public:
	virtual void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport) override;
	//~ End USceneComponent Interface

	//~ Begin IPhysicsComponent Interface.
public:
	virtual Chaos::FPhysicsObject* GetPhysicsObjectById(Chaos::FPhysicsObjectId Id) const override;
	virtual Chaos::FPhysicsObject* GetPhysicsObjectByName(const FName& Name) const override;
	virtual TArray<Chaos::FPhysicsObject*> GetAllPhysicsObjects() const override;
	virtual Chaos::FPhysicsObjectId GetIdFromGTParticle(Chaos::FGeometryParticle* Particle) const override;
	//~ End IPhysicsComponent Interface.

	//~ Begin UObject Interface.
public:
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	//~ End UObject Interface.
};