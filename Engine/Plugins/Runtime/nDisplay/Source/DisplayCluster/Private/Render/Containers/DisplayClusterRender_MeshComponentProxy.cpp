// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Containers/DisplayClusterRender_MeshComponentProxy.h"

#include "Render/Containers/DisplayClusterRender_MeshResources.h"
#include "Render/Containers/DisplayClusterRender_MeshGeometry.h"
#include "Misc/DisplayClusterLog.h"

#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"

TGlobalResource<FDisplayClusterMeshVertexDeclaration> GDisplayClusterMeshVertexDeclaration;

//*************************************************************************
//* FDisaplyClusterMeshComponentProxyData
//*************************************************************************
class FDisplayClusterRender_MeshComponentProxyData
{
public:
	TArray<uint32> IndexData;

	TArray<FDisplayClusterMeshVertex> VertexData;

	uint32 NumTriangles = 0;
	uint32 NumVertices = 0;

public:
	FDisplayClusterRender_MeshComponentProxyData(const UStaticMeshComponent& InMeshComponent)
	{
		check(IsInGameThread());

		UStaticMesh* StaticMesh = InMeshComponent.GetStaticMesh();
		if (StaticMesh)
		{
			const FStaticMeshLODResources& SrcMeshResource = StaticMesh->GetLODForExport(0);

			const FPositionVertexBuffer& VertexPosition = SrcMeshResource.VertexBuffers.PositionVertexBuffer;
			const FStaticMeshVertexBuffer& VertexBuffer = SrcMeshResource.VertexBuffers.StaticMeshVertexBuffer;
			const FRawStaticIndexBuffer& IndexBuffer = SrcMeshResource.IndexBuffer;

			NumTriangles = IndexBuffer.GetNumIndices() / 3; // Now by default no triangle strip supported
			NumVertices = VertexBuffer.GetNumVertices();

			IndexBuffer.GetCopy(IndexData);

			VertexData.AddZeroed(NumVertices);
			for (uint32 i = 0; i < NumVertices; i++)
			{
				VertexData[i].Position = VertexPosition.VertexPosition(i);
				VertexData[i].UV = VertexBuffer.GetVertexUV(i, 0);

				// Use channel 1 as default source for chromakey custom markers UV
				VertexData[i].UV_Chromakey = VertexBuffer.GetVertexUV(i, (SrcMeshResource.GetNumTexCoords() > 1) ? 1 : 0);
			}
		}
	}

	FDisplayClusterRender_MeshComponentProxyData(const FDisplayClusterRender_MeshGeometry& InMeshGeometry)
	{
		check(IsInGameThread());

		bool bUseChromakeyUV = InMeshGeometry.ChromakeyUV.Num() > 0;

		NumTriangles = InMeshGeometry.Triangles.Num() / 3; // No triangle strip supported by default
		NumVertices = InMeshGeometry.Vertices.Num();

		VertexData.AddZeroed(NumVertices);
		for (uint32 i = 0; i < NumVertices; i++)
		{
			VertexData[i].Position = InMeshGeometry.Vertices[i];
			VertexData[i].UV = InMeshGeometry.UV[i];

			// Use channel 1 as default source for chromakey custom markers UV
			VertexData[i].UV_Chromakey = bUseChromakeyUV ? InMeshGeometry.ChromakeyUV[i] : InMeshGeometry.UV[i];
		}

		int Idx = 0;
		IndexData.AddZeroed(InMeshGeometry.Triangles.Num());
		for (const int32& It : InMeshGeometry.Triangles)
		{
			IndexData[Idx++] = (uint32)It;
		}
	}
};

//*************************************************************************
//* FDisplayClusterRender_MeshComponentProxy
//*************************************************************************
FDisplayClusterRender_MeshComponentProxy::FDisplayClusterRender_MeshComponentProxy()
{ }

FDisplayClusterRender_MeshComponentProxy::FDisplayClusterRender_MeshComponentProxy(const FDisplayClusterRender_MeshGeometry& InMeshGeometry)
{
	check(IsInGameThread());

	UpdateDeffered(InMeshGeometry);
}

FDisplayClusterRender_MeshComponentProxy::~FDisplayClusterRender_MeshComponentProxy()
{
	VertexBufferRHI.SafeRelease();
	IndexBufferRHI.SafeRelease();
}

bool FDisplayClusterRender_MeshComponentProxy::BeginRender_RenderThread(FRHICommandListImmediate& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit) const
{
	check(IsInRenderingThread());

	if (VertexBufferRHI.IsValid() && IndexBufferRHI.IsValid())
	{
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GDisplayClusterMeshVertexDeclaration.VertexDeclarationRHI;
		return true;
	}

	return false;
}

