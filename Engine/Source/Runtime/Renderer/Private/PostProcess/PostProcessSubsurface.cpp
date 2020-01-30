// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PostProcessSubsurface.cpp: Screenspace subsurface scattering implementation.
		Indirect dispatch implementation high level description
	   1. Initialize counters
	   2. Setup pass: record the tiles that need to draw Burley and Separable in two different buffer.
	   3. Indirect dispatch Burley.
	   4. Indirect dispatch Separable.
	   5. Recombine.
=============================================================================*/

#include "PostProcess/PostProcessSubsurface.h"
#include "PostProcess/SceneRenderTargets.h"
#include "Engine/SubsurfaceProfile.h"
#include "CanvasTypes.h"
#include "RenderTargetTemp.h"
#include "SystemTextures.h"
#include "GenerateMips.h"
#include "ClearQuad.h"

namespace
{
	// Subsurface common parameters
	TAutoConsoleVariable<int32> CVarSubsurfaceScattering(
		TEXT("r.SubsurfaceScattering"),
		1,
		TEXT(" 0: disabled\n")
		TEXT(" 1: enabled (default)"),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	TAutoConsoleVariable<float> CVarSSSScale(
		TEXT("r.SSS.Scale"),
		1.0f,
		TEXT("Affects the Screen space Separable subsurface scattering pass ")
		TEXT("(use shadingmodel SubsurfaceProfile, get near to the object as the default)\n")
		TEXT("is human skin which only scatters about 1.2cm)\n")
		TEXT(" 0: off (if there is no object on the screen using this pass it should automatically disable the post process pass)\n")
		TEXT("<1: scale scatter radius down (for testing)\n")
		TEXT(" 1: use given radius form the Subsurface scattering asset (default)\n")
		TEXT(">1: scale scatter radius up (for testing)"),
		ECVF_Scalability | ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarSSSHalfRes(
		TEXT("r.SSS.HalfRes"),
		1,
		TEXT(" 0: full quality (Combined Burley and Separable pass. Separable is not optimized, as reference)\n")
		TEXT(" 1: parts of the algorithm runs in half resolution which is lower quality but faster (default, Separable only)"),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	TAutoConsoleVariable<int32> CVarSSSQuality(
		TEXT("r.SSS.Quality"),
		0,
		TEXT("Defines the quality of the recombine pass when using the SubsurfaceScatteringProfile shading model\n")
		TEXT(" 0: low (faster, default)\n")
		TEXT(" 1: high (sharper details but slower)\n")
		TEXT("-1: auto, 1 if TemporalAA is disabled (without TemporalAA the quality is more noticable)"),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	TAutoConsoleVariable<int32> CVarSSSFilter(
		TEXT("r.SSS.Filter"),
		1,
		TEXT("Defines the filter method for Screenspace Subsurface Scattering feature.\n")
		TEXT(" 0: point filter (useful for testing, could be cleaner)\n")
		TEXT(" 1: bilinear filter"),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	TAutoConsoleVariable<int32> CVarSSSSampleSet(
		TEXT("r.SSS.SampleSet"),
		2,
		TEXT("Defines how many samples we use for Separable Screenspace Subsurface Scattering feature.\n")
		TEXT(" 0: lowest quality (6*2+1)\n")
		TEXT(" 1: medium quality (9*2+1)\n")
		TEXT(" 2: high quality (13*2+1) (default)"),
		ECVF_RenderThreadSafe | ECVF_Scalability);

	TAutoConsoleVariable<int32> CVarSSSBurleyUpdateParameter(
		TEXT("r.SSS.Burley.AlwaysUpdateParametersFromSeparable"),
		0,
		TEXT("0: Will not update parameters when the program loads. (default)")
		TEXT("1: Always update from the separable when the program loads. (Correct only when Subsurface color is 1)."),
		ECVF_RenderThreadSafe | ECVF_Scalability
	);

	TAutoConsoleVariable<int32> CVarSSSCheckerboard(
		TEXT("r.SSS.Checkerboard"),
		2,
		TEXT("Enables or disables checkerboard rendering for subsurface profile rendering.\n")
		TEXT("This is necessary if SceneColor does not include a floating point alpha channel (e.g 32-bit formats)\n")
		TEXT(" 0: Disabled (high quality) \n")
		TEXT(" 1: Enabled (low quality). Surface lighting will be at reduced resolution.\n")
		TEXT(" 2: Automatic. Non-checkerboard lighting will be applied if we have a suitable rendertarget format\n"),
		ECVF_RenderThreadSafe);

	TAutoConsoleVariable<int32> CVarSSSBurleyQuality(
		TEXT("r.SSS.Burley.Quality"),
		1,
		TEXT("0: Fallback mode. Burley falls back to run scattering in Separable with transmission in Burley for better performance. Separable parameters are automatically fitted.")
		TEXT("1: Automatic. The subsurface will only switch to separable in half resolution. (default)"),
		ECVF_RenderThreadSafe | ECVF_Scalability
	);
}

// Define to use a custom ps to clear UAV.
#define USE_CUSTOM_CLEAR_UAV

// Define the size of subsurface group. @TODO: Set to 16 to use LDS.
const uint32 kSubsurfaceGroupSize = 8;

ENGINE_API IPooledRenderTarget* GetSubsufaceProfileTexture_RT(FRHICommandListImmediate& RHICmdList);

enum class ESubsurfaceMode : uint32
{
	// Performs a full resolution scattering filter.
	FullRes,

	// Performs a half resolution scattering filter.
	HalfRes,

	// Reconstructs lighting, but does not perform scattering.
	Bypass,

	MAX
};

// Returns the [0, N] clamped value of the 'r.SSS.Scale' CVar.
float GetSubsurfaceRadiusScale()
{
	static const auto CVar = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.SSS.Scale"));
	check(CVar);

	return FMath::Max(0.0f, CVar->GetValueOnRenderThread());
}

int32 GetSSSFilter()
{
	return CVarSSSFilter.GetValueOnRenderThread();
}

int32 GetSSSSampleSet()
{
	return CVarSSSSampleSet.GetValueOnRenderThread();
}

int32 GetSSSQuality()
{
	return CVarSSSQuality.GetValueOnRenderThread();
}

// Returns the SS profile texture with a black fallback texture if none exists yet.
// Actually we do not need this for the burley normalized SSS.
FRHITexture* GetSubsurfaceProfileTexture(FRHICommandListImmediate& RHICmdList)
{
	const IPooledRenderTarget* ProfileTextureTarget = GetSubsufaceProfileTexture_RT(RHICmdList);

	if (!ProfileTextureTarget)
	{
		// No subsurface profile was used yet
		ProfileTextureTarget = GSystemTextures.BlackDummy;
	}

	return ProfileTextureTarget->GetRenderTargetItem().ShaderResourceTexture;
}

// Returns the current subsurface mode required by the current view.
ESubsurfaceMode GetSubsurfaceModeForView(const FViewInfo& View)
{
	const float Radius = GetSubsurfaceRadiusScale();
	const bool bShowSubsurfaceScattering = Radius > 0 && View.Family->EngineShowFlags.SubsurfaceScattering;

	if (bShowSubsurfaceScattering)
	{
		const bool bHalfRes = CVarSSSHalfRes.GetValueOnRenderThread() != 0;
		if (bHalfRes)
		{
			return ESubsurfaceMode::HalfRes;
		}
		else
		{
			return ESubsurfaceMode::FullRes;
		}
	}
	else
	{
		return ESubsurfaceMode::Bypass;
	}
}

// A shader parameter struct for a single subsurface input texture.
BEGIN_SHADER_PARAMETER_STRUCT(FSubsurfaceInput, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FScreenPassTextureViewportParameters, Viewport)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, Texture)
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FSubsurfaceSRVInput, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FScreenPassTextureViewportParameters, Viewport)
	SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2D, Texture)
END_SHADER_PARAMETER_STRUCT();

// Set of common shader parameters shared by all subsurface shaders.
BEGIN_SHADER_PARAMETER_STRUCT(FSubsurfaceParameters, )
	SHADER_PARAMETER(FVector4, SubsurfaceParams)
	SHADER_PARAMETER_STRUCT_REF(FSceneTexturesUniformParameters, SceneUniformBuffer)
	SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
	SHADER_PARAMETER_SAMPLER(SamplerState, BilinearTextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, SSProfilesTexture)
END_SHADER_PARAMETER_STRUCT()

FSubsurfaceParameters GetSubsurfaceCommonParameters(FRHICommandListImmediate& RHICmdList, const FViewInfo& View)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	const float DistanceToProjectionWindow = View.ViewMatrices.GetProjectionMatrix().M[0][0];
	const float SSSScaleZ = DistanceToProjectionWindow * GetSubsurfaceRadiusScale();
	const float SSSScaleX = SSSScaleZ / SUBSURFACE_KERNEL_SIZE * 0.5f;

	FSubsurfaceParameters Parameters;
	Parameters.SubsurfaceParams = FVector4(SSSScaleX, SSSScaleZ, 0, 0);
	Parameters.ViewUniformBuffer = View.ViewUniformBuffer;
	Parameters.SceneUniformBuffer = CreateSceneTextureUniformBuffer(
		SceneContext, View.FeatureLevel, ESceneTextureSetupMode::All, EUniformBufferUsage::UniformBuffer_SingleFrame);
	Parameters.BilinearTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
	Parameters.SSProfilesTexture = GetSubsurfaceProfileTexture(RHICmdList);
	return Parameters;
}

FSubsurfaceInput GetSubsurfaceInput(FRDGTextureRef Texture, const FScreenPassTextureViewportParameters& ViewportParameters)
{
	FSubsurfaceInput Input;
	Input.Texture = Texture;
	Input.Viewport = ViewportParameters;
	return Input;
}

FSubsurfaceSRVInput GetSubsurfaceSRVInput(FRDGTextureSRVRef Texture, const FScreenPassTextureViewportParameters& ViewportParameters)
{
	FSubsurfaceSRVInput Input;
	Input.Texture = Texture;
	Input.Viewport = ViewportParameters;
	return Input;
}

bool IsSubsurfaceEnabled()
{
	const bool bEnabled = CVarSubsurfaceScattering.GetValueOnAnyThread() != 0;
	const bool bHasScale = CVarSSSScale.GetValueOnAnyThread() > 0.0f;
	return (bEnabled && bHasScale);
}

bool IsSubsurfaceRequiredForView(const FViewInfo& View)
{
	const bool bSimpleDynamicLighting = IsAnyForwardShadingEnabled(View.GetShaderPlatform());
	const bool bSubsurfaceEnabled = IsSubsurfaceEnabled();
	const bool bViewHasSubsurfaceMaterials = ((View.ShadingModelMaskInView & GetUseSubsurfaceProfileShadingModelMask()) != 0);
	return (bSubsurfaceEnabled && bViewHasSubsurfaceMaterials && !bSimpleDynamicLighting);
}

uint32 GetSubsurfaceRequiredViewMask(const TArray<FViewInfo>& Views)
{
	const uint32 ViewCount = Views.Num();
	uint32 ViewMask = 0;

	// Traverse the views to make sure we only process subsurface if requested by any view.
	for (uint32 ViewIndex = 0; ViewIndex < ViewCount; ++ViewIndex)
	{
		const FViewInfo& View = Views[ViewIndex];

		if (IsSubsurfaceRequiredForView(View))
		{
			const uint32 ViewBit = 1 << ViewIndex;

			ViewMask |= ViewBit;
		}
	}

	return ViewMask;
}

bool IsSubsurfaceCheckerboardFormat(EPixelFormat SceneColorFormat)
{
	int CVarValue = CVarSSSCheckerboard.GetValueOnRenderThread();
	if (CVarValue == 0)
	{
		return false;
	}
	else if (CVarValue == 1)
	{
		return true;
	}
	else if (CVarValue == 2)
	{
		switch (SceneColorFormat)
		{
		case PF_A32B32G32R32F:
		case PF_FloatRGBA:
			return false;
		default:
			return true;
		}
	}
	return true;
}


// Base class for a subsurface shader.
class FSubsurfaceShader : public FGlobalShader
{
public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SUBSURFACE_RADIUS_SCALE"), SUBSURFACE_RADIUS_SCALE);
		OutEnvironment.SetDefine(TEXT("SUBSURFACE_KERNEL_SIZE"), SUBSURFACE_KERNEL_SIZE);
	}

