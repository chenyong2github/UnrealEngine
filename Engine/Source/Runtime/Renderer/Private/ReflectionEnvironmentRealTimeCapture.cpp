// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Functionality for capturing and pre-filtering a sky env map in real time.
=============================================================================*/

#include "ReflectionEnvironmentCapture.h"
#include "ClearQuad.h"
#include "MeshPassProcessor.h"
#include "PrimitiveSceneProxy.h"
#include "MeshPassProcessor.inl"
#include "ScenePrivate.h"
#include "SkyPassRendering.h"
#include "RenderGraphUtils.h"
#include "VolumetricCloudRendering.h"
#include "VolumetricCloudProxy.h"
#include "FogRendering.h"
#include "GPUScene.h"

#if WITH_EDITOR
#include "CanvasTypes.h"
#include "RenderTargetTemp.h"
#endif

extern float GReflectionCaptureNearPlane;


DECLARE_GPU_STAT(CaptureConvolveSkyEnvMap);


static TAutoConsoleVariable<int32> CVarRealTimeReflectionCaptureTimeSlicing(
	TEXT("r.SkyLight.RealTimeReflectionCapture.TimeSlice"), 1,
	TEXT("When enabled, the real-time sky light capture and convolutions will by distributed over several frames to lower the per-frame cost."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRealTimeReflectionCaptureShadowFromOpaque(
	TEXT("r.SkyLight.RealTimeReflectionCapture.ShadowFromOpaque"), 0,
	TEXT("Opaque meshes cast shadow from directional lights onto sky and clouds when enabled.\n"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarRealTimeReflectionCaptureDepthBuffer(
	TEXT("r.SkyLight.RealTimeReflectionCapture.DepthBuffer"), 1,
	TEXT("When enabled, the real-time sky light capture will have a depth buffer, this is for multiple meshes to be cover each other correctly. The height fog wil lalso be applied according to the depth buffer."),
	ECVF_RenderThreadSafe);


class FDownsampleCubeFaceCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDownsampleCubeFaceCS);
	SHADER_USE_PARAMETER_STRUCT(FDownsampleCubeFaceCS, FGlobalShader);

	static const uint32 ThreadGroupSize = 8;

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, MipIndex)
		SHADER_PARAMETER(uint32, NumMips)
		SHADER_PARAMETER(int32, CubeFace)
		SHADER_PARAMETER(int32, FaceThreadGroupSize)
		SHADER_PARAMETER(FIntPoint, ValidDispatchCoord)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(TextureCube, SourceCubemapTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SourceCubemapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutTextureMipColor)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5; }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
		OutEnvironment.SetDefine(TEXT("USE_COMPUTE"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FDownsampleCubeFaceCS, "/Engine/Private/ReflectionEnvironmentShaders.usf", "DownsampleCS", SF_Compute);


class FConvolveSpecularFaceCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FConvolveSpecularFaceCS);
	SHADER_USE_PARAMETER_STRUCT(FConvolveSpecularFaceCS, FGlobalShader);

	static const uint32 ThreadGroupSize = 8;

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, MipIndex)
		SHADER_PARAMETER(uint32, NumMips)
		SHADER_PARAMETER(int32, CubeFaceOffset)
		SHADER_PARAMETER(int32, CubeFace)
		SHADER_PARAMETER(int32, FaceThreadGroupSize)
		SHADER_PARAMETER(FIntPoint, ValidDispatchCoord)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(TextureCube, SourceCubemapTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SourceCubemapSampler)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutTextureMipColor)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5; }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
		OutEnvironment.SetDefine(TEXT("USE_COMPUTE"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FConvolveSpecularFaceCS, "/Engine/Private/ReflectionEnvironmentShaders.usf", "FilterCS", SF_Compute);


class FComputeSkyEnvMapDiffuseIrradianceCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FComputeSkyEnvMapDiffuseIrradianceCS);
	SHADER_USE_PARAMETER_STRUCT(FComputeSkyEnvMapDiffuseIrradianceCS, FGlobalShader);

	// 8*8=64 threads in a group.
	// Each thread uses 4*7*RGB sh float => 84 bytes shared group memory. 
	// 64 * 84 = 5376 bytes which fits dx11 16KB shared memory limitation. 6144 with vector alignement in shared memory and it still fits
	// Low occupancy on a single CU.
	static const uint32 ThreadGroupSizeX = 8;
	static const uint32 ThreadGroupSizeY = 8;

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_SRV(TextureCube, SourceCubemapTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SourceCubemapSampler)
		SHADER_PARAMETER_UAV(RWStructuredBuffer, OutIrradianceEnvMapSH)
		SHADER_PARAMETER(float, UniformSampleSolidAngle)
		SHADER_PARAMETER(uint32, MipIndex)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5; }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_X"), ThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE_Y"), ThreadGroupSizeY);
		OutEnvironment.SetDefine(TEXT("SHADER_DIFFUSE_TO_SH"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FComputeSkyEnvMapDiffuseIrradianceCS, "/Engine/Private/ReflectionEnvironmentShaders.usf", "ComputeSkyEnvMapDiffuseIrradianceCS", SF_Compute);



class FApplyLowerHemisphereColor : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FApplyLowerHemisphereColor);
	SHADER_USE_PARAMETER_STRUCT(FApplyLowerHemisphereColor, FGlobalShader);

	static const uint32 ThreadGroupSize = 8;

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FLinearColor, LowerHemisphereSolidColor)
		SHADER_PARAMETER(FIntPoint, ValidDispatchCoord)
		SHADER_PARAMETER(int32, FaceThreadGroupSize)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, OutTextureMipColor)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5; }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), ThreadGroupSize);
		OutEnvironment.SetDefine(TEXT("USE_COMPUTE"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FApplyLowerHemisphereColor, "/Engine/Private/ReflectionEnvironmentShaders.usf", "ApplyLowerHemisphereColorCS", SF_Compute);


class FRenderRealTimeReflectionHeightFogVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRenderRealTimeReflectionHeightFogVS);
	SHADER_USE_PARAMETER_STRUCT(FRenderRealTimeReflectionHeightFogVS, FGlobalShader);;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("REALTIME_REFLECTION_HEIGHT_FOG"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FRenderRealTimeReflectionHeightFogVS, "/Engine/Private/ReflectionEnvironmentShaders.usf", "RenderRealTimeReflectionHeightFogVS", SF_Vertex);


class FRenderRealTimeReflectionHeightFogPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FRenderRealTimeReflectionHeightFogPS);
	SHADER_USE_PARAMETER_STRUCT(FRenderRealTimeReflectionHeightFogPS, FGlobalShader);

	class FDepthTexture : SHADER_PERMUTATION_BOOL("PERMUTATION_DEPTHTEXTURE");
	using FPermutationDomain = TShaderPermutationDomain<FDepthTexture>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FFogUniformParameters, FogStruct)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DepthTexture)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("REALTIME_REFLECTION_HEIGHT_FOG"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FRenderRealTimeReflectionHeightFogPS, "/Engine/Private/ReflectionEnvironmentShaders.usf", "RenderRealTimeReflectionHeightFogPS", SF_Pixel);


