// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Component/ComponentElementEditorSelectionInterface.h"
#include "Elements/Component/ComponentElementData.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"

#include "Elements/Framework/TypedElementList.h"
#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Object/ObjectElementEditorSelectionInterface.h"

bool UComponentElementEditorSelectionInterface::IsElementSelected(const FTypedElementHandle& InElementHandle, const UTypedElementList* InSelectionSet, const FTypedElementIsSelectedOptions& InSelectionOptions)
{
	const UActorComponent* Component = ComponentElementDataUtil::GetComponentFromHandleChecked(InElementHandle);
	return Component && IsComponentSelected(Component, InSelectionSet, InSelectionOptions);
}

bool UComponentElementEditorSelectionInterface::ShouldPreventTransactions(const FTypedElementHandle& InElementHandle)
{
	const UActorComponent* Component = ComponentElementDataUtil::GetComponentFromHandleChecked(InElementHandle);
	return Component && UObjectElementEditorSelectionInterface::ShouldObjectPreventTransactions(Component);
}

void UComponentElementEditorSelectionInterface::WriteTransactedElement(const FTypedElementHandle& InElementHandle, FArchive& InArchive)
{
	if (const UActorComponent* Component = ComponentElementDataUtil::GetComponentFromHandleChecked(InElementHandle))
	{
		UObjectElementEditorSelectionInterface::WriteTransactedObject(Component, InArchive);
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
