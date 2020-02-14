// Copyright Epic Games, Inc. All Rights Reserved.

#include "AppleARKitVideoOverlay.h"
#include "AppleARKitFrame.h"
#include "AppleARKitSystem.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MaterialShaderType.h"
#include "MaterialShader.h"
#include "ExternalTexture.h"
#include "Misc/Guid.h"
#include "ExternalTextureGuid.h"
#include "Containers/ResourceArray.h"
#include "MediaShaders.h"
#include "PipelineStateCache.h"
#include "RHIUtilities.h"
#include "RHIStaticStates.h"
#include "EngineModule.h"
#include "SceneUtils.h"
#include "RendererInterface.h"
#include "ScreenRendering.h"
#include "Containers/DynamicRHIResourceArray.h"
#include "PostProcess/SceneFilterRendering.h"
#include "PostProcess/PostProcessMaterial.h"
#include "PostProcessParameters.h"
#include "CommonRenderResources.h"
#if SUPPORTS_ARKIT_1_0
	#include "IOSAppDelegate.h"
#endif

DECLARE_FLOAT_COUNTER_STAT(TEXT("ARKit Frame to Texture Delay (ms)"), STAT_ARKitFrameToTextureDelay, STATGROUP_ARKIT);
DECLARE_FLOAT_COUNTER_STAT(TEXT("ARKit Frame to Render Delay (ms)"), STAT_ARKitFrameToRenderDelay, STATGROUP_ARKIT);



DECLARE_CYCLE_STAT(TEXT("Update Occlusion Textures"), STAT_UpdateOcclusionTextures, STATGROUP_ARKIT);

#if MATERIAL_CAMERAIMAGE_CONVERSION
const FString UARKitCameraOverlayMaterialLoader::OverlayMaterialPath(TEXT("/AppleARKit/ARKitCameraMaterial.ARKitCameraMaterial"));
#else
const FString UARKitCameraOverlayMaterialLoader::OverlayMaterialPath(TEXT("/AppleARKit/M_CameraOverlay.M_CameraOverlay"));
#endif

const FString UARKitCameraOverlayMaterialLoader::DepthOcclusionOverlayMaterialPath(TEXT("/AppleARKit/MI_DepthOcclusionOverlay.MI_DepthOcclusionOverlay"));
const FString UARKitCameraOverlayMaterialLoader::MatteOcclusionOverlayMaterialPath(TEXT("/AppleARKit/MI_MatteOcclusionOverlay.MI_MatteOcclusionOverlay"));

#if SUPPORTS_ARKIT_1_0 && MATERIAL_CAMERAIMAGE_CONVERSION
/**
* Passes a CVMetalTextureRef through to the RHI to wrap in an RHI texture without traversing system memory.
* @see FAvfTexture2DResourceWrapper & FMetalSurface::FMetalSurface
*/
class FAppleARKitCameraTextureResourceWrapper : public FResourceBulkDataInterface
{
public:
	FAppleARKitCameraTextureResourceWrapper(CFTypeRef InImageBuffer)
		: ImageBuffer(InImageBuffer)
	{
		check(ImageBuffer);
		CFRetain(ImageBuffer);
	}

	/**
	* @return ptr to the resource memory which has been preallocated
	*/
	virtual const void* GetResourceBulkData() const override
	{
		return ImageBuffer;
	}

	/**
	* @return size of resource memory
	*/
	virtual uint32 GetResourceBulkDataSize() const override
	{
		return 0;
	}

	/**
	* @return the type of bulk data for special handling
	*/
	virtual EBulkDataType GetResourceType() const override
	{
		return EBulkDataType::MediaTexture;
	}

	/**
	* Free memory after it has been used to initialize RHI resource
	*/
	virtual void Discard() override
	{
		delete this;
	}

	virtual ~FAppleARKitCameraTextureResourceWrapper()
	{
		CFRelease(ImageBuffer);
		ImageBuffer = nullptr;
	}

	CFTypeRef ImageBuffer;
};
#endif

