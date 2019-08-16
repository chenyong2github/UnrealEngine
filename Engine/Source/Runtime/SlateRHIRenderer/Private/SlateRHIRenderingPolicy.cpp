// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SlateRHIRenderingPolicy.h"
#include "UniformBuffer.h"
#include "Shader.h"
#include "ShowFlags.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/App.h"
#include "EngineGlobals.h"
#include "RHIStaticStates.h"
#include "RHIUtilities.h"
#include "SceneView.h"
#include "SceneUtils.h"
#include "Engine/Engine.h"
#include "SlateShaders.h"
#include "Rendering/SlateRenderer.h"
#include "SlateRHIRenderer.h"
#include "SlateMaterialShader.h"
#include "SlateUTextureResource.h"
#include "SlateMaterialResource.h"
#include "SlateUpdatableBuffer.h"
#include "SlatePostProcessor.h"
#include "Modules/ModuleManager.h"
#include "PipelineStateCache.h"
#include "Math/RandomStream.h"
#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "Types/SlateConstants.h"

extern void UpdateNoiseTextureParameters(FViewUniformShaderParameters& ViewUniformShaderParameters);

DECLARE_CYCLE_STAT(TEXT("Update Buffers RT"), STAT_SlateUpdateBufferRTTime, STATGROUP_Slate);


DECLARE_DWORD_COUNTER_STAT(TEXT("Num Layers"), STAT_SlateNumLayers, STATGROUP_Slate);
DECLARE_DWORD_COUNTER_STAT(TEXT("Num Batches"), STAT_SlateNumBatches, STATGROUP_Slate);
DECLARE_DWORD_COUNTER_STAT(TEXT("Num Vertices"), STAT_SlateVertexCount, STATGROUP_Slate);

DECLARE_CYCLE_STAT(TEXT("Slate RT: Texture Draw Call"), STAT_SlateRTTextureDrawCall, STATGROUP_Slate);
DECLARE_CYCLE_STAT(TEXT("Slate RT: Material Draw Call"), STAT_SlateRTMaterialDrawCall, STATGROUP_Slate);
DECLARE_CYCLE_STAT(TEXT("Slate RT: Scissor Draw Call"), STAT_SlateRTStencilDrawCall, STATGROUP_Slate);
DECLARE_CYCLE_STAT(TEXT("Slate RT: Custom Draw"), STAT_SlateRTCustomDraw, STATGROUP_Slate);

DECLARE_DWORD_COUNTER_STAT(TEXT("Clips (Scissor)"), STAT_SlateScissorClips, STATGROUP_Slate);
DECLARE_DWORD_COUNTER_STAT(TEXT("Clips (Stencil)"), STAT_SlateStencilClips, STATGROUP_Slate);

#if WITH_SLATE_DEBUGGING
int32 SlateEnableDrawEvents = 1;
#else
int32 SlateEnableDrawEvents = 0;
#endif
static FAutoConsoleVariableRef CVarSlateEnableDrawEvents(TEXT("Slate.EnableDrawEvents"), SlateEnableDrawEvents, TEXT("."), ECVF_Default);

#if WITH_SLATE_DEBUGGING
int32 BatchToDraw = -1;
static FAutoConsoleVariableRef CVarSlateDrawBatchNum(TEXT("Slate.DrawBatchNum"), BatchToDraw, TEXT("."), ECVF_Default);
#endif

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	#define SLATE_DRAW_EVENT(RHICmdList, EventName) SCOPED_CONDITIONAL_DRAW_EVENT(RHICmdList, EventName, SlateEnableDrawEvents);
#else
	#define SLATE_DRAW_EVENT(RHICmdList, EventName)
#endif

TAutoConsoleVariable<int32> CVarSlateAbsoluteIndices(
	TEXT("Slate.AbsoluteIndices"),
	0,
	TEXT("0: Each element first vertex index starts at 0 (default), 1: Use absolute indices, simplifies draw call setup on RHIs that do not support BaseVertex"),
	ECVF_Default
);

FSlateRHIRenderingPolicy::FSlateRHIRenderingPolicy(TSharedRef<FSlateFontServices> InSlateFontServices, TSharedRef<FSlateRHIResourceManager> InResourceManager, TOptional<int32> InitialBufferSize)
	: FSlateRenderingPolicy(InSlateFontServices, 0)
	, PostProcessor(new FSlatePostProcessor)
	, ResourceManager(InResourceManager)
	, bGammaCorrect(true)
	, bApplyColorDeficiencyCorrection(true)
	, InitialBufferSizeOverride(InitialBufferSize)
	, LastDeviceProfile(nullptr)
{
	InitResources();
}

void FSlateRHIRenderingPolicy::InitResources()
{
	int32 NumVertices = 100;

	if ( InitialBufferSizeOverride.IsSet() )
	{
		NumVertices = InitialBufferSizeOverride.GetValue();
	}
	else if ( GConfig )
	{
		int32 NumVertsInConfig = 0;
		if ( GConfig->GetInt(TEXT("SlateRenderer"), TEXT("NumPreallocatedVertices"), NumVertsInConfig, GEngineIni) )
		{
			NumVertices = NumVertsInConfig;
		}
	}

	// Always create a little space but never allow it to get too high
#if !SLATE_USE_32BIT_INDICES
	NumVertices = FMath::Clamp(NumVertices, 100, 65535);
#else
	NumVertices = FMath::Clamp(NumVertices, 100, 1000000);
#endif

	UE_LOG(LogSlate, Verbose, TEXT("Allocating space for %d vertices"), NumVertices);

	MasterVertexBuffer.Init(NumVertices);
	MasterIndexBuffer.Init(NumVertices);

	BeginInitResource(&StencilVertexBuffer);
}

void FSlateRHIRenderingPolicy::ReleaseResources()
{
	MasterVertexBuffer.Destroy();
	MasterIndexBuffer.Destroy();

	BeginReleaseResource(&StencilVertexBuffer);
}

void FSlateRHIRenderingPolicy::BeginDrawingWindows()
{
	check( IsInRenderingThread() );
}

void FSlateRHIRenderingPolicy::EndDrawingWindows()
{
	check( IsInParallelRenderingThread() );
}

struct FSlateUpdateVertexAndIndexBuffers final : public FRHICommand<FSlateUpdateVertexAndIndexBuffers>
{
	TSlateElementVertexBuffer<FSlateVertex>& VertexBuffer;
	FSlateElementIndexBuffer& IndexBuffer;
	FSlateBatchData& BatchData;
	bool bAbsoluteIndices;

	FSlateUpdateVertexAndIndexBuffers(TSlateElementVertexBuffer<FSlateVertex>& InVertexBuffer, FSlateElementIndexBuffer& InIndexBuffer, FSlateBatchData& InBatchData, bool bInAbsoluteIndices)
		: VertexBuffer(InVertexBuffer)
		, IndexBuffer(InIndexBuffer)
		, BatchData(InBatchData)
		, bAbsoluteIndices(bInAbsoluteIndices)
	{
		check(IsInRenderingThread());
	}

	void Execute(FRHICommandListBase& CmdList)
	{
		SCOPE_CYCLE_COUNTER(STAT_SlateUpdateBufferRTTime);

		const bool bIsInRenderingThread = !IsRunningRHIInSeparateThread() || CmdList.Bypass();

		const FSlateVertexArray& FinalVertexData = BatchData.GetFinalVertexData();
		const FSlateIndexArray& FinalIndexData = BatchData.GetFinalIndexData();

		const int32 NumBatchedVertices = FinalVertexData.Num();
		const int32 NumBatchedIndices = FinalIndexData.Num();

		//int32 RequiredVertexBufferSize = NumBatchedVertices*sizeof(FSlateVertex);
		uint8* VertexBufferData = (uint8*)VertexBuffer.LockBuffer(NumBatchedVertices, bIsInRenderingThread);

		//uint32 RequiredIndexBufferSize = NumBatchedIndices*sizeof(SlateIndex);		
		uint8* IndexBufferData = (uint8*)IndexBuffer.LockBuffer(NumBatchedIndices, bIsInRenderingThread);

		//Early out if we have an invalid buffer (might have lost context and now have invalid buffers)
		if ((nullptr != VertexBufferData) && (nullptr != IndexBufferData))
		{
			FMemory::Memcpy(VertexBufferData, FinalVertexData.GetData(), FinalVertexData.Num() * sizeof(FSlateVertex));
			FMemory::Memcpy(IndexBufferData, FinalIndexData.GetData(), FinalIndexData.Num() * sizeof(SlateIndex));
		}

		if (nullptr != VertexBufferData)
		{
			VertexBuffer.UnlockBuffer(bIsInRenderingThread);
		}

		if (nullptr != IndexBufferData)
		{
			IndexBuffer.UnlockBuffer(bIsInRenderingThread);
		}
	}
};