	FSubsurfaceShader() = default;
	FSubsurfaceShader(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};

// Encapsulates the post processing subsurface scattering common pixel shader.
class FSubsurfaceVisualizePS : public FSubsurfaceShader
{
public:
	DECLARE_GLOBAL_SHADER(FSubsurfaceVisualizePS);
	SHADER_USE_PARAMETER_STRUCT(FSubsurfaceVisualizePS, FSubsurfaceShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSubsurfaceParameters, Subsurface)
		SHADER_PARAMETER_STRUCT(FSubsurfaceInput, SubsurfaceInput0)
		SHADER_PARAMETER_TEXTURE(Texture2D, MiniFontTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SubsurfaceSampler0)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FSubsurfaceVisualizePS, "/Engine/Private/PostProcessSubsurface.usf", "VisualizePS", SF_Pixel);

// Encapsulates a simple copy pixel shader.
class FSubsurfaceViewportCopyPS : public FSubsurfaceShader
{
	DECLARE_GLOBAL_SHADER(FSubsurfaceViewportCopyPS);
	SHADER_USE_PARAMETER_STRUCT(FSubsurfaceViewportCopyPS, FSubsurfaceShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SubsurfaceInput0_Texture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SubsurfaceSampler0)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT();

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSubsurfaceViewportCopyPS, "/Engine/Private/PostProcessSubsurface.usf", "SubsurfaceViewportCopyPS", SF_Pixel);


//-------------------------------------------------------------------------------------------
// Indirect dispatch class and functions
//-------------------------------------------------------------------------------------------

// Subsurface uniform buffer layout
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FSubsurfaceUniformParameters, )
	SHADER_PARAMETER(uint32, MaxGroupCount)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FSubsurfaceUniformParameters, "SubsurfaceUniformParameters");
typedef TUniformBufferRef<FSubsurfaceUniformParameters> FSubsurfaceUniformRef;

// Return a uniform buffer with values filled and with single frame lifetime
FSubsurfaceUniformRef CreateUniformBuffer(FViewInfo const& View, int32 MaxGroupCount)
{
	FSubsurfaceUniformParameters Parameters;
	Parameters.MaxGroupCount = MaxGroupCount;
	return FSubsurfaceUniformRef::CreateUniformBufferImmediate(Parameters, UniformBuffer_SingleFrame);
}