FAppleARKitVideoOverlay::FAppleARKitVideoOverlay()
	: MID_CameraOverlay(nullptr)
{
	UMaterialInterface* LoadedMaterial = LoadObject<UMaterialInterface>(nullptr, *UARKitCameraOverlayMaterialLoader::OverlayMaterialPath);
	check(LoadedMaterial != nullptr);
	// Create a MaterialInstanceDynamic which we'll replace the texture with the camera texture from ARKit
	MID_CameraOverlay = UMaterialInstanceDynamic::Create(LoadedMaterial, GetTransientPackage());
	check(MID_CameraOverlay != nullptr);
	
	UMaterialInterface* DepthOcclusionMaterial = LoadObject<UMaterialInterface>(nullptr, *UARKitCameraOverlayMaterialLoader::DepthOcclusionOverlayMaterialPath);
	check(DepthOcclusionMaterial);
	MID_DepthOcclusionOverlay = UMaterialInstanceDynamic::Create(DepthOcclusionMaterial, GetTransientPackage());
	
	UMaterialInterface* MatteOcclusionMaterial = LoadObject<UMaterialInterface>(nullptr, *UARKitCameraOverlayMaterialLoader::MatteOcclusionOverlayMaterialPath);
	check(MatteOcclusionMaterial);
	MID_MatteOcclusionOverlay = UMaterialInstanceDynamic::Create(MatteOcclusionMaterial, GetTransientPackage());
}

FAppleARKitVideoOverlay::~FAppleARKitVideoOverlay()
{
#if SUPPORTS_ARKIT_3_0
	if (CommandQueue)
	{
		[CommandQueue release];
		CommandQueue = nullptr;
	}
	
	if (MatteGenerator)
	{
		[MatteGenerator release];
		MatteGenerator = nullptr;
	}
#endif
}

template <bool bIsMobileRenderer>
class TPostProcessMaterialShader : public FMaterialShader
{
public:
	using FParameters = FPostProcessMaterialParameters;
	SHADER_USE_PARAMETER_STRUCT_WITH_LEGACY_BASE(TPostProcessMaterialShader, FMaterialShader);

	static bool ShouldCompilePermutation(const FMaterialShaderPermutationParameters& Parameters)
	{
		if (bIsMobileRenderer)
		{
			return Parameters.MaterialParameters.MaterialDomain == MD_PostProcess && IsMobilePlatform(Parameters.Platform);
		}
		else
		{
			return Parameters.MaterialParameters.MaterialDomain == MD_PostProcess && !IsMobilePlatform(Parameters.Platform);
		}
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL"), 1);
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL_MOBILE"), bIsMobileRenderer);
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL_BEFORE_TONEMAP"), (Parameters.MaterialParameters.BlendableLocation != BL_AfterTonemapping) ? 1 : 0);
	}
};

// We use something similar to the PostProcessMaterial to render the color camera overlay.
template <bool bIsMobileRenderer>
class TARKitCameraOverlayVS : public TPostProcessMaterialShader<bIsMobileRenderer>
{
public:
	using FMaterialShader::FPermutationDomain;
	DECLARE_SHADER_TYPE(TARKitCameraOverlayVS, Material);

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		TPostProcessMaterialShader<bIsMobileRenderer>::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_AR_PASSTHROUGH"), 1);
	}

	TARKitCameraOverlayVS() = default;
	TARKitCameraOverlayVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: TPostProcessMaterialShader<bIsMobileRenderer>(Initializer)
	{}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView View)
	{
		FRHIVertexShader* ShaderRHI = RHICmdList.GetBoundVertexShader();
		TPostProcessMaterialShader<bIsMobileRenderer>::SetViewParameters(RHICmdList, ShaderRHI, View, View.ViewUniformBuffer);
	}
};

using FARKitCameraOverlayVS = TARKitCameraOverlayVS<false>;
using FARKitCameraOverlayMobileVS = TARKitCameraOverlayVS<true>;

IMPLEMENT_SHADER_TYPE(template<>, FARKitCameraOverlayVS, TEXT("/Engine/Private/PostProcessMaterialShaders.usf"), TEXT("MainVS_VideoOverlay"), SF_Vertex);
IMPLEMENT_SHADER_TYPE(template<>, FARKitCameraOverlayMobileVS, TEXT("/Engine/Private/PostProcessMaterialShaders.usf"), TEXT("MainVS"), SF_Vertex);

template <bool bIsMobileRenderer>
class TARKitCameraOverlayPS : public TPostProcessMaterialShader<bIsMobileRenderer>
{
public:
	using FMaterialShader::FPermutationDomain;
	DECLARE_SHADER_TYPE(TARKitCameraOverlayPS, Material);

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		TPostProcessMaterialShader<bIsMobileRenderer>::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("OUTPUT_MOBILE_HDR"), IsMobileHDR() ? 1 : 0);
	}

	TARKitCameraOverlayPS() = default;
	TARKitCameraOverlayPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: TPostProcessMaterialShader<bIsMobileRenderer>(Initializer)
	{}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView View, const FMaterialRenderProxy* Material)
	{
		FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();
		TPostProcessMaterialShader<bIsMobileRenderer>::SetParameters(RHICmdList, ShaderRHI, Material, *Material->GetMaterial(View.GetFeatureLevel()), View, View.ViewUniformBuffer, ESceneTextureSetupMode::None);
	}
};

