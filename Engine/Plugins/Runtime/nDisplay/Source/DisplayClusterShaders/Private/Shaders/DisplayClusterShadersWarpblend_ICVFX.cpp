// Copyright Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterShadersWarpblend_ICVFX.h"

#include "RenderResource.h"
#include "CommonRenderResources.h"
#include "PixelShaderUtils.h"

#include "Shader.h"
#include "GlobalShader.h"

#include "ShaderParameters.h"
#include "ShaderParameterUtils.h"
#include "ShaderParameterStruct.h"
#include "ShaderPermutation.h"

#include "Render/Containers/DisplayClusterRender_MeshComponent.h"
#include "Render/Containers/DisplayClusterRender_MeshComponentProxy.h"

#include "ShaderParameters/DisplayClusterShaderParameters_WarpBlend.h"
#include "ShaderParameters/DisplayClusterShaderParameters_ICVFX.h"

#include "WarpBlend/IDisplayClusterWarpBlend.h"

#define ICVFX_ShaderFileName "/Plugin/nDisplay/Private/MPCDIOverlayShaders.usf"

enum class EVarIcvfxMPCDIShaderType : uint8
{
	Default = 0,
	DefaultNoBlend,
	Passthrough,
	ForceMultiPassRender,
	Disable,
};

static TAutoConsoleVariable<int32> CVarIcvfxMPCDIShaderType(
	TEXT("nDisplay.render.icvfx.shader"),
	(int)EVarIcvfxMPCDIShaderType::Default,
	TEXT("Select shader for icvfx:\n")
	TEXT(" 0: Warp shader (used by default)\n")
	TEXT(" 1: Warp shader with disabled blend maps\n")
	TEXT(" 2: Passthrough shader\n")
	TEXT(" 3: Force Multi-Pass render shaders\n")
	TEXT(" 4: Disable all warp shaders\n"),
	ECVF_RenderThreadSafe
);

enum class EIcvfxShaderType : uint8
{
	Passthrough,  // Viewport frustum (no warpblend, only frustum math)
	WarpAndBlend, // Pure mpcdi warpblend for viewport
	Invalid,
};

namespace IcvfxShaderPermutation
{
	// Shared permutation for picp warp
	class FIcvfxShaderViewportInput    : SHADER_PERMUTATION_BOOL("VIEWPORT_INPUT");

	class FIcvfxShaderOverlayUnder     : SHADER_PERMUTATION_BOOL("OVERLAY_UNDER");
	class FIcvfxShaderOverlayOver      : SHADER_PERMUTATION_BOOL("OVERLAY_OVER");
	class FIcvfxShaderOverlayAlpha     : SHADER_PERMUTATION_BOOL("OVERLAY_ALPHA");

	class FIcvfxShaderInnerCamera      : SHADER_PERMUTATION_BOOL("INNER_CAMERA");

	class FIcvfxShaderChromakey           : SHADER_PERMUTATION_BOOL("CHROMAKEY");
	class FIcvfxShaderChromakeyFrameColor : SHADER_PERMUTATION_BOOL("CHROMAKEYFRAMECOLOR");

	class FIcvfxShaderChromakeyMarker  : SHADER_PERMUTATION_BOOL("CHROMAKEY_MARKER");

	class FIcvfxShaderAlphaMapBlending : SHADER_PERMUTATION_BOOL("ALPHAMAP_BLENDING");
	class FIcvfxShaderBetaMapBlending  : SHADER_PERMUTATION_BOOL("BETAMAP_BLENDING");

	class FIcvfxShaderMeshWarp         : SHADER_PERMUTATION_BOOL("MESH_WARP");

	using FCommonVSDomain = TShaderPermutationDomain<FIcvfxShaderMeshWarp>;

	using FCommonPSDomain = TShaderPermutationDomain<
		FIcvfxShaderViewportInput,
		FIcvfxShaderOverlayUnder,
		FIcvfxShaderOverlayOver,
		FIcvfxShaderOverlayAlpha,

		FIcvfxShaderInnerCamera,
		FIcvfxShaderChromakey,
		FIcvfxShaderChromakeyFrameColor,
		FIcvfxShaderChromakeyMarker,

