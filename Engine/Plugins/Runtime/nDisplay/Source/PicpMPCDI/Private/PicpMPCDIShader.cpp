// Copyright Epic Games, Inc. All Rights Reserved.

#include "PicpMPCDIShader.h"

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHIResources.h"

#include "RenderResource.h"
#include "CommonRenderResources.h"
#include "PixelShaderUtils.h"

#include "Shader.h"
#include "GlobalShader.h"

#include "ShaderParameters.h"
#include "ShaderParameterUtils.h"
#include "ShaderParameterStruct.h"
#include "ShaderPermutation.h"

#include "MPCDIData.h"
#include "MPCDIShader.h"
#include "MPCDIWarpTexture.h"

#define ShaderFileName "/Plugin/nDisplay/Private/MPCDIOverlayShaders.usf"

namespace PicpMpcdiStrings
{
	static constexpr auto RenderPassName = TEXT("DisplayClusterPicpWarpBlend");
	static constexpr auto RenderPassHint = TEXT("DisplayCluster Picp Warp&Blend");
};

enum class EVarPicpMPCDIShaderType : uint8
{
	Default = 0,
	DefaultNoBlend,
	Passthrough,
	ForceMultiPassRender,
	Disable,
};

static TAutoConsoleVariable<int32> CVarPicpMPCDIShaderType(
	TEXT("nDisplay.render.picp.shader"),
	(int)EVarPicpMPCDIShaderType::Default,
	TEXT("Select shader for picp:\n")
	TEXT(" 0: Warp shader (used by default)\n")
	TEXT(" 1: Warp shader with disabled blend maps\n")
	TEXT(" 2: Passthrough shader\n")
	TEXT(" 3: Force Multi-Pass render shaders\n")
	TEXT(" 4: Disable all warp shaders\n"),
	ECVF_RenderThreadSafe
);

enum class EPicpShaderType : uint8
{
	Passthrough,  // Viewport frustum (no warpblend, only frustum math)
	WarpAndBlend, // Pure mpcdi warpblend for viewport
	Invalid,
};


namespace PicpShaderPermutation
{
	// Shared permutation for picp warp
	class FPicpShaderViewportInput    : SHADER_PERMUTATION_BOOL("VIEWPORT_INPUT");

	class FPicpShaderOverlayUnder     : SHADER_PERMUTATION_BOOL("OVERLAY_UNDER");
	class FPicpShaderOverlayOver      : SHADER_PERMUTATION_BOOL("OVERLAY_OVER");

	class FPicpShaderInnerCamera      : SHADER_PERMUTATION_BOOL("INNER_CAMERA");
	class FPicpShaderChromakey        : SHADER_PERMUTATION_BOOL("CHROMAKEY");
	class FPicpShaderChromakeyMarker  : SHADER_PERMUTATION_BOOL("CHROMAKEY_MARKER");


	class FPicpShaderAlphaMapBlending : SHADER_PERMUTATION_BOOL("ALPHAMAP_BLENDING");
	class FPicpShaderBetaMapBlending  : SHADER_PERMUTATION_BOOL("BETAMAP_BLENDING");

	class FPicpShaderMeshWarp              : SHADER_PERMUTATION_BOOL("MESH_WARP");
	class FPicpShaderChromakeyMarkerMeshUV : SHADER_PERMUTATION_BOOL("CHROMAKEY_MARKER_MESH_UV");


	using FCommonVSDomain = TShaderPermutationDomain<FPicpShaderMeshWarp>;

	using FCommonPSDomain = TShaderPermutationDomain<
		FPicpShaderViewportInput,

		FPicpShaderOverlayUnder,
		FPicpShaderOverlayOver,

		FPicpShaderInnerCamera,
		FPicpShaderChromakey,
		FPicpShaderChromakeyMarker,

		FPicpShaderAlphaMapBlending,
		FPicpShaderBetaMapBlending,

		FPicpShaderMeshWarp,
		FPicpShaderChromakeyMarkerMeshUV
	>;
	