using FARKitCameraOverlayPS = TARKitCameraOverlayPS<false>;
using FARKitCameraOverlayMobilePS = TARKitCameraOverlayPS<true>;

IMPLEMENT_SHADER_TYPE(template<>, FARKitCameraOverlayPS, TEXT("/Engine/Private/PostProcessMaterialShaders.usf"), TEXT("MainPS_VideoOverlay"), SF_Pixel);
IMPLEMENT_SHADER_TYPE(template<>, FARKitCameraOverlayMobilePS, TEXT("/Engine/Private/PostProcessMaterialShaders.usf"), TEXT("MainPS"), SF_Pixel);

void FAppleARKitVideoOverlay::UpdateVideoTextures(const FAppleARKitFrame& Frame)
{
#if SUPPORTS_ARKIT_1_0 && MATERIAL_CAMERAIMAGE_CONVERSION
	if (FAppleARKitAvailability::SupportsARKit10())
	{
		check(IsMetalPlatform(GMaxRHIShaderPlatform));
		
		if (Frame.CapturedYImage && Frame.CapturedCbCrImage)
		{
			FRHIResourceCreateInfo CreateInfo;
			const uint32 CreateFlags = TexCreate_Dynamic | TexCreate_NoTiling | TexCreate_ShaderResource;
			CreateInfo.BulkData = new FAppleARKitCameraTextureResourceWrapper(Frame.CapturedYImage);
			CreateInfo.ResourceArray = nullptr;

			// pull the Y and CbCr textures out of the captured image planes (format is fake here, it will get the format from the FAppleARKitCameraTextureResourceWrapper)
			VideoTextureY = RHICreateTexture2D(Frame.CapturedYImageWidth, Frame.CapturedYImageHeight, /*Format=*/PF_B8G8R8A8, /*NumMips=*/1, /*NumSamples=*/1, CreateFlags, CreateInfo);

			CreateInfo.BulkData = new FAppleARKitCameraTextureResourceWrapper(Frame.CapturedCbCrImage);
			VideoTextureCbCr = RHICreateTexture2D(Frame.CapturedCbCrImageWidth, Frame.CapturedCbCrImageHeight, /*Format=*/PF_B8G8R8A8, /*NumMips=*/1, /*NumSamples=*/1, CreateFlags, CreateInfo);

			// todo: Add an update call to the registry instead of this unregister/re-register
			FExternalTextureRegistry::Get().UnregisterExternalTexture(ARKitPassthroughCameraExternalTextureYGuid);
			FExternalTextureRegistry::Get().UnregisterExternalTexture(ARKitPassthroughCameraExternalTextureCbCrGuid);

			FSamplerStateInitializerRHI SamplerStateInitializer(SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap);
			FSamplerStateRHIRef SamplerStateRHI = RHICreateSamplerState(SamplerStateInitializer);

			FExternalTextureRegistry::Get().RegisterExternalTexture(ARKitPassthroughCameraExternalTextureYGuid, VideoTextureY, SamplerStateRHI);
			FExternalTextureRegistry::Get().RegisterExternalTexture(ARKitPassthroughCameraExternalTextureCbCrGuid, VideoTextureCbCr, SamplerStateRHI);
			
			//Make sure AR camera pass through materials are updated properly
			FMaterialRenderProxy::UpdateDeferredCachedUniformExpressions();

			//CFRelease(Frame.CapturedYImage);
			//CFRelease(Frame.CapturedCbCrImage);
			//Frame.CapturedYImage = nullptr;
			//Frame.CapturedCbCrImage = nullptr;
		}
	}
#endif
}

