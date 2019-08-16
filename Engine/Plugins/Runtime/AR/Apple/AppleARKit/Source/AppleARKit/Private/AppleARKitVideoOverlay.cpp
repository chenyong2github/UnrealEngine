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

// We use something similar to the PostProcessMaterial to render the color camera overlay.
template <bool bIsMobileRenderer>
class FARKitCameraOverlayVS : public FMaterialShader
{
	DECLARE_SHADER_TYPE(FARKitCameraOverlayVS, Material);
public:

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
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL_BEFORE_TONEMAP"), (Parameters.Material->GetBlendableLocation() != BL_AfterTonemapping) ? 1 : 0);
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_AR_PASSTHROUGH"), 1);
	}

	FARKitCameraOverlayVS() { }
	FARKitCameraOverlayVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FMaterialShader(Initializer)
	{
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView View)
	{
		FRHIVertexShader* ShaderRHI = GetVertexShader();
		FMaterialShader::SetViewParameters(RHICmdList, ShaderRHI, View, View.ViewUniformBuffer);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		const bool bShaderHasOutdatedParameters = FMaterialShader::Serialize(Ar);
		return bShaderHasOutdatedParameters;
	}
};

IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FARKitCameraOverlayVS<true>, TEXT("/Engine/Private/PostProcessMaterialShaders.usf"), TEXT("MainVS_ES2"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FARKitCameraOverlayVS<false>, TEXT("/Engine/Private/PostProcessMaterialShaders.usf"), TEXT("MainVS_VideoOverlay"), SF_Vertex);

template <bool bIsMobileRenderer>
class FARKitCameraOverlayPS : public FMaterialShader
{
	DECLARE_SHADER_TYPE(FARKitCameraOverlayPS, Material);
public:

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
		FMaterialShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL"), 1);
		OutEnvironment.SetDefine(TEXT("OUTPUT_MOBILE_HDR"), IsMobileHDR() ? 1 : 0);
		OutEnvironment.SetDefine(TEXT("POST_PROCESS_MATERIAL_BEFORE_TONEMAP"), (Parameters.Material->GetBlendableLocation() != BL_AfterTonemapping) ? 1 : 0);
	}

	FARKitCameraOverlayPS() {}
	FARKitCameraOverlayPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) :
		FMaterialShader(Initializer)
	{
		for (uint32 InputIter = 0; InputIter < ePId_Input_MAX; ++InputIter)
		{
			PostprocessInputParameter[InputIter].Bind(Initializer.ParameterMap, *FString::Printf(TEXT("PostprocessInput%d"), InputIter));
			PostprocessInputParameterSampler[InputIter].Bind(Initializer.ParameterMap, *FString::Printf(TEXT("PostprocessInput%dSampler"), InputIter));
		}
	}

	void SetParameters(FRHICommandList& RHICmdList, const FSceneView View, const FMaterialRenderProxy* Material)
	{
		FRHIPixelShader* ShaderRHI = GetPixelShader();
		FMaterialShader::SetParameters(RHICmdList, ShaderRHI, Material, *Material->GetMaterial(View.GetFeatureLevel()), View, View.ViewUniformBuffer, ESceneTextureSetupMode::None);

		for (uint32 InputIter = 0; InputIter < ePId_Input_MAX; ++InputIter)
		{
			if (PostprocessInputParameter[InputIter].IsBound())
			{
				SetTextureParameter(
					RHICmdList, 
					ShaderRHI, 
					PostprocessInputParameter[InputIter],
					PostprocessInputParameterSampler[InputIter],
					TStaticSamplerState<>::GetRHI(),
					GBlackTexture->TextureRHI);
			}
		}
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		const bool bShaderHasOutdatedParameters = FMaterialShader::Serialize(Ar);
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderResourceParameter PostprocessInputParameter[ePId_Input_MAX];
	FShaderResourceParameter PostprocessInputParameterSampler[ePId_Input_MAX];
};

IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FARKitCameraOverlayPS<true>, TEXT("/Engine/Private/PostProcessMaterialShaders.usf"), TEXT("MainPS_ES2"), SF_Pixel);
IMPLEMENT_MATERIAL_SHADER_TYPE(template<>, FARKitCameraOverlayPS<false>, TEXT("/Engine/Private/PostProcessMaterialShaders.usf"), TEXT("MainPS_VideoOverlay"), SF_Pixel);

