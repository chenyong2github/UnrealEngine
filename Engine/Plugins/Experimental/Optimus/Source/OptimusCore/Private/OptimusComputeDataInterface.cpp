// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusComputeDataInterface.h"

#include "UObject/UObjectIterator.h"
#include "Components/SkeletalMeshComponent.h"
#include "ComputeFramework/ComputeGraph.h"
#include "ComputeFramework/ComputeGraphComponent.h"
#include "DataInterfaces/DataInterfaceSkeletalMeshRead.h"
#include "DataInterfaces/DataInterfaceSkinCacheWrite.h"
#include "DataInterfaces/DataInterfaceScene.h"
#include "DataInterfaces/DataInterfaceRawBuffer.h"
#include "SkeletalRenderPublic.h"


// Cached list of data interfaces.
TArray<UClass*> UOptimusComputeDataInterface::CachedClasses;


TArray<UClass*> UOptimusComputeDataInterface::GetAllComputeDataInterfaceClasses()
{
	if (CachedClasses.IsEmpty())
	{
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (!Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NotPlaceable) &&
				Class->IsChildOf(StaticClass()))
			{
				UOptimusComputeDataInterface* DataInterface = Cast<UOptimusComputeDataInterface>(Class->GetDefaultObject());
				if (DataInterface && DataInterface->IsVisible())
				{
					CachedClasses.Add(Class);
				}
			}
		}
	}
	return CachedClasses;
}


void UOptimusDataInterfaceHelpers::InitDataProviders(UComputeGraphComponent* ComputeGraphComponent, USkeletalMeshComponent* SkeletalMeshComponent)
{
	if (ComputeGraphComponent == nullptr || ComputeGraphComponent->ComputeGraph == nullptr || SkeletalMeshComponent == nullptr)
	{
		return;
	}

	ComputeGraphComponent->DataProviders.Reset();
	ComputeGraphComponent->ComputeGraph->CreateDataProviders(ComputeGraphComponent, false, ComputeGraphComponent->DataProviders);

	for (UComputeDataProvider* DataProvider : ComputeGraphComponent->DataProviders)
	{
		if (USkeletalMeshReadDataProvider* SkeletalMeshReadDataProvider = Cast<USkeletalMeshReadDataProvider>(DataProvider))
		{
			SkeletalMeshReadDataProvider->SkeletalMesh = SkeletalMeshComponent;
		}
		else if (USkeletalMeshSkinCacheDataProvider* SkeletalMeshSkinCacheDataProvider = Cast<USkeletalMeshSkinCacheDataProvider>(DataProvider))
		{
			SkeletalMeshSkinCacheDataProvider->SkeletalMesh = SkeletalMeshComponent;
		}
		else if (USceneDataProvider* SceneDataProvider = Cast<USceneDataProvider>(DataProvider))
		{
			SceneDataProvider->SceneComponent = SkeletalMeshComponent;
		}
		else if (UTransientBufferDataProvider* TransientBufferDataProvider = Cast<UTransientBufferDataProvider>(DataProvider))
		{
			if (SkeletalMeshComponent->MeshObject)
			{
				FSkeletalMeshRenderData const& SkeletalMeshRenderData = SkeletalMeshComponent->MeshObject->GetSkeletalMeshRenderData();
				FSkeletalMeshLODRenderData const* LodRenderData = SkeletalMeshRenderData.GetPendingFirstLOD(0);
				TransientBufferDataProvider->NumElements = LodRenderData->GetNumVertices();
			}
			else
			{
				TransientBufferDataProvider->NumElements = 0;
			}

			// For retained buffers we will probably want to clear them beforehand to keep up
			// with the principle of least surprise.
			TransientBufferDataProvider->bClearBeforeUse = false;
		}
	}
}
