// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Component/ComponentElementEditorSelectionInterface.h"
#include "Elements/Component/ComponentElementData.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "TypedElementList.h"
#include "Elements/EngineElementsLibrary.h"

bool UComponentElementEditorSelectionInterface::IsElementSelected(const FTypedElementHandle& InElementHandle, const UTypedElementList* InSelectionSet, const FTypedElementIsSelectedOptions& InSelectionOptions)
{
	const FComponentElementData* ComponentData = InElementHandle.GetData<FComponentElementData>();
	return ComponentData && IsComponentSelected(ComponentData->Component, InSelectionSet, InSelectionOptions);
}

bool UComponentElementEditorSelectionInterface::IsComponentSelected(const UActorComponent* InComponent, const UTypedElementList* InSelectionSet, const FTypedElementIsSelectedOptions& InSelectionOptions)
{
	if (InSelectionSet->Num() == 0)
	{
		return false;
	}

	if (InSelectionSet->Contains(UEngineElementsLibrary::AcquireEditorComponentElementHandle(InComponent)))
	{
		return true;
	}

	if (InSelectionOptions.AllowIndirect())
	{
		const AActor* ConsideredActor = InComponent->GetOwner();
		const USceneComponent* ConsideredComponent = Cast<USceneComponent>(InComponent);
		if (ConsideredActor)
		{
			while (ConsideredActor->IsChildActor())
			{
				ConsideredActor = ConsideredActor->GetParentActor();
				ConsideredComponent = ConsideredActor->GetParentComponent();
			}

			while (ConsideredComponent && ConsideredComponent->IsVisualizationComponent())
			{
				ConsideredComponent = ConsideredComponent->GetAttachParent();
			}
		}

		if (ConsideredComponent)
		{
			return InSelectionSet->Contains(UEngineElementsLibrary::AcquireEditorComponentElementHandle(ConsideredComponent));
		}
	}

	return false;
}