class FSubsurfaceInitValueBufferCS : public FSubsurfaceShader
{
public:
	DECLARE_GLOBAL_SHADER(FSubsurfaceInitValueBufferCS);
	SHADER_USE_PARAMETER_STRUCT(FSubsurfaceInitValueBufferCS, FSubsurfaceShader);

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SUBSURFACE_RADIUS_SCALE"), SUBSURFACE_RADIUS_SCALE);
		OutEnvironment.SetDefine(TEXT("SUBSURFACE_KERNEL_SIZE"), SUBSURFACE_KERNEL_SIZE);
		OutEnvironment.SetDefine(TEXT("SUBSURFACE_BURLEY_COMPUTE"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWSeparableGroupBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWBurleyGroupBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSubsurfaceInitValueBufferCS, "/Engine/Private/PostProcessSubsurface.usf", "InitValueBufferCS", SF_Compute);

class FSubsurfaceBuildIndirectDispatchArgsCS : public FSubsurfaceShader
{
public:
	DECLARE_GLOBAL_SHADER(FSubsurfaceBuildIndirectDispatchArgsCS);
	SHADER_USE_PARAMETER_STRUCT(FSubsurfaceBuildIndirectDispatchArgsCS, FSubsurfaceShader)

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SUBSURFACE_RADIUS_SCALE"), SUBSURFACE_RADIUS_SCALE);
		OutEnvironment.SetDefine(TEXT("SUBSURFACE_KERNEL_SIZE"), SUBSURFACE_KERNEL_SIZE);
		OutEnvironment.SetDefine(TEXT("SUBSURFACE_BURLEY_COMPUTE"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FSubsurfaceUniformParameters, SubsurfaceUniformParameters)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWIndirectDispatchArgsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, GroupBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSubsurfaceBuildIndirectDispatchArgsCS, "/Engine/Private/PostProcessSubsurface.usf", "BuildIndirectDispatchArgsCS", SF_Compute);

class FSubsurfaceIndirectDispatchSetupCS : public FSubsurfaceShader
{
public:
	DECLARE_GLOBAL_SHADER(FSubsurfaceIndirectDispatchSetupCS);
	SHADER_USE_PARAMETER_STRUCT(FSubsurfaceIndirectDispatchSetupCS, FSubsurfaceShader)

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SUBSURFACE_RADIUS_SCALE"), SUBSURFACE_RADIUS_SCALE);
		OutEnvironment.SetDefine(TEXT("SUBSURFACE_KERNEL_SIZE"), SUBSURFACE_KERNEL_SIZE);
		OutEnvironment.SetDefine(TEXT("SUBSURFACE_BURLEY_COMPUTE"), 1);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSubsurfaceParameters, Subsurface)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Output)
		SHADER_PARAMETER_STRUCT(FSubsurfaceInput, SubsurfaceInput0)
		SHADER_PARAMETER_SAMPLER(SamplerState, SubsurfaceSampler0)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, SetupTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWSeparableGroupBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWBurleyGroupBuffer)
		SHADER_PARAMETER_STRUCT_REF(FSubsurfaceUniformParameters, SubsurfaceUniformParameters)
	END_SHADER_PARAMETER_STRUCT()

	class FDimensionHalfRes : SHADER_PERMUTATION_BOOL("SUBSURFACE_HALF_RES");
	class FDimensionCheckerboard : SHADER_PERMUTATION_BOOL("SUBSURFACE_PROFILE_CHECKERBOARD");
	class FRunningInSeparable : SHADER_PERMUTATION_BOOL("SUBSURFACE_FORCE_SEPARABLE");
	using FPermutationDomain = TShaderPermutationDomain<FDimensionHalfRes, FDimensionCheckerboard, FRunningInSeparable>;
};

IMPLEMENT_GLOBAL_SHADER(FSubsurfaceIndirectDispatchSetupCS, "/Engine/Private/PostProcessSubsurface.usf", "SetupIndirectCS", SF_Compute);

class FSubsurfaceIndirectDispatchCS : public FSubsurfaceShader
{
public:
	DECLARE_GLOBAL_SHADER(FSubsurfaceIndirectDispatchCS);
	SHADER_USE_PARAMETER_STRUCT(FSubsurfaceIndirectDispatchCS, FSubsurfaceShader);

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SUBSURFACE_RADIUS_SCALE"), SUBSURFACE_RADIUS_SCALE);
		OutEnvironment.SetDefine(TEXT("SUBSURFACE_KERNEL_SIZE"), SUBSURFACE_KERNEL_SIZE);
		OutEnvironment.SetDefine(TEXT("SUBSURFACE_BURLEY_COMPUTE"), 1);
		OutEnvironment.SetDefine(TEXT("ENABLE_VELOCITY"), 1);
		OutEnvironment.SetDefine(TEXT("SUBSURFACE_GROUP_SIZE"), kSubsurfaceGroupSize);
	}

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSubsurfaceParameters, Subsurface)
		SHADER_PARAMETER_STRUCT(FScreenPassTextureViewportParameters, Output)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, SSSColorUAV)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, HistoryUAV)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, GroupBuffer)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, IndirectDispatchArgsBuffer)
		SHADER_PARAMETER_STRUCT(FSubsurfaceInput, SubsurfaceInput0)
		SHADER_PARAMETER_SAMPLER(SamplerState, SubsurfaceSampler0)
		SHADER_PARAMETER_STRUCT(FSubsurfaceInput, SubsurfaceInput1)	// History
		SHADER_PARAMETER_SAMPLER(SamplerState, SubsurfaceSampler1)
		SHADER_PARAMETER_STRUCT(FSubsurfaceInput, SubsurfaceInput2)   // Profile mask | Velocity
		SHADER_PARAMETER_SAMPLER(SamplerState, SubsurfaceSampler2)
	END_SHADER_PARAMETER_STRUCT()

	// Direction of the 1D separable filter.
	enum class EDirection : uint32
	{
		Horizontal,
		Vertical,
		MAX
	};

	enum class ESubsurfacePass : uint32
	{
		PassOne,    // Burley sampling   (or Horizontal) pass   pass one
		PassTwo,	// Variance updating (or   Vertical) pass pass two
		MAX
	};

	// Controls the quality (number of samples) of the blur kernel.
	enum class EQuality : uint32
	{
		Low,
		Medium,
		High,
		MAX
	};

	enum class ESubsurfaceSamplerType : uint32
	{
		PointSampler,
		NonPointSampler,	//bilinear on LDS or trilinear on texture. 
		MAX
	};

	enum class ESubsurfaceType : uint32
	{
		BURLEY,
		SEPARABLE,
		MAX
	};

	class FSubsurfacePassFunction : SHADER_PERMUTATION_ENUM_CLASS("SUBSURFACE_PASS", ESubsurfacePass);
	class FDimensionQuality : SHADER_PERMUTATION_ENUM_CLASS("SUBSURFACE_QUALITY", EQuality);
	class FSubsurfaceSamplerType : SHADER_PERMUTATION_ENUM_CLASS("SUBSURFACE_SAMPLER_TYPE", ESubsurfaceSamplerType);
	class FSubsurfaceType : SHADER_PERMUTATION_ENUM_CLASS("SUBSURFACE_TYPE", ESubsurfaceType);
	class FDimensionHalfRes : SHADER_PERMUTATION_BOOL("SUBSURFACE_HALF_RES");
	class FRunningInSeparable : SHADER_PERMUTATION_BOOL("SUBSURFACE_FORCE_SEPARABLE");
	using FPermutationDomain = TShaderPermutationDomain<FSubsurfacePassFunction, FDimensionQuality, FSubsurfaceSamplerType, FSubsurfaceType, FDimensionHalfRes, FRunningInSeparable>;

	// Returns the sampler state based on the requested SSS filter CVar setting and half resolution setting.
	static FRHISamplerState* GetSamplerState(bool bHalfRes)
	{
		if (GetSSSFilter())
		{	// Trilinear is used for mipmap sampling in full resolution
			if (bHalfRes)
			{
				return TStaticSamplerState<SF_Bilinear, AM_Border, AM_Border, AM_Border>::GetRHI();//SF_Bilinear
			}
			else
			{
				return TStaticSamplerState<SF_Trilinear, AM_Border, AM_Border, AM_Border>::GetRHI();//SF_Bilinear
			}
		}
		else
		{
			return TStaticSamplerState<SF_Point, AM_Border, AM_Border, AM_Border>::GetRHI();
		}
	}

	// Returns the SSS quality level requested by the SSS SampleSet CVar setting.
	static EQuality GetQuality()
	{
		return static_cast<FSubsurfaceIndirectDispatchCS::EQuality>(
			FMath::Clamp(
				GetSSSSampleSet(),
				static_cast<int32>(FSubsurfaceIndirectDispatchCS::EQuality::Low),
				static_cast<int32>(FSubsurfaceIndirectDispatchCS::EQuality::High)));
	}

	static ESubsurfaceSamplerType GetSamplerType()
	{
		if (GetSSSFilter())
		{
			return ESubsurfaceSamplerType::NonPointSampler;
		}
		else
		{
			return ESubsurfaceSamplerType::PointSampler;
		}
	}
};