void FSlateRHIRenderingPolicy::BuildRenderingBuffers(FRHICommandListImmediate& RHICmdList, FSlateBatchData& InBatchData)
{
	SCOPE_CYCLE_COUNTER(STAT_SlateUpdateBufferRTTime);

	// Should only be called by the rendering thread
	check(IsInRenderingThread());

	// Merge together batches for less draw calls
	InBatchData.MergeRenderBatches();

	const FSlateVertexArray& FinalVertexData = InBatchData.GetFinalVertexData();
	const FSlateIndexArray& FinalIndexData = InBatchData.GetFinalIndexData();

	const int32 NumVertices = FinalVertexData.Num();
	const int32 NumIndices = FinalIndexData.Num();

	if (InBatchData.GetRenderBatches().Num() > 0 && NumVertices > 0 && NumIndices > 0)
	{
		bool bShouldShrinkResources = false;
		bool bAbsoluteIndices = CVarSlateAbsoluteIndices.GetValueOnRenderThread() != 0;

		MasterVertexBuffer.PreFillBuffer(NumVertices, bShouldShrinkResources);
		MasterIndexBuffer.PreFillBuffer(NumIndices, bShouldShrinkResources);

		if (!IsRunningRHIInSeparateThread() || RHICmdList.Bypass())
		{
			FSlateUpdateVertexAndIndexBuffers UpdateBufferCommand(MasterVertexBuffer, MasterIndexBuffer, InBatchData, bAbsoluteIndices);

			UpdateBufferCommand.Execute(RHICmdList);
		}
		else
		{
			ALLOC_COMMAND_CL(RHICmdList, FSlateUpdateVertexAndIndexBuffers)(MasterVertexBuffer, MasterIndexBuffer, InBatchData, bAbsoluteIndices);
		}
	}

	checkSlow(MasterVertexBuffer.GetBufferUsageSize() <= MasterVertexBuffer.GetBufferSize());
	checkSlow(MasterIndexBuffer.GetBufferUsageSize() <= MasterIndexBuffer.GetBufferSize());

	SET_DWORD_STAT(STAT_SlateNumLayers, InBatchData.GetNumLayers());
	SET_DWORD_STAT(STAT_SlateNumBatches, InBatchData.GetNumFinalBatches());
	SET_DWORD_STAT(STAT_SlateVertexCount, InBatchData.GetFinalVertexData().Num());
}

static FSceneView* CreateSceneView( FSceneViewFamilyContext* ViewFamilyContext, FSlateBackBuffer& BackBuffer, const FMatrix& ViewProjectionMatrix )
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_Slate_CreateSceneView);
	// In loading screens, the engine is NULL, so we skip out.
	if (GEngine == nullptr)
	{
		return nullptr;
	}

	FIntRect ViewRect(FIntPoint(0, 0), BackBuffer.GetSizeXY());

	// make a temporary view
	FSceneViewInitOptions ViewInitOptions;
	ViewInitOptions.ViewFamily = ViewFamilyContext;
	ViewInitOptions.SetViewRectangle(ViewRect);
	ViewInitOptions.ViewOrigin = FVector::ZeroVector;
	ViewInitOptions.ViewRotationMatrix = FMatrix::Identity;
	ViewInitOptions.ProjectionMatrix = ViewProjectionMatrix;
	ViewInitOptions.BackgroundColor = FLinearColor::Black;
	ViewInitOptions.OverlayColor = FLinearColor::White;

	FSceneView* View = new FSceneView( ViewInitOptions );
	ViewFamilyContext->Views.Add( View );

	const FIntPoint BufferSize = BackBuffer.GetSizeXY();

	// Create the view's uniform buffer.
	FViewUniformShaderParameters ViewUniformShaderParameters;

	View->SetupCommonViewUniformBufferParameters(
		ViewUniformShaderParameters,
		BufferSize,
		1,
		ViewRect,
		View->ViewMatrices,
		FViewMatrices()
	);

	ViewUniformShaderParameters.WorldViewOrigin = View->ViewMatrices.GetViewOrigin();
	

	ERHIFeatureLevel::Type RHIFeatureLevel = View->GetFeatureLevel();

	ViewUniformShaderParameters.MobilePreviewMode =
		(GIsEditor &&
		(RHIFeatureLevel == ERHIFeatureLevel::ES2 || RHIFeatureLevel == ERHIFeatureLevel::ES3_1) &&
		GMaxRHIFeatureLevel > ERHIFeatureLevel::ES3_1) ? 1.0f : 0.0f;

	UpdateNoiseTextureParameters(ViewUniformShaderParameters);

	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_Slate_CreateViewUniformBufferImmediate);
		View->ViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(ViewUniformShaderParameters, UniformBuffer_SingleFrame);
	}

	return View;
}

static const FName RendererModuleName("Renderer");