		FIcvfxShaderAlphaMapBlending,
		FIcvfxShaderBetaMapBlending,
		FIcvfxShaderMeshWarp
	>;
	
	bool ShouldCompileCommonPSPermutation(const FGlobalShaderPermutationParameters& Parameters, const FCommonPSDomain& PermutationVector)
	{
		if (PermutationVector.Get<FIcvfxShaderOverlayAlpha>())
		{
			if (!PermutationVector.Get<FIcvfxShaderOverlayUnder>() && !PermutationVector.Get<FIcvfxShaderOverlayOver>())
			{
				return false;
			}
		}

		if (!PermutationVector.Get<FIcvfxShaderMeshWarp>())
		{
			if (PermutationVector.Get<FIcvfxShaderChromakey>() || PermutationVector.Get<FIcvfxShaderChromakeyFrameColor>())
			{
				return false;
			}
		}

		if (PermutationVector.Get<FIcvfxShaderChromakey>() && PermutationVector.Get<FIcvfxShaderChromakeyFrameColor>())
		{
			return false;
		}

		if (!PermutationVector.Get<FIcvfxShaderViewportInput>())
		{
			if (PermutationVector.Get<FIcvfxShaderOverlayUnder>() || (PermutationVector.Get<FIcvfxShaderInnerCamera>() == PermutationVector.Get<FIcvfxShaderOverlayOver>()))
			{
				return false;
			}
		}

		if (!PermutationVector.Get<FIcvfxShaderInnerCamera>())
		{
			if (PermutationVector.Get<FIcvfxShaderChromakey>() || PermutationVector.Get<FIcvfxShaderChromakeyFrameColor>())
		{
			return false;
		}
		}

		if (PermutationVector.Get<FIcvfxShaderChromakeyMarker>())
		{
			if (!PermutationVector.Get<FIcvfxShaderChromakey>() && !PermutationVector.Get<FIcvfxShaderChromakeyFrameColor>())
		{
			return false;
		}
		}

		if (!PermutationVector.Get<FIcvfxShaderAlphaMapBlending>() && PermutationVector.Get<FIcvfxShaderBetaMapBlending>())
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

BEGIN_SHADER_PARAMETER_STRUCT(FIcvfxVertexShaderParameters, )
	SHADER_PARAMETER(FVector4, DrawRectanglePosScaleBias)
	SHADER_PARAMETER(FVector4, DrawRectangleInvTargetSizeAndTextureSize)
	SHADER_PARAMETER(FVector4, DrawRectangleUVScaleBias)

	SHADER_PARAMETER(FMatrix, MeshToStageProjectionMatrix)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FIcvfxPixelShaderParameters, )
	SHADER_PARAMETER_TEXTURE(Texture2D, InputTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, WarpMapTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, AlphaMapTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, BetaMapTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, OverlayTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, OverlayAlphaTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, InnerCameraTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, ChromakeyCameraTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, ChromakeyMarkerTexture)

	SHADER_PARAMETER_SAMPLER(SamplerState, InputSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, WarpMapSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, AlphaMapSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, BetaMapSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, OverlaySampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, OverlayAlphaSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, InnerCameraSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, ChromakeyCameraSampler)
	SHADER_PARAMETER_SAMPLER(SamplerState, ChromakeyMarkerSampler)

	SHADER_PARAMETER(FMatrix, ViewportTextureProjectionMatrix)
	SHADER_PARAMETER(FMatrix, OverlayProjectionMatrix)
	SHADER_PARAMETER(FMatrix, InnerCameraProjectionMatrix)

	SHADER_PARAMETER(float, AlphaEmbeddedGamma)

	SHADER_PARAMETER(FVector4, InnerCameraSoftEdge)

	SHADER_PARAMETER(FVector4, ChromakeyColor)
	SHADER_PARAMETER(FVector4, ChromakeyMarkerColor)