IMPLEMENT_GLOBAL_SHADER(FSubsurfaceIndirectDispatchCS, "/Engine/Private/PostProcessSubsurface.usf", "MainIndirectDispatchCS", SF_Compute);

// resolve textures that is not SRV
// Encapsulates a simple copy pixel shader.
class FSubsurfaceSRVResolvePS : public FSubsurfaceShader
{
	DECLARE_GLOBAL_SHADER(FSubsurfaceSRVResolvePS);
	SHADER_USE_PARAMETER_STRUCT(FSubsurfaceSRVResolvePS, FSubsurfaceShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SubsurfaceInput0_Texture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SubsurfaceSampler0)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT();

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

IMPLEMENT_GLOBAL_SHADER(FSubsurfaceSRVResolvePS, "/Engine/Private/PostProcessSubsurface.usf", "SubsurfaceViewportCopyPS", SF_Pixel);

//-------------------------------------------------------------------------------------------
// End indirect dispatch class
//-------------------------------------------------------------------------------------------
FRDGTextureRef ResolveTextureToSRV(FRDGBuilder& GraphBuilder, FRDGTextureRef InputTexture, const FViewInfo& View,
	const FScreenPassTextureViewport& SceneViewport)
{
	FRDGTextureDesc SRVDesc = InputTexture->Desc;

	// if this texture can be used as SRV, we ignore this function call.
	if (SRVDesc.TargetableFlags &TexCreate_ShaderResource)
	{
		return InputTexture;
	}

	SRVDesc.TargetableFlags |= TexCreate_ShaderResource;
	FRDGTextureRef SRVTextureOutput = GraphBuilder.CreateTexture(SRVDesc, InputTexture->Desc.DebugName);

	FSubsurfaceSRVResolvePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSubsurfaceSRVResolvePS::FParameters>();
	PassParameters->RenderTargets[0] = FRenderTargetBinding(SRVTextureOutput, ERenderTargetLoadAction::ENoAction);
	PassParameters->SubsurfaceInput0_Texture = InputTexture;
	PassParameters->SubsurfaceSampler0 = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	TShaderMapRef<FSubsurfaceSRVResolvePS> PixelShader(View.ShaderMap);

	AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("SubsurfaceTextureResolve"), View, SceneViewport, SceneViewport, *PixelShader, PassParameters);

	return SRVTextureOutput;
}

FRDGTextureRef CreateBlackUAVTexture(FRDGBuilder& GraphBuilder, FRDGTextureDesc SRVDesc, const TCHAR* Name, const FViewInfo& View,
	const FScreenPassTextureViewport& SceneViewport)
{
#ifdef USE_CUSTOM_CLEAR_UAV
	SRVDesc.TargetableFlags |= TexCreate_ShaderResource | TexCreate_UAV;
	FRDGTextureRef SRVTextureOutput = GraphBuilder.CreateTexture(SRVDesc, Name);

	FSubsurfaceSRVResolvePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSubsurfaceSRVResolvePS::FParameters>();
	PassParameters->RenderTargets[0] = FRenderTargetBinding(SRVTextureOutput, ERenderTargetLoadAction::ENoAction);
	PassParameters->SubsurfaceInput0_Texture = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
	PassParameters->SubsurfaceSampler0 = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	TShaderMapRef<FSubsurfaceSRVResolvePS> PixelShader(View.ShaderMap);

	AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("ClearUAV"), View, SceneViewport, SceneViewport, *PixelShader, PassParameters);
#else
	FRDGTextureRef SRVTextureOutput = GraphBuilder.CreateTexture(SRVDesc, Name);
	FRDGTextureUAVDesc UAVClearDesc(SRVTextureOutput, 0);

	ClearUAV(GraphBuilder, FRDGEventName(TEXT("ClearUAV")), GraphBuilder.CreateUAV(UAVClearDesc), FLinearColor::Black);
#endif

	return SRVTextureOutput;
}

// Helper function to use external textures for the current GraphBuilder.
// When the texture is null, we use BlackDummy.
FRDGTextureRef RegisterExternalRenderTarget(FRDGBuilder& GraphBuilder, TRefCountPtr<IPooledRenderTarget>* PtrExternalTexture, FIntPoint CurentViewExtent, const TCHAR* Name)
{
	FRDGTextureRef RegisteredTexture = NULL;

	if (!PtrExternalTexture || !(*PtrExternalTexture))
	{
		RegisteredTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy, Name);
	}
	else
	{
		if (CurentViewExtent != (*PtrExternalTexture)->GetDesc().Extent)
		{
			RegisteredTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy, Name);
		}
		else
		{
			RegisteredTexture = GraphBuilder.RegisterExternalTexture(*PtrExternalTexture, Name);
		}
	}

	return RegisteredTexture;
}

// Encapsulates the post processing subsurface recombine pixel shader.
class FSubsurfaceRecombinePS : public FSubsurfaceShader
{
	DECLARE_GLOBAL_SHADER(FSubsurfaceRecombinePS);
	SHADER_USE_PARAMETER_STRUCT(FSubsurfaceRecombinePS, FSubsurfaceShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FSubsurfaceParameters, Subsurface)
		SHADER_PARAMETER_STRUCT(FSubsurfaceInput, SubsurfaceInput0)
		SHADER_PARAMETER_STRUCT(FSubsurfaceInput, SubsurfaceInput1)
		SHADER_PARAMETER_SAMPLER(SamplerState, SubsurfaceSampler0)
		SHADER_PARAMETER_SAMPLER(SamplerState, SubsurfaceSampler1)
		RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT();

	// Controls the quality of lighting reconstruction.
	enum class EQuality : uint32
	{
		Low,
		High,
		MAX
	};

