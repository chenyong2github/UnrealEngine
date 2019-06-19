// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	TriangleRendering.cpp: Simple triangle rendering implementation.
=============================================================================*/

#include "ShowFlags.h"
#include "RHI.h"
#include "RenderResource.h"
#include "RenderingThread.h"
#include "VertexFactory.h"
#include "PackedNormal.h"
#include "LocalVertexFactory.h"
#include "SceneView.h"
#include "CanvasTypes.h"
#include "MeshBatch.h"
#include "RendererInterface.h"
#include "SceneUtils.h"
#include "EngineModule.h"
#include "MeshPassProcessor.h"

FCanvasTriangleRendererItem::FTriangleVertexFactory::FTriangleVertexFactory(
	const FStaticMeshVertexBuffers* InVertexBuffers,
	ERHIFeatureLevel::Type InFeatureLevel)
	: FLocalVertexFactory(InFeatureLevel, "FTriangleVertexFactory")
	, VertexBuffers(InVertexBuffers)
{}

FCanvasTriangleRendererItem::FTriangleMesh::FTriangleMesh(
	const FRawIndexBuffer* InIndexBuffer,
	const FTriangleVertexFactory* InVertexFactory)
	: IndexBuffer(InIndexBuffer)
	, VertexFactory(InVertexFactory)
{}

void FCanvasTriangleRendererItem::FTriangleVertexFactory::InitResource()
{
	FLocalVertexFactory::FDataType VertexData;
	VertexBuffers->PositionVertexBuffer.BindPositionVertexBuffer(this, VertexData);
	VertexBuffers->StaticMeshVertexBuffer.BindTangentVertexBuffer(this, VertexData);
	VertexBuffers->StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(this, VertexData);
	VertexBuffers->StaticMeshVertexBuffer.BindLightMapVertexBuffer(this, VertexData, 0);
	VertexBuffers->ColorVertexBuffer.BindColorVertexBuffer(this, VertexData);
	SetData(VertexData);

	FLocalVertexFactory::InitResource();
}

void FCanvasTriangleRendererItem::FTriangleMesh::InitRHI()
{
	MeshBatch.VertexFactory = VertexFactory;
	MeshBatch.ReverseCulling = false;
	MeshBatch.bDisableBackfaceCulling = true;
	MeshBatch.Type = PT_TriangleList;
	MeshBatch.DepthPriorityGroup = SDPG_Foreground;

	FMeshBatchElement& MeshBatchElement = MeshBatch.Elements[0];
	MeshBatchElement.IndexBuffer = IndexBuffer;
	MeshBatchElement.FirstIndex = 0;
	MeshBatchElement.NumPrimitives = 1;
	MeshBatchElement.MinVertexIndex = 0;
	MeshBatchElement.MaxVertexIndex = 2;
	MeshBatchElement.PrimitiveUniformBufferResource = &GIdentityPrimitiveUniformBuffer;
}

