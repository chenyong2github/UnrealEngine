// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	
=============================================================================*/

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Misc/MemStack.h"
#include "EngineDefines.h"
#include "RHIDefinitions.h"
#include "RHI.h"
#include "RenderingThread.h"
#include "Engine/Scene.h"
#include "SceneInterface.h"
#include "LegacyScreenPercentageDriver.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "RHIStaticStates.h"
#include "SceneView.h"
#include "Shader.h"
#include "TextureResource.h"
#include "StaticBoundShaderState.h"
#include "SceneUtils.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SceneCaptureComponentCube.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/TextureRenderTargetCube.h"
#include "PostProcess/SceneRenderTargets.h"
#include "GlobalShader.h"
#include "SceneRenderTargetParameters.h"
#include "SceneRendering.h"
#include "DeferredShadingRenderer.h"
#include "ScenePrivate.h"
#include "PostProcess/SceneFilterRendering.h"
#include "ScreenRendering.h"
#include "MobileSceneCaptureRendering.h"
#include "ClearQuad.h"
#include "PipelineStateCache.h"
#include "RendererModule.h"
#include "Rendering/MotionVectorSimulation.h"
#include "SceneViewExtension.h"
#include "GenerateMips.h"

static TAutoConsoleVariable<int32> CVarEnableViewExtensionsForSceneCapture(
	TEXT("r.SceneCapture.EnableViewExtensions"),
	0,
	TEXT("Whether to enable view extensions when doing scene capture.\n")
	TEXT("0: Disable view extensions (default).\n")
	TEXT("1: Enable view extensions.\n"),
	ECVF_Default);

/** A pixel shader for capturing a component of the rendered scene for a scene capture.*/
class FSceneCapturePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FSceneCapturePS);
	SHADER_USE_PARAMETER_STRUCT(FSceneCapturePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneTextureUniformParameters, SceneTextures)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	enum class ESourceMode : uint32
	{
		ColorAndOpacity,
		ColorNoAlpha,
		ColorAndSceneDepth,
		SceneDepth,
		DeviceDepth,
		Normal,
		BaseColor,
		MAX
	};

	class FSourceModeDimension : SHADER_PERMUTATION_ENUM_CLASS("SOURCE_MODE", ESourceMode);
	using FPermutationDomain = TShaderPermutationDomain<FSourceModeDimension>;

	static FPermutationDomain GetPermutationVector(ESceneCaptureSource CaptureSource)
	{
		ESourceMode SourceMode = ESourceMode::MAX;
		switch (CaptureSource)
		{
		case SCS_SceneColorHDR:
			SourceMode = ESourceMode::ColorAndOpacity;
			break;
		case SCS_SceneColorHDRNoAlpha:
			SourceMode = ESourceMode::ColorNoAlpha;
			break;
		case SCS_SceneColorSceneDepth:
			SourceMode = ESourceMode::ColorAndSceneDepth;
			break;
		case SCS_SceneDepth:
			SourceMode = ESourceMode::SceneDepth;
			break;
		case SCS_DeviceDepth:
			SourceMode = ESourceMode::DeviceDepth;
			break;
		case SCS_Normal:
			SourceMode = ESourceMode::Normal;
			break;
		case SCS_BaseColor:
			SourceMode = ESourceMode::BaseColor;
			break;
		default:
			checkf(false, TEXT("SceneCaptureSource not implemented."));
		}
		FPermutationDomain PermutationVector;
		PermutationVector.Set<FSourceModeDimension>(SourceMode);
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) 
	{ 
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		static const TCHAR* ShaderSourceModeDefineName[] =
		{
			TEXT("SOURCE_MODE_SCENE_COLOR_AND_OPACITY"),
			TEXT("SOURCE_MODE_SCENE_COLOR_NO_ALPHA"),
			TEXT("SOURCE_MODE_SCENE_COLOR_SCENE_DEPTH"),
			TEXT("SOURCE_MODE_SCENE_DEPTH"),
			TEXT("SOURCE_MODE_DEVICE_DEPTH"),
			TEXT("SOURCE_MODE_NORMAL"),
			TEXT("SOURCE_MODE_BASE_COLOR")
		};
		static_assert(UE_ARRAY_COUNT(ShaderSourceModeDefineName) == (uint32)ESourceMode::MAX, "ESourceMode doesn't match define table.");

		const FPermutationDomain PermutationVector(Parameters.PermutationId);
		const uint32 SourceModeIndex = static_cast<uint32>(PermutationVector.Get<FSourceModeDimension>());
		OutEnvironment.SetDefine(ShaderSourceModeDefineName[SourceModeIndex], 1u);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSceneCapturePS, "/Engine/Private/SceneCapturePixelShader.usf", "Main", SF_Pixel);

class FODSCapturePS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FODSCapturePS);
	SHADER_USE_PARAMETER_STRUCT(FODSCapturePS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(TextureCube, LeftEyeTexture)
		SHADER_PARAMETER_RDG_TEXTURE(TextureCube, RightEyeTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, LeftEyeTextureSampler)
		SHADER_PARAMETER_SAMPLER(SamplerState, RightEyeTextureSampler)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FODSCapturePS, "/Engine/Private/ODSCapture.usf", "MainPS", SF_Pixel);