	SHADER_PARAMETER(float, ChromakeyMarkerScale)
	SHADER_PARAMETER(float, ChromakeyMarkerDistance)
	SHADER_PARAMETER(FVector2D, ChromakeyMarkerOffset)
END_SHADER_PARAMETER_STRUCT()

class FIcvfxWarpVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FIcvfxWarpVS);
	SHADER_USE_PARAMETER_STRUCT(FIcvfxWarpVS, FGlobalShader);
		
	using FPermutationDomain = IcvfxShaderPermutation::FCommonVSDomain;
	using FParameters = FIcvfxVertexShaderParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IcvfxShaderPermutation::ShouldCompileCommonVSPermutation(Parameters, FPermutationDomain(Parameters.PermutationId));
	}
};

IMPLEMENT_GLOBAL_SHADER(FIcvfxWarpVS, ICVFX_ShaderFileName, "IcvfxWarpVS", SF_Vertex);

class FIcvfxWarpPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FIcvfxWarpPS);
	SHADER_USE_PARAMETER_STRUCT(FIcvfxWarpPS, FGlobalShader);

	using FPermutationDomain = IcvfxShaderPermutation::FCommonPSDomain;
	using FParameters = FIcvfxPixelShaderParameters;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IcvfxShaderPermutation::ShouldCompileCommonPSPermutation(Parameters, FPermutationDomain(Parameters.PermutationId));
	}
};

IMPLEMENT_GLOBAL_SHADER(FIcvfxWarpPS, ICVFX_ShaderFileName, "IcvfxWarpPS", SF_Pixel);

class FIcvfxPassthroughPS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FIcvfxPassthroughPS);
	SHADER_USE_PARAMETER_STRUCT(FIcvfxPassthroughPS, FGlobalShader);
		
	using FParameters = FIcvfxPixelShaderParameters;
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return true;
	}
};

IMPLEMENT_GLOBAL_SHADER(FIcvfxPassthroughPS, ICVFX_ShaderFileName, "Passthrough_PS", SF_Pixel);


struct FIcvfxRenderPassData
{
	IcvfxShaderPermutation::FCommonVSDomain VSPermutationVector;
	FIcvfxVertexShaderParameters            VSParameters;

	IcvfxShaderPermutation::FCommonPSDomain PSPermutationVector;
	FIcvfxPixelShaderParameters             PSParameters;
};

class FIcvfxPassRenderer
{
public:
	FIcvfxPassRenderer(const FDisplayClusterShaderParameters_WarpBlend& InWarpBlendParameters, const FDisplayClusterShaderParameters_ICVFX& InICVFXParameters)
		: WarpBlendParameters(InWarpBlendParameters)
		, ICVFXParameters(InICVFXParameters)
	{}

private:
	const FDisplayClusterShaderParameters_WarpBlend& WarpBlendParameters;
	const FDisplayClusterShaderParameters_ICVFX& ICVFXParameters;

	EIcvfxShaderType PixelShaderType;

	FMatrix LocalUVMatrix;

	bool bIsBlendDisabled = false;
	bool bForceMultiCameraPass = false;
	int CameraIndex = 0;

private:
	inline bool IsMultiPassRender() const
	{
		return bForceMultiCameraPass || ICVFXParameters.IsMultiCamerasUsed();
	}

	inline bool IsFirstPass() const
	{
		return CameraIndex == 0;
	}

	inline bool IsLastPass() const
	{
		int TotalUsedCameras = ICVFXParameters.Cameras.Num();

		if (bForceMultiCameraPass)
		{
			// Last pass is N or N+1
			// 0 - viewport+overlayUnder
			// 1 - first camera additive pass
			// 2 - second camera additive pass
			// N - camera N
			// N+1 - overlayOver (optional)
			return (ICVFXParameters.IsLightcardOverUsed()) ? ( CameraIndex == (TotalUsedCameras + 1) ) : ( CameraIndex == TotalUsedCameras );
		}

		if (TotalUsedCameras>1)
		{
			// Multi-cam
			// 0   - viewport+overlayUnder+camera1
			// 1   - camera2
			// N-1 - camera N
			// N   - overlayOver (optional)
			return (ICVFXParameters.IsLightcardOverUsed()) ? ( CameraIndex == TotalUsedCameras ) : ( CameraIndex == (TotalUsedCameras-1) );
		}

		// By default render single camera in one pass
		return CameraIndex==0;
	}