void FCanvasTriangleRendererItem::FRenderData::InitTriangleMesh(const FSceneView& View, bool bNeedsToSwitchVerticalAxis)
{
	StaticMeshVertexBuffers.PositionVertexBuffer.Init(Triangles.Num() * 3);
	StaticMeshVertexBuffers.StaticMeshVertexBuffer.Init(Triangles.Num() * 3, 1);
	StaticMeshVertexBuffers.ColorVertexBuffer.Init(Triangles.Num() * 3);

	IndexBuffer.Indices.SetNum(Triangles.Num() * 3);

	for (int32 i = 0; i < Triangles.Num(); i++)
	{
		const uint32 StartIndex = i * 3;

		/** The use of an index buffer here is actually necessary to workaround an issue with BaseVertexIndex, DrawPrimitive, and manual vertex fetch.
		 *  In short, DrawIndexedPrimitive with StartIndex map SV_VertexId to the correct location, but DrawPrimitive with BaseVertexIndex will not.
		 */
		IndexBuffer.Indices[StartIndex + 0] = StartIndex + 0;
		IndexBuffer.Indices[StartIndex + 1] = StartIndex + 1;
		IndexBuffer.Indices[StartIndex + 2] = StartIndex + 2;

		const FCanvasUVTri& Tri = Triangles[i].Tri;

		// create verts. Notice the order is (1, 0, 2)
		if (bNeedsToSwitchVerticalAxis)
		{
			StaticMeshVertexBuffers.PositionVertexBuffer.VertexPosition(StartIndex + 0) = FVector(Tri.V1_Pos.X, View.UnscaledViewRect.Height() - Tri.V1_Pos.Y, 0.0f);
			StaticMeshVertexBuffers.PositionVertexBuffer.VertexPosition(StartIndex + 1) = FVector(Tri.V0_Pos.X, View.UnscaledViewRect.Height() - Tri.V0_Pos.Y, 0.0f);
			StaticMeshVertexBuffers.PositionVertexBuffer.VertexPosition(StartIndex + 2) = FVector(Tri.V2_Pos.X, View.UnscaledViewRect.Height() - Tri.V2_Pos.Y, 0.0f);
		}
		else
		{
			StaticMeshVertexBuffers.PositionVertexBuffer.VertexPosition(StartIndex + 0) = FVector(Tri.V1_Pos.X, Tri.V1_Pos.Y, 0.0f);
			StaticMeshVertexBuffers.PositionVertexBuffer.VertexPosition(StartIndex + 1) = FVector(Tri.V0_Pos.X, Tri.V0_Pos.Y, 0.0f);
			StaticMeshVertexBuffers.PositionVertexBuffer.VertexPosition(StartIndex + 2) = FVector(Tri.V2_Pos.X, Tri.V2_Pos.Y, 0.0f);
		}

		StaticMeshVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(StartIndex + 0, FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f));
		StaticMeshVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(StartIndex + 1, FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f));
		StaticMeshVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(StartIndex + 2, FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f));

		StaticMeshVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(StartIndex + 0, 0, FVector2D(Tri.V1_UV.X, Tri.V1_UV.Y));
		StaticMeshVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(StartIndex + 1, 0, FVector2D(Tri.V0_UV.X, Tri.V0_UV.Y));
		StaticMeshVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(StartIndex + 2, 0, FVector2D(Tri.V2_UV.X, Tri.V2_UV.Y));

		StaticMeshVertexBuffers.ColorVertexBuffer.VertexColor(StartIndex + 0) = Tri.V1_Color.ToFColor(true);
		StaticMeshVertexBuffers.ColorVertexBuffer.VertexColor(StartIndex + 1) = Tri.V0_Color.ToFColor(true);
		StaticMeshVertexBuffers.ColorVertexBuffer.VertexColor(StartIndex + 2) = Tri.V2_Color.ToFColor(true);
	}

	StaticMeshVertexBuffers.PositionVertexBuffer.InitResource();
	StaticMeshVertexBuffers.StaticMeshVertexBuffer.InitResource();
	StaticMeshVertexBuffers.ColorVertexBuffer.InitResource();
	IndexBuffer.InitResource();
	VertexFactory.InitResource();
	TriMesh.InitResource();
};

void FCanvasTriangleRendererItem::FRenderData::ReleaseTriangleMesh()
{
	TriMesh.ReleaseResource();
	VertexFactory.ReleaseResource();
	IndexBuffer.ReleaseResource();
	StaticMeshVertexBuffers.PositionVertexBuffer.ReleaseResource();
	StaticMeshVertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
	StaticMeshVertexBuffers.ColorVertexBuffer.ReleaseResource();
}

void FCanvasTriangleRendererItem::FRenderData::RenderTriangles(
	FRHICommandListImmediate& RHICmdList,
	FMeshPassProcessorRenderState& DrawRenderState,
	const FSceneView& View,
	bool bIsHitTesting,
	bool bNeedsToSwitchVerticalAxis)
{
	check(IsInRenderingThread());

	IRendererModule& RendererModule = GetRendererModule();

	InitTriangleMesh(View, bNeedsToSwitchVerticalAxis);

	SCOPED_DRAW_EVENTF(RHICmdList, CanvasDrawTriangles, *MaterialRenderProxy->GetMaterial(GMaxRHIFeatureLevel)->GetFriendlyName());

	for (int32 TriIdx = 0; TriIdx < Triangles.Num(); TriIdx++)
	{
		const FTriangleInst& Tri = Triangles[TriIdx];

		FMeshBatch& MeshBatch = TriMesh.MeshBatch;
		MeshBatch.VertexFactory = &VertexFactory;
		MeshBatch.MaterialRenderProxy = MaterialRenderProxy;
		MeshBatch.Elements[0].FirstIndex = 3 * TriIdx;

		GetRendererModule().DrawTileMesh(RHICmdList, DrawRenderState, View, MeshBatch, bIsHitTesting, Tri.HitProxyId);
	}

	ReleaseTriangleMesh();
}