static bool CaptureNeedsSceneColor(ESceneCaptureSource CaptureSource)
{
	return CaptureSource != SCS_FinalColorLDR && CaptureSource != SCS_FinalColorHDR && CaptureSource != SCS_FinalToneCurveHDR;
}

void FDeferredShadingSceneRenderer::CopySceneCaptureComponentToTarget(
	FRDGBuilder& GraphBuilder,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	FRDGTextureRef ViewFamilyTexture)
{
	ESceneCaptureSource SceneCaptureSource = ViewFamily.SceneCaptureSource;

	if (IsAnyForwardShadingEnabled(ViewFamily.GetShaderPlatform()) && (SceneCaptureSource == SCS_Normal || SceneCaptureSource == SCS_BaseColor))
	{
		SceneCaptureSource = SCS_SceneColorHDR;
	}

	if (CaptureNeedsSceneColor(SceneCaptureSource))
	{
		RDG_EVENT_SCOPE(GraphBuilder, "CaptureSceneComponent[%d]", SceneCaptureSource);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		if (SceneCaptureSource == SCS_SceneColorHDR && ViewFamily.SceneCaptureCompositeMode == SCCM_Composite)
		{
			// Blend with existing render target color. Scene capture color is already pre-multiplied by alpha.
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_Zero, BF_SourceAlpha>::GetRHI();
		}
		else if (SceneCaptureSource == SCS_SceneColorHDR && ViewFamily.SceneCaptureCompositeMode == SCCM_Additive)
		{
			// Add to existing render target color. Scene capture color is already pre-multiplied by alpha.
			GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_Zero, BF_SourceAlpha>::GetRHI();
		}
		else
		{
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		}

		const FSceneCapturePS::FPermutationDomain PixelPermutationVector = FSceneCapturePS::GetPermutationVector(SceneCaptureSource);

		TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextureUniformBuffer = CreateSceneTextureUniformBuffer(GraphBuilder, FeatureLevel, ESceneTextureSetupMode::GBuffers | ESceneTextureSetupMode::SceneColor | ESceneTextureSetupMode::SceneDepth);

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			FViewInfo& View = Views[ViewIndex];

			FSceneCapturePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSceneCapturePS::FParameters>();
			PassParameters->View = View.ViewUniformBuffer;
			PassParameters->SceneTextures = SceneTexturesUniformBuffer;
			PassParameters->RenderTargets[0] = FRenderTargetBinding(ViewFamilyTexture, ERenderTargetLoadAction::ENoAction);

			TShaderMapRef<FScreenVS> VertexShader(View.ShaderMap);
			TShaderMapRef<FSceneCapturePS> PixelShader(View.ShaderMap, PixelPermutationVector);

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("View(%d)", ViewIndex),
				PassParameters,
				ERDGPassFlags::Raster,
				[PassParameters, GraphicsPSOInit, VertexShader, PixelShader, &View] (FRHICommandListImmediate& RHICmdList)
			{
				FGraphicsPipelineStateInitializer LocalGraphicsPSOInit = GraphicsPSOInit;
				RHICmdList.ApplyCachedRenderTargets(LocalGraphicsPSOInit);
				SetGraphicsPipelineState(RHICmdList, LocalGraphicsPSOInit);
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);

				DrawRectangle(
					RHICmdList,
					View.ViewRect.Min.X, View.ViewRect.Min.Y,
					View.ViewRect.Width(), View.ViewRect.Height(),
					View.ViewRect.Min.X, View.ViewRect.Min.Y,
					View.ViewRect.Width(), View.ViewRect.Height(),
					View.UnconstrainedViewRect.Size(),
					FSceneRenderTargets::Get(RHICmdList).GetBufferSizeXY(),
					VertexShader,
					EDRF_UseTriangleOptimization);
			});
		}
	}
}

