// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Component/ComponentElementEditorSelectionInterface.h"
#include "Elements/Component/ComponentElementData.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

#include "TypedElementList.h"
#include "Elements/EngineElementsLibrary.h"
#include "Elements/Object/ObjectElementEditorSelectionInterface.h"

bool UComponentElementEditorSelectionInterface::IsElementSelected(const FTypedElementHandle& InElementHandle, const UTypedElementList* InSelectionSet, const FTypedElementIsSelectedOptions& InSelectionOptions)
{
	const FComponentElementData* ComponentData = InElementHandle.GetData<FComponentElementData>();
	return ComponentData && IsComponentSelected(ComponentData->Component, InSelectionSet, InSelectionOptions);
}

bool UComponentElementEditorSelectionInterface::ShouldPreventTransactions(const FTypedElementHandle& InElementHandle)
{
	const FComponentElementData* ComponentData = InElementHandle.GetData<FComponentElementData>();
	return ComponentData && UObjectElementEditorSelectionInterface::ShouldObjectPreventTransactions(ComponentData->Component);
}

void UComponentElementEditorSelectionInterface::WriteTransactedElement(const FTypedElementHandle& InElementHandle, FArchive& InArchive)
{
	const FComponentElementData* ComponentData = InElementHandle.GetData<FComponentElementData>();
	if (ComponentData)
	{
		UObjectElementEditorSelectionInterface::WriteTransactedObject(ComponentData->Component, InArchive);
	}
}

FTypedElementHandle UComponentElementEditorSelectionInterface::ReadTransactedElement(FArchive& InArchive)
{
	return UObjectElementEditorSelectionInterface::ReadTransactedObject(InArchive, [](const UObject* InObject)
	{
		return UEngineElementsLibrary::AcquireEditorComponentElementHandle(CastChecked<const UActorComponent>(InObject));
	});
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
