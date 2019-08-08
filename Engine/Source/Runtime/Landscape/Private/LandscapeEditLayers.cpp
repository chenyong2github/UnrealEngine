// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
LandscapeEditLayers.cpp: Landscape editing layers mode
=============================================================================*/

#include "LandscapeEdit.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "LandscapeStreamingProxy.h"
#include "LandscapeInfo.h"
#include "LandscapeComponent.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeDataAccess.h"
#include "LandscapePrivate.h"

#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "Engine/TextureRenderTarget2D.h"

#if WITH_EDITOR
#include "LandscapeEditorModule.h"
#include "LandscapeToolInterface.h"
#include "ComponentRecreateRenderStateContext.h"
#include "LandscapeBlueprintBrushBase.h"
#include "Materials/MaterialInstanceConstant.h"
#include "LandscapeMaterialInstanceConstant.h"
#include "Materials/MaterialExpressionLandscapeVisibilityMask.h"
#include "ShaderCompiler.h"
#include "Algo/Count.h"
#include "LandscapeSettings.h"
#include "LandscapeRender.h"
#include "LandscapeInfoMap.h"
#include "Misc/MessageDialog.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/UObjectThreadContext.h"
#endif

#define LOCTEXT_NAMESPACE "Landscape"

static const FString GEmptyDebugName(TEXT(""));

ENGINE_API extern bool GDisableAutomaticTextureMaterialUpdateDependencies;

#if WITH_EDITOR
static TAutoConsoleVariable<int32> CVarOutputLayersDebugDrawCallName(
	TEXT("landscape.OutputLayersDebugDrawCallName"),
	0,
	TEXT("This will output the name of each draw call for Scope Draw call event. This will allow readable draw call info through RenderDoc, for example."));

static TAutoConsoleVariable<int32> CVarOutputLayersRTContent(
	TEXT("landscape.OutputLayersRTContent"),
	0,
	TEXT("This will output the content of render target. This is used for debugging only."));

static TAutoConsoleVariable<int32> CVarOutputLayersWeightmapsRTContent(
	TEXT("landscape.OutputLayersWeightmapsRTContent"),
	0,
	TEXT("This will output the content of render target used for weightmap. This is used for debugging only."));


static TAutoConsoleVariable<int32> CVarLandscapeSimulatePhysics(
	TEXT("landscape.SimulatePhysics"),
	0,
	TEXT("This will enable physic simulation on worlds containing landscape."));
#endif

DECLARE_GPU_STAT_NAMED(LandscapeLayersRender, TEXT("Landscape Layer System Render"));

// Custom Resources

class FLandscapeTexture2DResource : public FTextureResource
{
public:
	FLandscapeTexture2DResource(uint32 InSizeX, uint32 InSizeY, EPixelFormat InFormat, uint32 InNumMips, bool InNeedUAV)
		: SizeX(InSizeX)
		, SizeY(InSizeY)
		, Format(InFormat)
		, NumMips(InNumMips)
		, CreateUAV(InNeedUAV)
	{}

	virtual uint32 GetSizeX() const override
	{
		return SizeX;
	}

	virtual uint32 GetSizeY() const override
	{
		return SizeY;
	}

	/** Called when the resource is initialized. This is only called by the rendering thread. */
	virtual void InitRHI() override
	{
		FTextureResource::InitRHI();

		FRHIResourceCreateInfo CreateInfo;
		uint32 Flags = TexCreate_NoTiling | TexCreate_OfflineProcessed;

		if (CreateUAV)
		{
			Flags |= TexCreate_UAV;
		}

		TextureRHI = RHICreateTexture2D(SizeX, SizeY, Format, NumMips, 1, Flags, CreateInfo);

		if (CreateUAV)
		{
			TextureUAV = RHICreateUnorderedAccessView(TextureRHI, 0);
		}
	}

	virtual void ReleaseRHI() override
	{
		if (CreateUAV)
		{
			TextureUAV.SafeRelease();
		}
		
		FTextureResource::ReleaseRHI();
	}

	FUnorderedAccessViewRHIRef TextureUAV;

private:
	uint32 SizeX;
	uint32 SizeY;
	EPixelFormat Format;
	uint32 NumMips;
	bool CreateUAV;
};

class FLandscapeTexture2DArrayResource : public FTextureResource
{
public:
	FLandscapeTexture2DArrayResource(uint32 InSizeX, uint32 InSizeY, uint32 InSizeZ, EPixelFormat InFormat, uint32 InNumMips, bool InNeedUAV)
		: SizeX(InSizeX)
		, SizeY(InSizeY)
		, SizeZ(InSizeZ)
		, Format(InFormat)
		, NumMips(InNumMips)
		, CreateUAV(InNeedUAV)
	{}

	virtual uint32 GetSizeX() const override
	{
		return SizeX;
	}

	virtual uint32 GetSizeY() const override
	{
		return SizeY;
	}

	virtual uint32 GetSizeZ() const
	{
		return SizeZ;
	}

	/** Called when the resource is initialized. This is only called by the rendering thread. */
	virtual void InitRHI() override
	{
		FTextureResource::InitRHI();

		FRHIResourceCreateInfo CreateInfo;
		uint32 Flags = TexCreate_NoTiling | TexCreate_OfflineProcessed;

		if (CreateUAV)
		{
			Flags |= TexCreate_UAV;
		}

		TextureRHI = RHICreateTexture2DArray(SizeX, SizeY, SizeZ, Format, NumMips, 1, Flags, CreateInfo);

		if (CreateUAV)
		{
			TextureUAV = RHICreateUnorderedAccessView(TextureRHI, 0);
		}
	}

	virtual void ReleaseRHI() override
	{
		FTextureResource::ReleaseRHI();

		TextureUAV.SafeRelease();
	}

	FUnorderedAccessViewRHIRef TextureUAV;

private:
	uint32 SizeX;
	uint32 SizeY;
	uint32 SizeZ;
	EPixelFormat Format;
	uint32 NumMips;
	bool CreateUAV;
};

// Vertex format and vertex buffer

struct FLandscapeLayersVertex
{
	FVector2D Position;
	FVector2D UV;
};

struct FLandscapeLayersTriangle
{
	FLandscapeLayersVertex V0;
	FLandscapeLayersVertex V1;
	FLandscapeLayersVertex V2;
};

class FLandscapeLayersVertexDeclaration : public FRenderResource
{
public:
	FVertexDeclarationRHIRef VertexDeclarationRHI;

	/** Destructor. */
	virtual ~FLandscapeLayersVertexDeclaration() {}

	virtual void InitRHI()
	{
		FVertexDeclarationElementList Elements;
		uint32 Stride = sizeof(FLandscapeLayersVertex);
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FLandscapeLayersVertex, Position), VET_Float2, 0, Stride));
		Elements.Add(FVertexElement(0, STRUCT_OFFSET(FLandscapeLayersVertex, UV), VET_Float2, 1, Stride));
		VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
	}

	virtual void ReleaseRHI()
	{
		VertexDeclarationRHI.SafeRelease();
	}
};

class FLandscapeLayersVertexBuffer : public FVertexBuffer
{
public:
	void Init(const TArray<FLandscapeLayersTriangle>& InTriangleList)
	{
		TriangleList = InTriangleList;
	}

private:

	/** Initialize the RHI for this rendering resource */
	void InitRHI() override
	{
		TResourceArray<FLandscapeLayersVertex, VERTEXBUFFER_ALIGNMENT> Vertices;
		Vertices.SetNumUninitialized(TriangleList.Num() * 3);

		for (int32 i = 0; i < TriangleList.Num(); ++i)
		{
			Vertices[i * 3 + 0] = TriangleList[i].V0;
			Vertices[i * 3 + 1] = TriangleList[i].V1;
			Vertices[i * 3 + 2] = TriangleList[i].V2;
		}

		// Create vertex buffer. Fill buffer with initial data upon creation
		FRHIResourceCreateInfo CreateInfo(&Vertices);
		VertexBufferRHI = RHICreateVertexBuffer(Vertices.GetResourceDataSize(), BUF_Static, CreateInfo);
	}

	TArray<FLandscapeLayersTriangle> TriangleList;
};

// Custom Pixel and Vertex shaders

class FLandscapeLayersVS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeLayersVS)

public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && !IsConsolePlatform(Parameters.Platform) && !IsMetalMobilePlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
	}

	FLandscapeLayersVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		TransformParam.Bind(Initializer.ParameterMap, TEXT("Transform"), SPF_Mandatory);
	}

	FLandscapeLayersVS()
	{}

	void SetParameters(FRHICommandList& RHICmdList, const FMatrix& InProjectionMatrix)
	{
		SetShaderValue(RHICmdList, GetVertexShader(), TransformParam, InProjectionMatrix);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FShader::Serialize(Ar);
		Ar << TransformParam;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderParameter TransformParam;
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeLayersVS, "/Engine/Private/LandscapeLayersVS.usf", "VSMain", SF_Vertex);

struct FLandscapeLayersHeightmapShaderParameters
{
	FLandscapeLayersHeightmapShaderParameters()
		: ReadHeightmap1(nullptr)
		, ReadHeightmap2(nullptr)
		, HeightmapSize(0, 0)
		, ApplyLayerModifiers(false)
		, LayerAlpha(1.0f)
		, LayerVisible(true)
		, LayerBlendMode(LSBM_AdditiveBlend)
		, GenerateNormals(false)
		, GridSize(0.0f, 0.0f, 0.0f)
		, CurrentMipSize(0, 0)
		, ParentMipSize(0, 0)
		, CurrentMipComponentVertexCount(0)
	{}

	UTexture* ReadHeightmap1;
	UTexture* ReadHeightmap2;
	FIntPoint HeightmapSize;
	bool ApplyLayerModifiers;
	float LayerAlpha;
	bool LayerVisible;
	ELandscapeBlendMode LayerBlendMode;
	bool GenerateNormals;
	FVector GridSize;
	FIntPoint CurrentMipSize;
	FIntPoint ParentMipSize;
	int32 CurrentMipComponentVertexCount;
};

class FLandscapeLayersHeightmapPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeLayersHeightmapPS);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && !IsConsolePlatform(Parameters.Platform) && !IsMetalMobilePlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
	}

	FLandscapeLayersHeightmapPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ReadTexture1Param.Bind(Initializer.ParameterMap, TEXT("ReadTexture1"));
		ReadTexture2Param.Bind(Initializer.ParameterMap, TEXT("ReadTexture2"));
		ReadTexture1SamplerParam.Bind(Initializer.ParameterMap, TEXT("ReadTexture1Sampler"));
		ReadTexture2SamplerParam.Bind(Initializer.ParameterMap, TEXT("ReadTexture2Sampler"));

		LayerInfoParam.Bind(Initializer.ParameterMap, TEXT("LayerInfo"));
		OutputConfigParam.Bind(Initializer.ParameterMap, TEXT("OutputConfig"));
		TextureSizeParam.Bind(Initializer.ParameterMap, TEXT("TextureSize"));
		LandscapeGridScaleParam.Bind(Initializer.ParameterMap, TEXT("LandscapeGridScale"));
		ComponentVertexCountParam.Bind(Initializer.ParameterMap, TEXT("CurrentMipComponentVertexCount"));
	}

	FLandscapeLayersHeightmapPS()
	{}

	void SetParameters(FRHICommandList& RHICmdList, const FLandscapeLayersHeightmapShaderParameters& InParams)
	{
		SetTextureParameter(RHICmdList, GetPixelShader(), ReadTexture1Param, ReadTexture1SamplerParam, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InParams.ReadHeightmap1->Resource->TextureRHI);
		SetTextureParameter(RHICmdList, GetPixelShader(), ReadTexture2Param, ReadTexture2SamplerParam, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InParams.ReadHeightmap2 != nullptr ? InParams.ReadHeightmap2->Resource->TextureRHI : GWhiteTexture->TextureRHI);

		FVector4 LayerInfo(InParams.LayerAlpha, InParams.LayerVisible ? 1.0f : 0.0f, InParams.LayerBlendMode == LSBM_AlphaBlend ? 1.0f : 0.f, 0.f);
		FVector4 OutputConfig(InParams.ApplyLayerModifiers ? 1.0f : 0.0f, 0.0f /*unused*/, InParams.ReadHeightmap2 ? 1.0f : 0.0f, InParams.GenerateNormals ? 1.0f : 0.0f);
		FVector2D TextureSize(InParams.HeightmapSize.X, InParams.HeightmapSize.Y);

		SetShaderValue(RHICmdList, GetPixelShader(), LayerInfoParam, LayerInfo);
		SetShaderValue(RHICmdList, GetPixelShader(), OutputConfigParam, OutputConfig);
		SetShaderValue(RHICmdList, GetPixelShader(), TextureSizeParam, TextureSize);
		SetShaderValue(RHICmdList, GetPixelShader(), LandscapeGridScaleParam, InParams.GridSize);
		SetShaderValue(RHICmdList, GetPixelShader(), ComponentVertexCountParam, (float)InParams.CurrentMipComponentVertexCount);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FShader::Serialize(Ar);
		Ar << ReadTexture1Param;
		Ar << ReadTexture2Param;
		Ar << ReadTexture1SamplerParam;
		Ar << ReadTexture2SamplerParam;
		Ar << LayerInfoParam;
		Ar << OutputConfigParam;
		Ar << TextureSizeParam;
		Ar << LandscapeGridScaleParam;
		Ar << ComponentVertexCountParam;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderResourceParameter ReadTexture1Param;
	FShaderResourceParameter ReadTexture2Param;
	FShaderResourceParameter ReadTexture1SamplerParam;
	FShaderResourceParameter ReadTexture2SamplerParam;
	FShaderParameter LayerInfoParam;
	FShaderParameter OutputConfigParam;
	FShaderParameter TextureSizeParam;
	FShaderParameter LandscapeGridScaleParam;
	FShaderParameter ComponentVertexCountParam;
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeLayersHeightmapPS, "/Engine/Private/LandscapeLayersPS.usf", "PSHeightmapMain", SF_Pixel);

class FLandscapeLayersHeightmapMipsPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeLayersHeightmapMipsPS);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && !IsConsolePlatform(Parameters.Platform) && !IsMetalMobilePlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
	}

	FLandscapeLayersHeightmapMipsPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ReadTexture1Param.Bind(Initializer.ParameterMap, TEXT("ReadTexture1"));
		ReadTexture1SamplerParam.Bind(Initializer.ParameterMap, TEXT("ReadTexture1Sampler"));
		CurrentMipSizeParam.Bind(Initializer.ParameterMap, TEXT("CurrentMipTextureSize"));
		ParentMipSizeParam.Bind(Initializer.ParameterMap, TEXT("ParentMipTextureSize"));
		CurrentMipComponentVertexCountParam.Bind(Initializer.ParameterMap, TEXT("CurrentMipComponentVertexCount"));
	}

	FLandscapeLayersHeightmapMipsPS()
	{}

	void SetParameters(FRHICommandList& RHICmdList, const FLandscapeLayersHeightmapShaderParameters& InParams)
	{
		SetTextureParameter(RHICmdList, GetPixelShader(), ReadTexture1Param, ReadTexture1SamplerParam, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InParams.ReadHeightmap1->Resource->TextureRHI);

		SetShaderValue(RHICmdList, GetPixelShader(), CurrentMipSizeParam, FVector2D(InParams.CurrentMipSize.X, InParams.CurrentMipSize.Y));
		SetShaderValue(RHICmdList, GetPixelShader(), ParentMipSizeParam, FVector2D(InParams.ParentMipSize.X, InParams.ParentMipSize.Y));
		SetShaderValue(RHICmdList, GetPixelShader(), CurrentMipComponentVertexCountParam, (float)InParams.CurrentMipComponentVertexCount);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FShader::Serialize(Ar);
		Ar << ReadTexture1Param;
		Ar << ReadTexture1SamplerParam;
		Ar << CurrentMipSizeParam;
		Ar << ParentMipSizeParam;
		Ar << CurrentMipComponentVertexCountParam;

		return bShaderHasOutdatedParameters;
	}

private:
	FShaderResourceParameter ReadTexture1Param;
	FShaderResourceParameter ReadTexture1SamplerParam;
	FShaderParameter CurrentMipSizeParam;
	FShaderParameter ParentMipSizeParam;
	FShaderParameter CurrentMipComponentVertexCountParam;
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeLayersHeightmapMipsPS, "/Engine/Private/LandscapeLayersPS.usf", "PSHeightmapMainMips", SF_Pixel);

struct FLandscapeLayersWeightmapShaderParameters
{
	FLandscapeLayersWeightmapShaderParameters()
		: ReadWeightmap1(nullptr)
		, ReadWeightmap2(nullptr)
		, ApplyLayerModifiers(false)
		, LayerAlpha(1.0f)
		, LayerVisible(true)
		, LayerBlendMode(LSBM_AdditiveBlend)
		, OutputAsSubstractive(false)
		, OutputAsNormalized(false)
		, CurrentMipSize(0, 0)
		, ParentMipSize(0, 0)
		, CurrentMipComponentVertexCount(0)
	{}

	UTexture* ReadWeightmap1;
	UTexture* ReadWeightmap2;
	bool ApplyLayerModifiers;
	float LayerAlpha;
	bool LayerVisible;
	ELandscapeBlendMode LayerBlendMode;
	bool OutputAsSubstractive;
	bool OutputAsNormalized;
	FIntPoint CurrentMipSize;
	FIntPoint ParentMipSize;
	int32 CurrentMipComponentVertexCount;
};

class FLandscapeLayersWeightmapPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeLayersWeightmapPS);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && !IsConsolePlatform(Parameters.Platform) && !IsMetalMobilePlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
	}

	FLandscapeLayersWeightmapPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ReadTexture1Param.Bind(Initializer.ParameterMap, TEXT("ReadTexture1"));
		ReadTexture2Param.Bind(Initializer.ParameterMap, TEXT("ReadTexture2"));
		ReadTexture1SamplerParam.Bind(Initializer.ParameterMap, TEXT("ReadTexture1Sampler"));
		ReadTexture2SamplerParam.Bind(Initializer.ParameterMap, TEXT("ReadTexture2Sampler"));
		LayerInfoParam.Bind(Initializer.ParameterMap, TEXT("LayerInfo"));
		OutputConfigParam.Bind(Initializer.ParameterMap, TEXT("OutputConfig"));
		ComponentVertexCountParam.Bind(Initializer.ParameterMap, TEXT("CurrentMipComponentVertexCount"));
	}

	FLandscapeLayersWeightmapPS()
	{}

	void SetParameters(FRHICommandList& RHICmdList, const FLandscapeLayersWeightmapShaderParameters& InParams)
	{
		SetTextureParameter(RHICmdList, GetPixelShader(), ReadTexture1Param, ReadTexture1SamplerParam, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InParams.ReadWeightmap1->Resource->TextureRHI);
		SetTextureParameter(RHICmdList, GetPixelShader(), ReadTexture2Param, ReadTexture2SamplerParam, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InParams.ReadWeightmap2 != nullptr ? InParams.ReadWeightmap2->Resource->TextureRHI : GWhiteTexture->TextureRHI);

		FVector4 LayerInfo(InParams.LayerAlpha, InParams.LayerVisible ? 1.0f : 0.0f, InParams.LayerBlendMode == LSBM_AlphaBlend ? 1.0f : 0.f, 0.f);
		FVector4 OutputConfig(InParams.ApplyLayerModifiers ? 1.0f : 0.0f, InParams.OutputAsSubstractive ? 1.0f : 0.0f, InParams.ReadWeightmap2 != nullptr ? 1.0f : 0.0f, InParams.OutputAsNormalized ? 1.0f : 0.0f);

		SetShaderValue(RHICmdList, GetPixelShader(), LayerInfoParam, LayerInfo);
		SetShaderValue(RHICmdList, GetPixelShader(), OutputConfigParam, OutputConfig);
		SetShaderValue(RHICmdList, GetPixelShader(), ComponentVertexCountParam, (float)InParams.CurrentMipComponentVertexCount);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FShader::Serialize(Ar);
		Ar << ReadTexture1Param;
		Ar << ReadTexture2Param;
		Ar << ReadTexture1SamplerParam;
		Ar << ReadTexture2SamplerParam;
		Ar << LayerInfoParam;
		Ar << OutputConfigParam;
		Ar << ComponentVertexCountParam;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderResourceParameter ReadTexture1Param;
	FShaderResourceParameter ReadTexture2Param;
	FShaderResourceParameter ReadTexture1SamplerParam;
	FShaderResourceParameter ReadTexture2SamplerParam;
	FShaderParameter LayerInfoParam;
	FShaderParameter OutputConfigParam;
	FShaderParameter ComponentVertexCountParam;
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeLayersWeightmapPS, "/Engine/Private/LandscapeLayersPS.usf", "PSWeightmapMain", SF_Pixel);

class FLandscapeLayersWeightmapMipsPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeLayersWeightmapMipsPS);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && !IsConsolePlatform(Parameters.Platform) && !IsMetalMobilePlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
	}

	FLandscapeLayersWeightmapMipsPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ReadTexture1Param.Bind(Initializer.ParameterMap, TEXT("ReadTexture1"));
		ReadTexture1SamplerParam.Bind(Initializer.ParameterMap, TEXT("ReadTexture1Sampler"));
		CurrentMipSizeParam.Bind(Initializer.ParameterMap, TEXT("CurrentMipTextureSize"));
		ParentMipSizeParam.Bind(Initializer.ParameterMap, TEXT("ParentMipTextureSize"));
		CurrentMipComponentVertexCountParam.Bind(Initializer.ParameterMap, TEXT("CurrentMipComponentVertexCount"));
	}

	FLandscapeLayersWeightmapMipsPS()
	{}

	void SetParameters(FRHICommandList& RHICmdList, const FLandscapeLayersWeightmapShaderParameters& InParams)
	{
		SetTextureParameter(RHICmdList, GetPixelShader(), ReadTexture1Param, ReadTexture1SamplerParam, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InParams.ReadWeightmap1->Resource->TextureRHI);

		SetShaderValue(RHICmdList, GetPixelShader(), CurrentMipSizeParam, FVector2D(InParams.CurrentMipSize.X, InParams.CurrentMipSize.Y));
		SetShaderValue(RHICmdList, GetPixelShader(), ParentMipSizeParam, FVector2D(InParams.ParentMipSize.X, InParams.ParentMipSize.Y));
		SetShaderValue(RHICmdList, GetPixelShader(), CurrentMipComponentVertexCountParam, (float)InParams.CurrentMipComponentVertexCount);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FShader::Serialize(Ar);
		Ar << ReadTexture1Param;
		Ar << ReadTexture1SamplerParam;
		Ar << CurrentMipSizeParam;
		Ar << ParentMipSizeParam;
		Ar << CurrentMipComponentVertexCountParam;

		return bShaderHasOutdatedParameters;
	}

private:
	FShaderResourceParameter ReadTexture1Param;
	FShaderResourceParameter ReadTexture1SamplerParam;
	FShaderParameter CurrentMipSizeParam;
	FShaderParameter ParentMipSizeParam;
	FShaderParameter CurrentMipComponentVertexCountParam;
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeLayersWeightmapMipsPS, "/Engine/Private/LandscapeLayersPS.usf", "PSWeightmapMainMips", SF_Pixel);

struct FLandscapeLayersWeightmapConvertFormatShaderParameter
{
	FLandscapeLayersWeightmapConvertFormatShaderParameter()
		: ReadWeightmap(nullptr)
	{}

	FLandscapeTexture2DResource* ReadWeightmap;
};

// This Shader exists only to support Win7 because DX11.0 doesn't support UAVs on BGRA8 Textures.
// The Shader will convert from R32 Format to BGRA8 Format
class FLandscapeLayersWeightmapConvertFormatPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeLayersWeightmapConvertFormatPS);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && !IsConsolePlatform(Parameters.Platform) && !IsMetalPlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
	}
	FLandscapeLayersWeightmapConvertFormatPS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ReadTexture1Param.Bind(Initializer.ParameterMap, TEXT("ReadTexture1"));
		ReadTexture1SamplerParam.Bind(Initializer.ParameterMap, TEXT("ReadTexture1Sampler"));
	}

	FLandscapeLayersWeightmapConvertFormatPS()
	{}

	void SetParameters(FRHICommandList& RHICmdList, const FLandscapeLayersWeightmapConvertFormatShaderParameter& InParams)
	{
		SetTextureParameter(RHICmdList, GetPixelShader(), ReadTexture1Param, ReadTexture1SamplerParam, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InParams.ReadWeightmap->TextureRHI);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FShader::Serialize(Ar);
		Ar << ReadTexture1Param;
		Ar << ReadTexture1SamplerParam;

		return bShaderHasOutdatedParameters;
	}

	FShaderResourceParameter ReadTexture1Param;
	FShaderResourceParameter ReadTexture1SamplerParam;
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeLayersWeightmapConvertFormatPS, "/Engine/Private/LandscapeLayersPS.usf", "PSWeightmapConvertFormat", SF_Pixel);

// Compute shaders data

int32 GLandscapeLayerWeightmapThreadGroupSizeX = 16;
int32 GLandscapeLayerWeightmapThreadGroupSizeY = 16;

struct FLandscapeLayerWeightmapExtractMaterialLayersComponentData
{
	FIntPoint ComponentVertexPosition;	// Section base converted to vertex instead of quad
	uint32 DestinationPaintLayerIndex;	// correspond to which layer info object index the data should be stored in the texture 2d array
	uint32 WeightmapChannelToProcess;	// correspond to which RGBA channel to process
	FIntPoint AtlasTexturePositionOutput;	// This represent the location we will write layer information
};

class FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderResource : public FRenderResource
{
public:
	FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderResource(const TArray<FLandscapeLayerWeightmapExtractMaterialLayersComponentData>& InComponentsData)
		: OriginalComponentsData(InComponentsData)
		, ComponentsDataCount(OriginalComponentsData.Num())
	{}

	~FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderResource()
	{
		ComponentsData.SafeRelease();
		ComponentsDataSRV.SafeRelease();
	}

	/** Called when the resource is initialized. This is only called by the rendering thread. */
	virtual void InitDynamicRHI() override
	{
		FRHIResourceCreateInfo CreateInfo;
		ComponentsData = RHICreateStructuredBuffer(sizeof(FLandscapeLayerWeightmapExtractMaterialLayersComponentData), OriginalComponentsData.Num() * sizeof(FLandscapeLayerWeightmapExtractMaterialLayersComponentData), BUF_ShaderResource | BUF_Volatile, CreateInfo);
		ComponentsDataSRV = RHICreateShaderResourceView(ComponentsData);

		uint8* Buffer = (uint8*)RHILockStructuredBuffer(ComponentsData, 0, OriginalComponentsData.Num() * sizeof(FLandscapeLayerWeightmapExtractMaterialLayersComponentData), RLM_WriteOnly);
		FMemory::Memcpy(Buffer, OriginalComponentsData.GetData(), OriginalComponentsData.Num() * sizeof(FLandscapeLayerWeightmapExtractMaterialLayersComponentData));
		RHIUnlockStructuredBuffer(ComponentsData);
	}

	virtual void ReleaseDynamicRHI() override
	{
		ComponentsData.SafeRelease();
		ComponentsDataSRV.SafeRelease();
	}

	int32 GetComponentsDataCount() const
	{
		return ComponentsDataCount;
	}

private:
	friend class FLandscapeLayerWeightmapExtractMaterialLayersCS;

	FStructuredBufferRHIRef ComponentsData;
	FShaderResourceViewRHIRef ComponentsDataSRV;
	TArray<FLandscapeLayerWeightmapExtractMaterialLayersComponentData> OriginalComponentsData;
	int32 ComponentsDataCount;
};

struct FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderParameters
{
	FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderParameters()
		: ComponentWeightmapResource(nullptr)
		, ComputeShaderResource(nullptr)
		, AtlasWeightmapsPerLayer(nullptr)
		, ComponentSize(0)
	{}

	FLandscapeTexture2DResource* ComponentWeightmapResource;
	FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderResource* ComputeShaderResource;
	FLandscapeTexture2DArrayResource* AtlasWeightmapsPerLayer;
	uint32 ComponentSize;
};

class FLandscapeLayerWeightmapExtractMaterialLayersCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeLayerWeightmapExtractMaterialLayersCS);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && !IsConsolePlatform(Parameters.Platform) && !IsMetalMobilePlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), GLandscapeLayerWeightmapThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), GLandscapeLayerWeightmapThreadGroupSizeY);
	}

	FLandscapeLayerWeightmapExtractMaterialLayersCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ComponentWeightmapParam.Bind(Initializer.ParameterMap, TEXT("InComponentWeightMaps"));
		AtlasPaintListsParam.Bind(Initializer.ParameterMap, TEXT("OutAtlasPaintLayers"));
		ComponentsDataParam.Bind(Initializer.ParameterMap, TEXT("InExtractLayersComponentsData"));
		ComponentSizeParam.Bind(Initializer.ParameterMap, TEXT("ComponentSize"));
	}

	FLandscapeLayerWeightmapExtractMaterialLayersCS()
	{}

	void SetParameters(FRHICommandList& RHICmdList, const FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderParameters& InParams)
	{
		SetTextureParameter(RHICmdList, GetComputeShader(), ComponentWeightmapParam, InParams.ComponentWeightmapResource->TextureRHI);
		SetUAVParameter(RHICmdList, GetComputeShader(), AtlasPaintListsParam, InParams.AtlasWeightmapsPerLayer->TextureUAV);
		SetSRVParameter(RHICmdList, GetComputeShader(), ComponentsDataParam, InParams.ComputeShaderResource->ComponentsDataSRV);
		SetShaderValue(RHICmdList, GetComputeShader(), ComponentSizeParam, InParams.ComponentSize);
	}

	void UnsetParameters(FRHICommandList& RHICmdList)
	{
		SetUAVParameter(RHICmdList, GetComputeShader(), AtlasPaintListsParam, nullptr);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FShader::Serialize(Ar);
		Ar << ComponentWeightmapParam;
		Ar << AtlasPaintListsParam;
		Ar << ComponentsDataParam;
		Ar << ComponentSizeParam;

		return bShaderHasOutdatedParameters;
	}

private:
	FShaderResourceParameter ComponentWeightmapParam;
	FShaderResourceParameter AtlasPaintListsParam;
	FShaderResourceParameter ComponentsDataParam;
	FShaderParameter ComponentSizeParam;
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeLayerWeightmapExtractMaterialLayersCS, "/Engine/Private/LandscapeLayersCS.usf", "ComputeWeightmapPerPaintLayer", SF_Compute);

class FLandscapeLayerWeightmapExtractMaterialLayersCSDispatch_RenderThread
{
public:
	FLandscapeLayerWeightmapExtractMaterialLayersCSDispatch_RenderThread(const FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderParameters& InShaderParams)
		: ShaderParams(InShaderParams)
	{}

	void ExtractLayers(FRHICommandListImmediate& InRHICmdList)
	{
		SCOPE_CYCLE_COUNTER(STAT_LandscapeLayersRegenerate_RenderThread);
		SCOPED_DRAW_EVENTF(InRHICmdList, LandscapeLayersRender, TEXT("ExtractLayers"));
		SCOPED_GPU_STAT(InRHICmdList, LandscapeLayersRender);

		TShaderMapRef<FLandscapeLayerWeightmapExtractMaterialLayersCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		InRHICmdList.SetComputeShader(ComputeShader->GetComputeShader());
		ComputeShader->SetParameters(InRHICmdList, ShaderParams);

		uint32 ThreadGroupCountX = FMath::CeilToInt((float)ShaderParams.ComponentSize / (float)GLandscapeLayerWeightmapThreadGroupSizeX);
		uint32 ThreadGroupCountY = FMath::CeilToInt((float)ShaderParams.ComponentSize / (float)GLandscapeLayerWeightmapThreadGroupSizeY);
		check(ThreadGroupCountX > 0 && ThreadGroupCountY > 0);

		DispatchComputeShader(InRHICmdList, *ComputeShader, ThreadGroupCountX, ThreadGroupCountY, ShaderParams.ComputeShaderResource->GetComponentsDataCount());
		ComputeShader->UnsetParameters(InRHICmdList);
		ShaderParams.ComputeShaderResource->ReleaseResource();
		delete ShaderParams.ComputeShaderResource;
	}

private:
	FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderParameters ShaderParams;
};

struct FLandscapeLayerWeightmapPackMaterialLayersComponentData
{
	int32 ComponentVertexPositionX[4];		// Section base converted to vertex instead of quad
	int32 ComponentVertexPositionY[4];		// Section base converted to vertex instead of quad
	int32 SourcePaintLayerIndex[4];			// correspond to which layer info object index the data should be loaded from the texture 2d array
	int32 WeightmapChannelToProcess[4];		// correspond to which RGBA channel to process
};

