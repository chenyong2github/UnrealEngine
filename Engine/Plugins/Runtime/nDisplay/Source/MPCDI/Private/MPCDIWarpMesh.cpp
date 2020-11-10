// Copyright Epic Games, Inc. All Rights Reserved.

#include "MPCDIWarpMesh.h"
#include "MPCDIData.h"
#include "MPCDIWarpHelpers.h"
#include "MPCDILog.h"

#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameters.h"
#include "ShaderParameterUtils.h"
#include "PixelShaderUtils.h"

#include "RHIResources.h"
#include "CommonRenderResources.h"

#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "Rendering/StaticMeshVertexBuffer.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"

#include "WarpMeshRenderResources.h"


TGlobalResource<FWarpMeshVertexDeclaration> GWarpMeshVertexDeclaration;

bool FMPCDIWarpMesh::IsValidWarpData() const
{
	check(!bIsDirtyFrustumData);

	FScopeLock lock(&MeshDataGuard);
	return bIsValidFrustumData && MeshComponentRef.IsDefinedSceneComponent();
}

void FMPCDIWarpMesh::BeginBuildFrustum(IMPCDI::FFrustum& OutFrustum)
{
	FScopeLock lock(&MeshDataGuard);

	UStaticMeshComponent* MeshComponent   = MeshComponentRef.GetOrFindWarpMeshComponent();
	USceneComponent*      OriginComponent = OriginComponentRef.GetOrFindSceneComponent();

	if (MeshComponent)
	{
		// If StaticMesh geometry changed, update mpcdi math and RHI resources
		if (MeshComponentRef.IsWarpMeshChanged())
		{
			// Update mesh geometry reference and reset bStaticMeshChanged flag
			MeshComponentRef.SetWarpMeshComponent(MeshComponent);

			// Mesh geometry changed, update related RHI data
			ReleaseRHIResources();

			// Support runtime dynamic changes (new mesh to exist viewport)
			bIsDirtyFrustumData = true;
			bIsValidFrustumData = false;
		}

		if (OriginComponent)
		{
			FMatrix MeshToWorldMatrix = MeshComponent->GetComponentTransform().ToMatrixWithScale();
			FMatrix WorldToOriginMatrix = OriginComponent->GetComponentTransform().ToInverseMatrixWithScale();

			FMatrix MeshToOriginMatrix = MeshToWorldMatrix * WorldToOriginMatrix;

			MeshToOrigin.SetFromMatrix(MeshToOriginMatrix);
		}
		else
		{
			MeshToOrigin = MeshComponent->GetRelativeTransform();
		}

		OutFrustum.MeshToCaveMatrix = MeshToOrigin.ToMatrixWithScale();

		if (bIsDirtyFrustumData)
		{
			//Cave data is changed, update all cached
			bIsValidFrustumData = false;
			BuildMeshAABBox();
		}

		// Update AABB & other vectors runtime (support dynamic cave, mesh transform matrix)
		BuildAABBox();
	}

	bIsDirtyFrustumData = false;
}

// Return false, for invalid view planes (for any warpmesh point is under view plane)
bool FMPCDIWarpMesh::CalcFrustum_TextureBOX(int DivX, int DivY, const IMPCDI::FFrustum& OutFrustum, const FMatrix& World2Local, float& Top, float& Bottom, float& Left, float& Right) const
{
	//@todo: NOT IMPLEMENTED, now use fullCPU
	return CalcFrustum_fullCPU(OutFrustum, World2Local, Top, Bottom, Left, Right);
}