void FAppleARKitVideoOverlay::UpdateOcclusionTextures(const FAppleARKitFrame& Frame)
{
#if SUPPORTS_ARKIT_3_0
	SCOPE_CYCLE_COUNTER(STAT_UpdateOcclusionTextures);
	
	if (FAppleARKitAvailability::SupportsARKit30())
	{
		ARFrame* NativeFrame = (ARFrame*)Frame.NativeFrame;
		if (bEnablePersonOcclusion && NativeFrame &&
			(NativeFrame.segmentationBuffer || NativeFrame.estimatedDepthData))
		{
			id<MTLDevice> Device = (id<MTLDevice>)GDynamicRHI->RHIGetNativeDevice();
			check(Device);

			if (!MatteGenerator)
			{
				// TODO: add config variable for the matte resolution
				MatteGenerator = [[ARMatteGenerator alloc] initWithDevice: Device
														  matteResolution: ARMatteResolutionFull];
			}

			if (!CommandQueue)
			{
				CommandQueue = [Device newCommandQueue];
			}

			id<MTLCommandBuffer> CommandBuffer = [CommandQueue commandBuffer];
			id<MTLTexture> MatteTexture = nullptr;
			id<MTLTexture> DepthTexture = nullptr;
			
			MatteTexture = [MatteGenerator generateMatteFromFrame: NativeFrame commandBuffer: CommandBuffer];
			DepthTexture = [MatteGenerator generateDilatedDepthFromFrame: NativeFrame commandBuffer: CommandBuffer];
			
			if (MatteTexture && OcclusionMatteTexture)
			{
				OcclusionMatteTexture->SetMetalTexture(Frame.Timestamp, MatteTexture);
			}

			if (DepthTexture && OcclusionDepthTexture)
			{
				OcclusionDepthTexture->SetMetalTexture(Frame.Timestamp, DepthTexture);
				bOcclusionDepthTextureRecentlyUpdated = true;
			}

			[CommandBuffer commit];
		}
	}
#endif
}