bool  FDisplayClusterRender_MeshComponentProxy::FinishRender_RenderThread(FRHICommandListImmediate& RHICmdList) const
{
	check(IsInRenderingThread());

	if (VertexBufferRHI.IsValid() && IndexBufferRHI.IsValid())
	{
		// Support update
		RHICmdList.SetStreamSource(0, VertexBufferRHI, 0);
		RHICmdList.DrawIndexedPrimitive(IndexBufferRHI, 0, 0, NumVertices, 0, NumTriangles, 1);
		return true;
	}

	return false;
}

void FDisplayClusterRender_MeshComponentProxy::AssignMeshRefs(UStaticMeshComponent* MeshComponent, USceneComponent* OriginComponent)
{
	check(IsInGameThread());

	// Update Origin component ref
	OriginComponentRef.SetSceneComponent(OriginComponent);
	MeshComponentRef.SetComponentRef(MeshComponent);

	// Mesh geometry changed, update related RHI data
	UpdateDefferedRef();
}

const FStaticMeshLODResources* FDisplayClusterRender_MeshComponentProxy::GetWarpMeshLodResource(int LodIndex) const
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

void FDisplayClusterRender_MeshComponentProxy::UpdateDefferedRef()
{
	check(IsInGameThread());

	UStaticMeshComponent* StaticMeshComponent = MeshComponentRef.GetOrFindMeshComponent();
	if (StaticMeshComponent)
	{
		FDisplayClusterRender_MeshComponentProxyData* NewProxyData = new FDisplayClusterRender_MeshComponentProxyData(*StaticMeshComponent);

	ENQUEUE_RENDER_COMMAND(DisplayClusterRender_UpdateMeshComponentProxyRHI)(
		[NewProxyData, pMeshComponentProxy = this](FRHICommandListImmediate& RHICmdList)
	{
		// Update RHI
			pMeshComponentProxy->UpdateRHI_RenderThread(RHICmdList, NewProxyData);
			delete NewProxyData;
	});
}
}

void FDisplayClusterRender_MeshComponentProxy::UpdateDeffered(const FDisplayClusterRender_MeshGeometry& InMeshGeometry)
{
	check(IsInGameThread());

	FDisplayClusterRender_MeshComponentProxyData* NewProxyData = new FDisplayClusterRender_MeshComponentProxyData(InMeshGeometry);

	ENQUEUE_RENDER_COMMAND(DisplayClusterRender_UpdateMeshComponentProxyRHI)(
		[NewProxyData, pMeshComponentProxy = this](FRHICommandListImmediate& RHICmdList)
	{
		// Update RHI
		pMeshComponentProxy->UpdateRHI_RenderThread(RHICmdList, NewProxyData);
		delete NewProxyData;
	});
}

void FDisplayClusterRender_MeshComponentProxy::UpdateRHI_RenderThread(FRHICommandListImmediate& RHICmdList, FDisplayClusterRender_MeshComponentProxyData* InMeshData)
{
	check(IsInRenderingThread());
	check(InMeshData);

	VertexBufferRHI.SafeRelease();
	IndexBufferRHI.SafeRelease();

	NumTriangles = InMeshData->NumTriangles;
	NumVertices = InMeshData->NumVertices;

		uint32 Usage = BUF_ShaderResource | BUF_Static;

		// Create Vertex buffer RHI:
		{
		FRHIResourceCreateInfo CreateInfo;
		size_t VertexDataSize = sizeof(FDisplayClusterMeshVertex) * NumVertices;
		if (VertexDataSize == 0)
		{
			UE_LOG(LogDisplayClusterRender, Warning, TEXT("MeshComponent has a vertex size of 0, please make sure a mesh is assigned."))
			return;
		}
		
		VertexBufferRHI = RHICreateVertexBuffer(VertexDataSize, Usage, CreateInfo);
		FDisplayClusterMeshVertex* DestVertexData = reinterpret_cast<FDisplayClusterMeshVertex*>(RHILockVertexBuffer(VertexBufferRHI, 0, VertexDataSize, RLM_WriteOnly));
		if (DestVertexData)
		{
			FPlatformMemory::Memcpy(DestVertexData, InMeshData->VertexData.GetData(), VertexDataSize);
		RHIUnlockVertexBuffer(VertexBufferRHI);
		}
	}

		// Create Index buffer RHI:
		{
			FRHIResourceCreateInfo CreateInfo;
		size_t IndexDataSize = sizeof(uint32) * InMeshData->IndexData.Num();

		IndexBufferRHI = RHICreateIndexBuffer(sizeof(uint32), IndexDataSize, Usage, CreateInfo);

		uint32* DestIndexData = reinterpret_cast<uint32*>(RHILockIndexBuffer(IndexBufferRHI, 0, IndexDataSize, RLM_WriteOnly));
		if(DestIndexData)
			{
			FPlatformMemory::Memcpy(DestIndexData, InMeshData->IndexData.GetData(), IndexDataSize);
			RHIUnlockIndexBuffer(IndexBufferRHI);
		}
	}
}