class FLandscapeLayerWeightmapPackMaterialLayersComputeShaderResource : public FRenderResource
{
public:
	FLandscapeLayerWeightmapPackMaterialLayersComputeShaderResource(const TArray<FLandscapeLayerWeightmapPackMaterialLayersComponentData>& InComponentsData, const TArray<float>& InWeightmapWeightBlendModeData, const TArray<FVector2D>& InTextureOutputOffset)
		: OriginalComponentsData(InComponentsData)
		, ComponentsDataCount(OriginalComponentsData.Num())
		, OriginalWeightmapWeightBlendModeData(InWeightmapWeightBlendModeData)
		, OriginalTextureOutputOffset(InTextureOutputOffset)
	{}

	~FLandscapeLayerWeightmapPackMaterialLayersComputeShaderResource()
	{
		ComponentsData.SafeRelease();
		ComponentsDataSRV.SafeRelease();
		WeightmapWeightBlendModeSRV.SafeRelease();
		WeightmapTextureOutputOffsetSRV.SafeRelease();
	}

	/** Called when the resource is initialized. This is only called by the rendering thread. */
	virtual void InitDynamicRHI() override
	{
		FRHIResourceCreateInfo CreateInfo;
		uint32 ComponentsDataMemSize = OriginalComponentsData.Num() * sizeof(FLandscapeLayerWeightmapPackMaterialLayersComponentData);
		ComponentsData = RHICreateStructuredBuffer(sizeof(FLandscapeLayerWeightmapPackMaterialLayersComponentData), ComponentsDataMemSize, BUF_ShaderResource | BUF_Volatile, CreateInfo);
		ComponentsDataSRV = RHICreateShaderResourceView(ComponentsData);

		uint8* Buffer = (uint8*)RHILockStructuredBuffer(ComponentsData, 0, ComponentsDataMemSize, RLM_WriteOnly);
		FMemory::Memcpy(Buffer, OriginalComponentsData.GetData(), ComponentsDataMemSize);
		RHIUnlockStructuredBuffer(ComponentsData);

		FRHIResourceCreateInfo WeightBlendCreateInfo;
		uint32 WeightBlendMemSize = OriginalWeightmapWeightBlendModeData.Num() * sizeof(float);
		WeightmapWeightBlendMode = RHICreateVertexBuffer(WeightBlendMemSize, BUF_ShaderResource | BUF_Volatile | BUF_Dynamic, WeightBlendCreateInfo);
		WeightmapWeightBlendModeSRV = RHICreateShaderResourceView(WeightmapWeightBlendMode, sizeof(float), PF_R32_FLOAT);

		void* WeightmapWeightBlendModePtr = RHILockVertexBuffer(WeightmapWeightBlendMode, 0, WeightBlendMemSize, RLM_WriteOnly);
		FMemory::Memcpy(WeightmapWeightBlendModePtr, OriginalWeightmapWeightBlendModeData.GetData(), WeightBlendMemSize);
		RHIUnlockVertexBuffer(WeightmapWeightBlendMode);

		FRHIResourceCreateInfo TextureOutputOffsetCreateInfo;
		uint32 TextureOutputOffsetMemSize = OriginalTextureOutputOffset.Num() * sizeof(FVector2D);
		WeightmapTextureOutputOffset = RHICreateVertexBuffer(TextureOutputOffsetMemSize, BUF_ShaderResource | BUF_Volatile | BUF_Dynamic, TextureOutputOffsetCreateInfo);
		WeightmapTextureOutputOffsetSRV = RHICreateShaderResourceView(WeightmapTextureOutputOffset, sizeof(FVector2D), PF_G32R32F);

		void* TextureOutputOffsetPtr = RHILockVertexBuffer(WeightmapTextureOutputOffset, 0, TextureOutputOffsetMemSize, RLM_WriteOnly);
		FMemory::Memcpy(TextureOutputOffsetPtr, OriginalTextureOutputOffset.GetData(), TextureOutputOffsetMemSize);
		RHIUnlockVertexBuffer(WeightmapTextureOutputOffset);
	}

	virtual void ReleaseDynamicRHI() override
	{
		ComponentsData.SafeRelease();
		ComponentsDataSRV.SafeRelease();
		WeightmapWeightBlendModeSRV.SafeRelease();
		WeightmapTextureOutputOffsetSRV.SafeRelease();
	}

	int32 GetComponentsDataCount() const
	{
		return ComponentsDataCount;
	}

private:
	friend class FLandscapeLayerWeightmapPackMaterialLayersCS;

	FStructuredBufferRHIRef ComponentsData;
	FShaderResourceViewRHIRef ComponentsDataSRV;
	TArray<FLandscapeLayerWeightmapPackMaterialLayersComponentData> OriginalComponentsData;
	int32 ComponentsDataCount;

	TArray<float> OriginalWeightmapWeightBlendModeData;
	FVertexBufferRHIRef WeightmapWeightBlendMode;
	FShaderResourceViewRHIRef WeightmapWeightBlendModeSRV;

	TArray<FVector2D> OriginalTextureOutputOffset;
	FVertexBufferRHIRef WeightmapTextureOutputOffset;
	FShaderResourceViewRHIRef WeightmapTextureOutputOffsetSRV;
};

struct FLandscapeLayerWeightmapPackMaterialLayersComputeShaderParameters
{
	FLandscapeLayerWeightmapPackMaterialLayersComputeShaderParameters()
		: ComponentWeightmapResource(nullptr)
		, ComputeShaderResource(nullptr)
		, AtlasWeightmapsPerLayer(nullptr)
		, ComponentSize(0)
	{}

	FLandscapeTexture2DResource* ComponentWeightmapResource;
	FLandscapeLayerWeightmapPackMaterialLayersComputeShaderResource* ComputeShaderResource;
	FLandscapeTexture2DArrayResource* AtlasWeightmapsPerLayer;
	uint32 ComponentSize;
};

class FLandscapeLayerWeightmapPackMaterialLayersCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeLayerWeightmapPackMaterialLayersCS);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && !IsConsolePlatform(Parameters.Platform) && !IsMetalMobilePlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEX"), GLandscapeLayerWeightmapThreadGroupSizeX);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZEY"), GLandscapeLayerWeightmapThreadGroupSizeY);
	}

	FLandscapeLayerWeightmapPackMaterialLayersCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ComponentWeightmapParam.Bind(Initializer.ParameterMap, TEXT("OutComponentWeightMaps"));
		AtlasPaintListsParam.Bind(Initializer.ParameterMap, TEXT("InAtlasPaintLayers"));
		ComponentsDataParam.Bind(Initializer.ParameterMap, TEXT("InPackLayersComponentsData"));
		ComponentSizeParam.Bind(Initializer.ParameterMap, TEXT("ComponentSize"));
		WeightmapWeightBlendModeParam.Bind(Initializer.ParameterMap, TEXT("InWeightmapWeightBlendMode"));
		WeightmapTextureOutputOffsetParam.Bind(Initializer.ParameterMap, TEXT("InWeightmapTextureOutputOffset"));
	}

	FLandscapeLayerWeightmapPackMaterialLayersCS()
	{}

	void SetParameters(FRHICommandList& RHICmdList, const FLandscapeLayerWeightmapPackMaterialLayersComputeShaderParameters& InParams)
	{
		SetUAVParameter(RHICmdList, GetComputeShader(), ComponentWeightmapParam, InParams.ComponentWeightmapResource->TextureUAV);
		SetTextureParameter(RHICmdList, GetComputeShader(), AtlasPaintListsParam, InParams.AtlasWeightmapsPerLayer->TextureRHI);
		SetSRVParameter(RHICmdList, GetComputeShader(), ComponentsDataParam, InParams.ComputeShaderResource->ComponentsDataSRV);
		SetShaderValue(RHICmdList, GetComputeShader(), ComponentSizeParam, InParams.ComponentSize);
		SetSRVParameter(RHICmdList, GetComputeShader(), WeightmapWeightBlendModeParam, InParams.ComputeShaderResource->WeightmapWeightBlendModeSRV);
		SetSRVParameter(RHICmdList, GetComputeShader(), WeightmapTextureOutputOffsetParam, InParams.ComputeShaderResource->WeightmapTextureOutputOffsetSRV);
	}

	void UnsetParameters(FRHICommandList& RHICmdList)
	{
		SetUAVParameter(RHICmdList, GetComputeShader(), ComponentWeightmapParam, nullptr);
	}

	virtual bool Serialize(FArchive& Ar) override
	{
		bool bShaderHasOutdatedParameters = FShader::Serialize(Ar);
		Ar << ComponentWeightmapParam;
		Ar << AtlasPaintListsParam;
		Ar << ComponentsDataParam;
		Ar << ComponentSizeParam;
		Ar << WeightmapWeightBlendModeParam;
		Ar << WeightmapTextureOutputOffsetParam;
		return bShaderHasOutdatedParameters;
	}

private:
	FShaderResourceParameter ComponentWeightmapParam;
	FShaderResourceParameter AtlasPaintListsParam;
	FShaderResourceParameter ComponentsDataParam;
	FShaderParameter ComponentSizeParam;
	FShaderResourceParameter WeightmapWeightBlendModeParam;
	FShaderResourceParameter WeightmapTextureOutputOffsetParam;
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeLayerWeightmapPackMaterialLayersCS, "/Engine/Private/LandscapeLayersCS.usf", "PackPaintLayerToWeightmap", SF_Compute);

class FLandscapeLayerWeightmapPackMaterialLayersCSDispatch_RenderThread
{
public:
	FLandscapeLayerWeightmapPackMaterialLayersCSDispatch_RenderThread(const FLandscapeLayerWeightmapPackMaterialLayersComputeShaderParameters& InShaderParams)
		: ShaderParams(InShaderParams)
	{}

	void PackLayers(FRHICommandListImmediate& InRHICmdList)
	{
		SCOPE_CYCLE_COUNTER(STAT_LandscapeLayersRegenerate_RenderThread);
		SCOPED_DRAW_EVENTF(InRHICmdList, LandscapeLayersRender, TEXT("PackLayers"));
		SCOPED_GPU_STAT(InRHICmdList, LandscapeLayersRender);

		TShaderMapRef<FLandscapeLayerWeightmapPackMaterialLayersCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		InRHICmdList.SetComputeShader(ComputeShader->GetComputeShader());
		ComputeShader->SetParameters(InRHICmdList, ShaderParams);

		uint32 ThreadGroupCountX = FMath::CeilToInt((float)ShaderParams.ComponentSize / (float)GLandscapeLayerWeightmapThreadGroupSizeX);
		uint32 ThreadGroupCountY = FMath::CeilToInt((float)ShaderParams.ComponentSize / (float)GLandscapeLayerWeightmapThreadGroupSizeY);
		check(ThreadGroupCountX > 0 && ThreadGroupCountY > 0);

		DispatchComputeShader(InRHICmdList, *ComputeShader, ThreadGroupCountX, ThreadGroupCountY, ShaderParams.ComputeShaderResource->GetComponentsDataCount());
		ComputeShader->UnsetParameters(InRHICmdList);
		ShaderParams.ComputeShaderResource->ReleaseResource();
		delete ShaderParams.ComputeShaderResource;
	}

private:
	FLandscapeLayerWeightmapPackMaterialLayersComputeShaderParameters ShaderParams;
};

// Copy texture render command

class FLandscapeLayersCopyTexture_RenderThread
{
public:
	FLandscapeLayersCopyTexture_RenderThread(const FLandscapeLayersCopyTextureParams& InParams)
		: Params(InParams)
	{}	

	void Copy(FRHICommandListImmediate& InRHICmdList)
	{
		SCOPE_CYCLE_COUNTER(STAT_LandscapeLayersRegenerate_RenderThread);
		SCOPED_DRAW_EVENTF(InRHICmdList, LandscapeLayersCopy, TEXT("LS Copy %s -> %s, Mip (%d -> %d), Array Index (%d -> %d)"), *Params.SourceResourceDebugName, *Params.DestResourceDebugName, Params.SourceMip, Params.DestMip, Params.SourceArrayIndex, Params.DestArrayIndex);
		SCOPED_GPU_STAT(InRHICmdList, LandscapeLayersRender);

		FIntPoint SourceSize(Params.SourceResource->GetSizeX(), Params.SourceResource->GetSizeY()); // SourceResource is always proper size, as it's always the good MIP we want to copy from
		FIntPoint DestSize(Params.DestResource->GetSizeX() >> Params.DestMip, Params.DestResource->GetSizeY() >> Params.DestMip);

		FRHICopyTextureInfo Info;
		Info.NumSlices = 1;
		Info.Size.Z = 1;
		Info.SourceSliceIndex = Params.SourceArrayIndex;
		Info.DestSliceIndex = Params.DestArrayIndex;
		Info.SourceMipIndex = 0; // In my case, always assume we copy from mip 0 to something else as in my case each mips will be stored into individual texture/RT
		Info.DestMipIndex = Params.DestMip;

		if (SourceSize.X <= DestSize.X)
		{
			Info.SourcePosition.X = 0;
			Info.Size.X = SourceSize.X;
			Info.DestPosition.X = Params.InitialPositionOffset.X >> Params.DestMip;
			check(Info.DestPosition.X + Info.Size.X <= DestSize.X);
		}
		else
		{
			Info.SourcePosition.X = Params.InitialPositionOffset.X >> Params.SourceMip;
			Info.Size.X = DestSize.X;
			Info.DestPosition.X = 0;
            check(Info.SourcePosition.X >= 0);
			check(Info.SourcePosition.X + Info.Size.X <= SourceSize.X);
			check(Info.DestPosition.X + Info.Size.X <= DestSize.X);
		}

		if (SourceSize.Y <= DestSize.Y)
		{
			Info.SourcePosition.Y = 0;
			Info.Size.Y = SourceSize.Y;
			Info.DestPosition.Y = Params.InitialPositionOffset.Y >> Params.DestMip;
			check(Info.DestPosition.Y + Info.Size.Y <= DestSize.Y);
		}
		else
		{
			Info.SourcePosition.Y = Params.InitialPositionOffset.Y >> Params.SourceMip;
			Info.Size.Y = DestSize.Y;
			Info.DestPosition.Y = 0;
check(Info.SourcePosition.Y >= 0);
			check(Info.SourcePosition.Y + Info.Size.Y <= SourceSize.Y);
			check(Info.DestPosition.Y + Info.Size.Y <= DestSize.Y);
		}

		InRHICmdList.CopyTexture(Params.SourceResource->TextureRHI, Params.DestResource->TextureRHI, Info);

		if (Params.DestCPUResource != nullptr)
		{
			InRHICmdList.CopyTexture(Params.SourceResource->TextureRHI, Params.DestCPUResource->TextureRHI, Info);
		}
	}

private:
	FLandscapeLayersCopyTextureParams Params;
};

// Clear command

class LandscapeLayersWeightmapClear_RenderThread
{
public:
	LandscapeLayersWeightmapClear_RenderThread(const FString& InDebugName, FTextureRenderTargetResource* InTextureResourceToClear)
		: DebugName(InDebugName)
		, RenderTargetResource(InTextureResourceToClear)
	{}

	virtual ~LandscapeLayersWeightmapClear_RenderThread()
	{}

	void Clear(FRHICommandListImmediate& InRHICmdList)
	{
		SCOPE_CYCLE_COUNTER(STAT_LandscapeLayersRegenerate_RenderThread);
		SCOPED_DRAW_EVENTF(InRHICmdList, LandscapeLayersRender, TEXT("%s"), DebugName.Len() > 0 ? *DebugName : TEXT("LandscapeLayersClear"));
		SCOPED_GPU_STAT(InRHICmdList, LandscapeLayersRender);

		check(IsInRenderingThread());

		FRHIRenderPassInfo RPInfo(RenderTargetResource->TextureRHI, ERenderTargetActions::Clear_Store);
		InRHICmdList.BeginRenderPass(RPInfo, TEXT("Clear"));
		InRHICmdList.EndRenderPass();
	}

	FString DebugName;
	FTextureRenderTargetResource* RenderTargetResource;
};

// Render command

template<typename ShaderDataType, typename ShaderPixelClass, typename ShaderPixelMipsClass>
class FLandscapeLayersRender_RenderThread
{
public:

	FLandscapeLayersRender_RenderThread(const FString& InDebugName, UTextureRenderTarget2D* InWriteRenderTarget, const FIntPoint& InWriteRenderTargetSize, const FIntPoint& InReadRenderTargetSize, const FMatrix& InProjectionMatrix,
										const ShaderDataType& InShaderParams, uint8 InCurrentMip, const TArray<FLandscapeLayersTriangle>& InTriangleList)
		: RenderTargetResource(InWriteRenderTarget->GameThread_GetRenderTargetResource())
		, WriteRenderTargetSize(InWriteRenderTargetSize)
		, ReadRenderTargetSize(InReadRenderTargetSize)
		, ProjectionMatrix(InProjectionMatrix)
		, ShaderParams(InShaderParams)
		, PrimitiveCount(InTriangleList.Num())
		, DebugName(InDebugName)
		, CurrentMip(InCurrentMip)
	{
		VertexBufferResource.Init(InTriangleList);
	}

	virtual ~FLandscapeLayersRender_RenderThread()
	{}

	void Render(FRHICommandListImmediate& InRHICmdList, bool InClearRT)
	{
		SCOPE_CYCLE_COUNTER(STAT_LandscapeLayersRegenerate_RenderThread);
		SCOPED_DRAW_EVENTF(InRHICmdList, LandscapeLayersRender, TEXT("%s"), DebugName.Len() > 0 ? *DebugName : TEXT("LandscapeLayersRender"));
		SCOPED_GPU_STAT(InRHICmdList, LandscapeLayersRender);
		INC_DWORD_STAT(STAT_LandscapeLayersRegenerateDrawCalls);

		check(IsInRenderingThread());

		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(RenderTargetResource, NULL, FEngineShowFlags(ESFIM_Game)).SetWorldTimes(FApp::GetCurrentTime() - GStartTime, FApp::GetDeltaTime(), FApp::GetCurrentTime() - GStartTime));

		FSceneViewInitOptions ViewInitOptions;
		ViewInitOptions.SetViewRectangle(FIntRect(0, 0, WriteRenderTargetSize.X, WriteRenderTargetSize.Y));
		ViewInitOptions.ViewOrigin = FVector::ZeroVector;
		ViewInitOptions.ViewRotationMatrix = FMatrix::Identity;
		ViewInitOptions.ProjectionMatrix = ProjectionMatrix;
		ViewInitOptions.ViewFamily = &ViewFamily;
		ViewInitOptions.BackgroundColor = FLinearColor::Black;
		ViewInitOptions.OverlayColor = FLinearColor::White;

		// Create and add the new view
		FSceneView* View = new FSceneView(ViewInitOptions);
		ViewFamily.Views.Add(View);

		// Init VB/IB Resource
		VertexDeclaration.InitResource();
		VertexBufferResource.InitResource();

		// Setup Pipeline
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = VertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_Zero, BO_Add, BF_One, BF_Zero>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		FRHIRenderPassInfo RenderPassInfo(ViewFamily.RenderTarget->GetRenderTargetTexture(), CurrentMip == 0 || InClearRT ? ERenderTargetActions::Clear_Store : ERenderTargetActions::Load_Store, nullptr, 0, 0);
		InRHICmdList.BeginRenderPass(RenderPassInfo, TEXT("DrawLayers"));

		if (CurrentMip == 0)
		{
			// Setup Shaders
			TShaderMapRef<FLandscapeLayersVS> VertexShader(GetGlobalShaderMap(View->GetFeatureLevel()));
			TShaderMapRef<ShaderPixelClass> PixelShader(GetGlobalShaderMap(View->GetFeatureLevel()));

			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);

			InRHICmdList.SetViewport(View->UnscaledViewRect.Min.X, View->UnscaledViewRect.Min.Y, 0.0f, View->UnscaledViewRect.Max.X, View->UnscaledViewRect.Max.Y, 1.0f);

			InRHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			SetGraphicsPipelineState(InRHICmdList, GraphicsPSOInit);

			// Set shader params
			VertexShader->SetParameters(InRHICmdList, ProjectionMatrix);
			PixelShader->SetParameters(InRHICmdList, ShaderParams);
		}
		else
		{
			// Setup Shaders
			TShaderMapRef<FLandscapeLayersVS> VertexShader(GetGlobalShaderMap(View->GetFeatureLevel()));
			TShaderMapRef<ShaderPixelMipsClass> PixelShader(GetGlobalShaderMap(View->GetFeatureLevel()));

			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);

			InRHICmdList.SetViewport(0.0f, 0.0f, 0.0f, WriteRenderTargetSize.X, WriteRenderTargetSize.Y, 1.0f);

			InRHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			SetGraphicsPipelineState(InRHICmdList, GraphicsPSOInit);

			// Set shader params
			VertexShader->SetParameters(InRHICmdList, ProjectionMatrix);
			PixelShader->SetParameters(InRHICmdList, ShaderParams);
		}

		InRHICmdList.SetStencilRef(0);
		InRHICmdList.SetScissorRect(false, 0, 0, 0, 0);
		InRHICmdList.SetStreamSource(0, VertexBufferResource.VertexBufferRHI, 0);

		InRHICmdList.DrawPrimitive(0, PrimitiveCount, 1);

		InRHICmdList.EndRenderPass();

		VertexDeclaration.ReleaseResource();
		VertexBufferResource.ReleaseResource();
	}

private:
	FTextureRenderTargetResource* RenderTargetResource;
	FIntPoint WriteRenderTargetSize;
	FIntPoint ReadRenderTargetSize;
	FMatrix ProjectionMatrix;
	ShaderDataType ShaderParams;
	FLandscapeLayersVertexBuffer VertexBufferResource;
	int32 PrimitiveCount;
	FLandscapeLayersVertexDeclaration VertexDeclaration;
	FString DebugName;
	uint8 CurrentMip;
};

typedef FLandscapeLayersRender_RenderThread<FLandscapeLayersHeightmapShaderParameters, FLandscapeLayersHeightmapPS, FLandscapeLayersHeightmapMipsPS> FLandscapeLayersHeightmapRender_RenderThread;
typedef FLandscapeLayersRender_RenderThread<FLandscapeLayersWeightmapShaderParameters, FLandscapeLayersWeightmapPS, FLandscapeLayersWeightmapMipsPS> FLandscapeLayersWeightmapRender_RenderThread;
// The Mips Param will not be used
typedef FLandscapeLayersRender_RenderThread<FLandscapeLayersWeightmapConvertFormatShaderParameter, FLandscapeLayersWeightmapConvertFormatPS, FLandscapeLayersWeightmapConvertFormatPS> FLandscapeLayersWeightmapConvertRender_RenderThread;

#if WITH_EDITOR

struct FLandscapeIsTextureFullyStreamedIn
{
	bool operator()(UTexture2D* InTexture, bool bInWaitForStreaming)
	{
		check(InTexture);
		if (bInWaitForStreaming)
		{
			InTexture->WaitForStreaming();
		}
		return InTexture->IsFullyStreamedIn();
	}
};

void ALandscape::CreateLayersRenderingResource()
{
	ULandscapeInfo* Info = GetLandscapeInfo();
	if (Info == nullptr)
	{
		return;
	}

	// Create & Set the CPU Readback to each component global data
	Info->ForAllLandscapeProxies([&](ALandscapeProxy* Proxy)
	{
		TArray<UTexture2D*> ComponentsHeightmaps;

		for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
		{
			UTexture2D* ComponentHeightmap = Component->GetHeightmap();

			FLandscapeLayersTexture2DCPUReadBackResource** CPUReadback = Proxy->HeightmapsCPUReadBack.Find(ComponentHeightmap);

			if (CPUReadback != nullptr)
			{
				BeginReleaseResource(*CPUReadback);
				*CPUReadback = nullptr;
			}

			if (CPUReadback == nullptr || *CPUReadback == nullptr)
			{
				FLandscapeLayersTexture2DCPUReadBackResource* NewCPUReadback = new FLandscapeLayersTexture2DCPUReadBackResource(ComponentHeightmap->Source.GetSizeX(), ComponentHeightmap->Source.GetSizeY(), ComponentHeightmap->GetPixelFormat(), ComponentHeightmap->Source.GetNumMips());
				BeginInitResource(NewCPUReadback);

				Proxy->HeightmapsCPUReadBack.Add(ComponentHeightmap, NewCPUReadback);
			}
		}
	});

	// Flush because TickLayers can access CPU Readbacks in the same frame (see ResolveLayersTexture)
	FlushRenderingCommands();

	const FIntPoint ComponentCounts = ComputeComponentCounts();

	ALandscape* Landscape = GetLandscapeActor();
	check(Landscape);

	if (Landscape->HeightmapRTList.Num() == 0)
	{
		Landscape->HeightmapRTList.Init(nullptr, EHeightmapRTType::HeightmapRT_Count);

		int32 CurrentMipSizeX = ((SubsectionSizeQuads + 1) * NumSubsections) * ComponentCounts.X;
		int32 CurrentMipSizeY = ((SubsectionSizeQuads + 1) * NumSubsections) * ComponentCounts.Y;

		for (int32 i = 0; i < EHeightmapRTType::HeightmapRT_Count; ++i)
		{
			Landscape->HeightmapRTList[i] = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
			check(Landscape->HeightmapRTList[i]);
			Landscape->HeightmapRTList[i]->RenderTargetFormat = RTF_RGBA8;
			Landscape->HeightmapRTList[i]->AddressX = TextureAddress::TA_Clamp;
			Landscape->HeightmapRTList[i]->AddressY = TextureAddress::TA_Clamp;
			Landscape->HeightmapRTList[i]->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

			if (i < EHeightmapRTType::HeightmapRT_Mip1) // Landscape size RT
			{
				Landscape->HeightmapRTList[i]->InitAutoFormat(FMath::RoundUpToPowerOfTwo(CurrentMipSizeX), FMath::RoundUpToPowerOfTwo(CurrentMipSizeY));
			}
			else // Mips
			{
				CurrentMipSizeX >>= 1;
				CurrentMipSizeY >>= 1;
				Landscape->HeightmapRTList[i]->InitAutoFormat(FMath::RoundUpToPowerOfTwo(CurrentMipSizeX), FMath::RoundUpToPowerOfTwo(CurrentMipSizeY));
			}

			Landscape->HeightmapRTList[i]->UpdateResourceImmediate(true);

			// Only generate required mips RT
			if (CurrentMipSizeX == ComponentCounts.X && CurrentMipSizeY == ComponentCounts.Y)
			{
				break;
			}
		}
	}
	else // Simply resize the render target
	{
		int32 CurrentMipSizeX = ((SubsectionSizeQuads + 1) * NumSubsections) * ComponentCounts.X;
		int32 CurrentMipSizeY = ((SubsectionSizeQuads + 1) * NumSubsections) * ComponentCounts.Y;

		for (int32 i = 0; i < EHeightmapRTType::HeightmapRT_Count; ++i)
		{
			if (i < EHeightmapRTType::HeightmapRT_Mip1) // Landscape size RT
			{
				Landscape->HeightmapRTList[i]->ResizeTarget(FMath::RoundUpToPowerOfTwo(CurrentMipSizeX), FMath::RoundUpToPowerOfTwo(CurrentMipSizeY));
			}
			else // Mips
			{
				CurrentMipSizeX >>= 1;
				CurrentMipSizeY >>= 1;
				Landscape->HeightmapRTList[i]->ResizeTarget(FMath::RoundUpToPowerOfTwo(CurrentMipSizeX), FMath::RoundUpToPowerOfTwo(CurrentMipSizeY));
			}

			// Only generate required mips RT
			if (CurrentMipSizeX == ComponentCounts.X && CurrentMipSizeY == ComponentCounts.Y)
			{
				break;
			}
		}
	}

	if (Landscape->WeightmapRTList.Num() == 0)
	{
		Landscape->WeightmapRTList.Init(nullptr, EWeightmapRTType::WeightmapRT_Count);

		int32 CurrentMipSizeX = ((SubsectionSizeQuads + 1) * NumSubsections) * ComponentCounts.X;
		int32 CurrentMipSizeY = ((SubsectionSizeQuads + 1) * NumSubsections) * ComponentCounts.Y;

		for (int32 i = 0; i < EWeightmapRTType::WeightmapRT_Count; ++i)
		{
			Landscape->WeightmapRTList[i] = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);

			check(Landscape->WeightmapRTList[i]);
			Landscape->WeightmapRTList[i]->AddressX = TextureAddress::TA_Clamp;
			Landscape->WeightmapRTList[i]->AddressY = TextureAddress::TA_Clamp;
			Landscape->WeightmapRTList[i]->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
			Landscape->WeightmapRTList[i]->RenderTargetFormat = RTF_RGBA8;

			if (i < EWeightmapRTType::WeightmapRT_Mip0) // Landscape size RT, only create the number of layer we have
			{
				Landscape->WeightmapRTList[i]->RenderTargetFormat = i == WeightmapRT_Scratch_RGBA ? RTF_RGBA8 : RTF_R8;
				Landscape->WeightmapRTList[i]->InitAutoFormat(FMath::RoundUpToPowerOfTwo(CurrentMipSizeX), FMath::RoundUpToPowerOfTwo(CurrentMipSizeY));
			}
			else // Mips
			{
				Landscape->WeightmapRTList[i]->InitAutoFormat(FMath::RoundUpToPowerOfTwo(CurrentMipSizeX), FMath::RoundUpToPowerOfTwo(CurrentMipSizeY));

				CurrentMipSizeX >>= 1;
				CurrentMipSizeY >>= 1;
			}

			Landscape->WeightmapRTList[i]->UpdateResourceImmediate(true);

			// Only generate required mips RT
			if (CurrentMipSizeX < ComponentCounts.X && CurrentMipSizeY < ComponentCounts.Y)
			{
				break;
			}
		}
	}
	else // Simply resize the render target
	{
		int32 CurrentMipSizeX = ((SubsectionSizeQuads + 1) * NumSubsections) * ComponentCounts.X;
		int32 CurrentMipSizeY = ((SubsectionSizeQuads + 1) * NumSubsections) * ComponentCounts.Y;

		for (int32 i = 0; i < EWeightmapRTType::WeightmapRT_Count; ++i)
		{
			if (i < EWeightmapRTType::WeightmapRT_Mip0) // Landscape size RT, only create the number of layer we have
			{
				Landscape->WeightmapRTList[i]->ResizeTarget(FMath::RoundUpToPowerOfTwo(CurrentMipSizeX), FMath::RoundUpToPowerOfTwo(CurrentMipSizeY));
			}
			else // Mips
			{
				Landscape->WeightmapRTList[i]->ResizeTarget(FMath::RoundUpToPowerOfTwo(CurrentMipSizeX), FMath::RoundUpToPowerOfTwo(CurrentMipSizeY));

				CurrentMipSizeX >>= 1;
				CurrentMipSizeY >>= 1;
			}

			// Only generate required mips RT
			if (CurrentMipSizeX < ComponentCounts.X && CurrentMipSizeY < ComponentCounts.Y)
			{
				break;
			}
		}
	}

	InitializeLayersWeightmapResources();
}

void ALandscape::ToggleCanHaveLayersContent()
{
	bCanHaveLayersContent = !bCanHaveLayersContent;

	if (!bCanHaveLayersContent)
	{
		ReleaseLayersRenderingResource();
		DeleteLayers();
	}
	else
	{
		check(GetLayerCount() == 0);
		CreateDefaultLayer();
		CopyOldDataToDefaultLayer();
	}

	if (LandscapeEdMode)
	{
		LandscapeEdMode->OnCanHaveLayersContentChanged();
	}
}

void ALandscape::ReleaseLayersRenderingResource()
{
	check(!CanHaveLayersContent());
	ULandscapeInfo* Info = GetLandscapeInfo();
	if (Info == nullptr)
	{
		return;
	}

	Info->ForAllLandscapeProxies([&](ALandscapeProxy* Proxy)
	{
		for (auto& ItPair : Proxy->HeightmapsCPUReadBack)
		{
			FLandscapeLayersTexture2DCPUReadBackResource* HeightmapCPUReadBack = ItPair.Value;

			if (HeightmapCPUReadBack != nullptr)
			{
				BeginReleaseResource(HeightmapCPUReadBack);
			}
		}

		for (auto& ItPair : Proxy->WeightmapsCPUReadBack)
		{
			FLandscapeLayersTexture2DCPUReadBackResource* WeightmapCPUReadBack = ItPair.Value;

			if (WeightmapCPUReadBack != nullptr)
			{
				BeginReleaseResource(WeightmapCPUReadBack);
			}
		}
	});

	if (CombinedLayersWeightmapAllMaterialLayersResource != nullptr)
	{
		BeginReleaseResource(CombinedLayersWeightmapAllMaterialLayersResource);
	}

	if (CurrentLayersWeightmapAllMaterialLayersResource != nullptr)
	{
		BeginReleaseResource(CurrentLayersWeightmapAllMaterialLayersResource);
	}

	if (WeightmapScratchExtractLayerTextureResource != nullptr)
	{
		BeginReleaseResource(WeightmapScratchExtractLayerTextureResource);
	}

	if (WeightmapScratchPackLayerTextureResource != nullptr)
	{
		BeginReleaseResource(WeightmapScratchPackLayerTextureResource);
	}

	FlushRenderingCommands();

	Info->ForAllLandscapeProxies([&](ALandscapeProxy* Proxy)
	{
		for (auto& ItPair : Proxy->HeightmapsCPUReadBack)
		{
			FLandscapeLayersTexture2DCPUReadBackResource* HeightmapCPUReadBack = ItPair.Value;

			delete HeightmapCPUReadBack;
			HeightmapCPUReadBack = nullptr;
		}
		Proxy->HeightmapsCPUReadBack.Empty();

		for (auto& ItPair : Proxy->WeightmapsCPUReadBack)
		{
			FLandscapeLayersTexture2DCPUReadBackResource* WeightmapCPUReadBack = ItPair.Value;

			delete WeightmapCPUReadBack;
			WeightmapCPUReadBack = nullptr;
		}
		Proxy->WeightmapsCPUReadBack.Empty();
	});

	delete CombinedLayersWeightmapAllMaterialLayersResource;
	delete CurrentLayersWeightmapAllMaterialLayersResource;
	delete WeightmapScratchExtractLayerTextureResource;
	delete WeightmapScratchPackLayerTextureResource;

	CombinedLayersWeightmapAllMaterialLayersResource = nullptr;
	CurrentLayersWeightmapAllMaterialLayersResource = nullptr;
	WeightmapScratchExtractLayerTextureResource = nullptr;
	WeightmapScratchPackLayerTextureResource = nullptr;
}