static bool UpdateScissorRect(
	FRHICommandList& RHICmdList, 
#if STATS
	int32& ScissorClips, 
	int32& StencilClips,
#endif
	uint32& StencilRef, 
	uint32& MaskingID,
	FSlateBackBuffer& BackBuffer,
	const FSlateRenderBatch& RenderBatch, 
	FTexture2DRHIRef& ColorTarget,
	FTexture2DRHIRef& DepthStencilTarget,
	const FSlateClippingState*& LastClippingState,
	const FVector2D& ViewTranslation2D, 
	bool bSwitchVerticalAxis,
	FGraphicsPipelineStateInitializer& InGraphicsPSOInit,
	FSlateStencilClipVertexBuffer& StencilVertexBuffer,
	const FMatrix& ViewProjection, 
	bool bForceStateChange)
{
	check(RHICmdList.IsInsideRenderPass());
	bool bDidRestartRenderpass = false;

	QUICK_SCOPE_CYCLE_COUNTER(STAT_Slate_UpdateScissorRect);

	if (RenderBatch.ClippingState != LastClippingState || bForceStateChange)
	{
		if (RenderBatch.ClippingState)
		{
			const FSlateClippingState& ClipState = *RenderBatch.ClippingState;
			if (ClipState.GetClippingMethod() == EClippingMethod::Scissor)
			{
#if STATS
				ScissorClips++;
#endif

				if (bForceStateChange && MaskingID > 0)
				{
					// #todo-renderpasses this is very gross. If/when this gets refactored we can detect a simple clear or batch up elements by rendertarget (and other stuff)
					RHICmdList.EndRenderPass();
					bDidRestartRenderpass = true;

					FRHIRenderPassInfo RPInfo(ColorTarget, ERenderTargetActions::Load_Store);
					RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(ERenderTargetActions::DontLoad_DontStore, ERenderTargetActions::Load_Store);
					RPInfo.DepthStencilRenderTarget.DepthStencilTarget = DepthStencilTarget;
					RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthNop_StencilWrite;

					RHICmdList.BeginRenderPass(RPInfo, TEXT("SlateUpdateScissorRect"));
				}

				const FSlateClippingZone& ScissorRect = ClipState.ScissorRect.GetValue();

				const FIntPoint SizeXY = BackBuffer.GetSizeXY();
				const FVector2D ViewSize((float) SizeXY.X, (float) SizeXY.Y);

				// Clamp scissor rect to BackBuffer size
				const FVector2D TopLeft     = FMath::Min(FMath::Max(ScissorRect.TopLeft     + ViewTranslation2D, FVector2D(0.0f, 0.0f)), ViewSize);
				const FVector2D BottomRight = FMath::Min(FMath::Max(ScissorRect.BottomRight + ViewTranslation2D, FVector2D(0.0f, 0.0f)), ViewSize);
				
				if (bSwitchVerticalAxis)
				{
					const int32 MinY = (ViewSize.Y - BottomRight.Y);
					const int32 MaxY = (ViewSize.Y - TopLeft.Y);
					RHICmdList.SetScissorRect(true, TopLeft.X, MinY, BottomRight.X, MaxY);
				}
				else
				{
					RHICmdList.SetScissorRect(true, TopLeft.X, TopLeft.Y, BottomRight.X, BottomRight.Y);
				}

				// Disable depth/stencil testing by default
				InGraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
				StencilRef = 0;
			}
			else
			{
#if STATS
				StencilClips++;
#endif

				SLATE_DRAW_EVENT(RHICmdList, StencilClipping);

				check(ClipState.StencilQuads.Num() > 0);

				const TArray<FSlateClippingZone>& StencilQuads = ClipState.StencilQuads;

				// We're going to overflow the masking ID this time, we need to reset the MaskingID to 0.
				// this will cause us to clear the stencil buffer so that we can begin fresh.
				if ((MaskingID + StencilQuads.Num()) > 255)
				{
					MaskingID = 0;
				}

				// We only clear the stencil the first time, and if some how the user draws more than 255 masking quads
				// in a single frame.
				bool bClearStencil = false;
				if (MaskingID == 0)
				{
					bClearStencil = true;

					// We don't want there to be any scissor rect when we clear the stencil
					RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
				}
				else
				{
					// There might be some large - useless stencils, especially in the first couple of stencils if large
					// widgets that clip also contain render targets, so, by setting the scissor to the AABB of the final
					// stencil, we can cut out a lot of work that can't possibly be useful.
					//
					// NOTE - We also round it, because if we don't it can over-eagerly slice off pixels it shouldn't.
					const FSlateClippingZone& MaskQuad = StencilQuads.Last();
					const FSlateRect LastStencilBoundingBox = MaskQuad.GetBoundingBox().Round();

					const FVector2D TopLeft = LastStencilBoundingBox.GetTopLeft() + ViewTranslation2D;
					const FVector2D BottomRight = LastStencilBoundingBox.GetBottomRight() + ViewTranslation2D;

					if (bSwitchVerticalAxis)
					{
						const FIntPoint ViewSize = BackBuffer.GetSizeXY();
						const int32 MinY = (ViewSize.Y - BottomRight.Y);
						const int32 MaxY = (ViewSize.Y - TopLeft.Y);
						RHICmdList.SetScissorRect(true, TopLeft.X, MinY, BottomRight.X, MaxY);
					}
					else
					{
						RHICmdList.SetScissorRect(true, TopLeft.X, TopLeft.Y, BottomRight.X, BottomRight.Y);
					}
				}

				// Don't bother setting the render targets unless we actually need to clear them.
				if (bClearStencil || bForceStateChange)
				{
					// #todo-renderpasses Similar to above this is gross. Would require a refactor to really fix.
					RHICmdList.EndRenderPass();
					bDidRestartRenderpass = true;

					// Clear current stencil buffer, we use ELoad/EStore, because we need to keep the stencil around.
					ERenderTargetLoadAction StencilLoadAction = bClearStencil ? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad;

					FRHIRenderPassInfo RPInfo(ColorTarget, ERenderTargetActions::Load_Store);
					RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(ERenderTargetActions::DontLoad_DontStore, MakeRenderTargetActions(StencilLoadAction, ERenderTargetStoreAction::EStore));
					RPInfo.DepthStencilRenderTarget.DepthStencilTarget = DepthStencilTarget;
					RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthNop_StencilWrite;
					RHICmdList.BeginRenderPass(RPInfo, TEXT("SlateUpdateScissorRect_ClearStencil"));
				}

				TShaderMap<FGlobalShaderType>* MaxFeatureLevelShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

				// Set the new shaders
				TShaderMapRef<FSlateMaskingVS> VertexShader(MaxFeatureLevelShaderMap);
				TShaderMapRef<FSlateMaskingPS> PixelShader(MaxFeatureLevelShaderMap);

				// Start by setting up the stenciling states so that we can write representations of the clipping zones into the stencil buffer only.
				{
					FGraphicsPipelineStateInitializer WriteMaskPSOInit;
					RHICmdList.ApplyCachedRenderTargets(WriteMaskPSOInit);
					WriteMaskPSOInit.BlendState = TStaticBlendStateWriteMask<CW_NONE>::GetRHI();
					WriteMaskPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
					WriteMaskPSOInit.DepthStencilState =
						TStaticDepthStencilState<
						/*bEnableDepthWrite*/ false
						, /*DepthTest*/ CF_Always
						, /*bEnableFrontFaceStencil*/ true
						, /*FrontFaceStencilTest*/ CF_Always
						, /*FrontFaceStencilFailStencilOp*/ SO_Keep
						, /*FrontFaceDepthFailStencilOp*/ SO_Keep
						, /*FrontFacePassStencilOp*/ SO_Replace
						, /*bEnableBackFaceStencil*/ true
						, /*BackFaceStencilTest*/ CF_Always
						, /*BackFaceStencilFailStencilOp*/ SO_Keep
						, /*BackFaceDepthFailStencilOp*/ SO_Keep
						, /*BackFacePassStencilOp*/ SO_Replace
						, /*StencilReadMask*/ 0xFF
						, /*StencilWriteMask*/ 0xFF>::GetRHI();

					WriteMaskPSOInit.BoundShaderState.VertexDeclarationRHI = GSlateMaskingVertexDeclaration.VertexDeclarationRHI;
					WriteMaskPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
					WriteMaskPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
					WriteMaskPSOInit.PrimitiveType = PT_TriangleStrip;

					SetGraphicsPipelineState(RHICmdList, WriteMaskPSOInit);

					VertexShader->SetViewProjection(RHICmdList, ViewProjection);
					VertexShader->SetVerticalAxisMultiplier(RHICmdList, bSwitchVerticalAxis ? -1.0f : 1.0f);

					// Draw the first stencil using SO_Replace, so that we stomp any pixel with a MaskingID + 1.
					{
						const FSlateClippingZone& MaskQuad = StencilQuads[0];

						RHICmdList.SetStencilRef(MaskingID + 1);

						SCOPE_CYCLE_COUNTER(STAT_SlateRTStencilDrawCall);

						VertexShader->SetMaskRect(RHICmdList, MaskQuad.TopLeft, MaskQuad.TopRight, MaskQuad.BottomLeft, MaskQuad.BottomRight);

						RHICmdList.SetStreamSource(0, StencilVertexBuffer.VertexBufferRHI, 0);
						RHICmdList.DrawPrimitive(0, 2, 1);
					}

					// Now setup the pipeline to use SO_SaturatedIncrement, since we've established the initial
					// stencil with SO_Replace, we can safely use SO_SaturatedIncrement, to build up the stencil
					// to the required mask of MaskingID + StencilQuads.Num(), thereby ensuring only the union of
					// all stencils will render pixels.
					{
						WriteMaskPSOInit.DepthStencilState =
							TStaticDepthStencilState<
							/*bEnableDepthWrite*/ false
							, /*DepthTest*/ CF_Always
							, /*bEnableFrontFaceStencil*/ true
							, /*FrontFaceStencilTest*/ CF_Always
							, /*FrontFaceStencilFailStencilOp*/ SO_Keep
							, /*FrontFaceDepthFailStencilOp*/ SO_Keep
							, /*FrontFacePassStencilOp*/ SO_SaturatedIncrement
							, /*bEnableBackFaceStencil*/ true
							, /*BackFaceStencilTest*/ CF_Always
							, /*BackFaceStencilFailStencilOp*/ SO_Keep
							, /*BackFaceDepthFailStencilOp*/ SO_Keep
							, /*BackFacePassStencilOp*/ SO_SaturatedIncrement
							, /*StencilReadMask*/ 0xFF
							, /*StencilWriteMask*/ 0xFF>::GetRHI();


						SetGraphicsPipelineState(RHICmdList, WriteMaskPSOInit);

						VertexShader->SetViewProjection(RHICmdList, ViewProjection);
						VertexShader->SetVerticalAxisMultiplier(RHICmdList, bSwitchVerticalAxis ? -1.0f : 1.0f);
					}
				}

				MaskingID += StencilQuads.Num();

				// Next write the number of quads representing the number of clipping zones have on top of each other.
				for (int32 MaskIndex = 1; MaskIndex < StencilQuads.Num(); MaskIndex++)
				{
					const FSlateClippingZone& MaskQuad = StencilQuads[MaskIndex];

					SCOPE_CYCLE_COUNTER(STAT_SlateRTStencilDrawCall);
					
					VertexShader->SetMaskRect(RHICmdList, MaskQuad.TopLeft, MaskQuad.TopRight, MaskQuad.BottomLeft, MaskQuad.BottomRight);

					RHICmdList.SetStreamSource(0, StencilVertexBuffer.VertexBufferRHI, 0);
					RHICmdList.DrawPrimitive(0, 2, 1);
				}

				// Setup the stenciling state to be read only now, disable depth writes, and restore the color buffer
				// because we're about to go back to rendering widgets "normally", but with the added effect that now
				// we have the stencil buffer bound with a bunch of clipping zones rendered into it.
				{
					FRHIDepthStencilState* DSMaskRead =
						TStaticDepthStencilState<
						/*bEnableDepthWrite*/ false
						, /*DepthTest*/ CF_Always
						, /*bEnableFrontFaceStencil*/ true
						, /*FrontFaceStencilTest*/ CF_Equal
						, /*FrontFaceStencilFailStencilOp*/ SO_Keep
						, /*FrontFaceDepthFailStencilOp*/ SO_Keep
						, /*FrontFacePassStencilOp*/ SO_Keep
						, /*bEnableBackFaceStencil*/ true
						, /*BackFaceStencilTest*/ CF_Equal
						, /*BackFaceStencilFailStencilOp*/ SO_Keep
						, /*BackFaceDepthFailStencilOp*/ SO_Keep
						, /*BackFacePassStencilOp*/ SO_Keep
						, /*StencilReadMask*/ 0xFF
						, /*StencilWriteMask*/ 0xFF>::GetRHI();

					InGraphicsPSOInit.DepthStencilState = DSMaskRead;

					// We set a StencilRef equal to the number of stenciling/clipping masks,
					// so unless the pixel we're rendering two is on top of a stencil pixel with the same number
					// it's going to get rejected, thereby clipping everything except for the cross-section of
					// all the stenciling quads.
					StencilRef = MaskingID;
				}
			}

			RHICmdList.ApplyCachedRenderTargets(InGraphicsPSOInit);
		}
		else
		{
			RHICmdList.SetScissorRect(false, 0, 0, 0, 0);

			// Disable depth/stencil testing
			InGraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			StencilRef = 0;
		}

		LastClippingState = RenderBatch.ClippingState;
	}

	return bDidRestartRenderpass;
}

