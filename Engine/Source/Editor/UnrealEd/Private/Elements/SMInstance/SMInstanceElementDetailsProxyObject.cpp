// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/SMInstance/SMInstanceElementDetailsProxyObject.h"
#include "Components/InstancedStaticMeshComponent.h"

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

bool USMInstanceElementDetailsProxyObject::Modify(bool bAlwaysMarkDirty)
{
	if (FSMInstanceId InstanceId = GetInstanceId())
	{
		return InstanceId.ISMComponent->Modify(bAlwaysMarkDirty);
	}

	return false;
}

void USMInstanceElementDetailsProxyObject::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property)
	{
		if (PropertyChangedEvent.Property->GetFName() == GET_MEMBER_NAME_CHECKED(USMInstanceElementDetailsProxyObject, Transform))
		{
			if (FSMInstanceId InstanceId = GetInstanceId())
			{
				// TODO: Need flag for local/world space, like FComponentTransformDetails
				InstanceId.ISMComponent->UpdateInstanceTransform(InstanceId.InstanceIndex, Transform, /*bWorldSpace*/false, /*bMarkRenderStateDirty*/true);
				
				GUnrealEd->UpdatePivotLocationForSelection();
				GUnrealEd->RedrawLevelEditingViewports();
			}
		}
	}

	Super::PostEditChangeChainProperty(PropertyChangedEvent);
}

void USMInstanceElementDetailsProxyObject::SyncProxyStateFromInstance()
{
	if (FSMInstanceId InstanceId = GetInstanceId())
	{
		// TODO: Need flag for local/world space, like FComponentTransformDetails
		InstanceId.ISMComponent->GetInstanceTransform(InstanceId.InstanceIndex, Transform, /*bWorldSpace*/false);
	}
	else
	{
		Transform = FTransform::Identity;
	}
}

FSMInstanceId USMInstanceElementDetailsProxyObject::GetInstanceId() const
{
	return FSMInstanceElementIdMap::Get().GetSMInstanceIdFromSMInstanceElementId(FSMInstanceElementId{ ISMComponent.Get(), ISMInstanceId });
}
