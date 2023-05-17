// Copyright Epic Games, Inc. All Rights Reserved.
#include "PhysicsEngine/ChaosBlueprintLibrary.h"
#include "Physics/Experimental/ChaosEventRelay.h"

const UChaosEventRelay* UChaosBlueprintLibrary::GetEventRelayFromContext(UObject* ContextObject)
{
	if (ContextObject)
	{
		UWorld* World = GEngine->GetWorldFromContextObject(ContextObject, EGetWorldErrorMode::ReturnNull);
		if (World)
		{
			return World->GetChaosEventRelay();
		}
	}
	return nullptr;
}