static void UpdateSceneCaptureContentDeferred_RenderThread(
	FRHICommandListImmediate& RHICmdList, 
	FSceneRenderer* SceneRenderer, 
	FRenderTarget* RenderTarget, 
	FTexture* RenderTargetTexture, 
	const FString& EventName, 
	const FResolveParams& ResolveParams,
	bool bGenerateMips,
	const FGenerateMipsParams& GenerateMipsParams
	)
{
	FMemMark MemStackMark(FMemStack::Get());

	// update any resources that needed a deferred update
	FDeferredUpdateResource::UpdateResources(RHICmdList);
	{
#if WANTS_DRAW_MESH_EVENTS
		SCOPED_DRAW_EVENTF(RHICmdList, SceneCapture, TEXT("SceneCapture %s"), *EventName);
#else
		SCOPED_DRAW_EVENT(RHICmdList, UpdateSceneCaptureContent_RenderThread);
#endif

		const FRenderTarget* Target = SceneRenderer->ViewFamily.RenderTarget;

		// TODO: Could avoid the clear by replacing with dummy black system texture.
		FViewInfo& View = SceneRenderer->Views[0];

		FRHIRenderPassInfo RPInfo(Target->GetRenderTargetTexture(), ERenderTargetActions::DontLoad_Store);
		RPInfo.ResolveParameters = ResolveParams;
		TransitionRenderPassTargets(RHICmdList, RPInfo);

		RHICmdList.BeginRenderPass(RPInfo, TEXT("ClearSceneCaptureContent"));
		DrawClearQuad(RHICmdList, true, FLinearColor::Black, false, 0, false, 0, Target->GetSizeXY(), View.UnscaledViewRect);
		RHICmdList.EndRenderPass();

		// Render the scene normally
		{
			SCOPED_DRAW_EVENT(RHICmdList, RenderScene);
			SceneRenderer->Render(RHICmdList);
		}

		FRDGBuilder GraphBuilder(RHICmdList);
		{
			FRDGTextureRef MipTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(RenderTarget->GetRenderTargetTexture(), TEXT("MipGenerationInput")));
			FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(RenderTargetTexture->TextureRHI, TEXT("MipGenerationOutput")));

			if (bGenerateMips)
			{
				FGenerateMips::Execute(GraphBuilder, MipTexture, GenerateMipsParams);
			}

			// Note: When the ViewFamily.SceneCaptureSource requires scene textures (i.e. SceneCaptureSource != SCS_FinalColorLDR), the copy to RenderTarget 
			// will be done in CopySceneCaptureComponentToTarget while the GBuffers are still alive for the frame.
			AddCopyToResolveTargetPass(GraphBuilder, MipTexture, OutputTexture, ResolveParams);
		}
		GraphBuilder.Execute();
	}

	FSceneRenderer::WaitForTasksClearSnapshotsAndDeleteSceneRenderer(RHICmdList, SceneRenderer);
}

static void ODSCapture_RenderThread(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef LeftEyeTexture,
	FRDGTextureRef RightEyeTexture,
	FRDGTextureRef OutputTexture,
	const ERHIFeatureLevel::Type FeatureLevel)
{
	FODSCapturePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FODSCapturePS::FParameters>();
	PassParameters->LeftEyeTexture = LeftEyeTexture;
	PassParameters->RightEyeTexture = RightEyeTexture;
	PassParameters->LeftEyeTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
	PassParameters->RightEyeTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
	PassParameters->RenderTargets[0] = FRenderTargetBinding(OutputTexture, ERenderTargetLoadAction::ELoad);

	const auto ShaderMap = GetGlobalShaderMap(FeatureLevel);
	TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
	TShaderMapRef<FODSCapturePS> PixelShader(ShaderMap);

	GraphBuilder.AddPass(
		RDG_EVENT_NAME("ODSCapture"),
		PassParameters,
		ERDGPassFlags::Raster,
		[VertexShader, PixelShader, OutputTexture](FRHICommandListImmediate& RHICmdList)
	{
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		extern TGlobalResource<FFilterVertexDeclaration> GFilterVertexDeclaration;

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		const FIntPoint& TargetSize = OutputTexture->Desc.Extent;
		RHICmdList.SetViewport(0, 0, 0.0f, TargetSize.X, TargetSize.Y, 1.0f);

		DrawRectangle(
			RHICmdList,
			0, 0,
			static_cast<float>(TargetSize.X), static_cast<float>(TargetSize.Y),
			0, 0,
			TargetSize.X, TargetSize.Y,
			TargetSize,
			TargetSize,
			VertexShader,
			EDRF_UseTriangleOptimization);
	});
}

static void UpdateSceneCaptureContent_RenderThread(
	FRHICommandListImmediate& RHICmdList,
	FSceneRenderer* SceneRenderer,
	FRenderTarget* RenderTarget,
	FTexture* RenderTargetTexture,
	const FString& EventName,
	const FResolveParams& ResolveParams,
	bool bGenerateMips,
	const FGenerateMipsParams& GenerateMipsParams,
	const bool bDisableFlipCopyLDRGLES)
{
	FMaterialRenderProxy::UpdateDeferredCachedUniformExpressions();

	switch (SceneRenderer->Scene->GetShadingPath())
	{
		case EShadingPath::Mobile:
		{
			UpdateSceneCaptureContentMobile_RenderThread(
				RHICmdList,
				SceneRenderer,
				RenderTarget,
				RenderTargetTexture,
				EventName,
				ResolveParams,
				bGenerateMips,
				GenerateMipsParams,
				bDisableFlipCopyLDRGLES);
			break;
		}
		case EShadingPath::Deferred:
		{
			UpdateSceneCaptureContentDeferred_RenderThread(
				RHICmdList,
				SceneRenderer,
				RenderTarget,
				RenderTargetTexture,
				EventName,
				ResolveParams,
				bGenerateMips,
				GenerateMipsParams);
			break;
		}
		default:
			checkNoEntry();
			break;
	}

	RHICmdList.Transition(FRHITransitionInfo(RenderTargetTexture->TextureRHI, ERHIAccess::Unknown, ERHIAccess::SRVMask));
}