void FSlateRHIRenderingPolicy::DrawElements(
	FRHICommandListImmediate& RHICmdList,
	FSlateBackBuffer& BackBuffer,
	FTexture2DRHIRef& ColorTarget,
	FTexture2DRHIRef& DepthStencilTarget,
	int32 FirstBatchIndex,
	const TArray<FSlateRenderBatch>& RenderBatches,
	const FSlateRenderingParams& Params)
{
	// Should only be called by the rendering thread
	check(IsInRenderingThread());
	check(RHICmdList.IsInsideRenderPass());

	// Cache the TextureLODGroups so that we can look them up for texture filtering.
	if (UDeviceProfileManager::DeviceProfileManagerSingleton)
	{
		if (UDeviceProfile* Profile = UDeviceProfileManager::Get().GetActiveProfile())
		{
			if (Profile != LastDeviceProfile)
			{
				TextureLODGroups = Profile->GetTextureLODSettings()->TextureLODGroups;
				LastDeviceProfile = Profile;
			}
		}
	}

	IRendererModule& RendererModule = FModuleManager::GetModuleChecked<IRendererModule>(RendererModuleName);

	static const FEngineShowFlags DefaultShowFlags(ESFIM_Game);

	// Disable gammatization when back buffer is in float 16 format.
	// Note that the final editor rendering won't compare 1:1 with 8/10 bit RGBA since blending
	// of "manually" gammatized values is wrong as there is no de-gammatization of the destination buffer
	// and re-gammatization of the resulting blending operation in the 8/10 bit RGBA path.
	const float EngineGamma = (BackBuffer.GetRenderTargetTexture()->GetFormat() == PF_FloatRGBA) ? 1.0 : GEngine ? GEngine->GetDisplayGamma() : 2.2f;
	const float DisplayGamma = bGammaCorrect ? EngineGamma : 1.0f;
	const float DisplayContrast = GSlateContrast;

	int32 ScissorClips = 0;
	int32 StencilClips = 0;

	// In order to support MaterialParameterCollections, we need to create multiple FSceneViews for 
	// each possible Scene that we encounter. The following code creates these as separate arrays, where the first 
	// N entries map directly to entries from ActiveScenes. The final entry is added to represent the absence of a
	// valid scene, i.e. a -1 in the SceneIndex parameter of the batch.
	int32 NumScenes = ResourceManager->GetSceneCount() + 1;
	TArray<FSceneView*, TInlineAllocator<3> > SceneViews;
	SceneViews.SetNum(NumScenes);
	TArray<FSceneViewFamilyContext*, TInlineAllocator<3> > SceneViewFamilyContexts;
	SceneViewFamilyContexts.SetNum(NumScenes);

	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_Slate_CreateScenes);
		for (int32 i = 0; i < ResourceManager->GetSceneCount(); i++)
		{
			SceneViewFamilyContexts[i] = new FSceneViewFamilyContext
			(
				FSceneViewFamily::ConstructionValues
				(
					&BackBuffer,
					ResourceManager->GetSceneAt(i),
					DefaultShowFlags
				)
				.SetWorldTimes(Params.CurrentWorldTime, Params.DeltaTimeSeconds, Params.CurrentRealTime)
				.SetGammaCorrection(DisplayGamma)
				.SetRealtimeUpdate(true)
			);
			SceneViews[i] = CreateSceneView(SceneViewFamilyContexts[i], BackBuffer, Params.ViewProjectionMatrix);
		}

		SceneViewFamilyContexts[NumScenes - 1] = new FSceneViewFamilyContext
		(
			FSceneViewFamily::ConstructionValues
			(
				&BackBuffer,
				nullptr,
				DefaultShowFlags
			)
			.SetWorldTimes(Params.CurrentWorldTime, Params.DeltaTimeSeconds, Params.CurrentRealTime)
			.SetGammaCorrection(DisplayGamma)
			.SetRealtimeUpdate(true)
		);
		SceneViews[NumScenes - 1] = CreateSceneView(SceneViewFamilyContexts[NumScenes - 1], BackBuffer, Params.ViewProjectionMatrix);
	}

	TShaderMapRef<FSlateElementVS> GlobalVertexShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

	FSamplerStateRHIRef BilinearClamp = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	TSlateElementVertexBuffer<FSlateVertex>* VertexBufferPtr = &MasterVertexBuffer;
	FSlateElementIndexBuffer* IndexBufferPtr = &MasterIndexBuffer;

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	const FSlateRenderDataHandle* LastHandle = nullptr;

	const ERHIFeatureLevel::Type FeatureLevel = GMaxRHIFeatureLevel;
	TShaderMap<FGlobalShaderType>* ShaderMap = GetGlobalShaderMap(FeatureLevel);

#if WITH_SLATE_VISUALIZERS
	FRandomStream BatchColors(1337);
#endif

	const bool bAbsoluteIndices = CVarSlateAbsoluteIndices.GetValueOnRenderThread() != 0;
	const bool bSwitchVerticalAxis = Params.bAllowSwitchVerticalAxis && RHINeedsToSwitchVerticalAxis(GShaderPlatformForFeatureLevel[GMaxRHIFeatureLevel]);

	// This variable tracks the last clipping state, so that if multiple batches have the same clipping state, we don't have to do any work.
	const FSlateClippingState* LastClippingState;

	// This is the stenciling ref variable we set any time we draw, so that any stencil comparisons use the right mask id.
	uint32 StencilRef = 0;
	// This is an accumulating maskID that we use to track the between batch usage of the stencil buffer, when at 0, or over 255
	// this signals that we need to reset the masking ID, and clear the stencil buffer, as we've used up the available scratch range.
	uint32 MaskingID = 0;

	RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
	// Disable depth/stencil testing by default
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	FVector2D ViewTranslation2D = Params.ViewOffset;

	// Draw each element
#if WITH_SLATE_DEBUGGING
	int32 NextRenderBatchIndex = BatchToDraw == -1 ? FirstBatchIndex : BatchToDraw;
#else
	int32 NextRenderBatchIndex = FirstBatchIndex;