FIntPoint ALandscape::ComputeComponentCounts() const
{
	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return FIntPoint(INDEX_NONE, INDEX_NONE);
	}

	FIntPoint NumComponents(0, 0);
	FIntPoint MaxSectionBase(0, 0);
	FIntPoint MinSectionBase(0, 0);

	Info->ForAllLandscapeProxies([&MaxSectionBase, &MinSectionBase](ALandscapeProxy* Proxy)
	{
		for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
		{
			MaxSectionBase.X = FMath::Max(MaxSectionBase.X, Component->SectionBaseX);
			MaxSectionBase.Y = FMath::Max(MaxSectionBase.Y, Component->SectionBaseY);

			MinSectionBase.X = FMath::Min(MinSectionBase.X, Component->SectionBaseX);
			MinSectionBase.Y = FMath::Min(MinSectionBase.Y, Component->SectionBaseY);
		}
	});

	NumComponents.X = ((MaxSectionBase.X - MinSectionBase.X) / ComponentSizeQuads) + 1;
	NumComponents.Y = ((MaxSectionBase.Y - MinSectionBase.Y) / ComponentSizeQuads) + 1;

	return NumComponents;
}

void ALandscape::CopyOldDataToDefaultLayer()
{
	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return;
	}

	Info->ForAllLandscapeProxies([&](ALandscapeProxy* Proxy)
	{
		CopyOldDataToDefaultLayer(Proxy);
	});
}

void ALandscape::CopyOldDataToDefaultLayer(ALandscapeProxy* InProxy)
{
	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return;
	}

	InProxy->Modify();

	FLandscapeLayer* DefaultLayer = GetLayer(0);
	check(DefaultLayer != nullptr);

	struct FWeightmapTextureData
	{
		UTexture2D* Texture;
		ULandscapeWeightmapUsage* Usage;
	};

	TMap<UTexture2D*, FWeightmapTextureData> ProcessedWeightmaps;
	TArray<UTexture2D*> ProcessedHeightmaps;
	TArray<ULandscapeComponent*> WeightmapsComponentsToCleanup;

	for (ULandscapeComponent* Component : InProxy->LandscapeComponents)
	{
		FLandscapeLayerComponentData* LayerData = Component->GetLayerData(DefaultLayer->Guid);

		if (ensure(LayerData != nullptr && LayerData->IsInitialized()))
		{
			// Heightmap
			UTexture2D* ComponentHeightmap = Component->GetHeightmap();

			if (!ProcessedHeightmaps.Contains(ComponentHeightmap))
			{
				ProcessedHeightmaps.Add(ComponentHeightmap);

				UTexture* DefaultLayerHeightmap = LayerData->HeightmapData.Texture;
				check(DefaultLayerHeightmap != nullptr);

				// Only copy Mip0 as other mips will get regenerated
				TArray64<uint8> ExistingMip0Data;
				ComponentHeightmap->Source.GetMipData(ExistingMip0Data, 0);

				FColor* Mip0Data = (FColor*)DefaultLayerHeightmap->Source.LockMip(0);
				FMemory::Memcpy(Mip0Data, ExistingMip0Data.GetData(), ExistingMip0Data.Num());
				DefaultLayerHeightmap->Source.UnlockMip(0);

				DefaultLayerHeightmap->BeginCachePlatformData();
				DefaultLayerHeightmap->ClearAllCachedCookedPlatformData();
			}

			// Weightmaps
			WeightmapsComponentsToCleanup.Add(Component);

			const TArray<UTexture2D*>& ComponentWeightmapTextures = Component->GetWeightmapTextures();
			const TArray<FWeightmapLayerAllocationInfo>& ComponentLayerAllocations = Component->GetWeightmapLayerAllocations();
			TArray<ULandscapeWeightmapUsage*>& ComponentWeightmapTexturesUsage = Component->GetWeightmapTexturesUsage();

			LayerData->WeightmapData.Textures.AddDefaulted(ComponentWeightmapTextures.Num());
			LayerData->WeightmapData.TextureUsages.AddDefaulted(ComponentWeightmapTexturesUsage.Num());

			for (int32 TextureIndex = 0; TextureIndex < ComponentWeightmapTextures.Num(); ++TextureIndex)
			{
				UTexture2D* ComponentWeightmap = ComponentWeightmapTextures[TextureIndex];
				const FWeightmapTextureData* WeightmapTextureData = ProcessedWeightmaps.Find(ComponentWeightmap);

				if (WeightmapTextureData != nullptr)
				{
					LayerData->WeightmapData.Textures[TextureIndex] = WeightmapTextureData->Texture;
					LayerData->WeightmapData.TextureUsages[TextureIndex] = WeightmapTextureData->Usage;
					check(WeightmapTextureData->Usage->LayerGuid == DefaultLayer->Guid);

					for (int32 ChannelIndex = 0; ChannelIndex < ULandscapeWeightmapUsage::NumChannels; ++ChannelIndex)
					{
						const ULandscapeComponent* ChannelLandscapeComponent = LayerData->WeightmapData.TextureUsages[TextureIndex]->ChannelUsage[ChannelIndex];

						if (ChannelLandscapeComponent != nullptr && ChannelLandscapeComponent == Component)
						{
							for (const FWeightmapLayerAllocationInfo& Allocation : ComponentLayerAllocations)
							{
								if (Allocation.WeightmapTextureIndex == TextureIndex)
								{
									LayerData->WeightmapData.LayerAllocations.Add(Allocation);
								}
							}

							break;
						}
					}
				}
				else
				{
					UTexture2D* NewLayerWeightmapTexture = InProxy->CreateLandscapeTexture(ComponentWeightmap->Source.GetSizeX(), ComponentWeightmap->Source.GetSizeY(), TEXTUREGROUP_Terrain_Weightmap, ComponentWeightmap->Source.GetFormat());

					// Only copy Mip0 as other mips will get regenerated
					TArray64<uint8> ExistingMip0Data;
					ComponentWeightmap->Source.GetMipData(ExistingMip0Data, 0);

					FColor* Mip0Data = (FColor*)NewLayerWeightmapTexture->Source.LockMip(0);
					FMemory::Memcpy(Mip0Data, ExistingMip0Data.GetData(), ExistingMip0Data.Num());
					NewLayerWeightmapTexture->Source.UnlockMip(0);

					LayerData->WeightmapData.Textures[TextureIndex] = NewLayerWeightmapTexture;
					LayerData->WeightmapData.TextureUsages[TextureIndex] = InProxy->WeightmapUsageMap.Add(NewLayerWeightmapTexture, InProxy->CreateWeightmapUsage());

					for (int32 ChannelIndex = 0; ChannelIndex < ULandscapeWeightmapUsage::NumChannels; ++ChannelIndex)
					{
						LayerData->WeightmapData.TextureUsages[TextureIndex]->ChannelUsage[ChannelIndex] = ComponentWeightmapTexturesUsage[TextureIndex]->ChannelUsage[ChannelIndex];
					}

					LayerData->WeightmapData.TextureUsages[TextureIndex]->LayerGuid = DefaultLayer->Guid;

					// Create new Usage for the "final" layer as the other one will now be used by the Default layer
					for (const FWeightmapLayerAllocationInfo& Allocation : ComponentLayerAllocations)
					{
						if (Allocation.WeightmapTextureIndex == TextureIndex)
						{
							LayerData->WeightmapData.LayerAllocations.Add(Allocation);
						}
					}

					FWeightmapTextureData NewTextureData;
					NewTextureData.Texture = NewLayerWeightmapTexture;
					NewTextureData.Usage = LayerData->WeightmapData.TextureUsages[TextureIndex];

					ProcessedWeightmaps.Add(ComponentWeightmap, NewTextureData);

					NewLayerWeightmapTexture->BeginCachePlatformData();
					NewLayerWeightmapTexture->ClearAllCachedCookedPlatformData();
				}
			}
		}
	}

	for (ULandscapeComponent* Component : WeightmapsComponentsToCleanup)
	{
		TArray<FWeightmapLayerAllocationInfo>& ComponentLayerAllocations = Component->GetWeightmapLayerAllocations();

		for (FWeightmapLayerAllocationInfo& Allocation : ComponentLayerAllocations)
		{
			Allocation.WeightmapTextureChannel = 255;
			Allocation.WeightmapTextureIndex = 255;
		}
	}
}

void ALandscape::InitializeLandscapeLayersWeightmapUsage()
{
	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return;
	}

	Info->ForAllLandscapeProxies([&](ALandscapeProxy* Proxy)
	{
		Proxy->InitializeProxyLayersWeightmapUsage();
	});
}

void ALandscapeProxy::InitializeProxyLayersWeightmapUsage()
{
	if (ALandscape* Landscape = GetLandscapeActor())
	{
		for (const FLandscapeLayer& Layer : Landscape->LandscapeLayers)
		{
			for (ULandscapeComponent* Component : LandscapeComponents)
			{
				// Compute per layer data
				FLandscapeLayerComponentData* LayerData = Component->GetLayerData(Layer.Guid);

				if (LayerData != nullptr && LayerData->IsInitialized())
				{
					LayerData->WeightmapData.TextureUsages.Reset();
					LayerData->WeightmapData.TextureUsages.AddDefaulted(LayerData->WeightmapData.Textures.Num());

					// regenerate the weightmap usage
					for (int32 LayerIdx = 0; LayerIdx < LayerData->WeightmapData.LayerAllocations.Num(); LayerIdx++)
					{
						FWeightmapLayerAllocationInfo& Allocation = LayerData->WeightmapData.LayerAllocations[LayerIdx];
						UTexture2D* WeightmapTexture = LayerData->WeightmapData.Textures[Allocation.WeightmapTextureIndex];
						ULandscapeWeightmapUsage** TempUsage = WeightmapUsageMap.Find(WeightmapTexture);

						if (TempUsage == nullptr)
						{
							TempUsage = &WeightmapUsageMap.Add(WeightmapTexture, CreateWeightmapUsage());
							(*TempUsage)->LayerGuid = Layer.Guid;
						}

						ULandscapeWeightmapUsage* Usage = *TempUsage;
						LayerData->WeightmapData.TextureUsages[Allocation.WeightmapTextureIndex] = Usage; // Keep a ref to it for faster access

						check(Usage->ChannelUsage[Allocation.WeightmapTextureChannel] == nullptr || Usage->ChannelUsage[Allocation.WeightmapTextureChannel] == Component);

						Usage->ChannelUsage[Allocation.WeightmapTextureChannel] = Component;
					}
				}
			}
		}
	}
}

void ALandscape::AddDeferredCopyLayersTexture(UTexture* InSourceTexture, UTexture* InDestTexture, FTextureResource* InDestCPUResource, const FIntPoint& InInitialPositionOffset, uint8 InSourceCurrentMip, uint8 InDestCurrentMip, uint32 InSourceArrayIndex, uint32 InDestArrayIndex)
{
	if (InSourceTexture != nullptr && InDestTexture != nullptr)
	{
		FString SourceDebugName = CVarOutputLayersDebugDrawCallName.GetValueOnGameThread() == 1 ? InSourceTexture->GetName() : GEmptyDebugName;
		FString DestDebugName = CVarOutputLayersDebugDrawCallName.GetValueOnGameThread() == 1 ? InDestTexture->GetName() : GEmptyDebugName;

		AddDeferredCopyLayersTexture(SourceDebugName, InSourceTexture->Resource, DestDebugName, InDestTexture->Resource, InDestCPUResource, InInitialPositionOffset, InSourceCurrentMip, InDestCurrentMip, InSourceArrayIndex, InDestArrayIndex);
	}
}

void ALandscape::AddDeferredCopyLayersTexture(const FString& InSourceDebugName, FTextureResource* InSourceResource, const FString& InDestDebugName, FTextureResource* InDestResource, FTextureResource* InDestCPUResource, const FIntPoint& InInitialPositionOffset,
											  uint8 InSourceCurrentMip, uint8 InDestCurrentMip, uint32 InSourceArrayIndex, uint32 InDestArrayIndex)
{
	check(InSourceResource != nullptr);
	check(InDestResource != nullptr);

	PendingCopyTextures.Add(FLandscapeLayersCopyTextureParams(InSourceDebugName, InSourceResource, InDestDebugName, InDestResource, InDestCPUResource, InInitialPositionOffset, SubsectionSizeQuads, NumSubsections, InSourceCurrentMip, InDestCurrentMip, InSourceArrayIndex, InDestArrayIndex));
}

void ALandscape::CommitDeferredCopyLayersTexture()
{
	TArray<FLandscapeLayersCopyTextureParams> LocalParams = MoveTemp(PendingCopyTextures);

	ENQUEUE_RENDER_COMMAND(FLandscapeLayersCopyAsyncCommand)(
		[LocalParams](FRHICommandListImmediate& RHICmdList) mutable
	{
		for (const FLandscapeLayersCopyTextureParams& Params : LocalParams)
		{
			FLandscapeLayersCopyTexture_RenderThread CopyTexture(Params);
			CopyTexture.Copy(RHICmdList);
		}
	});
}

void ALandscape::CopyLayersTexture(UTexture* InSourceTexture, UTexture* InDestTexture, FTextureResource* InDestCPUResource, const FIntPoint& InFirstComponentSectionBase, uint8 InSourceCurrentMip, uint8 InDestCurrentMip, uint32 InSourceArrayIndex, uint32 InDestArrayIndex) const
{
	if (InSourceTexture != nullptr && InDestTexture != nullptr)
	{
		FString SourceDebugName = CVarOutputLayersDebugDrawCallName.GetValueOnGameThread() == 1 ? InSourceTexture->GetName() : GEmptyDebugName;
		FString DestDebugName = CVarOutputLayersDebugDrawCallName.GetValueOnGameThread() == 1 ? InDestTexture->GetName() : GEmptyDebugName;

		CopyLayersTexture(SourceDebugName, InSourceTexture->Resource, DestDebugName, InDestTexture->Resource, InDestCPUResource, InFirstComponentSectionBase, InSourceCurrentMip, InDestCurrentMip, InSourceArrayIndex, InDestArrayIndex);
	}
}

void ALandscape::CopyLayersTexture(const FString& InSourceDebugName, FTextureResource* InSourceResource, const FString& InDestDebugName, FTextureResource* InDestResource, FTextureResource* InDestCPUResource, const FIntPoint& InInitialPositionOffset,
									uint8 InSourceCurrentMip, uint8 InDestCurrentMip, uint32 InSourceArrayIndex, uint32 InDestArrayIndex) const
{
	check(InSourceResource != nullptr);
	check(InDestResource != nullptr);

	FLandscapeLayersCopyTexture_RenderThread CopyTexture(FLandscapeLayersCopyTextureParams(InSourceDebugName, InSourceResource, InDestDebugName, InDestResource, InDestCPUResource, InInitialPositionOffset, SubsectionSizeQuads, NumSubsections, InSourceCurrentMip, InDestCurrentMip, InSourceArrayIndex, InDestArrayIndex));

	ENQUEUE_RENDER_COMMAND(FLandscapeLayersCopyCommand)(
		[CopyTexture](FRHICommandListImmediate& RHICmdList) mutable
	{
		CopyTexture.Copy(RHICmdList);
	});
}

void ALandscape::DrawWeightmapComponentsToRenderTarget(const FString& InDebugName, const TArray<FIntPoint>& InSectionBaseList, const FVector2D& InScaleBias, TArray<FVector2D>* InScaleBiasPerSection, UTexture* InWeightmapRTRead, UTextureRenderTarget2D* InOptionalWeightmapRTRead2, UTextureRenderTarget2D* InWeightmapRTWrite,
														ERTDrawingType InDrawType, bool InClearRTWrite, FLandscapeLayersWeightmapShaderParameters& InShaderParams, uint8 InMipRender) const
{
	check(InWeightmapRTRead != nullptr);
	check(InWeightmapRTWrite != nullptr);
	check(InScaleBiasPerSection == nullptr || InScaleBiasPerSection->Num() == InSectionBaseList.Num());

	FIntPoint WeightmapWriteTextureSize(InWeightmapRTWrite->SizeX, InWeightmapRTWrite->SizeY);
	FIntPoint WeightmapReadTextureSize(InWeightmapRTRead->Source.GetSizeX(), InWeightmapRTRead->Source.GetSizeY());
	UTextureRenderTarget2D* WeightmapRTRead = Cast<UTextureRenderTarget2D>(InWeightmapRTRead);

	if (WeightmapRTRead != nullptr)
	{
		WeightmapReadTextureSize.X = WeightmapRTRead->SizeX;
		WeightmapReadTextureSize.Y = WeightmapRTRead->SizeY;
	}

	// Quad Setup
	TArray<FLandscapeLayersTriangle> TriangleList;
	TriangleList.Reserve(InSectionBaseList.Num() * 2 * NumSubsections);

	for (int i = 0; i < InSectionBaseList.Num(); ++i)
	{
		const FVector2D& WeightmapScaleBias = InScaleBiasPerSection != nullptr ? (*InScaleBiasPerSection)[i] : InScaleBias;
		switch (InDrawType)
		{
		case ERTDrawingType::RTAtlas:
		{
			GenerateLayersRenderQuadsAtlas(InSectionBaseList[i], WeightmapScaleBias, SubsectionSizeQuads, WeightmapReadTextureSize, WeightmapWriteTextureSize, TriangleList);
		} break;

		case ERTDrawingType::RTAtlasToNonAtlas:
		{
			GenerateLayersRenderQuadsAtlasToNonAtlas(InSectionBaseList[i], WeightmapScaleBias, SubsectionSizeQuads, WeightmapReadTextureSize, WeightmapWriteTextureSize, TriangleList);
		} break;

		case ERTDrawingType::RTNonAtlas:
		{
			GenerateLayersRenderQuadsNonAtlas(InSectionBaseList[i], WeightmapScaleBias, SubsectionSizeQuads, WeightmapReadTextureSize, WeightmapWriteTextureSize, TriangleList);
		} break;

		case ERTDrawingType::RTNonAtlasToAtlas:
		{
			GenerateLayersRenderQuadsNonAtlasToAtlas(InSectionBaseList[i], WeightmapScaleBias, SubsectionSizeQuads, WeightmapReadTextureSize, WeightmapWriteTextureSize, TriangleList);
		} break;

		case ERTDrawingType::RTMips:
		{
			GenerateLayersRenderQuadsMip(InSectionBaseList[i], WeightmapScaleBias, SubsectionSizeQuads, WeightmapReadTextureSize, WeightmapWriteTextureSize, InMipRender, TriangleList);
		} break;

		default:
		{
			check(false);
			return;
		}
		}
	}

	InShaderParams.ReadWeightmap1 = InWeightmapRTRead;
	InShaderParams.ReadWeightmap2 = InOptionalWeightmapRTRead2;
	InShaderParams.CurrentMipComponentVertexCount = ((SubsectionSizeQuads + 1) >> InMipRender);

	if (InMipRender > 0)
	{
		InShaderParams.CurrentMipSize = WeightmapWriteTextureSize;
		InShaderParams.ParentMipSize = WeightmapReadTextureSize;
	}

	FMatrix ProjectionMatrix = AdjustProjectionMatrixForRHI(FTranslationMatrix(FVector(0, 0, 0)) *
		FMatrix(FPlane(1.0f / (FMath::Max<uint32>(WeightmapWriteTextureSize.X, 1.f) / 2.0f), 0.0, 0.0f, 0.0f), FPlane(0.0f, -1.0f / (FMath::Max<uint32>(WeightmapWriteTextureSize.Y, 1.f) / 2.0f), 0.0f, 0.0f), FPlane(0.0f, 0.0f, 1.0f, 0.0f), FPlane(-1.0f, 1.0f, 0.0f, 1.0f)));

	FLandscapeLayersWeightmapRender_RenderThread LayersRender(InDebugName, InWeightmapRTWrite, WeightmapWriteTextureSize, WeightmapReadTextureSize, ProjectionMatrix, InShaderParams, InMipRender, TriangleList);

	ENQUEUE_RENDER_COMMAND(FDrawLandscapeLayersWeightmapCommand)(
		[LayersRender, InClearRTWrite](FRHICommandListImmediate& RHICmdList) mutable
	{
		LayersRender.Render(RHICmdList, InClearRTWrite);
	});

	PrintLayersDebugRT(InDebugName, InWeightmapRTWrite, InMipRender, false);
}

void ALandscape::DrawWeightmapComponentsToRenderTarget(const FString& InDebugName, const TArray<ULandscapeComponent*>& InComponentsToDraw, const FIntPoint& InLandscapeBase, UTexture* InWeightmapRTRead, UTextureRenderTarget2D* InOptionalWeightmapRTRead2, UTextureRenderTarget2D* InWeightmapRTWrite,
														ERTDrawingType InDrawType, bool InClearRTWrite, FLandscapeLayersWeightmapShaderParameters& InShaderParams, uint8 InMipRender) const
{

	TArray<FIntPoint> SectionBaseList;
	SectionBaseList.Reserve(InComponentsToDraw.Num());
	TArray<FVector2D> WeightmapScaleBiasList;
	WeightmapScaleBiasList.Reserve(InComponentsToDraw.Num());

	for (ULandscapeComponent* Component : InComponentsToDraw)
	{
		FVector2D WeightmapScaleBias(Component->WeightmapScaleBias.Z, Component->WeightmapScaleBias.W);
		WeightmapScaleBiasList.Add(WeightmapScaleBias);

		FIntPoint ComponentSectionBase = Component->GetSectionBase() - InLandscapeBase;
		SectionBaseList.Add(ComponentSectionBase);
	}

	DrawWeightmapComponentsToRenderTarget(InDebugName, SectionBaseList, FVector2D::ZeroVector, &WeightmapScaleBiasList, InWeightmapRTRead, InOptionalWeightmapRTRead2, InWeightmapRTWrite, InDrawType, InClearRTWrite, InShaderParams, InMipRender);

	PrintLayersDebugRT(InDebugName, InWeightmapRTWrite, InMipRender, false);
}

void ALandscape::DrawWeightmapComponentToRenderTargetMips(const TArray<FVector2D>& InTexturePositionsToDraw, UTexture* InReadWeightmap, bool InClearRTWrite, struct FLandscapeLayersWeightmapShaderParameters& InShaderParams) const
{
	bool OutputDebugName = CVarOutputLayersDebugDrawCallName.GetValueOnAnyThread() == 1 || CVarOutputLayersRTContent.GetValueOnAnyThread() == 1 ? true : false;
	int32 CurrentMip = 1;
	UTexture* ReadMipRT = InReadWeightmap;

	// Convert from Texture position to SectionBase
	int32 LocalComponentSizeQuad = SubsectionSizeQuads * NumSubsections;
	int32 LocalComponentSizeVerts = (SubsectionSizeQuads + 1) * NumSubsections;

	TArray<FIntPoint> SectionBaseToDraw;
	SectionBaseToDraw.Reserve(InTexturePositionsToDraw.Num());

	for (const FVector2D& TexturePosition : InTexturePositionsToDraw)
	{
		FVector2D PositionOffset(FMath::RoundToInt(TexturePosition.X / LocalComponentSizeVerts), FMath::RoundToInt(TexturePosition.Y / LocalComponentSizeVerts));
		SectionBaseToDraw.Add(FIntPoint(PositionOffset.X * LocalComponentSizeQuad, PositionOffset.Y * LocalComponentSizeQuad));
	}

	FVector2D WeightmapScaleBias(0.0f, 0.0f); // we dont need a scale bias for mip drawing

	for (int32 MipRTIndex = EWeightmapRTType::WeightmapRT_Mip1; MipRTIndex < EWeightmapRTType::WeightmapRT_Count; ++MipRTIndex)
	{
		UTextureRenderTarget2D* WriteMipRT = WeightmapRTList[MipRTIndex];

		if (WriteMipRT != nullptr)
		{
			DrawWeightmapComponentsToRenderTarget(OutputDebugName ? FString::Printf(TEXT("LS Weight: %s = -> %s Mips %d"), *ReadMipRT->GetName(), *WriteMipRT->GetName(), CurrentMip) : GEmptyDebugName,
													SectionBaseToDraw, WeightmapScaleBias, nullptr, ReadMipRT, nullptr, WriteMipRT, ERTDrawingType::RTMips, InClearRTWrite, InShaderParams, CurrentMip++);
		}

		ReadMipRT = WeightmapRTList[MipRTIndex];
	}
}

void ALandscape::ConvertR32ToBGRA8(const TArray<FVector2D>& InWeightmapTextureOutputOffset, UTextureRenderTarget2D* InWeightmapRTWrite, const FLandscapeLayersWeightmapConvertFormatShaderParameter& InShaderParams)
{
	check(InShaderParams.ReadWeightmap != nullptr);
	check(InWeightmapRTWrite != nullptr);

	FIntPoint WeightmapWriteTextureSize(InWeightmapRTWrite->SizeX, InWeightmapRTWrite->SizeY);
	FIntPoint WeightmapReadTextureSize(InShaderParams.ReadWeightmap->GetSizeX(), InShaderParams.ReadWeightmap->GetSizeY());

	// Quad Setup
	const int32 LocalComponentSizeQuad = SubsectionSizeQuads * NumSubsections;
	const int32 LocalComponentSizeVerts = (SubsectionSizeQuads + 1) * NumSubsections;

	TArray<FLandscapeLayersTriangle> TriangleList;
	TriangleList.Reserve(InWeightmapTextureOutputOffset.Num() * 2 * NumSubsections);
	for (const FVector2D& TexturePosition : InWeightmapTextureOutputOffset)
	{
		FVector2D PositionOffset(FMath::RoundToInt(TexturePosition.X / LocalComponentSizeVerts), FMath::RoundToInt(TexturePosition.Y / LocalComponentSizeVerts));
		FIntPoint SectionBase(PositionOffset.X * LocalComponentSizeQuad, PositionOffset.Y * LocalComponentSizeQuad);
		GenerateLayersRenderQuadsAtlas(SectionBase, FVector2D::ZeroVector, SubsectionSizeQuads, WeightmapReadTextureSize, WeightmapWriteTextureSize, TriangleList);
	}

	FMatrix ProjectionMatrix = AdjustProjectionMatrixForRHI(FTranslationMatrix(FVector(0, 0, 0)) *
		FMatrix(FPlane(1.0f / (FMath::Max<uint32>(WeightmapWriteTextureSize.X, 1.f) / 2.0f), 0.0, 0.0f, 0.0f), FPlane(0.0f, -1.0f / (FMath::Max<uint32>(WeightmapWriteTextureSize.Y, 1.f) / 2.0f), 0.0f, 0.0f), FPlane(0.0f, 0.0f, 1.0f, 0.0f), FPlane(-1.0f, 1.0f, 0.0f, 1.0f)));

	FLandscapeLayersWeightmapConvertRender_RenderThread LayersRender(FString(TEXT("ConvertR32ToBGRA8")), InWeightmapRTWrite, WeightmapWriteTextureSize, WeightmapReadTextureSize, ProjectionMatrix, InShaderParams, 0, TriangleList);
	ENQUEUE_RENDER_COMMAND(FConvertR32ToBGRA8Command)(
		[LayersRender](FRHICommandListImmediate& RHICmdList) mutable
	{
		LayersRender.Render(RHICmdList, true);
	});
}

void ALandscape::ClearLayersWeightmapTextureResource(const FString& InDebugName, FTextureRenderTargetResource* InTextureResourceToClear) const
{
	LandscapeLayersWeightmapClear_RenderThread LayersClear(InDebugName, InTextureResourceToClear);

	ENQUEUE_RENDER_COMMAND(FLandscapeLayersClearWeightmapCommand)(
		[LayersClear](FRHICommandListImmediate& RHICmdList) mutable
	{
		LayersClear.Clear(RHICmdList);
	});
}

void ALandscape::DrawHeightmapComponentsToRenderTargetMips(const TArray<ULandscapeComponent*>& InComponentsToDraw, const FIntPoint& InLandscapeBase, UTexture* InReadHeightmap, bool InClearRTWrite, struct FLandscapeLayersHeightmapShaderParameters& InShaderParams) const
{
	bool OutputDebugName = CVarOutputLayersDebugDrawCallName.GetValueOnAnyThread() == 1 || CVarOutputLayersRTContent.GetValueOnAnyThread() == 1 ? true : false;
	int32 CurrentMip = 1;
	UTexture* ReadMipRT = InReadHeightmap;

	for (int32 MipRTIndex = EHeightmapRTType::HeightmapRT_Mip1; MipRTIndex < EHeightmapRTType::HeightmapRT_Count; ++MipRTIndex)
	{
		UTextureRenderTarget2D* WriteMipRT = HeightmapRTList[MipRTIndex];

		if (WriteMipRT != nullptr)
		{
			DrawHeightmapComponentsToRenderTarget(OutputDebugName ? FString::Printf(TEXT("LS Height: %s = -> %s CombinedAtlasWithMips %d"), *ReadMipRT->GetName(), *WriteMipRT->GetName(), CurrentMip) : GEmptyDebugName,
												  InComponentsToDraw, InLandscapeBase, ReadMipRT, nullptr, WriteMipRT, ERTDrawingType::RTMips, InClearRTWrite, InShaderParams, CurrentMip++);
		}

		ReadMipRT = HeightmapRTList[MipRTIndex];
	}
}

void ALandscape::DrawHeightmapComponentsToRenderTarget(const FString& InDebugName, const TArray<ULandscapeComponent*>& InComponentsToDraw, const FIntPoint& InLandscapeBase, UTexture* InHeightmapRTRead, UTextureRenderTarget2D* InOptionalHeightmapRTRead2, UTextureRenderTarget2D* InHeightmapRTWrite,
													  ERTDrawingType InDrawType, bool InClearRTWrite, FLandscapeLayersHeightmapShaderParameters& InShaderParams, uint8 InMipRender) const
{
	check(InHeightmapRTRead != nullptr);
	check(InHeightmapRTWrite != nullptr);

	FIntPoint HeightmapWriteTextureSize(InHeightmapRTWrite->SizeX, InHeightmapRTWrite->SizeY);
	FIntPoint HeightmapReadTextureSize(InHeightmapRTRead->Source.GetSizeX(), InHeightmapRTRead->Source.GetSizeY());
	UTextureRenderTarget2D* HeightmapRTRead = Cast<UTextureRenderTarget2D>(InHeightmapRTRead);

	if (HeightmapRTRead != nullptr)
	{
		HeightmapReadTextureSize.X = HeightmapRTRead->SizeX;
		HeightmapReadTextureSize.Y = HeightmapRTRead->SizeY;
	}

	// Quad Setup
	TArray<FLandscapeLayersTriangle> TriangleList;
	TriangleList.Reserve(InComponentsToDraw.Num() * 2 * NumSubsections);

	for (ULandscapeComponent* Component : InComponentsToDraw)
	{
		FVector2D HeightmapScaleBias(Component->HeightmapScaleBias.Z, Component->HeightmapScaleBias.W);
		FIntPoint ComponentSectionBase = Component->GetSectionBase() - InLandscapeBase;

		switch (InDrawType)
		{
			case ERTDrawingType::RTAtlas:
			{
				GenerateLayersRenderQuadsAtlas(ComponentSectionBase, HeightmapScaleBias, SubsectionSizeQuads, HeightmapReadTextureSize, HeightmapWriteTextureSize, TriangleList);
			} break;

			case ERTDrawingType::RTAtlasToNonAtlas:
			{
				GenerateLayersRenderQuadsAtlasToNonAtlas(ComponentSectionBase, HeightmapScaleBias, SubsectionSizeQuads, HeightmapReadTextureSize, HeightmapWriteTextureSize, TriangleList);
			} break;

			case ERTDrawingType::RTNonAtlas:
			{
				GenerateLayersRenderQuadsNonAtlas(ComponentSectionBase, HeightmapScaleBias, SubsectionSizeQuads, HeightmapReadTextureSize, HeightmapWriteTextureSize, TriangleList);
			} break;

			case ERTDrawingType::RTNonAtlasToAtlas:
			{
				GenerateLayersRenderQuadsNonAtlasToAtlas(ComponentSectionBase, HeightmapScaleBias, SubsectionSizeQuads, HeightmapReadTextureSize, HeightmapWriteTextureSize, TriangleList);
			} break;

			case ERTDrawingType::RTMips:
			{
				GenerateLayersRenderQuadsMip(ComponentSectionBase, HeightmapScaleBias, SubsectionSizeQuads, HeightmapReadTextureSize, HeightmapWriteTextureSize, InMipRender, TriangleList);
			} break;

			default:
			{
				check(false);
				return;
			}
		}
	}

	InShaderParams.ReadHeightmap1 = InHeightmapRTRead;
	InShaderParams.ReadHeightmap2 = InOptionalHeightmapRTRead2;
	InShaderParams.HeightmapSize = HeightmapReadTextureSize;
	InShaderParams.CurrentMipComponentVertexCount = ((SubsectionSizeQuads + 1) >> InMipRender);

	if (InMipRender > 0)
	{
		InShaderParams.CurrentMipSize = HeightmapWriteTextureSize;
		InShaderParams.ParentMipSize = HeightmapReadTextureSize;
	}

	FMatrix ProjectionMatrix = AdjustProjectionMatrixForRHI(FTranslationMatrix(FVector(0, 0, 0)) *
															FMatrix(FPlane(1.0f / (FMath::Max<uint32>(HeightmapWriteTextureSize.X, 1.f) / 2.0f), 0.0, 0.0f, 0.0f), FPlane(0.0f, -1.0f / (FMath::Max<uint32>(HeightmapWriteTextureSize.Y, 1.f) / 2.0f), 0.0f, 0.0f), FPlane(0.0f, 0.0f, 1.0f, 0.0f), FPlane(-1.0f, 1.0f, 0.0f, 1.0f)));

	FLandscapeLayersHeightmapRender_RenderThread LayersRender(InDebugName, InHeightmapRTWrite, HeightmapWriteTextureSize, HeightmapReadTextureSize, ProjectionMatrix, InShaderParams, InMipRender, TriangleList);

	ENQUEUE_RENDER_COMMAND(FDrawLandscapeLayersHeightmapCommand)(
		[LayersRender, InClearRTWrite](FRHICommandListImmediate& RHICmdList) mutable
	{
		LayersRender.Render(RHICmdList, InClearRTWrite);
	});

	PrintLayersDebugRT(InDebugName, InHeightmapRTWrite, InMipRender, true, InShaderParams.GenerateNormals);
}