void BuildProjectionMatrix(FIntPoint RenderTargetSize, ECameraProjectionMode::Type ProjectionType, float FOV, float InOrthoWidth, float InNearClippingPlane, FMatrix& ProjectionMatrix)
{
	float const XAxisMultiplier = 1.0f;
	float const YAxisMultiplier = RenderTargetSize.X / (float)RenderTargetSize.Y;

	if (ProjectionType == ECameraProjectionMode::Orthographic)
	{
		check((int32)ERHIZBuffer::IsInverted);
		const float OrthoWidth = InOrthoWidth / 2.0f;
		const float OrthoHeight = InOrthoWidth / 2.0f * XAxisMultiplier / YAxisMultiplier;

		const float NearPlane = 0;
		const float FarPlane = WORLD_MAX / 8.0f;

		const float ZScale = 1.0f / (FarPlane - NearPlane);
		const float ZOffset = -NearPlane;

		ProjectionMatrix = FReversedZOrthoMatrix(
			OrthoWidth,
			OrthoHeight,
			ZScale,
			ZOffset
			);
	}
	else
	{
		if ((int32)ERHIZBuffer::IsInverted)
		{
			ProjectionMatrix = FReversedZPerspectiveMatrix(
				FOV,
				FOV,
				XAxisMultiplier,
				YAxisMultiplier,
				InNearClippingPlane,
				InNearClippingPlane
				);
		}
		else
		{
			ProjectionMatrix = FPerspectiveMatrix(
				FOV,
				FOV,
				XAxisMultiplier,
				YAxisMultiplier,
				InNearClippingPlane,
				InNearClippingPlane
				);
		}
	}
}

void SetupViewFamilyForSceneCapture(
	FSceneViewFamily& ViewFamily,
	USceneCaptureComponent* SceneCaptureComponent,
	const TArrayView<const FSceneCaptureViewInfo> Views,
	float MaxViewDistance,
	bool bCaptureSceneColor,
	bool bIsPlanarReflection,
	FPostProcessSettings* PostProcessSettings,
	float PostProcessBlendWeight,
	const AActor* ViewActor)
{
	check(!ViewFamily.GetScreenPercentageInterface());

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
	{
		const FSceneCaptureViewInfo& SceneCaptureViewInfo = Views[ViewIndex];

		FSceneViewInitOptions ViewInitOptions;
		ViewInitOptions.SetViewRectangle(SceneCaptureViewInfo.ViewRect);
		ViewInitOptions.ViewFamily = &ViewFamily;
		ViewInitOptions.ViewActor = ViewActor;
		ViewInitOptions.ViewOrigin = SceneCaptureViewInfo.ViewLocation;
		ViewInitOptions.ViewRotationMatrix = SceneCaptureViewInfo.ViewRotationMatrix;
		ViewInitOptions.BackgroundColor = FLinearColor::Black;
		ViewInitOptions.OverrideFarClippingPlaneDistance = MaxViewDistance;
		ViewInitOptions.StereoPass = SceneCaptureViewInfo.StereoPass;
		ViewInitOptions.SceneViewStateInterface = SceneCaptureComponent->GetViewState(ViewIndex);
		ViewInitOptions.ProjectionMatrix = SceneCaptureViewInfo.ProjectionMatrix;
		ViewInitOptions.LODDistanceFactor = FMath::Clamp(SceneCaptureComponent->LODDistanceFactor, .01f, 100.0f);

		if (ViewFamily.Scene->GetWorld() != nullptr && ViewFamily.Scene->GetWorld()->GetWorldSettings() != nullptr)
		{
			ViewInitOptions.WorldToMetersScale = ViewFamily.Scene->GetWorld()->GetWorldSettings()->WorldToMeters;
		}
		ViewInitOptions.StereoIPD = SceneCaptureViewInfo.StereoIPD * (ViewInitOptions.WorldToMetersScale / 100.0f);

		if (bCaptureSceneColor)
		{
			ViewFamily.EngineShowFlags.PostProcessing = 0;
			ViewInitOptions.OverlayColor = FLinearColor::Black;
		}

		FSceneView* View = new FSceneView(ViewInitOptions);

		View->bIsSceneCapture = true;
		View->bSceneCaptureUsesRayTracing = SceneCaptureComponent->bUseRayTracingIfEnabled;
		// Note: this has to be set before EndFinalPostprocessSettings
		View->bIsPlanarReflection = bIsPlanarReflection;
        // Needs to be reconfigured now that bIsPlanarReflection has changed.
		View->SetupAntiAliasingMethod();

		check(SceneCaptureComponent);
		for (auto It = SceneCaptureComponent->HiddenComponents.CreateConstIterator(); It; ++It)
		{
			// If the primitive component was destroyed, the weak pointer will return NULL.
			UPrimitiveComponent* PrimitiveComponent = It->Get();
			if (PrimitiveComponent)
			{
				View->HiddenPrimitives.Add(PrimitiveComponent->ComponentId);
			}
		}

		for (auto It = SceneCaptureComponent->HiddenActors.CreateConstIterator(); It; ++It)
		{
			AActor* Actor = *It;

			if (Actor)
			{
				for (UActorComponent* Component : Actor->GetComponents())
				{
					if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Component))
					{
						View->HiddenPrimitives.Add(PrimComp->ComponentId);
					}
				}
			}
		}

		if (SceneCaptureComponent->PrimitiveRenderMode == ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList)
		{
			View->ShowOnlyPrimitives.Emplace();

			for (auto It = SceneCaptureComponent->ShowOnlyComponents.CreateConstIterator(); It; ++It)
			{
				// If the primitive component was destroyed, the weak pointer will return NULL.
				UPrimitiveComponent* PrimitiveComponent = It->Get();
				if (PrimitiveComponent)
				{
					View->ShowOnlyPrimitives->Add(PrimitiveComponent->ComponentId);
				}
			}

			for (auto It = SceneCaptureComponent->ShowOnlyActors.CreateConstIterator(); It; ++It)
			{
				AActor* Actor = *It;

				if (Actor)
				{
					for (UActorComponent* Component : Actor->GetComponents())
					{
						if (UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(Component))
						{
							View->ShowOnlyPrimitives->Add(PrimComp->ComponentId);
						}
					}
				}
			}
		}
		else if (SceneCaptureComponent->ShowOnlyComponents.Num() > 0 || SceneCaptureComponent->ShowOnlyActors.Num() > 0)
		{
			static bool bWarned = false;

			if (!bWarned)
			{
				UE_LOG(LogRenderer, Log, TEXT("Scene Capture has ShowOnlyComponents or ShowOnlyActors ignored by the PrimitiveRenderMode setting! %s"), *SceneCaptureComponent->GetPathName());
				bWarned = true;
			}
		}

		ViewFamily.Views.Add(View);

		View->StartFinalPostprocessSettings(SceneCaptureViewInfo.ViewLocation);
		View->OverridePostProcessSettings(*PostProcessSettings, PostProcessBlendWeight);
		View->EndFinalPostprocessSettings(ViewInitOptions);
	}
}

