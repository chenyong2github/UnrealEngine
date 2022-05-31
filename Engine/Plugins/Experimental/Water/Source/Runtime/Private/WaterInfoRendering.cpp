// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterInfoRendering.h"

#include "EngineModule.h"
#include "LegacyScreenPercentageDriver.h"
#include "RenderCaptureInterface.h"
#include "WaterBodyActor.h"
#include "WaterBodySceneProxy.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Renderer/Private/ScenePrivate.h"
#include "Renderer/Private/RendererModule.h"
#include "Renderer/Private/SceneCaptureRendering.h"
#include "Renderer/Private/PostProcess/SceneFilterRendering.h"
#include "Math/OrthoMatrix.h"
#include "GameFramework/WorldSettings.h"
#include "WaterZoneActor.h"
#include "Rendering/NaniteStreamingManager.h"
#include "SceneViewExtension.h"
#include "LandscapeRender.h"

static int32 RenderCaptureNextWaterInfoDraws = 0;
static FAutoConsoleVariableRef CVarRenderCaptureNextWaterInfoDraws(
	TEXT("r.Water.WaterInfo.RenderCaptureNextWaterInfoDraws"),
	RenderCaptureNextWaterInfoDraws,
	TEXT("Enable capturing of the water info texture for the next N draws"));

namespace UE::WaterInfo
{

struct FUpdateWaterInfoParams
{
	FSceneRenderer* DepthRenderer;
	FSceneRenderer* ColorRenderer;
	FRenderTarget* RenderTarget;
	FTexture* OutputTexture;

	FVector2D WaterZoneExtents;
	FVector2f WaterHeightExtents;
	float GroundZMin;
	float CaptureZ;
	int32 VelocityBlurRadius;
};


// ---------------------------------------------------------------------------------------------------------------------

/** A pixel shader for capturing a component of the rendered scene for a scene capture.*/
class FWaterInfoMergePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FWaterInfoMergePS);
	SHADER_USE_PARAMETER_STRUCT(FWaterInfoMergePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthTexture)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float4>, ColorTexture)
		SHADER_PARAMETER(FVector2f, WaterHeightExtents)
		SHADER_PARAMETER(float, GroundZMin)
		SHADER_PARAMETER(float, CaptureZ)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		// Water info merge unconditionally requires a 128 bit render target. Some platforms require explicitly enabling this output mode.
		bool bPlatformRequiresExplicit128bitRT = FDataDrivenShaderPlatformInfo::GetRequiresExplicit128bitRT(Parameters.Platform);
		if (bPlatformRequiresExplicit128bitRT)
		{
			OutEnvironment.SetRenderTargetOutputFormat(0, PF_A32B32G32R32F);
		}
	}
};

IMPLEMENT_GLOBAL_SHADER(FWaterInfoMergePS, "/Plugin/Water/Private/WaterInfoMerge.usf", "Main", SF_Pixel);

static void MergeWaterInfoAndDepth(
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	const FSceneViewFamily& ViewFamily,
	FViewInfo& View,
	FRDGTextureRef OutputTexture,
	FRDGTextureRef DepthTexture,
	FRDGTextureRef ColorTexture,
	FUpdateWaterInfoParams Params)
{
	RDG_EVENT_SCOPE(GraphBuilder, "WaterInfoDepthMerge");

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();

	{
		FWaterInfoMergePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FWaterInfoMergePS::FParameters>();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ENoAction);
		PassParameters->SceneTextures = SceneTextures.GetSceneTextureShaderParameters(ViewFamily.GetFeatureLevel());
		PassParameters->DepthTexture = DepthTexture;
		PassParameters->ColorTexture = ColorTexture;
		PassParameters->CaptureZ = Params.CaptureZ;
		PassParameters->WaterHeightExtents = Params.WaterHeightExtents;
		PassParameters->GroundZMin = Params.GroundZMin;

		TShaderMapRef<FScreenVS> VertexShader(View.ShaderMap);
		TShaderMapRef<FWaterInfoMergePS> PixelShader(View.ShaderMap);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("WaterInfoDepthMerge"),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, GraphicsPSOInit, VertexShader, PixelShader, &View](FRHICommandListImmediate& RHICmdList)
			{
				FGraphicsPipelineStateInitializer LocalGraphicsPSOInit = GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(LocalGraphicsPSOInit);
				SetGraphicsPipelineState(RHICmdList, LocalGraphicsPSOInit, 0);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);

				DrawRectangle(
					RHICmdList,
					View.ViewRect.Min.X, View.ViewRect.Min.Y,
					View.ViewRect.Width(), View.ViewRect.Height(),
					View.ViewRect.Min.X, View.ViewRect.Min.Y,
					View.ViewRect.Width(), View.ViewRect.Height(),
					View.UnconstrainedViewRect.Size(),
					View.UnconstrainedViewRect.Size(),
					VertexShader,
					EDRF_UseTriangleOptimization);
			});
	}
}