	inline bool IsOverlayUnderUsed() const
	{
		// Render OverlayUnder only on first pass
		return IsFirstPass() && ICVFXParameters.IsLightcardUnderUsed();
	}

	inline bool IsOverlayOverUsed() const
	{
		if (IsMultiPassRender())
		{
			// Render OverlayOver only on last additive pass
			return IsLastPass() && ICVFXParameters.IsLightcardOverUsed();
		}
		
		return ICVFXParameters.IsLightcardOverUsed();
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
		FIntPoint WarpDataSrcSize = WarpBlendParameters.Src.Texture->GetSizeXY();
		FIntPoint WarpDataDstSize = WarpBlendParameters.Dest.Texture->GetSizeXY();

		FMatrix StereoMatrix = FMatrix::Identity;
		StereoMatrix.M[0][0] = float(WarpBlendParameters.Src.Rect.Width()) / float(WarpDataSrcSize.X);
		StereoMatrix.M[1][1] = float(WarpBlendParameters.Src.Rect.Height()) / float(WarpDataSrcSize.Y);
		StereoMatrix.M[3][0] = float(WarpBlendParameters.Src.Rect.Min.X) / float(WarpDataSrcSize.X);
		StereoMatrix.M[3][1] = float(WarpBlendParameters.Src.Rect.Min.Y) / float(WarpDataSrcSize.Y);

		return StereoMatrix;
	}

	FIntRect GetViewportRect() const
	{
		int vpPosX = WarpBlendParameters.Dest.Rect.Min.X;
		int vpPosY = WarpBlendParameters.Dest.Rect.Min.Y;
		int vpSizeX = WarpBlendParameters.Dest.Rect.Width();
		int vpSizeY = WarpBlendParameters.Dest.Rect.Height();

		return FIntRect(FIntPoint(vpPosX, vpPosY), FIntPoint(vpPosX + vpSizeX, vpPosY + vpSizeY));
	}

	EIcvfxShaderType GetPixelShaderType()
	{
		if (WarpBlendParameters.WarpInterface.IsValid())
		{
			switch (WarpBlendParameters.WarpInterface->GetWarpProfileType())
			{
			case EDisplayClusterWarpProfileType::warp_2D:
			case EDisplayClusterWarpProfileType::warp_A3D:
				return EIcvfxShaderType::WarpAndBlend;
#if 0
			case EDisplayClusterWarpProfileType::warp_SL:
			case EDisplayClusterWarpProfileType::warp_3D:
#endif
			default:
				return EIcvfxShaderType::Passthrough;
			};
		}
		return EIcvfxShaderType::Invalid;
	}

public:

	bool GetViewportParameters(FIcvfxRenderPassData& RenderPassData)
	{
		if (IsFirstPass())
		{
			// Input viewport texture
			// Render only on first pass
			RenderPassData.PSParameters.InputTexture = WarpBlendParameters.Src.Texture;
			RenderPassData.PSParameters.InputSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

			RenderPassData.PSPermutationVector.Set<IcvfxShaderPermutation::FIcvfxShaderViewportInput>(true);
		}

		// Vertex shader viewport rect
		FIntPoint WarpDataSrcSize = WarpBlendParameters.Src.Texture->GetSizeXY();
		FIntPoint WarpDataDstSize = WarpBlendParameters.Dest.Texture->GetSizeXY();

		float U = WarpBlendParameters.Src.Rect.Min.X / (float)WarpDataSrcSize.X;
		float V = WarpBlendParameters.Src.Rect.Min.Y / (float)WarpDataSrcSize.Y;
		float USize = WarpBlendParameters.Src.Rect.Width() / (float)WarpDataSrcSize.X;
		float VSize = WarpBlendParameters.Src.Rect.Height() / (float)WarpDataSrcSize.Y;

		RenderPassData.VSParameters.DrawRectanglePosScaleBias = FVector4(1, 1, 0, 0);
		RenderPassData.VSParameters.DrawRectangleInvTargetSizeAndTextureSize = FVector4(1, 1, 1, 1);
		RenderPassData.VSParameters.DrawRectangleUVScaleBias = FVector4(USize, VSize, U, V);

		return true;
	}