void FAppleARKitVideoOverlay::RenderVideoOverlayWithMaterial(FRHICommandListImmediate& RHICmdList, const FSceneView& InView, struct FAppleARKitFrame& Frame, const EDeviceScreenOrientation DeviceOrientation, UMaterialInstanceDynamic* RenderingOverlayMaterial, const bool bRenderingOcclusion, const bool bRespectAllOrientations)
{
	if (RenderingOverlayMaterial == nullptr || !RenderingOverlayMaterial->IsValidLowLevel())
	{
		return;
	}

#if STATS && PLATFORM_IOS
	// LastUpdateTimestamp has been removed
	//const auto DelayMS = ([[NSProcessInfo processInfo] systemUptime] - LastUpdateTimestamp) * 1000.0;
	//SET_FLOAT_STAT(STAT_ARKitFrameToRenderDelay, DelayMS);
#endif
	
	SCOPED_DRAW_EVENTF(RHICmdList, RenderVideoOverlay, bRenderingOcclusion ? TEXT("VideoOverlay (Occlusion)") : TEXT("VideoOverlay (Background)"));
	
	if (OverlayVertexBufferRHITwoOrientations[0] == nullptr || !OverlayVertexBufferRHITwoOrientations[0].IsValid())
	{
		const FVector2D ViewSize(InView.UnconstrainedViewRect.Max.X, InView.UnconstrainedViewRect.Max.Y);

		FVector2D CameraSize = Frame.Camera.ImageResolution;
		if ((ViewSize.X > ViewSize.Y) != (CameraSize.X > CameraSize.Y))
		{
			CameraSize = FVector2D(CameraSize.Y, CameraSize.X);
		}

		const float CameraAspectRatio = CameraSize.X / CameraSize.Y;
		const float ViewAspectRatio = ViewSize.X / ViewSize.Y;
		const float ViewAspectRatioLandscape = (ViewSize.X > ViewSize.Y) ? ViewAspectRatio : ViewSize.Y / ViewSize.X;

		float UVOffsetAmount = 0.0f;
		if (!FMath::IsNearlyEqual(ViewAspectRatio, CameraAspectRatio))
		{
			if (ViewAspectRatio > CameraAspectRatio)
			{
				UVOffsetAmount = 0.5f * (1.0f - (CameraAspectRatio / ViewAspectRatio));
			}
			else
			{
				UVOffsetAmount = 0.5f * (1.0f - (ViewAspectRatio / CameraAspectRatio));
			}
		}

		UVOffset = (ViewAspectRatioLandscape <= Frame.Camera.GetAspectRatio()) ? FVector2D(UVOffsetAmount, 0.0f) : FVector2D(0.0f, UVOffsetAmount);

		// Setup vertex buffer
		const FVector4 Positions[] =
		{
			FVector4(0.0f, 1.0f, 0.0f, 1.0f),
			FVector4(0.0f, 0.0f, 0.0f, 1.0f),
			FVector4(1.0f, 1.0f, 0.0f, 1.0f),
			FVector4(1.0f, 0.0f, 0.0f, 1.0f)
		};

		// 2 orientations
		{
			const FVector2D UVs[] =
			{
				// Landscape
				FVector2D(UVOffset.X, 1.0f - UVOffset.Y),
				FVector2D(UVOffset.X, UVOffset.Y),
				FVector2D(1.0f - UVOffset.X, 1.0f - UVOffset.Y),
				FVector2D(1.0f - UVOffset.X, UVOffset.Y),
				
				// Portrait
				FVector2D(UVOffset.Y, 1.0f - UVOffset.X),
				FVector2D(UVOffset.Y, UVOffset.X),
				FVector2D(1.0f - UVOffset.Y, 1.0f - UVOffset.X),
				FVector2D(1.0f - UVOffset.Y, UVOffset.X),
			};

			uint32 UVIndex = 0;
			for (uint32 OrientationIter = 0; OrientationIter < 2; ++OrientationIter)
			{
				TResourceArray<FFilterVertex, VERTEXBUFFER_ALIGNMENT> Vertices;
				Vertices.SetNumUninitialized(4);

				Vertices[0].Position = Positions[0];
				Vertices[0].UV = UVs[UVIndex];

				Vertices[1].Position = Positions[1];
				Vertices[1].UV = UVs[UVIndex + 1];

				Vertices[2].Position = Positions[2];
				Vertices[2].UV = UVs[UVIndex + 2];

				Vertices[3].Position = Positions[3];
				Vertices[3].UV = UVs[UVIndex + 3];

				UVIndex += 4;

				FRHIResourceCreateInfo CreateInfoVB(&Vertices);
				OverlayVertexBufferRHITwoOrientations[OrientationIter] = RHICreateVertexBuffer(Vertices.GetResourceDataSize(), BUF_Static, CreateInfoVB);
			}
		}
		
		// 4 orientations
		{
			const FVector2D UVs[] =
			{
				// Landscape left
				FVector2D(UVOffset.X, 1.0f - UVOffset.Y),
				FVector2D(UVOffset.X, UVOffset.Y),
				FVector2D(1.0f - UVOffset.X, 1.0f - UVOffset.Y),
				FVector2D(1.0f - UVOffset.X, UVOffset.Y),

				// Landscape right
				FVector2D(1.0f - UVOffset.X, UVOffset.Y),
				FVector2D(1.0f - UVOffset.X, 1.0f - UVOffset.Y),
				FVector2D(UVOffset.X, UVOffset.Y),
				FVector2D(UVOffset.X, 1.0f - UVOffset.Y),

				// Portrait
				FVector2D(1.0f - UVOffset.X, 1.0f - UVOffset.Y),
				FVector2D(UVOffset.X, 1.0f - UVOffset.Y),
				FVector2D(1.0f - UVOffset.X, UVOffset.Y),
				FVector2D(UVOffset.X, UVOffset.Y),

				// Portrait Upside Down
				FVector2D(UVOffset.X, UVOffset.Y),
				FVector2D(1.0f - UVOffset.X, UVOffset.Y),
				FVector2D(UVOffset.X, 1.0f - UVOffset.Y),
				FVector2D(1.0f - UVOffset.X, 1.0f - UVOffset.Y)
			};

			uint32 UVIndex = 0;
			for (uint32 OrientationIter = 0; OrientationIter < 4; ++OrientationIter)
			{
				TResourceArray<FFilterVertex, VERTEXBUFFER_ALIGNMENT> Vertices;
				Vertices.SetNumUninitialized(4);

				Vertices[0].Position = Positions[0];
				Vertices[0].UV = UVs[UVIndex];

				Vertices[1].Position = Positions[1];
				Vertices[1].UV = UVs[UVIndex + 1];

				Vertices[2].Position = Positions[2];
				Vertices[2].UV = UVs[UVIndex + 2];

				Vertices[3].Position = Positions[3];
				Vertices[3].UV = UVs[UVIndex + 3];

				UVIndex += 4;

				FRHIResourceCreateInfo CreateInfoVB(&Vertices);
				OverlayVertexBufferRHIFourOrientations[OrientationIter] = RHICreateVertexBuffer(Vertices.GetResourceDataSize(), BUF_Static, CreateInfoVB);
			}
		}
	}


	if (IndexBufferRHI == nullptr || !IndexBufferRHI.IsValid())
	{
		// Setup index buffer
		const uint16 Indices[] = { 0, 1, 2, 2, 1, 3 };

		TResourceArray<uint16, INDEXBUFFER_ALIGNMENT> IndexBuffer;
		const uint32 NumIndices = UE_ARRAY_COUNT(Indices);
		IndexBuffer.AddUninitialized(NumIndices);
		FMemory::Memcpy(IndexBuffer.GetData(), Indices, NumIndices * sizeof(uint16));

		FRHIResourceCreateInfo CreateInfoIB(&IndexBuffer);
		IndexBufferRHI = RHICreateIndexBuffer(sizeof(uint16), IndexBuffer.GetResourceDataSize(), BUF_Static, CreateInfoIB);
	}

	const auto FeatureLevel = InView.GetFeatureLevel();
	IRendererModule& RendererModule = GetRendererModule();

	const FMaterial* const CameraMaterial = RenderingOverlayMaterial->GetRenderProxy()->GetMaterial(FeatureLevel);
	const FMaterialShaderMap* const MaterialShaderMap = CameraMaterial->GetRenderingThreadShaderMap();

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	if (bRenderingOcclusion)
	{
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
	}
	else
	{
		// Disable the write mask for the alpha channel so that the scene depth info saved in it is retained
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI();
	}
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;

	const bool bIsMobileRenderer = FeatureLevel <= ERHIFeatureLevel::ES3_1;
	TShaderRef<FMaterialShader> VertexShader;
	TShaderRef<FMaterialShader> PixelShader;
	if (bIsMobileRenderer)
	{
		VertexShader = MaterialShaderMap->GetShader<FARKitCameraOverlayMobileVS>();
		check(VertexShader.IsValid());
		PixelShader = MaterialShaderMap->GetShader<FARKitCameraOverlayMobilePS>();
		check(PixelShader.IsValid());
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	}
	else
	{
		VertexShader = MaterialShaderMap->GetShader<FARKitCameraOverlayVS>();
		check(VertexShader.IsValid());
		PixelShader = MaterialShaderMap->GetShader<FARKitCameraOverlayPS>();
		check(PixelShader.IsValid());
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
	}

	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

	const FIntPoint ViewSize = InView.UnconstrainedViewRect.Size();
	FDrawRectangleParameters Parameters;
	Parameters.PosScaleBias = FVector4(ViewSize.X, ViewSize.Y, 0, 0);
	Parameters.UVScaleBias = FVector4(1.0f, 1.0f, 0.0f, 0.0f);
	Parameters.InvTargetSizeAndTextureSize = FVector4(
													  1.0f / ViewSize.X, 1.0f / ViewSize.Y,
													  1.0f, 1.0f);

	if (bIsMobileRenderer)
	{
		FARKitCameraOverlayMobileVS* const VertexShaderPtr = static_cast<FARKitCameraOverlayMobileVS*>(VertexShader.GetShader());
		check(VertexShaderPtr != nullptr);
		SetUniformBufferParameterImmediate(RHICmdList, VertexShader.GetVertexShader(), VertexShaderPtr->GetUniformBufferParameter<FDrawRectangleParameters>(), Parameters);
		VertexShaderPtr->SetParameters(RHICmdList, InView);
		FARKitCameraOverlayMobilePS* PixelShaderPtr = static_cast<FARKitCameraOverlayMobilePS*>(PixelShader.GetShader());
		check(PixelShaderPtr != nullptr);
		PixelShaderPtr->SetParameters(RHICmdList, InView, RenderingOverlayMaterial->GetRenderProxy());
	}
	else
	{
		FARKitCameraOverlayVS* const VertexShaderPtr = static_cast<FARKitCameraOverlayVS*>(VertexShader.GetShader());
		check(VertexShaderPtr != nullptr);
		SetUniformBufferParameterImmediate(RHICmdList, VertexShader.GetVertexShader(), VertexShaderPtr->GetUniformBufferParameter<FDrawRectangleParameters>(), Parameters);
		VertexShaderPtr->SetParameters(RHICmdList, InView);
		FARKitCameraOverlayPS* PixelShaderPtr = static_cast<FARKitCameraOverlayPS*>(PixelShader.GetShader());
		check(PixelShaderPtr != nullptr);
		PixelShaderPtr->SetParameters(RHICmdList, InView, RenderingOverlayMaterial->GetRenderProxy());
	}

	FRHIVertexBuffer* VertexBufferRHI = nullptr;
	if (bRespectAllOrientations)
	{
		switch (DeviceOrientation)
		{
			case EDeviceScreenOrientation::LandscapeRight:
				VertexBufferRHI = OverlayVertexBufferRHIFourOrientations[0];
				break;

			case EDeviceScreenOrientation::LandscapeLeft:
				VertexBufferRHI = OverlayVertexBufferRHIFourOrientations[1];
				break;

			case EDeviceScreenOrientation::Portrait:
				VertexBufferRHI = OverlayVertexBufferRHIFourOrientations[2];
				break;

			case EDeviceScreenOrientation::PortraitUpsideDown:
				VertexBufferRHI = OverlayVertexBufferRHIFourOrientations[3];
				break;
				
			default:
				VertexBufferRHI = OverlayVertexBufferRHIFourOrientations[0];
				break;
		}
	}
	else
	{
		switch (DeviceOrientation)
		{
			case EDeviceScreenOrientation::LandscapeRight:
			case EDeviceScreenOrientation::LandscapeLeft:
				VertexBufferRHI = OverlayVertexBufferRHITwoOrientations[0];
				break;

			case EDeviceScreenOrientation::Portrait:
			case EDeviceScreenOrientation::PortraitUpsideDown:
				VertexBufferRHI = OverlayVertexBufferRHITwoOrientations[1];
				break;

			default:
				VertexBufferRHI = OverlayVertexBufferRHITwoOrientations[0];
				break;
		}
	}


	if (VertexBufferRHI && IndexBufferRHI.IsValid())
	{
		RHICmdList.SetStreamSource(0, VertexBufferRHI, 0);
		RHICmdList.DrawIndexedPrimitive(
			IndexBufferRHI,
			/*BaseVertexIndex=*/ 0,
			/*MinIndex=*/ 0,
			/*NumVertices=*/ 4,
			/*StartIndex=*/ 0,
			/*NumPrimitives=*/ 2,
			/*NumInstances=*/ 1
		);
	}
}