bool FMPCDIWarpMesh::CalcFrustum_fullCPU(const IMPCDI::FFrustum& OutFrustum, const FMatrix& World2Local, float& Top, float& Bottom, float& Left, float& Right) const
{
	UStaticMeshComponent* MeshComponent = MeshComponentRef.GetOrFindWarpMeshComponent();
	if (!MeshComponent)
	{
		return false;
	}

	FScopeLock lock(&MeshDataGuard);

	bool bResult = true;
	UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh();
	if (StaticMesh)
	{
		FMatrix WorldToMesh = MeshToOrigin.ToMatrixWithScale()*World2Local;

		// Search a camera space frustum
		const FStaticMeshLODResources& SrcMeshResource = StaticMesh->GetLODForExport(0);
		const FPositionVertexBuffer& VertexPosition = SrcMeshResource.VertexBuffers.PositionVertexBuffer;
		for (uint32 i = 0; i < VertexPosition.GetNumVertices(); i++)
		{
			const FVector4 Pts = FVector4(VertexPosition.VertexPosition(i),1);
			if (!GetProjectionClip(Pts, WorldToMesh, Top, Bottom, Left, Right))
			{
				bResult = false;
			}
		}
	}

	return bResult;
}

void FMPCDIWarpMesh::BuildMeshAABBox()
{
	UStaticMeshComponent* MeshComponent = MeshComponentRef.GetOrFindWarpMeshComponent();
	if (!MeshComponent)
	{
		return;
	}

	FScopeLock lock(&MeshDataGuard);

	ResetMeshAABB();
	UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh();
	if (StaticMesh)
	{
		bIsValidFrustumData = true;

		const FStaticMeshLODResources& SrcMeshResource = StaticMesh->GetLODForExport(0);
		const FPositionVertexBuffer& VertexPosition = SrcMeshResource.VertexBuffers.PositionVertexBuffer;
		for (uint32 i = 0; i < VertexPosition.GetNumVertices(); i++)
		{
			const FVector Pts = VertexPosition.VertexPosition(i);

			MeshAABBox.Min.X = FMath::Min(MeshAABBox.Min.X, Pts.X);
			MeshAABBox.Min.Y = FMath::Min(MeshAABBox.Min.Y, Pts.Y);
			MeshAABBox.Min.Z = FMath::Min(MeshAABBox.Min.Z, Pts.Z);

			MeshAABBox.Max.X = FMath::Max(MeshAABBox.Max.X, Pts.X);
			MeshAABBox.Max.Y = FMath::Max(MeshAABBox.Max.Y, Pts.Y);
			MeshAABBox.Max.Z = FMath::Max(MeshAABBox.Max.Z, Pts.Z);
		}

		{
			// Calc static normal and plane
			int IdxNum = SrcMeshResource.IndexBuffer.GetNumIndices();
			int TriNum = IdxNum / 3;

			int Ncount = 0;
			double Nxyz[3] = { 0,0,0 };

			for (int i = 0; i < TriNum; i++)
			{

				int Index0 = SrcMeshResource.IndexBuffer.GetIndex(i * 3 + 0);
				int Index1 = SrcMeshResource.IndexBuffer.GetIndex(i * 3 + 1);
				int Index2 = SrcMeshResource.IndexBuffer.GetIndex(i * 3 + 2);

				const FVector Pts1 = VertexPosition.VertexPosition(Index0);
				const FVector Pts0 = VertexPosition.VertexPosition(Index1);
				const FVector Pts2 = VertexPosition.VertexPosition(Index2);

				const FVector N1 = Pts1 - Pts0;
				const FVector N2 = Pts2 - Pts0;
				const FVector N = FVector::CrossProduct(N2, N1).GetSafeNormal();

				for (int j = 0; j < 3; j++)
				{
					Nxyz[j] += N[j];
				}

				Ncount++;
			}

			double Scale = double(1) / Ncount;
			for (int i = 0; i < 3; i++)
			{
				Nxyz[i] *= Scale;
			}

			MeshSurfaceViewNormal = FVector(Nxyz[0], Nxyz[1], Nxyz[2]).GetSafeNormal();

			//@todo: MeshSurfaceViewPlane not implemented, use MeshSurfaceViewNormal
			MeshSurfaceViewPlane = MeshSurfaceViewNormal;
		}
	}
}