	bool GetWarpMapParameters(FIcvfxRenderPassData& RenderPassData)
	{
		RenderPassData.PSParameters.ViewportTextureProjectionMatrix = LocalUVMatrix * GetStereoMatrix();

		if (WarpBlendParameters.WarpInterface.IsValid())
		{
			switch (WarpBlendParameters.WarpInterface->GetWarpGeometryType())
			{
				case EDisplayClusterWarpGeometryType::WarpMesh:
				{
					// Use mesh inseat of warp texture
					RenderPassData.PSPermutationVector.Set<IcvfxShaderPermutation::FIcvfxShaderMeshWarp>(true);
					RenderPassData.VSPermutationVector.Set<IcvfxShaderPermutation::FIcvfxShaderMeshWarp>(true);

					RenderPassData.VSParameters.MeshToStageProjectionMatrix = WarpBlendParameters.Context.MeshToStageMatrix;

					break;
				}

				case EDisplayClusterWarpGeometryType::WarpMap:
				{
					FRHITexture* WarpMap = WarpBlendParameters.WarpInterface->GetTexture(EDisplayClusterWarpBlendTextureType::WarpMap);
					if (WarpMap == nullptr)
					{
						return false;
					}
					
					RenderPassData.PSParameters.WarpMapTexture = WarpMap;
					RenderPassData.PSParameters.WarpMapSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

					break;
				}
			}

			return true;
		}

		return false;
	}

	bool GetWarpBlendParameters(FIcvfxRenderPassData& RenderPassData)
	{
		if (WarpBlendParameters.WarpInterface.IsValid())
		{
			FRHITexture* AlphaMap = WarpBlendParameters.WarpInterface->GetTexture(EDisplayClusterWarpBlendTextureType::AlphaMap);
			if (AlphaMap)
			{
				RenderPassData.PSParameters.AlphaMapTexture = AlphaMap;
				RenderPassData.PSParameters.AlphaMapSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				RenderPassData.PSParameters.AlphaEmbeddedGamma = WarpBlendParameters.WarpInterface->GetAlphaMapEmbeddedGamma();

				RenderPassData.PSPermutationVector.Set<IcvfxShaderPermutation::FIcvfxShaderAlphaMapBlending>(true);
			}

			FRHITexture* BetaMap = WarpBlendParameters.WarpInterface->GetTexture(EDisplayClusterWarpBlendTextureType::BetaMap);
			if (BetaMap)
			{
				RenderPassData.PSParameters.BetaMapTexture = BetaMap;
				RenderPassData.PSParameters.BetaMapSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

				RenderPassData.PSPermutationVector.Set<IcvfxShaderPermutation::FIcvfxShaderBetaMapBlending>(true);
			}

			return true;
		}

		return false;
	}

	bool GetOverlayParameters(FIcvfxRenderPassData& RenderPassData)
	{
		RenderPassData.PSParameters.OverlayProjectionMatrix = LocalUVMatrix;

		if (IsOverlayUnderUsed() || IsOverlayOverUsed())
		{

			if (ICVFXParameters.Lightcard_OCIO.IsValid())
			{
				// use OCIO with separated alpha lightmap texture
				RenderPassData.PSParameters.OverlaySampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				RenderPassData.PSParameters.OverlayTexture = ICVFXParameters.Lightcard_OCIO.Texture;

				RenderPassData.PSParameters.OverlayAlphaSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				RenderPassData.PSParameters.OverlayAlphaTexture = ICVFXParameters.Lightcard.Texture;

				RenderPassData.PSPermutationVector.Set<IcvfxShaderPermutation::FIcvfxShaderOverlayAlpha>(true);
			}
			else
			{
				RenderPassData.PSParameters.OverlaySampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
				RenderPassData.PSParameters.OverlayTexture = ICVFXParameters.Lightcard.Texture;
			}
			if (IsOverlayUnderUsed())
			{
				RenderPassData.PSPermutationVector.Set<IcvfxShaderPermutation::FIcvfxShaderOverlayUnder>(true);
			}
			if (IsOverlayOverUsed())
			{
				RenderPassData.PSPermutationVector.Set<IcvfxShaderPermutation::FIcvfxShaderOverlayOver>(true);
			}
		}

		return true;
	}