// ---------------------------------------------------------------------------------------------------------------------


/** A pixel shader for capturing a component of the rendered scene for a scene capture.*/
class FWaterInfoFinalizePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FWaterInfoFinalizePS);
	SHADER_USE_PARAMETER_STRUCT(FWaterInfoFinalizePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, WaterInfoTexture)
		SHADER_PARAMETER(FVector2f, WaterHeightExtents)
		SHADER_PARAMETER(float, GroundZMin)
		SHADER_PARAMETER(float, CaptureZ)
		SHADER_PARAMETER(int, BlurRadius)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	class FEnable128BitRT : SHADER_PERMUTATION_BOOL("ENABLE_128_BIT");
	using FPermutationDomain = TShaderPermutationDomain<FEnable128BitRT>;

	static FPermutationDomain GetPermutationVector(bool bUse128BitRT)
	{
		FPermutationDomain PermutationVector;
		PermutationVector.Set<FEnable128BitRT>(bUse128BitRT);
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{
		FPermutationDomain PermutationVector(Parameters.PermutationId);

		bool bPlatformRequiresExplicit128bitRT = FDataDrivenShaderPlatformInfo::GetRequiresExplicit128bitRT(Parameters.Platform);
		return (!PermutationVector.Get<FEnable128BitRT>() || bPlatformRequiresExplicit128bitRT);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FEnable128BitRT>())
		{
			OutEnvironment.SetRenderTargetOutputFormat(0, PF_A32B32G32R32F);
		}
	}
};

IMPLEMENT_GLOBAL_SHADER(FWaterInfoFinalizePS, "/Plugin/Water/Private/WaterInfoFinalize.usf", "Main", SF_Pixel);

static void FinalizeWaterInfo(
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	const FSceneViewFamily& ViewFamily,
	FViewInfo& View,
	FRDGTextureRef WaterInfoTexture,
	FRDGTextureRef OutputTexture,
	FUpdateWaterInfoParams Params)
{
	RDG_EVENT_SCOPE(GraphBuilder, "WaterInfoFinalize");

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();

	const bool bUse128BitRT = PlatformRequires128bitRT(OutputTexture->Desc.Format);
	const FWaterInfoFinalizePS::FPermutationDomain PixelPermutationVector = FWaterInfoFinalizePS::GetPermutationVector(bUse128BitRT);

	{
		FWaterInfoFinalizePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FWaterInfoFinalizePS::FParameters>();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ENoAction);
		PassParameters->SceneTextures = SceneTextures.GetSceneTextureShaderParameters(ViewFamily.GetFeatureLevel());
		PassParameters->WaterInfoTexture = WaterInfoTexture;
		PassParameters->BlurRadius = Params.VelocityBlurRadius;
		PassParameters->CaptureZ = Params.CaptureZ;
		PassParameters->WaterHeightExtents = Params.WaterHeightExtents;
		PassParameters->GroundZMin = Params.GroundZMin;

		TShaderMapRef<FScreenVS> VertexShader(View.ShaderMap);
		TShaderMapRef<FWaterInfoFinalizePS> PixelShader(View.ShaderMap, PixelPermutationVector);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("WaterInfoFinalize"),
			PassParameters,
			ERDGPassFlags::Raster,
			[PassParameters, GraphicsPSOInit, VertexShader, PixelShader, &View](FRHICommandListImmediate& RHICmdList)
			{
				FGraphicsPipelineStateInitializer LocalGraphicsPSOInit = GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(LocalGraphicsPSOInit);
				SetGraphicsPipelineState(RHICmdList, LocalGraphicsPSOInit, 0);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);
				
				DrawRectangle(
					RHICmdList,
					View.ViewRect.Min.X, View.ViewRect.Min.Y,
					View.ViewRect.Width(), View.ViewRect.Height(),
					View.ViewRect.Min.X, View.ViewRect.Min.Y,
					View.ViewRect.Width(), View.ViewRect.Height(),
					View.UnconstrainedViewRect.Size(),
					View.UnconstrainedViewRect.Size(),
					VertexShader,
					EDRF_UseTriangleOptimization);
			});
	}
}
// ---------------------------------------------------------------------------------------------------------------------

