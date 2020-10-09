// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	TileRendering.cpp: Tile rendering implementation.
=============================================================================*/

#include "RHI.h"
#include "ShowFlags.h"
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

DECLARE_GPU_STAT_NAMED(CanvasDrawTile, TEXT("CanvasDrawTile"));

static const uint32 CanvasTileVertexCount = 4;
static const uint32 CanvasTileIndexCount = 6;

FCanvasTileRendererItem::FTileVertexFactory::FTileVertexFactory(
	const FStaticMeshVertexBuffers* InVertexBuffers,
	ERHIFeatureLevel::Type InFeatureLevel)
	: FLocalVertexFactory(InFeatureLevel, "FTileVertexFactory")
	, VertexBuffers(InVertexBuffers)
{}

void FCanvasTileRendererItem::FTileVertexFactory::InitResource()
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

FCanvasTileRendererItem::FTileMesh::FTileMesh(
	const FRawIndexBuffer* InIndexBuffer,
	const FCanvasTileRendererItem::FTileVertexFactory* InVertexFactory)
	: IndexBuffer(InIndexBuffer)
	, VertexFactory(InVertexFactory)
{}

void FCanvasTileRendererItem::FTileMesh::InitRHI()
{
	MeshElement.VertexFactory = VertexFactory;
	MeshElement.ReverseCulling = false;
	MeshElement.Type = PT_TriangleList;
	MeshElement.DepthPriorityGroup = SDPG_Foreground;

	FMeshBatchElement& BatchElement = MeshElement.Elements[0];
	BatchElement.IndexBuffer = IndexBuffer;
	BatchElement.FirstIndex = 0;
	BatchElement.NumPrimitives = 2;
	BatchElement.MinVertexIndex = 0;
	BatchElement.MaxVertexIndex = CanvasTileVertexCount - 1;
	BatchElement.PrimitiveUniformBufferResource = &GIdentityPrimitiveUniformBuffer;
}

FCanvasTileRendererItem::FRenderData::FRenderData(
	ERHIFeatureLevel::Type InFeatureLevel,
	const FMaterialRenderProxy* InMaterialRenderProxy,
	const FCanvas::FTransformEntry& InTransform)
	: MaterialRenderProxy(InMaterialRenderProxy)
	, Transform(InTransform)
	, VertexFactory(&StaticMeshVertexBuffers, InFeatureLevel)
	, TileMesh(&IndexBuffer, &VertexFactory)
{}