#endif


	/*
		#todo-renderpasses This loop ends up with ugly logic.
		CustomDrawers will draw in their own renderpass. So we must remember to reopen the renderpass with the passed in Color/DepthStencil targets.
	*/
	while (NextRenderBatchIndex != INDEX_NONE)
	{
		VertexBufferPtr = &MasterVertexBuffer;
		IndexBufferPtr = &MasterIndexBuffer;

		if (!RHICmdList.IsInsideRenderPass())
		{
			// Restart the renderpass since the CustomDrawer or post-process may have changed it in last iteration
			FRHIRenderPassInfo RPInfo(BackBuffer.GetRenderTargetTexture(), ERenderTargetActions::Load_Store);
			RPInfo.DepthStencilRenderTarget.DepthStencilTarget = DepthStencilTarget;
			if (DepthStencilTarget)
			{
				RPInfo.DepthStencilRenderTarget.Action = EDepthStencilTargetActions::LoadDepthStencil_StoreDepthStencil;
				RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthWrite_StencilWrite;
			}
			else
			{
				RPInfo.DepthStencilRenderTarget.Action = EDepthStencilTargetActions::DontLoad_DontStore;
				RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthNop_StencilNop;
			}
			RHICmdList.BeginRenderPass(RPInfo, TEXT("RestartingSlateDrawElements"));

			// Something may have messed with the viewport size so set it back to the full target.
			RHICmdList.SetViewport(0, 0, 0, BackBuffer.GetSizeXY().X, BackBuffer.GetSizeXY().Y, 0.0f);
			RHICmdList.SetStreamSource(0, VertexBufferPtr->VertexBufferRHI, 0);
		}
				
#if WITH_SLATE_VISUALIZERS
		FLinearColor BatchColor = FLinearColor(BatchColors.GetUnitVector());
#endif
		const FSlateRenderBatch& RenderBatch = RenderBatches[NextRenderBatchIndex];

		NextRenderBatchIndex = RenderBatch.NextBatchIndex;

#if WITH_SLATE_DEBUGGING
		if (BatchToDraw != -1)
		{
			break;
		}
#endif

		const FSlateShaderResource* ShaderResource = RenderBatch.ShaderResource;
		const ESlateBatchDrawFlag DrawFlags = RenderBatch.DrawFlags;
		const ESlateDrawEffect DrawEffects = RenderBatch.DrawEffects;
		const ESlateShader ShaderType = RenderBatch.ShaderType;
		const FShaderParams& ShaderParams = RenderBatch.ShaderParams;

		if (EnumHasAllFlags(DrawFlags, ESlateBatchDrawFlag::Wireframe))
		{
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Wireframe, CM_None, false>::GetRHI();
		}
		else
		{
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None, false>::GetRHI();
		}

		if (!RenderBatch.CustomDrawer)
		{
			FMatrix DynamicOffset = FTranslationMatrix::Make(FVector(RenderBatch.DynamicOffset.X, RenderBatch.DynamicOffset.Y, 0));
			const FMatrix ViewProjection = DynamicOffset * Params.ViewProjectionMatrix;

			UpdateScissorRect(
				RHICmdList,
#if STATS
				ScissorClips,
				StencilClips,
#endif
				StencilRef,
				MaskingID,
				BackBuffer,
				RenderBatch,
				ColorTarget,
				DepthStencilTarget,
				LastClippingState,
				ViewTranslation2D,
				bSwitchVerticalAxis,
				GraphicsPSOInit,
				StencilVertexBuffer,
				ViewProjection,
				false);

			const uint32 PrimitiveCount = RenderBatch.DrawPrimitiveType == ESlateDrawPrimitive::LineList ? RenderBatch.NumIndices / 2 : RenderBatch.NumIndices / 3;

			ESlateShaderResource::Type ResourceType = ShaderResource ? ShaderResource->GetType() : ESlateShaderResource::Invalid;
			if (ResourceType != ESlateShaderResource::Material && ShaderType != ESlateShader::PostProcess)
			{
				check(RHICmdList.IsInsideRenderPass());
				check(RenderBatch.NumIndices > 0);
				FSlateElementPS* PixelShader = nullptr;

				const bool bUseInstancing = RenderBatch.InstanceCount > 1 && RenderBatch.InstanceData != nullptr;
				check(bUseInstancing == false);

#if WITH_SLATE_VISUALIZERS
				FSlateDebugBatchingPS* BatchingPixelShader = nullptr;
				if (CVarShowSlateBatching.GetValueOnRenderThread() != 0)
				{
					BatchingPixelShader = *TShaderMapRef<FSlateDebugBatchingPS>(ShaderMap);
					PixelShader = BatchingPixelShader;
				}
				else
#endif
				{
					PixelShader = GetTexturePixelShader(ShaderMap, ShaderType, DrawEffects);
				}

#if WITH_SLATE_VISUALIZERS
				if (CVarShowSlateBatching.GetValueOnRenderThread() != 0)
				{
					GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
				}
				else if (CVarShowSlateOverdraw.GetValueOnRenderThread() != 0)
				{
					GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI();
				}
				else
#endif
				{
					GraphicsPSOInit.BlendState =
						EnumHasAllFlags(DrawFlags, ESlateBatchDrawFlag::NoBlending)
						? TStaticBlendState<>::GetRHI()
						: (EnumHasAllFlags(DrawFlags, ESlateBatchDrawFlag::PreMultipliedAlpha)
							? TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI()
							: TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI())
						;
				}

				if (EnumHasAllFlags(DrawFlags, ESlateBatchDrawFlag::Wireframe) || Params.bWireFrame)
				{
					GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Wireframe, CM_None, false>::GetRHI();

					if (Params.bWireFrame)
					{
						GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
					}
				}
				else
				{
					GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None, false>::GetRHI();
				}

				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GSlateVertexDeclaration.VertexDeclarationRHI;
				GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*GlobalVertexShader);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(PixelShader);
				GraphicsPSOInit.PrimitiveType = GetRHIPrimitiveType(RenderBatch.DrawPrimitiveType);

				SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

				RHICmdList.SetStencilRef(StencilRef);

#if WITH_SLATE_VISUALIZERS
				if (CVarShowSlateBatching.GetValueOnRenderThread() != 0)
				{
					BatchingPixelShader->SetBatchColor(RHICmdList, BatchColor);
				}
#endif

				FRHISamplerState* SamplerState = BilinearClamp;
				FRHITexture* TextureRHI = GWhiteTexture->TextureRHI;
				if (ShaderResource)
				{
					ETextureSamplerFilter Filter = ETextureSamplerFilter::Bilinear;

					if (ResourceType == ESlateShaderResource::TextureObject)
					{
						FSlateBaseUTextureResource* TextureObjectResource = (FSlateBaseUTextureResource*)ShaderResource;
						if (const UTexture* TextureObj = TextureObjectResource->GetTextureObject())
						{
							TextureObjectResource->CheckForStaleResources();

							TextureRHI = TextureObjectResource->AccessRHIResource();

							Filter = GetSamplerFilter(TextureObj);
						}
					}
					else
					{
						FRHITexture* NativeTextureRHI = ((TSlateTexture<FTexture2DRHIRef>*)ShaderResource)->GetTypedResource();
						// Atlas textures that have no content are never initialized but null textures are invalid on many platforms.
						TextureRHI = NativeTextureRHI ? NativeTextureRHI : (FRHITexture*)GWhiteTexture->TextureRHI;
					}

					if (EnumHasAllFlags(DrawFlags, (ESlateBatchDrawFlag::TileU | ESlateBatchDrawFlag::TileV)))
					{
						switch (Filter)
						{
						case ETextureSamplerFilter::Point:
							SamplerState = TStaticSamplerState<SF_Point, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
							break;
						case ETextureSamplerFilter::AnisotropicPoint:
							SamplerState = TStaticSamplerState<SF_AnisotropicPoint, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
							break;
						case ETextureSamplerFilter::Trilinear:
							SamplerState = TStaticSamplerState<SF_Trilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
							break;
						case ETextureSamplerFilter::AnisotropicLinear:
							SamplerState = TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
							break;
						default:
							SamplerState = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();
							break;
						}
					}
					else if (EnumHasAllFlags(DrawFlags, ESlateBatchDrawFlag::TileU))
					{
						switch (Filter)
						{
						case ETextureSamplerFilter::Point:
							SamplerState = TStaticSamplerState<SF_Point, AM_Wrap, AM_Clamp, AM_Wrap>::GetRHI();
							break;
						case ETextureSamplerFilter::AnisotropicPoint:
							SamplerState = TStaticSamplerState<SF_AnisotropicPoint, AM_Wrap, AM_Clamp, AM_Wrap>::GetRHI();
							break;
						case ETextureSamplerFilter::Trilinear:
							SamplerState = TStaticSamplerState<SF_Trilinear, AM_Wrap, AM_Clamp, AM_Wrap>::GetRHI();
							break;
						case ETextureSamplerFilter::AnisotropicLinear:
							SamplerState = TStaticSamplerState<SF_AnisotropicLinear, AM_Wrap, AM_Clamp, AM_Wrap>::GetRHI();
							break;
						default:
							SamplerState = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Clamp, AM_Wrap>::GetRHI();
							break;
						}
					}
					else if (EnumHasAllFlags(DrawFlags, ESlateBatchDrawFlag::TileV))
					{
						switch (Filter)
						{
						case ETextureSamplerFilter::Point:
							SamplerState = TStaticSamplerState<SF_Point, AM_Clamp, AM_Wrap, AM_Wrap>::GetRHI();
							break;
						case ETextureSamplerFilter::AnisotropicPoint:
							SamplerState = TStaticSamplerState<SF_AnisotropicPoint, AM_Clamp, AM_Wrap, AM_Wrap>::GetRHI();
							break;
						case ETextureSamplerFilter::Trilinear:
							SamplerState = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Wrap, AM_Wrap>::GetRHI();
							break;
						case ETextureSamplerFilter::AnisotropicLinear:
							SamplerState = TStaticSamplerState<SF_AnisotropicLinear, AM_Clamp, AM_Wrap, AM_Wrap>::GetRHI();
							break;
						default:
							SamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Wrap, AM_Wrap>::GetRHI();
							break;
						}
					}
					else
					{
						switch (Filter)
						{
						case ETextureSamplerFilter::Point:
							SamplerState = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
							break;
						case ETextureSamplerFilter::AnisotropicPoint:
							SamplerState = TStaticSamplerState<SF_AnisotropicPoint, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
							break;
						case ETextureSamplerFilter::Trilinear:
							SamplerState = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
							break;
						case ETextureSamplerFilter::AnisotropicLinear:
							SamplerState = TStaticSamplerState<SF_AnisotropicLinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
							break;
						default:
							SamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
							break;
						}
					}
				}

				{
					QUICK_SCOPE_CYCLE_COUNTER(Slate_SetTextureShaderParams);

					GlobalVertexShader->SetViewProjection(RHICmdList, ViewProjection);
					GlobalVertexShader->SetVerticalAxisMultiplier(RHICmdList, bSwitchVerticalAxis ? -1.0f : 1.0f);

					PixelShader->SetTexture(RHICmdList, TextureRHI, SamplerState);
					PixelShader->SetShaderParams(RHICmdList, ShaderParams.PixelParams);
					const float FinalGamma = EnumHasAnyFlags(DrawFlags, ESlateBatchDrawFlag::ReverseGamma) ? (1.0f / EngineGamma) : EnumHasAnyFlags(DrawFlags, ESlateBatchDrawFlag::NoGamma) ? 1.0f : DisplayGamma;
					const float FinalContrast = EnumHasAnyFlags(DrawFlags, ESlateBatchDrawFlag::NoGamma) ? 1 : DisplayContrast;
					PixelShader->SetDisplayGammaAndInvertAlphaAndContrast(RHICmdList, FinalGamma, EnumHasAllFlags(DrawEffects, ESlateDrawEffect::InvertAlpha) ? 1.0f : 0.0f, FinalContrast);
				}

				{
					SCOPE_CYCLE_COUNTER(STAT_SlateRTTextureDrawCall);

					// for RHIs that can't handle VertexOffset, we need to offset the stream source each time
					if (1 || (!GRHISupportsBaseVertexIndex && !bAbsoluteIndices))
					{
						RHICmdList.SetStreamSource(0, VertexBufferPtr->VertexBufferRHI, RenderBatch.VertexOffset * sizeof(FSlateVertex));
						RHICmdList.DrawIndexedPrimitive(IndexBufferPtr->IndexBufferRHI, 0, 0, RenderBatch.NumVertices, RenderBatch.IndexOffset, PrimitiveCount, RenderBatch.InstanceCount);
					}
					else
					{
						uint32 VertexOffset = bAbsoluteIndices ? 0 : RenderBatch.VertexOffset;
						RHICmdList.SetStreamSource(0, VertexBufferPtr->VertexBufferRHI, 0);
						RHICmdList.DrawIndexedPrimitive(IndexBufferPtr->IndexBufferRHI, VertexOffset, 0, RenderBatch.NumVertices, RenderBatch.IndexOffset, PrimitiveCount, RenderBatch.InstanceCount);
					}
				}
			}
			else if (GEngine && ShaderResource && ShaderResource->GetType() == ESlateShaderResource::Material && ShaderType != ESlateShader::PostProcess)
			{
				SLATE_DRAW_EVENT(RHICmdList, MaterialBatch);
				check(RHICmdList.IsInsideRenderPass());

				check(RenderBatch.NumIndices > 0);
				// Note: This code is only executed if the engine is loaded (in early loading screens attempting to use a material is unsupported
				int ActiveSceneIndex = RenderBatch.SceneIndex;

				// We are assuming at this point that the SceneIndex from the batch is either -1, meaning no scene or a valid scene.
				// We set up the "no scene" option as the last SceneView in the array above.
				if (RenderBatch.SceneIndex == -1)
				{
					ActiveSceneIndex = NumScenes - 1;
				}
				else if (RenderBatch.SceneIndex >= ResourceManager->GetSceneCount())
				{
					// Ideally we should never hit this scenario, but given that Paragon may be using cached
					// render batches and is running into this daily, for this branch we should
					// just ignore the scene if the index is invalid. Note that the
					// MaterialParameterCollections will not be correct for this scene, should they be
					// used.
					ActiveSceneIndex = NumScenes - 1;
#if UE_BUILD_DEBUG && WITH_EDITOR
					UE_LOG(LogSlate, Error, TEXT("Invalid scene index in batch: %d of %d known scenes!"), RenderBatch.SceneIndex, ResourceManager->GetSceneCount());
#endif
				}

				// Handle the case where we skipped out early above
				if (SceneViews[ActiveSceneIndex] == nullptr)
				{
					continue;
				}

				const FSceneView& ActiveSceneView = *SceneViews[ActiveSceneIndex];

				FSlateMaterialResource* MaterialShaderResource = (FSlateMaterialResource*)ShaderResource;
				if (MaterialShaderResource->GetMaterialObject() != nullptr)
				{
					MaterialShaderResource->CheckForStaleResources();

					FMaterialRenderProxy* MaterialRenderProxy = MaterialShaderResource->GetRenderProxy();

					const FMaterial* Material = MaterialRenderProxy->GetMaterial(ActiveSceneView.GetFeatureLevel());

					FSlateMaterialShaderPS* PixelShader = GetMaterialPixelShader(Material, ShaderType, DrawEffects);

					const bool bUseInstancing = RenderBatch.InstanceCount > 0 && RenderBatch.InstanceData != nullptr;
					FSlateMaterialShaderVS* VertexShader = GetMaterialVertexShader(Material, bUseInstancing);

					if (VertexShader && PixelShader)
					{
#if WITH_SLATE_VISUALIZERS
						if (CVarShowSlateBatching.GetValueOnRenderThread() != 0)
						{
							FSlateDebugBatchingPS* BatchingPixelShader = *TShaderMapRef<FSlateDebugBatchingPS>(ShaderMap);

							GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = bUseInstancing ? GSlateInstancedVertexDeclaration.VertexDeclarationRHI : GSlateVertexDeclaration.VertexDeclarationRHI;
							GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*GlobalVertexShader);
							GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(BatchingPixelShader);

							BatchingPixelShader->SetBatchColor(RHICmdList, BatchColor);
							GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI();
						}
						else if (CVarShowSlateOverdraw.GetValueOnRenderThread() != 0)
						{
							FSlateElementPS* OverdrawPixelShader = *TShaderMapRef<FSlateDebugOverdrawPS>(ShaderMap);

							GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = bUseInstancing ? GSlateInstancedVertexDeclaration.VertexDeclarationRHI : GSlateVertexDeclaration.VertexDeclarationRHI;
							GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*GlobalVertexShader);
							GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(OverdrawPixelShader);

							GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_InverseSourceAlpha>::GetRHI();
						}
						else
#endif
						{
							PixelShader->SetBlendState(GraphicsPSOInit, Material);
							FSlateShaderResource* MaskResource = MaterialShaderResource->GetTextureMaskResource();
							if (MaskResource)
							{
								GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_InverseDestAlpha, BF_One>::GetRHI();
							}

							GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = bUseInstancing ? GSlateInstancedVertexDeclaration.VertexDeclarationRHI : GSlateVertexDeclaration.VertexDeclarationRHI;
							GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(VertexShader);
							GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(PixelShader);
							GraphicsPSOInit.PrimitiveType = GetRHIPrimitiveType(RenderBatch.DrawPrimitiveType);

							SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

							RHICmdList.SetStencilRef(StencilRef);

							{
								QUICK_SCOPE_CYCLE_COUNTER(Slate_SetMaterialShaderParams);
								VertexShader->SetViewProjection(RHICmdList, ViewProjection);
								VertexShader->SetVerticalAxisMultiplier(RHICmdList, bSwitchVerticalAxis ? -1.0f : 1.0f);
								VertexShader->SetMaterialShaderParameters(RHICmdList, ActiveSceneView, MaterialRenderProxy, Material);

								PixelShader->SetParameters(RHICmdList, ActiveSceneView, MaterialRenderProxy, Material, ShaderParams.PixelParams);
								const float FinalGamma = EnumHasAnyFlags(DrawFlags, ESlateBatchDrawFlag::ReverseGamma) ? 1.0f / EngineGamma : EnumHasAnyFlags(DrawFlags, ESlateBatchDrawFlag::NoGamma) ? 1.0f : DisplayGamma;
								const float FinalContrast = EnumHasAnyFlags(DrawFlags, ESlateBatchDrawFlag::NoGamma) ? 1 : DisplayContrast;
								PixelShader->SetDisplayGammaAndContrast(RHICmdList, FinalGamma, FinalContrast);

								if (MaskResource)
								{
									FTexture2DRHIRef TextureRHI;
									TextureRHI = ((TSlateTexture<FTexture2DRHIRef>*)MaskResource)->GetTypedResource();

									PixelShader->SetAdditionalTexture(RHICmdList, TextureRHI, BilinearClamp);
								}
							}
						}

						{
							SCOPE_CYCLE_COUNTER(STAT_SlateRTMaterialDrawCall);
							if (bUseInstancing)
							{
								uint32 InstanceCount = RenderBatch.InstanceCount;

								if (GRHISupportsInstancing)
								{
									FSlateUpdatableInstanceBuffer* InstanceBuffer = (FSlateUpdatableInstanceBuffer*)RenderBatch.InstanceData;
									InstanceBuffer->BindStreamSource(RHICmdList, 1, RenderBatch.InstanceOffset);

									// for RHIs that can't handle VertexOffset, we need to offset the stream source each time
									if (1 || (!GRHISupportsBaseVertexIndex && !bAbsoluteIndices))
									{
										RHICmdList.SetStreamSource(0, VertexBufferPtr->VertexBufferRHI, RenderBatch.VertexOffset * sizeof(FSlateVertex));
										RHICmdList.DrawIndexedPrimitive(IndexBufferPtr->IndexBufferRHI, 0, 0, RenderBatch.NumVertices, RenderBatch.IndexOffset, PrimitiveCount, InstanceCount);
									}
									else
									{
										uint32 VertexOffset = bAbsoluteIndices ? 0 : RenderBatch.VertexOffset;
										RHICmdList.SetStreamSource(0, VertexBufferPtr->VertexBufferRHI, 0);
										RHICmdList.DrawIndexedPrimitive(IndexBufferPtr->IndexBufferRHI, VertexOffset, 0, RenderBatch.NumVertices, RenderBatch.IndexOffset, PrimitiveCount, InstanceCount);
									}
								}
							}
							else
							{
								RHICmdList.SetStreamSource(1, nullptr, 0);

								// for RHIs that can't handle VertexOffset, we need to offset the stream source each time
								if (1 || (!GRHISupportsBaseVertexIndex && !bAbsoluteIndices))
								{
									RHICmdList.SetStreamSource(0, VertexBufferPtr->VertexBufferRHI, RenderBatch.VertexOffset * sizeof(FSlateVertex));
									RHICmdList.DrawIndexedPrimitive(IndexBufferPtr->IndexBufferRHI, 0, 0, RenderBatch.NumVertices, RenderBatch.IndexOffset, PrimitiveCount, 1);
								}
								else
								{
									uint32 VertexOffset = bAbsoluteIndices ? 0 : RenderBatch.VertexOffset;
									RHICmdList.SetStreamSource(0, VertexBufferPtr->VertexBufferRHI, 0);
									RHICmdList.DrawIndexedPrimitive(IndexBufferPtr->IndexBufferRHI, VertexOffset, 0, RenderBatch.NumVertices, RenderBatch.IndexOffset, PrimitiveCount, 1);
								}
							}
						}
					}
				}
			}
			else if (ShaderType == ESlateShader::PostProcess)
			{
				SLATE_DRAW_EVENT(RHICmdList, PostProcess);
				RHICmdList.EndRenderPass();

				const FVector4& QuadPositionData = ShaderParams.PixelParams;

				FPostProcessRectParams RectParams;
				RectParams.SourceTexture = BackBuffer.GetRenderTargetTexture();
				RectParams.SourceRect = FSlateRect(0, 0, BackBuffer.GetSizeXY().X, BackBuffer.GetSizeXY().Y);
				RectParams.DestRect = FSlateRect(QuadPositionData.X, QuadPositionData.Y, QuadPositionData.Z, QuadPositionData.W);
				RectParams.SourceTextureSize = BackBuffer.GetSizeXY();

				RectParams.RestoreStateFunc = [&](FRHICommandListImmediate&InRHICmdList, FGraphicsPipelineStateInitializer& InGraphicsPSOInit) {
					return UpdateScissorRect(
						InRHICmdList,
#if STATS
						ScissorClips,
						StencilClips,
#endif
						StencilRef,
						MaskingID,
						BackBuffer,
						RenderBatch,
						ColorTarget,
						DepthStencilTarget,
						LastClippingState,
						ViewTranslation2D,
						bSwitchVerticalAxis,
						InGraphicsPSOInit,
						StencilVertexBuffer,
						Params.ViewProjectionMatrix,
						true);
				};

				RectParams.RestoreStateFuncPostPipelineState = [&]() {
					RHICmdList.SetStencilRef(StencilRef);
				};

				FBlurRectParams BlurParams;
				BlurParams.KernelSize = ShaderParams.PixelParams2.X;
				BlurParams.Strength = ShaderParams.PixelParams2.Y;
				BlurParams.DownsampleAmount = ShaderParams.PixelParams2.Z;

				PostProcessor->BlurRect(RHICmdList, RendererModule, BlurParams, RectParams);

				check(RHICmdList.IsOutsideRenderPass());
				// Render pass for slate elements will be restarted on a next loop iteration if any
			}
		}
		else
		{
			ICustomSlateElement* CustomDrawer = RenderBatch.CustomDrawer;
			if (CustomDrawer)
			{
				// CustomDrawers will change the rendertarget. So we must close any outstanding renderpasses.
				// Render pass for slate elements will be restarted on a next loop iteration if any
				RHICmdList.EndRenderPass();	

				SLATE_DRAW_EVENT(RHICmdList, CustomDrawer);

				// Disable scissor rect. A previous draw element may have had one
				RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
				LastClippingState = nullptr;

				// This element is custom and has no Slate geometry.  Tell it to render itself now
				CustomDrawer->DrawRenderThread(RHICmdList, &BackBuffer.GetRenderTargetTexture());

				//We reset the maskingID here because otherwise the RT might not get re-set in the lines above see: if (bClearStencil || bForceStateChange)
				MaskingID = 0;
			}
		} // CustomDrawer
	}

	// Don't do color correction on iOS or Android, we don't have the GPU overhead for it.