static FMatrix BuildOrthoMatrix(float InOrthoWidth, float InOrthoHeight)
{
	check((int32)ERHIZBuffer::IsInverted);

	const float OrthoWidth = InOrthoWidth / 2.0f;
	const float OrthoHeight = InOrthoHeight / 2.0f;

	const float NearPlane = 0.f;
	const float FarPlane = WORLD_MAX / 8.0f;

	const float ZScale = 1.0f / (FarPlane - NearPlane);
	const float ZOffset = 0;

	return FReversedZOrthoMatrix(
		OrthoWidth,
		OrthoHeight,
		ZScale,
		ZOffset
		);
}

// ---------------------------------------------------------------------------------------------------------------------

static void SetWaterBodiesWithinWaterInfoPass(FSceneRenderer* SceneRenderer, bool bWithinWaterInfoPass)
{
	if (SceneRenderer->Views[0].ShowOnlyPrimitives.IsSet())
	{
		for (FPrimitiveComponentId PrimId : SceneRenderer->Views[0].ShowOnlyPrimitives.GetValue())
		{
			for (FPrimitiveSceneProxy* PrimProxy : SceneRenderer->Scene->PrimitiveSceneProxies)
			{
				if (PrimProxy && PrimProxy->GetPrimitiveComponentId() == PrimId)
				{
					FWaterBodySceneProxy* WaterProxy = (FWaterBodySceneProxy*)PrimProxy;
					WaterProxy->SetWithinWaterInfoPass(bWithinWaterInfoPass);
				}
			}
		}
	}
}