void FCanvasTileRendererItem::FRenderData::InitTileMesh(const FSceneView& View, bool bNeedsToSwitchVerticalAxis)
{
	static_assert(CanvasTileVertexCount == 4, "Invalid tile tri-list size.");
	static_assert(CanvasTileIndexCount == 6, "Invalid tile tri-list size.");

	const uint32 TotalVertexCount = Tiles.Num() * CanvasTileVertexCount;
	const uint32 TotalIndexCount = Tiles.Num() * CanvasTileIndexCount;

	StaticMeshVertexBuffers.PositionVertexBuffer.Init(TotalVertexCount);
	StaticMeshVertexBuffers.StaticMeshVertexBuffer.Init(TotalVertexCount, 1);
	StaticMeshVertexBuffers.ColorVertexBuffer.Init(TotalVertexCount);
	IndexBuffer.Indices.SetNum(TotalIndexCount);

	for (int32 i = 0; i < Tiles.Num(); i++)
	{
		const FTileInst& Tile = Tiles[i];
		const uint32 FirstIndex = i * CanvasTileIndexCount;
		const uint32 FirstVertex = i * CanvasTileVertexCount;

		IndexBuffer.Indices[FirstIndex + 0] = FirstVertex + 0;
		IndexBuffer.Indices[FirstIndex + 1] = FirstVertex + 1;
		IndexBuffer.Indices[FirstIndex + 2] = FirstVertex + 2;
		IndexBuffer.Indices[FirstIndex + 3] = FirstVertex + 2;
		IndexBuffer.Indices[FirstIndex + 4] = FirstVertex + 1;
		IndexBuffer.Indices[FirstIndex + 5] = FirstVertex + 3;

		const float X = Tile.X;
		const float Y = Tile.Y;
		const float U = Tile.U;
		const float V = Tile.V;
		const float SizeX = Tile.SizeX;
		const float SizeY = Tile.SizeY;
		const float SizeU = Tile.SizeU;
		const float SizeV = Tile.SizeV;

		if (bNeedsToSwitchVerticalAxis)
		{
			StaticMeshVertexBuffers.PositionVertexBuffer.VertexPosition(FirstVertex + 0) = FVector(X + SizeX, View.UnscaledViewRect.Height() - (Y + SizeY), 0.0f);
			StaticMeshVertexBuffers.PositionVertexBuffer.VertexPosition(FirstVertex + 1) = FVector(X, View.UnscaledViewRect.Height() - (Y + SizeY), 0.0f);
			StaticMeshVertexBuffers.PositionVertexBuffer.VertexPosition(FirstVertex + 2) = FVector(X + SizeX, View.UnscaledViewRect.Height() - Y, 0.0f);
			StaticMeshVertexBuffers.PositionVertexBuffer.VertexPosition(FirstVertex + 3) = FVector(X, View.UnscaledViewRect.Height() - Y, 0.0f);

			StaticMeshVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(FirstVertex + 0, 0, FVector2D(U + SizeU, V + SizeV));
			StaticMeshVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(FirstVertex + 1, 0, FVector2D(U, V + SizeV));
			StaticMeshVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(FirstVertex + 2, 0, FVector2D(U + SizeU, V));
			StaticMeshVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(FirstVertex + 3, 0, FVector2D(U, V));
		}
		else
		{
			StaticMeshVertexBuffers.PositionVertexBuffer.VertexPosition(FirstVertex + 0) = FVector(X + SizeX, Y, 0.0f);
			StaticMeshVertexBuffers.PositionVertexBuffer.VertexPosition(FirstVertex + 1) = FVector(X, Y, 0.0f);
			StaticMeshVertexBuffers.PositionVertexBuffer.VertexPosition(FirstVertex + 2) = FVector(X + SizeX, Y + SizeY, 0.0f);
			StaticMeshVertexBuffers.PositionVertexBuffer.VertexPosition(FirstVertex + 3) = FVector(X, Y + SizeY, 0.0f);

			StaticMeshVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(FirstVertex + 0, 0, FVector2D(U + SizeU, V));
			StaticMeshVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(FirstVertex + 1, 0, FVector2D(U, V));
			StaticMeshVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(FirstVertex + 2, 0, FVector2D(U + SizeU, V + SizeV));
			StaticMeshVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(FirstVertex + 3, 0, FVector2D(U, V + SizeV));
		}

		for (int j = 0; j < CanvasTileVertexCount; j++)
		{
			StaticMeshVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(FirstVertex + j, FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), FVector(0.0f, 0.0f, 1.0f));
			StaticMeshVertexBuffers.ColorVertexBuffer.VertexColor(FirstVertex + j) = Tile.InColor;
		}
	}

	StaticMeshVertexBuffers.PositionVertexBuffer.InitResource();
	StaticMeshVertexBuffers.StaticMeshVertexBuffer.InitResource();
	StaticMeshVertexBuffers.ColorVertexBuffer.InitResource();
	IndexBuffer.InitResource();
	VertexFactory.InitResource();
	TileMesh.InitResource();
}

void FCanvasTileRendererItem::FRenderData::ReleaseTileMesh()
{
	TileMesh.ReleaseResource();
	VertexFactory.ReleaseResource();
	IndexBuffer.ReleaseResource();
	StaticMeshVertexBuffers.PositionVertexBuffer.ReleaseResource();
	StaticMeshVertexBuffers.StaticMeshVertexBuffer.ReleaseResource();
	StaticMeshVertexBuffers.ColorVertexBuffer.ReleaseResource();
}

