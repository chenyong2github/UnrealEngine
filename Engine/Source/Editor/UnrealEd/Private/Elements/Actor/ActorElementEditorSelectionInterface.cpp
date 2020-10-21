// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Actor/ActorElementEditorSelectionInterface.h"
#include "Elements/Actor/ActorElementData.h"
#include "GameFramework/Actor.h"
#include "TypedElementList.h"
#include "Elements/EngineElementsLibrary.h"

bool UActorElementEditorSelectionInterface::IsElementSelected(const FTypedElementHandle& InElementHandle, const UTypedElementList* InSelectionSet, const FTypedElementIsSelectedOptions& InSelectionOptions)
{
	const FActorElementData* ActorData = InElementHandle.GetData<FActorElementData>();
	return ActorData && IsActorSelected(ActorData->Actor, InSelectionSet, InSelectionOptions);
}

bool UActorElementEditorSelectionInterface::IsActorSelected(const AActor* InActor, const UTypedElementList* InSelectionSet, const FTypedElementIsSelectedOptions& InSelectionOptions)
{
	if (InSelectionSet->Num() == 0)
	{
		return false;
	}

	if (InSelectionSet->Contains(UEngineElementsLibrary::AcquireEditorActorElementHandle(InActor)))
	{
		return true;
	}

	if (InSelectionOptions.AllowIndirect())
	{
		if (const AActor* RootSelectionActor = InActor->GetRootSelectionParent())
		{
			return InSelectionSet->Contains(UEngineElementsLibrary::AcquireEditorActorElementHandle(RootSelectionActor));
		}
	}

	return false;
}