void ALandscape::GenerateLayersRenderQuad(const FIntPoint& InVertexPosition, float InVertexSize, const FVector2D& InUVStart, const FVector2D& InUVSize, TArray<FLandscapeLayersTriangle>& OutTriangles) const
{
	FLandscapeLayersTriangle Tri1;

	Tri1.V0.Position = FVector2D(InVertexPosition.X, InVertexPosition.Y);
	Tri1.V1.Position = FVector2D(InVertexPosition.X + InVertexSize, InVertexPosition.Y);
	Tri1.V2.Position = FVector2D(InVertexPosition.X + InVertexSize, InVertexPosition.Y + InVertexSize);

	Tri1.V0.UV = FVector2D(InUVStart.X, InUVStart.Y);
	Tri1.V1.UV = FVector2D(InUVStart.X + InUVSize.X, InUVStart.Y);
	Tri1.V2.UV = FVector2D(InUVStart.X + InUVSize.X, InUVStart.Y + InUVSize.Y);
	OutTriangles.Add(Tri1);

	FLandscapeLayersTriangle Tri2;
	Tri2.V0.Position = FVector2D(InVertexPosition.X + InVertexSize, InVertexPosition.Y + InVertexSize);
	Tri2.V1.Position = FVector2D(InVertexPosition.X, InVertexPosition.Y + InVertexSize);
	Tri2.V2.Position = FVector2D(InVertexPosition.X, InVertexPosition.Y);

	Tri2.V0.UV = FVector2D(InUVStart.X + InUVSize.X, InUVStart.Y + InUVSize.Y);
	Tri2.V1.UV = FVector2D(InUVStart.X, InUVStart.Y + InUVSize.Y);
	Tri2.V2.UV = FVector2D(InUVStart.X, InUVStart.Y);

	OutTriangles.Add(Tri2);
}

void ALandscape::GenerateLayersRenderQuadsAtlas(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InReadSize, const FIntPoint& InWriteSize, TArray<FLandscapeLayersTriangle>& OutTriangles) const
{
	FIntPoint ComponentSectionBase = InSectionBase;
	FIntPoint UVComponentSectionBase = InSectionBase;

	int32 SubsectionSizeVerts = InSubSectionSizeQuad + 1;
	int32 LocalComponentSizeQuad = InSubSectionSizeQuad * NumSubsections;
	int32 LocalComponentSizeVerts = SubsectionSizeVerts * NumSubsections;

	FVector2D PositionOffset(FMath::RoundToInt(ComponentSectionBase.X / LocalComponentSizeQuad), FMath::RoundToInt(ComponentSectionBase.Y / LocalComponentSizeQuad));
	FVector2D ComponentsPerTexture(FMath::RoundToInt(InWriteSize.X / LocalComponentSizeQuad), FMath::RoundToInt(InWriteSize.Y / LocalComponentSizeQuad));

	if (InReadSize.X >= InWriteSize.X)
	{
		if (InReadSize.X == InWriteSize.X)
		{
			if (ComponentsPerTexture.X > 1.0f)
			{
				UVComponentSectionBase.X = PositionOffset.X * LocalComponentSizeVerts;
			}
			else
			{
				UVComponentSectionBase.X -= (UVComponentSectionBase.X + LocalComponentSizeQuad > InWriteSize.X) ? FMath::FloorToInt((PositionOffset.X / ComponentsPerTexture.X)) * ComponentsPerTexture.X * LocalComponentSizeQuad : 0;
			}
		}

		ComponentSectionBase.X -= (ComponentSectionBase.X + LocalComponentSizeQuad > InWriteSize.X) ? FMath::FloorToInt((PositionOffset.X / ComponentsPerTexture.X)) * ComponentsPerTexture.X * LocalComponentSizeQuad : 0;
		PositionOffset.X = ComponentSectionBase.X / LocalComponentSizeQuad;
	}

	if (InReadSize.Y >= InWriteSize.Y)
	{
		if (InReadSize.Y == InWriteSize.Y)
		{
			if (ComponentsPerTexture.Y > 1.0f)
			{
				UVComponentSectionBase.Y = PositionOffset.Y * LocalComponentSizeVerts;
			}
			else
			{
				UVComponentSectionBase.Y -= (UVComponentSectionBase.Y + LocalComponentSizeQuad > InWriteSize.Y) ? FMath::FloorToInt((PositionOffset.Y / ComponentsPerTexture.Y)) * ComponentsPerTexture.Y * LocalComponentSizeQuad : 0;
			}
		}

		ComponentSectionBase.Y -= (ComponentSectionBase.Y + LocalComponentSizeQuad > InWriteSize.Y) ? FMath::FloorToInt((PositionOffset.Y / ComponentsPerTexture.Y)) * ComponentsPerTexture.Y * LocalComponentSizeQuad : 0;
		PositionOffset.Y = ComponentSectionBase.Y / LocalComponentSizeQuad;
	}

	ComponentSectionBase.X = PositionOffset.X * LocalComponentSizeVerts;
	ComponentSectionBase.Y = PositionOffset.Y * LocalComponentSizeVerts;

	FVector2D UVSize((float)SubsectionSizeVerts / (float)InReadSize.X, (float)SubsectionSizeVerts / (float)InReadSize.Y);
	FIntPoint SubSectionSectionBase;

	for (int8 SubY = 0; SubY < NumSubsections; ++SubY)
	{
		for (int8 SubX = 0; SubX < NumSubsections; ++SubX)
		{
			SubSectionSectionBase.X = ComponentSectionBase.X + SubsectionSizeVerts * SubX;
			SubSectionSectionBase.Y = ComponentSectionBase.Y + SubsectionSizeVerts * SubY;

			// Offset for this component's data in texture
			FVector2D UVStart;

			if (InReadSize.X >= InWriteSize.X)
			{
				UVStart.X = ((float)UVComponentSectionBase.X / (float)InReadSize.X) + UVSize.X * (float)SubX;
			}
			else
			{
				UVStart.X = InScaleBias.X + UVSize.X * (float)SubX;
			}

			if (InReadSize.Y >= InWriteSize.Y)
			{
				UVStart.Y = ((float)UVComponentSectionBase.Y / (float)InReadSize.Y) + UVSize.Y * (float)SubY;
			}
			else
			{
				UVStart.Y = InScaleBias.Y + UVSize.Y * (float)SubY;
			}

			GenerateLayersRenderQuad(SubSectionSectionBase, SubsectionSizeVerts, UVStart, UVSize, OutTriangles);
		}
	}
}

void ALandscape::GenerateLayersRenderQuadsMip(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InReadSize, const FIntPoint& InWriteSize, uint8 InCurrentMip, TArray<FLandscapeLayersTriangle>& OutTriangles) const
{
	int32 SubsectionSizeVerts = InSubSectionSizeQuad + 1;
	int32 LocalComponentSizeQuad = InSubSectionSizeQuad * NumSubsections;
	int32 LocalComponentSizeVerts = SubsectionSizeVerts * NumSubsections;
	int32 MipSubsectionSizeVerts = SubsectionSizeVerts >> InCurrentMip;
	int32 MipLocalComponentSizeVerts = MipSubsectionSizeVerts * NumSubsections;

	FVector2D PositionOffset(FMath::RoundToInt(InSectionBase.X / LocalComponentSizeQuad), FMath::RoundToInt(InSectionBase.Y / LocalComponentSizeQuad));
	FVector2D ComponentsPerTexture(FMath::RoundToInt(InWriteSize.X / LocalComponentSizeQuad), FMath::RoundToInt(InWriteSize.Y / LocalComponentSizeQuad));

	FIntPoint ComponentSectionBase(PositionOffset.X * MipLocalComponentSizeVerts, PositionOffset.Y * MipLocalComponentSizeVerts);
	FIntPoint UVComponentSectionBase(PositionOffset.X * LocalComponentSizeVerts, PositionOffset.Y * LocalComponentSizeVerts);
	FVector2D UVSize((float)(SubsectionSizeVerts >> (InCurrentMip - 1)) / (float)InReadSize.X, (float)(SubsectionSizeVerts >> (InCurrentMip - 1)) / (float)InReadSize.Y);
	FIntPoint SubSectionSectionBase;

	for (int8 SubY = 0; SubY < NumSubsections; ++SubY)
	{
		for (int8 SubX = 0; SubX < NumSubsections; ++SubX)
		{
			SubSectionSectionBase.X = ComponentSectionBase.X + MipSubsectionSizeVerts * SubX;
			SubSectionSectionBase.Y = ComponentSectionBase.Y + MipSubsectionSizeVerts * SubY;

			// Offset for this component's data in texture
			FVector2D UVStart(((float)(UVComponentSectionBase.X >> (InCurrentMip - 1)) / (float)InReadSize.X) + UVSize.X * (float)SubX, ((float)(UVComponentSectionBase.Y >> (InCurrentMip - 1)) / (float)InReadSize.Y) + UVSize.Y * (float)SubY);

			GenerateLayersRenderQuad(SubSectionSectionBase, MipSubsectionSizeVerts, UVStart, UVSize, OutTriangles);
		}
	}
}

void ALandscape::GenerateLayersRenderQuadsAtlasToNonAtlas(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InReadSize, const FIntPoint& InWriteSize, TArray<struct FLandscapeLayersTriangle>& OutTriangles) const
{
	int32 SubsectionSizeVerts = InSubSectionSizeQuad + 1;
	FVector2D UVSize((float)SubsectionSizeVerts / (float)InReadSize.X, (float)SubsectionSizeVerts / (float)InReadSize.Y);

	for (int8 SubY = 0; SubY < NumSubsections; ++SubY)
	{
		for (int8 SubX = 0; SubX < NumSubsections; ++SubX)
		{
			FIntPoint SubSectionSectionBase(InSectionBase.X + InSubSectionSizeQuad * SubX, InSectionBase.Y + InSubSectionSizeQuad * SubY);
			FVector2D PositionOffset(FMath::RoundToInt(SubSectionSectionBase.X / InSubSectionSizeQuad), FMath::RoundToInt(SubSectionSectionBase.Y / InSubSectionSizeQuad));
			FIntPoint UVComponentSectionBase(PositionOffset.X * SubsectionSizeVerts, PositionOffset.Y * SubsectionSizeVerts);

			// Offset for this component's data in texture
			FVector2D UVStart;

			if (InReadSize.X >= InWriteSize.X)
			{
				UVStart.X = ((float)UVComponentSectionBase.X / (float)InReadSize.X);
			}
			else
			{
				UVStart.X = InScaleBias.X + UVSize.X * (float)SubX;
			}

			if (InReadSize.Y >= InWriteSize.Y)
			{
				UVStart.Y = ((float)UVComponentSectionBase.Y / (float)InReadSize.Y);
			}
			else
			{
				UVStart.Y = InScaleBias.Y + UVSize.Y * (float)SubY;
			}

			GenerateLayersRenderQuad(SubSectionSectionBase, SubsectionSizeVerts, UVStart, UVSize, OutTriangles);
		}
	}
}

void ALandscape::GenerateLayersRenderQuadsNonAtlas(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InReadSize, const FIntPoint& InWriteSize, TArray<struct FLandscapeLayersTriangle>& OutTriangles) const
{
	// We currently only support drawing in non atlas mode with the same texture size
	check(InReadSize.X == InWriteSize.X && InReadSize.Y == InWriteSize.Y);

	int32 SubsectionSizeVerts = InSubSectionSizeQuad + 1;
	
	FVector2D UVSize((float)SubsectionSizeVerts / (float)InReadSize.X, (float)SubsectionSizeVerts / (float)InReadSize.Y);
	
	for (int8 SubY = 0; SubY < NumSubsections; ++SubY)
	{
		for (int8 SubX = 0; SubX < NumSubsections; ++SubX)
		{
			FIntPoint SubSectionSectionBase(InSectionBase.X + SubsectionSizeQuads * SubX, InSectionBase.Y + SubsectionSizeQuads * SubY);
			FVector2D PositionOffset(FMath::RoundToInt(SubSectionSectionBase.X / InSubSectionSizeQuad), FMath::RoundToInt(SubSectionSectionBase.Y / InSubSectionSizeQuad));
			FIntPoint UVComponentSectionBase(PositionOffset.X * InSubSectionSizeQuad, PositionOffset.Y * InSubSectionSizeQuad);

			// Offset for this component's data in texture
			FVector2D UVStart(((float)UVComponentSectionBase.X / (float)InReadSize.X), ((float)UVComponentSectionBase.Y / (float)InReadSize.Y));
			GenerateLayersRenderQuad(SubSectionSectionBase, SubsectionSizeVerts, UVStart, UVSize, OutTriangles);
		}
	}
}

void ALandscape::GenerateLayersRenderQuadsNonAtlasToAtlas(const FIntPoint& InSectionBase, const FVector2D& InScaleBias, float InSubSectionSizeQuad, const FIntPoint& InReadSize, const FIntPoint& InWriteSize, TArray<struct FLandscapeLayersTriangle>& OutTriangles) const
{
	int32 SubsectionSizeVerts = InSubSectionSizeQuad + 1;
	int32 LocalComponentSizeQuad = InSubSectionSizeQuad * NumSubsections;
	int32 LocalComponentSizeVerts = SubsectionSizeVerts * NumSubsections;

	FVector2D PositionOffset(FMath::RoundToInt(InSectionBase.X / LocalComponentSizeQuad), FMath::RoundToInt(InSectionBase.Y / LocalComponentSizeQuad));
	FIntPoint ComponentSectionBase(PositionOffset.X * LocalComponentSizeVerts, PositionOffset.Y * LocalComponentSizeVerts);
	FVector2D UVSize((float)SubsectionSizeVerts / (float)InReadSize.X, (float)SubsectionSizeVerts / (float)InReadSize.Y);

	FIntPoint SubSectionSectionBase;

	for (int8 SubY = 0; SubY < NumSubsections; ++SubY)
	{
		for (int8 SubX = 0; SubX < NumSubsections; ++SubX)
		{
			SubSectionSectionBase.X = ComponentSectionBase.X + SubsectionSizeVerts * SubX;
			SubSectionSectionBase.Y = ComponentSectionBase.Y + SubsectionSizeVerts * SubY;

			// Offset for this component's data in texture
			float ScaleBiasZ = (float)InSectionBase.X / (float)InReadSize.X;
			float ScaleBiasW = (float)InSectionBase.Y / (float)InReadSize.Y;
			FVector2D UVStart(ScaleBiasZ + ((float)InSubSectionSizeQuad / (float)InReadSize.X) * (float)SubX, ScaleBiasW + ((float)InSubSectionSizeQuad / (float)InReadSize.Y) * (float)SubY);

			GenerateLayersRenderQuad(SubSectionSectionBase, SubsectionSizeVerts, UVStart, UVSize, OutTriangles);
		}
	}
}

void ALandscape::PrintLayersDebugHeightData(const FString& InContext, const TArray<FColor>& InHeightmapData, const FIntPoint& InDataSize, uint8 InMipRender, bool InOutputNormals) const
{
	bool DisplayDebugPrint = CVarOutputLayersRTContent.GetValueOnAnyThread() == 1 ? true : false;
	bool DisplayHeightAsDelta = false;

	if (!DisplayDebugPrint)
	{
		return;
	}

	TArray<uint16> HeightData;
	TArray<FVector> NormalData;
	HeightData.Reserve(InHeightmapData.Num());
	NormalData.Reserve(InHeightmapData.Num());

	for (FColor Color : InHeightmapData)
	{
		uint16 Height = ((Color.R << 8) | Color.G);
		HeightData.Add(Height);

		if (InOutputNormals)
		{
			FVector Normal;
			Normal.X = Color.B > 0.0f ? ((float)Color.B / 127.5f - 1.0f) : 0.0f;
			Normal.Y = Color.A > 0.0f ? ((float)Color.A / 127.5f - 1.0f) : 0.0f;
			Normal.Z = 0.0f;

			NormalData.Add(Normal);
		}
	}

	UE_LOG(LogLandscapeBP, Display, TEXT("Context: %s"), *InContext);

	int32 MipSize = ((SubsectionSizeQuads + 1) >> InMipRender);

	for (int32 Y = 0; Y < InDataSize.Y; ++Y)
	{
		FString HeightmapHeightOutput;

		for (int32 X = 0; X < InDataSize.X; ++X)
		{
			int32 HeightDelta = HeightData[X + Y * InDataSize.X];

			if (DisplayHeightAsDelta)
			{
				HeightDelta = HeightDelta >= 32768 ? HeightDelta - 32768 : HeightDelta;
			}

			if (X > 0 && MipSize > 0 && X % MipSize == 0)
			{
				HeightmapHeightOutput += FString::Printf(TEXT("  "));
			}

			FString HeightStr = FString::Printf(TEXT("%d"), HeightDelta);

			int32 PadCount = 5 - HeightStr.Len();
			if (PadCount > 0)
			{
				HeightStr = FString::ChrN(PadCount, '0') + HeightStr;
			}

			HeightmapHeightOutput += HeightStr + TEXT(" ");
		}

		if (Y > 0 && MipSize > 0 && Y % MipSize == 0)
		{
			UE_LOG(LogLandscapeBP, Display, TEXT(""));
		}

		UE_LOG(LogLandscapeBP, Display, TEXT("%s"), *HeightmapHeightOutput);
	}

	if (InOutputNormals)
	{
		UE_LOG(LogLandscapeBP, Display, TEXT(""));

		for (int32 Y = 0; Y < InDataSize.Y; ++Y)
		{
			FString HeightmapNormaltOutput;

			for (int32 X = 0; X < InDataSize.X; ++X)
			{
				FVector Normal = NormalData[X + Y * InDataSize.X];

				if (X > 0 && MipSize > 0 && X % MipSize == 0)
				{
					HeightmapNormaltOutput += FString::Printf(TEXT("  "));
				}

				HeightmapNormaltOutput += FString::Printf(TEXT(" %s"), *Normal.ToString());
			}

			if (Y > 0 && MipSize > 0 && Y % MipSize == 0)
			{
				UE_LOG(LogLandscapeBP, Display, TEXT(""));
			}

			UE_LOG(LogLandscapeBP, Display, TEXT("%s"), *HeightmapNormaltOutput);
		}
	}
}

void ALandscape::PrintLayersDebugWeightData(const FString& InContext, const TArray<FColor>& InWeightmapData, const FIntPoint& InDataSize, uint8 InMipRender) const
{
	bool DisplayDebugPrint = (CVarOutputLayersRTContent.GetValueOnAnyThread() == 1 || CVarOutputLayersWeightmapsRTContent.GetValueOnAnyThread() == 1) ? true : false;

	if (!DisplayDebugPrint)
	{
		return;
	}

	UE_LOG(LogLandscapeBP, Display, TEXT("Context: %s"), *InContext);

	int32 MipSize = ((SubsectionSizeQuads + 1) >> InMipRender);

	for (int32 Y = 0; Y < InDataSize.Y; ++Y)
	{
		FString WeightmapOutput;

		for (int32 X = 0; X < InDataSize.X; ++X)
		{
			const FColor& Weight = InWeightmapData[X + Y * InDataSize.X];

			if (X > 0 && MipSize > 0 && X % MipSize == 0)
			{
				WeightmapOutput += FString::Printf(TEXT("  "));
			}

			WeightmapOutput += FString::Printf(TEXT("%s "), *Weight.ToString());
		}

		if (Y > 0 && MipSize > 0 && Y % MipSize == 0)
		{
			UE_LOG(LogLandscapeBP, Display, TEXT(""));
		}

		UE_LOG(LogLandscapeBP, Display, TEXT("%s"), *WeightmapOutput);
	}
}

void ALandscape::PrintLayersDebugRT(const FString& InContext, UTextureRenderTarget2D* InDebugRT, uint8 InMipRender, bool InOutputHeight, bool InOutputNormals) const
{
	bool DisplayDebugPrint = (CVarOutputLayersRTContent.GetValueOnAnyThread() == 1 || CVarOutputLayersWeightmapsRTContent.GetValueOnAnyThread() == 1) ? true : false;

	if (!DisplayDebugPrint)
	{
		return;
	}

	FTextureRenderTargetResource* RenderTargetResource = InDebugRT->GameThread_GetRenderTargetResource();
	ENQUEUE_RENDER_COMMAND(FLandscapeLayersDebugRenderTargetResolveCommand)(
		[RenderTargetResource](FRHICommandListImmediate& RHICmdList) mutable
	{
		// Copy (resolve) the rendered image from the frame buffer to its render target texture
		RHICmdList.CopyToResolveTarget(RenderTargetResource->GetRenderTargetTexture(), RenderTargetResource->TextureRHI, FResolveParams());
	});

	FlushRenderingCommands();
	int32 MinX, MinY, MaxX, MaxY;
	const ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY);
	FIntRect SampleRect = FIntRect(0, 0, InDebugRT->SizeX, InDebugRT->SizeY);

	FReadSurfaceDataFlags Flags(RCM_UNorm, CubeFace_MAX);

	TArray<FColor> OutputRT;
	OutputRT.Reserve(SampleRect.Width() * SampleRect.Height());

	InDebugRT->GameThread_GetRenderTargetResource()->ReadPixels(OutputRT, Flags, SampleRect);

	if (InOutputHeight)
	{
		PrintLayersDebugHeightData(InContext, OutputRT, FIntPoint(SampleRect.Width(), SampleRect.Height()), InMipRender, InOutputNormals);
	}
	else
	{
		PrintLayersDebugWeightData(InContext, OutputRT, FIntPoint(SampleRect.Width(), SampleRect.Height()), InMipRender);
	}
}

void ALandscape::PrintLayersDebugTextureResource(const FString& InContext, FTextureResource* InTextureResource, uint8 InMipRender, bool InOutputHeight, bool InOutputNormals) const
{
	bool DisplayDebugPrint = (CVarOutputLayersRTContent.GetValueOnAnyThread() == 1 || CVarOutputLayersWeightmapsRTContent.GetValueOnAnyThread() == 1) ? true : false;

	if (!DisplayDebugPrint)
	{
		return;
	}

	int32 MinX, MinY, MaxX, MaxY;
	const ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY);
	FIntRect SampleRect = FIntRect(0, 0, InTextureResource->GetSizeX(), InTextureResource->GetSizeY());

	TArray<FColor> OutputTexels;
	OutputTexels.Reserve(SampleRect.Width() * SampleRect.Height());

	FReadSurfaceDataFlags Flags(RCM_UNorm, CubeFace_MAX);
	Flags.SetMip(InMipRender);

	ENQUEUE_RENDER_COMMAND(FLandscapeLayersDebugReadSurfaceCommand)(
		[SourceTextureRHI = InTextureResource->TextureRHI, SampleRect = SampleRect, OutData = &OutputTexels, ReadFlags = Flags](FRHICommandListImmediate& RHICmdList) mutable
	{
		RHICmdList.ReadSurfaceData(SourceTextureRHI, SampleRect, *OutData, ReadFlags);
	});

	FlushRenderingCommands();

	if (InOutputHeight)
	{
		PrintLayersDebugHeightData(InContext, OutputTexels, FIntPoint(SampleRect.Width(), SampleRect.Height()), InMipRender, InOutputNormals);
	}
	else
	{
		PrintLayersDebugWeightData(InContext, OutputTexels, FIntPoint(SampleRect.Width(), SampleRect.Height()), InMipRender);
	}
}

bool ALandscape::PrepareLayersHeightmapTextureResources(bool bInWaitForStreaming) const
{
	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return false;
	}

	bool IsReady = true;

	Info->ForAllLandscapeProxies([&](ALandscapeProxy* Proxy)
	{
		FLandscapeIsTextureFullyStreamedIn IsTextureFullyStreamedIn;
				
		for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
		{
			UTexture2D* ComponentHeightmap = Component->GetHeightmap();

			IsReady &= IsTextureFullyStreamedIn(ComponentHeightmap, bInWaitForStreaming);

			for (const FLandscapeLayer& Layer : LandscapeLayers)
			{
				FLandscapeLayerComponentData* ComponentLayerData = Component->GetLayerData(Layer.Guid);

				IsReady &= ComponentLayerData != nullptr;

				if (ComponentLayerData != nullptr)
				{
					UTexture2D* LayerHeightmap = ComponentLayerData->HeightmapData.Texture;

					if ((LayerHeightmap->IsAsyncCacheComplete() || bInWaitForStreaming) && LayerHeightmap->Resource == nullptr)
					{
						LayerHeightmap->FinishCachePlatformData();

						LayerHeightmap->Resource = LayerHeightmap->CreateResource();

						if (LayerHeightmap->Resource != nullptr)
						{
							BeginInitResource(LayerHeightmap->Resource);
						}
					}

					IsReady &= IsTextureFullyStreamedIn(LayerHeightmap, bInWaitForStreaming);
					IsReady &= bInWaitForStreaming || (LayerHeightmap->Resource != nullptr && LayerHeightmap->Resource->IsInitialized());
				}
			}
		}
	});

	if (bInWaitForStreaming)
	{
		FlushRenderingCommands();
	}

	return IsReady;
}

