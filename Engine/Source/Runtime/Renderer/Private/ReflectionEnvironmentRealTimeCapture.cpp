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


extern float GReflectionCaptureNearPlane;


DECLARE_GPU_STAT(CaptureConvolveSkyEnvMap);


static TAutoConsoleVariable<int32> CVarRealTimeReflectionCaptureTimeSlicing(
	TEXT("r.RealTimeReflectionCapture.TimeSlice"), 0,
	TEXT("SkyAtmosphere components are rendered when this is not 0, otherwise ignored.\n"),
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


void FScene::AllocateAndCaptureFrameSkyEnvMap(
	FRHICommandListImmediate& RHICmdList, FSceneRenderer& SceneRenderer, FViewInfo& MainView, 
	bool bShouldRenderSkyAtmosphere, bool bShouldRenderVolumetricCloud)
{
	check(SkyLight && SkyLight->bRealTimeCaptureEnabled && !SkyLight->bHasStaticLighting);

	SCOPED_DRAW_EVENT(RHICmdList, CaptureConvolveSkyEnvMap);
	SCOPED_GPU_STAT(RHICmdList, CaptureConvolveSkyEnvMap);

	const uint32 CubeWidth = SkyLight->CaptureCubeMapResolution;
	const uint32 CubeMipCount = FMath::CeilLogTwo(CubeWidth) + 1;

	// Make a snapshot we are going to use for the 6 cubemap faces and set it up.
	FViewInfo& CubeView = *MainView.CreateSnapshot();
	CubeView.FOV = 90.0f;
	// We cannot override exposure because sky input texture are using exposure

	FViewMatrices::FMinimalInitializer SceneCubeViewInitOptions;
	SceneCubeViewInitOptions.ConstrainedViewRect = FIntRect(FIntPoint(0, 0), FIntPoint(CubeWidth, CubeWidth));

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
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
		TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable, false, 1, CubeMipCount);

	const bool bTimeSlicedRealTimeCapture = CVarRealTimeReflectionCaptureTimeSlicing.GetValueOnRenderThread() > 0;


	if (!ConvolvedSkyRenderTarget.IsValid())
	{
		GRenderTargetPool.FindFreeElement(RHICmdList, SkyCubeTexDesc, ConvolvedSkyRenderTarget, TEXT("ConvolvedSkyRenderTarget"), true, ERenderTargetTransience::NonTransient);
		GRenderTargetPool.FindFreeElement(RHICmdList, SkyCubeTexDesc, CapturedSkyRenderTarget, TEXT("CapturedSkyRenderTarget"), true, ERenderTargetTransience::NonTransient);
	}
	if (bTimeSlicedRealTimeCapture && !ProcessedSkyRenderTarget.IsValid())
	{
		GRenderTargetPool.FindFreeElement(RHICmdList, SkyCubeTexDesc, ProcessedSkyRenderTarget, TEXT("CapturedSkyRenderTarget"), true, ERenderTargetTransience::NonTransient);
	}


	auto RenderCubeFaces_SkyCloud = [&](bool bExecuteSky, bool bExectureCloud, TRefCountPtr<IPooledRenderTarget> SkyRenderTarget)
	{
		if (bShouldRenderSkyAtmosphere)
		{
			FRDGBuilder GraphBuilder(RHICmdList);// , RDG_EVENT_NAME("CaptureConvolveSkyEnvMap"));

			FSkyAtmosphereRenderSceneInfo& SkyInfo = *GetSkyAtmosphereSceneInfo();
			const FSkyAtmosphereSceneProxy& SkyAtmosphereSceneProxy = SkyInfo.GetSkyAtmosphereSceneProxy();

			FRDGTextureRef SkyCubeTexture = GraphBuilder.RegisterExternalTexture(SkyRenderTarget, TEXT("SkyRenderTarget"));
			FRDGTextureRef BlackDummy2dTex = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
			FRDGTextureRef BlackDummy3dTex = GraphBuilder.RegisterExternalTexture(GSystemTextures.VolumetricBlackDummy);

			SkyAtmosphereRenderContext SkyRC;

			// Global data constant between faces
			const FAtmosphereSetup& AtmosphereSetup = SkyAtmosphereSceneProxy.GetAtmosphereSetup();
			SkyRC.bFastSky = false;
			SkyRC.bFastAerialPerspective = false;
			SkyRC.bFastAerialPerspectiveDepthTest = false;
			SkyRC.bSecondAtmosphereLightEnabled = IsSecondAtmosphereLightEnabled();

			SkyRC.bShouldSampleOpaqueShadow = false;	// No atmospheric light shadow from opaque
			SkyRC.bUseDepthBoundTestIfPossible = false;
			SkyRC.bForceRayMarching = true;				// We do not have any valid view LUT
			SkyRC.bDisableDepthRead = true;
			SkyRC.bDisableBlending = true;

			SkyRC.TransmittanceLut = GraphBuilder.RegisterExternalTexture(SkyInfo.GetTransmittanceLutTexture());
			SkyRC.MultiScatteredLuminanceLut = GraphBuilder.RegisterExternalTexture(SkyInfo.GetMultiScatteredLuminanceLutTexture());

			CloudRenderContext CloudRC;
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

					CloudRC.MainView = &CubeView;

					CloudRC.bShouldViewRenderVolumetricRenderTarget = false;
					CloudRC.bIsReflectionRendering = true;
					CloudRC.bSkipAtmosphericLightShadowmap = true;
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
				if (MainView.bSceneHasSkyMaterial)
				{
#if 1
					// Setup a constant referential for each of the faces of the dynamic reflection capture.
					const FVector SkyViewLutReferentialForward = FVector(1.0f, 0.0f, 0.0f);
					const FVector SkyViewLutReferentialRight = FVector(0.0f, 0.0f, -1.0f);
					AtmosphereSetup.ComputeViewData(SkyLight->CapturePosition, SkyViewLutReferentialForward, SkyViewLutReferentialRight,
						CubeView.CachedViewUniformShaderParameters->SkyWorldCameraOrigin, CubeView.CachedViewUniformShaderParameters->SkyPlanetCenterAndViewHeight, 
						CubeView.CachedViewUniformShaderParameters->SkyViewLutReferential);

					CubeView.CachedViewUniformShaderParameters->SkyViewLutTexture = RealTimeReflectionCaptureSkyAtmosphereViewLutTexture->GetRenderTargetItem().ShaderResourceTexture;
#else
					// Re-use the main view SkyView LUT. Thus we need to propagate the main view Forward/Right vector to have the LUT correctly oriented.
					// This can also go wrong because AtmosphereSetup.ComputeViewData uses the SkyLight position instead of the main view.
					// Also split screen will have second player have first player view ofr the sky atmosphere.
					CubeView.CachedViewUniformShaderParameters->SkyWorldCameraOrigin = MainView.CachedViewUniformShaderParameters->SkyWorldCameraOrigin;
					CubeView.CachedViewUniformShaderParameters->SkyPlanetCenterAndViewHeight = MainView.CachedViewUniformShaderParameters->SkyPlanetCenterAndViewHeight;
					CubeView.CachedViewUniformShaderParameters->SkyViewLutReferential = MainView.CachedViewUniformShaderParameters->SkyViewLutReferential;
#endif
				}
				// Else if there is no sky material, we assume that not material is sampling the sky texture in the reflection.

				if (MainView.bSceneHasSkyMaterial || HasVolumetricCloud())
				{
					CubeView.CachedViewUniformShaderParameters->CameraAerialPerspectiveVolume = RealTimeReflectionCaptureCamera360APLutTexture->GetRenderTargetItem().ShaderResourceTexture;
				}
				// Else we do nothing as we assum the MainView one will not be used

				SkyRC.ViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*CubeView.CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);
				SkyRC.ViewMatrices = &CubeViewMatrices;

				SkyRC.SkyAtmosphereViewLutTexture = BlackDummy2dTex;
				SkyRC.SkyAtmosphereCameraAerialPerspectiveVolume = BlackDummy3dTex;

				SkyRC.Viewport = FIntRect(FIntPoint(0, 0), FIntPoint(CubeWidth, CubeWidth));
				SkyRC.bLightDiskEnabled = false;
				SkyRC.bRenderSkyPixel = true;
				SkyRC.AerialPerspectiveStartDepthInCm = 0.01f;
				SkyRC.NearClippingDistance = 0.01f;
				SkyRC.FeatureLevel = FeatureLevel;

				//SkyRC.LightShadowShaderParams0UniformBuffer = nullptr;
				//SkyRC.LightShadowShaderParams1UniformBuffer = nullptr;

				SkyRC.bShouldSampleCloudShadow = HasVolumetricCloud() && MainView.VolumetricCloudShadowMap.IsValid();
				SkyRC.VolumetricCloudShadowMap = GraphBuilder.RegisterExternalTexture(SkyRC.bShouldSampleCloudShadow ? MainView.VolumetricCloudShadowMap : GSystemTextures.BlackDummy);

				SkyRC.bShouldSampleCloudSkyAO = HasVolumetricCloud() && MainView.VolumetricCloudSkyAO.IsValid();
				SkyRC.VolumetricCloudSkyAO = GraphBuilder.RegisterExternalTexture(SkyRC.bShouldSampleCloudSkyAO ? MainView.VolumetricCloudSkyAO : GSystemTextures.BlackDummy);

				if (bExecuteSky)
				{
					if (MainView.bSceneHasSkyMaterial)
					{
						FRenderTargetParameters* RenderTargetPassParameter = GraphBuilder.AllocParameters<FRenderTargetParameters>();
						RenderTargetPassParameter->RenderTargets = SkyRC.RenderTargets;

						// We need to keep a reference to the view uniform buffer for each pass referencing it when executed later.
						TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer = SkyRC.ViewUniformBuffer;

						GraphBuilder.AddPass(
							RDG_EVENT_NAME("CaptureSkyMeshReflection"),
							RenderTargetPassParameter,
							ERDGPassFlags::Raster,
							[&MainView, ViewUniformBuffer](FRHICommandListImmediate& RHICmdList)
							{
								DrawDynamicMeshPass(MainView, RHICmdList,
									[&MainView, &ViewUniformBuffer](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
									{
										FScene* Scene = MainView.Family->Scene->GetRenderScene();

										FMeshPassProcessorRenderState DrawRenderState(ViewUniformBuffer, Scene->UniformBuffers.OpaqueBasePassUniformBuffer);
										DrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);

										FExclusiveDepthStencil::Type BasePassDepthStencilAccess_NoDepthWrite = FExclusiveDepthStencil::Type(Scene->DefaultBasePassDepthStencilAccess & ~FExclusiveDepthStencil::DepthWrite);
										SetupBasePassState(BasePassDepthStencilAccess_NoDepthWrite, false, DrawRenderState);

										FSkyPassMeshProcessor PassMeshProcessor(Scene, nullptr, DrawRenderState, DynamicMeshPassContext);
										for (int32 MeshBatchIndex = 0; MeshBatchIndex < MainView.SkyMesheBatches.Num(); ++MeshBatchIndex)
										{
											const FMeshBatch* MeshBatch = MainView.SkyMesheBatches[MeshBatchIndex].Mesh;
											const FPrimitiveSceneProxy* PrimitiveSceneProxy = MainView.SkyMesheBatches[MeshBatchIndex].Proxy;
											const FPrimitiveSceneInfo* PrimitiveSceneInfo = PrimitiveSceneProxy->GetPrimitiveSceneInfo();

											const uint64 DefaultBatchElementMask = ~0ull;
											PassMeshProcessor.AddMeshBatch(*MeshBatch, DefaultBatchElementMask, PrimitiveSceneProxy);
										}
									});
							});
					}
					else
					{
						SceneRenderer.RenderSkyAtmosphereInternal(GraphBuilder, SkyRC);
					}
				}

				if (bShouldRenderVolumetricCloud && bExectureCloud)
				{
					CloudRC.ViewUniformBuffer = SkyRC.ViewUniformBuffer;

					CloudRC.RenderTargets[0] = SkyRC.RenderTargets[0];
					//	CloudRC.RenderTargets[1] = Null target will skip export

					SetVolumeShadowingDefaultShaderParameters(CloudRC.LightShadowShaderParams0);

					CloudRC.VolumetricCloudShadowTexture = CubeView.VolumetricCloudShadowMap.IsValid() ? GraphBuilder.RegisterExternalTexture(CubeView.VolumetricCloudShadowMap) : BlackDummy2dTex;

					SceneRenderer.RenderVolumetricCloudsInternal(GraphBuilder, CloudRC);
				}

			}

			GraphBuilder.Execute();
		//	GraphBuilder.QueueTextureExtraction(SkyCubeTexture, &SkyRenderTarget); // Not needed because SkyRenderTarget is not transient
		}
		else
		{
			FRDGBuilder GraphBuilder(RHICmdList);// , RDG_EVENT_NAME("ClearSkyRenderTarget"));
			FRDGTextureRef SkyCubeTexture = GraphBuilder.RegisterExternalTexture(SkyRenderTarget, TEXT("SkyRenderTarget"));

			for (int32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
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
			}
			GraphBuilder.Execute();
			//	GraphBuilder.QueueTextureExtraction(SkyCubeTexture, &SkyRenderTarget); // Not needed because SkyRenderTarget is not transient
		}
	};



	auto RenderCubeFaces_GenCubeMips = [&](uint32 CubeMipStart, uint32 CubeMipEnd, TRefCountPtr<IPooledRenderTarget> SkyRenderTarget)
	{
		check(CubeMipStart > 0);	// Never write to mip0 as it has just been redered into

		FRDGBuilder GraphBuilder(RHICmdList);// , RDG_EVENT_NAME("GenerateMipChain"));
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
				ERDGPassFlags::Compute | ERDGPassFlags::GenerateMips,
			[PassParameters, ComputeShader, NumGroups](FRHICommandList& RHICmdList)
			{
				FComputeShaderUtils::Dispatch(RHICmdList, ComputeShader, *PassParameters, NumGroups);
			});
		}

		GraphBuilder.Execute();

		FSceneRenderTargetItem& SkyRenderTargetItem = SkyRenderTarget->GetRenderTargetItem();
		RHICmdList.CopyToResolveTarget(SkyRenderTargetItem.TargetableTexture, SkyRenderTargetItem.ShaderResourceTexture, FResolveParams());
	};



	auto RenderCubeFaces_SpecularConvolution = [&](uint32 CubeMipStart, uint32 CubeMipEnd, TRefCountPtr<IPooledRenderTarget> DstRenderTarget, TRefCountPtr<IPooledRenderTarget> SrcRenderTarget)
	{
		FRDGBuilder GraphBuilder(RHICmdList);// , RDG_EVENT_NAME("ConvolveSpecular"));
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
			PassParameters->ValidDispatchCoord = FIntPoint(MipResolution, MipResolution);
			PassParameters->SourceCubemapSampler = TStaticSamplerState<SF_Point>::GetRHI();

			PassParameters->SourceCubemapTexture = RDGSrcRenderTargetSRV;
			PassParameters->OutTextureMipColor = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(RDGDstRenderTarget, MipIndex));

			FIntVector NumGroups = FIntVector::DivideAndRoundUp(FIntVector(MipResolution, MipResolution, 1), FIntVector(FConvolveSpecularFaceCS::ThreadGroupSize, FConvolveSpecularFaceCS::ThreadGroupSize, 1));

			// The groupd size per face with padding
			PassParameters->FaceThreadGroupSize = NumGroups.X * FConvolveSpecularFaceCS::ThreadGroupSize;

			// We are going to dispatch once for all faces 
			NumGroups.X *= 6;

			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("Convolve"), ComputeShader, PassParameters, NumGroups);
		}

		GraphBuilder.Execute();

		FSceneRenderTargetItem& DstRenderTargetItem = DstRenderTarget->GetRenderTargetItem();
		RHICmdList.CopyToResolveTarget(DstRenderTargetItem.TargetableTexture, DstRenderTargetItem.ShaderResourceTexture, FResolveParams());
	};



	auto RenderCubeFaces_DiffuseIrradiance = [&](TRefCountPtr<IPooledRenderTarget> SourceCubemap)
	{
		// ComputeDiffuseIrradiance using N uniform samples
		{
			RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EGfxToCompute, SkyIrradianceEnvironmentMap.UAV);

			FRDGBuilder GraphBuilder(RHICmdList);// , RDG_EVENT_NAME("ComputeDiffuseIrradiance"));

			FRDGTextureRef SourceCubemapTexture = GraphBuilder.RegisterExternalTexture(SourceCubemap, TEXT("SourceCubemap"));
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
			GraphBuilder.Execute();

			// This buffer is now going to be read for rendering.
			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, SkyIrradianceEnvironmentMap.UAV);
		}
	};

	const uint32 LastMipLevel = CubeMipCount - 1;

	// Generate a full cube map in a single frame for the first frame.
	if (!bTimeSlicedRealTimeCapture || bRealTimeSlicedReflectionCaptureFirstFrame)
	{
		// PS4 perf number for 128x128x6 a cubemap with sky and cloud and default settings

		// 0.60ms (0.12ms for faces with the most clouds)
		RenderCubeFaces_SkyCloud(true, true, CapturedSkyRenderTarget);

		// 0.05ms
		RenderCubeFaces_GenCubeMips(1, LastMipLevel, CapturedSkyRenderTarget);

		// 0.80ms total (0.30ms for mip0, 0.20ms for mip1+2, 0.30ms for remaining mips)
		RenderCubeFaces_SpecularConvolution(0, LastMipLevel, ConvolvedSkyRenderTarget, CapturedSkyRenderTarget);

		// 0.015ms
		RenderCubeFaces_DiffuseIrradiance(ConvolvedSkyRenderTarget);

		// Reset Scene time slicing state if time slicing is disabled
		if (!bTimeSlicedRealTimeCapture)
		{
			bRealTimeSlicedReflectionCaptureFirstFrame = true;
			RealTimeSlicedReflectionCaptureState = 0;
		}
		else
		{
			bRealTimeSlicedReflectionCaptureFirstFrame = false;
		}
	}
	else
	{
		// Each frame we capture the sky and work in ProcessedSkyRenderTarget to generate the specular convolution.
		// Once done, we copy the result into ConvolvedSkyRenderTarget and generate the sky irradiance SH from there.

		if (RealTimeSlicedReflectionCaptureState == 0)
		{
			RenderCubeFaces_SkyCloud(true, false, CapturedSkyRenderTarget);
		}
		else if (RealTimeSlicedReflectionCaptureState == 1)
		{
			RenderCubeFaces_SkyCloud(false, true, CapturedSkyRenderTarget);
		}
		else if (RealTimeSlicedReflectionCaptureState == 2)
		{
			RenderCubeFaces_GenCubeMips(1, LastMipLevel, CapturedSkyRenderTarget);
		}
		else if (RealTimeSlicedReflectionCaptureState == 3)
		{
			RenderCubeFaces_SpecularConvolution(0, 0, ProcessedSkyRenderTarget, CapturedSkyRenderTarget);
		}
		else if (RealTimeSlicedReflectionCaptureState == 4)
		{
			if (LastMipLevel >= 2)
			{
				RenderCubeFaces_SpecularConvolution(1, 2, ProcessedSkyRenderTarget, CapturedSkyRenderTarget);
			}
			else if (LastMipLevel >= 1)
			{
				RenderCubeFaces_SpecularConvolution(1, 1, ProcessedSkyRenderTarget, CapturedSkyRenderTarget);
			}
		}
		else if (RealTimeSlicedReflectionCaptureState == 5)
		{
			if (LastMipLevel >= 3)
			{
				RenderCubeFaces_SpecularConvolution(3, LastMipLevel, ProcessedSkyRenderTarget, CapturedSkyRenderTarget);
			}
		}
		else if (RealTimeSlicedReflectionCaptureState == 6)
		{
			// Copy last result to the texture bound when rendering reflection. This is 0.065ms on PS4 for a 128x128x6 cubemap.
			FRHICopyTextureInfo CopyInfo;
			CopyInfo.NumMips = ProcessedSkyRenderTarget->GetDesc().NumMips;
			CopyInfo.NumSlices = 6;
			RHICmdList.CopyTexture(ProcessedSkyRenderTarget->GetRenderTargetItem().ShaderResourceTexture, ConvolvedSkyRenderTarget->GetRenderTargetItem().ShaderResourceTexture, CopyInfo);

			// Update the sky irradiance SH buffer.
			RenderCubeFaces_DiffuseIrradiance(ConvolvedSkyRenderTarget);
		}

		RealTimeSlicedReflectionCaptureState++;
		RealTimeSlicedReflectionCaptureState = RealTimeSlicedReflectionCaptureState == 7 ? 0 : RealTimeSlicedReflectionCaptureState;
	}
}