void FScene::ValidateSkyLightRealTimeCapture(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	FRDGTextureRef SceneColorTexture)
{
#if WITH_EDITOR
	bool bSkyMeshInMainPassExist = false;
	bool bSkyMeshInRealTimeSkyCaptureExtist = false;

	const int32 SkyRealTimeReflectionOnlyMeshBatcheCount = View.SkyMeshBatches.Num();
	for (int32 MeshBatchIndex = 0; MeshBatchIndex < SkyRealTimeReflectionOnlyMeshBatcheCount; ++MeshBatchIndex)
	{
		const FSkyMeshBatch& SkyMeshBatch = View.SkyMeshBatches[MeshBatchIndex];
		bSkyMeshInMainPassExist |= SkyMeshBatch.bVisibleInMainPass;
		bSkyMeshInRealTimeSkyCaptureExtist |= SkyMeshBatch.bVisibleInRealTimeSkyCapture;
	}

	if (!bSkyMeshInMainPassExist || !bSkyMeshInRealTimeSkyCaptureExtist)
	{
		AddDrawCanvasPass(GraphBuilder, {}, View, FScreenPassRenderTarget(SceneColorTexture, View.ViewRect, ERenderTargetLoadAction::ELoad), [this, &View, bSkyMeshInMainPassExist, bSkyMeshInRealTimeSkyCaptureExtist](FCanvas& Canvas)
		{
			FLinearColor TextColor(1.0f, 0.5f, 0.0f);

			if (View.bSceneHasSkyMaterial && !bSkyMeshInMainPassExist)
			{
				Canvas.DrawShadowedString(100.0f, 100.0f, TEXT("At least one mesh with a sky material is in the scene but none are rendered in main view."), GetStatsFont(), TextColor);
			}
			if (View.bSceneHasSkyMaterial && !bSkyMeshInRealTimeSkyCaptureExtist && SkyLight && SkyLight->bRealTimeCaptureEnabled)
			{
				Canvas.DrawShadowedString(100.0f, 110.0f, TEXT("At least one mesh with a sky material is in the scene but none are rendered in the real-time sky light reflection."), GetStatsFont(), TextColor);
			}
		});
	}
#endif
}

BEGIN_SHADER_PARAMETER_STRUCT(FCaptureSkyMeshReflectionPassParameters, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FOpaqueBasePassUniformParameters, BasePass)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()


