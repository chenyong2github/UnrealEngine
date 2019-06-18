// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AppleARKitVideoOverlay.h"
#include "AppleARKitFrame.h"
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

FAppleARKitVideoOverlay::FAppleARKitVideoOverlay()
	: MID_CameraOverlay(nullptr)
	, RenderingOverlayMaterial(nullptr)
{
	UMaterialInterface* LoadedMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/AppleARKit/M_CameraOverlay.M_CameraOverlay"));
	check(LoadedMaterial != nullptr);
	// Create a MaterialInstanceDynamic which we'll replace the texture with the camera texture from ARKit
	MID_CameraOverlay = UMaterialInstanceDynamic::Create(LoadedMaterial, GetTransientPackage());
	RenderingOverlayMaterial = MID_CameraOverlay;
	check(RenderingOverlayMaterial != nullptr);
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
			return Parameters.Material->GetMaterialDomain() == MD_PostProcess && IsMobilePlatform(Parameters.Platform);
		}
		else
		{
			return Parameters.Material->GetMaterialDomain() == MD_PostProcess && !IsMobilePlatform(Parameters.Platform);
		}
	}

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FMaterialShader::ModifyCompilationEnvironment(Parameters.Platform, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL"), 1);
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL_MOBILE"), bIsMobileRenderer);
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL_BEFORE_TONEMAP"), (Parameters.Material->GetBlendableLocation() != BL_AfterTonemapping) ? 1 : 0);
	}
};

// We use something similar to the PostProcessMaterial to render the color camera overlay.
template <bool bIsMobileRenderer>
class TARKitCameraOverlayVS : public TPostProcessMaterialShader<bIsMobileRenderer>
{
public:
	DECLARE_MATERIAL_SHADER(TARKitCameraOverlayVS);

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		TPostProcessMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_AR_PASSTHROUGH"), 1);
	}

	TARKitCameraOverlayVS() = default;
	TARKitCameraOverlayVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: TPostProcessMaterialShader(Initializer)
	{}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView View)
	{
		FRHIVertexShader* ShaderRHI = GetVertexShader();
		TPostProcessMaterialShader::SetViewParameters(RHICmdList, ShaderRHI, View, View.ViewUniformBuffer);
	}
};

using FARKitCameraOverlayVS = TARKitCameraOverlayVS<false>;
using FARKitCameraOverlayMobileVS = TARKitCameraOverlayVS<true>;

IMPLEMENT_MATERIAL_SHADER(FARKitCameraOverlayVS, "/Engine/Private/PostProcessMaterialShaders.usf", "MainVS_VideoOverlay", SF_Vertex);
IMPLEMENT_MATERIAL_SHADER(FARKitCameraOverlayMobileVS, "/Engine/Private/PostProcessMaterialShaders.usf", "MainVS", SF_Vertex);

template <bool bIsMobileRenderer>
class TARKitCameraOverlayPS : public TPostProcessMaterialShader<bIsMobileRenderer>
{
public:
	DECLARE_MATERIAL_SHADER(TARKitCameraOverlayPS);

	static void ModifyCompilationEnvironment(const FMaterialShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		TPostProcessMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("OUTPUT_MOBILE_HDR"), IsMobileHDR() ? 1 : 0);
	}

	TARKitCameraOverlayPS() = default;
	TARKitCameraOverlayPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: TPostProcessMaterialShader(Initializer)
	{}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView View, const FMaterialRenderProxy* Material)
	{
		FRHIPixelShader* ShaderRHI = GetPixelShader();
		TPostProcessMaterialShader::SetParameters(RHICmdList, ShaderRHI, Material, *Material->GetMaterial(View.GetFeatureLevel()), View, View.ViewUniformBuffer, ESceneTextureSetupMode::None);
	}
};

using FARKitCameraOverlayPS = TARKitCameraOverlayPS<false>;
using FARKitCameraOverlayMobilePS = TARKitCameraOverlayPS<true>;

IMPLEMENT_MATERIAL_SHADER(FARKitCameraOverlayPS, "/Engine/Private/PostProcessMaterialShaders.usf", "MainPS_VideoOverlay", SF_Pixel);
IMPLEMENT_MATERIAL_SHADER(FARKitCameraOverlayMobilePS, "/Engine/Private/PostProcessMaterialShaders.usf", "MainPS_ES2", SF_Pixel);