static void SetOptimalLandscapeLODOverrides(const FUpdateWaterInfoParams& Params)
{
	// In order to prevent overdrawing the landscape components, we compute the lowest-detailed LOD level which satisfies the pixel coverage of the Water Info texture
	// and force it on all landscape components. This override is set different per Landscape actor in case there are multiple under the same water zone.
	//
	// Ex: If the WaterInfoTexture only has 1 pixel per 100 units, and the highest landscape LOD has 1 vertex per 20 units, we don't need to use the maximum landscape LOD
	// and can force a lower level of detail (in this case LOD2) while still satisfying the resolution of the water info texture.

	const double MinWaterInfoTextureExtent = (double)FMath::Min(Params.OutputTexture->GetSizeX(), Params.OutputTexture->GetSizeY());
	const double MaxWaterZoneExtent = FMath::Max(Params.WaterZoneExtents.X, Params.WaterZoneExtents.Y);
	const double WaterInfoUnitsPerPixel =  MaxWaterZoneExtent / MinWaterInfoTextureExtent;

	for (TPair<FLandscapeNeighborInfo::FLandscapeKey, FLandscapeRenderSystem*>& Pair : LandscapeRenderSystems)
	{
		int32 OptimalLODLevel = -1;
		// All components within the same landscape (and thus its render system) should have the same number of quads and the same extent.
		// therefore we can simply find the first component and compute its optimal LOD level.
		for (FLandscapeComponentSceneProxy* LandscapeComponentProxy : Pair.Value->SceneProxies)
		{
			if (LandscapeComponentProxy != nullptr)
			{
				// LandscapeComponent Max Extend represents the half-extent of the landscape component. Multiply by two to get the actual size.
				const double LandscapeComponentFullExtent = 2.0 * LandscapeComponentProxy->GetComponentMaxExtend();
				const double LandscapeComponentUnitsPerVertex = LandscapeComponentFullExtent / (double)(LandscapeComponentProxy->GetComponentSizeQuads() + 1);

				// Derived from:
				// LandscapeComponentUnitsPerVertex * 2 ^ (LODLevel) <= WaterInfoUnitsPerPixel
				OptimalLODLevel = FMath::FloorLog2(WaterInfoUnitsPerPixel / LandscapeComponentUnitsPerVertex);

				break;
			}
		}

		// There should always be at least one valid component proxy and the optimal LOD level should never be negative.
		check(OptimalLODLevel >= 0);

		for (FLandscapeRenderSystem::LODSettingsComponent& LODSettings : Pair.Value->SectionLODSettings)
		{
			LODSettings.ForcedLOD = OptimalLODLevel;
		}
	}
}

static void ResetLandscapeLODOverrides()
{
	for (TPair<FLandscapeNeighborInfo::FLandscapeKey, FLandscapeRenderSystem*>& Pair : LandscapeRenderSystems)
	{
		for (FLandscapeRenderSystem::LODSettingsComponent& LODSettings : Pair.Value->SectionLODSettings)
		{
			LODSettings.ForcedLOD = -1;
		}
	}
}

