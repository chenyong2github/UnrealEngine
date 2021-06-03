// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/SMInstance/SMInstanceElementDetailsProxyObject.h"
#include "Components/InstancedStaticMeshComponent.h"

#include "Elements/SMInstance/SMInstanceElementData.h"

#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"

void USMInstanceElementDetailsProxyObject::Initialize(const FSMInstanceElementId& InSMInstanceElementId)
{
	ISMComponent = InSMInstanceElementId.ISMComponent;
	ISMInstanceId = InSMInstanceElementId.InstanceId;

	TickHandle = FTicker::GetCoreTicker().AddTicker(TEXT("USMInstanceElementDetailsProxyObject"), 0.1f, [this](float)
	{
		SyncProxyStateFromInstance();
		return true;
	});
	SyncProxyStateFromInstance();
}

void USMInstanceElementDetailsProxyObject::Shutdown()
{
	if (TickHandle.IsValid())
	{
		FTicker::GetCoreTicker().RemoveTicker(TickHandle);
		TickHandle.Reset();
	}

	ISMComponent.Reset();
	ISMInstanceId = 0;

	SyncProxyStateFromInstance();
}

void USMInstanceElementDetailsProxyObject::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property)
	{
		if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(USMInstanceElementDetailsProxyObject, Transform))
		{
			if (FSMInstanceManager SMInstance = GetSMInstance())
			{
				if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
				{
					if (!bIsWithinInteractiveTransformEdit)
					{
						SMInstance.NotifySMInstanceMovementStarted();
					}
					bIsWithinInteractiveTransformEdit = true;
				}

				// TODO: Need flag for local/world space, like FComponentTransformDetails
				SMInstance.SetSMInstanceTransform(Transform, /*bWorldSpace*/false, /*bMarkRenderStateDirty*/true);
				
				if (PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive)
				{
					check(bIsWithinInteractiveTransformEdit);
					SMInstance.NotifySMInstanceMovementOngoing();
				}
				else
				{
					if (bIsWithinInteractiveTransformEdit)
					{
						SMInstance.NotifySMInstanceMovementEnded();
					}
					bIsWithinInteractiveTransformEdit = false;
				}

				GUnrealEd->UpdatePivotLocationForSelection();
				GUnrealEd->RedrawLevelEditingViewports();
			}
		}
	}

	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}

void USMInstanceElementDetailsProxyObject::SyncProxyStateFromInstance()
{
	if (FSMInstanceManager SMInstance = GetSMInstance())
	{
		// TODO: Need flag for local/world space, like FComponentTransformDetails
		SMInstance.GetSMInstanceTransform(Transform, /*bWorldSpace*/false);
	}
	else
	{
		Transform = FTransform::Identity;
	}
}

FSMInstanceManager USMInstanceElementDetailsProxyObject::GetSMInstance() const
{
	const FSMInstanceId SMInstanceId = FSMInstanceElementIdMap::Get().GetSMInstanceIdFromSMInstanceElementId(FSMInstanceElementId{ ISMComponent.Get(), ISMInstanceId });
	return FSMInstanceManager(SMInstanceId, SMInstanceElementDataUtil::GetSMInstanceManager(SMInstanceId));
}