	class FDimensionMode : SHADER_PERMUTATION_ENUM_CLASS("SUBSURFACE_RECOMBINE_MODE", ESubsurfaceMode);
	class FDimensionQuality : SHADER_PERMUTATION_ENUM_CLASS("SUBSURFACE_RECOMBINE_QUALITY", EQuality);
	class FDimensionCheckerboard : SHADER_PERMUTATION_BOOL("SUBSURFACE_PROFILE_CHECKERBOARD");
	class FDimensionHalfRes : SHADER_PERMUTATION_BOOL("SUBSURFACE_HALF_RES");
	class FRunningInSeparable : SHADER_PERMUTATION_BOOL("SUBSURFACE_FORCE_SEPARABLE");
	using FPermutationDomain = TShaderPermutationDomain<FDimensionMode, FDimensionQuality, FDimensionCheckerboard, FDimensionHalfRes, FRunningInSeparable>;

	// Returns the Recombine quality level requested by the SSS Quality CVar setting.
	static EQuality GetQuality(const FViewInfo& View)
	{
		const uint32 QualityCVar = GetSSSQuality();

		// Quality is forced to high when the CVar is set to 'auto' and TAA is NOT enabled.
		// TAA improves quality through temporal filtering, making it less necessary to use
		// high quality mode.
		const bool bUseHighQuality = (QualityCVar == -1 && View.AntiAliasingMethod != AAM_TemporalAA);

		if (QualityCVar == 1 || bUseHighQuality)
		{
			return EQuality::High;
		}
		else
		{
			return EQuality::Low;
		}
	}
};

IMPLEMENT_GLOBAL_SHADER(FSubsurfaceRecombinePS, "/Engine/Private/PostProcessSubsurface.usf", "SubsurfaceRecombinePS", SF_Pixel);