static void UpdateWaterInfoRendering_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	const FUpdateWaterInfoParams& Params)
{
	SetOptimalLandscapeLODOverrides(Params);

	FMaterialRenderProxy::UpdateDeferredCachedUniformExpressions();

	FRenderTarget* RenderTarget = Params.RenderTarget;
	FTexture* OutputTexture = Params.OutputTexture;

	// Depth-only pass for actors which are considered the ground for water rendering
	{
		FSceneRenderer* DepthRenderer = Params.DepthRenderer;

		// We need to execute the pre-render view extensions before we do any view dependent work.
		FSceneRenderer::ViewExtensionPreRender_RenderThread(RHICmdList, DepthRenderer);

		DepthRenderer->RenderThreadBegin(RHICmdList);
		
		FDeferredUpdateResource::UpdateResources(RHICmdList);
		
		SCOPED_DRAW_EVENT(RHICmdList, DepthRendering_RT);
		
		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("WaterInfoColorRendering"), ERDGBuilderFlags::AllowParallelExecute);
		
		FRDGTextureRef TargetTexture = RegisterExternalTexture(GraphBuilder, RenderTarget->GetRenderTargetTexture(), TEXT("WaterDepthTarget"));
		

		FViewInfo& View = DepthRenderer->Views[0];

		AddClearRenderTargetPass(GraphBuilder, TargetTexture, FLinearColor::Black, View.UnscaledViewRect);

		View.bDisableQuerySubmissions = true;
		View.bIgnoreExistingQueries = true;

		{
			RDG_RHI_EVENT_SCOPE(GraphBuilder, RenderWaterInfoDepth);
			DepthRenderer->Render(GraphBuilder);
		}

		FRDGTextureRef ShaderResourceTexture = RegisterExternalTexture(GraphBuilder, OutputTexture->TextureRHI, TEXT("WaterDepthTexture"));
		AddCopyTexturePass(GraphBuilder, TargetTexture, ShaderResourceTexture);

		if (DepthRenderer->Scene->GetShadingPath() == EShadingPath::Mobile)
		{
			const FMinimalSceneTextures& SceneTextures = View.GetSceneTextures();
			const bool bNeedsFlippedRenderTarget = false;
			RDG_EVENT_SCOPE(GraphBuilder, "CaptureSceneColor");
			CopySceneCaptureComponentToTarget(
				GraphBuilder,
				SceneTextures,
				ShaderResourceTexture,
				*DepthRenderer->ActiveViewFamily,
				DepthRenderer->Views,
				bNeedsFlippedRenderTarget);
		}
		
		GraphBuilder.Execute();

		DepthRenderer->RenderThreadEnd(RHICmdList);
	}

	// Render the water bodies' data including flow, zoffset, depth
	{
		FSceneRenderer* ColorRenderer = Params.ColorRenderer;

		// We need to execute the pre-render view extensions before we do any view dependent work.
		FSceneRenderer::ViewExtensionPreRender_RenderThread(RHICmdList, ColorRenderer);

		ColorRenderer->RenderThreadBegin(RHICmdList);

		SetWaterBodiesWithinWaterInfoPass(ColorRenderer, true);
		
		SCOPED_DRAW_EVENT(RHICmdList, ColorRendering_RT);
		
		FRDGBuilder GraphBuilder(RHICmdList, RDG_EVENT_NAME("WaterInfoColorRendering"), ERDGBuilderFlags::AllowParallelExecute);
		FRDGTextureRef TargetTexture = RegisterExternalTexture(GraphBuilder, RenderTarget->GetRenderTargetTexture(), TEXT("WaterInfoTarget"));

		FRDGTextureRef DepthTexture = GraphBuilder.CreateTexture(TargetTexture->Desc, TEXT("WaterInfoDepth"));
		AddCopyTexturePass(GraphBuilder, TargetTexture, DepthTexture);
		
		FViewInfo& View = ColorRenderer->Views[0];

		AddClearRenderTargetPass(GraphBuilder, TargetTexture, FLinearColor::Black, View.UnscaledViewRect);

		View.bDisableQuerySubmissions = true;
		View.bIgnoreExistingQueries = true;

		{
			RDG_RHI_EVENT_SCOPE(GraphBuilder, RenderWaterInfoColor);
			ColorRenderer->Render(GraphBuilder);
		}

		const FMinimalSceneTextures& SceneTextures = View.GetSceneTextures();
		FRDGTextureDesc ColorTextureDesc(TargetTexture->Desc);
		ColorTextureDesc.Format = PF_A32B32G32R32F;
		FRDGTextureRef ColorTexture = GraphBuilder.CreateTexture(ColorTextureDesc, TEXT("WaterInfoColor"));
		{
			const bool bNeedsFlippedRenderTarget = false;
			RDG_EVENT_SCOPE(GraphBuilder, "CaptureSceneColor");
			CopySceneCaptureComponentToTarget(
				GraphBuilder,
				SceneTextures,
				ColorTexture,
				*ColorRenderer->ActiveViewFamily,
				ColorRenderer->Views,
				bNeedsFlippedRenderTarget);
		}

		FRDGTextureRef MergeTargetTexture = GraphBuilder.CreateTexture(ColorTextureDesc, TEXT("WaterInfoMerged"));
		MergeWaterInfoAndDepth(GraphBuilder, SceneTextures, *ColorRenderer->ActiveViewFamily, ColorRenderer->Views[0], MergeTargetTexture, DepthTexture, ColorTexture, Params);

		FRDGTextureRef FinalizedTexture = GraphBuilder.CreateTexture(TargetTexture->Desc, TEXT("WaterInfoFinalized"));
		FinalizeWaterInfo(GraphBuilder, SceneTextures, *ColorRenderer->ActiveViewFamily, ColorRenderer->Views[0], MergeTargetTexture, FinalizedTexture, Params);
		
		FRDGTextureRef ShaderResourceTexture = RegisterExternalTexture(GraphBuilder, OutputTexture->TextureRHI, TEXT("WaterInfoResolve"));
		AddCopyTexturePass(GraphBuilder, FinalizedTexture, ShaderResourceTexture);
		GraphBuilder.Execute();
		
		SetWaterBodiesWithinWaterInfoPass(ColorRenderer, false);

		ColorRenderer->RenderThreadEnd(RHICmdList);
	}

	RHICmdList.Transition(FRHITransitionInfo(Params.OutputTexture->TextureRHI, ERHIAccess::RTV, ERHIAccess::SRVMask));

	ResetLandscapeLODOverrides();
}