void FAppleARKitVideoOverlay::RenderVideoOverlay_RenderThread(FRHICommandListImmediate& RHICmdList, const FSceneView& InView, FAppleARKitFrame& Frame, const EDeviceScreenOrientation DeviceOrientation, const float WorldToMeterScale)
{
	UpdateVideoTextures(Frame);
	UpdateOcclusionTextures(Frame);
	
#if MATERIAL_CAMERAIMAGE_CONVERSION
	RenderVideoOverlayWithMaterial(RHICmdList, InView, Frame, DeviceOrientation, MID_CameraOverlay, false, true);
#else
	RenderVideoOverlayWithMaterial(RHICmdList, InView, Frame, DeviceOrientation, MID_CameraOverlay, false, false);
#endif
	
	if (bEnablePersonOcclusion)
	{
		UMaterialInstanceDynamic* OcclusionMaterial = bOcclusionDepthTextureRecentlyUpdated ? MID_DepthOcclusionOverlay : MID_MatteOcclusionOverlay;
		
		static const FName WorldToMeterScaleParamName(TEXT("WorldToMeterScale"));
		OcclusionMaterial->SetScalarParameterValue(WorldToMeterScaleParamName, WorldToMeterScale);
		
		const bool bIsLandscape = (DeviceOrientation == EDeviceScreenOrientation::LandscapeLeft || DeviceOrientation == EDeviceScreenOrientation::LandscapeRight);
		FLinearColor DepthTextureUVParamValue = {
			1.f - (bIsLandscape ? UVOffset.X : UVOffset.Y) * 2.f,
			1.f - (bIsLandscape ? UVOffset.Y : UVOffset.X) * 2.f,
			1.f,
			1.f
		};
		
		static const FName DepthTextureUVParamName(TEXT("DepthTextureUVParam"));
		OcclusionMaterial->SetVectorParameterValue(DepthTextureUVParamName, DepthTextureUVParamValue);
		
		RenderVideoOverlayWithMaterial(RHICmdList, InView, Frame, DeviceOrientation, OcclusionMaterial, true, false);
		bOcclusionDepthTextureRecentlyUpdated = false;
	}
}