void ComputeSubsurfaceForView(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FScreenPassTextureViewport& SceneViewport,
	FRDGTextureRef SceneTexture,
	FRDGTextureRef SceneTextureOutput,
	ERenderTargetLoadAction SceneTextureLoadAction)
{
	check(SceneTexture);
	check(SceneTextureOutput);
	check(SceneViewport.Extent == SceneTexture->Desc.Extent);

	const FSceneViewFamily* ViewFamily = View.Family;

	const FRDGTextureDesc& SceneTextureDesc = SceneTexture->Desc;

	const ESubsurfaceMode SubsurfaceMode = GetSubsurfaceModeForView(View);

	const bool bHalfRes = (SubsurfaceMode == ESubsurfaceMode::HalfRes);

	const bool bCheckerboard = IsSubsurfaceCheckerboardFormat(SceneTextureDesc.Format);

	const uint32 ScaleFactor = bHalfRes ? 2 : 1;
	
	//We run in separable mode under three conditions: 1) Run Burley fallback mode. 2) when the screen is in half resolution. 3) OpenGL
	const bool bForceRunningInSeparable = CVarSSSBurleyQuality.GetValueOnRenderThread() == 0|| bHalfRes || View.GetShaderPlatform() == SP_OPENGL_SM5;

	/**
	 * All subsurface passes within the screen-space subsurface effect can operate at half or full resolution,
	 * depending on the subsurface mode. The values are precomputed and shared among all Subsurface textures.
	 */
	const FScreenPassTextureViewport SubsurfaceViewport = FScreenPassTextureViewport::CreateDownscaled(SceneViewport, ScaleFactor);

	const FIntPoint TileDimension = FIntPoint::DivideAndRoundUp(SubsurfaceViewport.Extent, kSubsurfaceGroupSize);
	const int32 MaxGroupCount = TileDimension.X*TileDimension.Y;

	const FRDGTextureDesc SceneTextureDescriptor = FRDGTextureDesc::Create2DDesc(
		SceneViewport.Extent,
		PF_FloatRGBA,
		FClearValueBinding(),
		TexCreate_None,
		TexCreate_RenderTargetable | TexCreate_ShaderResource | TexCreate_UAV,
		false);

	const FRDGTextureDesc SubsurfaceTextureDescriptor = FRDGTextureDesc::Create2DDesc(
		SubsurfaceViewport.Extent,
		PF_FloatRGBA,
		FClearValueBinding(),
		TexCreate_None,
		TexCreate_RenderTargetable | TexCreate_ShaderResource | TexCreate_UAV,
		false);

	const FRDGTextureDesc SubsurfaceTextureWith6MipsDescriptor = FRDGTextureDesc::Create2DDesc(
		SubsurfaceViewport.Extent,
		PF_FloatRGBA,
		FClearValueBinding(),
		TexCreate_None,
		TexCreate_RenderTargetable | TexCreate_ShaderResource | TexCreate_UAV,
		false,
		6);

	const FSubsurfaceParameters SubsurfaceCommonParameters = GetSubsurfaceCommonParameters(GraphBuilder.RHICmdList, View);
	const FScreenPassTextureViewportParameters SubsurfaceViewportParameters = GetScreenPassTextureViewportParameters(SubsurfaceViewport);
	const FScreenPassTextureViewportParameters SceneViewportParameters = GetScreenPassTextureViewportParameters(SceneViewport);

	FRDGTextureRef SetupTexture = SceneTexture;
	FRDGTextureRef SubsurfaceSubpassOneTex = nullptr;
	FRDGTextureRef SubsurfaceSubpassTwoTex = nullptr;

	FRHISamplerState* PointClampSampler = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	FRHISamplerState* BilinearBorderSampler = TStaticSamplerState<SF_Bilinear, AM_Border, AM_Border, AM_Border>::GetRHI();

	//History texture
	FSceneViewState* ViewState = (FSceneViewState*)View.State;
	TRefCountPtr<IPooledRenderTarget>* QualityHistoryState = ViewState ? &ViewState->SubsurfaceScatteringQualityHistoryRT : NULL;

	//allocate/reallocate the quality history texture. 
	FRDGTextureRef QualityHistoryTexture = RegisterExternalRenderTarget(GraphBuilder, QualityHistoryState, SceneTextureDescriptor.Extent, TEXT("QualityHistoryTexture"));
	FRDGTextureRef NewQualityHistoryTexture = nullptr;

	/**
	 * When in bypass mode, the setup and convolution passes are skipped, but lighting
	 * reconstruction is still performed in the recombine pass.
	 */
	if (SubsurfaceMode != ESubsurfaceMode::Bypass)
	{
		// Support mipmaps in full resolution only.
		SetupTexture = GraphBuilder.CreateTexture(bForceRunningInSeparable ? SubsurfaceTextureDescriptor:SubsurfaceTextureWith6MipsDescriptor, TEXT("SubsurfaceSetupTexture"));

		FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
		FRDGTextureRef VelocityTexture = RegisterExternalRenderTarget(GraphBuilder, &(SceneContext.SceneVelocity), SubsurfaceTextureDescriptor.Extent, TEXT("Velocity")); 
		FSubsurfaceUniformRef UniformBuffer = CreateUniformBuffer(View, MaxGroupCount);
		
		// Pre-allocate black UAV together.
		{
			SubsurfaceSubpassOneTex = CreateBlackUAVTexture(GraphBuilder, SubsurfaceTextureWith6MipsDescriptor, TEXT("SubsurfaceSubpassOneTex"),
				View, SubsurfaceViewport);
			SubsurfaceSubpassTwoTex = CreateBlackUAVTexture(GraphBuilder, SubsurfaceTextureWith6MipsDescriptor, TEXT("SubsurfaceSubpassTwoTex"),
				View, SubsurfaceViewport);
			// Only clear when we are in full resolution.
			if (!bForceRunningInSeparable)
			{
				NewQualityHistoryTexture = CreateBlackUAVTexture(GraphBuilder, SubsurfaceTextureDescriptor, TEXT("SubsurfaceQualityHistoryState"),
					View, SubsurfaceViewport);
			}
		}

		// Initialize the group buffer
		FRDGBufferRef SeparableGroupBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 2*(MaxGroupCount + 1)), TEXT("SeparableGroupBuffer"));;
		FRDGBufferRef BurleyGroupBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 2*(MaxGroupCount + 1)), TEXT("BurleyGroupBuffer"));;
		FRDGBufferRef SeparableIndirectDispatchArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(4), TEXT("SeprableIndirectDispatchArgs"));
		FRDGBufferRef BurleyIndirectDispatchArgsBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc(4), TEXT("BurleyIndirectDispatchArgs"));

		// Initialize the group counters
		{
			typedef FSubsurfaceInitValueBufferCS SHADER;
			TShaderMapRef<SHADER> ComputeShader(View.ShaderMap);
			SHADER::FParameters* PassParameters = GraphBuilder.AllocParameters<SHADER::FParameters>();
			PassParameters->RWBurleyGroupBuffer = GraphBuilder.CreateUAV(BurleyGroupBuffer, EPixelFormat::PF_R32_UINT);
			PassParameters->RWSeparableGroupBuffer = GraphBuilder.CreateUAV(SeparableGroupBuffer, EPixelFormat::PF_R32_UINT);
			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("InitGroupCounter"), *ComputeShader, PassParameters, FIntVector(1, 1, 1));
		}

		// Call the indirect setup
		{
			FRDGTextureSRVDesc SceneTextureSRVDesc = FRDGTextureSRVDesc::Create(SceneTexture);
			FRDGTextureUAVDesc SetupTextureOutDesc(SetupTexture, 0);

			typedef FSubsurfaceIndirectDispatchSetupCS SHADER;
			SHADER::FParameters* PassParameters = GraphBuilder.AllocParameters<SHADER::FParameters>();
			PassParameters->Subsurface = SubsurfaceCommonParameters;
			PassParameters->Output = SubsurfaceViewportParameters;
			PassParameters->SubsurfaceInput0 = GetSubsurfaceInput(SceneTexture, SceneViewportParameters);
			PassParameters->SubsurfaceSampler0 = PointClampSampler;
			PassParameters->SetupTexture = GraphBuilder.CreateUAV(SetupTextureOutDesc);
			PassParameters->RWBurleyGroupBuffer = GraphBuilder.CreateUAV(BurleyGroupBuffer, EPixelFormat::PF_R32_UINT);
			PassParameters->RWSeparableGroupBuffer = GraphBuilder.CreateUAV(SeparableGroupBuffer, EPixelFormat::PF_R32_UINT);
			PassParameters->SubsurfaceUniformParameters = UniformBuffer;

			SHADER::FPermutationDomain ComputeShaderPermutationVector;
			ComputeShaderPermutationVector.Set<SHADER::FDimensionHalfRes>(bHalfRes);
			ComputeShaderPermutationVector.Set<SHADER::FDimensionCheckerboard>(bCheckerboard);
			ComputeShaderPermutationVector.Set<SHADER::FRunningInSeparable>(bForceRunningInSeparable);
			TShaderMapRef<SHADER> ComputeShader(View.ShaderMap, ComputeShaderPermutationVector);

			FIntPoint ComputeGroupCount = FIntPoint::DivideAndRoundUp(SubsurfaceViewport.Extent, kSubsurfaceGroupSize);

			FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("SubsurfaceSetup"), *ComputeShader, PassParameters, FIntVector(ComputeGroupCount.X, ComputeGroupCount.Y, 1));
		}

		// In half resolution, only Separable is used. We do not need this mipmap.
		if(!bForceRunningInSeparable)
		{
			// Generate mipmap for the diffuse scene color and depth, use bilinear filter
			FGenerateMips::Execute(&GraphBuilder, SetupTexture, BilinearBorderSampler);
		}

		typedef FSubsurfaceIndirectDispatchCS SHADER;

		FRHISamplerState* SubsurfaceSamplerState = SHADER::GetSamplerState(bHalfRes);
		const SHADER::EQuality SubsurfaceQuality = SHADER::GetQuality();

		// Store the buffer
		const FRDGBufferRef SubsurfaceBufferUsage[] = { BurleyGroupBuffer,                      SeparableGroupBuffer };
		const FRDGBufferRef  SubsurfaceBufferArgs[] = { BurleyIndirectDispatchArgsBuffer,       SeparableIndirectDispatchArgsBuffer };
		const TCHAR*		  SubsurfacePhaseName[] = { TEXT("BuildBurleyIndirectDispatchArgs"),TEXT("BuildSeparableIndirectDispatchArgs") };

		// Setup the indirect arguments.
		{
			const int NumOfSubsurfaceType = 2;

			for (int SubsurfaceTypeIndex = 0; SubsurfaceTypeIndex < NumOfSubsurfaceType; ++SubsurfaceTypeIndex)
			{
				typedef FSubsurfaceBuildIndirectDispatchArgsCS ARGSETUPSHADER;
				ARGSETUPSHADER::FParameters* PassParameters = GraphBuilder.AllocParameters<ARGSETUPSHADER::FParameters>();
				PassParameters->SubsurfaceUniformParameters = UniformBuffer;
				PassParameters->RWIndirectDispatchArgsBuffer = GraphBuilder.CreateUAV(SubsurfaceBufferArgs[SubsurfaceTypeIndex], EPixelFormat::PF_R32_UINT);
				PassParameters->GroupBuffer = GraphBuilder.CreateSRV(SubsurfaceBufferUsage[SubsurfaceTypeIndex], EPixelFormat::PF_R32_UINT);

				TShaderMapRef<ARGSETUPSHADER> ComputeShader(View.ShaderMap);
				FComputeShaderUtils::AddPass(GraphBuilder, FRDGEventName(SubsurfacePhaseName[SubsurfaceTypeIndex]), *ComputeShader, PassParameters, FIntVector(1, 1, 1));
			}
		}

		// Major pass to combine Burley and Separable
		{
			struct FSubsurfacePassInfo
			{
				FSubsurfacePassInfo(const TCHAR* InName, FRDGTextureRef InInput, FRDGTextureRef InOutput,
					SHADER::ESubsurfaceType InSurfaceType, SHADER::ESubsurfacePass InSurfacePass)
					: Name(InName), Input(InInput), Output(InOutput), SurfaceType(InSurfaceType), SubsurfacePass(InSurfacePass)
				{}

				const TCHAR* Name;
				FRDGTextureRef Input;
				FRDGTextureRef Output;
				SHADER::ESubsurfaceType SurfaceType;
				SHADER::ESubsurfacePass SubsurfacePass;
			};

			const int NumOfSubsurfacePass = 4;

			const FSubsurfacePassInfo SubsurfacePassInfos[NumOfSubsurfacePass] =
			{
				{	TEXT("SubsurfacePassOne_Burley"),				SetupTexture, SubsurfaceSubpassOneTex, SHADER::ESubsurfaceType::BURLEY	 , SHADER::ESubsurfacePass::PassOne}, //Burley main pass
				{	TEXT("SubsurfacePassTwo_SepHon"),				SetupTexture, SubsurfaceSubpassOneTex, SHADER::ESubsurfaceType::SEPARABLE, SHADER::ESubsurfacePass::PassOne}, //Separable horizontal
				{ TEXT("SubsurfacePassThree_SepVer"),	 SubsurfaceSubpassOneTex, SubsurfaceSubpassTwoTex, SHADER::ESubsurfaceType::SEPARABLE, SHADER::ESubsurfacePass::PassTwo}, //Separable Vertical
				{	 TEXT("SubsurfacePassFour_BVar"),    SubsurfaceSubpassOneTex, SubsurfaceSubpassTwoTex, SHADER::ESubsurfaceType::BURLEY	 , SHADER::ESubsurfacePass::PassTwo}  //Burley Variance
			};

			//Dispatch the two phase for both SSS
			for (int PassIndex = 0; PassIndex < NumOfSubsurfacePass; ++PassIndex)
			{
				const FSubsurfacePassInfo& PassInfo = SubsurfacePassInfos[PassIndex];

				const SHADER::ESubsurfaceType SubsurfaceType = PassInfo.SurfaceType;
				const auto SubsurfacePassFunction = PassInfo.SubsurfacePass;
				const int SubsurfaceTypeIndex = static_cast<int>(SubsurfaceType);
				FRDGTextureRef TextureInput = PassInfo.Input;
				FRDGTextureRef TextureOutput = PassInfo.Output;

				FRDGTextureUAVDesc SSSColorUAVDesc(TextureOutput, 0);
				FRDGTextureSRVDesc InputSRVDesc = FRDGTextureSRVDesc::Create(TextureInput);

				SHADER::FParameters* PassParameters = GraphBuilder.AllocParameters<SHADER::FParameters>();
				PassParameters->Subsurface = SubsurfaceCommonParameters;
				PassParameters->Output = SubsurfaceViewportParameters;
				PassParameters->SSSColorUAV = GraphBuilder.CreateUAV(SSSColorUAVDesc);
				PassParameters->SubsurfaceInput0 = GetSubsurfaceInput(TextureInput, SubsurfaceViewportParameters);
				PassParameters->SubsurfaceSampler0 = SubsurfaceSamplerState;
				PassParameters->GroupBuffer = GraphBuilder.CreateSRV(SubsurfaceBufferUsage[SubsurfaceTypeIndex], EPixelFormat::PF_R32_UINT);
				PassParameters->IndirectDispatchArgsBuffer = SubsurfaceBufferArgs[SubsurfaceTypeIndex];

				if (SubsurfacePassFunction == SHADER::ESubsurfacePass::PassOne && SubsurfaceType == SHADER::ESubsurfaceType::BURLEY)
				{
					PassParameters->SubsurfaceInput1 = GetSubsurfaceInput(QualityHistoryTexture, SubsurfaceViewportParameters);
					PassParameters->SubsurfaceSampler1 = PointClampSampler;
				}

				if (SubsurfacePassFunction == SHADER::ESubsurfacePass::PassTwo && SubsurfaceType == SHADER::ESubsurfaceType::BURLEY)
				{
					// we do not write to history in separable mode.
					if (!bForceRunningInSeparable)
					{
						PassParameters->HistoryUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(NewQualityHistoryTexture, 0));
					}

					PassParameters->SubsurfaceInput1 = GetSubsurfaceInput(QualityHistoryTexture, SubsurfaceViewportParameters);
					PassParameters->SubsurfaceSampler1 = PointClampSampler;
					PassParameters->SubsurfaceInput2 = GetSubsurfaceInput(VelocityTexture, SubsurfaceViewportParameters);
					PassParameters->SubsurfaceSampler2 = PointClampSampler;
				}

				SHADER::FPermutationDomain ComputeShaderPermutationVector;
				ComputeShaderPermutationVector.Set<SHADER::FSubsurfacePassFunction>(SubsurfacePassFunction);
				ComputeShaderPermutationVector.Set<SHADER::FDimensionQuality>(SHADER::GetQuality());
				ComputeShaderPermutationVector.Set<SHADER::FSubsurfaceSamplerType>(SHADER::GetSamplerType());
				ComputeShaderPermutationVector.Set<SHADER::FSubsurfaceType>(SubsurfaceType);
				ComputeShaderPermutationVector.Set<SHADER::FDimensionHalfRes>(bHalfRes);
				ComputeShaderPermutationVector.Set<SHADER::FRunningInSeparable>(bForceRunningInSeparable);
				TShaderMapRef<SHADER> ComputeShader(View.ShaderMap, ComputeShaderPermutationVector);

				FComputeShaderUtils::AddPass(GraphBuilder, FRDGEventName(PassInfo.Name), *ComputeShader, PassParameters, SubsurfaceBufferArgs[SubsurfaceTypeIndex], 0);
			}
		}
	}

	// Recombines scattering result with scene color.
	{
		FSubsurfaceRecombinePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSubsurfaceRecombinePS::FParameters>();
		PassParameters->Subsurface = SubsurfaceCommonParameters;
		PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneTextureOutput, SceneTextureLoadAction);
		PassParameters->SubsurfaceInput0 = GetSubsurfaceInput(SceneTexture, SceneViewportParameters);
		PassParameters->SubsurfaceSampler0 = BilinearBorderSampler;

		// Scattering output target is only used when scattering is enabled.
		if (SubsurfaceMode != ESubsurfaceMode::Bypass)
		{
			PassParameters->SubsurfaceInput1 = GetSubsurfaceInput(SubsurfaceSubpassTwoTex, SubsurfaceViewportParameters);
			PassParameters->SubsurfaceSampler1 = BilinearBorderSampler;
		}

		const FSubsurfaceRecombinePS::EQuality RecombineQuality = FSubsurfaceRecombinePS::GetQuality(View);

		FSubsurfaceRecombinePS::FPermutationDomain PixelShaderPermutationVector;
		PixelShaderPermutationVector.Set<FSubsurfaceRecombinePS::FDimensionMode>(SubsurfaceMode);
		PixelShaderPermutationVector.Set<FSubsurfaceRecombinePS::FDimensionQuality>(RecombineQuality);
		PixelShaderPermutationVector.Set<FSubsurfaceRecombinePS::FDimensionCheckerboard>(bCheckerboard);
		PixelShaderPermutationVector.Set<FSubsurfaceRecombinePS::FDimensionHalfRes>(bHalfRes);
		PixelShaderPermutationVector.Set<FSubsurfaceRecombinePS::FRunningInSeparable>(bForceRunningInSeparable);

		TShaderMapRef<FSubsurfaceRecombinePS> PixelShader(View.ShaderMap, PixelShaderPermutationVector);

		/**
		 * See the related comment above in the prepare pass. The scene viewport is used as both the target and
		 * texture viewport in order to ensure that the correct pixel is sampled for checkerboard rendering.
		 */
		AddDrawScreenPass(
			GraphBuilder,
			RDG_EVENT_NAME("SubsurfaceRecombine"),
			View,
			SceneViewport,
			SceneViewport,
			*PixelShader,
			PassParameters,
			EScreenPassDrawFlags::AllowHMDHiddenAreaMask);
	}

	if (SubsurfaceMode != ESubsurfaceMode::Bypass && QualityHistoryState && !bForceRunningInSeparable)
	{
		GraphBuilder.QueueTextureExtraction(NewQualityHistoryTexture, QualityHistoryState, true);
	}
}