	bool GetCameraParameters(FIcvfxRenderPassData& RenderPassData)
	{
		int CurrentCameraIndex = GetUsedCameraIndex();
		if (!ICVFXParameters.IsCameraUsed(CurrentCameraIndex))
		{
			return false;
		}

		RenderPassData.PSPermutationVector.Set<IcvfxShaderPermutation::FIcvfxShaderInnerCamera>(true);

		const FDisplayClusterShaderParameters_ICVFX::FCameraSettings& Camera = ICVFXParameters.Cameras[CurrentCameraIndex];

		static const FMatrix Game2Render(
			FPlane(0, 0, 1, 0),
			FPlane(1, 0, 0, 0),
			FPlane(0, 1, 0, 0),
			FPlane(0, 0, 0, 1));

		static const FMatrix Render2Game = Game2Render.Inverse();

		FRotationTranslationMatrix CameraTranslationMatrix(Camera.CameraViewRotation, Camera.CameraViewLocation);

		FMatrix WorldToCamera = CameraTranslationMatrix.Inverse();
		FMatrix UVMatrix = WorldToCamera * Game2Render * Camera.CameraPrjMatrix;
		FMatrix InnerCameraProjectionMatrix = UVMatrix * WarpBlendParameters.Context.TextureMatrix * WarpBlendParameters.Context.RegionMatrix;

		RenderPassData.PSParameters.InnerCameraTexture = Camera.Resource.Texture;
		RenderPassData.PSParameters.InnerCameraSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

		RenderPassData.PSParameters.InnerCameraProjectionMatrix = InnerCameraProjectionMatrix;
		RenderPassData.PSParameters.InnerCameraSoftEdge = Camera.SoftEdge;

		return true;
	}

	bool GetCameraChromakeyParameters(FIcvfxRenderPassData& RenderPassData)
	{
		int CurrentCameraIndex = GetUsedCameraIndex();
		const FDisplayClusterShaderParameters_ICVFX::FCameraSettings& Camera = ICVFXParameters.Cameras[CurrentCameraIndex];
		
		switch (Camera.ChromakeySource)
		{
		case EDisplayClusterShaderParametersICVFX_ChromakeySource::ChromakeyLayers:
		{
			RenderPassData.PSParameters.ChromakeyColor = Camera.ChromakeyColor;
			RenderPassData.PSParameters.ChromakeyCameraTexture = Camera.Chromakey.Texture;
			RenderPassData.PSParameters.ChromakeyCameraSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

			RenderPassData.PSPermutationVector.Set<IcvfxShaderPermutation::FIcvfxShaderChromakey>(true);

			return true;
		}

		case EDisplayClusterShaderParametersICVFX_ChromakeySource::FrameColor:
		{
			RenderPassData.PSParameters.ChromakeyColor = Camera.ChromakeyColor;
			RenderPassData.PSPermutationVector.Set<IcvfxShaderPermutation::FIcvfxShaderChromakeyFrameColor>(true);

			return true;
		}

		case EDisplayClusterShaderParametersICVFX_ChromakeySource::Disabled:
		default:
			break;
		}

		return false;
	}

	bool GetCameraChromakeyMarkerParameters(FIcvfxRenderPassData& RenderPassData)
	{
		int CurrentCameraIndex = GetUsedCameraIndex();
		const FDisplayClusterShaderParameters_ICVFX::FCameraSettings& Camera = ICVFXParameters.Cameras[CurrentCameraIndex];

		if (Camera.IsChromakeyMarkerUsed())
		{
			RenderPassData.PSParameters.ChromakeyMarkerColor = Camera.ChromakeyMarkersColor;

			RenderPassData.PSParameters.ChromakeyMarkerTexture = Camera.ChromakeMarkerTextureRHI;
			RenderPassData.PSParameters.ChromakeyMarkerSampler = TStaticSamplerState<SF_Trilinear, AM_Wrap, AM_Wrap, AM_Wrap>::GetRHI();

			RenderPassData.PSParameters.ChromakeyMarkerScale    = Camera.ChromakeyMarkersScale;
			RenderPassData.PSParameters.ChromakeyMarkerDistance = Camera.ChromakeyMarkersDistance;
			RenderPassData.PSParameters.ChromakeyMarkerOffset   = Camera.ChromakeyMarkersOffset;

			RenderPassData.PSPermutationVector.Set<IcvfxShaderPermutation::FIcvfxShaderChromakeyMarker>(true);
			return true;
		}

		return false;
	}

