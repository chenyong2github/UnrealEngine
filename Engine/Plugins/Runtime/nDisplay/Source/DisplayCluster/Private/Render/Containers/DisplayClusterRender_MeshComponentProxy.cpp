// Copyright Epic Games, Inc. All Rights Reserved.

#include "Render/Containers/DisplayClusterRender_MeshComponentProxy.h"

#include "Render/Containers/DisplayClusterRender_MeshResources.h"
#include "Render/Containers/DisplayClusterRender_MeshGeometry.h"

TGlobalResource<FDisplayClusterMeshVertexDeclaration> GDisplayClusterMeshVertexDeclaration;

//*************************************************************************
//* FDisplayClusterRender_MeshComponentProxy
//*************************************************************************
FDisplayClusterRender_MeshComponentProxy::FDisplayClusterRender_MeshComponentProxy()
{ }

FDisplayClusterRender_MeshComponentProxy::FDisplayClusterRender_MeshComponentProxy(const FDisplayClusterRender_MeshGeometry& InMeshGeometry)
{
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
	// Update Origin component ref
	OriginComponentRef.SetSceneComponent(OriginComponent);
	MeshComponentRef.SetComponentRef(MeshComponent);

	// Mesh geometry changed, update related RHI data
	UpdateDefferedRef();
}

const FStaticMeshLODResources* FDisplayClusterRender_MeshComponentProxy::GetWarpMeshLodResource(int LodIndex) const
{
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

	ENQUEUE_RENDER_COMMAND(DisplayClusterRender_UpdateMeshComponentProxyRHI)(
		[pMeshComponentRef = new FDisplayClusterRender_MeshComponentRef(MeshComponentRef), pMeshComponentProxy = this](FRHICommandListImmediate& RHICmdList)
	{
		// Update RHI
		pMeshComponentProxy->UpdateRHI_RenderThread(RHICmdList, pMeshComponentRef->GetOrFindMeshComponent());
		delete pMeshComponentRef;
	});
}

void FDisplayClusterRender_MeshComponentProxy::UpdateDeffered(const FDisplayClusterRender_MeshGeometry& InMeshGeometry)
{
	check(IsInGameThread());

	ENQUEUE_RENDER_COMMAND(DisplayClusterRender_UpdateMeshComponentProxyRHI)(
		[pMeshGeometryRef = new FDisplayClusterRender_MeshGeometry(InMeshGeometry), pMeshComponentProxy = this](FRHICommandListImmediate& RHICmdList)
	{
		// Update RHI
		pMeshComponentProxy->UpdateRHI_RenderThread(RHICmdList, pMeshGeometryRef);
		delete pMeshGeometryRef;
	});
}

