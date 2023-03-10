// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsEngine/ClusterUnionReplicatedProxyComponent.h"

#include "Net/Core/PushModel/PushModel.h"
#include "Net/UnrealNetwork.h"
#include "PhysicsEngine/ClusterUnionComponent.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ClusterUnionReplicatedProxyComponent)

UClusterUnionReplicatedProxyComponent::UClusterUnionReplicatedProxyComponent(const FObjectInitializer& ObjectInitializer)
	: UActorComponent(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);

	ParentClusterUnion = nullptr;
	ChildClusteredComponent = nullptr;

	bNetUpdateParentClusterUnion = false;
	bNetUpdateChildClusteredComponent = false;
	bNetUpdateParticleBoneIds = false;
	bNetUpdateParticleChildToParents = false;
}

void UClusterUnionReplicatedProxyComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	UActorComponent::GetLifetimeReplicatedProps(OutLifetimeProps);

	FDoRepLifetimeParams Params;
	Params.bIsPushBased = true;

	DOREPLIFETIME_WITH_PARAMS_FAST(UClusterUnionReplicatedProxyComponent, ParentClusterUnion, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UClusterUnionReplicatedProxyComponent, ChildClusteredComponent, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UClusterUnionReplicatedProxyComponent, ParticleBoneIds, Params);
	DOREPLIFETIME_WITH_PARAMS_FAST(UClusterUnionReplicatedProxyComponent, ParticleChildToParents, Params);
}

void UClusterUnionReplicatedProxyComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UActorComponent::EndPlay(EndPlayReason);

	if (!GetOwner()->HasAuthority() && ParentClusterUnion && ChildClusteredComponent)
	{
		ParentClusterUnion->RemoveComponentFromCluster(ChildClusteredComponent);
	}
}

void UClusterUnionReplicatedProxyComponent::SetParentClusterUnion(UClusterUnionComponent* InComponent)
{
	ParentClusterUnion = InComponent;
	MARK_PROPERTY_DIRTY_FROM_NAME(UClusterUnionReplicatedProxyComponent, ParentClusterUnion, this);
}

void UClusterUnionReplicatedProxyComponent::SetChildClusteredComponent(UPrimitiveComponent* InComponent)
{
	ChildClusteredComponent = InComponent;
	MARK_PROPERTY_DIRTY_FROM_NAME(UClusterUnionReplicatedProxyComponent, ChildClusteredComponent, this);
}

void UClusterUnionReplicatedProxyComponent::SetParticleBoneIds(const TArray<int32>& InIds)
{
	ParticleBoneIds = InIds;

	ParticleChildToParents.Empty();
	ParticleChildToParents.Reserve(InIds.Num());
	for (int32 Index = 0; Index < InIds.Num(); ++Index)
	{
		ParticleChildToParents.Add(FTransform::Identity);
	}

	MARK_PROPERTY_DIRTY_FROM_NAME(UClusterUnionReplicatedProxyComponent, ParticleBoneIds, this);
}

void UClusterUnionReplicatedProxyComponent::SetParticleChildToParent(int32 BoneId, const FTransform& ChildToParent)
{
	int32 Index = INDEX_NONE;
	if (ParticleBoneIds.Find(BoneId, Index))
	{
		ParticleChildToParents[Index] = ChildToParent;
		MARK_PROPERTY_DIRTY_FROM_NAME(UClusterUnionReplicatedProxyComponent, ParticleChildToParents, this);
	}
}

void UClusterUnionReplicatedProxyComponent::OnRep_ParentClusterUnion()
{
	bNetUpdateParentClusterUnion = true;
}

void UClusterUnionReplicatedProxyComponent::OnRep_ChildClusteredComponent()
{
	bNetUpdateChildClusteredComponent = true;
}

void UClusterUnionReplicatedProxyComponent::OnRep_ParticleBoneIds()
{
	bNetUpdateParticleBoneIds = true;
}

void UClusterUnionReplicatedProxyComponent::OnRep_ParticleChildToParents()
{
	bNetUpdateParticleChildToParents = true;
}

void UClusterUnionReplicatedProxyComponent::PostRepNotifies()
{
	UActorComponent::PostRepNotifies();

	// These three properties should only get set once when the component is created.
	const bool bIsInitialReplication = bNetUpdateParentClusterUnion || bNetUpdateChildClusteredComponent || bNetUpdateParticleBoneIds;
	const bool bIsValid = ParentClusterUnion && ChildClusteredComponent && !ParticleBoneIds.IsEmpty();
	if (bIsInitialReplication)
	{
		if (bIsValid)
		{
			ParentClusterUnion->AddComponentToCluster(ChildClusteredComponent, ParticleBoneIds);
		}

		bNetUpdateParentClusterUnion = false;
		bNetUpdateChildClusteredComponent = false;
		bNetUpdateParticleBoneIds = false;
	}

	if (bIsValid && bNetUpdateParticleChildToParents && ParticleBoneIds.Num() == ParticleChildToParents.Num())
	{
		// This particular bit can't happen utnil *after* we add the component to the cluster union. There's an additional deferral
		// in AddComponentToCluster that we have to wait for.
		DeferUntilChildClusteredComponentInParentUnion(
			[this]()
			{
				ParentClusterUnion->ForceSetChildToParent(ChildClusteredComponent, ParticleBoneIds, ParticleChildToParents);
			}
		);
		bNetUpdateParticleChildToParents = false;
	}
}

void UClusterUnionReplicatedProxyComponent::DeferUntilChildClusteredComponentInParentUnion(TFunction<void()> Func)
{
	if (!ParentClusterUnion || !ChildClusteredComponent)
	{
		return;
	}

	if (ParentClusterUnion->IsComponentAdded(ChildClusteredComponent))
	{
		Func();
	}
	else
	{
		GetOwner()->GetWorldTimerManager().SetTimerForNextTick(
			[this, Func]()
			{
				DeferUntilChildClusteredComponentInParentUnion(Func);
			}
		);
	}
}