	bool ShouldCompileCommonPSPermutation(const FGlobalShaderPermutationParameters& Parameters, const FCommonPSDomain& PermutationVector)
	{
		if (PermutationVector.Get<FPicpShaderChromakeyMarkerMeshUV>() && 
			(!PermutationVector.Get<FPicpShaderChromakeyMarker>() || !PermutationVector.Get<FPicpShaderMeshWarp>()))
		{
			return false;
		}

		if (!PermutationVector.Get<FPicpShaderViewportInput>() &&
			(
			(PermutationVector.Get<FPicpShaderOverlayUnder>()) ||
			(PermutationVector.Get<FPicpShaderInnerCamera>() == PermutationVector.Get<FPicpShaderOverlayOver>())
			))
		{
			return false;
		}

		if (!PermutationVector.Get<FPicpShaderInnerCamera>() && PermutationVector.Get<FPicpShaderChromakey>())
		{
			return false;
		}

		if (!PermutationVector.Get<FPicpShaderChromakey>() && PermutationVector.Get<FPicpShaderChromakeyMarker>())
		{
			return false;
		}

		if (!PermutationVector.Get<FPicpShaderAlphaMapBlending>() && PermutationVector.Get<FPicpShaderBetaMapBlending>())
		{
			return false;
		}

		return true;
	}

	bool ShouldCompileCommonVSPermutation(const FGlobalShaderPermutationParameters& Parameters, const FCommonVSDomain& PermutationVector)
	{
		return true;
	}
};

BEGIN_SHADER_PARAMETER_STRUCT(FPicpVertexShaderParameters, )
	SHADER_PARAMETER(FVector4, DrawRectanglePosScaleBias)
	SHADER_PARAMETER(FVector4, DrawRectangleInvTargetSizeAndTextureSize)
	SHADER_PARAMETER(FVector4, DrawRectangleUVScaleBias)

	SHADER_PARAMETER(FMatrix, MeshToCaveProjectionMatrix)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FPicpPixelShaderParameters, )
	SHADER_PARAMETER_TEXTURE(Texture2D, InputTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, WarpMapTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, AlphaMapTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, BetaMapTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, OverlayUnderTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, OverlayOverTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, InnerCameraTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, ChromakeyCameraTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, ChromakeyMarkerTexture)

	SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, WarpMapSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, AlphaMapSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, BetaMapSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, OverlayUnderSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, OverlayOverSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, InnerCameraSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, ChromakeyCameraSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, ChromakeyMarkerSampler)

	SHADER_PARAMETER(FMatrix, ViewportTextureProjectionMatrix)
	SHADER_PARAMETER(FMatrix, OverlayProjectionMatrix)
	SHADER_PARAMETER(FMatrix, InnerCameraProjectionMatrix)

	SHADER_PARAMETER(float, AlphaEmbeddedGamma)

	SHADER_PARAMETER(FVector4, InnerCameraSoftEdge)

	SHADER_PARAMETER(float, ChromakeyMarkerScale)
END_SHADER_PARAMETER_STRUCT()

class FPicpWarpVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FPicpWarpVS);
	SHADER_USE_PARAMETER_STRUCT(FPicpWarpVS, FGlobalShader);
		
	using FPermutationDomain = PicpShaderPermutation::FCommonVSDomain;
	using FParameters = FPicpVertexShaderParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return PicpShaderPermutation::ShouldCompileCommonVSPermutation(Parameters, FPermutationDomain(Parameters.PermutationId));
	}
};

IMPLEMENT_GLOBAL_SHADER(FPicpWarpVS, ShaderFileName, "PicpWarpVS", SF_Vertex);

class FPicpWarpPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FPicpWarpPS);
	SHADER_USE_PARAMETER_STRUCT(FPicpWarpPS, FGlobalShader);

	using FPermutationDomain = PicpShaderPermutation::FCommonPSDomain;
	using FParameters = FPicpPixelShaderParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return PicpShaderPermutation::ShouldCompileCommonPSPermutation(Parameters, FPermutationDomain(Parameters.PermutationId));
	}
};

IMPLEMENT_GLOBAL_SHADER(FPicpWarpPS, ShaderFileName, "PicpWarpPS", SF_Pixel);

class FPicpPassthroughPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FPicpPassthroughPS);
	SHADER_USE_PARAMETER_STRUCT(FPicpPassthroughPS, FGlobalShader);
		
	using FParameters = FPicpPixelShaderParameters;
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}
};

IMPLEMENT_GLOBAL_SHADER(FPicpPassthroughPS, ShaderFileName, "Passthrough_PS", SF_Pixel);