#if !(PLATFORM_IOS || PLATFORM_ANDROID)
	if (bApplyColorDeficiencyCorrection && GSlateColorDeficiencyType != EColorVisionDeficiency::NormalVision && GSlateColorDeficiencySeverity > 0)
	{
		if (RHICmdList.IsInsideRenderPass())
		{
			RHICmdList.EndRenderPass();
		}

		FPostProcessRectParams RectParams;
		RectParams.SourceTexture = BackBuffer.GetRenderTargetTexture();
		RectParams.SourceRect = FSlateRect(0, 0, BackBuffer.GetSizeXY().X, BackBuffer.GetSizeXY().Y);
		RectParams.DestRect = FSlateRect(0, 0, BackBuffer.GetSizeXY().X, BackBuffer.GetSizeXY().Y);
		RectParams.SourceTextureSize = BackBuffer.GetSizeXY();

		PostProcessor->ColorDeficiency(RHICmdList, RendererModule, RectParams);

		FRHIRenderPassInfo RPInfo(ColorTarget, ERenderTargetActions::Load_Store);
		RPInfo.DepthStencilRenderTarget.DepthStencilTarget = DepthStencilTarget;

		// @todo refactor this.
		// ColorDeficiency has self-contained renderpasses. To avoid starting an empty renderpass we do not
		// restart the renderpass here.
	}