void FAppleARKitVideoOverlay::RenderVideoOverlay_RenderThread(FRHICommandListImmediate& RHICmdList, const FSceneView& InView)
{
	if (RenderingOverlayMaterial == nullptr || !RenderingOverlayMaterial->IsValidLowLevel())
	{
		return;
	}

	if (VertexBufferRHI == nullptr || !VertexBufferRHI.IsValid())
	{
		// Setup vertex buffer
		TResourceArray<FFilterVertex, VERTEXBUFFER_ALIGNMENT> Vertices;
		Vertices.SetNumUninitialized(4);

		Vertices[0].Position = FVector4(0.f, 0.f, 0.f, 1.f);
		Vertices[0].UV = FVector2D(0.f, 0.f);

		Vertices[1].Position = FVector4(1.f, 0.f, 0.f, 1.f);
		Vertices[1].UV = FVector2D(1.f, 0.f);

		Vertices[2].Position = FVector4(1.f, 1.f, 0.f, 1.f);
		Vertices[2].UV = FVector2D(1.f, 1.f);

		Vertices[3].Position = FVector4(0.f, 1.f, 0.f, 1.f);
		Vertices[3].UV = FVector2D(0.f, 1.f);

		FRHIResourceCreateInfo CreateInfoVB(&Vertices);
		VertexBufferRHI = RHICreateVertexBuffer(Vertices.GetResourceDataSize(), BUF_Static, CreateInfoVB);
	}

	if (IndexBufferRHI == nullptr || !IndexBufferRHI.IsValid())
	{
		// Setup index buffer
		const uint16 Indices[] = { 0, 1, 2, 2, 3, 0 };

		TResourceArray<uint16, INDEXBUFFER_ALIGNMENT> IndexBuffer;
		const uint32 NumIndices = ARRAY_COUNT(Indices);
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

	GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
	GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
	GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;
	GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;

	const bool bIsMobileRenderer = FeatureLevel <= ERHIFeatureLevel::ES3_1;
	FMaterialShader* VertexShader = nullptr;
	FMaterialShader* PixelShader = nullptr;
	if (bIsMobileRenderer)
	{
		VertexShader = MaterialShaderMap->GetShader<FARKitCameraOverlayMobileVS>();
		check(VertexShader != nullptr);
		PixelShader = MaterialShaderMap->GetShader<FARKitCameraOverlayMobilePS>();
		check(PixelShader != nullptr);
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(VertexShader);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(PixelShader);
	}
	else
	{
		VertexShader = MaterialShaderMap->GetShader<FARKitCameraOverlayVS>();
		check(VertexShader != nullptr);
		PixelShader = MaterialShaderMap->GetShader<FARKitCameraOverlayPS>();
		check(PixelShader != nullptr);
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(VertexShader);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(PixelShader);
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
		FARKitCameraOverlayMobileVS* const VertexShaderPtr = static_cast<FARKitCameraOverlayMobileVS*>(VertexShader);
		check(VertexShaderPtr != nullptr);
		SetUniformBufferParameterImmediate(RHICmdList, VertexShaderPtr->GetVertexShader(), VertexShaderPtr->GetUniformBufferParameter<FDrawRectangleParameters>(), Parameters);
		VertexShaderPtr->SetParameters(RHICmdList, InView);
		FARKitCameraOverlayMobilePS* PixelShaderPtr = static_cast<FARKitCameraOverlayMobilePS*>(PixelShader);
		check(PixelShaderPtr != nullptr);
		PixelShaderPtr->SetParameters(RHICmdList, InView, RenderingOverlayMaterial->GetRenderProxy());
	}
	else
	{
		FARKitCameraOverlayVS* const VertexShaderPtr = static_cast<FARKitCameraOverlayVS*>(VertexShader);
		check(VertexShaderPtr != nullptr);
		SetUniformBufferParameterImmediate(RHICmdList, VertexShaderPtr->GetVertexShader(), VertexShaderPtr->GetUniformBufferParameter<FDrawRectangleParameters>(), Parameters);
		VertexShaderPtr->SetParameters(RHICmdList, InView);
		FARKitCameraOverlayPS* PixelShaderPtr = static_cast<FARKitCameraOverlayPS*>(PixelShader);
		check(PixelShaderPtr != nullptr);
		PixelShaderPtr->SetParameters(RHICmdList, InView, RenderingOverlayMaterial->GetRenderProxy());
	}

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


bool FAppleARKitVideoOverlay::GetPassthroughCameraUVs_RenderThread(TArray<FVector2D>& OutUVs, const EDeviceScreenOrientation)
{
	OutUVs.Reset(4);

	new(OutUVs) FVector2D(0.f, 0.f);
	new(OutUVs) FVector2D(1.f, 0.f);
	new(OutUVs) FVector2D(1.0f, 1.0f);
	new(OutUVs) FVector2D(0.f, 1.0f);

	return true;
}

void FAppleARKitVideoOverlay::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(RenderingOverlayMaterial);
	Collector.AddReferencedObject(MID_CameraOverlay);
}

void FAppleARKitVideoOverlay::SetOverlayTexture(UARTextureCameraImage* InCameraImage)
{
	MID_CameraOverlay->SetTextureParameterValue(FName("CameraImage"), InCameraImage);
}
