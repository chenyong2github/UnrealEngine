// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Containers/DisplayClusterRender_MeshComponent.h"
#include "Render/Containers/DisplayClusterRender_MeshComponentProxy.h"
#include "Render/Containers/DisplayClusterRender_MeshComponentProxyData.h"

#include "Misc/DisplayClusterLog.h"

static void ImplUpdateMeshComponentProxyData(FDisplayClusterRender_MeshComponentProxy* MeshComponentProxy, FDisplayClusterRender_MeshComponentProxyData* NewProxyData)
{
	if (MeshComponentProxy)
	{
		ENQUEUE_RENDER_COMMAND(DisplayClusterRender_MeshComponent_UpdateProxyData)(
			[MeshComponentProxy, NewProxyData](FRHICommandListImmediate& RHICmdList)
		{
			// Update RHI
			if (NewProxyData && NewProxyData->IsValid())
			{
				MeshComponentProxy->UpdateRHI_RenderThread(RHICmdList, NewProxyData);
				delete NewProxyData;
			}
			else
			{
				MeshComponentProxy->Release_RenderThread();
			}
		});
	}
}

static void ImplDeleteMeshComponentProxy(FDisplayClusterRender_MeshComponentProxy* MeshComponentProxy)
{
	ENQUEUE_RENDER_COMMAND(DisplayClusterRender_MeshComponent_Delete)(
		[MeshComponentProxy](FRHICommandListImmediate& RHICmdList)
	{
		delete MeshComponentProxy;
	});
}

//*************************************************************************
//* FDisplayClusterRender_MeshComponent
//*************************************************************************
FDisplayClusterRender_MeshComponent::FDisplayClusterRender_MeshComponent()
	: MeshComponentProxy(new FDisplayClusterRender_MeshComponentProxy())
{ }

FDisplayClusterRender_MeshComponent::~FDisplayClusterRender_MeshComponent()
{
	ImplDeleteMeshComponentProxy(MeshComponentProxy);
	MeshComponentProxy = nullptr;
}

void FDisplayClusterRender_MeshComponent::AssignMeshRefs(UStaticMeshComponent* MeshComponent, USceneComponent* OriginComponent)
{
	check(IsInGameThread());

	// Update Origin component ref
	OriginComponentRef.SetSceneComponent(OriginComponent);
	MeshComponentRef.SetComponentRef(MeshComponent);

	// Mesh geometry changed, update related RHI data
	UpdateDefferedRef();
}

const FStaticMeshLODResources* FDisplayClusterRender_MeshComponent::GetStaticMeshLODResource(int LodIndex) const
{
	check(IsInGameThread());

	UStaticMeshComponent* MeshComponent = MeshComponentRef.GetOrFindMeshComponent();
	if (MeshComponent != nullptr)
	{
		UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh();
		if (StaticMesh != nullptr)
		{
			return &(StaticMesh->GetLODForExport(LodIndex));
		}
	}

	return nullptr;
}

void FDisplayClusterRender_MeshComponent::Release()
{
	check(IsInGameThread());

	OriginComponentRef.ResetSceneComponent();
	MeshComponentRef.ResetSceneComponent();

	ImplUpdateMeshComponentProxyData(MeshComponentProxy, nullptr);
}

void FDisplayClusterRender_MeshComponent::UpdateDefferedRef()
{
	check(IsInGameThread());

	FDisplayClusterRender_MeshComponentProxyData* NewProxyData = nullptr;
	UStaticMeshComponent* StaticMeshComponent = MeshComponentRef.GetOrFindMeshComponent();
	if (StaticMeshComponent)
	{
		NewProxyData = new FDisplayClusterRender_MeshComponentProxyData(DataFunc, *StaticMeshComponent);
	}

	ImplUpdateMeshComponentProxyData(MeshComponentProxy, NewProxyData);
}

void FDisplayClusterRender_MeshComponent::UpdateDeffered(const UStaticMesh* InStaticMesh)
{
	FDisplayClusterRender_MeshComponentProxyData* NewProxyData = nullptr;
	if (InStaticMesh)
	{
		NewProxyData = new FDisplayClusterRender_MeshComponentProxyData(DataFunc, *InStaticMesh);
	}

	ImplUpdateMeshComponentProxyData(MeshComponentProxy, NewProxyData);
}

void FDisplayClusterRender_MeshComponent::UpdateDeffered(const FDisplayClusterRender_MeshGeometry& InMeshGeometry)
{
	check(IsInGameThread());

	ImplUpdateMeshComponentProxyData(MeshComponentProxy, new FDisplayClusterRender_MeshComponentProxyData(DataFunc, InMeshGeometry));
}
