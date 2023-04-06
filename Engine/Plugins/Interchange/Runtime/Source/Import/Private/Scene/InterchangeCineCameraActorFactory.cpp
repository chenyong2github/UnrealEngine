// Copyright Epic Games, Inc. All Rights Reserved.

#include "Scene/InterchangeCineCameraActorFactory.h"

#include "InterchangeCineCameraFactoryNode.h"
#include "Scene/InterchangeActorFactory.h"
#include "Scene/InterchangeActorHelper.h"

#include "CineCameraActor.h"
#include "CineCameraComponent.h"
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(InterchangeCineCameraActorFactory)

UClass* UInterchangeCineCameraActorFactory::GetFactoryClass() const
{
	return ACineCameraActor::StaticClass();
}

UObject* UInterchangeCineCameraActorFactory::ProcessActor(AActor& SpawnedActor, const UInterchangeActorFactoryNode& /*FactoryNode*/, const UInterchangeBaseNodeContainer& /*NodeContainer*/)
{
	ACineCameraActor* CineCameraActor = Cast<ACineCameraActor>(&SpawnedActor);

	return CineCameraActor ? CineCameraActor->GetCineCameraComponent() : nullptr;
}