	void InitRenderPass(FIcvfxRenderPassData& RenderPassData)
	{
		// Forward input viewport
		GetViewportParameters(RenderPassData);

		// Configure mutable pixel shader:
		if (WarpBlendParameters.WarpInterface.IsValid())
		{
			GetWarpMapParameters(RenderPassData);

			if (!bIsBlendDisabled)
			{
				GetWarpBlendParameters(RenderPassData);
			}

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

	bool ImplBeginRenderPass(FRHICommandListImmediate& RHICmdList, FGraphicsPipelineStateInitializer& GraphicsPSOInit) const
	{
		if (WarpBlendParameters.WarpInterface.IsValid())
		{
			switch (WarpBlendParameters.WarpInterface->GetWarpGeometryType())
			{
			case EDisplayClusterWarpGeometryType::WarpMap:
				GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
				return true;

			case EDisplayClusterWarpGeometryType::WarpMesh:
			{
				const FDisplayClusterRender_MeshComponent* DCWarpMeshComponent = WarpBlendParameters.WarpInterface->GetWarpMesh();
				if (DCWarpMeshComponent)
				{
					const FDisplayClusterRender_MeshComponentProxy* WarpMesh = DCWarpMeshComponent->GetProxy();
					if (WarpMesh)
					{
						return WarpMesh->BeginRender_RenderThread(RHICmdList, GraphicsPSOInit);
					}
				}
				break;
			}
			default:
				break;
			}
		}

		return false;
	}

	bool ImplFinishRenderPass(FRHICommandListImmediate& RHICmdList) const
	{
		if (WarpBlendParameters.WarpInterface.IsValid())
		{
			switch (WarpBlendParameters.WarpInterface->GetWarpGeometryType())
			{
			case EDisplayClusterWarpGeometryType::WarpMap:
				// Render quad
				FPixelShaderUtils::DrawFullscreenQuad(RHICmdList, 1);
				return true;

			case EDisplayClusterWarpGeometryType::WarpMesh:
			{
				const FDisplayClusterRender_MeshComponent* DCWarpMeshComponent = WarpBlendParameters.WarpInterface->GetWarpMesh();
				if (DCWarpMeshComponent)
				{
					const FDisplayClusterRender_MeshComponentProxy* WarpMesh = DCWarpMeshComponent->GetProxy();
					if (WarpMesh)
					{
						return WarpMesh->FinishRender_RenderThread(RHICmdList);
					}
				}
				break;
			}
			default:
				break;
			}
		}

		return false;
	}
	
	bool RenderPassthrough(FRHICommandListImmediate& RHICmdList, FIcvfxRenderPassData& RenderPassData)
	{
		if (WarpBlendParameters.WarpInterface.IsValid() == false)
		{
			return false;
		}

		// Get mutable shaders:
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FIcvfxWarpVS> VertexShader(ShaderMap, RenderPassData.VSPermutationVector);
		TShaderMapRef<FIcvfxPassthroughPS> PixelShader(ShaderMap);


		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		// Set the graphic pipeline state.
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);

		if (ImplBeginRenderPass(RHICmdList, GraphicsPSOInit))
		{

			// Setup graphics pipeline
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Never>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();

			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			GraphicsPSOInit.BlendState = TStaticBlendState <>::GetRHI();
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

			// Setup shaders data
			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), RenderPassData.VSParameters);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), RenderPassData.PSParameters);

			ImplFinishRenderPass(RHICmdList);

			return true;
		}

		return false;
	}

	bool RenderCurentPass(FRHICommandListImmediate& RHICmdList, FIcvfxRenderPassData& RenderPassData)
	{
		if (WarpBlendParameters.WarpInterface.IsValid() == false)
		{
			return false;
		}
		
		// Get mutable shaders:
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FIcvfxWarpVS> VertexShader(ShaderMap, RenderPassData.VSPermutationVector);
		TShaderMapRef<FIcvfxWarpPS> PixelShader(ShaderMap, RenderPassData.PSPermutationVector);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		// Set the graphic pipeline state.
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		if (ImplBeginRenderPass(RHICmdList, GraphicsPSOInit))
		{
			// Setup graphics pipeline
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Never>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();

			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			if (IsFirstPass())
			{
				// First pass always override old viewport image
				GraphicsPSOInit.BlendState = TStaticBlendState <>::GetRHI();
			}
			else
			{
				if (RenderPassData.PSPermutationVector.Get<IcvfxShaderPermutation::FIcvfxShaderInnerCamera>())
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

			// Setup shaders data
			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), RenderPassData.VSParameters);
			SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), RenderPassData.PSParameters);

			ImplFinishRenderPass(RHICmdList);

			return true;
		}

		return false;
	}

	bool Initialize()
	{
		if (WarpBlendParameters.WarpInterface.IsValid() == false)
		{
			return false;
		}

		// Map ProfileType to shader type to use
		PixelShaderType = GetPixelShaderType();
		if (EIcvfxShaderType::Invalid == PixelShaderType)
		{
			return false;
		}

		bIsBlendDisabled = WarpBlendParameters.WarpInterface->GetTexture(EDisplayClusterWarpBlendTextureType::AlphaMap) == nullptr;
		bForceMultiCameraPass = false;

		const EVarIcvfxMPCDIShaderType ShaderType = (EVarIcvfxMPCDIShaderType)CVarIcvfxMPCDIShaderType.GetValueOnAnyThread();

		switch (ShaderType)
		{
		case EVarIcvfxMPCDIShaderType::DefaultNoBlend:
			bIsBlendDisabled = true;
			break;

		case EVarIcvfxMPCDIShaderType::Passthrough:
			PixelShaderType = EIcvfxShaderType::Passthrough;
			break;

		case EVarIcvfxMPCDIShaderType::ForceMultiPassRender:
			bForceMultiCameraPass = true;
			break;

		case EVarIcvfxMPCDIShaderType::Disable:
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
			case EIcvfxShaderType::Passthrough:
			{
				LocalUVMatrix = FMatrix::Identity;
				FIcvfxRenderPassData RenderPassData;
				InitRenderPass(RenderPassData);
				RenderPassthrough(RHICmdList, RenderPassData);
				break;
			}

			case EIcvfxShaderType::WarpAndBlend:
			{
				LocalUVMatrix = WarpBlendParameters.Context.UVMatrix * WarpBlendParameters.Context.TextureMatrix * WarpBlendParameters.Context.RegionMatrix;
				{
					// Render Full Icvfx Warp&Blend
					while (true)
					{
						{
							// Build and draw render-pass:
							FIcvfxRenderPassData RenderPassData;
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

DECLARE_GPU_STAT_NAMED(nDisplay_Icvfx_WarpBlend, TEXT("nDisplay Icvfx::Warp&Blend"));

bool FDisplayClusterShadersWarpblend_ICVFX::RenderWarpBlend_ICVFX(FRHICommandListImmediate& RHICmdList, const FDisplayClusterShaderParameters_WarpBlend& InWarpBlendParameters, const FDisplayClusterShaderParameters_ICVFX& InICVFXParameters)
{
	check(IsInRenderingThread());

	FIcvfxPassRenderer IcvfxPassRenderer(InWarpBlendParameters, InICVFXParameters);

	if (!IcvfxPassRenderer.Initialize())
	{
		return false;
	}

	SCOPED_GPU_STAT(RHICmdList, nDisplay_Icvfx_WarpBlend);
	SCOPED_DRAW_EVENT(RHICmdList, nDisplay_Icvfx_WarpBlend);

	// Do single warp render pass
	bool bIsRenderSuccess = false;
	FRHIRenderPassInfo RPInfo(InWarpBlendParameters.Dest.Texture, ERenderTargetActions::Load_Store);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("nDisplay_IcvfxWarpBlend"));
	bIsRenderSuccess = IcvfxPassRenderer.Render(RHICmdList);
	RHICmdList.EndRenderPass();

	return bIsRenderSuccess;
};