static FSceneRenderer* CreateSceneRendererForSceneCapture(
	FScene* Scene,
	USceneCaptureComponent* SceneCaptureComponent,
	FRenderTarget* RenderTarget,
	FIntPoint RenderTargetSize,
	const FMatrix& ViewRotationMatrix,
	const FVector& ViewLocation,
	const FMatrix& ProjectionMatrix,
	float MaxViewDistance,
	bool bCaptureSceneColor,
	FPostProcessSettings* PostProcessSettings,
	float PostProcessBlendWeight,
	const AActor* ViewActor, 
	const float StereoIPD = 0.0f)
{
	FSceneCaptureViewInfo SceneCaptureViewInfo;
	SceneCaptureViewInfo.ViewRotationMatrix = ViewRotationMatrix;
	SceneCaptureViewInfo.ViewLocation = ViewLocation;
	SceneCaptureViewInfo.ProjectionMatrix = ProjectionMatrix;
	SceneCaptureViewInfo.StereoPass = EStereoscopicPass::eSSP_FULL;
	SceneCaptureViewInfo.StereoIPD = StereoIPD;
	SceneCaptureViewInfo.ViewRect = FIntRect(0, 0, RenderTargetSize.X, RenderTargetSize.Y);

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		RenderTarget,
		Scene,
		SceneCaptureComponent->ShowFlags)
		.SetResolveScene(!bCaptureSceneColor)
		.SetRealtimeUpdate(SceneCaptureComponent->bCaptureEveryFrame || SceneCaptureComponent->bAlwaysPersistRenderingState));
	
	if (CVarEnableViewExtensionsForSceneCapture.GetValueOnAnyThread() > 0)
	{
		ViewFamily.ViewExtensions = GEngine->ViewExtensions->GatherActiveExtensions(FSceneViewExtensionContext(Scene));
	}
	
	SetupViewFamilyForSceneCapture(
		ViewFamily,
		SceneCaptureComponent,
		MakeArrayView(&SceneCaptureViewInfo, 1),
		MaxViewDistance, 
		bCaptureSceneColor,
		/* bIsPlanarReflection = */ false,
		PostProcessSettings, 
		PostProcessBlendWeight,
		ViewActor);

	// Screen percentage is still not supported in scene capture.
	ViewFamily.EngineShowFlags.ScreenPercentage = false;
	ViewFamily.SetScreenPercentageInterface(new FLegacyScreenPercentageDriver(
		ViewFamily, /* GlobalResolutionFraction = */ 1.0f, /* AllowPostProcessSettingsScreenPercentage = */ false));

	return FSceneRenderer::CreateSceneRenderer(&ViewFamily, nullptr);
}