void FAppleARKitVideoOverlay::RenderVideoOverlay_RenderThread(FRHICommandListImmediate& RHICmdList, const FSceneView& InView, FAppleARKitFrame& Frame, const EDeviceScreenOrientation DeviceOrientation)
{
	if (RenderingOverlayMaterial == nullptr || !RenderingOverlayMaterial->IsValidLowLevel())
	{
		return;
	}

	if (OverlayVertexBufferRHI[0] == nullptr || !OverlayVertexBufferRHI[0].IsValid())
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
			OverlayVertexBufferRHI[OrientationIter] = RHICreateVertexBuffer(Vertices.GetResourceDataSize(), BUF_Static, CreateInfoVB);
		}
	}


	if (IndexBufferRHI == nullptr || !IndexBufferRHI.IsValid())
	{
		// Setup index buffer
		const uint16 Indices[] = { 0, 1, 2, 2, 1, 3 };

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
		VertexShader = MaterialShaderMap->GetShader<FARKitCameraOverlayVS<true>>();
		check(VertexShader != nullptr);
		PixelShader = MaterialShaderMap->GetShader<FARKitCameraOverlayPS<true>>();
		check(PixelShader != nullptr);
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(VertexShader);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(PixelShader);
	}
	else
	{
		VertexShader = MaterialShaderMap->GetShader<FARKitCameraOverlayVS<false>>();
		check(VertexShader != nullptr);
		PixelShader = MaterialShaderMap->GetShader<FARKitCameraOverlayPS<false>>();
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
		FARKitCameraOverlayVS<true>* const VertexShaderPtr = static_cast<FARKitCameraOverlayVS<true>*>(VertexShader);
		check(VertexShaderPtr != nullptr);
		SetUniformBufferParameterImmediate(RHICmdList, VertexShaderPtr->GetVertexShader(), VertexShaderPtr->GetUniformBufferParameter<FDrawRectangleParameters>(), Parameters);
		VertexShaderPtr->SetParameters(RHICmdList, InView);
		FARKitCameraOverlayPS<true>* PixelShaderPtr = static_cast<FARKitCameraOverlayPS<true>*>(PixelShader);
		check(PixelShaderPtr != nullptr);
		PixelShaderPtr->SetParameters(RHICmdList, InView, RenderingOverlayMaterial->GetRenderProxy());
	}
	else
	{
		FARKitCameraOverlayVS<false>* const VertexShaderPtr = static_cast<FARKitCameraOverlayVS<false>*>(VertexShader);
		check(VertexShaderPtr != nullptr);
		SetUniformBufferParameterImmediate(RHICmdList, VertexShaderPtr->GetVertexShader(), VertexShaderPtr->GetUniformBufferParameter<FDrawRectangleParameters>(), Parameters);
		VertexShaderPtr->SetParameters(RHICmdList, InView);
		FARKitCameraOverlayPS<false>* PixelShaderPtr = static_cast<FARKitCameraOverlayPS<false>*>(PixelShader);
		check(PixelShaderPtr != nullptr);
		PixelShaderPtr->SetParameters(RHICmdList, InView, RenderingOverlayMaterial->GetRenderProxy());
	}

	FRHIVertexBuffer* VertexBufferRHI = nullptr;
	switch (DeviceOrientation)
	{
		case EDeviceScreenOrientation::LandscapeRight:
		case EDeviceScreenOrientation::LandscapeLeft:
			VertexBufferRHI = OverlayVertexBufferRHI[0];
			break;

		case EDeviceScreenOrientation::Portrait:
		case EDeviceScreenOrientation::PortraitUpsideDown:
			VertexBufferRHI = OverlayVertexBufferRHI[1];
			break;

		default:
			VertexBufferRHI = OverlayVertexBufferRHI[0];
			break;
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


bool FAppleARKitVideoOverlay::GetPassthroughCameraUVs_RenderThread(TArray<FVector2D>& OutUVs, const EDeviceScreenOrientation DeviceOrientation)
{
#if SUPPORTS_ARKIT_1_0
	if (OverlayVertexBufferRHI[0] != nullptr && OverlayVertexBufferRHI[0].IsValid())
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
	Collector.AddReferencedObject(RenderingOverlayMaterial);
	Collector.AddReferencedObject(MID_CameraOverlay);
}

void FAppleARKitVideoOverlay::SetOverlayTexture(UARTextureCameraImage* InCameraImage)
{
	MID_CameraOverlay->SetTextureParameterValue(FName("CameraImage"), InCameraImage);
}
