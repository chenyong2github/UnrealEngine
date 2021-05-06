// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/SMInstance/SMInstanceElementAssetDataInterface.h"

#include "Elements/SMInstance/SMInstanceElementData.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"

FAssetData USMInstanceElementAssetDataInterface::GetAssetData(const FTypedElementHandle& InElementHandle)
{
	if (FSMInstanceId SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandle(InElementHandle))
	{
		if (SMInstance.ISMComponent->GetStaticMesh())
		{
			return FAssetData(SMInstance.ISMComponent->GetStaticMesh());
		}
	}

	return FAssetData();
}
