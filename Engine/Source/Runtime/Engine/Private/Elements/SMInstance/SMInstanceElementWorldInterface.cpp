// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/SMInstance/SMInstanceElementWorldInterface.h"
#include "Elements/SMInstance/SMInstanceElementData.h"
#include "Components/InstancedStaticMeshComponent.h"

#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Framework/TypedElementSelectionSet.h"

#include "Engine/StaticMesh.h"

bool USMInstanceElementWorldInterface::CanEditElement(const FTypedElementHandle& InElementHandle)
{
	const FSMInstanceId SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandle(InElementHandle);
	return SMInstance && CanEditSMInstance(SMInstance);
}

bool USMInstanceElementWorldInterface::IsTemplateElement(const FTypedElementHandle& InElementHandle)
{
	const FSMInstanceId SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandle(InElementHandle);
	return SMInstance && SMInstance.ISMComponent->IsTemplate();
}

ULevel* USMInstanceElementWorldInterface::GetOwnerLevel(const FTypedElementHandle& InElementHandle)
{
	if (const FSMInstanceId SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandle(InElementHandle))
	{
		if (const AActor* ComponentOwner = SMInstance.ISMComponent->GetOwner())
		{
			return ComponentOwner->GetLevel();
		}
	}

	return nullptr;
}

UWorld* USMInstanceElementWorldInterface::GetOwnerWorld(const FTypedElementHandle& InElementHandle)
{
	const FSMInstanceId SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandle(InElementHandle);
	return SMInstance ? SMInstance.ISMComponent->GetWorld() : nullptr;
}

bool USMInstanceElementWorldInterface::GetBounds(const FTypedElementHandle& InElementHandle, FBoxSphereBounds& OutBounds)
{
	if (const FSMInstanceId SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandle(InElementHandle))
	{
		if (const UStaticMesh* StaticMesh = SMInstance.ISMComponent->GetStaticMesh())
		{
			OutBounds = StaticMesh->GetBounds();
		}
		else
		{
			OutBounds = FBoxSphereBounds();
		}

		FTransform InstanceTransform;
		SMInstance.ISMComponent->GetInstanceTransform(SMInstance.InstanceIndex, InstanceTransform, /*bWorldSpace*/true);

		OutBounds = OutBounds.TransformBy(InstanceTransform);
		return true;
	}

	return false;
}

bool USMInstanceElementWorldInterface::GetWorldTransform(const FTypedElementHandle& InElementHandle, FTransform& OutTransform)
{
	const FSMInstanceId SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandle(InElementHandle);
	return SMInstance && SMInstance.ISMComponent->GetInstanceTransform(SMInstance.InstanceIndex, OutTransform, /*bWorldSpace*/true);
}

bool USMInstanceElementWorldInterface::SetWorldTransform(const FTypedElementHandle& InElementHandle, const FTransform& InTransform)
{
	const FSMInstanceId SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandle(InElementHandle);
	return SMInstance && SMInstance.ISMComponent->UpdateInstanceTransform(SMInstance.InstanceIndex, InTransform, /*bWorldSpace*/true, /*bMarkRenderStateDirty*/true);
}

bool USMInstanceElementWorldInterface::GetRelativeTransform(const FTypedElementHandle& InElementHandle, FTransform& OutTransform)
{
	const FSMInstanceId SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandle(InElementHandle);
	return SMInstance && SMInstance.ISMComponent->GetInstanceTransform(SMInstance.InstanceIndex, OutTransform, /*bWorldSpace*/false);
}

bool USMInstanceElementWorldInterface::SetRelativeTransform(const FTypedElementHandle& InElementHandle, const FTransform& InTransform)
{
	const FSMInstanceId SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandle(InElementHandle);
	return SMInstance && SMInstance.ISMComponent->UpdateInstanceTransform(SMInstance.InstanceIndex, InTransform, /*bWorldSpace*/false, /*bMarkRenderStateDirty*/true);
}

bool USMInstanceElementWorldInterface::CanEditSMInstance(const FSMInstanceId& InSMInstanceId)
{
	return InSMInstanceId.ISMComponent->IsEditableWhenInherited();
}
