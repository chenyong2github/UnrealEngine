// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Actor/ActorElementObjectInterface.h"
#include "Elements/Actor/ActorElementData.h"
#include "GameFramework/Actor.h"

UObject* UActorElementObjectInterface::GetObject(const FTypedElementHandle& InElementHandle)
{
	return ActorElementDataUtil::GetActorFromHandle(InElementHandle);
}
