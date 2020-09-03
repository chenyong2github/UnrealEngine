// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkPredictionReplicatedManager.h"
#include "Net/UnrealNetwork.h"
#include "NetworkPredictionWorldManager.h"


ANetworkPredictionReplicatedManager::FOnAuthoritySpawn ANetworkPredictionReplicatedManager::OnAuthoritySpawnDelegate;
TWeakObjectPtr<ANetworkPredictionReplicatedManager> ANetworkPredictionReplicatedManager::AuthorityInstance;

ANetworkPredictionReplicatedManager::ANetworkPredictionReplicatedManager()
{
	bReplicates = true;
	NetPriority = 1000.f; // We want this to be super high priority when it replicates
	NetUpdateFrequency = 0.001f; // Very low frequency: we will use ForceNetUpdate when important data changes
	bAlwaysRelevant = true;
}

void ANetworkPredictionReplicatedManager::BeginPlay()
{
	Super::BeginPlay();
	if (GetLocalRole() == ROLE_Authority)
	{
		OnAuthoritySpawnDelegate.Broadcast(this);
	}
	else
	{
		UNetworkPredictionWorldManager* NetworkPredictionWorldManager = GetWorld()->GetSubsystem<UNetworkPredictionWorldManager>();
		npCheckSlow(NetworkPredictionWorldManager);
		NetworkPredictionWorldManager->ReplicatedManager = this;
	}
}

void ANetworkPredictionReplicatedManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ANetworkPredictionReplicatedManager, SharedPackageMap);
}

FDelegateHandle ANetworkPredictionReplicatedManager::OnAuthoritySpawn(const TFunction<void(ANetworkPredictionReplicatedManager*)>& Func)
{
	if (AuthorityInstance.IsValid())
	{
		Func(AuthorityInstance.Get());
	}

	// I don't think there is a way to move a TUniqueFunction onto a delegate, so TFunction will have to do
	return OnAuthoritySpawnDelegate.AddLambda(Func);
}

uint8 ANetworkPredictionReplicatedManager::GetIDForObject(UObject* Obj)
{
	// Naive lookup
	for (auto It = SharedPackageMap.Items.CreateIterator(); It; ++It)
	{
		FSharedPackageMapItem& Item = *It;
		if (Item.SoftPtr.Get() == Obj)
		{
			npCheckSlow(It.GetIndex() < TNumericLimits<uint8>::Max());
			return (uint8)It.GetIndex();
		}
	}

	npEnsureMsgf(false, TEXT("Could not find Object %s in SharedPackageMap."), *GetNameSafe(Obj));
	return 0;
}

TSoftObjectPtr<UObject> ANetworkPredictionReplicatedManager::GetObjectForID(uint8 ID)
{
	if (SharedPackageMap.Items.IsValidIndex(ID))
	{
		return SharedPackageMap.Items[ID].SoftPtr;
	}

	return TSoftObjectPtr<UObject>();
}

uint8 ANetworkPredictionReplicatedManager::AddObjectToSharedPackageMap(TSoftObjectPtr<UObject> SoftPtr)
{
	if (SharedPackageMap.Items.Num()+1 >= TNumericLimits<uint8>::Max())
	{
		UE_LOG(LogTemp, Warning, TEXT("Mock SharedPackageMap has overflowed!"));
		for (FSharedPackageMapItem& Item : SharedPackageMap.Items)
		{
			UE_LOG(LogTemp, Warning, TEXT("   %s"), *Item.SoftPtr.ToString());
		}
		ensureMsgf(false, TEXT("SharedPackageMap overflowed"));
		return 0;
	}

	SharedPackageMap.Items.Add(FSharedPackageMapItem{SoftPtr});
	return SharedPackageMap.Items.Num()-1;
}