#endif

	// Disable scissor rect we no longer need this.
	RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
	// Disable depth/stencil testing once we're done also.
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

	for (int i = 0; i < NumScenes; i++)
	{
		// Don't need to delete SceneViews b/c the SceneViewFamily will delete it when it goes away.
		delete SceneViewFamilyContexts[i];
	}

	SceneViews.Empty();
	SceneViewFamilyContexts.Empty();

	INC_DWORD_STAT_BY(STAT_SlateScissorClips, ScissorClips);
	INC_DWORD_STAT_BY(STAT_SlateStencilClips, StencilClips);

	// Disable scissor rect. 
	// This fixes drawing on Metal when the last drawn element used a valid scissor rect
	RHICmdList.SetScissorRect(false, 0, 0, 0, 0);
}

ETextureSamplerFilter FSlateRHIRenderingPolicy::GetSamplerFilter(const UTexture* Texture) const
{
	// Default to point filtering.
	ETextureSamplerFilter Filter = ETextureSamplerFilter::Point;

	switch (Texture->Filter)
	{
	case TF_Nearest: 
		Filter = ETextureSamplerFilter::Point; 
		break;
	case TF_Bilinear:
		Filter = ETextureSamplerFilter::Bilinear; 
		break;
	case TF_Trilinear: 
		Filter = ETextureSamplerFilter::Trilinear; 
		break;

		// TF_Default
	default:
		// Use LOD group value to find proper filter setting.
		if (Texture->LODGroup < TextureLODGroups.Num())
		{
			Filter = TextureLODGroups[Texture->LODGroup].Filter;
		}
	}

	return Filter;
}