bool FAppleARKitVideoOverlay::GetPassthroughCameraUVs_RenderThread(TArray<FVector2D>& OutUVs, const EDeviceScreenOrientation DeviceOrientation)
{
#if SUPPORTS_ARKIT_1_0
	// UVOffset is initialized when OverlayVertexBufferRHITwoOrientations is created
	if (OverlayVertexBufferRHITwoOrientations[0] != nullptr && OverlayVertexBufferRHITwoOrientations[0].IsValid())
	{
		OutUVs.SetNumUninitialized(4);

		switch (DeviceOrientation)
		{
		case EDeviceScreenOrientation::LandscapeRight:
			OutUVs[1] = FVector2D(UVOffset.X, 1.0f - UVOffset.Y);
			OutUVs[0] = FVector2D(UVOffset.X, UVOffset.Y);
			OutUVs[3] = FVector2D(1.0f - UVOffset.X, 1.0f - UVOffset.Y);
			OutUVs[2] = FVector2D(1.0f - UVOffset.X, UVOffset.Y);
			return true;

		case EDeviceScreenOrientation::LandscapeLeft:
            OutUVs[1] = FVector2D(UVOffset.X, 1.0f - UVOffset.Y);
            OutUVs[0] = FVector2D(UVOffset.X, UVOffset.Y);
            OutUVs[3] = FVector2D(1.0f - UVOffset.X, 1.0f - UVOffset.Y);
            OutUVs[2] = FVector2D(1.0f - UVOffset.X, UVOffset.Y);
			return true;

		case EDeviceScreenOrientation::Portrait:
			OutUVs[1] = FVector2D(UVOffset.Y, 1.0f - UVOffset.X);
			OutUVs[0] = FVector2D(UVOffset.Y, UVOffset.X);
			OutUVs[3] = FVector2D(1.0f - UVOffset.Y, 1.0f - UVOffset.X);
			OutUVs[2] = FVector2D(1.0f - UVOffset.Y, UVOffset.X);
			return true;

		case EDeviceScreenOrientation::PortraitUpsideDown:
            OutUVs[1] = FVector2D(UVOffset.Y, 1.0f - UVOffset.X);
            OutUVs[0] = FVector2D(UVOffset.Y, UVOffset.X);
            OutUVs[3] = FVector2D(1.0f - UVOffset.Y, 1.0f - UVOffset.X);
            OutUVs[2] = FVector2D(1.0f - UVOffset.Y, UVOffset.X);
			return true;

		default:
			return false;
		}
	}
	else
#endif
	{
		return false;
	}
}

