// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Component/ComponentElementEditorWorldInterface.h"
#include "Elements/Component/ComponentElementData.h"
#include "Components/ActorComponent.h"

#include "Editor.h"

void UComponentElementEditorWorldInterface::NotifyMovementStarted(const FTypedElementHandle& InElementHandle)
{
	if (UActorComponent* Component = ComponentElementDataUtil::GetComponentFromHandle(InElementHandle))
	{
		GEditor->BroadcastBeginObjectMovement(*Component);
	}
}

void UComponentElementEditorWorldInterface::NotifyMovementOngoing(const FTypedElementHandle& InElementHandle)
{
	if (UActorComponent* Component = ComponentElementDataUtil::GetComponentFromHandle(InElementHandle))
	{
		if (AActor* Actor = Component->GetOwner())
		{
			Actor->PostEditMove(false);
		}
	}
}

void UComponentElementEditorWorldInterface::NotifyMovementEnded(const FTypedElementHandle& InElementHandle)
{
	if (UActorComponent* Component = ComponentElementDataUtil::GetComponentFromHandle(InElementHandle))
	{
		GEditor->BroadcastEndObjectMovement(*Component);
		if (AActor* Actor = Component->GetOwner())
		{
			Actor->PostEditMove(true);
			Actor->InvalidateLightingCache();
		}

		Component->MarkPackageDirty();
	}
}
