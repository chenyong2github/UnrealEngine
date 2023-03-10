// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Components/ActorComponent.h"
#include "GenericPlatform/GenericPlatformMisc.h"

#include "ClusterUnionReplicatedProxyComponent.generated.h"

class UClusterUnionComponent;
class UPrimitiveComponent;

/**
 * This component lets us store replicated information about how any particular UPrimitiveComponent
 * should be attached to its parent cluster union. The benefits of using a separate components are:
 * 
 *	1) It lets us avoid adding any additional overhead into the UPrimitiveComponent.
 *  2) It lets the replicated information have the same net relevancy as the actor being added to the cluster union
 *     rather than having the same net relevancy as the cluster union (i.e. in the case of replicating this data in
 *     an array in the ClusterUnionComponent).
 *  3) It lets us pinpoint what exactly is being added/removed (vs if all this data was stored in an array) which lets
 *     us be a bit more efficient in terms of modifying the cluster union.
 */
UCLASS()
class ENGINE_API UClusterUnionReplicatedProxyComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UClusterUnionReplicatedProxyComponent(const FObjectInitializer& ObjectInitializer);

	UFUNCTION()
	UClusterUnionComponent* GetParentClusterUnionComponent() const { return ParentClusterUnion; }

	UFUNCTION()
	void SetParentClusterUnion(UClusterUnionComponent* InComponent);

	UFUNCTION()
	void SetChildClusteredComponent(UPrimitiveComponent* InComponent);

	UFUNCTION()
	void SetParticleBoneIds(const TArray<int32>& InIds);

	UFUNCTION()
	void SetParticleChildToParent(int32 BoneId, const FTransform& ChildToParent);

protected:

	UFUNCTION()
	void OnRep_ParentClusterUnion();

	UFUNCTION()
	void OnRep_ChildClusteredComponent();

	UFUNCTION()
	void OnRep_ParticleBoneIds();

	UFUNCTION()
	void OnRep_ParticleChildToParents();

private:
	UPROPERTY(ReplicatedUsing=OnRep_ParentClusterUnion)
	TObjectPtr<UClusterUnionComponent> ParentClusterUnion;

	UPROPERTY()
	bool bNetUpdateParentClusterUnion;

	UPROPERTY(ReplicatedUsing=OnRep_ChildClusteredComponent)
	TObjectPtr<UPrimitiveComponent> ChildClusteredComponent;

	UPROPERTY()
	bool bNetUpdateChildClusteredComponent;

	UPROPERTY(ReplicatedUsing=OnRep_ParticleBoneIds)
	TArray<int32> ParticleBoneIds;

	UPROPERTY()
	bool bNetUpdateParticleBoneIds;

	UPROPERTY(ReplicatedUsing=OnRep_ParticleChildToParents)
	TArray<FTransform> ParticleChildToParents;

	UPROPERTY()
	bool bNetUpdateParticleChildToParents;

	void DeferUntilChildClusteredComponentInParentUnion(TFunction<void()> Func);

	//~ Begin UActorComponent Interface
public:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent Interface

	//~ Begin UObject Interface
public:
	virtual void PostRepNotifies() override;
	//~ End UObject Interface
};