void FDisplayClusterRender_MeshComponentProxy::UpdateRHI_RenderThread(FRHICommandListImmediate& RHICmdList, FDisplayClusterRender_MeshGeometry* InMeshGeometry)
{
	check(IsInRenderingThread());

	VertexBufferRHI.SafeRelease();
	IndexBufferRHI.SafeRelease();

	NumTriangles = NumVertices = 0;

	if (InMeshGeometry)
	{
		bool bUseChromakeyUV = InMeshGeometry->ChromakeyUV.Num() > 0;

		NumTriangles = InMeshGeometry->Triangles.Num() / 3; // No triangle strip supported by default
		NumVertices = InMeshGeometry->Vertices.Num();

		uint32 Usage = BUF_ShaderResource | BUF_Static;

		// Create Vertex buffer RHI:
		{
		FRHIResourceCreateInfo CreateInfo;
		VertexBufferRHI = RHICreateVertexBuffer(sizeof(FDisplayClusterMeshVertex) * NumVertices, Usage, CreateInfo);
		}

		// Initialize vertex buffer
		{
			void* VoidPtr = RHILockVertexBuffer(VertexBufferRHI, 0, sizeof(FDisplayClusterMeshVertex) * NumVertices, RLM_WriteOnly);
			FDisplayClusterMeshVertex* pVertices = reinterpret_cast<FDisplayClusterMeshVertex*>(VoidPtr);
			for (uint32 i = 0; i < NumVertices; i++)
		{
				FDisplayClusterMeshVertex& Vertex = pVertices[i];

				Vertex.Position = InMeshGeometry->Vertices[i];
				Vertex.UV = InMeshGeometry->UV[i];

				// Use channel 1 as default source for chromakey custom markers UV
				Vertex.UV_Chromakey = bUseChromakeyUV ? InMeshGeometry->ChromakeyUV[i] : InMeshGeometry->UV[i];
		}
		RHIUnlockVertexBuffer(VertexBufferRHI);
		}

		// Create Index buffer RHI:
		{
			FRHIResourceCreateInfo CreateInfo;
			IndexBufferRHI = RHICreateIndexBuffer(sizeof(uint32), sizeof(uint32) * InMeshGeometry->Triangles.Num(), Usage, CreateInfo);
		}

		// Initialize index buffer
		{
			void* VoidPtr2 = RHILockIndexBuffer(IndexBufferRHI, 0, sizeof(uint32) * InMeshGeometry->Triangles.Num(), RLM_WriteOnly);
			uint32* pIndices = reinterpret_cast<uint32*>(VoidPtr2);
			int Idx = 0;

			for (int32& It : InMeshGeometry->Triangles)
			{
				pIndices[Idx++] = (uint32)It;
			}

			RHIUnlockIndexBuffer(IndexBufferRHI);
		}
	}
}

void FDisplayClusterRender_MeshComponentProxy::UpdateRHI_RenderThread(FRHICommandListImmediate& RHICmdList, UStaticMeshComponent* InMeshComponent)
{
	check(IsInRenderingThread());

	VertexBufferRHI.SafeRelease();
	IndexBufferRHI.SafeRelease();

	NumTriangles = NumVertices = 0;

	if (InMeshComponent)
	{
		UStaticMesh* StaticMesh = InMeshComponent->GetStaticMesh();
		if (StaticMesh)
		{
			const FStaticMeshLODResources& SrcMeshResource = StaticMesh->GetLODForExport(0);

			const FPositionVertexBuffer& VertexPosition = SrcMeshResource.VertexBuffers.PositionVertexBuffer;
			const FStaticMeshVertexBuffer& VertexBuffer = SrcMeshResource.VertexBuffers.StaticMeshVertexBuffer;
			const FRawStaticIndexBuffer&   IndexBuffer = SrcMeshResource.IndexBuffer;

			NumTriangles = IndexBuffer.GetNumIndices() / 3; // Now by default no triangle strip supported
			NumVertices = VertexBuffer.GetNumVertices();

			// Copy Index buffer RHI
			IndexBufferRHI = IndexBuffer.IndexBufferRHI;

			// Create Vertex buffer RHI:
			FRHIResourceCreateInfo CreateInfo;
			VertexBufferRHI = RHICreateVertexBuffer(sizeof(FDisplayClusterMeshVertex) * NumVertices, BUF_Dynamic, CreateInfo);

			// Initialize vertex from mesh source streams
			void* VoidPtr = RHILockVertexBuffer(VertexBufferRHI, 0, sizeof(FDisplayClusterMeshVertex) * NumVertices, RLM_WriteOnly);
			FDisplayClusterMeshVertex* pVertices = reinterpret_cast<FDisplayClusterMeshVertex*>(VoidPtr);
			for (uint32 i = 0; i < NumVertices; i++)
			{
				FDisplayClusterMeshVertex& Vertex = pVertices[i];
				Vertex.Position = VertexPosition.VertexPosition(i);
				Vertex.UV           = VertexBuffer.GetVertexUV(i, 0);

				// Use channel 1 as default source for chromakey custom markers UV
				Vertex.UV_Chromakey = VertexBuffer.GetVertexUV(i, (SrcMeshResource.GetNumTexCoords() > 1) ? 1 : 0);
			}

			RHIUnlockVertexBuffer(VertexBufferRHI);
		}
	}
};