FRDGTextureRef ComputeSubsurface(
	FRDGBuilder& GraphBuilder,
	FRDGTextureRef SceneTexture,
	const TArray<FViewInfo>& Views)
{
	const uint32 ViewCount = Views.Num();
	const uint32 ViewMaskAll = (1 << ViewCount) - 1;
	const uint32 ViewMask = GetSubsurfaceRequiredViewMask(Views);

	// Return the original target if no views have subsurface applied.
	if (!ViewMask)
	{
		return SceneTexture;
	}

	FRDGTextureDesc SceneColorDesc = SceneTexture->Desc;
	SceneColorDesc.TargetableFlags |= TexCreate_RenderTargetable;
	FRDGTextureRef SceneTextureOutput = GraphBuilder.CreateTexture(SceneColorDesc, TEXT("SceneColorSubsurface"));

	ERenderTargetLoadAction SceneTextureLoadAction = ERenderTargetLoadAction::ENoAction;

	const bool bHasNonSubsurfaceView = ViewMask != ViewMaskAll;

	/**
	 * Since we are outputting to a new texture and certain views may not utilize subsurface scattering,
	 * we need to copy all non-subsurface views onto the destination texture.
	 */
	if (bHasNonSubsurfaceView)
	{
		FSubsurfaceViewportCopyPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSubsurfaceViewportCopyPS::FParameters>();
		PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneTextureOutput, ERenderTargetLoadAction::ENoAction);
		PassParameters->SubsurfaceInput0_Texture = SceneTexture;
		PassParameters->SubsurfaceSampler0 = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

		TShaderMapRef<FSubsurfaceViewportCopyPS> PixelShader(Views[0].ShaderMap);

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("SubsurfaceViewportCopy"),
			PassParameters,
			ERDGPassFlags::Raster,
			[&Views, ViewMask, ViewCount, PixelShader, SceneTexture, PassParameters](FRHICommandListImmediate& RHICmdList)
		{
			for (uint32 ViewIndex = 0; ViewIndex < ViewCount; ++ViewIndex)
			{
				const uint32 ViewBit = 1 << ViewIndex;

				const bool bIsNonSubsurfaceView = (ViewMask & ViewBit) == 0;

				if (bIsNonSubsurfaceView)
				{
					const FViewInfo& View = Views[ViewIndex];
					const FScreenPassTextureViewport TextureViewport(SceneTexture, View.ViewRect);

					DrawScreenPass(RHICmdList, View, TextureViewport, TextureViewport, *PixelShader, *PassParameters);
				}
			}
		});

		// Subsequent render passes should load the texture contents.
		SceneTextureLoadAction = ERenderTargetLoadAction::ELoad;
	}

	for (uint32 ViewIndex = 0; ViewIndex < ViewCount; ++ViewIndex)
	{
		const uint32 ViewBit = 1 << ViewIndex;

		const bool bIsSubsurfaceView = (ViewMask & ViewBit) != 0;

		if (bIsSubsurfaceView)
		{
			RDG_EVENT_SCOPE(GraphBuilder, "SubsurfaceScattering(ViewId=%d)", ViewIndex);

			const FViewInfo& View = Views[ViewIndex];
			const FScreenPassTextureViewport SceneViewport(SceneTexture, View.ViewRect);

			ComputeSubsurfaceForView(GraphBuilder, View, SceneViewport, SceneTexture, SceneTextureOutput, SceneTextureLoadAction);

			// Subsequent render passes should load the texture contents.
			SceneTextureLoadAction = ERenderTargetLoadAction::ELoad;
		}
	}

	return SceneTextureOutput;
}