struct FPicpRenderPassData
{
	PicpShaderPermutation::FCommonVSDomain VSPermutationVector;
	FPicpVertexShaderParameters            VSParameters;

	PicpShaderPermutation::FCommonPSDomain PSPermutationVector;
	FPicpPixelShaderParameters             PSParameters;
};

class FPicpPassRenderer
{
public:
	IMPCDI::FTextureWarpData &TextureWarpData;
	IMPCDI::FShaderInputData &ShaderInputData;

	const  FMPCDIRegion     *MPCDIRegion = nullptr;
	const  FMPCDIData       *MPCDIData   = nullptr;

	const FPicpProjectionOverlayViewportData* ViewportOverlayData = nullptr;

	EPicpShaderType PixelShaderType;

	bool bIsBlendDisabled = false;
	bool bForceMultiCameraPass = false;
	int CameraIndex = 0;

private:
	inline bool IsMultiPassRender() const
	{
		return bForceMultiCameraPass || ViewportOverlayData->IsMultiCamerasUsed();
	}

	inline bool IsFirstPass() const
	{
		return CameraIndex == 0;
	}

	inline bool IsLastPass() const
	{
		int TotalUsedCameras = ViewportOverlayData->Cameras.Num();

		if (bForceMultiCameraPass)
		{
			// Last pass is N or N+1
			// 0 - viewport+overlayUnder
			// 1 - first camera additive pass
			// 2 - second camera additive pass
			// N - camera N
			// N+1 - overlayOver (optional)
			return (ViewportOverlayData->IsViewportOverUsed()) ? ( CameraIndex == (TotalUsedCameras + 1) ) : ( CameraIndex == TotalUsedCameras );
		}

		if (TotalUsedCameras>1)
		{
			// Multi-cam
			// 0   - viewport+overlayUnder+camera1
			// 1   - camera2
			// N-1 - camera N
			// N   - overlayOver (optional)
			return (ViewportOverlayData->IsViewportOverUsed()) ? ( CameraIndex == TotalUsedCameras ) : ( CameraIndex == (TotalUsedCameras-1) );
		}

		// By default render single camera in one pass
		return CameraIndex==0;
	}

	inline bool IsOverlayUnderUsed() const
	{
		// Render OverlayUnder only on first pass
		return IsFirstPass() && ViewportOverlayData->IsViewportUnderUsed();
	}

	inline bool IsOverlayOverUsed() const
	{
		if (IsMultiPassRender())
		{
			// Render OverlayOver only on last additive pass
			return IsLastPass() && ViewportOverlayData->IsViewportOverUsed();
		}
		
		return ViewportOverlayData->IsViewportOverUsed();
	}

	inline int GetUsedCameraIndex() const
	{
		if (bForceMultiCameraPass)
		{
			return CameraIndex - 1;
		}

		return CameraIndex;
	}

	FMatrix GetStereoMatrix() const
	{
		FIntPoint WarpDataSrcSize = TextureWarpData.SrcTexture->GetSizeXY();
		FIntPoint WarpDataDstSize = TextureWarpData.DstTexture->GetSizeXY();

		FMatrix StereoMatrix = FMatrix::Identity;
		StereoMatrix.M[0][0] = float(TextureWarpData.SrcRect.Width()) / float(WarpDataSrcSize.X);
		StereoMatrix.M[1][1] = float(TextureWarpData.SrcRect.Height()) / float(WarpDataSrcSize.Y);
		StereoMatrix.M[3][0] = float(TextureWarpData.SrcRect.Min.X) / float(WarpDataSrcSize.X);
		StereoMatrix.M[3][1] = float(TextureWarpData.SrcRect.Min.Y) / float(WarpDataSrcSize.Y);

		return StereoMatrix;
	}

	FIntRect GetViewportRect() const
	{
		int vpPosX = TextureWarpData.DstRect.Min.X;
		int vpPosY = TextureWarpData.DstRect.Min.Y;
		int vpSizeX = TextureWarpData.DstRect.Width();
		int vpSizeY = TextureWarpData.DstRect.Height();

		return FIntRect(FIntPoint(vpPosX, vpPosY), FIntPoint(vpPosX + vpSizeX, vpPosY + vpSizeY));
	}