static FEngineShowFlags GetWaterInfoBaseShowFlags()
{
	FEngineShowFlags ShowFlags(ESFIM_Game);
	ShowFlags.NaniteMeshes = 0;
	ShowFlags.Atmosphere = 0;
	ShowFlags.Lighting = 0;
	ShowFlags.Bloom = 0;
	ShowFlags.ScreenPercentage = 0;
	ShowFlags.Translucency = 0;
	ShowFlags.SeparateTranslucency = 0;
	ShowFlags.AntiAliasing = 0;
	ShowFlags.Fog = 0;
	ShowFlags.VolumetricFog = 0;
	ShowFlags.DynamicShadows = 0;
	return ShowFlags;
}

static FSceneRenderer* CreateWaterInfoDepthRenderer(
	FSceneInterface* Scene,
	FRenderTarget* RenderTarget,
	const WaterInfo::FRenderingContext& Context,
	FIntPoint RenderTargetSize,
	const FMatrix& ViewRotationMatrix,
	const FVector& ViewLocation,
	const FMatrix& ProjectionMatrix)
{
	FEngineShowFlags ShowFlags = GetWaterInfoBaseShowFlags();

	FSceneViewFamilyContext DepthViewFamily(FSceneViewFamily::ConstructionValues(
		RenderTarget,
		Scene,
		ShowFlags)
		.SetRealtimeUpdate(false)
		.SetResolveScene(false));
	DepthViewFamily.SceneCaptureSource = ESceneCaptureSource::SCS_DeviceDepth;

	// Setup the view family
	FSceneViewInitOptions DepthViewInitOptions;
	DepthViewInitOptions.SetViewRectangle(FIntRect(0, 0, RenderTargetSize.X, RenderTargetSize.Y));
	DepthViewInitOptions.ViewFamily = &DepthViewFamily;
	DepthViewInitOptions.ViewActor = Context.ZoneToRender;
	DepthViewInitOptions.ViewRotationMatrix = ViewRotationMatrix;
	DepthViewInitOptions.ViewOrigin = ViewLocation;
	DepthViewInitOptions.BackgroundColor = FLinearColor::Black;
	DepthViewInitOptions.OverrideFarClippingPlaneDistance = -1.f;
	DepthViewInitOptions.SceneViewStateInterface = nullptr;
	DepthViewInitOptions.ProjectionMatrix = ProjectionMatrix;
	DepthViewInitOptions.LODDistanceFactor = 0.001f;
	DepthViewInitOptions.OverlayColor = FLinearColor::Black;

	if (DepthViewFamily.Scene->GetWorld() != nullptr && DepthViewFamily.Scene->GetWorld()->GetWorldSettings() != nullptr)
	{
		DepthViewInitOptions.WorldToMetersScale = DepthViewFamily.Scene->GetWorld()->GetWorldSettings()->WorldToMeters;
	}

	FSceneView* DepthView = new FSceneView(DepthViewInitOptions);
	DepthView->AntiAliasingMethod = EAntiAliasingMethod::AAM_None;
	DepthView->SetupAntiAliasingMethod();

	if (Context.GroundActors.Num() > 0)
	{
		DepthView->ShowOnlyPrimitives.Emplace();
		DepthView->ShowOnlyPrimitives->Reserve(Context.GroundActors.Num());
		for (TWeakObjectPtr<AActor> GroundActor : Context.GroundActors)
		{
			if (GroundActor.IsValid())
			{
				TInlineComponentArray<UPrimitiveComponent*> PrimComps(GroundActor.Get());
				for (UPrimitiveComponent* PrimComp : PrimComps)
				{
					if (PrimComp)
					{
						DepthView->ShowOnlyPrimitives->Add(PrimComp->ComponentId);
					}
				}
			}
		}
	}

	DepthViewFamily.Views.Add(DepthView);
	
	DepthView->StartFinalPostprocessSettings(ViewLocation);
	DepthView->EndFinalPostprocessSettings(DepthViewInitOptions);

	DepthViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(DepthViewFamily, 1.f));

	DepthViewFamily.ViewExtensions = GEngine->ViewExtensions->GatherActiveExtensions(FSceneViewExtensionContext(Scene));
	for (const FSceneViewExtensionRef& Extension : DepthViewFamily.ViewExtensions)
	{
		Extension->SetupViewFamily(DepthViewFamily);
		Extension->SetupView(DepthViewFamily, *DepthView);
	}

	return FSceneRenderer::CreateSceneRenderer(&DepthViewFamily, nullptr);
}

