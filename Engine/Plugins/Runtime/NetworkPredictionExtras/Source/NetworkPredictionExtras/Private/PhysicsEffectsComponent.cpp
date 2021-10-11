// Copyright Epic Games, Inc. All Rights Reserved.

#include "PhysicsEffectsComponent.h"
#include "Net/UnrealNetwork.h"

UPhysicsEffectsComponent::UPhysicsEffectsComponent()
{
	bWantsInitializeComponent = true;
	bAutoActivate = true;
	SetIsReplicatedByDefault(true);

	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_DuringPhysics;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UPhysicsEffectsComponent::InitializeComponent()
{
	Super::InitializeComponent();
#if WITH_CHAOS
	UWorld* World = GetWorld();	
	checkSlow(World);
	
	FPhysicsEffectLocalState LocalState;
	LocalState.Proxy = this->GetManagedProxy();
	LocalState.QueryParams.AddIgnoredActor(GetOwner());
		
	if (LocalState.Proxy)
	{
		if (ensure(NetworkPredictionProxy.RegisterProxy(GetWorld())))
		{
			InitializePhysicsEffects(MoveTemp(LocalState));
		}
		else
		{
			UE_LOG(LogNetworkPhysics, Warning, TEXT("No valid physics body found on %s"), *GetName());
		}
	}
#endif
}

void UPhysicsEffectsComponent::UninitializeComponent()
{
	Super::UninitializeComponent();
	NetworkPredictionProxy.UnregisterProxy();
}

void UPhysicsEffectsComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction *ThisTickFunction)
{
	NetworkPredictionProxy.RegisterController(nullptr); // fixme this is needed but shouldn't be
	SyncPhysicsEffects();
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UPhysicsEffectsComponent::PreReplication(IRepChangedPropertyTracker & ChangedPropertyTracker)
{
	Super::PreReplication(ChangedPropertyTracker);
	NetworkPredictionProxy.OnPreReplication();
}

void UPhysicsEffectsComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME( UPhysicsEffectsComponent, NetworkPredictionProxy);	
}

bool UPhysicsEffectsComponent::IsController() const
{
	const bool bIServer = GetOwnerRole() == ROLE_Authority;
	return bIServer;
}