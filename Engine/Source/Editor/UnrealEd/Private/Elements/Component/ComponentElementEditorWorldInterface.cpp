// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Component/ComponentElementEditorWorldInterface.h"
#include "Elements/Component/ComponentElementData.h"
#include "Components/ActorComponent.h"

#include "Elements/Framework/EngineElementsLibrary.h"

#include "Editor.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"

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

void UComponentElementEditorWorldInterface::DuplicateElements(TArrayView<const FTypedElementHandle> InElementHandles, UWorld* InWorld, bool bOffsetLocations, TArray<FTypedElementHandle>& OutNewElements)
{
	const TArray<UActorComponent*> ComponentsToDuplicate = ComponentElementDataUtil::GetComponentsFromHandles(InElementHandles);

	if (ComponentsToDuplicate.Num() > 0)
	{
		TArray<UActorComponent*> NewComponents;
		GUnrealEd->DuplicateComponents(ComponentsToDuplicate, NewComponents);

		OutNewElements.Reserve(OutNewElements.Num() + NewComponents.Num());
		for (UActorComponent* NewComponent : NewComponents)
		{
			OutNewElements.Add(UEngineElementsLibrary::AcquireEditorComponentElementHandle(NewComponent));
		}
	}
}