int32 ALandscape::RegenerateLayersHeightmaps(const TArray<ULandscapeComponent*>& InLandscapeComponents, bool bInWaitForStreaming)
{
	SCOPE_CYCLE_COUNTER(STAT_LandscapeLayersRegenerateHeightmaps);
	ULandscapeInfo* Info = GetLandscapeInfo();

	const int32 AllHeightmapUpdateModes = (ELandscapeLayerUpdateMode::Update_Heightmap_All | ELandscapeLayerUpdateMode::Update_Heightmap_Editing | ELandscapeLayerUpdateMode::Update_Heightmap_Editing_NoCollision);
	const int32 HeightmapUpdateModes = LayerContentUpdateModes & AllHeightmapUpdateModes;
	const bool bForceRender = CVarOutputLayersDebugDrawCallName.GetValueOnAnyThread() == 1;

	if ((HeightmapUpdateModes == 0 && !bForceRender) || Info == nullptr || !PrepareLayersHeightmapTextureResources(bInWaitForStreaming))
	{
		return 0;
	}
	   
	// Handle missing Heightmap CPUReadback (Undo of a delete landscape component can trigger that case)
	if (HeightmapUpdateModes)
	{
		Info->ForAllLandscapeProxies([&](ALandscapeProxy* Proxy)
		{
			for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
			{
				UTexture2D* ComponentHeightmap = Component->GetHeightmap();
				FLandscapeLayersTexture2DCPUReadBackResource** CPUReadback = Proxy->HeightmapsCPUReadBack.Find(ComponentHeightmap);
				if (CPUReadback == nullptr)
				{
					FLandscapeLayersTexture2DCPUReadBackResource* NewCPUReadBackResource = new FLandscapeLayersTexture2DCPUReadBackResource(ComponentHeightmap->Source.GetSizeX(), ComponentHeightmap->Source.GetSizeY(), ComponentHeightmap->GetPixelFormat(), ComponentHeightmap->Source.GetNumMips());
					BeginInitResource(NewCPUReadBackResource);
					Proxy->HeightmapsCPUReadBack.Add(ComponentHeightmap, NewCPUReadBackResource);
				}
			}
		});
	}

	if (HeightmapUpdateModes || bForceRender)
	{
		check(HeightmapRTList.Num() > 0);

		FIntRect LandscapeExtent;
		if (!Info->GetLandscapeExtent(LandscapeExtent.Min.X, LandscapeExtent.Min.Y, LandscapeExtent.Max.X, LandscapeExtent.Max.Y))
		{
			return 0;
		}

		// Use to compute TopLeft Component per Heightmap
		struct FHeightmapTopLeft
		{
			FHeightmapTopLeft(UTexture2D* InTexture, FIntPoint InTopLeftSectionBase, FLandscapeLayersTexture2DCPUReadBackResource* InCPUReadback = nullptr) 
			: Texture(InTexture)
			, TopLeftSectionBase(InTopLeftSectionBase)
			, CPUReadback(InCPUReadback)
			{}

			FHeightmapTopLeft(FHeightmapTopLeft&&) = default;
			FHeightmapTopLeft& operator=(FHeightmapTopLeft&&) = default;

			UTexture2D* Texture;
			FIntPoint TopLeftSectionBase;
			FLandscapeLayersTexture2DCPUReadBackResource* CPUReadback;
		};

		// Calculate Top Left Lambda
		auto GetUniqueHeightmaps = [&](const TArray<ULandscapeComponent*>& InLandscapeComponent, TArray<FHeightmapTopLeft>& OutHeightmaps, const FIntPoint& LandscapeBaseQuads, const FGuid& InLayerGuid = FGuid())
		{
			FScopedSetLandscapeEditingLayer Scope(this, InLayerGuid);
			
			const int32 ComponentSizeQuad = SubsectionSizeQuads * NumSubsections;
			const int32 ComponentSizeVerts = (SubsectionSizeQuads + 1) * NumSubsections;
			for (ULandscapeComponent* Component : InLandscapeComponents)
			{
				UTexture2D* ComponentHeightmap = Component->GetHeightmap(true);
		
				int32 Index = OutHeightmaps.IndexOfByPredicate([=](const FHeightmapTopLeft& LayerHeightmap) { return LayerHeightmap.Texture == ComponentHeightmap; });

				FIntPoint ComponentSectionBase = Component->GetSectionBase() - LandscapeBaseQuads;
				FVector2D SourcePositionOffset(FMath::RoundToInt(ComponentSectionBase.X / ComponentSizeQuad), FMath::RoundToInt(ComponentSectionBase.Y / ComponentSizeQuad));
				FIntPoint ComponentVertexPosition = FIntPoint(SourcePositionOffset.X * ComponentSizeVerts, SourcePositionOffset.Y * ComponentSizeVerts);
				ALandscapeProxy* Proxy = Component->GetLandscapeProxy();

				if (Index == INDEX_NONE)
				{
					FLandscapeLayersTexture2DCPUReadBackResource** CPUReadback = Proxy->HeightmapsCPUReadBack.Find(ComponentHeightmap);
					OutHeightmaps.Add(FHeightmapTopLeft(ComponentHeightmap, ComponentVertexPosition, CPUReadback != nullptr ? *CPUReadback : nullptr));
				}
				else
				{
					OutHeightmaps[Index].TopLeftSectionBase.X = FMath::Min(OutHeightmaps[Index].TopLeftSectionBase.X, ComponentVertexPosition.X);
					OutHeightmaps[Index].TopLeftSectionBase.Y = FMath::Min(OutHeightmaps[Index].TopLeftSectionBase.Y, ComponentVertexPosition.Y);
				}
			}
		};

		FLandscapeLayersHeightmapShaderParameters ShaderParams;

		bool FirstLayer = true;
		UTextureRenderTarget2D* CombinedHeightmapAtlasRT = HeightmapRTList[EHeightmapRTType::HeightmapRT_CombinedAtlas];
		UTextureRenderTarget2D* CombinedHeightmapNonAtlasRT = HeightmapRTList[EHeightmapRTType::HeightmapRT_CombinedNonAtlas];
		UTextureRenderTarget2D* LandscapeScratchRT1 = HeightmapRTList[EHeightmapRTType::HeightmapRT_Scratch1];
		UTextureRenderTarget2D* LandscapeScratchRT2 = HeightmapRTList[EHeightmapRTType::HeightmapRT_Scratch2];
		UTextureRenderTarget2D* LandscapeScratchRT3 = HeightmapRTList[EHeightmapRTType::HeightmapRT_Scratch3];

		bool OutputDebugName = (CVarOutputLayersDebugDrawCallName.GetValueOnAnyThread() == 1 || CVarOutputLayersRTContent.GetValueOnAnyThread() == 1) ? true : false;

		for (FLandscapeLayer& Layer : LandscapeLayers)
		{
			//Draw Layer heightmap to Combined RT Atlas
			ShaderParams.ApplyLayerModifiers = false;
			ShaderParams.LayerVisible = Layer.bVisible;
			ShaderParams.GenerateNormals = false;
			ShaderParams.LayerBlendMode = Layer.BlendMode;

			if (Layer.BlendMode == LSBM_AlphaBlend)
			{
				// For now, only Layer reserved for Landscape Splines will use the AlphaBlendMode
				const FLandscapeLayer* SplinesReservedLayer = GetLandscapeSplinesReservedLayer();
				check(&Layer == SplinesReservedLayer);
				ShaderParams.LayerAlpha = 1.0f;
			}
			else
			{
				check(Layer.BlendMode == LSBM_AdditiveBlend);
				ShaderParams.LayerAlpha = Layer.HeightmapAlpha;
			}

			{
				TArray<FHeightmapTopLeft> LayerHeightmaps;
				GetUniqueHeightmaps(InLandscapeComponents, LayerHeightmaps, LandscapeExtent.Min, Layer.Guid);
				for (const FHeightmapTopLeft& LayerHeightmap : LayerHeightmaps)
				{
					AddDeferredCopyLayersTexture(LayerHeightmap.Texture, LandscapeScratchRT1, nullptr, LayerHeightmap.TopLeftSectionBase);
				}
			}
			CommitDeferredCopyLayersTexture();

			// NOTE: From this point on, we always work in non atlas, we'll convert back at the end to atlas only
			DrawHeightmapComponentsToRenderTarget(OutputDebugName ? FString::Printf(TEXT("LS Height: %s += -> NonAtlas %s"), *Layer.Name.ToString(), *LandscapeScratchRT1->GetName(), *LandscapeScratchRT2->GetName()) : GEmptyDebugName,
												  InLandscapeComponents, LandscapeExtent.Min, LandscapeScratchRT1, nullptr, LandscapeScratchRT2, ERTDrawingType::RTAtlasToNonAtlas, true, ShaderParams);

			ShaderParams.ApplyLayerModifiers = true;

			// Combine Current layer with current result
			DrawHeightmapComponentsToRenderTarget(OutputDebugName ? FString::Printf(TEXT("LS Height: %s += -> CombinedNonAtlas %s"), *Layer.Name.ToString(), *LandscapeScratchRT2->GetName(), *CombinedHeightmapNonAtlasRT->GetName()) : GEmptyDebugName,
												InLandscapeComponents, LandscapeExtent.Min, LandscapeScratchRT2, FirstLayer ? nullptr : LandscapeScratchRT3, CombinedHeightmapNonAtlasRT, ERTDrawingType::RTNonAtlas, FirstLayer, ShaderParams);

			ShaderParams.ApplyLayerModifiers = false;

			if (Layer.bVisible)
			{
				// Draw each brushes
				for (int32 i = 0; i < Layer.Brushes.Num(); ++i)
				{
					// TODO: handle conversion from float to RG8 by using material params to write correct values
					// TODO: handle conversion/handling of RT not same size as internal size

					FLandscapeLayerBrush& Brush = Layer.Brushes[i];
					UTextureRenderTarget2D* BrushOutputNonAtlasRT = Brush.Render(true, LandscapeExtent, CombinedHeightmapNonAtlasRT);
					if (BrushOutputNonAtlasRT == nullptr || BrushOutputNonAtlasRT->SizeX != CombinedHeightmapNonAtlasRT->SizeX || BrushOutputNonAtlasRT->SizeY != CombinedHeightmapNonAtlasRT->SizeY)
					{
						continue;
					}

					INC_DWORD_STAT(STAT_LandscapeLayersRegenerateDrawCalls); // Brush Render

					PrintLayersDebugRT(OutputDebugName ? FString::Printf(TEXT("LS Height: %s %s -> BrushNonAtlas %s"), *Layer.Name.ToString(), *Brush.GetBrush()->GetName(), *BrushOutputNonAtlasRT->GetName()) : GEmptyDebugName, BrushOutputNonAtlasRT);

					// Resolve back to Combined heightmap
					CopyLayersTexture(BrushOutputNonAtlasRT, CombinedHeightmapNonAtlasRT);
					PrintLayersDebugRT(OutputDebugName ? FString::Printf(TEXT("LS Height: %s Component %s += -> CombinedNonAtlas %s"), *Layer.Name.ToString(), *BrushOutputNonAtlasRT->GetName(), *CombinedHeightmapNonAtlasRT->GetName()) : GEmptyDebugName, CombinedHeightmapNonAtlasRT);
				}
			}

			CopyLayersTexture(CombinedHeightmapNonAtlasRT, LandscapeScratchRT3);
			PrintLayersDebugRT(OutputDebugName ? FString::Printf(TEXT("LS Height: %s Component %s += -> CombinedNonAtlas %s"), *Layer.Name.ToString(), *CombinedHeightmapNonAtlasRT->GetName(), *LandscapeScratchRT3->GetName()) : GEmptyDebugName, LandscapeScratchRT3);

			FirstLayer = false;
		}

		ShaderParams.GenerateNormals = true;
		ShaderParams.GridSize = GetRootComponent()->RelativeScale3D;

		DrawHeightmapComponentsToRenderTarget(OutputDebugName ? FString::Printf(TEXT("LS Height: %s = -> CombinedNonAtlasNormals : %s"), *CombinedHeightmapNonAtlasRT->GetName(), *LandscapeScratchRT1->GetName()) : GEmptyDebugName,
											  InLandscapeComponents, LandscapeExtent.Min, CombinedHeightmapNonAtlasRT, nullptr, LandscapeScratchRT1, ERTDrawingType::RTNonAtlas, true, ShaderParams);

		ShaderParams.GenerateNormals = false;

		DrawHeightmapComponentsToRenderTarget(OutputDebugName ? FString::Printf(TEXT("LS Height: %s = -> CombinedAtlasFinal : %s"), *LandscapeScratchRT1->GetName(), *CombinedHeightmapAtlasRT->GetName()) : GEmptyDebugName,
											  InLandscapeComponents, LandscapeExtent.Min, LandscapeScratchRT1, nullptr, CombinedHeightmapAtlasRT, ERTDrawingType::RTNonAtlasToAtlas, true, ShaderParams);

		DrawHeightmapComponentsToRenderTargetMips(InLandscapeComponents, LandscapeExtent.Min, CombinedHeightmapAtlasRT, true, ShaderParams);

		// Copy back all Mips to original heightmap data
		{
			TArray<FHeightmapTopLeft> Heightmaps;
			GetUniqueHeightmaps(InLandscapeComponents, Heightmaps, LandscapeExtent.Min);
			for (const FHeightmapTopLeft& Heightmap : Heightmaps)
			{
				FIntPoint TextureTopLeftSectionBase = Heightmap.TopLeftSectionBase;
				uint8 MipIndex = 0;

				check(Heightmap.CPUReadback);

				// Mip 0
				AddDeferredCopyLayersTexture(CombinedHeightmapAtlasRT, Heightmap.Texture, Heightmap.CPUReadback, TextureTopLeftSectionBase, MipIndex, MipIndex);
				++MipIndex;

				// Other Mips
				for (int32 MipRTIndex = EHeightmapRTType::HeightmapRT_Mip1; MipRTIndex < EHeightmapRTType::HeightmapRT_Count; ++MipRTIndex)
				{
					UTextureRenderTarget2D* RenderTargetMip = HeightmapRTList[MipRTIndex];

					if (RenderTargetMip != nullptr)
					{
						AddDeferredCopyLayersTexture(RenderTargetMip, Heightmap.Texture, Heightmap.CPUReadback, TextureTopLeftSectionBase, MipIndex, MipIndex);
						++MipIndex;
					}
				}
			}
		}
		CommitDeferredCopyLayersTexture();
	}

	if (HeightmapUpdateModes)
	{
		ResolveLayersHeightmapTexture(InLandscapeComponents);

		// Partial Component Update
		for (ULandscapeComponent* Component : InLandscapeComponents)
		{
			if(Component->IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag::Component_Update_Heightmap_Collision, HeightmapUpdateModes))
			{
				Component->UpdateCachedBounds();
				Component->UpdateComponentToWorld();

				// Avoid updating height field if we are going to recreate collision in this update
				bool bUpdateHeightfieldRegion = !Component->IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag::Component_Update_Recreate_Collision, HeightmapUpdateModes);
				Component->UpdateCollisionData(bUpdateHeightfieldRegion);
			}
			else if (Component->IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag::Component_Update_Approximated_Bounds, HeightmapUpdateModes))
			{
				// Update bounds with an approximated value (real computation will be done anyways when computing collision)
				const bool bInApproximateBounds = true;
				Component->UpdateCachedBounds(bInApproximateBounds);
				Component->UpdateComponentToWorld();
			}
		}

		Info->UpdateAllAddCollisions();
	}

	return HeightmapUpdateModes;
}

void ALandscape::ResolveLayersHeightmapTexture(const TArray<ULandscapeComponent*>& InLandscapeComponents)
{
	SCOPE_CYCLE_COUNTER(STAT_LandscapeLayersResolveHeightmaps);

	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return;
	}

	TArray<UTexture*> ProcessedTexture;

	for (ULandscapeComponent* Component : InLandscapeComponents)
	{
		UTexture2D* ComponentHeightmap = Component->GetHeightmap();

		if (!ProcessedTexture.Contains(ComponentHeightmap))
		{
			FLandscapeLayersTexture2DCPUReadBackResource** CPUReadback = Component->GetLandscapeProxy()->HeightmapsCPUReadBack.Find(ComponentHeightmap);

			if (CPUReadback == nullptr)
			{
				continue;
			}

			ProcessedTexture.Add(ComponentHeightmap);

			ResolveLayersTexture(*CPUReadback, ComponentHeightmap);
		}
	}
}

void ALandscape::ResolveLayersTexture(FLandscapeLayersTexture2DCPUReadBackResource* InCPUReadBackTexture, UTexture2D* InOutputTexture)
{
	SCOPE_CYCLE_COUNTER(STAT_LandscapeLayersResolveTexture);

	TArray<TArray<FColor>> OutMipsData;
	OutMipsData.AddDefaulted(InCPUReadBackTexture->TextureRHI->GetNumMips());

	ENQUEUE_RENDER_COMMAND(FLandscapeLayersReadSurfaceCommand)(
		[InCPUReadBackTexture, &OutMipsData](FRHICommandListImmediate& RHICmdList) mutable
	{
		int32 MipSizeU = InCPUReadBackTexture->GetSizeX();
		int32 MipSizeV = InCPUReadBackTexture->GetSizeY();
		int32 MipIndex = 0;

		while (MipSizeU >= 1 && MipSizeV >= 1)
		{
			OutMipsData[MipIndex].Reset();

			FReadSurfaceDataFlags Flags(RCM_UNorm, CubeFace_MAX);
			Flags.SetMip(MipIndex);
			FIntRect Rect(0, 0, MipSizeU, MipSizeV);

			RHICmdList.ReadSurfaceData(InCPUReadBackTexture->TextureRHI, Rect, OutMipsData[MipIndex], Flags);

			MipSizeU >>= 1;
			MipSizeV >>= 1;
			++MipIndex;
		}
	});

	// TODO: find a way to NOT have to flush the rendering command as this create hic up of ~10-15ms
	FlushRenderingCommands();

	for (int8 MipIndex = 0; MipIndex < OutMipsData.Num(); ++MipIndex)
	{
		if (OutMipsData[MipIndex].Num() > 0)
		{
			FColor* TextureData = (FColor*)InOutputTexture->Source.LockMip(MipIndex);
			FMemory::Memcpy(TextureData, OutMipsData[MipIndex].GetData(), OutMipsData[MipIndex].Num() * sizeof(FColor));
			InOutputTexture->Source.UnlockMip(MipIndex);
		}
	}	
}

void ALandscape::PrepareComponentDataToExtractMaterialLayersCS(const TArray<ULandscapeComponent*>& InLandscapeComponents, const FLandscapeLayer& InLayer, int32 InCurrentWeightmapToProcessIndex, const FIntPoint& InLandscapeBase, bool InOutputDebugName, FLandscapeTexture2DResource* InOutTextureData,
																TArray<FLandscapeLayerWeightmapExtractMaterialLayersComponentData>& OutComponentData, TMap<ULandscapeLayerInfoObject*, int32>& OutLayerInfoObjects)
{
	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return;
	}

	const int32 LocalComponentSizeQuad = SubsectionSizeQuads * NumSubsections;
	const int32 LocalComponentSizeVerts = (SubsectionSizeQuads + 1) * NumSubsections;
	for (ULandscapeComponent* Component : InLandscapeComponents)
	{
		FLandscapeLayerComponentData* ComponentLayerData = Component->GetLayerData(InLayer.Guid);

		if (ComponentLayerData != nullptr)
		{
			if (ComponentLayerData->WeightmapData.Textures.IsValidIndex(InCurrentWeightmapToProcessIndex) && ComponentLayerData->WeightmapData.TextureUsages.IsValidIndex(InCurrentWeightmapToProcessIndex))
			{
				UTexture2D* LayerWeightmap = ComponentLayerData->WeightmapData.Textures[InCurrentWeightmapToProcessIndex];
				check(LayerWeightmap != nullptr);

				const ULandscapeWeightmapUsage* LayerWeightmapUsage = ComponentLayerData->WeightmapData.TextureUsages[InCurrentWeightmapToProcessIndex];
				check(LayerWeightmapUsage != nullptr);

				FIntPoint ComponentSectionBase = Component->GetSectionBase() - InLandscapeBase;
				FVector2D SourcePositionOffset(FMath::RoundToInt(ComponentSectionBase.X / LocalComponentSizeQuad), FMath::RoundToInt(ComponentSectionBase.Y / LocalComponentSizeQuad));
				FIntPoint SourceComponentVertexPosition = FIntPoint(SourcePositionOffset.X * LocalComponentSizeVerts, SourcePositionOffset.Y * LocalComponentSizeVerts);

				AddDeferredCopyLayersTexture(InOutputDebugName ? *LayerWeightmap->GetName() : GEmptyDebugName, LayerWeightmap->Resource, InOutputDebugName ? FString::Printf(TEXT("%s WeightmapScratchTexture"), *InLayer.Name.ToString()) : GEmptyDebugName, InOutTextureData, nullptr, SourceComponentVertexPosition, 0);
				PrintLayersDebugTextureResource(InOutputDebugName ? FString::Printf(TEXT("LS Weight: %s WeightmapScratchTexture %s"), *InLayer.Name.ToString(), TEXT("WeightmapScratchTextureResource")) : GEmptyDebugName, InOutTextureData, 0, false);

				for (const FWeightmapLayerAllocationInfo& WeightmapLayerAllocation : ComponentLayerData->WeightmapData.LayerAllocations)
				{
					if (WeightmapLayerAllocation.LayerInfo != nullptr && WeightmapLayerAllocation.WeightmapTextureIndex != 255 && ComponentLayerData->WeightmapData.Textures[WeightmapLayerAllocation.WeightmapTextureIndex] == LayerWeightmap)
					{
						FLandscapeLayerWeightmapExtractMaterialLayersComponentData Data;

						const ULandscapeComponent* DestComponent = LayerWeightmapUsage->ChannelUsage[WeightmapLayerAllocation.WeightmapTextureChannel];
						check(DestComponent);

						FIntPoint DestComponentSectionBase = DestComponent->GetSectionBase() - InLandscapeBase;

						// Compute component top left vertex position from section base info
						FVector2D DestPositionOffset(FMath::RoundToInt(DestComponentSectionBase.X / LocalComponentSizeQuad), FMath::RoundToInt(DestComponentSectionBase.Y / LocalComponentSizeQuad));

						Data.ComponentVertexPosition = SourceComponentVertexPosition;
						Data.AtlasTexturePositionOutput = FIntPoint(DestPositionOffset.X * LocalComponentSizeVerts, DestPositionOffset.Y * LocalComponentSizeVerts);
						Data.WeightmapChannelToProcess = WeightmapLayerAllocation.WeightmapTextureChannel;

						if (WeightmapLayerAllocation.LayerInfo == ALandscapeProxy::VisibilityLayer)
						{
							Data.DestinationPaintLayerIndex = 0;
							int32& NewLayerinfoObjectIndex = OutLayerInfoObjects.FindOrAdd(ALandscapeProxy::VisibilityLayer);
							NewLayerinfoObjectIndex = 0;
						}
						else
						{
							for (int32 LayerInfoSettingsIndex = 0; LayerInfoSettingsIndex < Info->Layers.Num(); ++LayerInfoSettingsIndex)
							{
								const FLandscapeInfoLayerSettings& InfoLayerSettings = Info->Layers[LayerInfoSettingsIndex];

								if (InfoLayerSettings.LayerInfoObj == WeightmapLayerAllocation.LayerInfo)
								{
									Data.DestinationPaintLayerIndex = LayerInfoSettingsIndex + 1; // due to visibility layer that is at 0
									int32& NewLayerinfoObjectIndex = OutLayerInfoObjects.FindOrAdd(WeightmapLayerAllocation.LayerInfo);
									NewLayerinfoObjectIndex = LayerInfoSettingsIndex + 1;

									break;
								}
							}
						}

						OutComponentData.Add(Data);
					}
				}
			}
		}
	}
	
	CommitDeferredCopyLayersTexture();
}

void ALandscape::PrepareComponentDataToPackMaterialLayersCS(int32 InCurrentWeightmapToProcessIndex, const FIntPoint& InLandscapeBase, bool InOutputDebugName, const TArray<ULandscapeComponent*>& InAllLandscapeComponents, TArray<UTexture2D*>& OutProcessedWeightmaps,
															TArray<FLandscapeLayersTexture2DCPUReadBackResource*>& OutProcessedCPUReadBackTexture, TArray<FLandscapeLayerWeightmapPackMaterialLayersComponentData>& OutComponentData)
{
	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return;
	}

	// Compute a mapping of all textures for the asked index and their usage
	TMap<UTexture2D*, ULandscapeWeightmapUsage*> WeightmapsToProcess;

	for (ULandscapeComponent* Component : InAllLandscapeComponents)
	{
		const TArray<UTexture2D*>& ComponentWeightmapTextures = Component->GetWeightmapTextures();
		const TArray<ULandscapeWeightmapUsage*>& ComponentWeightmapTexturesUsage = Component->GetWeightmapTexturesUsage();

		if (ComponentWeightmapTextures.IsValidIndex(InCurrentWeightmapToProcessIndex))
		{
			UTexture2D* ComponentWeightmapTexture = ComponentWeightmapTextures[InCurrentWeightmapToProcessIndex];
			ULandscapeWeightmapUsage* ComponentWeightmapTextureUsage = ComponentWeightmapTexturesUsage[InCurrentWeightmapToProcessIndex];
			check(ComponentWeightmapTextureUsage != nullptr);

			if (WeightmapsToProcess.Find(ComponentWeightmapTexture) == nullptr)
			{
				WeightmapsToProcess.Add(ComponentWeightmapTexture, ComponentWeightmapTextureUsage);
				OutProcessedWeightmaps.Add(ComponentWeightmapTexture);

				FLandscapeLayersTexture2DCPUReadBackResource** CPUReadback = Component->GetLandscapeProxy()->WeightmapsCPUReadBack.Find(ComponentWeightmapTexture);
				check(CPUReadback != nullptr);

				OutProcessedCPUReadBackTexture.Add(*CPUReadback);
			}
		}
	}

	TArray<const FWeightmapLayerAllocationInfo*> AlreadyProcessedAllocation;

	// Build for each texture what each channel should contains
	for (auto& ItPair : WeightmapsToProcess)
	{
		FLandscapeLayerWeightmapPackMaterialLayersComponentData Data;

		for (int32 WeightmapChannelIndex = 0; WeightmapChannelIndex < ULandscapeWeightmapUsage::NumChannels; ++WeightmapChannelIndex)
		{
			UTexture2D* ComponentWeightmapTexture = ItPair.Key;
			ULandscapeWeightmapUsage* ComponentWeightmapTextureUsage = ItPair.Value;

			// Clear out data to known values
			Data.ComponentVertexPositionX[WeightmapChannelIndex] = INDEX_NONE;
			Data.ComponentVertexPositionY[WeightmapChannelIndex] = INDEX_NONE;
			Data.SourcePaintLayerIndex[WeightmapChannelIndex] = INDEX_NONE;
			Data.WeightmapChannelToProcess[WeightmapChannelIndex] = INDEX_NONE;

			if (ComponentWeightmapTextureUsage->ChannelUsage[WeightmapChannelIndex] != nullptr)
			{
				const ULandscapeComponent* ChannelComponent = ComponentWeightmapTextureUsage->ChannelUsage[WeightmapChannelIndex];

				const TArray<FWeightmapLayerAllocationInfo>& ChannelLayerAllocations = ChannelComponent->GetWeightmapLayerAllocations();
				const TArray<UTexture2D*>& ChannelComponentWeightmapTextures = ChannelComponent->GetWeightmapTextures();

				for (const FWeightmapLayerAllocationInfo& ChannelLayerAllocation : ChannelLayerAllocations)
				{
					if (ChannelLayerAllocation.LayerInfo != nullptr && !AlreadyProcessedAllocation.Contains(&ChannelLayerAllocation) && ChannelComponentWeightmapTextures[ChannelLayerAllocation.WeightmapTextureIndex] == ComponentWeightmapTexture)
					{
						FIntPoint ComponentSectionBase = ChannelComponent->GetSectionBase() - InLandscapeBase;

						// Compute component top left vertex position from section base info
						int32 LocalComponentSizeQuad = ChannelComponent->SubsectionSizeQuads * NumSubsections;
						int32 LocalComponentSizeVerts = (ChannelComponent->SubsectionSizeQuads + 1) * NumSubsections;
						FVector2D PositionOffset(FMath::RoundToInt(ComponentSectionBase.X / LocalComponentSizeQuad), FMath::RoundToInt(ComponentSectionBase.Y / LocalComponentSizeQuad));

						Data.ComponentVertexPositionX[WeightmapChannelIndex] = PositionOffset.X * LocalComponentSizeVerts;
						Data.ComponentVertexPositionY[WeightmapChannelIndex] = PositionOffset.Y * LocalComponentSizeVerts;

						Data.WeightmapChannelToProcess[WeightmapChannelIndex] = ChannelLayerAllocation.WeightmapTextureChannel;

						AlreadyProcessedAllocation.Add(&ChannelLayerAllocation);

						if (ChannelLayerAllocation.LayerInfo == ALandscapeProxy::VisibilityLayer)
						{
							Data.SourcePaintLayerIndex[WeightmapChannelIndex] = 0; // Always store after the last weightmap index
						}
						else
						{
							for (int32 LayerInfoSettingsIndex = 0; LayerInfoSettingsIndex < Info->Layers.Num(); ++LayerInfoSettingsIndex)
							{
								const FLandscapeInfoLayerSettings& LayerInfo = Info->Layers[LayerInfoSettingsIndex];

								if (ChannelLayerAllocation.LayerInfo == LayerInfo.LayerInfoObj)
								{
									Data.SourcePaintLayerIndex[WeightmapChannelIndex] = LayerInfoSettingsIndex + 1; // due to visibility layer that is at 0
									break;
								}
							}
						}

						break;
					}
				}
			}
		}

		OutComponentData.Add(Data);
	}
}

void ALandscape::ReallocateLayersWeightmaps(const TArray<ULandscapeLayerInfoObject*>& InBrushRequiredAllocations)
{
	SCOPE_CYCLE_COUNTER(STAT_LandscapeLayersReallocateWeightmaps);

	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return;
	}

	TArray<ULandscapeComponent*> AllLandscapeComponents;

	Info->ForAllLandscapeProxies([&AllLandscapeComponents](ALandscapeProxy* Proxy)
	{
		AllLandscapeComponents.Append(Proxy->LandscapeComponents);
	});
		
	// Clear allocation data
	for (ULandscapeComponent* Component : AllLandscapeComponents)
	{
		TArray<FWeightmapLayerAllocationInfo>& BaseLayerAllocations = Component->GetWeightmapLayerAllocations();

		for (FWeightmapLayerAllocationInfo& BaseWeightmapAllocation : BaseLayerAllocations)
		{
			BaseWeightmapAllocation.WeightmapTextureChannel = 255;
			BaseWeightmapAllocation.WeightmapTextureIndex = 255;
		}

		TArray<ULandscapeWeightmapUsage*>& WeightmapTexturesUsage = Component->GetWeightmapTexturesUsage();

		for (int32 i = 0; i < WeightmapTexturesUsage.Num(); ++i)
		{
			ULandscapeWeightmapUsage* Usage = WeightmapTexturesUsage[i];
			check(Usage != nullptr);

			Usage->ClearUsage();
		}
	}
		
	// Build a map of all the allocation per components
	TMap<ULandscapeComponent*, TArray<ULandscapeLayerInfoObject*>> LayerAllocsPerComponent;

	Info->ForAllLandscapeProxies([&](ALandscapeProxy* Proxy)
	{
		for (FLandscapeLayer& Layer : LandscapeLayers)
		{
			for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
			{
				TArray<ULandscapeLayerInfoObject*>* ComponentLayerAlloc = LayerAllocsPerComponent.Find(Component);

				if (ComponentLayerAlloc == nullptr)
				{
					TArray<ULandscapeLayerInfoObject*> NewLayerAllocs;
					ComponentLayerAlloc = &LayerAllocsPerComponent.Add(Component, NewLayerAllocs);
				}

				FLandscapeLayerComponentData* LayerComponentData = Component->GetLayerData(Layer.Guid);

				for (const FWeightmapLayerAllocationInfo& LayerWeightmapAllocation : LayerComponentData->WeightmapData.LayerAllocations)
				{
					if (LayerWeightmapAllocation.LayerInfo != nullptr)
					{
						ComponentLayerAlloc->AddUnique(LayerWeightmapAllocation.LayerInfo);
					}
				}

				// Add the brush alloc also
				for (ULandscapeLayerInfoObject* BrushLayerInfo : InBrushRequiredAllocations)
				{
					if (BrushLayerInfo != nullptr)
					{
						ComponentLayerAlloc->AddUnique(BrushLayerInfo);
					}
				}
			}
		}
	});

	// Determine if the Final layer need to add/remove some alloc
	for (auto& ItPair : LayerAllocsPerComponent)
	{
		ULandscapeComponent* Component = ItPair.Key;
		TArray<ULandscapeLayerInfoObject*>& ComponentLayerAlloc = ItPair.Value;
		TArray<FWeightmapLayerAllocationInfo>& ComponentBaseLayerAlloc = Component->GetWeightmapLayerAllocations();

		// Deal with the one that need removal
		for (int32 i = ComponentBaseLayerAlloc.Num() - 1; i >= 0; --i)
		{
			FWeightmapLayerAllocationInfo& Alloc = ComponentBaseLayerAlloc[i];

			if (!ComponentLayerAlloc.Contains(Alloc.LayerInfo))
			{
				ComponentBaseLayerAlloc.RemoveAt(i);
			}
		}

		// Then add the new one
		for (ULandscapeLayerInfoObject* LayerAlloc : ComponentLayerAlloc)
		{
			const bool AllocExist = ComponentBaseLayerAlloc.ContainsByPredicate([&LayerAlloc](FWeightmapLayerAllocationInfo& BaseLayerAlloc) { return (LayerAlloc == BaseLayerAlloc.LayerInfo); });

			if (!AllocExist)
			{
				ComponentBaseLayerAlloc.Add(FWeightmapLayerAllocationInfo(LayerAlloc));
			}
		}
	}

	// Realloc the weightmap so it will create proper texture (if needed) and will set the allocations information
	TArray<UTexture2D*> NewCreatedTextures;

	for (ULandscapeComponent* Component : AllLandscapeComponents)
	{
		Component->ReallocateWeightmaps(nullptr, false, false, true, false, nullptr, &NewCreatedTextures);
	}

	// TODO: correctly only recreate what is required instead of everything..
	//GDisableAutomaticTextureMaterialUpdateDependencies = true;

	for (UTexture2D* Texture : NewCreatedTextures)
	{
		Texture->FinishCachePlatformData();
		Texture->PostEditChange();

		Texture->WaitForStreaming();
	}

	//GDisableAutomaticTextureMaterialUpdateDependencies = false;

	// Clean-up unused weightmap CPUReadback resources
	Info->ForAllLandscapeProxies([&AllLandscapeComponents](ALandscapeProxy* Proxy)
	{
		TArray<UTexture2D*, TInlineAllocator<64>> EntriesToRemoveFromMap;
		for (auto& Pair : Proxy->WeightmapsCPUReadBack)
		{
			UTexture2D* WeightmapTextureKey = Pair.Key;
			bool IsTextureReferenced = false;
			for (ULandscapeComponent* Component : AllLandscapeComponents)
			{
				for (UTexture2D* WeightmapTexture : Component->GetWeightmapTextures(false))
				{
					if (WeightmapTexture == WeightmapTextureKey)
					{
						IsTextureReferenced = true;
						break;
					}
				}
			}
			if (!IsTextureReferenced)
			{
				EntriesToRemoveFromMap.Add(WeightmapTextureKey);
				if (FLandscapeLayersTexture2DCPUReadBackResource* ResourceToDelete = Pair.Value)
				{
					BeginReleaseResource(ResourceToDelete);
				}
			}
		}

		if (EntriesToRemoveFromMap.Num())
		{
			FlushRenderingCommands();
			for (UTexture2D* OldWeightmapTexture : EntriesToRemoveFromMap)
			{
				if (FLandscapeLayersTexture2DCPUReadBackResource** ResourceToDelete = Proxy->WeightmapsCPUReadBack.Find(OldWeightmapTexture))
				{
					check(*ResourceToDelete);
					delete *ResourceToDelete;
					Proxy->WeightmapsCPUReadBack.Remove(OldWeightmapTexture);
				}
			}
		}
	});
}

void ALandscape::InitializeLayersWeightmapResources()
{
	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr || Info->Layers.Num() == 0)
	{
		return;
	}

	// Destroy existing resource
	TArray<FTextureResource*> ResourceToDestroy;
	ResourceToDestroy.Add(CombinedLayersWeightmapAllMaterialLayersResource);
	ResourceToDestroy.Add(CurrentLayersWeightmapAllMaterialLayersResource);
	ResourceToDestroy.Add(WeightmapScratchExtractLayerTextureResource);
	ResourceToDestroy.Add(WeightmapScratchPackLayerTextureResource);

	for (FTextureResource* Resource : ResourceToDestroy)
	{
		if (Resource != nullptr)
		{
			ENQUEUE_RENDER_COMMAND(ReleaseCommand)(
				[Resource](FRHICommandList& RHICmdList)
			{
				Resource->ReleaseResource();
				delete Resource;
			});
		}
	}

	// Create resources

	int32 LayerCount = Info->Layers.Num() + 1; // due to visibility being stored at 0

	// Use the 1st one to compute the resource as they are all the same anyway
	UTextureRenderTarget2D* FirstWeightmapRT = WeightmapRTList[WeightmapRT_Scratch1];

	CombinedLayersWeightmapAllMaterialLayersResource = new FLandscapeTexture2DArrayResource(FirstWeightmapRT->SizeX, FirstWeightmapRT->SizeY, LayerCount, PF_G8, 1, true);
	BeginInitResource(CombinedLayersWeightmapAllMaterialLayersResource);

	CurrentLayersWeightmapAllMaterialLayersResource = new FLandscapeTexture2DArrayResource(FirstWeightmapRT->SizeX, FirstWeightmapRT->SizeY, LayerCount, PF_G8, 1, true);
	BeginInitResource(CurrentLayersWeightmapAllMaterialLayersResource);

	WeightmapScratchExtractLayerTextureResource = new FLandscapeTexture2DResource(FirstWeightmapRT->SizeX, FirstWeightmapRT->SizeY, PF_B8G8R8A8, 1, false);
	BeginInitResource(WeightmapScratchExtractLayerTextureResource);

	int32 MipCount = 0;

	for (int32 MipRTIndex = EWeightmapRTType::WeightmapRT_Mip0; MipRTIndex < EWeightmapRTType::WeightmapRT_Count; ++MipRTIndex)
	{
		if (WeightmapRTList[MipRTIndex] != nullptr)
		{
			++MipCount;
		}
	}

	WeightmapScratchPackLayerTextureResource = new FLandscapeTexture2DResource(FirstWeightmapRT->SizeX, FirstWeightmapRT->SizeY, PF_R32_UINT, MipCount, true);
	BeginInitResource(WeightmapScratchPackLayerTextureResource);
}

