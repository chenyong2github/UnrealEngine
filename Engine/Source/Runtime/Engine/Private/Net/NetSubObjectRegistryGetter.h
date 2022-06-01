// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "Net/Core/Misc/NetSubObjectRegistry.h"

namespace UE::Net
{

/** Helper class to restrict access to the subobject list of actors and actor components */
class FSubObjectRegistryGetter final
{
public:
	using FReplicatedComponentInfo = UE::Net::FReplicatedComponentInfo;

	FSubObjectRegistryGetter() = delete;
	~FSubObjectRegistryGetter() = delete;

	using FSubObjectRegistry = UE::Net::FSubObjectRegistry;

	static const FSubObjectRegistry& GetSubObjects(AActor* InActor)
	{
		return InActor->ReplicatedSubObjects;
	}

	static const FSubObjectRegistry* GetSubObjectsOfActorCompoment(AActor* InActor, UActorComponent* InActorComp)
	{
		UE::Net::FReplicatedComponentInfo* ComponentInfo = InActor->ReplicatedComponentsInfo.FindByKey(InActorComp);
		return ComponentInfo ? &(ComponentInfo->SubObjects) : nullptr;
	}

	static const TArray<UE::Net::FReplicatedComponentInfo>& GetReplicatedComponents(AActor* InActor)
	{
		return InActor->ReplicatedComponentsInfo;
	}

	static const FReplicatedComponentInfo* GetReplicatedComponentInfoForComponent(AActor* InActor, UActorComponent* InActorComp)
	{
		return InActor->ReplicatedComponentsInfo.FindByKey(InActorComp);
	}

	static bool IsSubObjectInRegistry(AActor* InActor, UActorComponent* InActorComp, UObject* InSubObject)
	{
		UE::Net::FReplicatedComponentInfo* ComponentInfo = InActor->ReplicatedComponentsInfo.FindByKey(InActorComp);
		return ComponentInfo ? ComponentInfo->SubObjects.IsSubObjectInRegistry(InSubObject) : false;
	}
};

}