void FScene::UpdateSceneCaptureContents(USceneCaptureComponent2D* CaptureComponent)
{
	check(CaptureComponent);

	if (UTextureRenderTarget2D* TextureRenderTarget = CaptureComponent->TextureTarget)
	{
		FTransform Transform = CaptureComponent->GetComponentToWorld();
		FVector ViewLocation = Transform.GetTranslation();

		// Remove the translation from Transform because we only need rotation.
		Transform.SetTranslation(FVector::ZeroVector);
		Transform.SetScale3D(FVector::OneVector);
		FMatrix ViewRotationMatrix = Transform.ToInverseMatrixWithScale();

		// swap axis st. x=z,y=x,z=y (unreal coord space) so that z is up
		ViewRotationMatrix = ViewRotationMatrix * FMatrix(
			FPlane(0, 0, 1, 0),
			FPlane(1, 0, 0, 0),
			FPlane(0, 1, 0, 0),
			FPlane(0, 0, 0, 1));
		const float FOV = CaptureComponent->FOVAngle * (float)PI / 360.0f;
		FIntPoint CaptureSize(TextureRenderTarget->GetSurfaceWidth(), TextureRenderTarget->GetSurfaceHeight());

		FMatrix ProjectionMatrix;
		if (CaptureComponent->bUseCustomProjectionMatrix)
		{
			ProjectionMatrix = CaptureComponent->CustomProjectionMatrix;
		}
		else
		{
			const float ClippingPlane = (CaptureComponent->bOverride_CustomNearClippingPlane) ? CaptureComponent->CustomNearClippingPlane : GNearClippingPlane;
			BuildProjectionMatrix(CaptureSize, CaptureComponent->ProjectionType, FOV, CaptureComponent->OrthoWidth, ClippingPlane, ProjectionMatrix);
		}

		const bool bUseSceneColorTexture = CaptureNeedsSceneColor(CaptureComponent->CaptureSource);

		FSceneRenderer* SceneRenderer = CreateSceneRendererForSceneCapture(
			this, 
			CaptureComponent, 
			TextureRenderTarget->GameThread_GetRenderTargetResource(), 
			CaptureSize, 
			ViewRotationMatrix, 
			ViewLocation, 
			ProjectionMatrix, 
			CaptureComponent->MaxViewDistanceOverride, 
			bUseSceneColorTexture,
			&CaptureComponent->PostProcessSettings, 
			CaptureComponent->PostProcessBlendWeight,
			CaptureComponent->GetViewOwner());

		check(SceneRenderer != nullptr);

		SceneRenderer->Views[0].bFogOnlyOnRenderedOpaque = CaptureComponent->bConsiderUnrenderedOpaquePixelAsFullyTranslucent;

		SceneRenderer->ViewFamily.SceneCaptureSource = CaptureComponent->CaptureSource;
		SceneRenderer->ViewFamily.SceneCaptureCompositeMode = CaptureComponent->CompositeMode;

		// Ensure that the views for this scene capture reflect any simulated camera motion for this frame
		TOptional<FTransform> PreviousTransform = FMotionVectorSimulation::Get().GetPreviousTransform(CaptureComponent);

		// Process Scene View extensions for the capture component
		{
			for (int32 Index = 0; Index < CaptureComponent->SceneViewExtensions.Num(); ++Index)
			{
				TSharedPtr<ISceneViewExtension, ESPMode::ThreadSafe> Extension = CaptureComponent->SceneViewExtensions[Index].Pin();
				if (Extension.IsValid())
				{
					if (Extension->IsActiveThisFrame(FSceneViewExtensionContext(SceneRenderer->Scene)))
					{
						SceneRenderer->ViewFamily.ViewExtensions.Add(Extension.ToSharedRef());
					}
				}
				else
				{
					CaptureComponent->SceneViewExtensions.RemoveAt(Index, 1, false);
					--Index;
				}
			}

			for (const TSharedRef<ISceneViewExtension, ESPMode::ThreadSafe>& Extension : SceneRenderer->ViewFamily.ViewExtensions)
			{
				Extension->SetupViewFamily(SceneRenderer->ViewFamily);
			}
		}

		{
			FPlane ClipPlane = FPlane(CaptureComponent->ClipPlaneBase, CaptureComponent->ClipPlaneNormal.GetSafeNormal());

			for (FSceneView& View : SceneRenderer->Views)
			{
				if (PreviousTransform.IsSet())
				{
					View.PreviousViewTransform = PreviousTransform.GetValue();
				}

				View.bCameraCut = CaptureComponent->bCameraCutThisFrame;

				if (CaptureComponent->bEnableClipPlane)
				{
					View.GlobalClippingPlane = ClipPlane;
					// Jitter can't be removed completely due to the clipping plane
					View.bAllowTemporalJitter = false;
				}

				for (const FSceneViewExtensionRef& Extension : SceneRenderer->ViewFamily.ViewExtensions)
				{
					Extension->SetupView(SceneRenderer->ViewFamily, View);
				}
			}
		}

		// Reset scene capture's camera cut.
		CaptureComponent->bCameraCutThisFrame = false;

		FTextureRenderTargetResource* TextureRenderTargetResource = TextureRenderTarget->GameThread_GetRenderTargetResource();

		FString EventName;
		if (!CaptureComponent->ProfilingEventName.IsEmpty())
		{
			EventName = CaptureComponent->ProfilingEventName;
		}
		else if (CaptureComponent->GetOwner())
		{
			CaptureComponent->GetOwner()->GetFName().ToString(EventName);
		}

		const bool bGenerateMips = TextureRenderTarget->bAutoGenerateMips;
		FGenerateMipsParams GenerateMipsParams{TextureRenderTarget->MipsSamplerFilter == TF_Nearest ? SF_Point : (TextureRenderTarget->MipsSamplerFilter == TF_Trilinear ? SF_Trilinear : SF_Bilinear),
			TextureRenderTarget->MipsAddressU == TA_Wrap ? AM_Wrap : (TextureRenderTarget->MipsAddressU == TA_Mirror ? AM_Mirror : AM_Clamp),
			TextureRenderTarget->MipsAddressV == TA_Wrap ? AM_Wrap : (TextureRenderTarget->MipsAddressV == TA_Mirror ? AM_Mirror : AM_Clamp)};

		const bool bDisableFlipCopyGLES = CaptureComponent->bDisableFlipCopyGLES;

		// If capturing every frame, only render to the GPUs that are actually being used
		// this frame. Otherwise we will get poor performance in AFR. We can only determine
		// this by querying the viewport back buffer on the render thread, so pass that
		// along if it exists.
		FRenderTarget* GameViewportRT = nullptr;
		if (CaptureComponent->bCaptureEveryFrame)
		{
			if (GEngine->GameViewport != nullptr)
			{
				GameViewportRT = GEngine->GameViewport->Viewport;
			}
		}

		ENQUEUE_RENDER_COMMAND(CaptureCommand)(
			[SceneRenderer, TextureRenderTargetResource, EventName, bGenerateMips, GenerateMipsParams, bDisableFlipCopyGLES, GameViewportRT](FRHICommandListImmediate& RHICmdList)
			{
				if (GameViewportRT != nullptr)
				{
					const FRHIGPUMask GPUMask = AFRUtils::GetGPUMaskForGroup(GameViewportRT->GetGPUMask(RHICmdList));
					TextureRenderTargetResource->SetActiveGPUMask(GPUMask);
				}
				else
				{
					TextureRenderTargetResource->SetActiveGPUMask(FRHIGPUMask::All());
				}
				UpdateSceneCaptureContent_RenderThread(RHICmdList, SceneRenderer, TextureRenderTargetResource, TextureRenderTargetResource, EventName, FResolveParams(), bGenerateMips, GenerateMipsParams, bDisableFlipCopyGLES);
			}
		);
	}
}