bool ALandscape::PrepareLayersWeightmapTextureResources(bool bInWaitForStreaming) const
{
	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return false;
	}

	bool IsReady = true;

	Info->ForAllLandscapeProxies([&](ALandscapeProxy* Proxy)
	{
		FLandscapeIsTextureFullyStreamedIn IsTextureFullyStreamedIn;
		for (const FLandscapeLayer& Layer : LandscapeLayers)
		{
			for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
			{
				for (UTexture2D* ComponentWeightmap : Component->GetWeightmapTextures())
				{
					if (!IsTextureFullyStreamedIn(ComponentWeightmap, bInWaitForStreaming))
					{
						IsReady = false;
						break;
					}
				}

				FLandscapeLayerComponentData* ComponentLayerData = Component->GetLayerData(Layer.Guid);

				IsReady &= ComponentLayerData != nullptr;

				if (ComponentLayerData != nullptr)
				{
					for (UTexture2D* LayerWeightmap : ComponentLayerData->WeightmapData.Textures)
					{
						if ((LayerWeightmap->IsAsyncCacheComplete() || bInWaitForStreaming) && LayerWeightmap->Resource == nullptr)
						{
							LayerWeightmap->FinishCachePlatformData();

							LayerWeightmap->Resource = LayerWeightmap->CreateResource();

							if (LayerWeightmap->Resource != nullptr)
							{
								BeginInitResource(LayerWeightmap->Resource);
							}
						}

						IsReady &= IsTextureFullyStreamedIn(LayerWeightmap, bInWaitForStreaming);
						IsReady &= bInWaitForStreaming || (LayerWeightmap->Resource != nullptr && LayerWeightmap->Resource->IsInitialized());
					}
				}
			}
		}
	});

	if (bInWaitForStreaming)
	{
		FlushRenderingCommands();
	}

	return IsReady;
}

int32 ALandscape::RegenerateLayersWeightmaps(const TArray<ULandscapeComponent*>& InLandscapeComponents, bool bInWaitForStreaming)
{
	SCOPE_CYCLE_COUNTER(STAT_LandscapeLayersRegenerateWeightmaps);
	const int32 AllWeightmapUpdateModes = (ELandscapeLayerUpdateMode::Update_Weightmap_All | ELandscapeLayerUpdateMode::Update_Weightmap_Editing | ELandscapeLayerUpdateMode::Update_Weightmap_Editing_NoCollision);
	const int32 WeightmapUpdateModes = LayerContentUpdateModes & AllWeightmapUpdateModes;
	const bool bForceRender = CVarOutputLayersDebugDrawCallName.GetValueOnAnyThread() == 1;
	
	ULandscapeInfo* Info = GetLandscapeInfo();

	if ((WeightmapUpdateModes == 0 && !bForceRender) || Info == nullptr || Info->Layers.Num() == 0 || !PrepareLayersWeightmapTextureResources(bInWaitForStreaming))
	{
		return 0;
	}
			
	TArray<ULandscapeComponent*> ComponentThatNeedMaterialRebuild;
	TArray<ULandscapeLayerInfoObject*> BrushRequiredAllocations;
	int32 LayerCount = Info->Layers.Num() + 1; // due to visibility being stored at 0
	bool ClearFlagsAfterUpdate = true;

	if (WeightmapUpdateModes || bForceRender)
	{
		FIntRect LandscapeExtent;
		if (!Info->GetLandscapeExtent(LandscapeExtent.Min.X, LandscapeExtent.Min.Y, LandscapeExtent.Max.X, LandscapeExtent.Max.Y))
		{
			return 0;
		}
		
		check(WeightmapRTList.Num() > 0);

		UTextureRenderTarget2D* LandscapeScratchRT1 = WeightmapRTList[EWeightmapRTType::WeightmapRT_Scratch1];
		UTextureRenderTarget2D* LandscapeScratchRT2 = WeightmapRTList[EWeightmapRTType::WeightmapRT_Scratch2];
		UTextureRenderTarget2D* LandscapeScratchRT3 = WeightmapRTList[EWeightmapRTType::WeightmapRT_Scratch3];
		UTextureRenderTarget2D* EmptyRT = WeightmapRTList[EWeightmapRTType::WeightmapRT_Scratch_RGBA];
		FLandscapeLayersWeightmapShaderParameters PSShaderParams;
		bool OutputDebugName = (CVarOutputLayersDebugDrawCallName.GetValueOnAnyThread() == 1 || CVarOutputLayersRTContent.GetValueOnAnyThread() == 1 || CVarOutputLayersWeightmapsRTContent.GetValueOnAnyThread() == 1) ? true : false;
		FString SourceDebugName = GEmptyDebugName;
		FString DestDebugName = GEmptyDebugName;
		ClearLayersWeightmapTextureResource(TEXT("ClearRT RGBA"), EmptyRT->GameThread_GetRenderTargetResource());
		ClearLayersWeightmapTextureResource(TEXT("ClearRT R"), LandscapeScratchRT1->GameThread_GetRenderTargetResource());

		for (int32 LayerIndex = 0; LayerIndex < LayerCount; ++LayerIndex)
		{
			SourceDebugName = OutputDebugName ? LandscapeScratchRT1->GetName() : GEmptyDebugName;
			DestDebugName = OutputDebugName ? FString::Printf(TEXT("Weight: Clear CombinedProcLayerWeightmapAllLayersResource %d, "), LayerIndex) : GEmptyDebugName;

			AddDeferredCopyLayersTexture(SourceDebugName, LandscapeScratchRT1->GameThread_GetRenderTargetResource(), DestDebugName, CombinedLayersWeightmapAllMaterialLayersResource, nullptr, FIntPoint(0, 0), 0, 0, 0, LayerIndex);
		}

		CommitDeferredCopyLayersTexture();

		bool bHasWeightmapData = false;
		bool bFirstLayer = true;
		TMap<ULandscapeLayerInfoObject*, bool> WeightmapLayersBlendSubstractive;

		for (FLandscapeLayer& Layer : LandscapeLayers)
		{
			int8 CurrentWeightmapToProcessIndex = 0;
			bool HasFoundWeightmapToProcess = true; // try processing at least once

			TMap<ULandscapeLayerInfoObject*, int32> LayerInfoObjects; // <LayerInfoObj, LayerIndex>

			// Determine if some brush want to write to layer that we have currently no data on
			if (Layer.bVisible)
			{
				for (int32 LayerInfoSettingsIndex = 0; LayerInfoSettingsIndex < Info->Layers.Num(); ++LayerInfoSettingsIndex)
				{
					const FLandscapeInfoLayerSettings& InfoLayerSettings = Info->Layers[LayerInfoSettingsIndex];

					for (int32 i = 0; i < Layer.Brushes.Num(); ++i)
					{
						FLandscapeLayerBrush& Brush = Layer.Brushes[i];
						if (Brush.IsAffectingWeightmapLayer(InfoLayerSettings.GetLayerName()) && !LayerInfoObjects.Contains(InfoLayerSettings.LayerInfoObj))
						{
							LayerInfoObjects.Add(InfoLayerSettings.LayerInfoObj, LayerInfoSettingsIndex + 1); // due to visibility layer that is at 0
							bHasWeightmapData = true;
						}
					}
				}
			}

			// Loop until there is no more weightmap texture to process
			while (HasFoundWeightmapToProcess)
			{
				SourceDebugName = OutputDebugName ? EmptyRT->GetName() : GEmptyDebugName;
				DestDebugName = OutputDebugName ? FString::Printf(TEXT("Weight: %s Clear WeightmapScratchExtractLayerTextureResource"), *Layer.Name.ToString()) : GEmptyDebugName;

				CopyLayersTexture(SourceDebugName, EmptyRT->GameThread_GetRenderTargetResource(), DestDebugName, WeightmapScratchExtractLayerTextureResource);

				// Prepare compute shader data
				TArray<FLandscapeLayerWeightmapExtractMaterialLayersComponentData> ComponentsData;
				PrepareComponentDataToExtractMaterialLayersCS(InLandscapeComponents, Layer, CurrentWeightmapToProcessIndex, LandscapeExtent.Min, OutputDebugName, WeightmapScratchExtractLayerTextureResource, ComponentsData, LayerInfoObjects);

				HasFoundWeightmapToProcess = ComponentsData.Num() > 0;

				// Clear the current atlas if required
				if (CurrentWeightmapToProcessIndex == 0)
				{
					ClearLayersWeightmapTextureResource(TEXT("ClearRT"), LandscapeScratchRT1->GameThread_GetRenderTargetResource());

					// Important: for performance reason we only clear the layer we will write to, the other one might contain data but they will not be read during the blend phase
					for (auto& ItPair : LayerInfoObjects)
					{
						int32 LayerIndex = ItPair.Value;

						SourceDebugName = OutputDebugName ? LandscapeScratchRT1->GetName() : GEmptyDebugName;
						DestDebugName = OutputDebugName ? FString::Printf(TEXT("Weight: %s Clear CurrentProcLayerWeightmapAllLayersResource %d, "), *Layer.Name.ToString(), LayerIndex) : GEmptyDebugName;

						AddDeferredCopyLayersTexture(SourceDebugName, LandscapeScratchRT1->GameThread_GetRenderTargetResource(), DestDebugName, CurrentLayersWeightmapAllMaterialLayersResource, nullptr, FIntPoint(0, 0), 0, 0, 0, LayerIndex);
					}

					CommitDeferredCopyLayersTexture();
				}

				// Perform the compute shader
				if (ComponentsData.Num() > 0)
				{
					PrintLayersDebugTextureResource(OutputDebugName ? FString::Printf(TEXT("LS Weight: %s WeightmapScratchTexture %s"), *Layer.Name.ToString(), TEXT("WeightmapScratchTextureResource")) : GEmptyDebugName, WeightmapScratchExtractLayerTextureResource, 0, false);
										
					FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderParameters CSExtractLayersShaderParams;
					CSExtractLayersShaderParams.AtlasWeightmapsPerLayer = CurrentLayersWeightmapAllMaterialLayersResource;
					CSExtractLayersShaderParams.ComponentWeightmapResource = WeightmapScratchExtractLayerTextureResource;
					CSExtractLayersShaderParams.ComputeShaderResource = new FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderResource(ComponentsData);
					CSExtractLayersShaderParams.ComponentSize = (SubsectionSizeQuads + 1) * NumSubsections;

					BeginInitResource(CSExtractLayersShaderParams.ComputeShaderResource);

					FLandscapeLayerWeightmapExtractMaterialLayersCSDispatch_RenderThread CSDispatch(CSExtractLayersShaderParams);

					ENQUEUE_RENDER_COMMAND(FLandscapeLayersExtractMaterialLayersCSCommand)(
						[CSDispatch](FRHICommandListImmediate& RHICmdList) mutable
					{
						CSDispatch.ExtractLayers(RHICmdList);
					});

					++CurrentWeightmapToProcessIndex;
					bHasWeightmapData = true; // at least 1 CS was executed, so we can continue the processing
				}
			}

			// If we have data in at least one weight map layer
			if (LayerInfoObjects.Num() > 0)
			{
				for (auto& LayerInfoObject : LayerInfoObjects)
				{
					int32 LayerIndex = LayerInfoObject.Value;
					ULandscapeLayerInfoObject* LayerInfoObj = LayerInfoObject.Key;

					// Copy the layer we are working on
					SourceDebugName = OutputDebugName ? FString::Printf(TEXT("Weight: %s PaintLayer: %s, CurrentProcLayerWeightmapAllLayersResource"), *Layer.Name.ToString(), *LayerInfoObj->LayerName.ToString()) : GEmptyDebugName;
					DestDebugName = OutputDebugName ? LandscapeScratchRT1->GetName() : GEmptyDebugName;

					CopyLayersTexture(SourceDebugName, CurrentLayersWeightmapAllMaterialLayersResource, DestDebugName, LandscapeScratchRT1->GameThread_GetRenderTargetResource(), nullptr, FIntPoint(0, 0), 0, 0, LayerIndex, 0);
					PrintLayersDebugRT(OutputDebugName ? FString::Printf(TEXT("LS Weight: %s CurrentProcLayerWeightmapAllLayersResource -> Paint Layer RT %s"), *Layer.Name.ToString(), *LandscapeScratchRT1->GetName()) : GEmptyDebugName, LandscapeScratchRT1, 0, false);

					PSShaderParams.ApplyLayerModifiers = true;
					PSShaderParams.LayerVisible = Layer.bVisible;
					PSShaderParams.LayerAlpha = LayerInfoObj == ALandscapeProxy::VisibilityLayer ? 1.0f : Layer.WeightmapAlpha; // visibility can't be affected by weight

					DrawWeightmapComponentsToRenderTarget(OutputDebugName ? FString::Printf(TEXT("LS Weight: %s Paint: %s += -> %s"), *Layer.Name.ToString(), *LayerInfoObj->LayerName.ToString(), *LandscapeScratchRT1->GetName(), *LandscapeScratchRT2->GetName()) : GEmptyDebugName,
														  InLandscapeComponents, LandscapeExtent.Min, LandscapeScratchRT1, nullptr, LandscapeScratchRT2, ERTDrawingType::RTAtlas, true, PSShaderParams, 0);

					PSShaderParams.ApplyLayerModifiers = false;

					// Combined Layer data with current stack
					SourceDebugName = OutputDebugName ? FString::Printf(TEXT("Weight: %s PaintLayer: %s CombinedProcLayerWeightmap"), *Layer.Name.ToString(), *LayerInfoObj->LayerName.ToString()) : GEmptyDebugName;
					DestDebugName = OutputDebugName ? LandscapeScratchRT1->GetName() : GEmptyDebugName;

					CopyLayersTexture(SourceDebugName, CombinedLayersWeightmapAllMaterialLayersResource, DestDebugName, LandscapeScratchRT1->GameThread_GetRenderTargetResource(), nullptr, FIntPoint(0, 0), 0, 0, LayerIndex, 0);
					PrintLayersDebugRT(OutputDebugName ? FString::Printf(TEXT("LS Weight: %s CombinedProcLayerWeightmap -> Paint Layer RT %s"), *Layer.Name.ToString(), *LandscapeScratchRT1->GetName()) : GEmptyDebugName, LandscapeScratchRT1, 0, false);

					// Combine with current status and copy back to the combined 2d resource array
					PSShaderParams.OutputAsSubstractive = false;

					if (!bFirstLayer)
					{
						const bool* BlendSubstractive = Layer.WeightmapLayerAllocationBlend.Find(LayerInfoObj);
						PSShaderParams.OutputAsSubstractive = BlendSubstractive != nullptr ? *BlendSubstractive : false;

						if (PSShaderParams.OutputAsSubstractive)
						{
							bool& IsSubstractiveBlend = WeightmapLayersBlendSubstractive.FindOrAdd(LayerInfoObj);
							IsSubstractiveBlend = true;
						}
					}

					DrawWeightmapComponentsToRenderTarget(OutputDebugName ? FString::Printf(TEXT("LS Weight: %s PaintLayer: %s, %s += -> Combined %s"), *Layer.Name.ToString(), *LayerInfoObj->LayerName.ToString(), *LandscapeScratchRT2->GetName(), *LandscapeScratchRT3->GetName()) : GEmptyDebugName,
														  InLandscapeComponents, LandscapeExtent.Min, LandscapeScratchRT2, bFirstLayer ? nullptr : LandscapeScratchRT1, LandscapeScratchRT3, ERTDrawingType::RTAtlasToNonAtlas, true, PSShaderParams, 0);

					PSShaderParams.OutputAsSubstractive = false;

					SourceDebugName = OutputDebugName ? FString::Printf(TEXT("Weight: %s PaintLayer: %s %s"), *Layer.Name.ToString(), *LayerInfoObj->LayerName.ToString(), *LandscapeScratchRT3->GetName()) : GEmptyDebugName;
					DestDebugName = OutputDebugName ? TEXT("CombinedProcLayerWeightmap") : GEmptyDebugName;

					// Handle brush blending
					if (Layer.bVisible)
					{
						// Draw each brushes				
						for (int32 i = 0; i < Layer.Brushes.Num(); ++i)
						{
							// TODO: handle conversion/handling of RT not same size as internal size

							FLandscapeLayerBrush& Brush = Layer.Brushes[i];
							UTextureRenderTarget2D* BrushOutputRT = Brush.Render(false, LandscapeExtent, LandscapeScratchRT3, LayerInfoObj->LayerName);
							if (BrushOutputRT == nullptr || BrushOutputRT->SizeX != LandscapeScratchRT3->SizeX || BrushOutputRT->SizeY != LandscapeScratchRT3->SizeY)
							{
								continue;
							}

							BrushRequiredAllocations.AddUnique(LayerInfoObj);

							INC_DWORD_STAT(STAT_LandscapeLayersRegenerateDrawCalls); // Brush RenderInitialize

							PrintLayersDebugRT(OutputDebugName ? FString::Printf(TEXT("LS Weight: %s %s -> Brush %s"), *Layer.Name.ToString(), *Brush.GetBrush()->GetName(), *BrushOutputRT->GetName()) : GEmptyDebugName, BrushOutputRT);

							SourceDebugName = OutputDebugName ? FString::Printf(TEXT("Weight: %s PaintLayer: %s Brush: %s"), *Layer.Name.ToString(), *LayerInfoObj->LayerName.ToString(), *BrushOutputRT->GetName()) : GEmptyDebugName;
							DestDebugName = OutputDebugName ? LandscapeScratchRT3->GetName() : GEmptyDebugName;

							CopyLayersTexture(SourceDebugName, BrushOutputRT->GameThread_GetRenderTargetResource(), DestDebugName, LandscapeScratchRT3->GameThread_GetRenderTargetResource());
							PrintLayersDebugRT(OutputDebugName ? FString::Printf(TEXT("LS Weight: %s Component %s += -> Combined %s"), *Layer.Name.ToString(), *BrushOutputRT->GetName(), *LandscapeScratchRT3->GetName()) : GEmptyDebugName, LandscapeScratchRT3);
						}

						PrintLayersDebugRT(OutputDebugName ? FString::Printf(TEXT("LS Weight: %s CombinedPostBrushProcLayerWeightmap -> Paint Layer RT %s"), *Layer.Name.ToString(), *LandscapeScratchRT3->GetName()) : GEmptyDebugName, LandscapeScratchRT3, 0, false);

						SourceDebugName = OutputDebugName ? FString::Printf(TEXT("Weight: %s PaintLayer: %s %s"), *Layer.Name.ToString(), *LayerInfoObj->LayerName.ToString(), *LandscapeScratchRT3->GetName()) : GEmptyDebugName;
						DestDebugName = OutputDebugName ? TEXT("CombinedProcLayerWeightmap") : GEmptyDebugName;
						CopyLayersTexture(SourceDebugName, LandscapeScratchRT3->GameThread_GetRenderTargetResource(), DestDebugName, CombinedLayersWeightmapAllMaterialLayersResource, nullptr, FIntPoint(0, 0), 0, 0, 0, LayerIndex);
					}

					DrawWeightmapComponentsToRenderTarget(OutputDebugName ? FString::Printf(TEXT("LS Weight: %s Combined Scratch No Border to %s Combined Scratch with Border"), *LandscapeScratchRT3->GetName(), *LandscapeScratchRT1->GetName()) : GEmptyDebugName,
						InLandscapeComponents, LandscapeExtent.Min, LandscapeScratchRT3, nullptr, LandscapeScratchRT1, ERTDrawingType::RTNonAtlasToAtlas, true, PSShaderParams, 0);


					CopyLayersTexture(SourceDebugName, LandscapeScratchRT1->GameThread_GetRenderTargetResource(), DestDebugName, CombinedLayersWeightmapAllMaterialLayersResource, nullptr, FIntPoint(0, 0), 0, 0, 0, LayerIndex);
				}

				PSShaderParams.ApplyLayerModifiers = false;
			}

			bFirstLayer = false;
		}

		ReallocateLayersWeightmaps(BrushRequiredAllocations);

		if (bHasWeightmapData)
		{
			// Will generate CPU read back resource, if required
			bool bHasPendingInitResource = false;
			Info->ForAllLandscapeProxies([&](ALandscapeProxy* Proxy)
			{
				for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
				{
					const TArray<UTexture2D*>& ComponentWeightmapTextures = Component->GetWeightmapTextures();

					for (UTexture2D* WeightmapTexture : ComponentWeightmapTextures)
					{
						FLandscapeLayersTexture2DCPUReadBackResource** CPUReadback = Component->GetLandscapeProxy()->WeightmapsCPUReadBack.Find(WeightmapTexture);

						if (CPUReadback == nullptr)
						{
							FLandscapeLayersTexture2DCPUReadBackResource* NewWeightmapCPUReadBack = new FLandscapeLayersTexture2DCPUReadBackResource(WeightmapTexture->Source.GetSizeX(), WeightmapTexture->Source.GetSizeY(), WeightmapTexture->GetPixelFormat(), WeightmapTexture->Source.GetNumMips());
							BeginInitResource(NewWeightmapCPUReadBack);
							Component->GetLandscapeProxy()->WeightmapsCPUReadBack.Add(WeightmapTexture, NewWeightmapCPUReadBack);
							bHasPendingInitResource = true;
						}
					}
				}
			});

			if (bHasPendingInitResource)
			{
				// Flush because TickLayers can access CPU Readbacks in the same frame (see ResolveLayersTexture)
				FlushRenderingCommands();
			}

			int8 CurrentWeightmapToProcessIndex = 0;
			bool HasFoundWeightmapToProcess = true; // try processing at least once

			TArray<float> WeightmapLayerWeightBlend;
			TArray<UTexture2D*> ProcessedWeightmaps;
			TArray<FLandscapeLayersTexture2DCPUReadBackResource*> ProcessedCPUReadbackTextures;
			int32 NextTextureIndexToProcess = 0;

			// Generate the component data from the weightmap allocation that were done earlier and weight blend them if required (i.e renormalize)
			while (HasFoundWeightmapToProcess)
			{
				TArray<FLandscapeLayerWeightmapPackMaterialLayersComponentData> PackLayersComponentsData;
				PrepareComponentDataToPackMaterialLayersCS(CurrentWeightmapToProcessIndex, LandscapeExtent.Min, OutputDebugName, InLandscapeComponents, ProcessedWeightmaps, ProcessedCPUReadbackTextures, PackLayersComponentsData);
				HasFoundWeightmapToProcess = PackLayersComponentsData.Num() > 0;

				// Perform the compute shader
				if (PackLayersComponentsData.Num() > 0)
				{
					// Compute the weightblend mode of each layer for the compute shader
					if (WeightmapLayerWeightBlend.Num() != LayerCount)
					{
						WeightmapLayerWeightBlend.SetNum(LayerCount);

						for (int32 LayerInfoSettingsIndex = 0; LayerInfoSettingsIndex < Info->Layers.Num(); ++LayerInfoSettingsIndex)
						{
							const FLandscapeInfoLayerSettings& LayerInfo = Info->Layers[LayerInfoSettingsIndex];
							WeightmapLayerWeightBlend[LayerInfoSettingsIndex + 1] = LayerInfo.LayerInfoObj != nullptr ? (LayerInfo.LayerInfoObj->bNoWeightBlend ? 0.0f : 1.0f) : 1.0f;
						}

						WeightmapLayerWeightBlend[0] = 0.0f; // Blend of Visibility 
					}

					TArray<FVector2D> WeightmapTextureOutputOffset;

					// Compute each weightmap location so compute shader will be able to output at expected location
					int32 ComponentSize = (SubsectionSizeQuads + 1) * NumSubsections;
					float ComponentY = 0;
					float ComponentX = 0;

					for (int32 i = 0; i < PackLayersComponentsData.Num(); ++i)
					{
						check(ComponentY+ComponentSize <= WeightmapScratchPackLayerTextureResource->GetSizeY()); // This should never happen as it would be a bug in the algo

						if (ComponentX+ComponentSize > WeightmapScratchPackLayerTextureResource->GetSizeX())
						{
							ComponentY += ComponentSize;
							ComponentX = 0;
						}

						WeightmapTextureOutputOffset.Add(FVector2D(ComponentX, ComponentY));
						ComponentX += ComponentSize;
					}

					// Clear Pack texture
					SourceDebugName = OutputDebugName ? *EmptyRT->GetName() : GEmptyDebugName;
					DestDebugName = OutputDebugName ? TEXT("Weight: Clear WeightmapScratchPackLayerTextureResource") : GEmptyDebugName;

					CopyLayersTexture(SourceDebugName, EmptyRT->GameThread_GetRenderTargetResource(), DestDebugName, WeightmapScratchPackLayerTextureResource);

					FLandscapeLayerWeightmapPackMaterialLayersComputeShaderParameters CSPackLayersShaderParams;
					CSPackLayersShaderParams.AtlasWeightmapsPerLayer = CombinedLayersWeightmapAllMaterialLayersResource;
					CSPackLayersShaderParams.ComponentWeightmapResource = WeightmapScratchPackLayerTextureResource;
					CSPackLayersShaderParams.ComputeShaderResource = new FLandscapeLayerWeightmapPackMaterialLayersComputeShaderResource(PackLayersComponentsData, WeightmapLayerWeightBlend, WeightmapTextureOutputOffset);
					CSPackLayersShaderParams.ComponentSize = ComponentSize;
					BeginInitResource(CSPackLayersShaderParams.ComputeShaderResource);

					FLandscapeLayerWeightmapPackMaterialLayersCSDispatch_RenderThread CSDispatch(CSPackLayersShaderParams);

					ENQUEUE_RENDER_COMMAND(FLandscapeLayersPackMaterialLayersCSCommand)(
						[CSDispatch](FRHICommandListImmediate& RHICmdList) mutable
					{
						CSDispatch.PackLayers(RHICmdList);
					});

					UTextureRenderTarget2D* CurrentRT = WeightmapRTList[WeightmapRT_Mip0];

					SourceDebugName = OutputDebugName ? TEXT("WeightmapScratchTexture") : GEmptyDebugName;
					DestDebugName = OutputDebugName ? CurrentRT->GetName() : GEmptyDebugName;
					
					// Convert Back to BGRA8(Win7)
					FLandscapeLayersWeightmapConvertFormatShaderParameter ShaderParams;
					ShaderParams.ReadWeightmap = WeightmapScratchPackLayerTextureResource;
					ConvertR32ToBGRA8(WeightmapTextureOutputOffset, CurrentRT, ShaderParams);
					
					DrawWeightmapComponentToRenderTargetMips(WeightmapTextureOutputOffset, CurrentRT, true, PSShaderParams);

					int32 StartTextureIndex = NextTextureIndexToProcess;

					for (; NextTextureIndexToProcess < ProcessedWeightmaps.Num(); ++NextTextureIndexToProcess)
					{
						UTexture2D* WeightmapTexture = ProcessedWeightmaps[NextTextureIndexToProcess];
						FLandscapeLayersTexture2DCPUReadBackResource* WeightmapCPUReadBack = ProcessedCPUReadbackTextures[NextTextureIndexToProcess];
						FIntPoint TextureTopLeftPositionInAtlas(WeightmapTextureOutputOffset[NextTextureIndexToProcess - StartTextureIndex].X, WeightmapTextureOutputOffset[NextTextureIndexToProcess - StartTextureIndex].Y);

						int32 CurrentMip = 0;

						for (int32 MipRTIndex = EWeightmapRTType::WeightmapRT_Mip0; MipRTIndex < EWeightmapRTType::WeightmapRT_Count; ++MipRTIndex)
						{
							CurrentRT = WeightmapRTList[MipRTIndex];

							if (CurrentRT != nullptr)
							{
								SourceDebugName = OutputDebugName ? CurrentRT->GetName() : GEmptyDebugName;
								DestDebugName = OutputDebugName ? FString::Printf(TEXT("Weightmap Mip: %d"), CurrentMip) : GEmptyDebugName;

								AddDeferredCopyLayersTexture(SourceDebugName, CurrentRT->GameThread_GetRenderTargetResource(), DestDebugName, WeightmapTexture->Resource, WeightmapCPUReadBack, TextureTopLeftPositionInAtlas, CurrentMip, CurrentMip);
								++CurrentMip;
							}
						}
					}

					CommitDeferredCopyLayersTexture();
				}

				++CurrentWeightmapToProcessIndex;
			}
		}

		UpdateLayersMaterialInstances(InLandscapeComponents);
	}

	if (WeightmapUpdateModes)
	{
		ResolveLayersWeightmapTexture(InLandscapeComponents);
	
		for (ULandscapeComponent* Component : InLandscapeComponents)
		{
			if (Component->IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag::Component_Update_Weightmap_Collision, WeightmapUpdateModes))
			{
				Component->UpdateCollisionLayerData();
			}
		}
	}

	return WeightmapUpdateModes;
}

uint32 ULandscapeComponent::ComputeWeightmapsHash()
{
	uint32 Hash = 0;
	TArray<FWeightmapLayerAllocationInfo>& ComponentWeightmapAllocations = GetWeightmapLayerAllocations();
	for (const FWeightmapLayerAllocationInfo& AllocationInfo : ComponentWeightmapAllocations)
	{
		Hash = HashCombine(AllocationInfo.GetHash(), Hash);
	}

	TArray<UTexture2D*>& ComponentWeightmapTextures = GetWeightmapTextures();
	TArray<ULandscapeWeightmapUsage*>& ComponentWeightmapTextureUsage = GetWeightmapTexturesUsage();
	for (int32 i = 0; i < ComponentWeightmapTextures.Num(); ++i)
	{
		Hash = PointerHash(ComponentWeightmapTextures[i], Hash);
		Hash = PointerHash(ComponentWeightmapTextureUsage[i], Hash);
		for (int32 j = 0; j < ULandscapeWeightmapUsage::NumChannels; ++j)
		{
			Hash = PointerHash(ComponentWeightmapTextureUsage[i]->ChannelUsage[j], Hash);
		}
	}
	return Hash;
}