void FMPCDIWarpMesh::BuildAABBox()
{
	// update math in cave space (support dynamic cave)
	AABBox.Min = MeshToOrigin.TransformPosition(MeshAABBox.Min);
	AABBox.Max = MeshToOrigin.TransformPosition(MeshAABBox.Max);

	SurfaceViewNormal = MeshToOrigin.TransformVector(MeshSurfaceViewNormal);
	SurfaceViewPlane = MeshToOrigin.TransformVector(MeshSurfaceViewPlane);
}

bool FMPCDIWarpMesh::SetStaticMeshWarp(UStaticMeshComponent* InMeshComponent, USceneComponent* InOriginComponent)
{
	// Inside the BeginBuildFrustum() function, we always check for changes to the mesh components IsWarpMeshChanged(), and update the corresponding resources and data.
	// No assignment needed when changing a mesh component
	UStaticMeshComponent* MeshComponent = MeshComponentRef.GetOrFindWarpMeshComponent();
	USceneComponent* OriginComponent = OriginComponentRef.GetOrFindSceneComponent();
	if (MeshComponent == InMeshComponent && OriginComponent == InOriginComponent)
	{
		// Same values, use current warpmesh
		return true;
	}

	// Reset previous one
	ReleaseRHIResources();
	MeshComponentRef.ResetMeshComponent();
	OriginComponentRef.ResetSceneComponent();

	FScopeLock lock(&MeshDataGuard);

	// Just setup data ptr
	// calcs only if data used on node viewports from BeginBuildFrustum() & BeginRender() calls
	MeshComponentRef.SetWarpMeshComponent(InMeshComponent);
	OriginComponentRef.SetSceneComponent(InOriginComponent);

	// Support runtime dynamic changes (new mesh to exist viewport)
	bIsDirtyFrustumData = true;
	bIsValidFrustumData = false;
	FString MeshName = "";
	InMeshComponent->GetName(MeshName);
	UE_LOG(LogMPCDI, Log, TEXT("Warp mesh [%s] assigned."), *MeshName);

	return true;
}

void FMPCDIWarpMesh::BeginRender(FRHICommandListImmediate& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit)
{
	check(IsInRenderingThread());

	if (bIsDirtyRHI)
	{
		CreateRHIResources();
	}

	if (bIsValidRHI)
	{
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GWarpMeshVertexDeclaration.VertexDeclarationRHI;
	}
}

void FMPCDIWarpMesh::FinishRender(FRHICommandListImmediate& RHICmdList)
{
	check(IsInRenderingThread());

	if (bIsValidRHI)
	{
		RHICmdList.SetStreamSource(0, VertexBufferRHI, 0);
		RHICmdList.DrawIndexedPrimitive(IndexBufferRHI, 0, 0, NumVertices, 0, NumTriangles, 1);
	}
}

void FMPCDIWarpMesh::ReleaseRHIResources()
{
	VertexBufferRHI.SafeRelease();
	IndexBufferRHI.SafeRelease();

	bIsValidRHI = false;
	bIsDirtyRHI = true; // Force recreate RHI on render request
}