static FSceneRenderer* CreateWaterInfoColorRenderer(
	FSceneInterface* Scene,
	FRenderTarget* RenderTarget,
	const WaterInfo::FRenderingContext& Context,
	FIntPoint RenderTargetSize,
	const FMatrix& ViewRotationMatrix,
	const FVector& ViewLocation,
	const FMatrix& ProjectionMatrix)
{
	FEngineShowFlags ShowFlags = GetWaterInfoBaseShowFlags();
	
	FSceneViewFamilyContext ColorViewFamily(FSceneViewFamily::ConstructionValues(
		RenderTarget,
		Scene,
		ShowFlags)
		.SetRealtimeUpdate(false)
		.SetResolveScene(false));
	ColorViewFamily.SceneCaptureSource = ESceneCaptureSource::SCS_SceneColorSceneDepth;

	FSceneViewInitOptions ColorViewInitOptions;
	ColorViewInitOptions.SetViewRectangle(FIntRect(0, 0, RenderTargetSize.X, RenderTargetSize.Y));
	ColorViewInitOptions.ViewFamily = &ColorViewFamily;
	ColorViewInitOptions.ViewActor = Context.ZoneToRender;
	ColorViewInitOptions.ViewRotationMatrix = ViewRotationMatrix;
	ColorViewInitOptions.ViewOrigin = ViewLocation;
	ColorViewInitOptions.BackgroundColor = FLinearColor::Black;
	ColorViewInitOptions.OverrideFarClippingPlaneDistance = -1.f;
	ColorViewInitOptions.SceneViewStateInterface = nullptr;
	ColorViewInitOptions.ProjectionMatrix = ProjectionMatrix;
	ColorViewInitOptions.LODDistanceFactor = 0.001f;
	ColorViewInitOptions.OverlayColor = FLinearColor::Black;

	if (ColorViewFamily.Scene->GetWorld() != nullptr && ColorViewFamily.Scene->GetWorld()->GetWorldSettings() != nullptr)
	{
		ColorViewInitOptions.WorldToMetersScale = ColorViewFamily.Scene->GetWorld()->GetWorldSettings()->WorldToMeters;
	}

	FSceneView* ColorView = new FSceneView(ColorViewInitOptions);
	ColorView->bIsSceneCapture = true;
	ColorView->AntiAliasingMethod = EAntiAliasingMethod::AAM_None;
	ColorView->SetupAntiAliasingMethod();

	if (Context.WaterBodies.Num() > 0)
	{
		ColorView->ShowOnlyPrimitives.Emplace();
		ColorView->ShowOnlyPrimitives->Reserve(Context.WaterBodies.Num());
		for (const UWaterBodyComponent* WaterBodyToRender : Context.WaterBodies)
		{
			ColorView->ShowOnlyPrimitives->Add(WaterBodyToRender->ComponentId);
		}
	}

	ColorViewFamily.Views.Add(ColorView);

	ColorView->StartFinalPostprocessSettings(ViewLocation);
	ColorView->EndFinalPostprocessSettings(ColorViewInitOptions);

	ColorViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(ColorViewFamily, 1.f));

	ColorViewFamily.ViewExtensions = GEngine->ViewExtensions->GatherActiveExtensions(FSceneViewExtensionContext(Scene));
	for (const FSceneViewExtensionRef& Extension : ColorViewFamily.ViewExtensions)
	{
		Extension->SetupViewFamily(ColorViewFamily);
		Extension->SetupView(ColorViewFamily, *ColorView);
	}

	return FSceneRenderer::CreateSceneRenderer(&ColorViewFamily, nullptr);
}

