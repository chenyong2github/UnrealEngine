// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/SMInstance/SMInstanceElementLevelEditorSelectionCustomization.h"
#include "Elements/SMInstance/SMInstanceElementData.h"
#include "Components/InstancedStaticMeshComponent.h"

#include "Elements/Framework/TypedElementList.h"
#include "Elements/Framework/EngineElementsLibrary.h"

DEFINE_LOG_CATEGORY_STATIC(LogSMInstanceLevelEditorSelection, Log, All);

bool FSMInstanceElementLevelEditorSelectionCustomization::CanSelectElement(const TTypedElement<UTypedElementSelectionInterface>& InElementSelectionHandle, const FTypedElementSelectionOptions& InSelectionOptions)
{
	const FSMInstanceId SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandleChecked(InElementSelectionHandle);
	
	// Bail if global selection is locked
	return !GEdSelectionLock;
}

bool FSMInstanceElementLevelEditorSelectionCustomization::CanDeselectElement(const TTypedElement<UTypedElementSelectionInterface>& InElementSelectionHandle, const FTypedElementSelectionOptions& InSelectionOptions)
{
	const FSMInstanceId SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandleChecked(InElementSelectionHandle);

	// Bail if global selection is locked
	return !GEdSelectionLock;
}

bool FSMInstanceElementLevelEditorSelectionCustomization::SelectElement(const TTypedElement<UTypedElementSelectionInterface>& InElementSelectionHandle, UTypedElementList* InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions)
{
	const FSMInstanceId SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandleChecked(InElementSelectionHandle);

	if (!InElementSelectionHandle.SelectElement(InSelectionSet, InSelectionOptions))
	{
		return false;
	}

	UE_LOG(LogSMInstanceLevelEditorSelection, Verbose, TEXT("Selected SMInstance: %s (%s), Index %d"), *SMInstance.ISMComponent->GetPathName(), *SMInstance.ISMComponent->GetClass()->GetName(), SMInstance.InstanceIndex);

	// Update the internal selection state for viewport selection rendering
	SMInstance.ISMComponent->SelectInstance(/*bIsSelected*/true, SMInstance.InstanceIndex);
	
	return true;
}

bool FSMInstanceElementLevelEditorSelectionCustomization::DeselectElement(const TTypedElement<UTypedElementSelectionInterface>& InElementSelectionHandle, UTypedElementList* InSelectionSet, const FTypedElementSelectionOptions& InSelectionOptions)
{
	const FSMInstanceId SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandleChecked(InElementSelectionHandle);

	if (!InElementSelectionHandle.DeselectElement(InSelectionSet, InSelectionOptions))
	{
		return false;
	}

	UE_LOG(LogSMInstanceLevelEditorSelection, Verbose, TEXT("Deselected SMInstance: %s (%s), Index %d"), *SMInstance.ISMComponent->GetPathName(), *SMInstance.ISMComponent->GetClass()->GetName(), SMInstance.InstanceIndex);

	// Update the internal selection state for viewport selection rendering
	SMInstance.ISMComponent->SelectInstance(/*bIsSelected*/false, SMInstance.InstanceIndex);

	return true;
}