void FCanvasTileRendererItem::FRenderData::RenderTiles(
	FRHICommandListImmediate& RHICmdList,
	FMeshPassProcessorRenderState& DrawRenderState,
	const FSceneView& View,
	bool bIsHitTesting,
	bool bNeedsToSwitchVerticalAxis,
	bool bUse128bitRT)
{
	check(IsInRenderingThread());

	SCOPED_GPU_STAT(RHICmdList, CanvasDrawTile);
	SCOPED_DRAW_EVENTF(RHICmdList, CanvasDrawTile, *MaterialRenderProxy->GetIncompleteMaterialWithFallback(GMaxRHIFeatureLevel).GetFriendlyName());
	TRACE_CPUPROFILER_EVENT_SCOPE(CanvasDrawTile);
	QUICK_SCOPE_CYCLE_COUNTER(STAT_CanvasDrawTile)

	IRendererModule& RendererModule = GetRendererModule();

	InitTileMesh(View, bNeedsToSwitchVerticalAxis);

	for (int32 TileIdx = 0; TileIdx < Tiles.Num(); TileIdx++)
	{
		FRenderData::FTileInst& Tile = Tiles[TileIdx];

		FMeshBatch& Mesh = TileMesh.MeshElement;
		Mesh.MaterialRenderProxy = MaterialRenderProxy;
		Mesh.Elements[0].FirstIndex = CanvasTileIndexCount * TileIdx;

		RendererModule.DrawTileMesh(RHICmdList, DrawRenderState, View, Mesh, bIsHitTesting, Tile.HitProxyId, bUse128bitRT);
	}

	ReleaseTileMesh();
}

bool FCanvasTileRendererItem::Render_RenderThread(FRHICommandListImmediate& RHICmdList, FMeshPassProcessorRenderState& DrawRenderState, const FCanvas* Canvas)
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

	const bool bNeedsToSwitchVerticalAxis = RHINeedsToSwitchVerticalAxis(Canvas->GetShaderPlatform()) && Canvas->GetAllowSwitchVerticalAxis(); 

	Data->RenderTiles(RHICmdList, DrawRenderState, *View, Canvas->IsHitTesting(), bNeedsToSwitchVerticalAxis);

	if (Canvas->GetAllowedModes() & FCanvas::Allow_DeleteOnRender)
	{
		Data = nullptr;
	}

	return true;
}

bool FCanvasTileRendererItem::Render_GameThread(const FCanvas* Canvas, FRenderThreadScope& RenderScope)
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
	if (ensure(CanvasRenderTarget))
	{
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

		const bool bNeedsToSwitchVerticalAxis = RHINeedsToSwitchVerticalAxis(Canvas->GetShaderPlatform()) && Canvas->GetAllowSwitchVerticalAxis();
		const bool bIsHitTesting = Canvas->IsHitTesting();
		const bool bDeleteOnRender = Canvas->GetAllowedModes() & FCanvas::Allow_DeleteOnRender;

		bool bRequiresExplicit128bitRT = false;

		FTexture2DRHIRef CanvasRTTexture = CanvasRenderTarget->GetRenderTargetTexture();
		if (CanvasRTTexture)
		{
			bRequiresExplicit128bitRT = PlatformRequires128bitRT(CanvasRTTexture->GetFormat());
		}

		RenderScope.EnqueueRenderCommand(
			[LocalData = Data, View, bIsHitTesting, bNeedsToSwitchVerticalAxis, bRequiresExplicit128bitRT]
		(FRHICommandListImmediate& RHICmdList)
		{
			FMeshPassProcessorRenderState DrawRenderState(*View);

			// disable depth test & writes
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());

			LocalData->RenderTiles(RHICmdList, DrawRenderState, *View, bIsHitTesting, bNeedsToSwitchVerticalAxis, bRequiresExplicit128bitRT);

			delete View->Family;
			delete View;
		});

		if (bDeleteOnRender)
		{
			Data = nullptr;
		}

		return true;
	}
		
	return false;
}