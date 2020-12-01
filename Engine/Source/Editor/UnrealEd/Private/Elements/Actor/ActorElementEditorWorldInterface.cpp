// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Actor/ActorElementEditorWorldInterface.h"
#include "Elements/Actor/ActorElementData.h"
#include "GameFramework/Actor.h"

#include "Editor.h"

void UActorElementEditorWorldInterface::NotifyMovementStarted(const FTypedElementHandle& InElementHandle)
{
	if (AActor* Actor = ActorElementDataUtil::GetActorFromHandle(InElementHandle))
	{
		GEditor->BroadcastBeginObjectMovement(*Actor);
	}
}

void UActorElementEditorWorldInterface::NotifyMovementOngoing(const FTypedElementHandle& InElementHandle)
{
	if (AActor* Actor = ActorElementDataUtil::GetActorFromHandle(InElementHandle))
	{
		Actor->PostEditMove(false);
	}
}

void UActorElementEditorWorldInterface::NotifyMovementEnded(const FTypedElementHandle& InElementHandle)
{
	if (AActor* Actor = ActorElementDataUtil::GetActorFromHandle(InElementHandle))
	{
		GEditor->BroadcastEndObjectMovement(*Actor);
		Actor->PostEditMove(true);

		Actor->InvalidateLightingCache();
		Actor->UpdateComponentTransforms();
		Actor->MarkPackageDirty();
	}
}