void UpdateWaterInfoRendering(
	FSceneInterface* Scene,
	const WaterInfo::FRenderingContext& Context)
{
	RenderCaptureInterface::FScopedCapture RenderCapture((RenderCaptureNextWaterInfoDraws != 0), TEXT("RenderWaterInfo"));
	RenderCaptureNextWaterInfoDraws = FMath::Max(0, RenderCaptureNextWaterInfoDraws - 1);

	if (Context.TextureRenderTarget == nullptr || Scene == nullptr)
	{
		return;
	}

	const FVector2D ZoneExtent = Context.ZoneToRender->GetZoneExtent();
	FVector ViewLocation = Context.ZoneToRender->GetActorLocation();
	ViewLocation.Z = Context.CaptureZ;

	// Zone rendering always happens facing towards negative z.
	const FVector LookAt = ViewLocation - FVector(0.f, 0.f, 1.f);

	FMatrix ViewRotationMat = FLookAtMatrix(ViewLocation, LookAt, FVector(0.f, -1.f, 0.f));
	ViewRotationMat = ViewRotationMat.RemoveTranslation();
	ViewRotationMat.RemoveScaling();
	
	const FIntPoint CaptureExtent(Context.TextureRenderTarget->GetSurfaceWidth(), Context.TextureRenderTarget->GetSurfaceHeight());

	const FMatrix OrthoProj = BuildOrthoMatrix(ZoneExtent.X, ZoneExtent.Y);

	FSceneRenderer* DepthRenderer = CreateWaterInfoDepthRenderer(
		Scene,
		Context.TextureRenderTarget->GameThread_GetRenderTargetResource(),
		Context,
		CaptureExtent,
		ViewRotationMat,
		ViewLocation,
		OrthoProj);
	
	FSceneRenderer* ColorRenderer = CreateWaterInfoColorRenderer(
		Scene,
		Context.TextureRenderTarget->GameThread_GetRenderTargetResource(),
		Context,
		CaptureExtent,
		ViewRotationMat,
		ViewLocation,
		OrthoProj);

	FTextureRenderTargetResource* TextureRenderTargetResource = Context.TextureRenderTarget->GameThread_GetRenderTargetResource();

	FUpdateWaterInfoParams Params;
	Params.DepthRenderer = DepthRenderer;
	Params.ColorRenderer = ColorRenderer;
	Params.RenderTarget = TextureRenderTargetResource;
	Params.OutputTexture = TextureRenderTargetResource;
	Params.CaptureZ = ViewLocation.Z;
	Params.WaterHeightExtents = Context.ZoneToRender->GetWaterHeightExtents();
	Params.GroundZMin = Context.ZoneToRender->GetGroundZMin();
	Params.VelocityBlurRadius = Context.ZoneToRender->GetVelocityBlurRadius();
	Params.WaterZoneExtents = Context.ZoneToRender->GetZoneExtent();

	ENQUEUE_RENDER_COMMAND(WaterInfoCommand)(
	[Params, ZoneName = Context.ZoneToRender->GetActorNameOrLabel()](FRHICommandListImmediate& RHICmdList)
		{
			SCOPED_DRAW_EVENTF(RHICmdList, WaterZoneInfoRendering_RT, TEXT("RenderWaterInfo_%s"), *ZoneName);

			UpdateWaterInfoRendering_RenderThread(RHICmdList, Params);
		});
}

} // namespace WaterInfo