void ALandscape::UpdateLayersMaterialInstances(const TArray<ULandscapeComponent*>& InLandscapeComponents)
{
	SCOPE_CYCLE_COUNTER(STAT_LandscapeLayersUpdateMaterialInstance);
	TArray<ULandscapeComponent*> ComponentsToUpdate;

	// Compute Weightmap usage changes
	if (ULandscapeInfo* Info = GetLandscapeInfo())
	{
		for (ULandscapeComponent* LandscapeComponent : InLandscapeComponents)
		{
			uint32 NewHash = LandscapeComponent->ComputeWeightmapsHash();
			if (LandscapeComponent->WeightmapsHash != NewHash)
			{
				ComponentsToUpdate.Add(LandscapeComponent);
				LandscapeComponent->WeightmapsHash = NewHash;
			}
		}
	}

	if (ComponentsToUpdate.Num() == 0)
	{
		return;
	}

	// we're not having the material update context recreate render states because we will manually do it for only our components
	TArray<FComponentRecreateRenderStateContext> RecreateRenderStateContexts;
	RecreateRenderStateContexts.Reserve(ComponentsToUpdate.Num());

	for (ULandscapeComponent* Component : ComponentsToUpdate)
	{
		RecreateRenderStateContexts.Emplace(Component);
	}
	TOptional<FMaterialUpdateContext> MaterialUpdateContext;
	MaterialUpdateContext.Emplace(FMaterialUpdateContext::EOptions::Default & ~FMaterialUpdateContext::EOptions::RecreateRenderStates);

	bool bHasUniformExpressionUpdatePending = false;

	for (ULandscapeComponent* Component : ComponentsToUpdate)
	{
		int32 MaxLOD = FMath::CeilLogTwo(SubsectionSizeQuads + 1) - 1;
		TMap<UMaterialInterface*, int8> NewMaterialPerLOD;
		Component->LODIndexToMaterialIndex.SetNumUninitialized(MaxLOD + 1);
		int8 LastLODIndex = INDEX_NONE;

		UMaterialInterface* BaseMaterial = GetLandscapeMaterial();
		UMaterialInterface* LOD0Material = GetLandscapeMaterial(0);

		for (int32 LODIndex = 0; LODIndex <= MaxLOD; ++LODIndex)
		{
			UMaterialInterface* CurrentMaterial = GetLandscapeMaterial(LODIndex);

			// if we have a LOD0 override, do not let the base material override it, it should override everything!
			if (CurrentMaterial == BaseMaterial && BaseMaterial != LOD0Material)
			{
				CurrentMaterial = LOD0Material;
			}

			const int8* MaterialLOD = NewMaterialPerLOD.Find(CurrentMaterial);

			if (MaterialLOD != nullptr)
			{
				Component->LODIndexToMaterialIndex[LODIndex] = *MaterialLOD > LastLODIndex ? *MaterialLOD : LastLODIndex;
			}
			else
			{
				int32 AddedIndex = NewMaterialPerLOD.Num();
				NewMaterialPerLOD.Add(CurrentMaterial, LODIndex);
				Component->LODIndexToMaterialIndex[LODIndex] = AddedIndex;
				LastLODIndex = AddedIndex;
			}
		}

		Component->MaterialPerLOD = NewMaterialPerLOD;

		Component->MaterialInstances.SetNumZeroed(Component->MaterialPerLOD.Num()/* * 2*/); // over allocate in case we are using tessellation
		Component->MaterialIndexToDisabledTessellationMaterial.Init(INDEX_NONE, MaxLOD + 1);
		int8 TessellatedMaterialCount = 0;
		int8 MaterialIndex = 0;

		TArray<FWeightmapLayerAllocationInfo> WeightmapBaseLayerAllocation = Component->GetWeightmapLayerAllocations(); // We copy the array here

		TArray<UTexture2D*>& ComponentWeightmapTextures = Component->GetWeightmapTextures();
		UTexture2D* Heightmap = Component->GetHeightmap();

		for (auto& ItPair : Component->MaterialPerLOD)
		{
			const int8 MaterialLOD = ItPair.Value;

			// Find or set a matching MIC in the Landscape's map.
			UMaterialInstanceConstant* CombinationMaterialInstance = Component->GetCombinationMaterial(&MaterialUpdateContext.GetValue(), WeightmapBaseLayerAllocation, MaterialLOD, false);

			if (CombinationMaterialInstance != nullptr)
			{
				UMaterialInstanceConstant* MaterialInstance = Component->MaterialInstances[MaterialIndex];
				bool NeedToCreateMIC = MaterialInstance == nullptr;

				if (NeedToCreateMIC)
				{
					// Create the instance for this component, that will use the layer combination instance.
					MaterialInstance = NewObject<ULandscapeMaterialInstanceConstant>(this);
					Component->MaterialInstances[MaterialIndex] = MaterialInstance;
				}

				MaterialInstance->SetParentEditorOnly(CombinationMaterialInstance);

				MaterialUpdateContext.GetValue().AddMaterialInstance(MaterialInstance); // must be done after SetParent				

				FLinearColor Masks[4] = { FLinearColor(1.0f, 0.0f, 0.0f, 0.0f), FLinearColor(0.0f, 1.0f, 0.0f, 0.0f), FLinearColor(0.0f, 0.0f, 1.0f, 0.0f), FLinearColor(0.0f, 0.0f, 0.0f, 1.0f) };

				// Set the layer mask
				for (int32 AllocIdx = 0; AllocIdx < WeightmapBaseLayerAllocation.Num(); AllocIdx++)
				{
					FWeightmapLayerAllocationInfo& Allocation = WeightmapBaseLayerAllocation[AllocIdx];

					FName LayerName = Allocation.LayerInfo == ALandscapeProxy::VisibilityLayer ? UMaterialExpressionLandscapeVisibilityMask::ParameterName : Allocation.LayerInfo ? Allocation.LayerInfo->LayerName : NAME_None;
					MaterialInstance->SetVectorParameterValueEditorOnly(FName(*FString::Printf(TEXT("LayerMask_%s"), *LayerName.ToString())), Masks[Allocation.WeightmapTextureChannel]);
				}

				// Set the weightmaps
				for (int32 i = 0; i < ComponentWeightmapTextures.Num(); i++)
				{
					MaterialInstance->SetTextureParameterValueEditorOnly(FName(*FString::Printf(TEXT("Weightmap%d"), i)), ComponentWeightmapTextures[i]);
				}

				if (NeedToCreateMIC)
				{
					MaterialInstance->PostEditChange();
				}
				else
				{
					bHasUniformExpressionUpdatePending = true;
					MaterialInstance->RecacheUniformExpressions(true);
				}

				/*// Setup material instance with disabled tessellation
				if (CombinationMaterialInstance->GetMaterial()->D3D11TessellationMode != EMaterialTessellationMode::MTM_NoTessellation)
				{
					int32 TessellatedMaterialIndex = MaterialPerLOD.Num() + TessellatedMaterialCount++;
					ULandscapeMaterialInstanceConstant* TessellationMaterialInstance = Cast<ULandscapeMaterialInstanceConstant>(MaterialInstances[TessellatedMaterialIndex]);

					if (NeedToCreateMIC || TessellationMaterialInstance == nullptr)
					{
						TessellationMaterialInstance = NewObject<ULandscapeMaterialInstanceConstant>(this);
						TessellationMaterialInstance->SetParentEditorOnly(MaterialInstance);

						MaterialInstances[TessellatedMaterialIndex] = TessellationMaterialInstance;
						MaterialIndexToDisabledTessellationMaterial[MaterialIndex] = TessellatedMaterialIndex;

						TessellationMaterialInstance->bDisableTessellation = true;
						TessellationMaterialInstance->PostEditChange();
					}

					Context.AddMaterialInstance(TessellationMaterialInstance); // must be done after SetParent
				}
				*/
			}

			++MaterialIndex;
		}

		if (Component->MaterialPerLOD.Num() == 0)
		{
			Component->MaterialInstances.Empty(1);
			Component->MaterialInstances.Add(nullptr);
			Component->LODIndexToMaterialIndex.Empty(1);
			Component->LODIndexToMaterialIndex.Add(0);
		}

		Component->EditToolRenderData.UpdateDebugColorMaterial(Component);
	}

	// End material update
	MaterialUpdateContext.Reset();

	// Recreate the render state for our components, needed to update the static drawlist which has cached the MaterialRenderProxies
	// Must be after the FMaterialUpdateContext is destroyed
	RecreateRenderStateContexts.Empty();

	if (bHasUniformExpressionUpdatePending)
	{
		ENQUEUE_RENDER_COMMAND(UpdateDeferredCachedUniformExpressions)(
			[](FRHICommandList& RHICmdList)
		{
			FMaterialRenderProxy::UpdateDeferredCachedUniformExpressions();
		});
	}
}

void ALandscape::ResolveLayersWeightmapTexture(const TArray<ULandscapeComponent*>& InLandscapeComponents)
{
	SCOPE_CYCLE_COUNTER(STAT_LandscapeLayersResolveWeightmaps);

	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return;
	}

	TArray<UTexture2D*> ProcessedWeightmaps;

	for (ULandscapeComponent* Component : InLandscapeComponents)
	{
		TArray<UTexture2D*>& ComponentWeightmaps = Component->GetWeightmapTextures();

		for (UTexture2D* ComponentLayerWeightmap : ComponentWeightmaps)
		{
			if (!ProcessedWeightmaps.Contains(ComponentLayerWeightmap))
			{
				FLandscapeLayersTexture2DCPUReadBackResource** CPUReadback = Component->GetLandscapeProxy()->WeightmapsCPUReadBack.Find(ComponentLayerWeightmap);

				if (CPUReadback == nullptr)
				{
					continue;
				}

				ProcessedWeightmaps.Add(ComponentLayerWeightmap);

				ResolveLayersTexture(*CPUReadback, ComponentLayerWeightmap);
			}
		}
	}
}

bool ALandscape::HasLayersContent() const
{
	return LandscapeLayers.Num() > 0;
}

void ALandscape::RequestLayersInitialization(bool bInRequestContentUpdate)
{
	if (!CanHaveLayersContent())
	{
		return;
	}

	bLandscapeLayersAreInitialized = false;
	LandscapeSplinesAffectedComponents.Empty();

	if (bInRequestContentUpdate)
	{
		RequestLayersContentUpdateForceAll();
	}
}

void ALandscape::RequestLayersContentUpdate(ELandscapeLayerUpdateMode InUpdateMode)
{
	LayerContentUpdateModes |= InUpdateMode;
}

void ALandscape::RequestLayersContentUpdateForceAll(ELandscapeLayerUpdateMode InModeMask)
{
	// Ignore Update requests while in PostLoad (to avoid dirtying package on load)
	if (FUObjectThreadContext::Get().IsRoutingPostLoad)
	{
		return;
	}

	if (!CanHaveLayersContent())
	{
		return;
	}

	const bool bUpdateWeightmap = (InModeMask & (ELandscapeLayerUpdateMode::Update_Weightmap_All | ELandscapeLayerUpdateMode::Update_Weightmap_Editing | ELandscapeLayerUpdateMode::Update_Weightmap_Editing_NoCollision)) != 0;
	const bool bUpdateHeightmap = (InModeMask & (ELandscapeLayerUpdateMode::Update_Heightmap_All | ELandscapeLayerUpdateMode::Update_Heightmap_Editing | ELandscapeLayerUpdateMode::Update_Heightmap_Editing_NoCollision)) != 0;
	const bool bUpdateWeightCollision = (InModeMask & (ELandscapeLayerUpdateMode::Update_Weightmap_All | ELandscapeLayerUpdateMode::Update_Weightmap_Editing)) != 0;
	const bool bUpdateHeightCollision = (InModeMask & (ELandscapeLayerUpdateMode::Update_Heightmap_All | ELandscapeLayerUpdateMode::Update_Heightmap_Editing)) != 0;
	const bool bUpdateAllHeightmap = (InModeMask & ELandscapeLayerUpdateMode::Update_Heightmap_All) != 0;
	const bool bUpdateAllWeightmap = (InModeMask & ELandscapeLayerUpdateMode::Update_Weightmap_All) != 0;
	const bool bUpdateClientUdpateEditing = (InModeMask & ELandscapeLayerUpdateMode::Update_Client_Editing) != 0;
	if (ULandscapeInfo* LandscapeInfo = GetLandscapeInfo())
	{
		LandscapeInfo->ForAllLandscapeProxies([bUpdateHeightmap, bUpdateWeightmap, bUpdateAllHeightmap, bUpdateAllWeightmap, bUpdateHeightCollision, bUpdateWeightCollision, bUpdateClientUdpateEditing](ALandscapeProxy* Proxy)
		{
			if (Proxy)
			{
				if (bUpdateHeightmap || bUpdateWeightmap)
				{
					Proxy->InvalidateGeneratedComponentData();
				}

				for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
				{
					if (bUpdateWeightmap || bUpdateHeightmap)
					{
						Component->Modify();
					}

					if (bUpdateHeightmap)
					{
						Component->RequestHeightmapUpdate(bUpdateAllHeightmap, bUpdateHeightCollision);
					}

					if (bUpdateWeightmap)
					{
						Component->RequestWeightmapUpdate(bUpdateAllWeightmap, bUpdateWeightCollision);
					}

					if (bUpdateClientUdpateEditing)
					{
						Component->RequestEditingClientUpdate();
					}
				}
			}
		});
	}

	RequestLayersContentUpdate(InModeMask);
}

bool ULandscapeComponent::IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag InFlag, uint32 InModeMask) const
{
	uint32 UpdateMode = (LayerUpdateFlagPerMode & InModeMask);
	
	if (UpdateMode & ELandscapeLayerUpdateMode::Update_Heightmap_All)
	{
		const uint32 HeightmapAllFlags = ELandscapeComponentUpdateFlag::Component_Update_Heightmap_Collision | ELandscapeComponentUpdateFlag::Component_Update_Recreate_Collision | ELandscapeComponentUpdateFlag::Component_Update_Client;
		if (HeightmapAllFlags & InFlag)
		{
			return true;
		}
	}
		
	if (UpdateMode & ELandscapeLayerUpdateMode::Update_Heightmap_Editing)
	{
		const uint32 HeightmapEditingFlags = ELandscapeComponentUpdateFlag::Component_Update_Heightmap_Collision | ELandscapeComponentUpdateFlag::Component_Update_Client_Editing;
		if (HeightmapEditingFlags & InFlag)
		{
			return true;
		}
	}

	if (UpdateMode & ELandscapeLayerUpdateMode::Update_Weightmap_All)
	{
		const uint32 WeightmapAllFlags = ELandscapeComponentUpdateFlag::Component_Update_Weightmap_Collision | ELandscapeComponentUpdateFlag::Component_Update_Recreate_Collision | ELandscapeComponentUpdateFlag::Component_Update_Client;
		if (WeightmapAllFlags & InFlag)
		{
			return true;
		}
	}

	if (UpdateMode & ELandscapeLayerUpdateMode::Update_Weightmap_Editing)
	{
		const uint32 WeightmapEditingFlags = ELandscapeComponentUpdateFlag::Component_Update_Weightmap_Collision | ELandscapeComponentUpdateFlag::Component_Update_Client_Editing;
		if (WeightmapEditingFlags & InFlag)
		{
			return true;
		}
	}

	if (UpdateMode & ELandscapeLayerUpdateMode::Update_Client_Editing)
	{
		const uint32 WeightmapEditingFlags = ELandscapeComponentUpdateFlag::Component_Update_Client_Editing;
		if (WeightmapEditingFlags & InFlag)
		{
			return true;
		}
	}

	if (UpdateMode & ELandscapeLayerUpdateMode::Update_Client_Deferred)
	{
		const uint32 DeferredClientUpdateFlags = ELandscapeComponentUpdateFlag::Component_Update_Client;
		if (DeferredClientUpdateFlags & InFlag)
		{
			return true;
		}
	}

	if (UpdateMode & (ELandscapeLayerUpdateMode::Update_Heightmap_Editing_NoCollision | ELandscapeLayerUpdateMode::Update_Weightmap_Editing_NoCollision))
	{
		const uint32 EditingNoCollisionFlags = ELandscapeComponentUpdateFlag::Component_Update_Approximated_Bounds;
		if (EditingNoCollisionFlags & InFlag)
		{
			return true;
		}
	}

	return false;
}

void ULandscapeComponent::ClearUpdateFlagsForModes(uint32 InModeMask)
{
	LayerUpdateFlagPerMode &= ~InModeMask;
}

void ULandscapeComponent::RequestDeferredClientUpdate()
{
	LayerUpdateFlagPerMode |= ELandscapeLayerUpdateMode::Update_Client_Deferred;
}

void ULandscapeComponent::RequestEditingClientUpdate()
{
	LayerUpdateFlagPerMode |= ELandscapeLayerUpdateMode::Update_Client_Editing;
	if (ALandscape* LandscapeActor = GetLandscapeActor())
	{
		LandscapeActor->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Client_Editing);
	}
}

void ULandscapeComponent::RequestHeightmapUpdate(bool bUpdateAll, bool bUpdateCollision)
{
	if (bUpdateAll || bUpdateCollision)
	{
		LayerUpdateFlagPerMode |= ELandscapeLayerUpdateMode::Update_Heightmap_Editing;
		LayerUpdateFlagPerMode |= ELandscapeLayerUpdateMode::Update_Heightmap_All;
	}
	else
	{
		LayerUpdateFlagPerMode |= ELandscapeLayerUpdateMode::Update_Heightmap_Editing_NoCollision;
	}
	if (ALandscape* LandscapeActor = GetLandscapeActor())
	{
		LandscapeActor->RequestLayersContentUpdate(bUpdateCollision ? ELandscapeLayerUpdateMode::Update_Heightmap_Editing : ELandscapeLayerUpdateMode::Update_Heightmap_Editing_NoCollision);
		if (bUpdateAll)
		{
			LandscapeActor->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Heightmap_All);
		}
	}
}

void ULandscapeComponent::RequestWeightmapUpdate(bool bUpdateAll, bool bUpdateCollision)
{
	if (bUpdateAll || bUpdateCollision)
	{
		LayerUpdateFlagPerMode |= ELandscapeLayerUpdateMode::Update_Weightmap_Editing;
		LayerUpdateFlagPerMode |= ELandscapeLayerUpdateMode::Update_Weightmap_All;
	}
	else
	{
		LayerUpdateFlagPerMode |= ELandscapeLayerUpdateMode::Update_Weightmap_Editing_NoCollision;
	}
	if (ALandscape* LandscapeActor = GetLandscapeActor())
	{
		LandscapeActor->RequestLayersContentUpdate(bUpdateCollision ? ELandscapeLayerUpdateMode::Update_Weightmap_Editing : ELandscapeLayerUpdateMode::Update_Weightmap_Editing_NoCollision);
		if (bUpdateAll)
		{
			LandscapeActor->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Weightmap_All);
		}
	}
}

void ALandscape::MonitorLandscapeEdModeChanges()
{
	bool bRequiredEditingClientFullUpdate = false;
	if (LandscapeEdModeInfo.ViewMode != GLandscapeViewMode)
	{
		LandscapeEdModeInfo.ViewMode = GLandscapeViewMode;
		bRequiredEditingClientFullUpdate = true;
	}

	ELandscapeToolTargetType::Type NewValue = LandscapeEdMode ? LandscapeEdMode->GetLandscapeToolTargetType() : ELandscapeToolTargetType::Invalid;
	if (LandscapeEdModeInfo.ToolTarget != NewValue)
	{
		LandscapeEdModeInfo.ToolTarget = NewValue;
		bRequiredEditingClientFullUpdate = true;
	}

	const FLandscapeLayer* SelectedLayer = LandscapeEdMode ? LandscapeEdMode->GetLandscapeSelectedLayer() : nullptr;
	FGuid NewSelectedLayer = SelectedLayer && SelectedLayer->bVisible ? SelectedLayer->Guid : FGuid();
	if (LandscapeEdModeInfo.SelectedLayer != NewSelectedLayer)
	{
		LandscapeEdModeInfo.SelectedLayer = NewSelectedLayer;
		bRequiredEditingClientFullUpdate = true;
	}

	TWeakObjectPtr<ULandscapeLayerInfoObject> NewLayerInfoObject;
	if (LandscapeEdMode)
	{
		NewLayerInfoObject = LandscapeEdMode->GetSelectedLandscapeLayerInfo();
	}
	if (LandscapeEdModeInfo.SelectedLayerInfoObject != NewLayerInfoObject)
	{
		LandscapeEdModeInfo.SelectedLayerInfoObject = NewLayerInfoObject;
		bRequiredEditingClientFullUpdate = true;
	}

	if (bRequiredEditingClientFullUpdate && (LandscapeEdModeInfo.ViewMode == ELandscapeViewMode::LayerContribution))
	{
		RequestLayersContentUpdateForceAll(ELandscapeLayerUpdateMode::Update_Client_Editing);
	}
}

void ALandscape::MonitorShaderCompilation()
{
	// If doing editing while shader are compiling or at load of a map, it's possible we will need another update pass after shader are completed to see the correct result
	const int32 RemainingShadersThisFrame = GShaderCompilingManager->GetNumRemainingJobs();
	if (!WasCompilingShaders && RemainingShadersThisFrame > 0)
	{
		WasCompilingShaders = true;
	}
	else if (WasCompilingShaders)
	{
		WasCompilingShaders = false;
		RequestLayersContentUpdateForceAll();
	}
}

void ALandscape::UpdateLayersContent(bool bInWaitForStreaming, bool bInSkipMonitorLandscapeEdModeChanges)
{
	if (GetLandscapeInfo() == nullptr || !CanHaveLayersContent())
	{
		return;
	}

	if (!bLandscapeLayersAreInitialized)
	{
		InitializeLayers();
	}

	if (!bInSkipMonitorLandscapeEdModeChanges)
	{
		MonitorLandscapeEdModeChanges();
	}
	MonitorShaderCompilation();

	const bool bForceRender = CVarOutputLayersDebugDrawCallName.GetValueOnAnyThread() == 1;

	if (LayerContentUpdateModes == 0 && !bForceRender)
	{
		return;
	}

	TArray<ULandscapeComponent*> AllLandscapeComponents;
	GetLandscapeInfo()->ForAllLandscapeComponents([&AllLandscapeComponents](ULandscapeComponent* InLandscapeComponent)
	{
		AllLandscapeComponents.Add(InLandscapeComponent);
	});

	int32 ProcessedModes = 0;
	ProcessedModes |= RegenerateLayersHeightmaps(AllLandscapeComponents, bInWaitForStreaming);
	ProcessedModes |= RegenerateLayersWeightmaps(AllLandscapeComponents, bInWaitForStreaming);
	ProcessedModes |= (LayerContentUpdateModes & ELandscapeLayerUpdateMode::Update_Client_Deferred);
	ProcessedModes |= (LayerContentUpdateModes & ELandscapeLayerUpdateMode::Update_Client_Editing);
	LayerContentUpdateModes &= ~ProcessedModes;

	if (!ALandscape::UpdateCollisionAndClients(AllLandscapeComponents, ProcessedModes))
	{
		LayerContentUpdateModes |= ELandscapeLayerUpdateMode::Update_Client_Deferred;
	}

	if (LandscapeEdMode)
	{
		LandscapeEdMode->PostUpdateLayerContent();
	}
}

// not thread safe
struct FEnableCollisionHashOptimScope
{
	FEnableCollisionHashOptimScope(ULandscapeHeightfieldCollisionComponent* InCollisionComponent)
	{
		CollisionComponent = InCollisionComponent;
		if (CollisionComponent)
		{
			// not reentrant
			check(!CollisionComponent->bEnableCollisionHashOptim);
			CollisionComponent->bEnableCollisionHashOptim = true;
		}
	}

	~FEnableCollisionHashOptimScope()
	{
		if (CollisionComponent)
		{
			CollisionComponent->bEnableCollisionHashOptim = false;
		}
	}

private:
	ULandscapeHeightfieldCollisionComponent* CollisionComponent;
};

bool ALandscape::UpdateCollisionAndClients(const TArray<ULandscapeComponent*>& InLandscapeComponents, const int32 InContentUpdateModes)
{
	bool bAllClientsUpdated = true;

	const uint16 DefaultHeightValue = LandscapeDataAccess::GetTexHeight(0.f);
	const uint8 MaxLayerContributingValue = UINT8_MAX;
	const float HeightValueNormalizationFactor = 1.f / (0.5f * UINT16_MAX);
	TArray<uint16> HeightData;
	TArray<uint8> LayerContributionMaskData;

	for (ULandscapeComponent* LandscapeComponent : InLandscapeComponents)
	{
		bool bDeferClientUpdateForComponent = false;
		bool bDoUpdateClient = true;
		if (LandscapeComponent->IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag::Component_Update_Recreate_Collision, InContentUpdateModes))
		{
			if (ULandscapeHeightfieldCollisionComponent* CollisionComp = LandscapeComponent->CollisionComponent.Get())
			{
				FEnableCollisionHashOptimScope Scope(CollisionComp);
				bDoUpdateClient = CollisionComp->RecreateCollision();
			}
		}

		if (bDoUpdateClient && LandscapeComponent->IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag::Component_Update_Client, InContentUpdateModes))
		{
			if (!GUndo)
			{
				if (ULandscapeHeightfieldCollisionComponent* CollisionComp = LandscapeComponent->CollisionComponent.Get())
				{
					FNavigationSystem::UpdateComponentData(*CollisionComp);
					CollisionComp->SnapFoliageInstances();
				}
			}
			else
			{
				bDeferClientUpdateForComponent = true;
				bAllClientsUpdated = false;
			}
		}

		if (LandscapeComponent->IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag::Component_Update_Client_Editing, InContentUpdateModes))
		{
			if (LandscapeEdModeInfo.ViewMode == ELandscapeViewMode::LayerContribution)
			{
				check(ComponentSizeQuads == LandscapeComponent->ComponentSizeQuads);
				const int32 Stride = (1 + ComponentSizeQuads);
				const int32 ArraySize = Stride * Stride;
				if (LayerContributionMaskData.Num() != ArraySize)
				{
					LayerContributionMaskData.AddZeroed(ArraySize);
				}
				uint8* LayerContributionMaskDataPtr = LayerContributionMaskData.GetData();
				const int32 X1 = LandscapeComponent->GetSectionBase().X;
				const int32 X2 = X1 + ComponentSizeQuads;
				const int32 Y1 = LandscapeComponent->GetSectionBase().Y;
				const int32 Y2 = Y1 + ComponentSizeQuads;
				bool bLayerContributionWrittenData = false;

				ULandscapeInfo* Info = LandscapeComponent->GetLandscapeInfo();
				check(Info);
				FLandscapeEditDataInterface LandscapeEdit(Info);

				if (LandscapeEdModeInfo.SelectedLayer.IsValid())
				{
					FScopedSetLandscapeEditingLayer Scope(this, LandscapeEdModeInfo.SelectedLayer);
					if (LandscapeEdModeInfo.ToolTarget == ELandscapeToolTargetType::Heightmap)
					{
						if (HeightData.Num() != ArraySize)
						{
							HeightData.AddZeroed(ArraySize);
						}
						LandscapeEdit.GetHeightDataFast(X1, Y1, X2, Y2, HeightData.GetData(), Stride);
						for (int i = 0; i < ArraySize; ++i)
						{
							LayerContributionMaskData[i] = HeightData[i] != DefaultHeightValue ? (uint8)(FMath::Pow(FMath::Clamp((HeightValueNormalizationFactor*FMath::Abs(HeightData[i] - DefaultHeightValue)), 0.f, (float)1.f), 0.25f)*MaxLayerContributingValue) : 0;
						}
						bLayerContributionWrittenData = true;
					}
					else if (LandscapeEdModeInfo.ToolTarget == ELandscapeToolTargetType::Weightmap || LandscapeEdModeInfo.ToolTarget == ELandscapeToolTargetType::Visibility)
					{
						ULandscapeLayerInfoObject* LayerObject = (LandscapeEdModeInfo.ToolTarget == ELandscapeToolTargetType::Visibility) ? ALandscapeProxy::VisibilityLayer : LandscapeEdModeInfo.SelectedLayerInfoObject.Get();
						if (LayerObject)
						{
							LandscapeEdit.GetWeightDataFast(LayerObject, X1, Y1, X2, Y2, LayerContributionMaskData.GetData(), Stride);
							bLayerContributionWrittenData = true;
						}
					}
				}
				if (!bLayerContributionWrittenData)
				{
					FMemory::Memzero(LayerContributionMaskDataPtr, ArraySize);
				}
				LandscapeEdit.SetLayerContributionData(X1, Y1, X2, Y2, LayerContributionMaskDataPtr, 0);
			}
		}

		LandscapeComponent->ClearUpdateFlagsForModes(InContentUpdateModes);
		if (bDeferClientUpdateForComponent)
		{
			LandscapeComponent->RequestDeferredClientUpdate();
		}
	}

	return bAllClientsUpdated;
}

void ALandscape::InitializeLayers()
{
	check(HasLayersContent());
	check(!bLandscapeLayersAreInitialized);

	CreateLayersRenderingResource();
	InitializeLandscapeLayersWeightmapUsage();

	bLandscapeLayersAreInitialized = true;
}

void ALandscape::OnPreSave()
{
	ForceUpdateLayersContent();
}

void ALandscape::ForceUpdateLayersContent()
{
	const bool bWaitForStreaming = true;
	const bool bInSkipMonitorLandscapeEdModeChanges = true;
	UpdateLayersContent(bWaitForStreaming, bInSkipMonitorLandscapeEdModeChanges);
}

void ALandscape::TickLayers(float DeltaTime, ELevelTick TickType, FActorTickFunction& ThisTickFunction)
{
	check(GIsEditor);

	UWorld* World = GetWorld();
	if (World && !World->IsPlayInEditor() && GetLandscapeInfo())
	{
		if (CVarLandscapeSimulatePhysics.GetValueOnAnyThread() == 1)
		{
			World->bShouldSimulatePhysics = true;
		}

		UpdateLayersContent();
	}
}

#endif

void ALandscapeProxy::BeginDestroy()
{
#if WITH_EDITORONLY_DATA
	if (CanHaveLayersContent())
	{
		for (auto& ItPair : HeightmapsCPUReadBack)
		{
			FLandscapeLayersTexture2DCPUReadBackResource* HeightmapCPUReadBack = ItPair.Value;

			if (HeightmapCPUReadBack != nullptr)
			{
				BeginReleaseResource(HeightmapCPUReadBack);
			}
		}

		for (auto& ItPair : WeightmapsCPUReadBack)
		{
			FLandscapeLayersTexture2DCPUReadBackResource* WeightmapCPUReadBack = ItPair.Value;

			if (WeightmapCPUReadBack != nullptr)
			{
				BeginReleaseResource(WeightmapCPUReadBack);
			}
		}

		ReleaseResourceFence.BeginFence();
	}
#endif

	Super::BeginDestroy();
}

bool ALandscapeProxy::IsReadyForFinishDestroy()
{
	bool bReadyForFinishDestroy = Super::IsReadyForFinishDestroy();

#if WITH_EDITORONLY_DATA
	if (CanHaveLayersContent())
	{
		if (bReadyForFinishDestroy)
		{
			bReadyForFinishDestroy = ReleaseResourceFence.IsFenceComplete();
		}
	}
#endif

	return bReadyForFinishDestroy;
}

void ALandscapeProxy::FinishDestroy()
{
#if WITH_EDITORONLY_DATA
	if (CanHaveLayersContent())
	{
		check(ReleaseResourceFence.IsFenceComplete());

		for (auto& ItPair : HeightmapsCPUReadBack)
		{
			FLandscapeLayersTexture2DCPUReadBackResource* HeightmapCPUReadBack = ItPair.Value;

			delete HeightmapCPUReadBack;
			HeightmapCPUReadBack = nullptr;
		}

		for (auto& ItPair : WeightmapsCPUReadBack)
		{
			FLandscapeLayersTexture2DCPUReadBackResource* WeightmapCPUReadBack = ItPair.Value;

			delete WeightmapCPUReadBack;
			WeightmapCPUReadBack = nullptr;
		}
	}
#endif

	Super::FinishDestroy();
}

#if WITH_EDITOR
bool ALandscapeProxy::CanHaveLayersContent() const
{
	if (HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		return false;
	}

	if (const ALandscape* LandscapeActor = GetLandscapeActor())
	{
		return LandscapeActor->bCanHaveLayersContent;
	}

	return false;
}

bool ALandscapeProxy::HasLayersContent() const
{
	return bHasLayersContent;
}

void ALandscapeProxy::UpdateCachedHasLayersContent(bool InCheckComponentDataIntegrity)
{
	// In the case of InCheckComponentDataIntegrity we will loop through all components to make sure they all have the same state and in the other case we will assume that the 1st component represent the state of all the others.
	bHasLayersContent = LandscapeComponents.Num() > 0 ? LandscapeComponents[0]->HasLayersData() : false;

	if (InCheckComponentDataIntegrity)
	{
		for (const ULandscapeComponent* Component : LandscapeComponents)
		{
			check(bHasLayersContent == Component->HasLayersData());
		}
	}
}

bool ALandscapeProxy::RemoveObsoleteLayers(const TSet<FGuid>& InExistingLayers)
{
	TSet<FGuid> ComponentLayers;
	for (ULandscapeComponent* Component : LandscapeComponents)
	{
		Component->ForEachLayer([&](const FGuid& InGuid, FLandscapeLayerComponentData&) { ComponentLayers.Add(InGuid); });
	}

	bool bModified = false;

	for (const FGuid& LayerGuid : ComponentLayers)
	{
		if (!InExistingLayers.Contains(LayerGuid))
		{
			UE_LOG(LogLandscape, Warning, TEXT("Layer '%s' was removed from LandscapeProxy '%s' because it doesn't match any of the LandscapeActor Layers. Possible loss of data."), 
				*LayerGuid.ToString(EGuidFormats::HexValuesInBraces), *GetPathName());
			DeleteLayer(LayerGuid);
			bModified = true;
		}
	}

	if (bModified)
	{
		if (ALandscape* LandscapeActor = GetLandscapeActor())
		{
			LandscapeActor->RequestLayersContentUpdateForceAll();
		}
	}

	return bModified;
}

bool ALandscapeProxy::AddLayer(const FGuid& InLayerGuid)
{
	bool bModified = false;
	for (ULandscapeComponent* Component : LandscapeComponents)
	{
		if (!Component->GetLayerData(InLayerGuid))
		{
			Component->AddLayerData(InLayerGuid, FLandscapeLayerComponentData());
			bModified = true;
		}
	}	

	UpdateCachedHasLayersContent();

	if (bModified)
	{
		InitializeLayerWithEmptyContent(InLayerGuid);
	}

	return bModified;
}

void ALandscapeProxy::DeleteLayer(const FGuid& InLayerGuid)
{
	for (ULandscapeComponent* Component : LandscapeComponents)
	{
		const FLandscapeLayerComponentData* LayerComponentData = Component->GetLayerData(InLayerGuid);

		if (LayerComponentData != nullptr)
		{
			for (const FWeightmapLayerAllocationInfo& Allocation : LayerComponentData->WeightmapData.LayerAllocations)
			{
				UTexture2D* WeightmapTexture = LayerComponentData->WeightmapData.Textures[Allocation.WeightmapTextureIndex];
				ULandscapeWeightmapUsage** Usage = WeightmapUsageMap.Find(WeightmapTexture);

				if (Usage != nullptr && (*Usage) != nullptr)
				{
					(*Usage)->Modify();
					(*Usage)->ChannelUsage[Allocation.WeightmapTextureChannel] = nullptr;

					if ((*Usage)->IsEmpty())
					{
						Modify();
						WeightmapUsageMap.Remove(WeightmapTexture);
					}
				}
			}
			Component->RemoveLayerData(InLayerGuid);
		}
	}

	UpdateCachedHasLayersContent();
}