	EPicpShaderType GetPixelShaderType()
	{
		if (MPCDIData && MPCDIData->IsValid())
		{
			switch (MPCDIData->GetProfileType())
			{
			case IMPCDI::EMPCDIProfileType::mpcdi_2D:
			case IMPCDI::EMPCDIProfileType::mpcdi_A3D:
				return EPicpShaderType::WarpAndBlend;
#if 0
			case IMPCDI::EMPCDIProfileType::mpcdi_SL:
			case IMPCDI::EMPCDIProfileType::mpcdi_3D:
#endif
			default:
				return EPicpShaderType::Passthrough;
			};
		}
		return EPicpShaderType::Invalid;
	}

public:
	FPicpPassRenderer(IMPCDI::FTextureWarpData& InTextureWarpData, IMPCDI::FShaderInputData& InShaderInputData, FMPCDIData* InMPCDIData, FPicpProjectionOverlayViewportData* InViewportOverlayData)
		: TextureWarpData(InTextureWarpData)
		, ShaderInputData(InShaderInputData)
		, MPCDIData(InMPCDIData)
		, ViewportOverlayData(InViewportOverlayData)
	{ };

	bool GetViewportParameters(FPicpRenderPassData& RenderPassData)
	{
		if (IsFirstPass())
		{
			// Input viewport texture
			// Render only on first pass
			RenderPassData.PSParameters.InputTexture = TextureWarpData.SrcTexture;
			RenderPassData.PSParameters.InputSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

			RenderPassData.PSPermutationVector.Set<PicpShaderPermutation::FPicpShaderViewportInput>(true);
		}

		// Vertex shader viewport rect
		FIntPoint WarpDataSrcSize = TextureWarpData.SrcTexture->GetSizeXY();
		FIntPoint WarpDataDstSize = TextureWarpData.DstTexture->GetSizeXY();

		float U = TextureWarpData.SrcRect.Min.X / (float)WarpDataSrcSize.X;
		float V = TextureWarpData.SrcRect.Min.Y / (float)WarpDataSrcSize.Y;
		float USize = TextureWarpData.SrcRect.Width() / (float)WarpDataSrcSize.X;
		float VSize = TextureWarpData.SrcRect.Height() / (float)WarpDataSrcSize.Y;

		RenderPassData.VSParameters.DrawRectanglePosScaleBias = FVector4(1, 1, 0, 0);
		RenderPassData.VSParameters.DrawRectangleInvTargetSizeAndTextureSize = FVector4(1, 1, 1, 1);
		RenderPassData.VSParameters.DrawRectangleUVScaleBias = FVector4(USize, VSize, U, V);

		return true;
	}

	bool GetWarpMapParameters(FPicpRenderPassData& RenderPassData)
	{
		RenderPassData.PSParameters.ViewportTextureProjectionMatrix = ShaderInputData.UVMatrix*GetStereoMatrix();;

		if (MPCDIRegion->WarpData)
		{
			switch (MPCDIRegion->WarpData->GetWarpGeometryType())
			{
				case EWarpGeometryType::UE_StaticMesh:
				{
					// Use mesh inseat of warp texture
					RenderPassData.PSPermutationVector.Set<PicpShaderPermutation::FPicpShaderMeshWarp>(true);
					RenderPassData.VSPermutationVector.Set<PicpShaderPermutation::FPicpShaderMeshWarp>(true);

					RenderPassData.VSParameters.MeshToCaveProjectionMatrix = ShaderInputData.Frustum.MeshToCaveMatrix;

					break;
				}

				case EWarpGeometryType::PFM_Texture:
				{
					FMPCDIWarpTexture* WarpMap = static_cast<FMPCDIWarpTexture*>(MPCDIRegion->WarpData);
					RenderPassData.PSParameters.WarpMapTexture = WarpMap->TextureRHI;
					RenderPassData.PSParameters.WarpMapSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

					break;
				}
			}
		}

		return true;
	}