void FScene::AllocateAndCaptureFrameSkyEnvMap(
	FRDGBuilder& GraphBuilder, FSceneRenderer& SceneRenderer, FViewInfo& MainView,
	bool bShouldRenderSkyAtmosphere, bool bShouldRenderVolumetricCloud)
{
	check(SkyLight && SkyLight->bRealTimeCaptureEnabled && !SkyLight->bHasStaticLighting);

	// Ignore viewfamilies without the Atmosphere showflag enabled as the sky capture may fail otherwise.
	if (!MainView.Family->EngineShowFlags.Atmosphere)
	{
		return;
	}

	const bool bIsNewFrame = GFrameNumberRenderThread != RealTimeSlicedReflectionCaptureFrameNumber;
	RealTimeSlicedReflectionCaptureFrameNumber = GFrameNumberRenderThread;

	RDG_EVENT_SCOPE(GraphBuilder, "CaptureConvolveSkyEnvMap");
	RDG_GPU_STAT_SCOPE(GraphBuilder, CaptureConvolveSkyEnvMap);

	const uint32 CubeWidth = SkyLight->CaptureCubeMapResolution;
	const uint32 CubeMipCount = FMath::CeilLogTwo(CubeWidth) + 1;

	// Make a snapshot we are going to use for the 6 cubemap faces and set it up.
	// Note: cube view is not meant to be sent to lambdas because we only create a single one. You should only send the ViewUniformBuffer around.
	FViewInfo& CubeView = *MainView.CreateSnapshot();
	CubeView.FOV = 90.0f;
	// Note: We cannot override exposure because sky input texture are using exposure

	// DYNAMIC PRIMITIVES - We empty the CubeView dynamic primitive list to make sure UploadDynamicPrimitiveShaderDataForViewInternal is going through the cheap fast path only updating uniform buffer.
	// This means we cannot render procedurally animated meshes into the real-time sky capture as of today.
	CubeView.DynamicPrimitiveShaderData.Empty();

	// Other view data clean up
	CubeView.StereoPass = eSSP_FULL;
	CubeView.DrawDynamicFlags = EDrawDynamicFlags::ForceLowestLOD;
	CubeView.MaterialTextureMipBias = 0;

	FViewMatrices::FMinimalInitializer SceneCubeViewInitOptions;
	SceneCubeViewInitOptions.ConstrainedViewRect = FIntRect(FIntPoint(0, 0), FIntPoint(CubeWidth, CubeWidth));

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
	FBox VolumeBounds[TVC_MAX];
	CubeView.CachedViewUniformShaderParameters = MakeUnique<FViewUniformShaderParameters>();
	CubeView.SetupUniformBufferParameters(
		SceneContext,
		VolumeBounds,
		TVC_MAX,
		*CubeView.CachedViewUniformShaderParameters);

	const FMatrix CubeProjectionMatrix = GetCubeProjectionMatrix(CubeView.FOV * 0.5f, (float)CubeWidth, GReflectionCaptureNearPlane);
	CubeView.UpdateProjectionMatrix(CubeProjectionMatrix);

	FPooledRenderTargetDesc SkyCubeTexDesc = FPooledRenderTargetDesc::CreateCubemapDesc(CubeWidth, 
		PF_FloatR11G11B10, FClearValueBinding::Black, TexCreate_TargetArraySlicesIndependently,
		TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable, false, 1, CubeMipCount, false);

	const bool bTimeSlicedRealTimeCapture = CVarRealTimeReflectionCaptureTimeSlicing.GetValueOnRenderThread() > 0;

	const bool CubeResolutionInvalidated = ConvolvedSkyRenderTargetReadyIndex < 0 || (ConvolvedSkyRenderTarget[ConvolvedSkyRenderTargetReadyIndex].IsValid() && ConvolvedSkyRenderTarget[ConvolvedSkyRenderTargetReadyIndex]->GetDesc().GetSize().X != CubeWidth);
	if (!ConvolvedSkyRenderTarget[0].IsValid() || CubeResolutionInvalidated)
	{
		// Always allocated
		GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, SkyCubeTexDesc, ConvolvedSkyRenderTarget[0], TEXT("ConvolvedSkyRenderTarget"), ERenderTargetTransience::NonTransient);
		GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, SkyCubeTexDesc, CapturedSkyRenderTarget, TEXT("CapturedSkyRenderTarget"), ERenderTargetTransience::NonTransient);
	}
	if (bTimeSlicedRealTimeCapture && (CubeResolutionInvalidated || !ConvolvedSkyRenderTarget[ConvolvedSkyRenderTargetReadyIndex].IsValid()))
	{
		// Additional allocation for time slicing
		GRenderTargetPool.FindFreeElement(GraphBuilder.RHICmdList, SkyCubeTexDesc, ConvolvedSkyRenderTarget[1], TEXT("ConvolvedSkyRenderTarget"), ERenderTargetTransience::NonTransient);
	}

	auto ClearCubeFace = [&](FRDGTextureRef SkyCubeTexture, int32 CubeFace)
	{
		FRenderTargetParameters* Parameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
		Parameters->RenderTargets[0] = FRenderTargetBinding(SkyCubeTexture, ERenderTargetLoadAction::ENoAction, 0, CubeFace);

		FLinearColor ClearColor = FLinearColor::Black;
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("ClearSkyRenderTarget"),
			Parameters,
			ERDGPassFlags::Raster,
			[Parameters, ClearColor](FRHICommandList& RHICmdList)
			{
				DrawClearQuad(RHICmdList, ClearColor);
			});
	};

	auto RenderCubeFaces_SkyCloud = [&](bool bExecuteSky, bool bExecuteCloud, TRefCountPtr<IPooledRenderTarget>& SkyRenderTarget)
	{
		FScene* Scene = MainView.Family->Scene->GetRenderScene();

		FRDGTextureRef SkyCubeTexture = GraphBuilder.RegisterExternalTexture(SkyRenderTarget, TEXT("SkyRenderTarget"));

		if (bExecuteSky || bExecuteCloud)
		{
			FRDGTextureRef BlackDummy2dTex = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
			FRDGTextureRef BlackDummy3dTex = GraphBuilder.RegisterExternalTexture(GSystemTextures.VolumetricBlackDummy);
			const bool CaptureShadowFromOpaque = CVarRealTimeReflectionCaptureShadowFromOpaque.GetValueOnRenderThread() > 0;

			FSkyAtmosphereRenderContext SkyRC;
			const FAtmosphereSetup* AtmosphereSetup = nullptr;
			if (bShouldRenderSkyAtmosphere)
			{
				FSkyAtmosphereRenderSceneInfo& SkyInfo = *GetSkyAtmosphereSceneInfo();
				const FSkyAtmosphereSceneProxy& SkyAtmosphereSceneProxy = SkyInfo.GetSkyAtmosphereSceneProxy();

				// Global data constant between faces
				AtmosphereSetup = &SkyAtmosphereSceneProxy.GetAtmosphereSetup();

				SkyRC.bFastSky = false;
				SkyRC.bFastAerialPerspective = false;
				SkyRC.bFastAerialPerspectiveDepthTest = false;
				SkyRC.bSecondAtmosphereLightEnabled = IsSecondAtmosphereLightEnabled();

				// Enable opaque shadow on sky if needed
				SkyRC.bShouldSampleOpaqueShadow = false;
				if (CaptureShadowFromOpaque)
				{
					SkyAtmosphereLightShadowData LightShadowData;
					SkyRC.bShouldSampleOpaqueShadow = ShouldSkySampleAtmosphereLightsOpaqueShadow(*Scene, SceneRenderer.VisibleLightInfos, LightShadowData);
					GetSkyAtmosphereLightsUniformBuffers(SkyRC.LightShadowShaderParams0UniformBuffer, SkyRC.LightShadowShaderParams1UniformBuffer,
						LightShadowData, CubeView, SkyRC.bShouldSampleOpaqueShadow, UniformBuffer_SingleDraw);
				}

				SkyRC.bUseDepthBoundTestIfPossible = false;
				SkyRC.bForceRayMarching = true;				// We do not have any valid view LUT
				SkyRC.bDepthReadDisabled = true;
				SkyRC.bDisableBlending = true;

				SkyRC.TransmittanceLut = GraphBuilder.RegisterExternalTexture(SkyInfo.GetTransmittanceLutTexture());
				SkyRC.MultiScatteredLuminanceLut = GraphBuilder.RegisterExternalTexture(SkyInfo.GetMultiScatteredLuminanceLutTexture());
			}

			FCloudRenderContext CloudRC;
			if (bShouldRenderVolumetricCloud)
			{
				FVolumetricCloudRenderSceneInfo& CloudInfo = *GetVolumetricCloudSceneInfo();
				FVolumetricCloudSceneProxy& CloudSceneProxy = CloudInfo.GetVolumetricCloudSceneProxy();

				if (CloudSceneProxy.GetCloudVolumeMaterial())
				{
					FMaterialRenderProxy* CloudVolumeMaterialProxy = CloudSceneProxy.GetCloudVolumeMaterial()->GetRenderProxy();
					CloudRC.CloudInfo = &CloudInfo;
					CloudRC.CloudVolumeMaterialProxy = CloudVolumeMaterialProxy;
					CloudRC.SceneDepthZ = GSystemTextures.MaxFP16Depth;

					CloudRC.MainView = &CubeView; /// This is only accessing data that is not changing between view orientation. Such data are accessed from the ViewUniformBuffer. See CubeView comment above.

					CloudRC.bShouldViewRenderVolumetricRenderTarget = false;
					CloudRC.bIsReflectionRendering = true;
					CloudRC.bIsSkyRealTimeReflectionRendering = true;
					CloudRC.bSecondAtmosphereLightEnabled = IsSecondAtmosphereLightEnabled();

					CloudRC.bSkipAtmosphericLightShadowmap = !CaptureShadowFromOpaque;
					if (CaptureShadowFromOpaque)
					{
						FLightSceneInfo* AtmosphericLight0Info = Scene->AtmosphereLights[0];
						FLightSceneProxy* AtmosphericLight0 = AtmosphericLight0Info ? AtmosphericLight0Info->Proxy : nullptr;
						const FProjectedShadowInfo* ProjectedShadowInfo0 = nullptr;
						if (AtmosphericLight0Info)
						{
							ProjectedShadowInfo0 = GetLastCascadeShadowInfo(AtmosphericLight0, SceneRenderer.VisibleLightInfos[AtmosphericLight0Info->Id]);
						}

						// Get the main view shadow info for the cloud shadows in reflection.
						if (!CloudRC.bSkipAtmosphericLightShadowmap && AtmosphericLight0 && ProjectedShadowInfo0)
						{
							SetVolumeShadowingShaderParameters(CloudRC.LightShadowShaderParams0, MainView, AtmosphericLight0Info, ProjectedShadowInfo0, INDEX_NONE);
						}
						else
						{
							SetVolumeShadowingDefaultShaderParameters(CloudRC.LightShadowShaderParams0);
						}
					}
					else
					{
						SetVolumeShadowingDefaultShaderParameters(CloudRC.LightShadowShaderParams0);
					}
				}
				else
				{
					bShouldRenderVolumetricCloud = false; // Disable cloud rendering
				}
			}


			for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
			{
				SkyRC.RenderTargets[0] = FRenderTargetBinding(SkyCubeTexture, ERenderTargetLoadAction::ENoAction, 0, CubeFace);

				const FMatrix CubeViewRotationMatrix = CalcCubeFaceViewRotationMatrix((ECubeFace)CubeFace);

				SceneCubeViewInitOptions.ViewRotationMatrix = CubeViewRotationMatrix;
				SceneCubeViewInitOptions.ViewOrigin = SkyLight->CapturePosition;
				SceneCubeViewInitOptions.ProjectionMatrix = CubeProjectionMatrix;
				FViewMatrices CubeViewMatrices = FViewMatrices(SceneCubeViewInitOptions);
				CubeView.SetupCommonViewUniformBufferParameters(
					*CubeView.CachedViewUniformShaderParameters,
					FIntPoint(CubeWidth, CubeWidth),
					1,
					FIntRect(FIntPoint(0, 0), FIntPoint(CubeWidth, CubeWidth)),
					CubeViewMatrices,
					CubeViewMatrices);

				// Notify the fact that we render a reflection, e.g. remove sun disk.
				CubeView.CachedViewUniformShaderParameters->RenderingReflectionCaptureMask = 1.0f;
				// Notify the fact that we render a reflection, e.g. use special exposure.
				CubeView.CachedViewUniformShaderParameters->RealTimeReflectionCapture = 1.0f;

				// We have rendered a sky dome with identity rotation at the SkyLight position for the capture.
				if (AtmosphereSetup)
				{
					if (MainView.bSceneHasSkyMaterial)
					{
						// Setup a constant referential for each of the faces of the dynamic reflection capture.
						// This is to have the FastSkyViewLUT match the one generated specifically for the capture point of view.
						const FVector SkyViewLutReferentialForward = FVector(1.0f, 0.0f, 0.0f);
						const FVector SkyViewLutReferentialRight = FVector(0.0f, 0.0f, -1.0f);
						AtmosphereSetup->ComputeViewData(SkyLight->CapturePosition, SkyViewLutReferentialForward, SkyViewLutReferentialRight,
							CubeView.CachedViewUniformShaderParameters->SkyWorldCameraOrigin, CubeView.CachedViewUniformShaderParameters->SkyPlanetCenterAndViewHeight,
							CubeView.CachedViewUniformShaderParameters->SkyViewLutReferential);

						CubeView.CachedViewUniformShaderParameters->SkyViewLutTexture = RealTimeReflectionCaptureSkyAtmosphereViewLutTexture->GetRenderTargetItem().ShaderResourceTexture;
					}
					else
					{
						// Else if there is no sky material, we assume that no material is sampling the FastSkyViewLUT texture in the sky light reflection (bFastSky=bFastAerialPerspective=false).
						// But, we still need to udpate the sky parameters on the view according to the sky light capture position
						const FVector SkyViewLutReferentialForward = FVector(1.0f, 0.0f, 0.0f);
						const FVector SkyViewLutReferentialRight = FVector(0.0f, 0.0f, -1.0f);
						AtmosphereSetup->ComputeViewData(SkyLight->CapturePosition, SkyViewLutReferentialForward, SkyViewLutReferentialRight,
							CubeView.CachedViewUniformShaderParameters->SkyWorldCameraOrigin, CubeView.CachedViewUniformShaderParameters->SkyPlanetCenterAndViewHeight,
							CubeView.CachedViewUniformShaderParameters->SkyViewLutReferential);
					}
				}

				if (HasSkyAtmosphere() && (MainView.bSceneHasSkyMaterial || HasVolumetricCloud()))
				{
					CubeView.CachedViewUniformShaderParameters->CameraAerialPerspectiveVolume = RealTimeReflectionCaptureCamera360APLutTexture->GetRenderTargetItem().ShaderResourceTexture;
				}
				// Else we do nothing as we assume the MainView one will not be used

				TUniformBufferRef<FViewUniformShaderParameters> CubeViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*CubeView.CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);
				CubeView.ViewUniformBuffer = CubeViewUniformBuffer;
				if (CubeView.bSceneHasSkyMaterial)
				{
					// DYNAMIC PRIMITIVES - This will hit the fast path not updating the GPU scene, but only setting the GPUSCene resources on the view uniform buffer.
					UploadDynamicPrimitiveShaderDataForView(GraphBuilder.RHICmdList, *this, CubeView);
				}

				SkyRC.ViewUniformBuffer = CubeViewUniformBuffer;
				SkyRC.ViewMatrices = &CubeViewMatrices;

				SkyRC.SkyAtmosphereViewLutTexture = BlackDummy2dTex;
				SkyRC.SkyAtmosphereCameraAerialPerspectiveVolume = BlackDummy3dTex;

				SkyRC.Viewport = FIntRect(FIntPoint(0, 0), FIntPoint(CubeWidth, CubeWidth));
				SkyRC.bLightDiskEnabled = false;
				SkyRC.bRenderSkyPixel = true;
				SkyRC.AerialPerspectiveStartDepthInCm = 0.01f;
				SkyRC.NearClippingDistance = 0.01f;
				SkyRC.FeatureLevel = FeatureLevel;

				FCloudShadowAOData CloudShadowAOData;
				GetCloudShadowAOData(GetVolumetricCloudSceneInfo(), CubeView, GraphBuilder, CloudShadowAOData);
				SkyRC.bShouldSampleCloudShadow = CloudShadowAOData.bShouldSampleCloudShadow;
				SkyRC.VolumetricCloudShadowMap[0] = CloudShadowAOData.VolumetricCloudShadowMap[0];
				SkyRC.VolumetricCloudShadowMap[1] = CloudShadowAOData.VolumetricCloudShadowMap[1];
				SkyRC.bShouldSampleCloudSkyAO = CloudShadowAOData.bShouldSampleCloudSkyAO;
				SkyRC.VolumetricCloudSkyAO = CloudShadowAOData.VolumetricCloudSkyAO;

				const bool bUseDepthBuffer = CVarRealTimeReflectionCaptureDepthBuffer.GetValueOnRenderThread() > 0;
				FRDGTextureRef CubeDepthTexture = nullptr;

				if (bExecuteSky)
				{
					if(MainView.bSceneHasSkyMaterial || bShouldRenderSkyAtmosphere)
					{
						// If there are any mesh tagged as IsSky then we render them only, otherwise we simply render the sky atmosphere itself.
						if (MainView.bSceneHasSkyMaterial)
						{
							auto* PassParameters = GraphBuilder.AllocParameters<FCaptureSkyMeshReflectionPassParameters>();
							PassParameters->RenderTargets = SkyRC.RenderTargets;
							PassParameters->BasePass = CreateOpaqueBasePassUniformBuffer(GraphBuilder, MainView, nullptr, nullptr, 0);

							// Setup the depth buffer
							if (bUseDepthBuffer)
							{
								FRDGTextureDesc CubeDepthTextureDesc = FRDGTextureDesc::Create2D(FIntPoint(CubeWidth, CubeWidth), PF_DepthStencil, SceneContext.GetDefaultDepthClear(),
									TexCreate_DepthStencilTargetable | TexCreate_ShaderResource);
								CubeDepthTexture = GraphBuilder.CreateTexture(CubeDepthTextureDesc, TEXT("CubeDepthTexture"));
								PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(CubeDepthTexture, ERenderTargetLoadAction::EClear, FExclusiveDepthStencil::DepthWrite_StencilNop);
							}

							GraphBuilder.AddPass(
								RDG_EVENT_NAME("CaptureSkyMeshReflection"),
								PassParameters,
								ERDGPassFlags::Raster,
								[&MainView, CubeViewUniformBuffer, bUseDepthBuffer](FRHICommandListImmediate& RHICmdList)
								{
									DrawDynamicMeshPass(MainView, RHICmdList,
										[&MainView, &CubeViewUniformBuffer, bUseDepthBuffer](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
										{
											FScene* Scene = MainView.Family->Scene->GetRenderScene();

											FMeshPassProcessorRenderState DrawRenderState(CubeViewUniformBuffer);
											DrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);

											FExclusiveDepthStencil::Type BasePassDepthStencilAccess_Sky = bUseDepthBuffer ? FExclusiveDepthStencil::Type(Scene->DefaultBasePassDepthStencilAccess | FExclusiveDepthStencil::DepthWrite)
												: FExclusiveDepthStencil::Type(Scene->DefaultBasePassDepthStencilAccess & ~FExclusiveDepthStencil::DepthWrite);
											SetupBasePassState(BasePassDepthStencilAccess_Sky, false, DrawRenderState);

											FSkyPassMeshProcessor PassMeshProcessor(Scene, nullptr, DrawRenderState, DynamicMeshPassContext);
											const int32 SkyRealTimeReflectionOnlyMeshBatcheCount = MainView.SkyMeshBatches.Num();
											for (int32 MeshBatchIndex = 0; MeshBatchIndex < SkyRealTimeReflectionOnlyMeshBatcheCount; ++MeshBatchIndex)
											{
												FSkyMeshBatch& SkyMeshBatch = MainView.SkyMeshBatches[MeshBatchIndex];
												if (!SkyMeshBatch.bVisibleInRealTimeSkyCapture)
												{
													continue;
												}

												const FMeshBatch* MeshBatch = SkyMeshBatch.Mesh;
												const FPrimitiveSceneProxy* PrimitiveSceneProxy = SkyMeshBatch.Proxy;
												const FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveSceneProxy->GetPrimitiveSceneInfo();

												const uint64 DefaultBatchElementMask = ~0ull;
												PassMeshProcessor.AddMeshBatch(*MeshBatch, DefaultBatchElementMask, PrimitiveSceneProxy);
											}
										});
								});
						}
						else
						{
							FSceneTextureShaderParameters SceneTextures = CreateSceneTextureShaderParameters(GraphBuilder, SceneRenderer.FeatureLevel, ESceneTextureSetupMode::SceneDepth);
							SceneRenderer.RenderSkyAtmosphereInternal(GraphBuilder, SceneTextures, SkyRC);
						}

						// Also render the height fog as part of the sky render pass when time slicing is enabled.
						if (Scene && Scene->ExponentialFogs.Num() > 0)
						{
							FRenderRealTimeReflectionHeightFogVS::FPermutationDomain VsPermutationVector;
							TShaderMapRef<FRenderRealTimeReflectionHeightFogVS> VertexShader(GetGlobalShaderMap(SkyRC.FeatureLevel), VsPermutationVector);

							FRenderRealTimeReflectionHeightFogPS::FPermutationDomain PsPermutationVector;
							PsPermutationVector.Set<FRenderRealTimeReflectionHeightFogPS::FDepthTexture>(CubeDepthTexture != nullptr);
							TShaderMapRef<FRenderRealTimeReflectionHeightFogPS> PixelShader(GetGlobalShaderMap(SkyRC.FeatureLevel), PsPermutationVector);

							FRenderRealTimeReflectionHeightFogPS::FParameters* PsPassParameters = GraphBuilder.AllocParameters<FRenderRealTimeReflectionHeightFogPS::FParameters>();
							PsPassParameters->ViewUniformBuffer = CubeViewUniformBuffer;
							PsPassParameters->RenderTargets = SkyRC.RenderTargets;
							PsPassParameters->DepthTexture = CubeDepthTexture != nullptr ? CubeDepthTexture : BlackDummy2dTex;
							PsPassParameters->FogStruct = CreateFogUniformBuffer(GraphBuilder, CubeView);

							ClearUnusedGraphResources(PixelShader, PsPassParameters);

							// Render height fog at an infinite distance since real time reflections does not have a depth buffer for now.
							// Volumetric fog is not supported in such reflections.
							GraphBuilder.AddPass(
								RDG_EVENT_NAME("DistantHeightFog"),
								PsPassParameters,
								ERDGPassFlags::Raster,
								[PsPassParameters, VertexShader, PixelShader, CubeWidth](FRHICommandList& RHICmdListLambda)
								{
									RHICmdListLambda.SetViewport(0.0f, 0.0f, 0.0f, CubeWidth, CubeWidth, 1.0f);

									FGraphicsPipelineStateInitializer GraphicsPSOInit;
									RHICmdListLambda.ApplyCachedRenderTargets(GraphicsPSOInit);

									GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();
									GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
									GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
									GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
									GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
									GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
									GraphicsPSOInit.PrimitiveType = PT_TriangleList;
									SetGraphicsPipelineState(RHICmdListLambda, GraphicsPSOInit);

									FRenderRealTimeReflectionHeightFogVS::FParameters VsPassParameters;
									VsPassParameters.ViewUniformBuffer = PsPassParameters->ViewUniformBuffer;
									SetShaderParameters(RHICmdListLambda, VertexShader, VertexShader.GetVertexShader(), VsPassParameters);
									SetShaderParameters(RHICmdListLambda, PixelShader, PixelShader.GetPixelShader(), *PsPassParameters);

									RHICmdListLambda.DrawPrimitive(0, 1, 1);
								});
						}
					}
					else
					{
						ClearCubeFace(SkyCubeTexture, CubeFace);
					}
				}

				if (bShouldRenderVolumetricCloud && bExecuteCloud)
				{
					CloudRC.ViewUniformBuffer = CubeViewUniformBuffer;

					CloudRC.RenderTargets[0] = SkyRC.RenderTargets[0];
					//	CloudRC.RenderTargets[1] = Null target will skip export

					CloudRC.VolumetricCloudShadowTexture[0] = CloudShadowAOData.VolumetricCloudShadowMap[0];
					CloudRC.VolumetricCloudShadowTexture[1] = CloudShadowAOData.VolumetricCloudShadowMap[1];

					SceneRenderer.RenderVolumetricCloudsInternal(GraphBuilder, CloudRC);
				}
			}

			// Render lower hemisphere color
			if (SkyLight->bLowerHemisphereIsSolidColor)
			{
				FApplyLowerHemisphereColor::FPermutationDomain PermutationVector;
				TShaderMapRef<FApplyLowerHemisphereColor> ComputeShader(GetGlobalShaderMap(FeatureLevel), PermutationVector);

				const uint32 MipIndex = 0;
				const uint32 Mip0Resolution = SkyCubeTexture->Desc.GetSize().X;
				FApplyLowerHemisphereColor::FParameters* PassParameters = GraphBuilder.AllocParameters<FApplyLowerHemisphereColor::FParameters>();
				PassParameters->ValidDispatchCoord = FIntPoint(Mip0Resolution, Mip0Resolution);
				PassParameters->LowerHemisphereSolidColor = SkyLight->LowerHemisphereColor;
				PassParameters->OutTextureMipColor = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SkyCubeTexture, MipIndex));

				FIntVector NumGroups = FIntVector::DivideAndRoundUp(FIntVector(Mip0Resolution, Mip0Resolution, 1), FIntVector(FApplyLowerHemisphereColor::ThreadGroupSize, FApplyLowerHemisphereColor::ThreadGroupSize, 1));

				// The groupd size per face with padding
				PassParameters->FaceThreadGroupSize = NumGroups.X * FConvolveSpecularFaceCS::ThreadGroupSize;

				// We are going to dispatch once for all faces 
				NumGroups.X *= 6;

				FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("ApplyLowerHemisphereColor"), ComputeShader, PassParameters, NumGroups);
			}
		}
		else
		{
			for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
			{
				ClearCubeFace(SkyCubeTexture, CubeFace);
			}
		}
	};

	auto RenderCubeFaces_GenCubeMips = [&](uint32 CubeMipStart, uint32 CubeMipEnd, TRefCountPtr<IPooledRenderTarget>& SkyRenderTarget)
	{
		check(CubeMipStart > 0);	// Never write to mip0 as it has just been redered into

		FRDGTextureRef SkyCubeTexture = GraphBuilder.RegisterExternalTexture(SkyRenderTarget, TEXT("SkyRenderTarget"));

		FDownsampleCubeFaceCS::FPermutationDomain PermutationVector;
		TShaderMapRef<FDownsampleCubeFaceCS> ComputeShader(GetGlobalShaderMap(FeatureLevel), PermutationVector);

		for (uint32 MipIndex = CubeMipStart; MipIndex <= CubeMipEnd; MipIndex++)
		{
			const uint32 MipResolution = 1 << (CubeMipCount - MipIndex - 1);
			FRDGTextureSRVRef SkyCubeTextureSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::CreateForMipLevel(SkyCubeTexture, MipIndex - 1)); // slice/face selection is useless so remove from CreateForMipLevel

			FDownsampleCubeFaceCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDownsampleCubeFaceCS::FParameters>();
			PassParameters->MipIndex = MipIndex;
			PassParameters->NumMips = CubeMipCount;
			PassParameters->CubeFace = 0; // unused
			PassParameters->ValidDispatchCoord = FIntPoint(MipResolution, MipResolution);
			PassParameters->SourceCubemapSampler = TStaticSamplerState<SF_Point>::GetRHI();

			PassParameters->SourceCubemapTexture = SkyCubeTextureSRV;
			PassParameters->OutTextureMipColor = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(SkyCubeTexture, MipIndex));

			FIntVector NumGroups = FIntVector::DivideAndRoundUp(FIntVector(MipResolution, MipResolution, 1), FIntVector(FDownsampleCubeFaceCS::ThreadGroupSize, FDownsampleCubeFaceCS::ThreadGroupSize, 1));

			// The groupd size per face with padding
			PassParameters->FaceThreadGroupSize = NumGroups.X * FDownsampleCubeFaceCS::ThreadGroupSize;

			// We are going to dispatch once for all faces 
			NumGroups.X *= 6;

			// Dispatch with GenerateMips: reading from a slice through SRV and writing into lower mip through UAV.
			ClearUnusedGraphResources(ComputeShader, PassParameters);
			GraphBuilder.AddPass(
				Forward<FRDGEventName>(RDG_EVENT_NAME("MipGen")),
				PassParameters,
				ERDGPassFlags::Compute,
			[PassParameters, ComputeShader, NumGroups](FRHICommandList& RHICmdList)
			{
				FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, NumGroups);
			});
		}
	};

	auto RenderCubeFaces_SpecularConvolution = [&](uint32 CubeMipStart, uint32 CubeMipEnd, uint32 FaceStart, uint32 FaceCount, TRefCountPtr<IPooledRenderTarget>& DstRenderTarget, TRefCountPtr<IPooledRenderTarget>& SrcRenderTarget)
	{
		check((FaceStart + FaceCount) <= 6);
		FRDGTextureRef RDGSrcRenderTarget = GraphBuilder.RegisterExternalTexture(SrcRenderTarget, TEXT("CapturedSkyRenderTarget"));
		FRDGTextureRef RDGDstRenderTarget = GraphBuilder.RegisterExternalTexture(DstRenderTarget, TEXT("CapturedSkyRenderTarget"));

		FRDGTextureSRVRef RDGSrcRenderTargetSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(RDGSrcRenderTarget));

		FDownsampleCubeFaceCS::FPermutationDomain PermutationVector;
		TShaderMapRef<FConvolveSpecularFaceCS> ComputeShader(GetGlobalShaderMap(FeatureLevel), PermutationVector);
		for (uint32 MipIndex = CubeMipStart; MipIndex <= CubeMipEnd; MipIndex++)
		{
			const uint32 MipResolution = 1 << (CubeMipCount - MipIndex - 1);

			FConvolveSpecularFaceCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FConvolveSpecularFaceCS::FParameters>();
			PassParameters->MipIndex = MipIndex;
			PassParameters->NumMips = CubeMipCount;
			PassParameters->CubeFace = 0; // unused
			PassParameters->CubeFaceOffset = int(FaceStart);
			PassParameters->ValidDispatchCoord = FIntPoint(MipResolution, MipResolution);
			PassParameters->SourceCubemapSampler = TStaticSamplerState<SF_Point>::GetRHI();

			PassParameters->SourceCubemapTexture = RDGSrcRenderTargetSRV;
			PassParameters->OutTextureMipColor = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RDGDstRenderTarget, MipIndex));

			FIntVector NumGroups = FIntVector::DivideAndRoundUp(FIntVector(MipResolution, MipResolution, 1), FIntVector(FConvolveSpecularFaceCS::ThreadGroupSize, FConvolveSpecularFaceCS::ThreadGroupSize, 1));

			// The groupd size per face with padding
			PassParameters->FaceThreadGroupSize = NumGroups.X * FConvolveSpecularFaceCS::ThreadGroupSize;

			// We are going to dispatch once for all faces 
			NumGroups.X *= FaceCount;

			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Convolve"), ComputeShader, PassParameters, NumGroups);
		}
	};

	auto RenderCubeFaces_DiffuseIrradiance = [&](TRefCountPtr<IPooledRenderTarget>& SourceCubemap)
	{
		// ComputeDiffuseIrradiance using N uniform samples
		AddPass(GraphBuilder, [SkyIrradianceEnvironmentMap = SkyIrradianceEnvironmentMap.Buffer, SkyIrradianceEnvironmentMapUAV = SkyIrradianceEnvironmentMap.UAV](FRHIComputeCommandList& RHICmdList)
		{
			RHICmdList.Transition(FRHITransitionInfo(SkyIrradianceEnvironmentMapUAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
		});

		FRDGTextureRef SourceCubemapTexture = GraphBuilder.RegisterExternalTexture(SourceCubemap);
		FRDGTextureSRVRef SourceCubemapTextureSRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(SourceCubemapTexture));

		TShaderMapRef<FComputeSkyEnvMapDiffuseIrradianceCS> ComputeShader(GetGlobalShaderMap(FeatureLevel));

		const float SampleCount = FComputeSkyEnvMapDiffuseIrradianceCS::ThreadGroupSizeX * FComputeSkyEnvMapDiffuseIrradianceCS::ThreadGroupSizeY;
		const float UniformSampleSolidAngle = 4.0f * PI / SampleCount; // uniform distribution

		FComputeSkyEnvMapDiffuseIrradianceCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FComputeSkyEnvMapDiffuseIrradianceCS::FParameters>();
		PassParameters->SourceCubemapSampler = TStaticSamplerState<SF_Point>::GetRHI();
		PassParameters->SourceCubemapTexture = SourceCubemapTextureSRV;
		PassParameters->OutIrradianceEnvMapSH = SkyIrradianceEnvironmentMap.UAV;
		PassParameters->UniformSampleSolidAngle = UniformSampleSolidAngle;

		// For 64 uniform samples on the unit sphere, we roughly have 10 samples per face.
		// Considering mip generation and bilinear sampling, we can assume 10 samples is enough to integrate 10*4=40 texels.
		// With that, we target integration of 16*16 face.
		const uint32 Log2_16 = 4; // FMath::Log2(16.0f)
		PassParameters->MipIndex = uint32(FMath::Log2(float(CapturedSkyRenderTarget->GetDesc().GetSize().X))) - Log2_16;

		const FIntVector NumGroups = FIntVector(1, 1, 1);
		FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("ComputeSkyEnvMapDiffuseIrradianceCS"), ComputeShader, PassParameters, NumGroups);

		AddPass(GraphBuilder, [SkyIrradianceEnvironmentMap = SkyIrradianceEnvironmentMap.Buffer, SkyIrradianceEnvironmentMapUAV = SkyIrradianceEnvironmentMap.UAV](FRHICommandList& RHICmdList)
		{
			RHICmdList.Transition(FRHITransitionInfo(SkyIrradianceEnvironmentMapUAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
		});
	};

	const uint32 LastMipLevel = CubeMipCount - 1;

	// Ensure the main view got the full cubemap by running all the capture operations for the first frame.
	// This ensures a proper initial state when time-slicing the steps.

	// Update the firt frame detection state variable
	if (bTimeSlicedRealTimeCapture)
	{
		// Go to next state iff this is a new frame
		if (bIsNewFrame)
		{
			switch (RealTimeSlicedReflectionCaptureFirstFrameState)
			{
				case ERealTimeSlicedReflectionCaptureFirstFrameState::INIT:
					RealTimeSlicedReflectionCaptureFirstFrameState = ERealTimeSlicedReflectionCaptureFirstFrameState::FIRST_FRAME;
					break;

				case ERealTimeSlicedReflectionCaptureFirstFrameState::FIRST_FRAME:
					RealTimeSlicedReflectionCaptureFirstFrameState = ERealTimeSlicedReflectionCaptureFirstFrameState::BEYOND_FIRST_FRAME;
					break;

				default:
					break;
			}
		}
	}
	else
	{
		// Reset the time-slicing first frame detection state when not time-slicing.
		RealTimeSlicedReflectionCaptureFirstFrameState = ERealTimeSlicedReflectionCaptureFirstFrameState::INIT;
	}

	if (!bTimeSlicedRealTimeCapture 
		|| (RealTimeSlicedReflectionCaptureFirstFrameState < ERealTimeSlicedReflectionCaptureFirstFrameState::BEYOND_FIRST_FRAME))
	{
		// Generate a full cube map in a single frame for the first frame.
		// Perf number are for a 128x128x6 a cubemap on PS4 with sky and cloud and default settings

		// Since it is entirely generated each frame when time slicing is not enabled, we always use cubemap index 0 always allocated above
		ConvolvedSkyRenderTargetReadyIndex = 0;

		// 0.60ms (0.12ms for faces with the most clouds)
		RenderCubeFaces_SkyCloud(true, true, CapturedSkyRenderTarget);

		// 0.05ms
		RenderCubeFaces_GenCubeMips(1, LastMipLevel, CapturedSkyRenderTarget);

		// 0.80ms total (0.30ms for mip0, 0.20ms for mip1+2, 0.30ms for remaining mips)
		RenderCubeFaces_SpecularConvolution(0, LastMipLevel, 0, 6, ConvolvedSkyRenderTarget[ConvolvedSkyRenderTargetReadyIndex], CapturedSkyRenderTarget);

		// 0.015ms
		RenderCubeFaces_DiffuseIrradiance(ConvolvedSkyRenderTarget[ConvolvedSkyRenderTargetReadyIndex]);

		// Reset Scene time slicing state so that it starts from the beginning if/when we get out of non-time-sliced.
		RealTimeSlicedReflectionCaptureState = -1; // Value of -1 indicates this is the first time-sliced iteration.

		// The sky just changed, so invalidate these textures, so that the path tracer can rebuild them
		PathTracingSkylightTexture.SafeRelease();
		PathTracingSkylightPdf.SafeRelease();
	}
	else
	{
		// Each frame we capture the sky and work in ProcessedSkyRenderTarget to generate the specular convolution.
		// Once done, we copy the result into ConvolvedSkyRenderTarget and generate the sky irradiance SH from there.

		// On the first frame, we always fully initialise the convolution so ConvolvedSkyRenderTargetReadyIndex should already be valid.
		check(ConvolvedSkyRenderTargetReadyIndex >= 0 && ConvolvedSkyRenderTargetReadyIndex <= 1);
		const int32 ConvolvedSkyRenderTargetWorkIndex = 1 - ConvolvedSkyRenderTargetReadyIndex;
		const int32 TimeSliceCount = 12;

		// Update the current time-slicing state if this is a new frame
		// Note: RealTimeSlicedReflectionCaptureState will initially be -1.
		if (bIsNewFrame)
		{
			if (++RealTimeSlicedReflectionCaptureState >= TimeSliceCount)
			{
				RealTimeSlicedReflectionCaptureState = 0;
			}
		}

#define DEBUG_TIME_SLICE 0
#if DEBUG_TIME_SLICE
		RealTimeSlicedReflectionCaptureState = 0;
		for(int i=0; i<TimeSliceCount; ++i)
		{
#endif 

		if (RealTimeSlicedReflectionCaptureState <= 0)
		{
			RDG_EVENT_SCOPE(GraphBuilder, "RenderSky");
			RenderCubeFaces_SkyCloud(true, false, CapturedSkyRenderTarget);
		}
		else if (RealTimeSlicedReflectionCaptureState == 1)
		{
			RDG_EVENT_SCOPE(GraphBuilder, "RenderCloud");
			RenderCubeFaces_SkyCloud(false, true, CapturedSkyRenderTarget);
		}
		else if (RealTimeSlicedReflectionCaptureState == 2)
		{
			RDG_EVENT_SCOPE(GraphBuilder, "GenCubeMips");
			RenderCubeFaces_GenCubeMips(1, LastMipLevel, CapturedSkyRenderTarget);
		}
		else if (RealTimeSlicedReflectionCaptureState == 3)
		{
			RDG_EVENT_SCOPE(GraphBuilder, "ConvolutionMip0Face01");
			RenderCubeFaces_SpecularConvolution(0, 0, 0, 2, ConvolvedSkyRenderTarget[ConvolvedSkyRenderTargetWorkIndex], CapturedSkyRenderTarget); // convolution of mip0, face 0, 1
		}
		else if (RealTimeSlicedReflectionCaptureState == 4)
		{
			RDG_EVENT_SCOPE(GraphBuilder, "ConvolutionMip0Face23");
			RenderCubeFaces_SpecularConvolution(0, 0, 2, 2, ConvolvedSkyRenderTarget[ConvolvedSkyRenderTargetWorkIndex], CapturedSkyRenderTarget); // convolution of mip0, face 2, 3
		}
		else if (RealTimeSlicedReflectionCaptureState == 5)
		{
			RDG_EVENT_SCOPE(GraphBuilder, "ConvolutionMip0Face45");
			RenderCubeFaces_SpecularConvolution(0, 0, 4, 2, ConvolvedSkyRenderTarget[ConvolvedSkyRenderTargetWorkIndex], CapturedSkyRenderTarget); // convolution of mip0, face 4, 5
		}
		else if (RealTimeSlicedReflectionCaptureState == 6)
		{
			if (LastMipLevel >= 1)
			{
				RDG_EVENT_SCOPE(GraphBuilder, "ConvolutionMip1");
				RenderCubeFaces_SpecularConvolution(1, 1, 0, 6, ConvolvedSkyRenderTarget[ConvolvedSkyRenderTargetWorkIndex], CapturedSkyRenderTarget);
			}
		}
		else if (RealTimeSlicedReflectionCaptureState == 7)
		{
			if (LastMipLevel >= 2)
			{
				RDG_EVENT_SCOPE(GraphBuilder, "ConvolutionMip2");
				RenderCubeFaces_SpecularConvolution(2, 2, 0, 6, ConvolvedSkyRenderTarget[ConvolvedSkyRenderTargetWorkIndex], CapturedSkyRenderTarget);
			}
		}
		else if (RealTimeSlicedReflectionCaptureState == 8)
		{
			if (LastMipLevel >= 3)
			{
				RDG_EVENT_SCOPE(GraphBuilder, "ConvolutionMip3");
				RenderCubeFaces_SpecularConvolution(3, 3, 0, 6, ConvolvedSkyRenderTarget[ConvolvedSkyRenderTargetWorkIndex], CapturedSkyRenderTarget);
			}
		}
		else if (RealTimeSlicedReflectionCaptureState == 9)
		{
			if (LastMipLevel >= 5)
			{
				RDG_EVENT_SCOPE(GraphBuilder, "ConvolutionMip45");
				RenderCubeFaces_SpecularConvolution(4, 5, 0, 6, ConvolvedSkyRenderTarget[ConvolvedSkyRenderTargetWorkIndex], CapturedSkyRenderTarget);
			}
			else if (LastMipLevel >= 4)
			{
				RDG_EVENT_SCOPE(GraphBuilder, "ConvolutionMip4");
				RenderCubeFaces_SpecularConvolution(4, 4, 0, 6, ConvolvedSkyRenderTarget[ConvolvedSkyRenderTargetWorkIndex], CapturedSkyRenderTarget);
			}
		}
		else if (RealTimeSlicedReflectionCaptureState == 10)
		{
			if (LastMipLevel >= 6)
			{
				RDG_EVENT_SCOPE(GraphBuilder, "ConvolutionMip6Etc");
				RenderCubeFaces_SpecularConvolution(6, LastMipLevel, 0, 6, ConvolvedSkyRenderTarget[ConvolvedSkyRenderTargetWorkIndex], CapturedSkyRenderTarget);
			}
		}
		else if (RealTimeSlicedReflectionCaptureState == 11)
		{
			RDG_EVENT_SCOPE(GraphBuilder, "DiffuseIrradiance");

			// Update the sky irradiance SH buffer.
			RenderCubeFaces_DiffuseIrradiance(ConvolvedSkyRenderTarget[ConvolvedSkyRenderTargetWorkIndex]);

			// Now use the new cubemap
			ConvolvedSkyRenderTargetReadyIndex = ConvolvedSkyRenderTargetWorkIndex;

			// The sky just changed, so invalidate these textures, so that the path tracer can rebuild them
			PathTracingSkylightTexture.SafeRelease();
			PathTracingSkylightPdf.SafeRelease();
		}

#if DEBUG_TIME_SLICE
		}
#endif
	}

	if (ConvolvedSkyRenderTarget[ConvolvedSkyRenderTargetReadyIndex])
	{
		ConvertToUntrackedTexture(GraphBuilder, GraphBuilder.RegisterExternalTexture(ConvolvedSkyRenderTarget[ConvolvedSkyRenderTargetReadyIndex]), ERHIAccess::SRVMask);
	}
}