void ALandscapeProxy::InitializeLayerWithEmptyContent(const FGuid& InLayerGuid)
{
	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return;
	}
		
	// Build a mapping between each Heightmaps and Component in them
	TMap<UTexture2D*, TArray<ULandscapeComponent*>> ComponentsPerHeightmaps;

	for (ULandscapeComponent* Component : LandscapeComponents)
	{
		UTexture2D* ComponentHeightmapTexture = Component->GetHeightmap();
		TArray<ULandscapeComponent*>& ComponentList = ComponentsPerHeightmaps.FindOrAdd(ComponentHeightmapTexture);
		ComponentList.Add(Component);
	}
		
	// Init layers with valid "empty" data
	TMap<UTexture2D*, UTexture2D*> CreatedHeightmapTextures; // < Final layer texture, New created texture for layer

	for (ULandscapeComponent* Component : LandscapeComponents)
	{
		UTexture2D* ComponentHeightmap = Component->GetHeightmap();
		const TArray<ULandscapeComponent*>* ComponentsUsingHeightmap = ComponentsPerHeightmaps.Find(ComponentHeightmap);
		check(ComponentsUsingHeightmap != nullptr);

		Component->AddDefaultLayerData(InLayerGuid, *ComponentsUsingHeightmap, CreatedHeightmapTextures);
	}

	// Finish caching
	for (TPair<UTexture2D*, UTexture2D*> Pair : CreatedHeightmapTextures)
	{
		if (Pair.Value != nullptr && !Pair.Value->IsAsyncCacheComplete())
		{
			Pair.Value->FinishCachePlatformData();
		}
	}
}
#endif

void ALandscape::BeginDestroy()
{
#if WITH_EDITOR
	if (CanHaveLayersContent())
	{
		if (CombinedLayersWeightmapAllMaterialLayersResource != nullptr)
		{
			BeginReleaseResource(CombinedLayersWeightmapAllMaterialLayersResource);
		}

		if (CurrentLayersWeightmapAllMaterialLayersResource != nullptr)
		{
			BeginReleaseResource(CurrentLayersWeightmapAllMaterialLayersResource);
		}

		if (WeightmapScratchExtractLayerTextureResource != nullptr)
		{
			BeginReleaseResource(WeightmapScratchExtractLayerTextureResource);
		}

		if (WeightmapScratchPackLayerTextureResource != nullptr)
		{
			BeginReleaseResource(WeightmapScratchPackLayerTextureResource);
		}

		// Use ResourceFence from base class		
	}
#endif

	Super::BeginDestroy();
}

void ALandscape::FinishDestroy()
{
#if WITH_EDITORONLY_DATA
	if (CanHaveLayersContent())
	{
		check(ReleaseResourceFence.IsFenceComplete());

		delete CombinedLayersWeightmapAllMaterialLayersResource;
		delete CurrentLayersWeightmapAllMaterialLayersResource;
		delete WeightmapScratchExtractLayerTextureResource;
		delete WeightmapScratchPackLayerTextureResource;

		CombinedLayersWeightmapAllMaterialLayersResource = nullptr;
		CurrentLayersWeightmapAllMaterialLayersResource = nullptr;
		WeightmapScratchExtractLayerTextureResource = nullptr;
		WeightmapScratchPackLayerTextureResource = nullptr;
	}
#endif

	Super::FinishDestroy();
}

bool ALandscape::IsUpToDate() const
{
#if WITH_EDITORONLY_DATA
	if (CanHaveLayersContent())
	{
		return LayerContentUpdateModes == 0;
	}
#endif

	return true;
}

#if WITH_EDITOR
bool ALandscape::IsLayerNameUnique(const FName& InName) const
{
	return Algo::CountIf(LandscapeLayers, [InName](const FLandscapeLayer& Layer) { return (Layer.Name == InName); }) == 0;
}

void ALandscape::SetLayerName(int32 InLayerIndex, const FName& InName)
{
	FLandscapeLayer* Layer = GetLayer(InLayerIndex);
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	if (!LandscapeInfo || !Layer || Layer->Name == InName)
	{
		return;
	}

	if (!IsLayerNameUnique(InName))
	{
		return;
	}

	Modify();
	LandscapeLayers[InLayerIndex].Name = InName;
}

float ALandscape::GetLayerAlpha(int32 InLayerIndex, bool bInHeightmap) const
{
	const FLandscapeLayer* SplinesReservedLayer = GetLandscapeSplinesReservedLayer();
	const FLandscapeLayer* Layer = GetLayer(InLayerIndex);
	if (Layer && SplinesReservedLayer != Layer)
	{
		return GetClampedLayerAlpha(bInHeightmap ? Layer->HeightmapAlpha : Layer->WeightmapAlpha, bInHeightmap);
	}
	return 1.0f;
}

float ALandscape::GetClampedLayerAlpha(float InAlpha, bool bInHeightmap) const
{
	float AlphaClamped = FMath::Clamp<float>(InAlpha, bInHeightmap  ? -1.f : 0.f, 1.f);
	return AlphaClamped;
}

void ALandscape::SetLayerAlpha(int32 InLayerIndex, float InAlpha, bool bInHeightmap)
{
	FLandscapeLayer* Layer = GetLayer(InLayerIndex);
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	if (!LandscapeInfo || !Layer)
	{
		return;
	}
	const float InAlphaClamped = GetClampedLayerAlpha(InAlpha, bInHeightmap);
	float& LayerAlpha = bInHeightmap ? Layer->HeightmapAlpha : Layer->WeightmapAlpha;
	if (LayerAlpha == InAlphaClamped)
	{
		return;
	}

	Modify();
	LayerAlpha = InAlphaClamped;
	RequestLayersContentUpdateForceAll();
}

void ALandscape::SetLayerVisibility(int32 InLayerIndex, bool bInVisible)
{
	FLandscapeLayer* Layer = GetLayer(InLayerIndex);
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	if (!LandscapeInfo || !Layer || Layer->bVisible == bInVisible)
	{
		return;
	}

	Modify();
	Layer->bVisible = bInVisible;
	RequestLayersContentUpdateForceAll();
}

void ALandscape::SetLayerLocked(int32 InLayerIndex, bool bLocked)
{
	FLandscapeLayer* Layer = GetLayer(InLayerIndex);
	if (!Layer || Layer->bLocked == bLocked)
	{
		return;
	}

	Modify();
	Layer->bLocked = bLocked;
}

uint8 ALandscape::GetLayerCount() const
{
	return LandscapeLayers.Num();
}

FLandscapeLayer* ALandscape::GetLayer(int32 InLayerIndex)
{
	if (LandscapeLayers.IsValidIndex(InLayerIndex))
	{
		return &LandscapeLayers[InLayerIndex];
	}
	return nullptr;
}

const FLandscapeLayer* ALandscape::GetLayer(int32 InLayerIndex) const
{
	if (LandscapeLayers.IsValidIndex(InLayerIndex))
	{
		return &LandscapeLayers[InLayerIndex];
	}
	return nullptr;
}

int32 ALandscape::GetLayerIndex(FName InLayerName) const
{
	return LandscapeLayers.IndexOfByPredicate([InLayerName](const FLandscapeLayer& Layer) { return Layer.Name == InLayerName; });
}

const FLandscapeLayer* ALandscape::GetLayer(const FGuid& InLayerGuid) const
{
	return LandscapeLayers.FindByPredicate([&InLayerGuid](const FLandscapeLayer& Other) { return Other.Guid == InLayerGuid; });
}

void ALandscape::ForEachLayer(TFunctionRef<void(struct FLandscapeLayer&)> Fn)
{
	for (FLandscapeLayer& Layer : LandscapeLayers)
	{
		Fn(Layer);
	}
}

void ALandscape::DeleteLayers()
{
	for (int32 LayerIndex = LandscapeLayers.Num() - 1; LayerIndex >= 0; --LayerIndex)
	{
		DeleteLayer(LayerIndex);
	}
}

void ALandscape::DeleteLayer(int32 InLayerIndex)
{
	ensure(HasLayersContent());

	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	const FLandscapeLayer* Layer = GetLayer(InLayerIndex);
	if (!LandscapeInfo || !Layer)
	{
		return;
	}

	Modify();
	FGuid LayerGuid = Layer->Guid;

	// Clean up Weightmap usage in LandscapeProxies
	LandscapeInfo->ForAllLandscapeProxies([&LayerGuid](ALandscapeProxy* Proxy)
	{
		Proxy->DeleteLayer(LayerGuid);
	});

	const FLandscapeLayer* SplinesReservedLayer = GetLandscapeSplinesReservedLayer();
	if (SplinesReservedLayer == Layer)
	{
		LandscapeSplinesTargetLayerGuid.Invalidate();
	}

	// Remove layer from list
	LandscapeLayers.RemoveAt(InLayerIndex);

	// Request Update
	RequestLayersContentUpdateForceAll();
}

void ALandscape::GetUsedPaintLayers(int32 InLayerIndex, TArray<ULandscapeLayerInfoObject*>& OutUsedLayerInfos) const
{
	const FLandscapeLayer* Layer = GetLayer(InLayerIndex);
	if (Layer)
	{
		GetUsedPaintLayers(Layer->Guid, OutUsedLayerInfos);
	}
}

void ALandscape::GetUsedPaintLayers(const FGuid& InLayerGuid, TArray<ULandscapeLayerInfoObject*>& OutUsedLayerInfos) const
{
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	if (!LandscapeInfo)
	{
		return;
	}
	
	LandscapeInfo->GetUsedPaintLayers(InLayerGuid, OutUsedLayerInfos);
}

void ALandscape::ClearPaintLayer(int32 InLayerIndex, ULandscapeLayerInfoObject* InLayerInfo)
{
	const FLandscapeLayer* Layer = GetLayer(InLayerIndex);
	if (Layer)
	{
		ClearPaintLayer(Layer->Guid, InLayerInfo);
	}
}

void ALandscape::ClearPaintLayer(const FGuid& InLayerGuid, ULandscapeLayerInfoObject* InLayerInfo)
{
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	if (!LandscapeInfo)
	{
		return;
	}

	Modify();
	FScopedSetLandscapeEditingLayer Scope(this, InLayerGuid, [=] { RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Weightmap_All); });

	FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo);
	LandscapeInfo->ForAllLandscapeProxies([&](ALandscapeProxy* Proxy)
	{
		Proxy->Modify();
		for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
		{
			Component->DeleteLayer(InLayerInfo, LandscapeEdit);
		}
	});
}

void ALandscape::ClearLayer(int32 InLayerIndex, TSet<ULandscapeComponent*>* InComponents, ELandscapeClearMode InClearMode)
{
	const FLandscapeLayer* Layer = GetLayer(InLayerIndex);
	if (Layer)
	{
		ClearLayer(Layer->Guid, InComponents, InClearMode);
	}
}

void ALandscape::ClearLayer(const FGuid& InLayerGuid, TSet<ULandscapeComponent*>* InComponents, ELandscapeClearMode InClearMode)
{
	ensure(HasLayersContent());

	const FLandscapeLayer* Layer = GetLayer(InLayerGuid);
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	if (!LandscapeInfo || !Layer)
	{
		return;
	}

	Modify();
	FScopedSetLandscapeEditingLayer Scope(this, Layer->Guid, [=] { RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_All); });

	TArray<uint16> NewHeightData;
	NewHeightData.AddZeroed(FMath::Square(ComponentSizeQuads + 1));
	uint16 ZeroValue = LandscapeDataAccess::GetTexHeight(0.f);
	for (uint16& NewHeightDataValue : NewHeightData)
	{
		NewHeightDataValue = ZeroValue;
	}

	TArray<uint16> NewHeightAlphaBlendData;
	TArray<uint8> NewHeightFlagsData;

	if (InClearMode & ELandscapeClearMode::Clear_Heightmap)
	{
		if (Layer->BlendMode == LSBM_AlphaBlend)
		{
			NewHeightAlphaBlendData.Init(MAX_uint16, FMath::Square(ComponentSizeQuads + 1));
			NewHeightFlagsData.AddZeroed(FMath::Square(ComponentSizeQuads + 1));
		}
	}

	TArray<ULandscapeComponent*> Components;
	if (InComponents)
	{
		TSet<ALandscapeProxy*> Proxies;
		Components.Reserve(InComponents->Num());
		for (ULandscapeComponent* Component : *InComponents)
		{
			if (Component)
			{
				Components.Add(Component);
				ALandscapeProxy* Proxy = Component->GetLandscapeProxy();
				if (!Proxies.Find(Proxy))
				{
					Proxies.Add(Proxy);
					Proxy->Modify();
				}
			}
		}
	}
	else
	{
		LandscapeInfo->ForAllLandscapeProxies([&](ALandscapeProxy* Proxy)
		{
			Proxy->Modify();
			Components.Append(Proxy->LandscapeComponents);
		});
	}

	FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo);
	for (ULandscapeComponent* Component : Components)
	{
		if (InClearMode & ELandscapeClearMode::Clear_Heightmap)
		{
			int32 MinX = MAX_int32;
			int32 MinY = MAX_int32;
			int32 MaxX = MIN_int32;
			int32 MaxY = MIN_int32;
			Component->GetComponentExtent(MinX, MinY, MaxX, MaxY);
			check(ComponentSizeQuads == (MaxX - MinX));
			check(ComponentSizeQuads == (MaxY - MinY));
			LandscapeEdit.SetHeightData(MinX, MinY, MaxX, MaxY, NewHeightData.GetData(), 0, false, nullptr, NewHeightAlphaBlendData.GetData(), NewHeightFlagsData.GetData());
		}

		if (InClearMode & ELandscapeClearMode::Clear_Weightmap)
		{
			// Clear weight maps
			for (FLandscapeInfoLayerSettings& LayerSettings : LandscapeInfo->Layers)
			{
				Component->DeleteLayer(LayerSettings.LayerInfoObj, LandscapeEdit);
			}
		}
	}
}

void ALandscape::ShowOnlySelectedLayer(int32 InLayerIndex)
{
	const FLandscapeLayer* VisibleLayer = GetLayer(InLayerIndex);
	if (VisibleLayer)
	{
		for (FLandscapeLayer& Layer : LandscapeLayers)
		{
			Layer.bVisible = (&Layer == VisibleLayer);
		}
		RequestLayersContentUpdateForceAll();
	}
}

void ALandscape::ShowAllLayers()
{
	if (LandscapeLayers.Num() > 0)
	{
		for (FLandscapeLayer& Layer : LandscapeLayers)
		{
			Layer.bVisible = true;
		}
		RequestLayersContentUpdateForceAll();
	}
}

void ALandscape::SetLandscapeSplinesReservedLayer(int32 InLayerIndex)
{
	Modify();
	FLandscapeLayer* NewLayer = GetLayer(InLayerIndex);
	FLandscapeLayer* PreviousLayer = GetLandscapeSplinesReservedLayer();
	if (NewLayer != PreviousLayer)
	{
		LandscapeSplinesAffectedComponents.Empty();
		if (PreviousLayer)
		{
			ClearLayer(LandscapeSplinesTargetLayerGuid);
			PreviousLayer->BlendMode = LSBM_AdditiveBlend;
		}
		if (NewLayer)
		{
			NewLayer->HeightmapAlpha = 1.0f;
			NewLayer->WeightmapAlpha = 1.0f;
			NewLayer->BlendMode = LSBM_AlphaBlend;
			LandscapeSplinesTargetLayerGuid = NewLayer->Guid;
			ClearLayer(LandscapeSplinesTargetLayerGuid);
		}
		else
		{
			LandscapeSplinesTargetLayerGuid.Invalidate();
		}
	}
}

const FLandscapeLayer* ALandscape::GetLandscapeSplinesReservedLayer() const
{
	if (LandscapeSplinesTargetLayerGuid.IsValid())
	{
		return LandscapeLayers.FindByPredicate([this](const FLandscapeLayer& Other) { return Other.Guid == LandscapeSplinesTargetLayerGuid; });
	}
	return nullptr;
}

FLandscapeLayer* ALandscape::GetLandscapeSplinesReservedLayer()
{
	if (LandscapeSplinesTargetLayerGuid.IsValid())
	{
		return LandscapeLayers.FindByPredicate([this](const FLandscapeLayer& Other) { return Other.Guid == LandscapeSplinesTargetLayerGuid; });
	}
	return nullptr;
}

LANDSCAPE_API extern bool GDisableUpdateLandscapeMaterialInstances;

void ALandscape::UpdateLandscapeSplines(const FGuid& InTargetLayer, bool bInUpdateOnlySelected, bool bInForceUpdateAllCompoments)
{
	check(CanHaveLayersContent());
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	FGuid TargetLayerGuid = LandscapeSplinesTargetLayerGuid.IsValid() ? LandscapeSplinesTargetLayerGuid : InTargetLayer;
	const FLandscapeLayer* TargetLayer = GetLayer(TargetLayerGuid);
	if (LandscapeInfo && TargetLayer)
	{
		FScopedSetLandscapeEditingLayer Scope(this, TargetLayerGuid, [=] { this->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_All); });
		// Temporarily disable material instance updates since it will be done once at the end (requested by RequestLayersContentUpdateForceAll)
		GDisableUpdateLandscapeMaterialInstances = true;
		TSet<ULandscapeComponent*>* ModifiedComponent = nullptr;
		if (LandscapeSplinesTargetLayerGuid.IsValid())
		{
			ClearLayer(LandscapeSplinesTargetLayerGuid, (!bInForceUpdateAllCompoments && LandscapeSplinesAffectedComponents.Num()) ? &LandscapeSplinesAffectedComponents : nullptr);
			LandscapeSplinesAffectedComponents.Empty();
			ModifiedComponent = &LandscapeSplinesAffectedComponents;
			// For now, in Landscape Layer System Mode with a reserved layer for splines, we always update all the splines since we clear the whole layer first
			bInUpdateOnlySelected = false;
		}
		LandscapeInfo->ApplySplines(bInUpdateOnlySelected, ModifiedComponent);
		GDisableUpdateLandscapeMaterialInstances = false;
	}
}

FScopedSetLandscapeEditingLayer::FScopedSetLandscapeEditingLayer(ALandscape* InLandscape, const FGuid& InLayerGUID, TFunction<void()> InCompletionCallback)
	: Landscape(InLandscape)
	, CompletionCallback(MoveTemp(InCompletionCallback))
{
	if (Landscape.IsValid() && Landscape.Get()->CanHaveLayersContent())
	{
		PreviousLayerGUID = Landscape->GetEditingLayer();
		Landscape->SetEditingLayer(InLayerGUID);
	}
}

FScopedSetLandscapeEditingLayer::~FScopedSetLandscapeEditingLayer()
{
	if (Landscape.IsValid() && Landscape.Get()->CanHaveLayersContent())
	{
		Landscape->SetEditingLayer(PreviousLayerGUID);
		if (CompletionCallback)
		{
			CompletionCallback();
		}
	}
}

bool ALandscape::IsEditingLayerReservedForSplines() const
{
	if (CanHaveLayersContent())
	{
		const FLandscapeLayer* SplinesReservedLayer = GetLandscapeSplinesReservedLayer();
		return SplinesReservedLayer && SplinesReservedLayer->Guid == EditingLayer;
	}
	return false;
}

void ALandscape::SetEditingLayer(const FGuid& InLayerGuid)
{
	ensure(CanHaveLayersContent());
	
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	if (!LandscapeInfo)
	{
		EditingLayer.Invalidate();
		return;
	}

	EditingLayer = InLayerGuid;

	// Propagate Editing Layer to components (will be cached)
	LandscapeInfo->ForAllLandscapeProxies([&](ALandscapeProxy* Proxy)
	{
		for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
		{
			Component->SetEditingLayer(InLayerGuid);
		}
	});
}

void ALandscape::SetGrassUpdateEnabled(bool bInGrassUpdateEnabled)
{
#if WITH_EDITORONLY_DATA
	bGrassUpdateEnabled = bInGrassUpdateEnabled;
#endif
}

const FGuid& ALandscape::GetEditingLayer() const
{
	return EditingLayer;
}

bool ALandscape::IsMaxLayersReached() const
{
	return LandscapeLayers.Num() >= GetDefault<ULandscapeSettings>()->MaxNumberOfLayers;
}

TMap<UTexture2D*, TArray<ULandscapeComponent*>> ALandscape::GenerateComponentsPerHeightmaps() const
{
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();

	TMap<UTexture2D*, TArray<ULandscapeComponent*>> ComponentsPerHeightmaps;

	if (LandscapeInfo != nullptr)
	{
		LandscapeInfo->ForAllLandscapeProxies([&](ALandscapeProxy* Proxy)
		{
			for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
			{
				UTexture2D* ComponentHeightmapTexture = Component->GetHeightmap();
				TArray<ULandscapeComponent*>& ComponentList = ComponentsPerHeightmaps.FindOrAdd(ComponentHeightmapTexture);
				ComponentList.Add(Component);
			}
		});
	}

	return ComponentsPerHeightmaps;
}

void ALandscape::CreateDefaultLayer()
{
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	if (!LandscapeInfo || !CanHaveLayersContent())
	{
		return;
	}

	check(LandscapeLayers.Num() == 0); // We can only call this function if we have no layers

	CreateLayer(FName(TEXT("Layer")));
	// Force update rendering resources
	RequestLayersInitialization();
}

FLandscapeLayer* ALandscape::DuplicateLayerAndMoveBrushes(const FLandscapeLayer& InOtherLayer)
{
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	if (!LandscapeInfo || !CanHaveLayersContent())
	{
		return nullptr;
	}

	Modify();

	FLandscapeLayer NewLayer(InOtherLayer);
	NewLayer.Guid = FGuid::NewGuid();

	// Update owning landscape and reparent to landscape's level if necessary
	for (FLandscapeLayerBrush& Brush : NewLayer.Brushes)
	{
		Brush.SetOwner(this);
	}

	int32 AddedIndex = LandscapeLayers.Add(NewLayer);

	// Create associated layer data in each landscape proxy
	LandscapeInfo->ForAllLandscapeProxies([&NewLayer](ALandscapeProxy* Proxy)
	{
		Proxy->AddLayer(NewLayer.Guid);
	});

	return &LandscapeLayers[AddedIndex];
}

int32 ALandscape::CreateLayer(FName InName)
{
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	if (!LandscapeInfo || IsMaxLayersReached() || !CanHaveLayersContent())
	{
		return INDEX_NONE;
	}

	Modify();
	FLandscapeLayer NewLayer;
	NewLayer.Name = GenerateUniqueLayerName(InName);
	int32 LayerIndex = LandscapeLayers.Add(NewLayer);

	// Create associated layer data in each landscape proxy
	LandscapeInfo->ForAllLandscapeProxies([&NewLayer](ALandscapeProxy* Proxy)
	{
		Proxy->AddLayer(NewLayer.Guid);
	});

	return LayerIndex;
}

void ALandscape::AddLayersToProxy(ALandscapeProxy* InProxy)
{
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	if (!LandscapeInfo || !CanHaveLayersContent())
	{
		return;
	}

	check(InProxy != this);
	check(InProxy != nullptr);

	ForEachLayer([&](FLandscapeLayer& Layer)
	{
		InProxy->AddLayer(Layer.Guid);
	});

	// Force update rendering resources
	RequestLayersInitialization();
}

bool ALandscape::ReorderLayer(int32 InStartingLayerIndex, int32 InDestinationLayerIndex)
{
	if (InStartingLayerIndex != InDestinationLayerIndex &&
		LandscapeLayers.IsValidIndex(InStartingLayerIndex) &&
		LandscapeLayers.IsValidIndex(InDestinationLayerIndex))
	{
		Modify();
		FLandscapeLayer Layer = LandscapeLayers[InStartingLayerIndex];
		LandscapeLayers.RemoveAt(InStartingLayerIndex);
		LandscapeLayers.Insert(Layer, InDestinationLayerIndex);
		RequestLayersContentUpdateForceAll();
		return true;
	}
	return false;
}

FName ALandscape::GenerateUniqueLayerName(FName InName) const
{
	// If we are receiving a unique name, use it.
	if (InName != NAME_None && !LandscapeLayers.ContainsByPredicate([InName](const FLandscapeLayer& Layer) { return Layer.Name == InName; }))
	{
		return InName;
	}

	FString BaseName = InName == NAME_None ? "Layer" : InName.ToString();
	FName NewName;
	int32 LayerIndex = 0;
	do
	{
		++LayerIndex;
		NewName = FName(*FString::Printf(TEXT("%s%d"), *BaseName, LayerIndex));
	} while (LandscapeLayers.ContainsByPredicate([NewName](const FLandscapeLayer& Layer) { return Layer.Name == NewName; }));

	return NewName;
}

bool ALandscape::IsLayerBlendSubstractive(int32 InLayerIndex, const TWeakObjectPtr<ULandscapeLayerInfoObject>& InLayerInfoObj) const
{
	const FLandscapeLayer* Layer = GetLayer(InLayerIndex);

	if (Layer == nullptr)
	{
		return false;
	}

	const bool* AllocationBlend = Layer->WeightmapLayerAllocationBlend.Find(InLayerInfoObj.Get());

	if (AllocationBlend != nullptr)
	{
		return (*AllocationBlend);
	}

	return false;
}

void ALandscape::SetLayerSubstractiveBlendStatus(int32 InLayerIndex, bool InStatus, const TWeakObjectPtr<ULandscapeLayerInfoObject>& InLayerInfoObj)
{
	FLandscapeLayer* Layer = GetLayer(InLayerIndex);

	if (Layer == nullptr)
	{
		return;
	}

	Modify();
	bool* AllocationBlend = Layer->WeightmapLayerAllocationBlend.Find(InLayerInfoObj.Get());

	if (AllocationBlend == nullptr)
	{
		Layer->WeightmapLayerAllocationBlend.Add(InLayerInfoObj.Get(), InStatus);
	}
	else
	{
		*AllocationBlend = InStatus;
	}

	RequestLayersContentUpdateForceAll(ELandscapeLayerUpdateMode::Update_Weightmap_All);
}

bool ALandscape::ReorderLayerBrush(int32 InLayerIndex, int32 InStartingLayerBrushIndex, int32 InDestinationLayerBrushIndex)
{
	if (FLandscapeLayer* Layer = GetLayer(InLayerIndex))
	{
		if (InStartingLayerBrushIndex != InDestinationLayerBrushIndex &&
			Layer->Brushes.IsValidIndex(InStartingLayerBrushIndex) &&
			Layer->Brushes.IsValidIndex(InDestinationLayerBrushIndex))
		{
			Modify();
			FLandscapeLayerBrush MovingBrush = Layer->Brushes[InStartingLayerBrushIndex];
			Layer->Brushes.RemoveAt(InStartingLayerBrushIndex);
			Layer->Brushes.Insert(MovingBrush, InDestinationLayerBrushIndex);
			RequestLayersContentUpdateForceAll();
			return true;
		}
	}
	return false;
}

void ALandscape::AddBrushToLayer(int32 InLayerIndex, ALandscapeBlueprintBrushBase* InBrush)
{
	if (FLandscapeLayer* Layer = GetLayer(InLayerIndex))
	{
		Modify();
		Layer->Brushes.Add(FLandscapeLayerBrush(InBrush));
		InBrush->SetOwningLandscape(this);
		RequestLayersContentUpdateForceAll();
	}
}

void ALandscape::RemoveBrush(ALandscapeBlueprintBrushBase* InBrush)
{
	for (int32 LayerIndex = 0; LayerIndex < LandscapeLayers.Num(); ++LayerIndex)
	{
		RemoveBrushFromLayer(LayerIndex, InBrush);
	}
}

void ALandscape::RemoveBrushFromLayer(int32 InLayerIndex, ALandscapeBlueprintBrushBase* InBrush)
{
	if (FLandscapeLayer* Layer = GetLayer(InLayerIndex))
	{
		for (int32 i = 0; i < Layer->Brushes.Num(); ++i)
		{
			if (Layer->Brushes[i].GetBrush() == InBrush)
			{
				Modify();
				Layer->Brushes.RemoveAt(i);
				InBrush->SetOwningLandscape(nullptr);
				RequestLayersContentUpdateForceAll();
				break;
			}
		}
	}
}

void ALandscape::OnBlueprintBrushChanged()
{
#if WITH_EDITORONLY_DATA
	LandscapeBlueprintBrushChangedDelegate.Broadcast();
	RequestLayersContentUpdateForceAll();
#endif
}

ALandscapeBlueprintBrushBase* ALandscape::GetBrushForLayer(int32 InLayerIndex, int8 InBrushIndex) const
{
	if (const FLandscapeLayer* Layer = GetLayer(InLayerIndex))
	{
		if (Layer->Brushes.IsValidIndex(InBrushIndex))
		{
			return Layer->Brushes[InBrushIndex].GetBrush();
		}
	}
	return nullptr;
}

TArray<ALandscapeBlueprintBrushBase*> ALandscape::GetBrushesForLayer(int32 InLayerIndex) const
{
	TArray<ALandscapeBlueprintBrushBase*> Brushes;
	if (const FLandscapeLayer* Layer = GetLayer(InLayerIndex))
	{
		Brushes.Reserve(Layer->Brushes.Num());
		for (const FLandscapeLayerBrush& Brush : Layer->Brushes)
		{
			Brushes.Add(Brush.GetBrush());
		}
	}
	return Brushes;
}

ALandscapeBlueprintBrushBase* FLandscapeLayerBrush::GetBrush() const
{
#if WITH_EDITORONLY_DATA
	return BlueprintBrush;
#else
	return nullptr;
#endif
}

void FLandscapeLayerBrush::SetOwner(ALandscape* InOwner)
{
#if WITH_EDITORONLY_DATA
	if (BlueprintBrush && InOwner)
	{
		if (BlueprintBrush->GetTypedOuter<ULevel>() != InOwner->GetTypedOuter<ULevel>())
		{
			BlueprintBrush->Rename(nullptr, InOwner->GetTypedOuter<ULevel>());
		}
		BlueprintBrush->SetOwningLandscape(InOwner);
	}
#endif
}

bool FLandscapeLayerBrush::IsAffectingHeightmap() const
{
#if WITH_EDITORONLY_DATA
	return BlueprintBrush && BlueprintBrush->IsVisible() && BlueprintBrush->IsAffectingHeightmap();
#else
	return false;
#endif
}

bool FLandscapeLayerBrush::IsAffectingWeightmapLayer(const FName& InWeightmapLayerName) const
{
#if WITH_EDITORONLY_DATA
	return BlueprintBrush && BlueprintBrush->IsVisible() && BlueprintBrush->IsAffectingWeightmap() && BlueprintBrush->IsAffectingWeightmapLayer(InWeightmapLayerName);
#else
	return false;
#endif
}

UTextureRenderTarget2D* FLandscapeLayerBrush::Render(bool InIsHeightmap, const FIntRect& InLandscapeExtent, UTextureRenderTarget2D* InLandscapeRenderTarget, const FName& InWeightmapLayerName)
{
#if WITH_EDITORONLY_DATA
	if ((InIsHeightmap && !IsAffectingHeightmap()) ||
		(!InIsHeightmap && !IsAffectingWeightmapLayer(InWeightmapLayerName)))
	{
		return nullptr;
	}
	if (Initialize(InLandscapeExtent, InLandscapeRenderTarget))
	{
		TGuardValue<bool> AutoRestore(GAllowActorScriptExecutionInEditor, true);
		return BlueprintBrush->Render(InIsHeightmap, InLandscapeRenderTarget, InWeightmapLayerName);
	}
#endif
	return nullptr;
}

bool FLandscapeLayerBrush::Initialize(const FIntRect& InLandscapeExtent, UTextureRenderTarget2D* InLandscapeRenderTarget)
{
#if WITH_EDITORONLY_DATA
	if (BlueprintBrush && InLandscapeRenderTarget)
	{
		if (ALandscape* Landscape = BlueprintBrush->GetOwningLandscape())
		{
			const FIntPoint NewLandscapeRenderTargetSize = FIntPoint(InLandscapeRenderTarget->SizeX, InLandscapeRenderTarget->SizeY);
			FTransform NewLandscapeTransform = Landscape->GetTransform();
			FVector OffsetVector(InLandscapeExtent.Min.X, InLandscapeExtent.Min.Y, 0.f);
			FVector Translation = NewLandscapeTransform.TransformFVector4(OffsetVector);
			NewLandscapeTransform.SetTranslation(Translation);
			FIntPoint NewLandscapeSize = InLandscapeExtent.Max - InLandscapeExtent.Min;
			if (!LandscapeTransform.Equals(NewLandscapeTransform) || (LandscapeSize != NewLandscapeSize) || LandscapeRenderTargetSize != NewLandscapeRenderTargetSize)
			{
				LandscapeTransform = NewLandscapeTransform;
				LandscapeRenderTargetSize = NewLandscapeRenderTargetSize;
				LandscapeSize = NewLandscapeSize;
				
				TGuardValue<bool> AutoRestore(GAllowActorScriptExecutionInEditor, true);
				BlueprintBrush->Initialize(LandscapeTransform, LandscapeSize, LandscapeRenderTargetSize);
			}
			return true;
		}
	}
#endif
	return false;
}

#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