bool FCanvasTriangleRendererItem::Render_RenderThread(FRHICommandListImmediate& RHICmdList, FMeshPassProcessorRenderState& DrawRenderState, const FCanvas* Canvas)
{
	float CurrentRealTime = 0.f;
	float CurrentWorldTime = 0.f;
	float DeltaWorldTime = 0.f;

	if (!bFreezeTime)
	{
		CurrentRealTime = Canvas->GetCurrentRealTime();
		CurrentWorldTime = Canvas->GetCurrentWorldTime();
		DeltaWorldTime = Canvas->GetCurrentDeltaWorldTime();
	}

	checkSlow(Data);

	const FRenderTarget* CanvasRenderTarget = Canvas->GetRenderTarget();

	TUniquePtr<const FSceneViewFamily> ViewFamily = MakeUnique<const FSceneViewFamily>(FSceneViewFamily::ConstructionValues(
		CanvasRenderTarget,
		nullptr,
		FEngineShowFlags(ESFIM_Game))
		.SetWorldTimes(CurrentWorldTime, DeltaWorldTime, CurrentRealTime)
		.SetGammaCorrection(CanvasRenderTarget->GetDisplayGamma()));

	const FIntRect ViewRect(FIntPoint(0, 0), CanvasRenderTarget->GetSizeXY());

	// make a temporary view
	FSceneViewInitOptions ViewInitOptions;
	ViewInitOptions.ViewFamily = ViewFamily.Get();
	ViewInitOptions.SetViewRectangle(ViewRect);
	ViewInitOptions.ViewOrigin = FVector::ZeroVector;
	ViewInitOptions.ViewRotationMatrix = FMatrix::Identity;
	ViewInitOptions.ProjectionMatrix = Data->Transform.GetMatrix();
	ViewInitOptions.BackgroundColor = FLinearColor::Black;
	ViewInitOptions.OverlayColor = FLinearColor::White;

	TUniquePtr<const FSceneView> View = MakeUnique<const FSceneView>(ViewInitOptions);

	const bool bNeedsToSwitchVerticalAxis = RHINeedsToSwitchVerticalAxis(Canvas->GetShaderPlatform()) && XOR(IsMobileHDR(), Canvas->GetAllowSwitchVerticalAxis());

	Data->RenderTriangles(RHICmdList, DrawRenderState, *View, Canvas->IsHitTesting(), bNeedsToSwitchVerticalAxis);

	if (Canvas->GetAllowedModes() & FCanvas::Allow_DeleteOnRender)
	{
		Data = nullptr;
	}

	return true;
}

bool FCanvasTriangleRendererItem::Render_GameThread(const FCanvas* Canvas, FRenderThreadScope& RenderScope)
{
	float CurrentRealTime = 0.f;
	float CurrentWorldTime = 0.f;
	float DeltaWorldTime = 0.f;

	if (!bFreezeTime)
	{
		CurrentRealTime = Canvas->GetCurrentRealTime();
		CurrentWorldTime = Canvas->GetCurrentWorldTime();
		DeltaWorldTime = Canvas->GetCurrentDeltaWorldTime();
	}

	checkSlow(Data);

	const FRenderTarget* CanvasRenderTarget = Canvas->GetRenderTarget();

	const FSceneViewFamily* ViewFamily = new FSceneViewFamily(FSceneViewFamily::ConstructionValues(
		CanvasRenderTarget,
		Canvas->GetScene(),
		FEngineShowFlags(ESFIM_Game))
		.SetWorldTimes(CurrentWorldTime, DeltaWorldTime, CurrentRealTime)
		.SetGammaCorrection(CanvasRenderTarget->GetDisplayGamma()));

	const FIntRect ViewRect(FIntPoint(0, 0), CanvasRenderTarget->GetSizeXY());

	// make a temporary view
	FSceneViewInitOptions ViewInitOptions;
	ViewInitOptions.ViewFamily = ViewFamily;
	ViewInitOptions.SetViewRectangle(ViewRect);
	ViewInitOptions.ViewOrigin = FVector::ZeroVector;
	ViewInitOptions.ViewRotationMatrix = FMatrix::Identity;
	ViewInitOptions.ProjectionMatrix = Data->Transform.GetMatrix();
	ViewInitOptions.BackgroundColor = FLinearColor::Black;
	ViewInitOptions.OverlayColor = FLinearColor::White;

	const FSceneView* View = new FSceneView(ViewInitOptions);

	const bool bNeedsToSwitchVerticalAxis = RHINeedsToSwitchVerticalAxis(Canvas->GetShaderPlatform()) && XOR(IsMobileHDR(),Canvas->GetAllowSwitchVerticalAxis());
	const bool bIsHitTesting = Canvas->IsHitTesting();
	const bool bDeleteOnRender = Canvas->GetAllowedModes() & FCanvas::Allow_DeleteOnRender;

	RenderScope.EnqueueRenderCommand(
		[LocalData = Data, View, bIsHitTesting, bNeedsToSwitchVerticalAxis](FRHICommandListImmediate& RHICmdList)
	{
		FMeshPassProcessorRenderState DrawRenderState(*View);

		// disable depth test & writes
		DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());

		LocalData->RenderTriangles(RHICmdList, DrawRenderState, *View, bIsHitTesting, bNeedsToSwitchVerticalAxis);

		delete View->Family;
		delete View;
	});

	if (Canvas->GetAllowedModes() & FCanvas::Allow_DeleteOnRender)
	{
		Data = nullptr;
	}

	return true;
}