void FAppleARKitVideoOverlay::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(MID_CameraOverlay);
	Collector.AddReferencedObject(OcclusionMatteTexture);
	Collector.AddReferencedObject(OcclusionDepthTexture);
	Collector.AddReferencedObject(MID_DepthOcclusionOverlay);
	Collector.AddReferencedObject(MID_MatteOcclusionOverlay);
}

void FAppleARKitVideoOverlay::SetOverlayTexture(UARTextureCameraImage* InCameraImage)
{
	static const FName ParamName(TEXT("CameraImage"));
	MID_CameraOverlay->SetTextureParameterValue(ParamName, InCameraImage);
	MID_DepthOcclusionOverlay->SetTextureParameterValue(ParamName, InCameraImage);
	MID_MatteOcclusionOverlay->SetTextureParameterValue(ParamName, InCameraImage);
}

void FAppleARKitVideoOverlay::SetEnablePersonOcclusion(bool bEnable)
{
#if SUPPORTS_ARKIT_3_0
	bEnablePersonOcclusion = bEnable;
	
	if (bEnablePersonOcclusion)
	{
		// TODO: add new EARTextureType for the occlusion textures
		OcclusionMatteTexture = NewObject<UAppleARKitOcclusionTexture>();
		OcclusionMatteTexture->TextureType = EARTextureType::CameraImage;
		OcclusionMatteTexture->UpdateResource();
		
		OcclusionDepthTexture = NewObject<UAppleARKitOcclusionTexture>();
		OcclusionDepthTexture->TextureType = EARTextureType::CameraImage;
		OcclusionDepthTexture->UpdateResource();
	}
	else
	{
		OcclusionMatteTexture = OcclusionDepthTexture = nullptr;
	}
	
	static const FName MatteTextureParamName(TEXT("OcclusionMatteTexture"));
	static const FName DepthTextureParamName(TEXT("OcclusionDepthTexture"));
	
	MID_DepthOcclusionOverlay->SetTextureParameterValue(MatteTextureParamName, OcclusionMatteTexture);
	MID_DepthOcclusionOverlay->SetTextureParameterValue(DepthTextureParamName, OcclusionDepthTexture);
	
	MID_MatteOcclusionOverlay->SetTextureParameterValue(MatteTextureParamName, OcclusionMatteTexture);
	MID_MatteOcclusionOverlay->SetTextureParameterValue(DepthTextureParamName, OcclusionDepthTexture);
	
	// we need to clear the scene color with max alpha to ensure the scene depth is correct on mobile
	static IConsoleVariable* CVarSceneColorClearAlphaFlag = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MobileClearSceneColorWithMaxAlpha"));
	if (CVarSceneColorClearAlphaFlag)
	{
		CVarSceneColorClearAlphaFlag->Set(bEnablePersonOcclusion ? 1 : 0);
	}
#endif
}