void FMPCDIWarpMesh::CreateRHIResources()
{
	check(IsInRenderingThread());

	ReleaseRHIResources();

	bIsValidRHI = false;
	bIsDirtyRHI = false;

	FScopeLock lock(&MeshDataGuard);
	UStaticMeshComponent* MeshComponent = MeshComponentRef.GetOrFindWarpMeshComponent();
	if (MeshComponent)
	{
		UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh();
		if (StaticMesh)
		{
			const FStaticMeshLODResources& SrcMeshResource = StaticMesh->GetLODForExport(0);

			const FPositionVertexBuffer& VertexPosition = SrcMeshResource.VertexBuffers.PositionVertexBuffer;
			const FStaticMeshVertexBuffer& VertexBuffer = SrcMeshResource.VertexBuffers.StaticMeshVertexBuffer;
			const FRawStaticIndexBuffer&   IndexBuffer = SrcMeshResource.IndexBuffer;

			NumTriangles = IndexBuffer.GetNumIndices() / 3; //! Now by default no triangle strip supported
			NumVertices = VertexBuffer.GetNumVertices();

			IndexBufferRHI = IndexBuffer.IndexBufferRHI;

			// Use channel 1 as default source for chromakey custom markers UV
			int TotalNumTexCoords = SrcMeshResource.GetNumTexCoords();
			int ChromakeyMarkerUVChannel = (TotalNumTexCoords > 1) ? 1 : 0;

			// Create Vertex buffer RHI:
			FRHIResourceCreateInfo CreateInfo;
			VertexBufferRHI = RHICreateVertexBuffer(sizeof(FWarpMeshVertex) * NumVertices, BUF_Dynamic, CreateInfo);
			void* VoidPtr = RHILockVertexBuffer(VertexBufferRHI, 0, sizeof(FWarpMeshVertex) * NumVertices, RLM_WriteOnly);
			FWarpMeshVertex* pVertices = reinterpret_cast<FWarpMeshVertex*>(VoidPtr);
			for (uint32 i = 0; i < NumVertices; i++)
			{
				FWarpMeshVertex& Vertex = pVertices[i];
				Vertex.Position = VertexPosition.VertexPosition(i);
				Vertex.UV           = VertexBuffer.GetVertexUV(i, 0);
				Vertex.UV_Chromakey = VertexBuffer.GetVertexUV(i, ChromakeyMarkerUVChannel);
			}

			RHIUnlockVertexBuffer(VertexBufferRHI);
			bIsValidRHI = true;
		}
	}
};

//*************************************************************************
//* FDisplayClusterWarpMeshComponentRef
//*************************************************************************
inline FName GetStaticMeshName(UStaticMeshComponent* StaticMeshComponent)
{
	if (StaticMeshComponent)
	{
		UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();

		// Return null geometry ptr as text
		if (StaticMesh == nullptr)
		{
			return FName(TEXT("nullptr"));
		}

		// Return geometry asset name
		return StaticMesh->GetFName();
	}

	// Return empty FName for null component ptr
	return FName();
}

UStaticMeshComponent* FDisplayClusterWarpMeshComponentRef::GetOrFindWarpMeshComponent() const
{
	FScopeLock lock(&DataGuard);

	USceneComponent* SceneComponent = GetOrFindSceneComponent();
	if (SceneComponent)
	{
		UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(SceneComponent);
		if (StaticMeshComponent)
		{
			// Raise the flag [mutable bIsStaticMeshChanged] for the changed mesh geometry and store the new mesh name in [mutable StaticMeshName]
			if (!bIsStaticMeshChanged && GetStaticMeshName(StaticMeshComponent) != StaticMeshName)
			{
				bIsStaticMeshChanged = true;
			}

			return StaticMeshComponent;
		}
	}

	return nullptr;
}

void FDisplayClusterWarpMeshComponentRef::ResetWarpMeshChangedFlag()
{
	bIsStaticMeshChanged = false;
	StaticMeshName = FName();
}

void FDisplayClusterWarpMeshComponentRef::ResetMeshComponent()
{
	ResetWarpMeshChangedFlag();
	ResetSceneComponent();
}

bool FDisplayClusterWarpMeshComponentRef::SetWarpMeshComponent(UStaticMeshComponent* StaticMeshComponent)
{
	FScopeLock lock(&DataGuard);

	ResetWarpMeshChangedFlag();
	if(SetSceneComponent(StaticMeshComponent))
	{ 
		// Store the name of the current mesh geometry
		StaticMeshName = GetStaticMeshName(StaticMeshComponent);
		return true;
	}

	return false;
}