	bool GetWarpBlendParameters(FPicpRenderPassData& RenderPassData)
	{
		if (MPCDIRegion->AlphaMap.IsValid())
		{
			RenderPassData.PSParameters.AlphaMapTexture = MPCDIRegion->AlphaMap.TextureRHI;
			RenderPassData.PSParameters.AlphaMapSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
			RenderPassData.PSParameters.AlphaEmbeddedGamma = MPCDIRegion->AlphaMap.GetEmbeddedGamma();

			RenderPassData.PSPermutationVector.Set<PicpShaderPermutation::FPicpShaderAlphaMapBlending>(true);
		}

		if (MPCDIRegion->BetaMap.IsValid())
		{
			RenderPassData.PSParameters.BetaMapTexture = MPCDIRegion->BetaMap.TextureRHI;
			RenderPassData.PSParameters.BetaMapSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

			RenderPassData.PSPermutationVector.Set<PicpShaderPermutation::FPicpShaderBetaMapBlending>(true);
		}

		return true;
	}

	bool GetOverlayParameters(FPicpRenderPassData& RenderPassData)
	{
		RenderPassData.PSParameters.OverlayProjectionMatrix = ShaderInputData.UVMatrix;

		if (IsOverlayUnderUsed())
		{
			RenderPassData.PSParameters.OverlayUnderTexture = ViewportOverlayData->ViewportUnder.ViewportTexture;
			RenderPassData.PSParameters.OverlayUnderSampler = TStaticSamplerState<SF_AnisotropicLinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

			RenderPassData.PSPermutationVector.Set<PicpShaderPermutation::FPicpShaderOverlayUnder>(true);
		}

		if (IsOverlayOverUsed())
		{
			RenderPassData.PSParameters.OverlayOverTexture = ViewportOverlayData->ViewportOver.ViewportTexture;
			RenderPassData.PSParameters.OverlayOverSampler = TStaticSamplerState<SF_AnisotropicLinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

			RenderPassData.PSPermutationVector.Set<PicpShaderPermutation::FPicpShaderOverlayOver>(true);
		}

		return true;
	}

	bool GetCameraParameters(FPicpRenderPassData& RenderPassData)
	{
		int CurrentCameraIndex = GetUsedCameraIndex();
		if (!ViewportOverlayData->IsCameraUsed(CurrentCameraIndex))
		{
			return false;
		}

		RenderPassData.PSPermutationVector.Set<PicpShaderPermutation::FPicpShaderInnerCamera>(true);

		const FPicpProjectionOverlayCamera& Camera = ViewportOverlayData->Cameras[CurrentCameraIndex];

		static const FMatrix Game2Render(
			FPlane(0, 0, 1, 0),
			FPlane(1, 0, 0, 0),
			FPlane(0, 1, 0, 0),
			FPlane(0, 0, 0, 1));

		static const FMatrix Render2Game = Game2Render.Inverse();

		FRotationTranslationMatrix CameraTranslationMatrix(Camera.ViewRot, Camera.ViewLoc);

		FMatrix WorldToCamera = CameraTranslationMatrix.Inverse();
		FMatrix UVMatrix = WorldToCamera * Game2Render * Camera.Prj;
		FMatrix InnerCameraProjectionMatrix = UVMatrix * MPCDIData->GetTextureMatrix() *MPCDIRegion->GetRegionMatrix();

		RenderPassData.PSParameters.InnerCameraTexture = Camera.CameraTexture;
		RenderPassData.PSParameters.InnerCameraSampler = TStaticSamplerState<SF_AnisotropicLinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

		RenderPassData.PSParameters.InnerCameraProjectionMatrix = InnerCameraProjectionMatrix;
		RenderPassData.PSParameters.InnerCameraSoftEdge = Camera.SoftEdge;

		return true;
	}

	bool GetCameraChromakeyParameters(FPicpRenderPassData& RenderPassData)
	{
		int CurrentCameraIndex = GetUsedCameraIndex();
		const FPicpProjectionOverlayCamera& Camera = ViewportOverlayData->Cameras[CurrentCameraIndex];
		if (Camera.Chromakey.IsChromakeyUsed())
		{
			RenderPassData.PSParameters.ChromakeyCameraTexture = Camera.Chromakey.ChromakeyTexture;
			RenderPassData.PSParameters.ChromakeyCameraSampler = TStaticSamplerState<SF_AnisotropicLinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

			RenderPassData.PSPermutationVector.Set<PicpShaderPermutation::FPicpShaderChromakey>(true);

			return true;
		}

		return false;
	}