FScreenPassTexture AddVisualizeSubsurfacePass(FRDGBuilder& GraphBuilder, const FViewInfo& View, const FVisualizeSubsurfaceInputs& Inputs)
{
	check(Inputs.SceneColor.IsValid());

	FScreenPassRenderTarget Output = Inputs.OverrideOutput;

	if (!Output.IsValid())
	{
		Output = FScreenPassRenderTarget::CreateFromInput(GraphBuilder, Inputs.SceneColor, View.GetOverwriteLoadAction(), TEXT("VisualizeSubsurface"));
	}

	const FScreenPassTextureViewport InputViewport(Inputs.SceneColor);

	FSubsurfaceVisualizePS::FParameters* PassParameters = GraphBuilder.AllocParameters<FSubsurfaceVisualizePS::FParameters>();
	PassParameters->Subsurface = GetSubsurfaceCommonParameters(GraphBuilder.RHICmdList, View);
	PassParameters->RenderTargets[0] = Output.GetRenderTargetBinding();
	PassParameters->SubsurfaceInput0.Texture = Inputs.SceneColor.Texture;
	PassParameters->SubsurfaceInput0.Viewport = GetScreenPassTextureViewportParameters(InputViewport);
	PassParameters->SubsurfaceSampler0 = TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	PassParameters->MiniFontTexture = GetMiniFontTexture();

	TShaderMapRef<FSubsurfaceVisualizePS> PixelShader(View.ShaderMap);

	RDG_EVENT_SCOPE(GraphBuilder, "VisualizeSubsurface");

	AddDrawScreenPass(GraphBuilder, RDG_EVENT_NAME("Visualizer"), View, FScreenPassTextureViewport(Output), InputViewport, *PixelShader, PassParameters);

	Output.LoadAction = ERenderTargetLoadAction::ELoad;

	AddDrawCanvasPass(GraphBuilder, RDG_EVENT_NAME("Text"), View, Output, [](FCanvas& Canvas)
	{
		float X = 30;
		float Y = 28;
		const float YStep = 14;

		FString Line = FString::Printf(TEXT("Visualize Screen Space Subsurface Scattering"));
		Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), FLinearColor(1, 1, 1));

		Y += YStep;

		uint32 Index = 0;
		while (GSubsurfaceProfileTextureObject.GetEntryString(Index++, Line))
		{
			Canvas.DrawShadowedString(X, Y += YStep, *Line, GetStatsFont(), FLinearColor(1, 1, 1));
		}
	});

	return MoveTemp(Output);
}

//////////////////////////////////////////////////////////////////////////
//! Shim methods to hook into the legacy pipeline until the full RDG conversion is complete.

void ComputeSubsurfaceShim(FRHICommandListImmediate& RHICmdList, const TArray<FViewInfo>& Views)
{
	FSceneRenderTargets& SceneRenderTargets = FSceneRenderTargets::Get(RHICmdList);

	FRDGBuilder GraphBuilder(RHICmdList);

	FRDGTextureRef SceneTexture = GraphBuilder.RegisterExternalTexture(SceneRenderTargets.GetSceneColor(), TEXT("SceneColor"));

	FRDGTextureRef SceneTextureOutput = ComputeSubsurface(GraphBuilder, SceneTexture, Views);

	// Extract the result texture out and re-assign it to the scene render targets blackboard.
	TRefCountPtr<IPooledRenderTarget> SceneTarget;
	GraphBuilder.QueueTextureExtraction(SceneTextureOutput, &SceneTarget, false);
	GraphBuilder.Execute();

	SceneRenderTargets.SetSceneColor(SceneTarget);

	// The RT should be released as early as possible to allow sharing of that memory for other purposes.
	// This becomes even more important with some limited VRam (XBoxOne).
	SceneRenderTargets.SetLightAttenuation(nullptr);
}

//////////////////////////////////////////////////////////////////////////