FSlateElementPS* FSlateRHIRenderingPolicy::GetTexturePixelShader( TShaderMap<FGlobalShaderType>* ShaderMap, ESlateShader ShaderType, ESlateDrawEffect DrawEffects )
{
	FSlateElementPS* PixelShader = nullptr;
	QUICK_SCOPE_CYCLE_COUNTER(STAT_Slate_GetTexturePixelShader);

#if WITH_SLATE_VISUALIZERS
	if ( CVarShowSlateOverdraw.GetValueOnRenderThread() != 0 )
	{
		PixelShader = *TShaderMapRef<FSlateDebugOverdrawPS>(ShaderMap);
	}
	else
#endif
	{
	const bool bDrawDisabled = EnumHasAllFlags( DrawEffects, ESlateDrawEffect::DisabledEffect );
	const bool bUseTextureAlpha = !EnumHasAllFlags( DrawEffects, ESlateDrawEffect::IgnoreTextureAlpha );

	if ( bDrawDisabled )
	{
		switch ( ShaderType )
		{
		default:
		case ESlateShader::Default:
			if ( bUseTextureAlpha )
			{
				PixelShader = *TShaderMapRef<TSlateElementPS<ESlateShader::Default, true, true> >(ShaderMap);
			}
			else
			{
				PixelShader = *TShaderMapRef<TSlateElementPS<ESlateShader::Default, true, false> >(ShaderMap);
			}
			break;
		case ESlateShader::Border:
			if ( bUseTextureAlpha )
			{
				PixelShader = *TShaderMapRef<TSlateElementPS<ESlateShader::Border, true, true> >(ShaderMap);
			}
			else
			{
				PixelShader = *TShaderMapRef<TSlateElementPS<ESlateShader::Border, true, false> >(ShaderMap);
			}
			break;
		case ESlateShader::Font:
			PixelShader = *TShaderMapRef<TSlateElementPS<ESlateShader::Font, true> >(ShaderMap);
			break;
		case ESlateShader::LineSegment:
			PixelShader = *TShaderMapRef<TSlateElementPS<ESlateShader::LineSegment, true> >(ShaderMap);
			break;
		}
	}
	else
	{
		switch ( ShaderType )
		{
		default:
		case ESlateShader::Default:
			if ( bUseTextureAlpha )
			{
				PixelShader = *TShaderMapRef<TSlateElementPS<ESlateShader::Default, false, true> >(ShaderMap);
			}
			else
			{
				PixelShader = *TShaderMapRef<TSlateElementPS<ESlateShader::Default, false, false> >(ShaderMap);
			}
			break;
		case ESlateShader::Border:
			if ( bUseTextureAlpha )
			{
				PixelShader = *TShaderMapRef<TSlateElementPS<ESlateShader::Border, false, true> >(ShaderMap);
			}
			else
			{
				PixelShader = *TShaderMapRef<TSlateElementPS<ESlateShader::Border, false, false> >(ShaderMap);
			}
			break;
		case ESlateShader::Font:
			PixelShader = *TShaderMapRef<TSlateElementPS<ESlateShader::Font, false> >(ShaderMap);
			break;
		case ESlateShader::LineSegment:
			PixelShader = *TShaderMapRef<TSlateElementPS<ESlateShader::LineSegment, false> >(ShaderMap);
			break;
		}
	}
	}

#undef PixelShaderLookupTable

	return PixelShader;
}

FSlateMaterialShaderPS* FSlateRHIRenderingPolicy::GetMaterialPixelShader( const FMaterial* Material, ESlateShader ShaderType, ESlateDrawEffect DrawEffects )
{
	const FMaterialShaderMap* MaterialShaderMap = Material->GetRenderingThreadShaderMap();

	const bool bDrawDisabled = EnumHasAllFlags(DrawEffects, ESlateDrawEffect::DisabledEffect);
	const bool bUseTextureAlpha = !EnumHasAllFlags(DrawEffects, ESlateDrawEffect::IgnoreTextureAlpha);

	FShader* FoundShader = nullptr;
	switch (ShaderType)
	{
	case ESlateShader::Default:
		if (bDrawDisabled)
		{
			FoundShader = MaterialShaderMap->GetShader(&TSlateMaterialShaderPS<ESlateShader::Default, true>::StaticType);
		}
		else
		{
			FoundShader = MaterialShaderMap->GetShader(&TSlateMaterialShaderPS<ESlateShader::Default, false>::StaticType);
		}
		break;
	case ESlateShader::Border:
		if (bDrawDisabled)
		{
			FoundShader = MaterialShaderMap->GetShader(&TSlateMaterialShaderPS<ESlateShader::Border, true>::StaticType);
		}
		else
		{
			FoundShader = MaterialShaderMap->GetShader(&TSlateMaterialShaderPS<ESlateShader::Border, false>::StaticType);
		}
		break;
	case ESlateShader::Font:
		if(bDrawDisabled)
		{
			FoundShader = MaterialShaderMap->GetShader(&TSlateMaterialShaderPS<ESlateShader::Font, true>::StaticType);
		}
		else
		{
			FoundShader = MaterialShaderMap->GetShader(&TSlateMaterialShaderPS<ESlateShader::Font, false>::StaticType);
		}
		break;
	case ESlateShader::Custom:
		{
			FoundShader = MaterialShaderMap->GetShader(&TSlateMaterialShaderPS<ESlateShader::Custom, false>::StaticType);
		}
		break;
	default:
		checkf(false, TEXT("Unsupported Slate shader type for use with materials"));
		break;
	}

	return FoundShader ? (FSlateMaterialShaderPS*)FoundShader->GetShader() : nullptr;
}

FSlateMaterialShaderVS* FSlateRHIRenderingPolicy::GetMaterialVertexShader( const FMaterial* Material, bool bUseInstancing )
{
	const FMaterialShaderMap* MaterialShaderMap = Material->GetRenderingThreadShaderMap();

	FShader* FoundShader = nullptr;
	if( bUseInstancing )
	{
		FoundShader = MaterialShaderMap->GetShader(&TSlateMaterialShaderVS<true>::StaticType);
	}
	else
	{
		FoundShader = MaterialShaderMap->GetShader(&TSlateMaterialShaderVS<false>::StaticType);
	}
	
	return FoundShader ? (FSlateMaterialShaderVS*)FoundShader->GetShader() : nullptr;
}

EPrimitiveType FSlateRHIRenderingPolicy::GetRHIPrimitiveType(ESlateDrawPrimitive SlateType)
{
	switch(SlateType)
	{
	case ESlateDrawPrimitive::LineList:
		return PT_LineList;
	case ESlateDrawPrimitive::TriangleList:
	default:
		return PT_TriangleList;
	}

};


void FSlateRHIRenderingPolicy::AddSceneAt(FSceneInterface* Scene, int32 Index)
{
	ResourceManager->AddSceneAt(Scene, Index);
}

void FSlateRHIRenderingPolicy::ClearScenes()
{
	ResourceManager->ClearScenes();
}

void FSlateRHIRenderingPolicy::FlushGeneratedResources()
{
	PostProcessor->ReleaseRenderTargets();
}
	