	bool GetCameraChromakeyMarkerParameters(FPicpRenderPassData& RenderPassData)
	{
		int CurrentCameraIndex = GetUsedCameraIndex();
		const FPicpProjectionOverlayCamera& Camera = ViewportOverlayData->Cameras[CurrentCameraIndex];

		if (Camera.Chromakey.IsChromakeyMarkerUsed())
		{
			RenderPassData.PSParameters.ChromakeyMarkerTexture = Camera.Chromakey.ChromakeyMarkerTexture;
			RenderPassData.PSParameters.ChromakeyMarkerSampler = TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();

			RenderPassData.PSParameters.ChromakeyMarkerScale = Camera.Chromakey.ChromakeyMarkerScale;

			RenderPassData.PSPermutationVector.Set<PicpShaderPermutation::FPicpShaderChromakeyMarker>(true);
			RenderPassData.PSPermutationVector.Set<PicpShaderPermutation::FPicpShaderChromakeyMarkerMeshUV>(Camera.Chromakey.bChromakeyMarkerUseMeshUV);

			return true;
		}

		return false;
	}

	void InitRenderPass(FPicpRenderPassData& RenderPassData)
	{
		// Forward input viewport
		GetViewportParameters(RenderPassData);

		// Configure mutable pixel shader:
		if (MPCDIRegion)
		{
			GetWarpMapParameters(RenderPassData);

			if (!bIsBlendDisabled)
			{
				GetWarpBlendParameters(RenderPassData);
			}
		}

		if (ViewportOverlayData)
		{
			GetOverlayParameters(RenderPassData);
			if (GetCameraParameters(RenderPassData))
			{
				// Chromakey inside cam:
				if (GetCameraChromakeyParameters(RenderPassData))
				{
					//Support chromakey markers
					GetCameraChromakeyMarkerParameters(RenderPassData);
				}
			}
		}
	}
	
	bool RenderPassthrough(FRHICommandListImmediate& RHICmdList, FPicpRenderPassData& RenderPassData)
	{
		if (!MPCDIRegion)
		{
			return false;
		}

		// Get mutable shaders:
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FPicpWarpVS> VertexShader(ShaderMap, RenderPassData.VSPermutationVector);
		TShaderMapRef<FPicpPassthroughPS> PixelShader(ShaderMap);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		{
			// Set the graphic pipeline state.
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);

			MPCDIRegion->WarpData->BeginRender(RHICmdList, GraphicsPSOInit);

			// Setup graphics pipeline
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Never>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();

			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			GraphicsPSOInit.BlendState = TStaticBlendState <>::GetRHI();
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
		}

