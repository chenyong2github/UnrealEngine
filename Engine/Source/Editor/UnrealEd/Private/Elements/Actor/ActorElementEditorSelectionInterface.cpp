// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Actor/ActorElementEditorSelectionInterface.h"
#include "Elements/Actor/ActorElementData.h"
#include "GameFramework/Actor.h"

#include "Elements/Framework/TypedElementList.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Object/ObjectElementEditorSelectionInterface.h"

bool UActorElementEditorSelectionInterface::IsElementSelected(const FTypedElementHandle& InElementHandle, const UTypedElementList* InSelectionSet, const FTypedElementIsSelectedOptions& InSelectionOptions)
{
	const AActor* Actor = ActorElementDataUtil::GetActorFromHandle(InElementHandle);
	return Actor && IsActorSelected(Actor, InSelectionSet, InSelectionOptions);
}

bool UActorElementEditorSelectionInterface::ShouldPreventTransactions(const FTypedElementHandle& InElementHandle)
{
	const AActor* Actor = ActorElementDataUtil::GetActorFromHandle(InElementHandle);
	return Actor && UObjectElementEditorSelectionInterface::ShouldObjectPreventTransactions(Actor);
}

void UActorElementEditorSelectionInterface::WriteTransactedElement(const FTypedElementHandle& InElementHandle, FArchive& InArchive)
{
	if (const AActor* Actor = ActorElementDataUtil::GetActorFromHandle(InElementHandle))
	{
		UObjectElementEditorSelectionInterface::WriteTransactedObject(Actor, InArchive);
	}
}

FTypedElementHandle UActorElementEditorSelectionInterface::ReadTransactedElement(FArchive& InArchive)
{
	return UObjectElementEditorSelectionInterface::ReadTransactedObject(InArchive, [](const UObject* InObject)
	{
		return UEngineElementsLibrary::AcquireEditorActorElementHandle(CastChecked<const AActor>(InObject));
	});
}

bool UActorElementEditorSelectionInterface::IsActorSelected(const AActor* InActor, const UTypedElementList* InSelectionSet, const FTypedElementIsSelectedOptions& InSelectionOptions)
{
	if (InSelectionSet->Num() == 0)
	{
		return false;
	}

	if (FTypedElementHandle ActorElement = UEngineElementsLibrary::AcquireEditorActorElementHandle(InActor, /*bAllowCreate*/false))
	{
		if (InSelectionSet->Contains(ActorElement))
		{
			return true;
		}
	}

	if (InSelectionOptions.AllowIndirect())
	{
		if (const AActor* RootSelectionActor = InActor->GetRootSelectionParent())
		{
			if (FTypedElementHandle RootSelectionElement = UEngineElementsLibrary::AcquireEditorActorElementHandle(RootSelectionActor, /*bAllowCreate*/false))
			{
				return InSelectionSet->Contains(RootSelectionElement);
			}
		}
	}

	return false;
}