void FScene::UpdateSceneCaptureContents(USceneCaptureComponentCube* CaptureComponent)
{
	struct FLocal
	{
		/** Creates a transformation for a cubemap face, following the D3D cubemap layout. */
		static FMatrix CalcCubeFaceTransform(ECubeFace Face)
		{
			static const FVector XAxis(1.f, 0.f, 0.f);
			static const FVector YAxis(0.f, 1.f, 0.f);
			static const FVector ZAxis(0.f, 0.f, 1.f);

			// vectors we will need for our basis
			FVector vUp(YAxis);
			FVector vDir;
			switch (Face)
			{
				case CubeFace_PosX:
					vDir = XAxis;
					break;
				case CubeFace_NegX:
					vDir = -XAxis;
					break;
				case CubeFace_PosY:
					vUp = -ZAxis;
					vDir = YAxis;
					break;
				case CubeFace_NegY:
					vUp = ZAxis;
					vDir = -YAxis;
					break;
				case CubeFace_PosZ:
					vDir = ZAxis;
					break;
				case CubeFace_NegZ:
					vDir = -ZAxis;
					break;
			}
			// derive right vector
			FVector vRight(vUp ^ vDir);
			// create matrix from the 3 axes
			return FBasisVectorMatrix(vRight, vUp, vDir, FVector::ZeroVector);
		}
	} ;

	check(CaptureComponent);

	const bool bIsODS = CaptureComponent->TextureTargetLeft && CaptureComponent->TextureTargetRight && CaptureComponent->TextureTargetODS;
	const uint32 StartIndex = (bIsODS) ? 1 : 0;
	const uint32 EndIndex = (bIsODS) ? 3 : 1;
	
	UTextureRenderTargetCube* const TextureTargets[] = {
		CaptureComponent->TextureTarget, 
		CaptureComponent->TextureTargetLeft, 
		CaptureComponent->TextureTargetRight
	};

	FTransform Transform = CaptureComponent->GetComponentToWorld();
	const FVector ViewLocation = Transform.GetTranslation();

	if (CaptureComponent->bCaptureRotation)
	{
		// Remove the translation from Transform because we only need rotation.
		Transform.SetTranslation(FVector::ZeroVector);
		Transform.SetScale3D(FVector::OneVector);
	}

	for (uint32 CaptureIter = StartIndex; CaptureIter < EndIndex; ++CaptureIter)
	{
		UTextureRenderTargetCube* const TextureTarget = TextureTargets[CaptureIter];

		if (GetFeatureLevel() >= ERHIFeatureLevel::ES3_1 && TextureTarget)
		{
			const float FOV = 90 * (float)PI / 360.0f;
			for (int32 faceidx = 0; faceidx < (int32)ECubeFace::CubeFace_MAX; faceidx++)
			{
				const ECubeFace TargetFace = (ECubeFace)faceidx;
				const FVector Location = CaptureComponent->GetComponentToWorld().GetTranslation();

				FMatrix ViewRotationMatrix;

				if (CaptureComponent->bCaptureRotation)
				{
					ViewRotationMatrix = Transform.ToInverseMatrixWithScale() * FLocal::CalcCubeFaceTransform(TargetFace);
				}
				else
				{
					ViewRotationMatrix = FLocal::CalcCubeFaceTransform(TargetFace);
				}
				FIntPoint CaptureSize(TextureTarget->GetSurfaceWidth(), TextureTarget->GetSurfaceHeight());
				FMatrix ProjectionMatrix;
				BuildProjectionMatrix(CaptureSize, ECameraProjectionMode::Perspective, FOV, 1.0f, GNearClippingPlane, ProjectionMatrix);
				FPostProcessSettings PostProcessSettings;

				float StereoIPD = 0.0f;
				if (bIsODS)
				{
					StereoIPD = (CaptureIter == 1) ? CaptureComponent->IPD * -0.5f : CaptureComponent->IPD * 0.5f;
				}

				bool bCaptureSceneColor = CaptureNeedsSceneColor(CaptureComponent->CaptureSource);

				FSceneRenderer* SceneRenderer = CreateSceneRendererForSceneCapture(this, CaptureComponent, 
				    TextureTarget->GameThread_GetRenderTargetResource(), CaptureSize, ViewRotationMatrix, 
					Location, ProjectionMatrix, CaptureComponent->MaxViewDistanceOverride, 
					bCaptureSceneColor, &PostProcessSettings, 0, CaptureComponent->GetViewOwner(), StereoIPD);

				SceneRenderer->ViewFamily.SceneCaptureSource = CaptureComponent->CaptureSource;

				FTextureRenderTargetCubeResource* TextureRenderTarget = static_cast<FTextureRenderTargetCubeResource*>(TextureTarget->GameThread_GetRenderTargetResource());
				FString EventName;
				if (!CaptureComponent->ProfilingEventName.IsEmpty())
				{
					EventName = CaptureComponent->ProfilingEventName;
				}
				else if (CaptureComponent->GetOwner())
				{
					CaptureComponent->GetOwner()->GetFName().ToString(EventName);
				}
				ENQUEUE_RENDER_COMMAND(CaptureCommand)(
					[SceneRenderer, TextureRenderTarget, EventName, TargetFace](FRHICommandListImmediate& RHICmdList)
				{
					UpdateSceneCaptureContent_RenderThread(RHICmdList, SceneRenderer, TextureRenderTarget, TextureRenderTarget, EventName, FResolveParams(FResolveRect(), TargetFace), false, FGenerateMipsParams(), false);
				}
				);
			}
		}
	}

	if (bIsODS)
	{
		const FTextureRenderTargetCubeResource* const LeftEye = static_cast<FTextureRenderTargetCubeResource*>(CaptureComponent->TextureTargetLeft->GameThread_GetRenderTargetResource());
		const FTextureRenderTargetCubeResource* const RightEye = static_cast<FTextureRenderTargetCubeResource*>(CaptureComponent->TextureTargetRight->GameThread_GetRenderTargetResource());
		FTextureRenderTargetResource* const RenderTarget = CaptureComponent->TextureTargetODS->GameThread_GetRenderTargetResource();
		const ERHIFeatureLevel::Type InFeatureLevel = FeatureLevel;

		ENQUEUE_RENDER_COMMAND(ODSCaptureCommand)(
			[LeftEye, RightEye, RenderTarget, InFeatureLevel](FRHICommandListImmediate& RHICmdList)
		{
			const ERHIAccess FinalAccess = ERHIAccess::EWritable;

			FRDGBuilder GraphBuilder(RHICmdList);
			FRDGTextureRef OutputTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(RenderTarget->GetRenderTargetTexture(), TEXT("Output")));
			FRDGTextureRef LeftEyeTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(LeftEye->TextureRHI, TEXT("LeftEye")));
			FRDGTextureRef RightEyeTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(RightEye->TextureRHI, TEXT("RightEye")));
			ODSCapture_RenderThread(GraphBuilder, LeftEyeTexture, RightEyeTexture, OutputTexture, InFeatureLevel);

			GraphBuilder.SetTextureAccessFinal(LeftEyeTexture, FinalAccess);
			GraphBuilder.SetTextureAccessFinal(RightEyeTexture, FinalAccess);
			GraphBuilder.Execute();
		});
	}
}