		// Setup shaders data
		SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), RenderPassData.VSParameters);
		SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), RenderPassData.PSParameters);

		MPCDIRegion->WarpData->FinishRender(RHICmdList);
		return true;
	}

	bool RenderCurentPass(FRHICommandListImmediate& RHICmdList, FPicpRenderPassData& RenderPassData)
	{
		if (!MPCDIRegion)
		{
			return false;
		}
		
		// Get mutable shaders:
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FPicpWarpVS> VertexShader(ShaderMap, RenderPassData.VSPermutationVector);
		TShaderMapRef<FPicpWarpPS> PixelShader(ShaderMap, RenderPassData.PSPermutationVector);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		{
			// Set the graphic pipeline state.
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThreadFlushResources);

			MPCDIRegion->WarpData->BeginRender(RHICmdList, GraphicsPSOInit);

			// Setup graphics pipeline
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Never>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();

			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI  = PixelShader.GetPixelShader();

			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			if (IsFirstPass())
			{
				// First pass always override old viewport image
				GraphicsPSOInit.BlendState = TStaticBlendState <>::GetRHI();
			}
			else
			{
				if (RenderPassData.PSPermutationVector.Get<PicpShaderPermutation::FPicpShaderInnerCamera>())
				{
					// Render additive camera frame
					GraphicsPSOInit.BlendState = TStaticBlendState <CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_InverseDestAlpha, BF_One>::GetRHI();
				}
				else
				{
					// Render additive overlay frame
					GraphicsPSOInit.BlendState = TStaticBlendState <CW_RGBA, BO_Add, BF_InverseSourceAlpha, BF_SourceAlpha, BO_Add, BF_InverseDestAlpha, BF_One>::GetRHI();
				}
			}

			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
		}

		// Setup shaders data
		SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), RenderPassData.VSParameters);
		SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), RenderPassData.PSParameters);

		MPCDIRegion->WarpData->FinishRender(RHICmdList);

		return true;
	}

	bool Initialize()
	{
		if (MPCDIData && MPCDIData->IsValid())
		{
			MPCDIRegion = MPCDIData->GetRegion(ShaderInputData.RegionLocator);
		}

		if (MPCDIRegion == nullptr)
		{
			//Handle error
			return false;
		}

		// Map ProfileType to shader type to use
		PixelShaderType = GetPixelShaderType();
		if (EPicpShaderType::Invalid == PixelShaderType)
		{
			//@todo: handle error
			return false;
		}

		bIsBlendDisabled = !MPCDIRegion->AlphaMap.IsInitialized();
		bForceMultiCameraPass = false;

		const EVarPicpMPCDIShaderType ShaderType = (EVarPicpMPCDIShaderType)CVarPicpMPCDIShaderType.GetValueOnAnyThread();

		switch (ShaderType)
		{
		case EVarPicpMPCDIShaderType::DefaultNoBlend:
			bIsBlendDisabled = true;
			break;

		case EVarPicpMPCDIShaderType::Passthrough:
			PixelShaderType = EPicpShaderType::Passthrough;
			break;

		case EVarPicpMPCDIShaderType::ForceMultiPassRender:
			bForceMultiCameraPass = true;
			break;

		case EVarPicpMPCDIShaderType::Disable:
			return false;
			break;
		};

		return true;
	}
	
	bool Render(FRHICommandListImmediate& RHICmdList)
	{
		// Setup viewport before render
		FIntRect MPCDIViewportRect = GetViewportRect();
		RHICmdList.SetViewport(MPCDIViewportRect.Min.X, MPCDIViewportRect.Min.Y, 0.0f, MPCDIViewportRect.Max.X, MPCDIViewportRect.Max.Y, 1.0f);

		// Render
		switch (PixelShaderType)
		{
			case EPicpShaderType::Passthrough:
			{
				ShaderInputData.UVMatrix = FMatrix::Identity;
				FPicpRenderPassData RenderPassData;
				InitRenderPass(RenderPassData);
				RenderPassthrough(RHICmdList, RenderPassData);
				break;
			}

			case EPicpShaderType::WarpAndBlend:
			{
				ShaderInputData.UVMatrix = ShaderInputData.Frustum.UVMatrix*MPCDIData->GetTextureMatrix()*MPCDIRegion->GetRegionMatrix();
				{
					// Render Full Picp Warp&Blend
					while (true)
					{
						{
							// Build and draw render-pass:
							FPicpRenderPassData RenderPassData;
							InitRenderPass(RenderPassData);
							RenderCurentPass(RHICmdList, RenderPassData);
						}

						if (IsLastPass())
						{
							break;
						}

						// Switch to next render-pass
						CameraIndex++;
					}
				}

				break;
			}
		}

		return true;
	}
};


DECLARE_GPU_STAT_NAMED(DisplayClusterPicpMPCDIWarpBlend, PicpMpcdiStrings::RenderPassHint);

bool FPicpMPCDIShader::ApplyWarpBlend(FRHICommandListImmediate& RHICmdList, IMPCDI::FTextureWarpData& InTextureWarpData, IMPCDI::FShaderInputData& InShaderInputData, FMPCDIData* InMPCDIData, FPicpProjectionOverlayViewportData* InViewportOverlayData)
{
	check(IsInRenderingThread());

	FPicpPassRenderer PicpPassRenderer(InTextureWarpData, InShaderInputData, InMPCDIData, InViewportOverlayData);

	if (!PicpPassRenderer.Initialize())
	{
		return false;
	}

	SCOPED_GPU_STAT(RHICmdList, DisplayClusterPicpMPCDIWarpBlend);
	SCOPED_DRAW_EVENTF(RHICmdList, DisplayClusterPicpMPCDIWarpBlend, PicpMpcdiStrings::RenderPassName);

	// Do single warp render pass
	bool bIsRenderSuccess = false;
	FRHIRenderPassInfo RPInfo(InTextureWarpData.DstTexture, ERenderTargetActions::Load_Store);
	RHICmdList.BeginRenderPass(RPInfo, PicpMpcdiStrings::RenderPassName);
	bIsRenderSuccess = PicpPassRenderer.Render(RHICmdList);
	RHICmdList.EndRenderPass();

	return bIsRenderSuccess;
};
