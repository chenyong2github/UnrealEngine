// Copyright Epic Games, Inc. All Rights Reserved.

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
#include "LandscapeEditReadback.h"

#include "Shader.h"
#include "GlobalShader.h"
#include "ShaderParameterUtils.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Logging/MessageLog.h"
#include "RenderCaptureInterface.h"

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
#include "LandscapeSplinesComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/MapErrors.h"
#include "Misc/UObjectToken.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopedSlowTask.h"
#include "Editor.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#endif

#define LOCTEXT_NAMESPACE "Landscape"

// Channel remapping
extern const size_t ChannelOffsets[4];

ENGINE_API extern bool GDisableAutomaticTextureMaterialUpdateDependencies;

// GPU profiling stats
DECLARE_GPU_STAT_NAMED(LandscapeLayers_Clear, TEXT("Landscape Layer Clear"));
DECLARE_GPU_STAT_NAMED(LandscapeLayers_Render, TEXT("Landscape Layer Render"));
DECLARE_GPU_STAT_NAMED(LandscapeLayers_CopyTexture, TEXT("Landscape Layer Copy Texture"));
DECLARE_GPU_STAT_NAMED(LandscapeLayers_CopyTexturePS, TEXT("Landscape Layer Copy Texture PS"));
DECLARE_GPU_STAT_NAMED(LandscapeLayers_ExtractLayers, TEXT("Landscape Extract Layers"));
DECLARE_GPU_STAT_NAMED(LandscapeLayers_PackLayers, TEXT("Landscape Pack Layers"));

#if WITH_EDITOR
static TAutoConsoleVariable<int32> CVarForceLayersUpdate(
	TEXT("landscape.ForceLayersUpdate"),
	0,
	TEXT("This will force landscape edit layers to be update every frame, rather than when requested only."));

int32 RenderCaptureLayersNextHeightmapDraws = 0;
static FAutoConsoleVariableRef CVarRenderCaptureLayersNextHeightmapDraws(
	TEXT("landscape.RenderCaptureLayersNextHeightmapDraws"),
	RenderCaptureLayersNextHeightmapDraws,
	TEXT("Trigger a render capture during the next heightmap draw calls."));

int32 RenderCaptureLayersNextWeightmapDraws = 0;
static FAutoConsoleVariableRef CVarRenderCaptureLayersNextWeightmapDraws(
	TEXT("landscape.RenderCaptureLayersNextWeightmapDraws"),
	RenderCaptureLayersNextWeightmapDraws,
	TEXT("Trigger a render capture during the next weightmap draw calls."));

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

static TAutoConsoleVariable<int32> CVarLandscapeLayerOptim(
	TEXT("landscape.Optim"),
	1,
	TEXT("This will enable landscape layers optim."));

static TAutoConsoleVariable<int32> CVarLandscapeLayerBrushOptim(
	TEXT("landscape.BrushOptim"),
	0,
	TEXT("This will enable landscape layers optim."));

static TAutoConsoleVariable<int32> CVarLandscapeOutputDiffBitmap(
	TEXT("landscape.OutputDiffBitmap"),
	0,
	TEXT("This will save images for readback textures that have changed in the last layer blend phase. (= 1 Heightmap Diff, = 2 Weightmap Diff, = 3 All Diffs"));

TAutoConsoleVariable<int32> CVarLandscapeShowDirty(
	TEXT("landscape.ShowDirty"),
	0,
	TEXT("This will highlight the data that has changed during the layer blend phase."));

TAutoConsoleVariable<int32> CVarLandscapeTrackDirty(
	TEXT("landscape.TrackDirty"),
	0,
	TEXT("This will track the accumulation of data changes during the layer blend phase."));

TAutoConsoleVariable<int32> CVarLandscapeForceFlush(
	TEXT("landscape.ForceFlush"),
	0,
	TEXT("This will force a render flush every frame when landscape editing."));

struct FLandscapeDirty
{
	FLandscapeDirty()
		: ClearDiffConsoleCommand(
			TEXT("Landscape.ClearDirty"),
			TEXT("Clears all Landscape Dirty Debug Data"),
			FConsoleCommandDelegate::CreateRaw(this, &FLandscapeDirty::ClearDirty))
	{
	}

private:
	FAutoConsoleCommand ClearDiffConsoleCommand;

	void ClearDirty()
	{
		if(!GWorld || GWorld->IsGameWorld())
		{ 
			return;
		}

		auto& LandscapeInfoMap = ULandscapeInfoMap::GetLandscapeInfoMap(GWorld);
		for (TPair<FGuid, ULandscapeInfo*>& Pair : LandscapeInfoMap.Map)
		{
			if (Pair.Value)
			{
				Pair.Value->ClearDirtyData();
			}
		}

		UE_LOG(LogLandscape, Display, TEXT("Landscape.Dirty: Cleared"));
	}
};

FLandscapeDirty GLandscapeDebugDirty;

/**
 * Mapping between heightmaps/weightmaps and components.
 * It's not safe to persist this across frames, so we recalculate at the start of each update.
 */
struct FTextureToComponentHelper
{
	FTextureToComponentHelper(ULandscapeInfo const& InLandscapeInfo)
	{
		InLandscapeInfo.ForAllLandscapeComponents([&](ULandscapeComponent* Component)
		{
			LandscapeComponents.Add(Component);

			{
				UTexture2D* Heightmap = Component->GetHeightmap();
				check(Heightmap != nullptr);

				Heightmaps.Add(Heightmap);
				HeightmapToComponents.FindOrAdd(Heightmap).Add(Component);
			}

			{
				TArray<UTexture2D*>& WeightmapTextures = Component->GetWeightmapTextures();
				TArray<FWeightmapLayerAllocationInfo>& AllocInfos = Component->GetWeightmapLayerAllocations();

				for (FWeightmapLayerAllocationInfo const& AllocInfo : AllocInfos)
				{
					if(AllocInfo.IsAllocated() && AllocInfo.WeightmapTextureIndex < WeightmapTextures.Num())
					{
						UTexture2D* Weightmap = WeightmapTextures[AllocInfo.WeightmapTextureIndex];
						check(Weightmap != nullptr);

						Weightmaps.Add(Weightmap);
						WeightmapToComponents.FindOrAdd(Weightmap).AddUnique(Component);
					}
				}
			}
		});
	}

	TArray< ULandscapeComponent* > LandscapeComponents;
	TSet< UTexture2D* > Heightmaps;
	TMap< UTexture2D*, TArray<ULandscapeComponent*> > HeightmapToComponents;
	TSet< UTexture2D* > Weightmaps;
	TMap< UTexture2D*, TArray<ULandscapeComponent*> > WeightmapToComponents;
};
#endif

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
		ETextureCreateFlags Flags = TexCreate_None;

		if (CreateUAV)
		{
			Flags |= TexCreate_RenderTargetable|TexCreate_UAV;
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
		ETextureCreateFlags Flags = TexCreate_NoTiling | TexCreate_OfflineProcessed;

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
		SetShaderValue(RHICmdList, RHICmdList.GetBoundVertexShader(), TransformParam, InProjectionMatrix);
	}

private:
	LAYOUT_FIELD(FShaderParameter, TransformParam);
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
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), ReadTexture1Param, ReadTexture1SamplerParam, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InParams.ReadHeightmap1->Resource->TextureRHI);
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), ReadTexture2Param, ReadTexture2SamplerParam, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InParams.ReadHeightmap2 != nullptr ? InParams.ReadHeightmap2->Resource->TextureRHI : GWhiteTexture->TextureRHI);

		FVector4 LayerInfo(InParams.LayerAlpha, InParams.LayerVisible ? 1.0f : 0.0f, InParams.LayerBlendMode == LSBM_AlphaBlend ? 1.0f : 0.f, 0.f);
		FVector4 OutputConfig(InParams.ApplyLayerModifiers ? 1.0f : 0.0f, 0.0f /*unused*/, InParams.ReadHeightmap2 ? 1.0f : 0.0f, InParams.GenerateNormals ? 1.0f : 0.0f);
		FVector2D TextureSize(InParams.HeightmapSize.X, InParams.HeightmapSize.Y);

		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), LayerInfoParam, LayerInfo);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), OutputConfigParam, OutputConfig);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), TextureSizeParam, TextureSize);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), LandscapeGridScaleParam, InParams.GridSize);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), ComponentVertexCountParam, (float)InParams.CurrentMipComponentVertexCount);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture1Param);
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture2Param);
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture1SamplerParam);
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture2SamplerParam);
	LAYOUT_FIELD(FShaderParameter, LayerInfoParam);
	LAYOUT_FIELD(FShaderParameter, OutputConfigParam);
	LAYOUT_FIELD(FShaderParameter, TextureSizeParam);
	LAYOUT_FIELD(FShaderParameter, LandscapeGridScaleParam);
	LAYOUT_FIELD(FShaderParameter, ComponentVertexCountParam);
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
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), ReadTexture1Param, ReadTexture1SamplerParam, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InParams.ReadHeightmap1->Resource->TextureRHI);

		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), CurrentMipSizeParam, FVector2D(InParams.CurrentMipSize.X, InParams.CurrentMipSize.Y));
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), ParentMipSizeParam, FVector2D(InParams.ParentMipSize.X, InParams.ParentMipSize.Y));
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), CurrentMipComponentVertexCountParam, (float)InParams.CurrentMipComponentVertexCount);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture1Param);
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture1SamplerParam);
	LAYOUT_FIELD(FShaderParameter, CurrentMipSizeParam);
	LAYOUT_FIELD(FShaderParameter, ParentMipSizeParam);
	LAYOUT_FIELD(FShaderParameter, CurrentMipComponentVertexCountParam);
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
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), ReadTexture1Param, ReadTexture1SamplerParam, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InParams.ReadWeightmap1->Resource->TextureRHI);
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), ReadTexture2Param, ReadTexture2SamplerParam, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InParams.ReadWeightmap2 != nullptr ? InParams.ReadWeightmap2->Resource->TextureRHI : GWhiteTexture->TextureRHI);

		FVector4 LayerInfo(InParams.LayerAlpha, InParams.LayerVisible ? 1.0f : 0.0f, InParams.LayerBlendMode == LSBM_AlphaBlend ? 1.0f : 0.f, 0.f);
		FVector4 OutputConfig(InParams.ApplyLayerModifiers ? 1.0f : 0.0f, InParams.OutputAsSubstractive ? 1.0f : 0.0f, InParams.ReadWeightmap2 != nullptr ? 1.0f : 0.0f, InParams.OutputAsNormalized ? 1.0f : 0.0f);

		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), LayerInfoParam, LayerInfo);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), OutputConfigParam, OutputConfig);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), ComponentVertexCountParam, (float)InParams.CurrentMipComponentVertexCount);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture1Param);
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture2Param);
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture1SamplerParam);
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture2SamplerParam);
	LAYOUT_FIELD(FShaderParameter, LayerInfoParam);
	LAYOUT_FIELD(FShaderParameter, OutputConfigParam);
	LAYOUT_FIELD(FShaderParameter, ComponentVertexCountParam);
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
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), ReadTexture1Param, ReadTexture1SamplerParam, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), InParams.ReadWeightmap1->Resource->TextureRHI);

		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), CurrentMipSizeParam, FVector2D(InParams.CurrentMipSize.X, InParams.CurrentMipSize.Y));
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), ParentMipSizeParam, FVector2D(InParams.ParentMipSize.X, InParams.ParentMipSize.Y));
		SetShaderValue(RHICmdList, RHICmdList.GetBoundPixelShader(), CurrentMipComponentVertexCountParam, (float)InParams.CurrentMipComponentVertexCount);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture1Param);
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture1SamplerParam);
	LAYOUT_FIELD(FShaderParameter, CurrentMipSizeParam);
	LAYOUT_FIELD(FShaderParameter, ParentMipSizeParam);
	LAYOUT_FIELD(FShaderParameter, CurrentMipComponentVertexCountParam);
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeLayersWeightmapMipsPS, "/Engine/Private/LandscapeLayersPS.usf", "PSWeightmapMainMips", SF_Pixel);

class FLandscapeCopyTextureVS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FLandscapeCopyTextureVS);

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters&)
	{
		return true;
	}

	FLandscapeCopyTextureVS() 
	{};
	
	FLandscapeCopyTextureVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{}
};

class FLandscapeCopyTexturePS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FLandscapeCopyTexturePS);
public:

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5) && !IsConsolePlatform(Parameters.Platform) && !IsMetalMobilePlatform(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
	}

	FLandscapeCopyTexturePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		ReadTexture1Param.Bind(Initializer.ParameterMap, TEXT("ReadTexture1"));
		ReadTexture1SamplerParam.Bind(Initializer.ParameterMap, TEXT("ReadTexture1Sampler"));
	}

	FLandscapeCopyTexturePS()
	{}

	void SetParameters(FRHICommandList& RHICmdList, FRHITexture* TextureRHI)
	{
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundPixelShader(), ReadTexture1Param, ReadTexture1SamplerParam, TStaticSamplerState<SF_Point, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI(), TextureRHI);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture1Param);
	LAYOUT_FIELD(FShaderResourceParameter, ReadTexture1SamplerParam);
};

IMPLEMENT_GLOBAL_SHADER(FLandscapeCopyTextureVS, "/Engine/Private/LandscapeLayersPS.usf", "CopyTextureVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FLandscapeCopyTexturePS, "/Engine/Private/LandscapeLayersPS.usf", "CopyTexturePS", SF_Pixel);

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
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ComponentWeightmapParam, InParams.ComponentWeightmapResource->TextureRHI);
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), AtlasPaintListsParam, InParams.AtlasWeightmapsPerLayer->TextureUAV);
		SetSRVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ComponentsDataParam, InParams.ComputeShaderResource->ComponentsDataSRV);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundComputeShader(), ComponentSizeParam, InParams.ComponentSize);
	}

	void UnsetParameters(FRHICommandList& RHICmdList)
	{
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), AtlasPaintListsParam, nullptr);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, ComponentWeightmapParam);
	LAYOUT_FIELD(FShaderResourceParameter, AtlasPaintListsParam);
	LAYOUT_FIELD(FShaderResourceParameter, ComponentsDataParam);
	LAYOUT_FIELD(FShaderParameter, ComponentSizeParam);
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
		SCOPED_GPU_STAT(InRHICmdList, LandscapeLayers_ExtractLayers);
		SCOPED_DRAW_EVENTF(InRHICmdList, LandscapeLayers, TEXT("LandscapeLayers_ExtractLayers"));

		TShaderMapRef<FLandscapeLayerWeightmapExtractMaterialLayersCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		InRHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
		ComputeShader->SetParameters(InRHICmdList, ShaderParams);

		uint32 ThreadGroupCountX = FMath::CeilToInt((float)ShaderParams.ComponentSize / (float)GLandscapeLayerWeightmapThreadGroupSizeX);
		uint32 ThreadGroupCountY = FMath::CeilToInt((float)ShaderParams.ComponentSize / (float)GLandscapeLayerWeightmapThreadGroupSizeY);
		check(ThreadGroupCountX > 0 && ThreadGroupCountY > 0);

		DispatchComputeShader(InRHICmdList, ComputeShader.GetShader(), ThreadGroupCountX, ThreadGroupCountY, ShaderParams.ComputeShaderResource->GetComponentsDataCount());
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
		WeightmapWeightBlendMode = RHICreateVertexBuffer(WeightBlendMemSize, BUF_ShaderResource | BUF_Volatile, WeightBlendCreateInfo);
		WeightmapWeightBlendModeSRV = RHICreateShaderResourceView(WeightmapWeightBlendMode, sizeof(float), PF_R32_FLOAT);

		void* WeightmapWeightBlendModePtr = RHILockVertexBuffer(WeightmapWeightBlendMode, 0, WeightBlendMemSize, RLM_WriteOnly);
		FMemory::Memcpy(WeightmapWeightBlendModePtr, OriginalWeightmapWeightBlendModeData.GetData(), WeightBlendMemSize);
		RHIUnlockVertexBuffer(WeightmapWeightBlendMode);

		FRHIResourceCreateInfo TextureOutputOffsetCreateInfo;
		uint32 TextureOutputOffsetMemSize = OriginalTextureOutputOffset.Num() * sizeof(FVector2D);
		WeightmapTextureOutputOffset = RHICreateVertexBuffer(TextureOutputOffsetMemSize, BUF_ShaderResource | BUF_Volatile, TextureOutputOffsetCreateInfo);
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
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ComponentWeightmapParam, InParams.ComponentWeightmapResource->TextureUAV);
		SetTextureParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), AtlasPaintListsParam, InParams.AtlasWeightmapsPerLayer->TextureRHI);
		SetSRVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ComponentsDataParam, InParams.ComputeShaderResource->ComponentsDataSRV);
		SetShaderValue(RHICmdList, RHICmdList.GetBoundComputeShader(), ComponentSizeParam, InParams.ComponentSize);
		SetSRVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), WeightmapWeightBlendModeParam, InParams.ComputeShaderResource->WeightmapWeightBlendModeSRV);
		SetSRVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), WeightmapTextureOutputOffsetParam, InParams.ComputeShaderResource->WeightmapTextureOutputOffsetSRV);
	}

	void UnsetParameters(FRHICommandList& RHICmdList)
	{
		SetUAVParameter(RHICmdList, RHICmdList.GetBoundComputeShader(), ComponentWeightmapParam, nullptr);
	}

private:
	LAYOUT_FIELD(FShaderResourceParameter, ComponentWeightmapParam);
	LAYOUT_FIELD(FShaderResourceParameter, AtlasPaintListsParam);
	LAYOUT_FIELD(FShaderResourceParameter, ComponentsDataParam);
	LAYOUT_FIELD(FShaderParameter, ComponentSizeParam);
	LAYOUT_FIELD(FShaderResourceParameter, WeightmapWeightBlendModeParam);
	LAYOUT_FIELD(FShaderResourceParameter, WeightmapTextureOutputOffsetParam);
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
		SCOPED_GPU_STAT(InRHICmdList, LandscapeLayers_PackLayers);
		SCOPED_DRAW_EVENTF(InRHICmdList, LandscapeLayers, TEXT("LandscapeLayers_PackLayers"));

		TShaderMapRef<FLandscapeLayerWeightmapPackMaterialLayersCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
		InRHICmdList.SetComputeShader(ComputeShader.GetComputeShader());
		ComputeShader->SetParameters(InRHICmdList, ShaderParams);

		uint32 ThreadGroupCountX = FMath::CeilToInt((float)ShaderParams.ComponentSize / (float)GLandscapeLayerWeightmapThreadGroupSizeX);
		uint32 ThreadGroupCountY = FMath::CeilToInt((float)ShaderParams.ComponentSize / (float)GLandscapeLayerWeightmapThreadGroupSizeY);
		check(ThreadGroupCountX > 0 && ThreadGroupCountY > 0);

		DispatchComputeShader(InRHICmdList, ComputeShader.GetShader(), ThreadGroupCountX, ThreadGroupCountY, ShaderParams.ComputeShaderResource->GetComponentsDataCount());
		ComputeShader->UnsetParameters(InRHICmdList);
		ShaderParams.ComputeShaderResource->ReleaseResource();
		delete ShaderParams.ComputeShaderResource;
	}

private:
	FLandscapeLayerWeightmapPackMaterialLayersComputeShaderParameters ShaderParams;
};

// Copy texture render command

struct FLandscapeLayersCopyTextureParams
{
	FLandscapeLayersCopyTextureParams(UTexture* InSourceTexture, UTexture* InDestTexture)
	{
		if (InSourceTexture != nullptr)
		{
			SourceResourceDebugName = InSourceTexture->GetName();
			SourceResource = InSourceTexture->Resource;
		}
		if (InDestTexture != nullptr)
		{
			DestResourceDebugName = InDestTexture->GetName();
			DestResource = InDestTexture->Resource;
		}
	}

	FLandscapeLayersCopyTextureParams(const FString& InSourceResourceDebugName, FTextureResource* InSourceResource, const FString& InDestResourceDebugName, FTextureResource* InDestResource)
		: SourceResourceDebugName(InSourceResourceDebugName)
		, SourceResource(InSourceResource)
		, DestResourceDebugName(InDestResourceDebugName)
		, DestResource(InDestResource)
	{}

	FLandscapeLayersCopyTextureParams(const FLandscapeLayersCopyTextureParams&) = default;
	FLandscapeLayersCopyTextureParams(FLandscapeLayersCopyTextureParams&&) = default;
	FLandscapeLayersCopyTextureParams& operator=(FLandscapeLayersCopyTextureParams&&) = default;

	FString SourceResourceDebugName;
	FTextureResource* SourceResource = nullptr;
	FString DestResourceDebugName;
	FTextureResource* DestResource = nullptr;
	FIntPoint CopySize = FIntPoint(0, 0);
	FIntPoint SourcePosition = FIntPoint(0, 0);
	FIntPoint DestPosition = FIntPoint(0, 0);
	uint8 SourceMip = 0;
	uint8 DestMip = 0;
	uint32 SourceArrayIndex = 0;
	uint32 DestArrayIndex = 0;
	ERHIAccess SourceAccess = ERHIAccess::SRVMask;
	ERHIAccess DestAccess = ERHIAccess::SRVMask;
};

class FLandscapeLayersCopyTexture_RenderThread
{
public:
	FLandscapeLayersCopyTexture_RenderThread(const FLandscapeLayersCopyTextureParams& InParams)
		: Params(InParams)
	{}	

	void Copy(FRHICommandListImmediate& InRHICmdList)
	{
		SCOPED_GPU_STAT(InRHICmdList, LandscapeLayers_CopyTexture);
		SCOPED_DRAW_EVENTF(InRHICmdList, LandscapeLayers, TEXT("LandscapeLayers_Copy %s -> %s, Mip (%d -> %d), Array Index (%d -> %d)"), *Params.SourceResourceDebugName, *Params.DestResourceDebugName, Params.SourceMip, Params.DestMip, Params.SourceArrayIndex, Params.DestArrayIndex);

		FIntPoint SourceSize(Params.SourceResource->GetSizeX() >> Params.SourceMip, Params.SourceResource->GetSizeY() >> Params.SourceMip);
		FIntPoint DestSize(Params.DestResource->GetSizeX() >> Params.DestMip, Params.DestResource->GetSizeY() >> Params.DestMip);

		FRHICopyTextureInfo Info;
		Info.NumSlices = 1;
		// If CopySize is passed, used that as the size (and don't adjust with the mip level : consider that the user has computed it properly) : 
		Info.Size.X = (Params.CopySize.X > 0) ? Params.CopySize.X : SourceSize.X;
		Info.Size.Y = (Params.CopySize.Y > 0) ? Params.CopySize.Y : SourceSize.Y;
		Info.Size.Z = 1;
		Info.SourcePosition.X = Params.SourcePosition.X;
		Info.SourcePosition.Y = Params.SourcePosition.Y;
		Info.DestPosition.X = Params.DestPosition.X;
		Info.DestPosition.Y = Params.DestPosition.Y;
		Info.SourceSliceIndex = Params.SourceArrayIndex;
		Info.DestSliceIndex = Params.DestArrayIndex;
		Info.SourceMipIndex = Params.SourceMip; 
		Info.DestMipIndex = Params.DestMip;

		check((Info.SourcePosition.X >= 0) && (Info.SourcePosition.Y >= 0) && (Info.DestPosition.X >= 0) && (Info.DestPosition.Y >= 0));
		check(Info.SourcePosition.X + Info.Size.X <= SourceSize.X);
		check(Info.SourcePosition.Y + Info.Size.Y <= SourceSize.Y);
		check(Info.DestPosition.X + Info.Size.X <= DestSize.X);
		check(Info.DestPosition.Y + Info.Size.Y <= DestSize.Y);

		InRHICmdList.Transition(FRHITransitionInfo(Params.SourceResource->TextureRHI, Params.SourceAccess, ERHIAccess::CopySrc));
		InRHICmdList.Transition(FRHITransitionInfo(Params.DestResource->TextureRHI, Params.DestAccess, ERHIAccess::CopyDest));
		InRHICmdList.CopyTexture(Params.SourceResource->TextureRHI, Params.DestResource->TextureRHI, Info);
		InRHICmdList.Transition(FRHITransitionInfo(Params.SourceResource->TextureRHI, ERHIAccess::CopySrc, Params.SourceAccess));
		InRHICmdList.Transition(FRHITransitionInfo(Params.DestResource->TextureRHI, ERHIAccess::CopyDest, Params.DestAccess));
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
		SCOPED_GPU_STAT(InRHICmdList, LandscapeLayers_Clear);
		SCOPED_DRAW_EVENTF(InRHICmdList, LandscapeLayers, TEXT("LandscapeLayers_Clear %s"), DebugName.Len() > 0 ? *DebugName : TEXT(""));

		check(IsInRenderingThread());

		InRHICmdList.Transition(FRHITransitionInfo(RenderTargetResource->TextureRHI, ERHIAccess::SRVMask, ERHIAccess::RTV));
		FRHIRenderPassInfo RPInfo(RenderTargetResource->TextureRHI, ERenderTargetActions::Clear_Store);
		InRHICmdList.BeginRenderPass(RPInfo, TEXT("Clear"));
		InRHICmdList.EndRenderPass();
		InRHICmdList.Transition(FRHITransitionInfo(RenderTargetResource->TextureRHI, ERHIAccess::RTV, ERHIAccess::SRVMask));
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
		SCOPED_GPU_STAT(InRHICmdList, LandscapeLayers_Render);
		SCOPED_DRAW_EVENTF(InRHICmdList, LandscapeLayers, TEXT("LandscapeLayers_Render %s"), DebugName.Len() > 0 ? *DebugName : TEXT(""));
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

		InRHICmdList.Transition(FRHITransitionInfo(ViewFamily.RenderTarget->GetRenderTargetTexture(), ERHIAccess::SRVMask, ERHIAccess::RTV));

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

			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

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

			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

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
		InRHICmdList.Transition(FRHITransitionInfo(ViewFamily.RenderTarget->GetRenderTargetTexture(), ERHIAccess::RTV, ERHIAccess::SRVMask));

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

#if WITH_EDITOR

void ALandscape::ShowEditLayersResourcesNotification(const FText& InText, TWeakPtr<SNotificationItem>& NotificationItem)
{
	TSharedPtr<SNotificationItem> PinnedItem = NotificationItem.Pin();
	if (!PinnedItem.IsValid())
	{
		FNotificationInfo Info(InText);
		Info.bUseThrobber = true;
		PinnedItem = FSlateNotificationManager::Get().AddNotification(Info);
		NotificationItem = PinnedItem;
	}
	else
	{
		PinnedItem->SetText(InText);
	}
	PinnedItem->SetCompletionState(SNotificationItem::ECompletionState::CS_Pending);
	PinnedItem->SetExpireDuration(1.0f);
	PinnedItem->ExpireAndFadeout();
}

void ALandscape::HideEditLayersResourcesNotification(TWeakPtr<SNotificationItem>& InNotificationItem)
{
	TSharedPtr<SNotificationItem> PinnedItem = InNotificationItem.Pin();
	if (PinnedItem.IsValid() && (PinnedItem->GetCompletionState() != SNotificationItem::ECompletionState::CS_Success))
	{
		PinnedItem->SetCompletionState(SNotificationItem::ECompletionState::CS_Success);
		PinnedItem->SetExpireDuration(1.0f);
		PinnedItem->ExpireAndFadeout();
	}
}

bool ALandscape::IsStreamableAssetFullyStreamedIn(UStreamableRenderAsset* InStreamableAsset, bool bInWaitForStreaming) const
{
	check(InStreamableAsset);
	InStreamableAsset->bForceMiplevelsToBeResident = true;
	if (bInWaitForStreaming && !InStreamableAsset->IsFullyStreamedIn())
	{
		InStreamableAsset->WaitForStreaming();
	}
	return InStreamableAsset->IsFullyStreamedIn();
}

bool ALandscape::IsMaterialResourceCompiled(FMaterialResource* InMaterialResource, bool bInWaitForCompilation) const
{
	check(InMaterialResource);
	if (bInWaitForCompilation && !InMaterialResource->HasValidGameThreadShaderMap())
	{
		InMaterialResource->FinishCompilation();
	}
	return InMaterialResource->HasValidGameThreadShaderMap();
}

bool ALandscape::ComputeLandscapeLayerBrushInfo(FTransform& OutLandscapeTransform, FIntPoint& OutLandscapeSize, FIntPoint& OutLandscapeRenderTargetSize)
{
	ULandscapeInfo* Info = GetLandscapeInfo();
	if (Info == nullptr)
	{
		return false;
	}

	FIntRect LandscapeExtent;
	if (!Info->GetLandscapeExtent(LandscapeExtent.Min.X, LandscapeExtent.Min.Y, LandscapeExtent.Max.X, LandscapeExtent.Max.Y))
	{
		return false;
	}

	ALandscape* Landscape = GetLandscapeActor();
	if (Landscape == nullptr)
	{
		return false;
	}
	
	OutLandscapeTransform = Landscape->GetTransform();
	FVector OffsetVector(LandscapeExtent.Min.X, LandscapeExtent.Min.Y, 0.f);
	FVector Translation = OutLandscapeTransform.TransformFVector4(OffsetVector);
	OutLandscapeTransform.SetTranslation(Translation);
	OutLandscapeSize = LandscapeExtent.Max - LandscapeExtent.Min;

	const FIntPoint ComponentCounts = ComputeComponentCounts();
	OutLandscapeRenderTargetSize.X = FMath::RoundUpToPowerOfTwo(((SubsectionSizeQuads + 1) * NumSubsections) * ComponentCounts.X);
	OutLandscapeRenderTargetSize.Y = FMath::RoundUpToPowerOfTwo(((SubsectionSizeQuads + 1) * NumSubsections) * ComponentCounts.Y);

	return true;
}

void ALandscape::CreateLayersRenderingResource()
{
	ULandscapeInfo* Info = GetLandscapeInfo();
	if (Info == nullptr)
	{
		return;
	}

	const FIntPoint ComponentCounts = ComputeComponentCounts();

	ALandscape* Landscape = GetLandscapeActor();
	check(Landscape);

	if (Landscape->HeightmapRTList.Num() == 0)
	{
		Landscape->HeightmapRTList.Init(nullptr, (int32)EHeightmapRTType::HeightmapRT_Count);

		int32 CurrentMipSizeX = ((SubsectionSizeQuads + 1) * NumSubsections) * ComponentCounts.X;
		int32 CurrentMipSizeY = ((SubsectionSizeQuads + 1) * NumSubsections) * ComponentCounts.Y;

		for (int32 i = 0; i < (int32)EHeightmapRTType::HeightmapRT_Count; ++i)
		{
			FText DisplayName = StaticEnum<EHeightmapRTType>()->GetDisplayValueAsText((EHeightmapRTType)i);
			FName RTName = MakeUniqueObjectName(GetTransientPackage(), UTextureRenderTarget2D::StaticClass(), FName(*DisplayName.ToString()));
			Landscape->HeightmapRTList[i] = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), RTName, RF_Transient);
			check(Landscape->HeightmapRTList[i]);
			Landscape->HeightmapRTList[i]->RenderTargetFormat = RTF_RGBA8;
			Landscape->HeightmapRTList[i]->AddressX = TextureAddress::TA_Clamp;
			Landscape->HeightmapRTList[i]->AddressY = TextureAddress::TA_Clamp;
			Landscape->HeightmapRTList[i]->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

			if (i < (int32)EHeightmapRTType::HeightmapRT_Mip1) // Landscape size RT
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

		for (int32 i = 0; i < (int32)EHeightmapRTType::HeightmapRT_Count; ++i)
		{
			if (i < (int32)EHeightmapRTType::HeightmapRT_Mip1) // Landscape size RT
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
		Landscape->WeightmapRTList.Init(nullptr, (int32)EWeightmapRTType::WeightmapRT_Count);

		int32 CurrentMipSizeX = ((SubsectionSizeQuads + 1) * NumSubsections) * ComponentCounts.X;
		int32 CurrentMipSizeY = ((SubsectionSizeQuads + 1) * NumSubsections) * ComponentCounts.Y;

		for (int32 i = 0; i < (int32)EWeightmapRTType::WeightmapRT_Count; ++i)
		{
			FText DisplayName = StaticEnum<EHeightmapRTType>()->GetDisplayValueAsText((EWeightmapRTType)i);
			FName RTName = MakeUniqueObjectName(GetTransientPackage(), UTextureRenderTarget2D::StaticClass(), FName(*DisplayName.ToString()));
			Landscape->WeightmapRTList[i] = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), RTName, RF_Transient);

			check(Landscape->WeightmapRTList[i]);
			Landscape->WeightmapRTList[i]->AddressX = TextureAddress::TA_Clamp;
			Landscape->WeightmapRTList[i]->AddressY = TextureAddress::TA_Clamp;
			Landscape->WeightmapRTList[i]->ClearColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
			Landscape->WeightmapRTList[i]->RenderTargetFormat = RTF_RGBA8;

			if (i < (int32)EWeightmapRTType::WeightmapRT_Mip0) // Landscape size RT, only create the number of layer we have
			{
				Landscape->WeightmapRTList[i]->RenderTargetFormat = (i == (int32)EWeightmapRTType::WeightmapRT_Scratch_RGBA) ? RTF_RGBA8 : RTF_R8;
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

		for (int32 i = 0; i < (int32)EWeightmapRTType::WeightmapRT_Count; ++i)
		{
			if (i < (int32)EWeightmapRTType::WeightmapRT_Mip0) // Landscape size RT, only create the number of layer we have
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
		for (auto& ItPair : Proxy->HeightmapsCPUReadback)
		{
			FLandscapeEditLayerReadback* HeightmapCPUReadback = ItPair.Value;
			delete HeightmapCPUReadback;
		}
		Proxy->HeightmapsCPUReadback.Empty();

		for (auto& ItPair : Proxy->WeightmapsCPUReadback)
		{
			FLandscapeEditLayerReadback* WeightmapCPUReadback = ItPair.Value;
			delete WeightmapCPUReadback;
		}
		Proxy->WeightmapsCPUReadback.Empty();
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

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_Flush_ResourceRelease);
		FlushRenderingCommands();
	}

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
			Allocation.Free();
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

void ExecuteCopyLayersTexture(TArray<FLandscapeLayersCopyTextureParams>&& InCopyTextureParams)
{
	ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_CopyTexture)(
		[CopyTextureParams = MoveTemp(InCopyTextureParams)](FRHICommandListImmediate& RHICmdList) mutable
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RT_CopyTexture);

		for (const FLandscapeLayersCopyTextureParams& Params : CopyTextureParams)
		{
			if ((Params.SourceResource != nullptr) && (Params.DestResource != nullptr))
			{
				FLandscapeLayersCopyTexture_RenderThread CopyTexture(Params);
				CopyTexture.Copy(RHICmdList);
			}
		}
	});
}

/** Per component information from read back results. */
struct FLandscapeEditLayerReadbackResult
{
	/** ELandscapeLayerUpdateMode flags set on ULandscapeComponent at time when read back task was submitted. */
	int32 UpdateModes;
	/** Were the associated heightmap/weightmaps modified. */
	bool bModified;

	FLandscapeEditLayerReadbackResult()
		: UpdateModes(0)
		, bModified(false)
	{}

	FLandscapeEditLayerReadbackResult(int32 InUpdateModes, bool bInModified)
		: UpdateModes(InUpdateModes)
		, bModified(bInModified)
	{}
};

/** Description for a single read back operation. */
struct FLandscapeLayersCopyReadbackTextureParams
{
	FLandscapeLayersCopyReadbackTextureParams(UTexture2D* InSource, FLandscapeEditLayerReadback* InDest)
		: Source(InSource)
		, Dest(InDest)
	{}

	UTexture2D* Source;
	FLandscapeEditLayerReadback* Dest;
	FLandscapeEditLayerReadback::FReadbackContext Context;
};

void ExecuteCopyToReadbackTexture(TArray<FLandscapeLayersCopyReadbackTextureParams>& InParams)
{
	for (FLandscapeLayersCopyReadbackTextureParams& Params : InParams)
	{
		Params.Dest->Enqueue(Params.Source, MoveTemp(Params.Context));
	}
}

void ALandscape::CopyTexturePS(const FString& InSourceDebugName, FTextureResource* InSourceResource, const FString& InDestDebugName, FTextureResource* InDestResource) const
{
	check(InSourceResource != nullptr);
	check(InDestResource != nullptr);
	
	ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_CopyTexturePS)(
		[InSourceDebugName, InSourceResource, InDestDebugName, InDestResource](FRHICommandListImmediate& RHICmdList)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RT_CopyTexturePS);
		SCOPED_GPU_STAT(RHICmdList, LandscapeLayers_CopyTexturePS);
		SCOPED_DRAW_EVENTF(RHICmdList, LandscapeLayers, TEXT("LandscapeLayers_CopyTexturePS %s -> %s"), *InSourceDebugName, *InDestDebugName);

		check(InSourceResource->GetSizeX() == InDestResource->GetSizeX());
		check(InSourceResource->GetSizeY() == InDestResource->GetSizeY());
		FRHIRenderPassInfo RPInfo(InDestResource->TextureRHI, ERenderTargetActions::DontLoad_Store);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("CopyTexture"));

		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef< FLandscapeCopyTextureVS > VertexShader(GlobalShaderMap);
		TShaderMapRef< FLandscapeCopyTexturePS > PixelShader(GlobalShaderMap);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		PixelShader->SetParameters(RHICmdList, InSourceResource->TextureRHI);

		RHICmdList.SetViewport(0.0f, 0.0f, 0.0f, (float)InDestResource->GetSizeX(), (float)InDestResource->GetSizeY(), 1.0f);
		RHICmdList.DrawIndexedPrimitive(GTwoTrianglesIndexBuffer.IndexBufferRHI, 0, 0, 4, 0, 2, 1);

		RHICmdList.EndRenderPass();
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

	ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_RenderWeightmap)(
		[LayersRender, InDebugName, InDrawType, InClearRTWrite](FRHICommandListImmediate& RHICmdList) mutable
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RT_RenderWeightmap);
		SCOPED_DRAW_EVENTF(RHICmdList, LandscapeLayers, TEXT("DrawWeightmapComponentsToRenderTarget %s (%s)"), *InDebugName, *StaticEnum<EHeightmapRTType>()->GetDisplayValueAsText(InDrawType).ToString());
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

	for (int32 MipRTIndex = (int32)EWeightmapRTType::WeightmapRT_Mip1; MipRTIndex < (int32)EWeightmapRTType::WeightmapRT_Count; ++MipRTIndex)
	{
		UTextureRenderTarget2D* WriteMipRT = WeightmapRTList[MipRTIndex];

		if (WriteMipRT != nullptr)
		{
			DrawWeightmapComponentsToRenderTarget(FString::Printf(TEXT("LS Weight: %s = -> %s Mips %d"), *ReadMipRT->GetName(), *WriteMipRT->GetName(), CurrentMip),
													SectionBaseToDraw, WeightmapScaleBias, nullptr, ReadMipRT, nullptr, WriteMipRT, ERTDrawingType::RTMips, InClearRTWrite, InShaderParams, CurrentMip);
			++CurrentMip;
		}

		ReadMipRT = WeightmapRTList[MipRTIndex];
	}
}

void ALandscape::ClearLayersWeightmapTextureResource(const FString& InDebugName, FTextureRenderTargetResource* InTextureResourceToClear) const
{
	LandscapeLayersWeightmapClear_RenderThread LayersClear(InDebugName, InTextureResourceToClear);

	ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_Clear)(
		[LayersClear](FRHICommandListImmediate& RHICmdList) mutable
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RT_Clear);
		LayersClear.Clear(RHICmdList);
	});
}

void ALandscape::DrawHeightmapComponentsToRenderTargetMips(const TArray<ULandscapeComponent*>& InComponentsToDraw, const FIntPoint& InLandscapeBase, UTexture* InReadHeightmap, bool InClearRTWrite, struct FLandscapeLayersHeightmapShaderParameters& InShaderParams) const
{
	int32 CurrentMip = 1;
	UTexture* ReadMipRT = InReadHeightmap;

	for (int32 MipRTIndex = (int32)EHeightmapRTType::HeightmapRT_Mip1; MipRTIndex < (int32)EHeightmapRTType::HeightmapRT_Count; ++MipRTIndex)
	{
		UTextureRenderTarget2D* WriteMipRT = HeightmapRTList[MipRTIndex];

		if (WriteMipRT != nullptr)
		{
			DrawHeightmapComponentsToRenderTarget(FString::Printf(TEXT("LS Height: %s = -> %s CombinedAtlasWithMips %d"), *ReadMipRT->GetName(), *WriteMipRT->GetName(), CurrentMip),
												  InComponentsToDraw, InLandscapeBase, ReadMipRT, nullptr, WriteMipRT, ERTDrawingType::RTMips, InClearRTWrite, InShaderParams, CurrentMip);
			++CurrentMip;
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

	ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_RenderHeightmap)(
		[LayersRender, InDebugName, InDrawType, InClearRTWrite](FRHICommandListImmediate& RHICmdList) mutable
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RT_RenderHeightmap);
		SCOPED_DRAW_EVENTF(RHICmdList, LandscapeLayers, TEXT("DrawHeightmapComponentsToRenderTarget %s (%s)"), *InDebugName, *StaticEnum<EHeightmapRTType>()->GetDisplayValueAsText(InDrawType).ToString());
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
	ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_DebugResolve)(
		[RenderTargetResource](FRHICommandListImmediate& RHICmdList) mutable
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RT_DebugResolve);
		// Copy (resolve) the rendered image from the frame buffer to its render target texture
		RHICmdList.CopyToResolveTarget(RenderTargetResource->GetRenderTargetTexture(), RenderTargetResource->TextureRHI, FResolveParams());
	});

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_Flush_DebugResolve);
		FlushRenderingCommands();
	}

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

	ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_Readback)(
		[InTextureResource, SampleRect = SampleRect, OutData = &OutputTexels, ReadFlags = Flags](FRHICommandListImmediate& RHICmdList) mutable
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RT_Readback);
		RHICmdList.ReadSurfaceData(InTextureResource->TextureRHI, SampleRect, *OutData, ReadFlags);
	});

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_Flush_Readback);
		FlushRenderingCommands();
	}

	if (InOutputHeight)
	{
		PrintLayersDebugHeightData(InContext, OutputTexels, FIntPoint(SampleRect.Width(), SampleRect.Height()), InMipRender, InOutputNormals);
	}
	else
	{
		PrintLayersDebugWeightData(InContext, OutputTexels, FIntPoint(SampleRect.Width(), SampleRect.Height()), InMipRender);
	}
}

bool ALandscape::PrepareLayersBrushResources(bool bInWaitForStreaming, bool bHeightmap) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_PrepareLayersBrushTextureResources);
	TSet<UObject*> Dependencies;
	for (const FLandscapeLayer& Layer : LandscapeLayers)
	{
		for (const FLandscapeLayerBrush& Brush : Layer.Brushes)
		{
			if (ALandscapeBlueprintBrushBase* LandscapeBrush = Brush.GetBrush())
			{
				if ((LandscapeBrush->IsAffectingWeightmap() && !bHeightmap) || (LandscapeBrush->IsAffectingHeightmap() && bHeightmap))
				{
					LandscapeBrush->GetRenderDependencies(Dependencies);
				}
			}
		}
	}

	ERHIFeatureLevel::Type FeatureLevel = GetWorld() ? (ERHIFeatureLevel::Type)GetWorld()->FeatureLevel : GMaxRHIFeatureLevel;
	for (UObject* Dependency : Dependencies)
	{
		// Streamable textures need to be fully streamed in : 
		if (UStreamableRenderAsset* StreamableRenderAsset = Cast<UStreamableRenderAsset>(Dependency))
		{
			if (!IsStreamableAssetFullyStreamedIn(StreamableRenderAsset, bInWaitForStreaming))
			{
				return false;
			}
		}

		// Material shaders need to be fully compiled : 
		if (UMaterialInterface* MaterialInterface = Cast<UMaterialInterface>(Dependency))
		{
			if (FMaterialResource* MaterialResource = MaterialInterface->GetMaterialResource(FeatureLevel))
			{
				if (!IsMaterialResourceCompiled(MaterialResource, bInWaitForStreaming))
				{
					return false;
				}
			}
		}
	}

	return true;
}

bool ALandscape::PrepareLayersHeightmapTextureResources(bool bInWaitForStreaming) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_PrepareLayersHeightmapTextureResources);
	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return false;
	}

	bool IsReady = true;

	Info->ForAllLandscapeProxies([&](ALandscapeProxy* Proxy)
	{
				
		for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
		{
			UTexture2D* ComponentHeightmap = Component->GetHeightmap();

			IsReady &= IsStreamableAssetFullyStreamedIn(ComponentHeightmap, bInWaitForStreaming);

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

						// Explicit call to base class to skip CachePlatormData of UTexture2D
						LayerHeightmap->UTexture::UpdateResource();
					}

					IsReady &= IsStreamableAssetFullyStreamedIn(LayerHeightmap, bInWaitForStreaming);
					IsReady &= bInWaitForStreaming || (LayerHeightmap->Resource != nullptr && LayerHeightmap->Resource->IsInitialized());
				}
			}
		}
	});

	return IsReady;
}

int32 ALandscape::RegenerateLayersHeightmaps(FTextureToComponentHelper const& MapHelper, const TArray<ULandscapeComponent*>& InLandscapeComponentsToRender, const TArray<ULandscapeComponent*>& InLandscapeComponentsToResolve)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RegenerateLayersHeightmaps);
	SCOPE_CYCLE_COUNTER(STAT_LandscapeLayersRegenerateHeightmaps);
	ULandscapeInfo* Info = GetLandscapeInfo();

	const int32 AllHeightmapUpdateModes = (ELandscapeLayerUpdateMode::Update_Heightmap_All | ELandscapeLayerUpdateMode::Update_Heightmap_Editing | ELandscapeLayerUpdateMode::Update_Heightmap_Editing_NoCollision);
	const int32 HeightmapUpdateModes = LayerContentUpdateModes & AllHeightmapUpdateModes;
	const bool bForceRender = CVarForceLayersUpdate.GetValueOnAnyThread() != 0;
	const bool bSkipBrush = CVarLandscapeLayerBrushOptim.GetValueOnAnyThread() == 1 && ((HeightmapUpdateModes & AllHeightmapUpdateModes) == ELandscapeLayerUpdateMode::Update_Heightmap_Editing);

	if ((HeightmapUpdateModes == 0 && !bForceRender) || Info == nullptr)
	{
		return 0;
	}

	// Nothing to do (return that we did the processing)
	if (InLandscapeComponentsToResolve.Num() == 0)
	{
		return HeightmapUpdateModes;
	}

	// Lazily create CPU read back objects as required
	if (HeightmapUpdateModes)
	{
		for(ULandscapeComponent* Component : InLandscapeComponentsToRender)
		{
			UTexture2D* ComponentHeightmap = Component->GetHeightmap(false);
			ALandscapeProxy* Proxy = Component->GetLandscapeProxy();
			FLandscapeEditLayerReadback** CPUReadback = Proxy->HeightmapsCPUReadback.Find(ComponentHeightmap);
			if (CPUReadback == nullptr)
			{
				FLandscapeEditLayerReadback* NewCPUReadback = new FLandscapeEditLayerReadback();
				const uint8* LockedMip = ComponentHeightmap->Source.LockMip(0);
				const uint32 Hash = FLandscapeEditLayerReadback::CalculateHash(LockedMip, ComponentHeightmap->GetSizeX() * ComponentHeightmap->GetSizeY() * sizeof(FColor));
				ComponentHeightmap->Source.UnlockMip(0);
				NewCPUReadback->SetHash(Hash);
				Proxy->HeightmapsCPUReadback.Add(ComponentHeightmap, NewCPUReadback);
			}
		}
	}

	if (HeightmapUpdateModes || bForceRender)
	{
		RenderCaptureInterface::FScopedCapture RenderCapture((RenderCaptureLayersNextHeightmapDraws != 0), TEXT("LandscapeLayersHeightmapCapture"));

		check(HeightmapRTList.Num() > 0);

		FIntRect LandscapeExtent;
		if (!Info->GetLandscapeExtent(LandscapeExtent.Min.X, LandscapeExtent.Min.Y, LandscapeExtent.Max.X, LandscapeExtent.Max.Y))
		{
			return 0;
		}

		// Use to compute top-left vertex position per Heightmap and the actual size to copy : 
		struct FHeightmapCopyInfo
		{
			FHeightmapCopyInfo(UTexture2D* InTexture, const FIntPoint& InComponentVertexPosition, int32 InComponentSizeVerts, FLandscapeEditLayerReadback* InCPUReadback = nullptr)
				: Texture(InTexture)
				, ComponentSizeVerts(InComponentSizeVerts)
				, SectionRect(InComponentVertexPosition, InComponentVertexPosition + FIntPoint(InComponentSizeVerts, InComponentSizeVerts))
				, CPUReadback(InCPUReadback)
			{}

			FHeightmapCopyInfo(FHeightmapCopyInfo&&) = default;
			FHeightmapCopyInfo& operator=(FHeightmapCopyInfo&&) = default;

			void Union(const FIntPoint& InComponentVertexPosition)
			{
				SectionRect.Union(FIntRect(InComponentVertexPosition, InComponentVertexPosition + FIntPoint(ComponentSizeVerts, ComponentSizeVerts)));
			}

			UTexture2D* Texture;
			int32 ComponentSizeVerts;
			FIntRect SectionRect;
			FLandscapeEditLayerReadback* CPUReadback;
		};

		// Calculate Top Left Lambda
		auto GetUniqueHeightmaps = [&](const TArray<ULandscapeComponent*>& InLandscapeComponents, TArray<FHeightmapCopyInfo>& OutHeightmaps, const FIntPoint& LandscapeBaseQuads, const FGuid& InLayerGuid = FGuid())
		{
			FScopedSetLandscapeEditingLayer Scope(this, InLayerGuid);
			
			const int32 ComponentSizeQuad = SubsectionSizeQuads * NumSubsections;
			const int32 ComponentSizeVerts = (SubsectionSizeQuads + 1) * NumSubsections;
			for (ULandscapeComponent* Component : InLandscapeComponents)
			{
				UTexture2D* ComponentHeightmap = Component->GetHeightmap(true);
		
				int32 Index = OutHeightmaps.IndexOfByPredicate([=](const FHeightmapCopyInfo& LayerHeightmap) { return LayerHeightmap.Texture == ComponentHeightmap; });

				FIntPoint ComponentSectionBase = Component->GetSectionBase() - LandscapeBaseQuads;
				FVector2D SourcePositionOffset(FMath::RoundToInt(ComponentSectionBase.X / ComponentSizeQuad), FMath::RoundToInt(ComponentSectionBase.Y / ComponentSizeQuad));
				FIntPoint ComponentVertexPosition = FIntPoint(SourcePositionOffset.X * ComponentSizeVerts, SourcePositionOffset.Y * ComponentSizeVerts);
				ALandscapeProxy* Proxy = Component->GetLandscapeProxy();

				if (Index == INDEX_NONE)
				{
					FLandscapeEditLayerReadback** CPUReadback = Proxy->HeightmapsCPUReadback.Find(ComponentHeightmap);
					OutHeightmaps.Add(FHeightmapCopyInfo(ComponentHeightmap, ComponentVertexPosition, ComponentSizeVerts, CPUReadback != nullptr ? *CPUReadback : nullptr));
				}
				else
				{
					OutHeightmaps[Index].Union(ComponentVertexPosition);
				}
			}
		};

		FLandscapeLayersHeightmapShaderParameters ShaderParams;

		bool FirstLayer = true;
		UTextureRenderTarget2D* CombinedHeightmapAtlasRT = HeightmapRTList[(int32)EHeightmapRTType::HeightmapRT_CombinedAtlas];
		UTextureRenderTarget2D* CombinedHeightmapNonAtlasRT = HeightmapRTList[(int32)EHeightmapRTType::HeightmapRT_CombinedNonAtlas];
		UTextureRenderTarget2D* LandscapeScratchRT1 = HeightmapRTList[(int32)EHeightmapRTType::HeightmapRT_Scratch1];
		UTextureRenderTarget2D* LandscapeScratchRT2 = HeightmapRTList[(int32)EHeightmapRTType::HeightmapRT_Scratch2];
		UTextureRenderTarget2D* LandscapeScratchRT3 = HeightmapRTList[(int32)EHeightmapRTType::HeightmapRT_Scratch3];

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
				TArray<FLandscapeLayersCopyTextureParams> DeferredCopyTextures;
				TArray<FHeightmapCopyInfo> LayerHeightmaps;
				GetUniqueHeightmaps(InLandscapeComponentsToRender, LayerHeightmaps, LandscapeExtent.Min, Layer.Guid);
				for (const FHeightmapCopyInfo& LayerHeightmap : LayerHeightmaps)
				{
					FLandscapeLayersCopyTextureParams& CopyTextureParams = DeferredCopyTextures.Add_GetRef(FLandscapeLayersCopyTextureParams(LayerHeightmap.Texture, LandscapeScratchRT1));
					// Only copy the size that's actually needed : 
					CopyTextureParams.CopySize = LayerHeightmap.SectionRect.Size();
					// Copy from the heightmap's top-left corner to the composited texture's position :
					CopyTextureParams.DestPosition = LayerHeightmap.SectionRect.Min;
				}
				ExecuteCopyLayersTexture(MoveTemp(DeferredCopyTextures));
			}

			// NOTE: From this point on, we always work in non atlas, we'll convert back at the end to atlas only
			DrawHeightmapComponentsToRenderTarget(FString::Printf(TEXT("LS Height: %s += -> NonAtlas %s"), *Layer.Name.ToString(), *LandscapeScratchRT1->GetName(), *LandscapeScratchRT2->GetName()),
												  InLandscapeComponentsToRender, LandscapeExtent.Min, LandscapeScratchRT1, nullptr, LandscapeScratchRT2, ERTDrawingType::RTAtlasToNonAtlas, true, ShaderParams);

			ShaderParams.ApplyLayerModifiers = true;

			// Combine Current layer with current result
			DrawHeightmapComponentsToRenderTarget(FString::Printf(TEXT("LS Height: %s += -> CombinedNonAtlas %s"), *Layer.Name.ToString(), *LandscapeScratchRT2->GetName(), *CombinedHeightmapNonAtlasRT->GetName()),
												InLandscapeComponentsToRender, LandscapeExtent.Min, LandscapeScratchRT2, FirstLayer ? nullptr : LandscapeScratchRT3, CombinedHeightmapNonAtlasRT, ERTDrawingType::RTNonAtlas, FirstLayer, ShaderParams);

			ShaderParams.ApplyLayerModifiers = false;

			if (Layer.bVisible && !bSkipBrush)
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

					ALandscapeBlueprintBrushBase* LandscapeBrush = Brush.GetBrush();
					check(LandscapeBrush != nullptr); // If we managed to render, the brush should be valid

					INC_DWORD_STAT(STAT_LandscapeLayersRegenerateDrawCalls); // Brush Render

					PrintLayersDebugRT(FString::Printf(TEXT("LS Height: %s %s -> BrushNonAtlas %s"), *Layer.Name.ToString(), *LandscapeBrush->GetName(), *BrushOutputNonAtlasRT->GetName()), BrushOutputNonAtlasRT);

					// Resolve back to Combined heightmap
					ExecuteCopyLayersTexture({ FLandscapeLayersCopyTextureParams(BrushOutputNonAtlasRT, CombinedHeightmapNonAtlasRT) });
					PrintLayersDebugRT(FString::Printf(TEXT("LS Height: %s Component %s += -> CombinedNonAtlas %s"), *Layer.Name.ToString(), *BrushOutputNonAtlasRT->GetName(), *CombinedHeightmapNonAtlasRT->GetName()), CombinedHeightmapNonAtlasRT);
				}
			}

			ExecuteCopyLayersTexture({ FLandscapeLayersCopyTextureParams(CombinedHeightmapNonAtlasRT, LandscapeScratchRT3) });
			PrintLayersDebugRT(FString::Printf(TEXT("LS Height: %s Component %s += -> CombinedNonAtlas %s"), *Layer.Name.ToString(), *CombinedHeightmapNonAtlasRT->GetName(), *LandscapeScratchRT3->GetName()), LandscapeScratchRT3);

			FirstLayer = false;
		}

		ShaderParams.GenerateNormals = true;
		ShaderParams.GridSize = GetRootComponent()->GetRelativeScale3D();

		// Broadcast Event of the Full Render
		if ((HeightmapUpdateModes & Update_Heightmap_All) == Update_Heightmap_All)
		{
			LandscapeFullHeightmapRenderDoneDelegate.Broadcast(LandscapeScratchRT3);
		}

		DrawHeightmapComponentsToRenderTarget(FString::Printf(TEXT("LS Height: %s = -> CombinedNonAtlasNormals : %s"), *CombinedHeightmapNonAtlasRT->GetName(), *LandscapeScratchRT1->GetName()),
											  InLandscapeComponentsToRender, LandscapeExtent.Min, CombinedHeightmapNonAtlasRT, nullptr, LandscapeScratchRT1, ERTDrawingType::RTNonAtlas, true, ShaderParams);

		ShaderParams.GenerateNormals = false;

		DrawHeightmapComponentsToRenderTarget(FString::Printf(TEXT("LS Height: %s = -> CombinedAtlasFinal : %s"), *LandscapeScratchRT1->GetName(), *CombinedHeightmapAtlasRT->GetName()),
											  InLandscapeComponentsToRender, LandscapeExtent.Min, LandscapeScratchRT1, nullptr, CombinedHeightmapAtlasRT, ERTDrawingType::RTNonAtlasToAtlas, true, ShaderParams);

		DrawHeightmapComponentsToRenderTargetMips(InLandscapeComponentsToRender, LandscapeExtent.Min, CombinedHeightmapAtlasRT, true, ShaderParams);

		// Copy back all Mips to original heightmap data
		{
			TArray<FLandscapeLayersCopyTextureParams> DeferredCopyTextures;
			TArray<FLandscapeLayersCopyReadbackTextureParams> DeferredCopyReadbackTextures;
			TArray<FHeightmapCopyInfo> Heightmaps;
			GetUniqueHeightmaps(InLandscapeComponentsToResolve, Heightmaps, LandscapeExtent.Min);
			for (const FHeightmapCopyInfo& Heightmap : Heightmaps)
			{
				check(Heightmap.CPUReadback);

				const FIntPoint Mip0CopySize = Heightmap.SectionRect.Size();
				const FIntPoint Mip0SourcePosition = Heightmap.SectionRect.Min;
				// Mip 0
				{
					FLandscapeLayersCopyTextureParams& CopyTextureParams = DeferredCopyTextures.Add_GetRef(FLandscapeLayersCopyTextureParams(CombinedHeightmapAtlasRT, Heightmap.Texture));
					// Only copy the size that's actually needed : 
					CopyTextureParams.CopySize = Mip0CopySize;
					// Copy from the composited texture's position to the top-left corner of the heightmap
					CopyTextureParams.SourcePosition = Mip0SourcePosition;
				}

				// Other Mips
				uint8 MipIndex = 1;
				for (int32 MipRTIndex = (int32)EHeightmapRTType::HeightmapRT_Mip1; MipRTIndex < (int32)EHeightmapRTType::HeightmapRT_Count; ++MipRTIndex)
				{
					UTextureRenderTarget2D* RenderTargetMip = HeightmapRTList[MipRTIndex];
					if (RenderTargetMip != nullptr)
					{
						FLandscapeLayersCopyTextureParams& CopyTextureParams = DeferredCopyTextures.Add_GetRef(FLandscapeLayersCopyTextureParams(RenderTargetMip, Heightmap.Texture));
						CopyTextureParams.CopySize.X = Mip0CopySize.X >> MipIndex;
						CopyTextureParams.CopySize.Y = Mip0CopySize.Y >> MipIndex;
						CopyTextureParams.SourcePosition.X = Mip0SourcePosition.X >> MipIndex;
						CopyTextureParams.SourcePosition.Y = Mip0SourcePosition.Y >> MipIndex;
						CopyTextureParams.DestMip = MipIndex;

						++MipIndex;
					}
				}

				// Init the CPU read back parameters. This includes a context containing the current component states.
				const int32 ReadbackIndex = DeferredCopyReadbackTextures.Add(FLandscapeLayersCopyReadbackTextureParams(Heightmap.Texture, Heightmap.CPUReadback));
				for (ULandscapeComponent* Component : *MapHelper.HeightmapToComponents.Find(Heightmap.Texture))
				{
					const FIntPoint ComponentKey = Component->GetSectionBase() / Component->ComponentSizeQuads;
					const int32 ComponentFlags = Component->GetLayerUpdateFlagPerMode();
					DeferredCopyReadbackTextures[ReadbackIndex].Context.Add(FLandscapeEditLayerReadback::FComponentReadbackContext(ComponentKey, ComponentFlags));
				}
			}
			ExecuteCopyLayersTexture(MoveTemp(DeferredCopyTextures));
			ExecuteCopyToReadbackTexture(DeferredCopyReadbackTextures);
		}

		RenderCaptureLayersNextHeightmapDraws = 0;
	}

	return HeightmapUpdateModes;
}

void ALandscape::UpdateForChangedHeightmaps(ULandscapeComponent* InComponent, const FLandscapeEditLayerReadbackResult& InReadbackResult)
{
	// If the source data has changed, mark the component as needing a collision data update:
	//  - If ELandscapeComponentUpdateFlag::Component_Update_Heightmap_Collision is passed, it will be done immediately
	//  - If not, at least the component's collision data will still get updated eventually, when the flag is finally passed :
	if (InReadbackResult.bModified)
	{
		InComponent->SetPendingCollisionDataUpdate(true);
	}

	const int32 HeightUpdateMode = InReadbackResult.UpdateModes & (ELandscapeLayerUpdateMode::Update_Heightmap_All | ELandscapeLayerUpdateMode::Update_Heightmap_Editing | ELandscapeLayerUpdateMode::Update_Heightmap_Editing_NoCollision);

	// Only update collision if there was an actual change performed on the source data : 
	if (InComponent->GetPendingCollisionDataUpdate())
	{
		if (IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag::Component_Update_Heightmap_Collision, HeightUpdateMode))
		{
			InComponent->UpdateCachedBounds();
			InComponent->UpdateComponentToWorld();

			// Avoid updating height field if we are going to recreate collision in this update
			bool bUpdateHeightfieldRegion = !IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag::Component_Update_Recreate_Collision, HeightUpdateMode);
			InComponent->UpdateCollisionData(bUpdateHeightfieldRegion);
			InComponent->SetPendingCollisionDataUpdate(false);
		}
		else if (IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag::Component_Update_Approximated_Bounds, HeightUpdateMode))
		{
			// Update bounds with an approximated value (real computation will be done anyways when computing collision)
			const bool bInApproximateBounds = true;
			InComponent->UpdateCachedBounds(bInApproximateBounds);
			InComponent->UpdateComponentToWorld();
		}
	}
}

void ALandscape::ResolveLayersHeightmapTexture(
	FTextureToComponentHelper const& MapHelper,
	TSet<UTexture2D*> const& HeightmapsToResolve,
	bool bIntermediateRender,
	bool bFlushRender,
	TMap<ULandscapeComponent*, FLandscapeEditLayerReadbackResult>& InOutComponents)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_ResolveLayersHeightmapTexture);
	SCOPE_CYCLE_COUNTER(STAT_LandscapeLayersResolveHeightmaps);

	ULandscapeInfo* Info = GetLandscapeInfo();
	if (Info == nullptr)
	{
		return;
	}

	FDirtyDelegate OnDirtyDelegate = [this, &MapHelper](UTexture2D const* Heightmap, FColor const* OldData, FColor const* NewData)
	{
		OnDirtyHeightmap(MapHelper, Heightmap, OldData, NewData);
	};

	TArray<ULandscapeComponent*> ChangedComponents;
	for (UTexture2D* Heightmap : HeightmapsToResolve)
	{
		ALandscapeProxy* LandscapeProxy = Heightmap->GetTypedOuter<ALandscapeProxy>();
		check(LandscapeProxy);
		if (FLandscapeEditLayerReadback** CPUReadback = LandscapeProxy->HeightmapsCPUReadback.Find(Heightmap))
		{
			const bool bChanged = ResolveLayersTexture(MapHelper, *CPUReadback, Heightmap, bIntermediateRender, bFlushRender, InOutComponents, OnDirtyDelegate);
			if (bChanged)
			{
				ChangedComponents.Append(MapHelper.HeightmapToComponents[Heightmap]);
				Heightmap->MarkPackageDirty();
			}
		}
	}

	const bool bInvalidateLightingCache = true;
	InvalidateGeneratedComponentData(ChangedComponents, bInvalidateLightingCache);
}

void ALandscape::ClearDirtyData(ULandscapeComponent* InLandscapeComponent)
{
	if (InLandscapeComponent->EditToolRenderData.DirtyTexture == nullptr)
	{
		return;
	}

	if (!CVarLandscapeTrackDirty.GetValueOnAnyThread())
	{
		return;
	}

	FLandscapeEditDataInterface LandscapeEdit(GetLandscapeInfo());
	const int32 X1 = InLandscapeComponent->GetSectionBase().X;
	const int32 X2 = X1 + ComponentSizeQuads;
	const int32 Y1 = InLandscapeComponent->GetSectionBase().Y;
	const int32 Y2 = Y1 + ComponentSizeQuads;
	const int32 ComponentWidth = (SubsectionSizeQuads + 1)*NumSubsections;
	const int32 DirtyDataSize = ComponentWidth * ComponentWidth;
	TUniquePtr<uint8[]> DirtyData = MakeUnique<uint8[]>(DirtyDataSize);
	FMemory::Memzero(DirtyData.Get(), DirtyDataSize);
	LandscapeEdit.SetDirtyData(X1, Y1, X2, Y2, DirtyData.Get(), 0);
}
	
void ALandscape::UpdateWeightDirtyData(ULandscapeComponent* InLandscapeComponent, UTexture2D const* InWeightmap, FColor const* InOldData, FColor const* InNewData, uint8 InChannel)
{
	check(InOldData && InNewData);

	FLandscapeEditDataInterface LandscapeEdit(GetLandscapeInfo());
	const int32 X1 = InLandscapeComponent->GetSectionBase().X;
	const int32 X2 = X1 + ComponentSizeQuads;
	const int32 Y1 = InLandscapeComponent->GetSectionBase().Y;
	const int32 Y2 = Y1 + ComponentSizeQuads;
	const int32 ComponentWidth = (SubsectionSizeQuads + 1) * NumSubsections;
	const int32 DirtyDataSize = ComponentWidth * ComponentWidth;
	TUniquePtr<uint8[]> DirtyData = MakeUnique<uint8[]>(DirtyDataSize);
	const int32 SizeU = InWeightmap->Source.GetSizeX();
	const int32 SizeV = InWeightmap->Source.GetSizeY();
	check(DirtyDataSize == SizeU * SizeV);

	const uint8 DirtyWeight = 1<<1;
	LandscapeEdit.GetDirtyData(X1, Y1, X2, Y2, DirtyData.Get(), 0);

	for (int32 Index = 0; Index < DirtyDataSize; ++Index)
	{
		uint8* OldChannelValue = (uint8*)&InOldData[Index] + ChannelOffsets[InChannel];
		uint8* NewChannelValue = (uint8*)&InNewData[Index] + ChannelOffsets[InChannel];
		if (*OldChannelValue != *NewChannelValue)
		{
			DirtyData[Index] |= DirtyWeight;
		}
	}

	LandscapeEdit.SetDirtyData(X1, Y1, X2, Y2, DirtyData.Get(), 0);
}

void ALandscape::OnDirtyWeightmap(FTextureToComponentHelper const& MapHelper, UTexture2D const* InWeightmap, FColor const* InOldData, FColor const* InNewData)
{
	const bool bWriteDiff = (CVarLandscapeOutputDiffBitmap.GetValueOnAnyThread() & 2) != 0;
	const bool bTrackDirty = CVarLandscapeTrackDirty.GetValueOnAnyThread() != 0;
	if (!bWriteDiff && !bTrackDirty)
	{
		return;
	}

	TArray<ULandscapeComponent*> const* Components = MapHelper.WeightmapToComponents.Find(InWeightmap);
	if (Components != nullptr)
	{
		for (ULandscapeComponent* Component : *Components)
		{
			TArray<UTexture2D*>& WeightmapTextures = Component->GetWeightmapTextures();
			TArray<FWeightmapLayerAllocationInfo>& AllocInfos = Component->GetWeightmapLayerAllocations();

			for (FWeightmapLayerAllocationInfo const& AllocInfo : AllocInfos)
			{
				check(AllocInfo.IsAllocated() && AllocInfo.WeightmapTextureIndex < WeightmapTextures.Num());
				if (InWeightmap == WeightmapTextures[AllocInfo.WeightmapTextureIndex])
				{
					if (bTrackDirty)
					{
						UpdateWeightDirtyData(Component, InWeightmap, InOldData, InNewData, AllocInfo.WeightmapTextureChannel);
					}

					if (bWriteDiff)
					{
						size_t ChannelOffset = ChannelOffsets[AllocInfo.WeightmapTextureChannel];
						FString LevelName = FPackageName::GetShortName(Component->GetOutermost());
						FString FilePattern = FString::Format(TEXT("LandscapeLayers/{0}-{1}-{2}-WM"), { LevelName, Component->GetName(), AllocInfo.GetLayerName().ToString() });
						FFileHelper::CreateBitmap(*(FilePattern + "-Pre.bmp"), InWeightmap->Source.GetSizeX(), InWeightmap->Source.GetSizeY(), InOldData, nullptr, &IFileManager::Get(), nullptr, true, (FFileHelper::EChannelMask)ChannelOffset);
						FFileHelper::CreateBitmap(*(FilePattern + "-Post.bmp"), InWeightmap->Source.GetSizeX(), InWeightmap->Source.GetSizeY(), InNewData, nullptr, &IFileManager::Get(), nullptr, true, (FFileHelper::EChannelMask)ChannelOffset);
					}
				}
			}
		}
	}
}

void ALandscape::UpdateHeightDirtyData(ULandscapeComponent* InLandscapeComponent, UTexture2D const* InHeightmap, FColor const* InOldData, FColor const* InNewData)
{
	check(InOldData && InNewData);

	FLandscapeEditDataInterface LandscapeEdit(GetLandscapeInfo());
	const int32 X1 = InLandscapeComponent->GetSectionBase().X;
	const int32 X2 = X1 + ComponentSizeQuads;
	const int32 Y1 = InLandscapeComponent->GetSectionBase().Y;
	const int32 Y2 = Y1 + ComponentSizeQuads;
	const int32 ComponentWidth = (SubsectionSizeQuads + 1)*NumSubsections;
	const int32 DirtyDataSize = ComponentWidth * ComponentWidth;
	TUniquePtr<uint8[]> DirtyData = MakeUnique<uint8[]>(DirtyDataSize);
	const int32 SizeU = InHeightmap->Source.GetSizeX();
	const int32 SizeV = InHeightmap->Source.GetSizeY();
	const int32 HeightmapOffsetX = InLandscapeComponent->HeightmapScaleBias.Z * (float)SizeU;
	const int32 HeightmapOffsetY = InLandscapeComponent->HeightmapScaleBias.W * (float)SizeV;
	const uint8 DirtyHeight = 1<<0;
	LandscapeEdit.GetDirtyData(X1, Y1, X2, Y2, DirtyData.Get(), 0);
	
	for (int32 X = 0; X < ComponentWidth; ++X)
	{
		for (int32 Y = 0; Y < ComponentWidth; ++Y)
		{
			int32 TexX = HeightmapOffsetX + X;
			int32 TexY = HeightmapOffsetY + Y;
			int32 TexIndex = TexX + TexY * SizeU;
			check(TexIndex < SizeU * SizeV);
			if (InOldData[TexIndex] != InNewData[TexIndex])
			{
				DirtyData[X + Y * ComponentWidth] |= DirtyHeight;
			}
		}
	}

	LandscapeEdit.SetDirtyData(X1, Y1, X2, Y2, DirtyData.Get(), 0);
}

void ALandscape::OnDirtyHeightmap(FTextureToComponentHelper const& MapHelper, UTexture2D const* InHeightmap, FColor const* InOldData, FColor const* InNewData)
{
	const bool bHeightmapDiff = (CVarLandscapeOutputDiffBitmap.GetValueOnAnyThread() & 1) != 0;
	const bool bTrackDirty = CVarLandscapeTrackDirty.GetValueOnAnyThread() != 0;

	if (!bHeightmapDiff && !bTrackDirty)
	{
		return;
	}

	TArray<ULandscapeComponent*> const* Components = MapHelper.HeightmapToComponents.Find(InHeightmap);
	if (Components != nullptr)
	{
		for (ULandscapeComponent* Component : *Components)
		{
			if (bTrackDirty)
			{
				UpdateHeightDirtyData(Component, InHeightmap, InOldData, InNewData);
			}

			if (bHeightmapDiff)
			{
				FString LevelName = FPackageName::GetShortName(Component->GetOutermost());
				FString FilePattern = FString::Format(TEXT("LandscapeLayers/{0}-{1}-HM"), { LevelName, Component->GetName() });

				const int32 SizeU = InHeightmap->Source.GetSizeX();
				const int32 SizeV = InHeightmap->Source.GetSizeY();
				const int32 HeightmapOffsetX = Component->HeightmapScaleBias.Z * (float)SizeU;
				const int32 HeightmapOffsetY = Component->HeightmapScaleBias.W * (float)SizeV;
				const int32 ComponentWidth = (SubsectionSizeQuads + 1) * NumSubsections;
				FIntRect SubRegion(HeightmapOffsetX, HeightmapOffsetY, HeightmapOffsetX + ComponentWidth, HeightmapOffsetY + ComponentWidth);

				FFileHelper::CreateBitmap(*(FilePattern + "-Pre.bmp"), InHeightmap->Source.GetSizeX(), InHeightmap->Source.GetSizeY(), InOldData, &SubRegion, &IFileManager::Get(), nullptr, true);
				FFileHelper::CreateBitmap(*(FilePattern + "-Post.bmp"), InHeightmap->Source.GetSizeX(), InHeightmap->Source.GetSizeY(), InNewData, &SubRegion, &IFileManager::Get(), nullptr, true);
			}
		}
	}
}

bool ALandscape::ResolveLayersTexture(
	FTextureToComponentHelper const& MapHelper,
	FLandscapeEditLayerReadback* InCPUReadback,
	UTexture2D* InOutputTexture,
	bool bIntermediateRender,
	bool bFlushRender,
	TMap<ULandscapeComponent*, FLandscapeEditLayerReadbackResult>& InOutComponents,
	FDirtyDelegate DirtyDelegate)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_ResolveLayersTexture);
	SCOPE_CYCLE_COUNTER(STAT_LandscapeLayersResolveTexture);

	if (bFlushRender)
	{
		InCPUReadback->Flush();
	}
	else
	{
		InCPUReadback->Tick();
	}

	bool bChanged = false;

	const int32 CompletedReadbackNum = InCPUReadback->GetCompletedResultNum();
	if (CompletedReadbackNum > 0)
	{
		// Copy final result to texture source.
		TArray<TArray<FColor>> const& OutMipsData = InCPUReadback->GetResult(CompletedReadbackNum - 1);

		for (int8 MipIndex = 0; MipIndex < OutMipsData.Num(); ++MipIndex)
		{
			if (OutMipsData[MipIndex].Num() > 0)
			{
				uint8* TextureData = InOutputTexture->Source.LockMip(MipIndex);

				// Do dirty detection on first mip.
				// Don't do this for intermediate renders.
				if (MipIndex == 0 && !bIntermediateRender)
				{
					const uint32 Hash = FLandscapeEditLayerReadback::CalculateHash((uint8*)OutMipsData[MipIndex].GetData(), OutMipsData[MipIndex].Num() * sizeof(FColor));
					if (InCPUReadback->SetHash(Hash))
					{
						DirtyDelegate(InOutputTexture, (FColor*)TextureData, OutMipsData[MipIndex].GetData());
						bChanged = true;
					}
				}

				FMemory::Memcpy(TextureData, OutMipsData[MipIndex].GetData(), OutMipsData[MipIndex].Num() * sizeof(FColor));

				InOutputTexture->Source.UnlockMip(MipIndex);
			}
		}

		// Process component flags from all result contexts.
		for (int32 ResultIndex = 0; ResultIndex < CompletedReadbackNum; ++ResultIndex)
		{
			FLandscapeEditLayerReadback::FReadbackContext const& ResultContext = InCPUReadback->GetResultContext(ResultIndex);
			for (auto ComponentContext : ResultContext)
			{
				ULandscapeComponent** Component = GetLandscapeInfo()->XYtoComponentMap.Find(ComponentContext.ComponentKey);
				if (Component != nullptr && *Component != nullptr)
				{
					FLandscapeEditLayerReadbackResult& ComponentInfo = InOutComponents.FindOrAdd(*Component);
					ComponentInfo.UpdateModes |= ComponentContext.UpdateModes;
					ComponentInfo.bModified |= bChanged ? 1 : 0;
				}
			}
		}

		// Release the processed read backs
		InCPUReadback->ReleaseCompletedResults(CompletedReadbackNum);
	}

	return bChanged;
}

void ALandscape::PrepareComponentDataToExtractMaterialLayersCS(const TArray<ULandscapeComponent*>& InLandscapeComponents, const FLandscapeLayer& InLayer, int32 InCurrentWeightmapToProcessIndex, const FIntPoint& InLandscapeBase, FLandscapeTexture2DResource* InOutTextureData,
																TArray<FLandscapeLayerWeightmapExtractMaterialLayersComponentData>& OutComponentData, TMap<ULandscapeLayerInfoObject*, int32>& OutLayerInfoObjects)
{
	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return;
	}

	TArray<FLandscapeLayersCopyTextureParams> DeferredCopyTextures;

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

				FLandscapeLayersCopyTextureParams& CopyTextureParams = DeferredCopyTextures.Add_GetRef(FLandscapeLayersCopyTextureParams(*LayerWeightmap->GetName(), LayerWeightmap->Resource, FString::Printf(TEXT("%s WeightmapScratchTexture"), *InLayer.Name.ToString()), InOutTextureData));
				// Only copy the size that's actually needed : 
				CopyTextureParams.CopySize.X = LayerWeightmap->Resource->GetSizeX();
				CopyTextureParams.CopySize.Y = LayerWeightmap->Resource->GetSizeY();
				// Copy from the top-left corner of the weightmap to the composited texture's position
				CopyTextureParams.DestPosition = SourceComponentVertexPosition;
				PrintLayersDebugTextureResource(FString::Printf(TEXT("LS Weight: %s WeightmapScratchTexture %s"), *InLayer.Name.ToString(), TEXT("WeightmapScratchTextureResource")), InOutTextureData, 0, false);

				for (const FWeightmapLayerAllocationInfo& WeightmapLayerAllocation : ComponentLayerData->WeightmapData.LayerAllocations)
				{
					if (WeightmapLayerAllocation.LayerInfo != nullptr && WeightmapLayerAllocation.IsAllocated() && ComponentLayerData->WeightmapData.Textures[WeightmapLayerAllocation.WeightmapTextureIndex] == LayerWeightmap)
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
	
	ExecuteCopyLayersTexture(MoveTemp(DeferredCopyTextures));
}

void ALandscape::PrepareComponentDataToPackMaterialLayersCS(int32 InCurrentWeightmapToProcessIndex, const FIntPoint& InLandscapeBase, const TArray<ULandscapeComponent*>& InAllLandscapeComponents, TArray<UTexture2D*>& OutProcessedWeightmaps,
	TArray<FLandscapeEditLayerReadback*>& OutProcessedCPUReadbacks, TArray<FLandscapeLayerWeightmapPackMaterialLayersComponentData>& OutComponentData)
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

				FLandscapeEditLayerReadback** CPUReadback = Component->GetLandscapeProxy()->WeightmapsCPUReadback.Find(ComponentWeightmapTexture);
				check(CPUReadback != nullptr);

				OutProcessedCPUReadbacks.Add(*CPUReadback);
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

void ALandscape::ReallocateLayersWeightmaps(const TArray<ULandscapeComponent*>& InLandscapeComponents, const TArray<ULandscapeLayerInfoObject*>& InBrushRequiredAllocations)
{
	SCOPE_CYCLE_COUNTER(STAT_LandscapeLayersReallocateWeightmaps);

	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
	{
		return;
	}

	// Clear allocation data
	for (ULandscapeComponent* Component : InLandscapeComponents)
	{
		TArray<FWeightmapLayerAllocationInfo>& BaseLayerAllocations = Component->GetWeightmapLayerAllocations();

		for (FWeightmapLayerAllocationInfo& BaseWeightmapAllocation : BaseLayerAllocations)
		{
			BaseWeightmapAllocation.Free();
		}

		TArray<ULandscapeWeightmapUsage*>& WeightmapTexturesUsage = Component->GetWeightmapTexturesUsage();

		for (int32 i = 0; i < WeightmapTexturesUsage.Num(); ++i)
		{
			ULandscapeWeightmapUsage* Usage = WeightmapTexturesUsage[i];
			check(Usage != nullptr);

			Usage->ClearUsage(Component);
		}
	}
		
	// Build a map of all the allocation per components
	TMap<ULandscapeComponent*, TArray<ULandscapeLayerInfoObject*>> LayerAllocsPerComponent;

	for (FLandscapeLayer& Layer : LandscapeLayers)
	{
		for (ULandscapeComponent* Component : InLandscapeComponents)
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

	for (ULandscapeComponent* Component : InLandscapeComponents)
	{
		Component->ReallocateWeightmaps(nullptr, false, false, true, false, nullptr, &NewCreatedTextures);
	}

	// TODO: correctly only recreate what is required instead of everything..
	//GDisableAutomaticTextureMaterialUpdateDependencies = true;

	for (UTexture2D* Texture : NewCreatedTextures)
	{
		Texture->FinishCachePlatformData();
		Texture->PostEditChange();
		Texture->bForceMiplevelsToBeResident = true;
		Texture->WaitForStreaming();
	}

	//GDisableAutomaticTextureMaterialUpdateDependencies = false;

	// Clean-up unused weightmap CPUReadback resources
	Info->ForAllLandscapeProxies([](ALandscapeProxy* Proxy)
	{
		TArray<UTexture2D*, TInlineAllocator<64>> EntriesToRemoveFromMap;
		for (auto& Pair : Proxy->WeightmapsCPUReadback)
		{
			UTexture2D* WeightmapTextureKey = Pair.Key;
			bool IsTextureReferenced = false;
			for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
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
			}
		}

		if (EntriesToRemoveFromMap.Num())
		{
			for (UTexture2D* OldWeightmapTexture : EntriesToRemoveFromMap)
			{
				if (FLandscapeEditLayerReadback** CPUReadbackToDelete = Proxy->WeightmapsCPUReadback.Find(OldWeightmapTexture))
				{
					check(*CPUReadbackToDelete);
					delete * CPUReadbackToDelete;
					Proxy->WeightmapsCPUReadback.Remove(OldWeightmapTexture);
				}
			}
		}
	});
}

void ALandscape::InitializeLayersWeightmapResources()
{
	ULandscapeInfo* Info = GetLandscapeInfo();

	if (Info == nullptr)
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
			ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_ReleaseResources)(
				[Resource](FRHICommandList& RHICmdList)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RT_ReleaseResources);
				Resource->ReleaseResource();
				delete Resource;
			});
		}
	}

	// Create resources

	int32 LayerCount = Info->Layers.Num() + 1; // due to visibility being stored at 0

	// Use the 1st one to compute the resource as they are all the same anyway
	UTextureRenderTarget2D* FirstWeightmapRT = WeightmapRTList[(int32)EWeightmapRTType::WeightmapRT_Scratch1];

	CombinedLayersWeightmapAllMaterialLayersResource = new FLandscapeTexture2DArrayResource(FirstWeightmapRT->SizeX, FirstWeightmapRT->SizeY, LayerCount, PF_G8, 1, true);
	BeginInitResource(CombinedLayersWeightmapAllMaterialLayersResource);

	CurrentLayersWeightmapAllMaterialLayersResource = new FLandscapeTexture2DArrayResource(FirstWeightmapRT->SizeX, FirstWeightmapRT->SizeY, LayerCount, PF_G8, 1, true);
	BeginInitResource(CurrentLayersWeightmapAllMaterialLayersResource);

	WeightmapScratchExtractLayerTextureResource = new FLandscapeTexture2DResource(FirstWeightmapRT->SizeX, FirstWeightmapRT->SizeY, PF_B8G8R8A8, 1, false);
	BeginInitResource(WeightmapScratchExtractLayerTextureResource);

	int32 MipCount = 0;

	for (int32 MipRTIndex = (int32)EWeightmapRTType::WeightmapRT_Mip0; MipRTIndex < (int32)EWeightmapRTType::WeightmapRT_Count; ++MipRTIndex)
	{
		if (WeightmapRTList[MipRTIndex] != nullptr)
		{
			++MipCount;
		}
	}

	// Format for UAV can't be PF_B8G8R8A8 on Windows 7 so use PF_R8G8B8A8
	// We make the final copy out of this to a PF_R8G8B8A8 target with CopyTexturePS() instead of CopyLayersTexture() because a pixel shader will automatically handle the channel swizzling (where a RHICopyTexture won't)
	WeightmapScratchPackLayerTextureResource = new FLandscapeTexture2DResource(FirstWeightmapRT->SizeX, FirstWeightmapRT->SizeY, PF_R8G8B8A8, MipCount, true);
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
		for (const FLandscapeLayer& Layer : LandscapeLayers)
		{
			for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
			{
				for (UTexture2D* ComponentWeightmap : Component->GetWeightmapTextures())
				{
					if (!IsStreamableAssetFullyStreamedIn(ComponentWeightmap, bInWaitForStreaming))
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

							// Explicit call to base class to skip CachePlatormData of UTexture2D
							LayerWeightmap->UTexture::UpdateResource();
						}

						IsReady &= IsStreamableAssetFullyStreamedIn(LayerWeightmap, bInWaitForStreaming);
						IsReady &= bInWaitForStreaming || (LayerWeightmap->Resource != nullptr && LayerWeightmap->Resource->IsInitialized());
					}
				}
			}
		}
	});

	return IsReady;
}

int32 ALandscape::RegenerateLayersWeightmaps(FTextureToComponentHelper const& MapHelper, const TArray<ULandscapeComponent*>& InLandscapeComponentsToRender, const TArray<ULandscapeComponent*>& InLandscapeComponentsToResolve)
{
	SCOPE_CYCLE_COUNTER(STAT_LandscapeLayersRegenerateWeightmaps);
	const int32 AllWeightmapUpdateModes = (ELandscapeLayerUpdateMode::Update_Weightmap_All | ELandscapeLayerUpdateMode::Update_Weightmap_Editing | ELandscapeLayerUpdateMode::Update_Weightmap_Editing_NoCollision);
	const int32 WeightmapUpdateModes = LayerContentUpdateModes & AllWeightmapUpdateModes;
	const bool bSkipBrush = CVarLandscapeLayerBrushOptim.GetValueOnAnyThread() == 1 && ((WeightmapUpdateModes & AllWeightmapUpdateModes) == ELandscapeLayerUpdateMode::Update_Weightmap_Editing);
	const bool bForceRender = CVarForceLayersUpdate.GetValueOnAnyThread() != 0;
	
	ULandscapeInfo* Info = GetLandscapeInfo();

	if (WeightmapUpdateModes == 0 && !bForceRender)
	{
		return 0;
	}

	if (InLandscapeComponentsToResolve.Num() == 0 || Info == nullptr)
	{
		return WeightmapUpdateModes;
	}
			
	TArray<ULandscapeComponent*> ComponentThatNeedMaterialRebuild;
	TArray<ULandscapeLayerInfoObject*> BrushRequiredAllocations;
	int32 LayerCount = Info->Layers.Num() + 1; // due to visibility being stored at 0
	bool ClearFlagsAfterUpdate = true;

	if (WeightmapUpdateModes || bForceRender)
	{
		RenderCaptureInterface::FScopedCapture RenderCapture((RenderCaptureLayersNextWeightmapDraws != 0), TEXT("LandscapeLayersWeightmapCapture"));

		FIntRect LandscapeExtent;
		if (!Info->GetLandscapeExtent(LandscapeExtent.Min.X, LandscapeExtent.Min.Y, LandscapeExtent.Max.X, LandscapeExtent.Max.Y))
		{
			return 0;
		}
		
		check(WeightmapRTList.Num() > 0);

		UTextureRenderTarget2D* LandscapeScratchRT1 = WeightmapRTList[(int32)EWeightmapRTType::WeightmapRT_Scratch1];
		UTextureRenderTarget2D* LandscapeScratchRT2 = WeightmapRTList[(int32)EWeightmapRTType::WeightmapRT_Scratch2];
		UTextureRenderTarget2D* LandscapeScratchRT3 = WeightmapRTList[(int32)EWeightmapRTType::WeightmapRT_Scratch3];
		UTextureRenderTarget2D* EmptyRT = WeightmapRTList[(int32)EWeightmapRTType::WeightmapRT_Scratch_RGBA];
		FLandscapeLayersWeightmapShaderParameters PSShaderParams;
		FString SourceDebugName;
		FString DestDebugName;
		ClearLayersWeightmapTextureResource(TEXT("ClearRT RGBA"), EmptyRT->GameThread_GetRenderTargetResource());
		ClearLayersWeightmapTextureResource(TEXT("ClearRT R"), LandscapeScratchRT1->GameThread_GetRenderTargetResource());

		{
			TArray<FLandscapeLayersCopyTextureParams> DeferredCopyTextures;
			for (int32 LayerIndex = 0; LayerIndex < LayerCount; ++LayerIndex)
			{
				SourceDebugName = LandscapeScratchRT1->GetName();
				DestDebugName = FString::Printf(TEXT("Weight: Clear CombinedProcLayerWeightmapAllLayersResource %d, "), LayerIndex);

				FLandscapeLayersCopyTextureParams& CopyTextureParams = DeferredCopyTextures.Add_GetRef(FLandscapeLayersCopyTextureParams(SourceDebugName, LandscapeScratchRT1->GameThread_GetRenderTargetResource(), DestDebugName, CombinedLayersWeightmapAllMaterialLayersResource));
				CopyTextureParams.DestArrayIndex = LayerIndex;
				CopyTextureParams.SourceAccess = ERHIAccess::SRVMask;
				CopyTextureParams.DestAccess = ERHIAccess::UAVMask;
			}

			ExecuteCopyLayersTexture(MoveTemp(DeferredCopyTextures));
		}

		bool bHasWeightmapData = false;
		bool bFirstLayer = true;
		TMap<ULandscapeLayerInfoObject*, bool> WeightmapLayersBlendSubstractive;

		for (FLandscapeLayer& Layer : LandscapeLayers)
		{
			int8 CurrentWeightmapToProcessIndex = 0;
			bool HasFoundWeightmapToProcess = true; // try processing at least once

			TMap<ULandscapeLayerInfoObject*, int32> LayerInfoObjects; // <LayerInfoObj, LayerIndex>

			// Determine if some brush want to write to layer that we have currently no data on
			if (Layer.bVisible && !bSkipBrush)
			{
				for (int32 LayerInfoSettingsIndex = 0; LayerInfoSettingsIndex < Info->Layers.Num(); ++LayerInfoSettingsIndex)
				{
					const FLandscapeInfoLayerSettings& InfoLayerSettings = Info->Layers[LayerInfoSettingsIndex];
					
					// It is possible that no layer info has been assigned so that InfoLayerSettings.LayerInfoObj == nullptr. In that case we don't consider it the layer here.
					if (InfoLayerSettings.LayerInfoObj != nullptr)
					{
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
			}

			// Track the layers that we have cleared (use a TBitArray in case we get more than 64 layers!)
			TBitArray<> ClearedLayers(0, Info->Layers.Num() + 1);

			// Loop until there is no more weightmap texture to process
			while (HasFoundWeightmapToProcess)
			{
				SourceDebugName = EmptyRT->GetName();
				DestDebugName = FString::Printf(TEXT("Weight: %s Clear WeightmapScratchExtractLayerTextureResource"), *Layer.Name.ToString());

				ExecuteCopyLayersTexture({ FLandscapeLayersCopyTextureParams(SourceDebugName, EmptyRT->GameThread_GetRenderTargetResource(), DestDebugName, WeightmapScratchExtractLayerTextureResource) });

				// Prepare compute shader data
				TArray<FLandscapeLayerWeightmapExtractMaterialLayersComponentData> ComponentsData;
				PrepareComponentDataToExtractMaterialLayersCS(InLandscapeComponentsToRender, Layer, CurrentWeightmapToProcessIndex, LandscapeExtent.Min, WeightmapScratchExtractLayerTextureResource, ComponentsData, LayerInfoObjects);

				HasFoundWeightmapToProcess = ComponentsData.Num() > 0;

				// Clear the current atlas if required
				if (CurrentWeightmapToProcessIndex == 0)
				{
					ClearLayersWeightmapTextureResource(TEXT("ClearRT"), LandscapeScratchRT1->GameThread_GetRenderTargetResource());
				}

				TArray<FLandscapeLayersCopyTextureParams> DeferredCopyTextures;
				// Important: for performance reason we only clear the layer we will write to, the other one might contain data but they will not be read during the blend phase
				if (ClearedLayers.CountSetBits() < LayerInfoObjects.Num())
				{
					for (auto& ItPair : LayerInfoObjects)
					{
						// Only clear the layers that we haven't already cleared
						const int32 LayerIndex = ItPair.Value;
						FBitReference ClearedLayerBit = ClearedLayers[LayerIndex];
						if (!ClearedLayerBit)
						{
							ClearedLayerBit = true;

							SourceDebugName = LandscapeScratchRT1->GetName();
							DestDebugName = FString::Printf(TEXT("Weight: %s Clear CurrentProcLayerWeightmapAllLayersResource %d, "), *Layer.Name.ToString(), LayerIndex);

							FLandscapeLayersCopyTextureParams& CopyTextureParams = DeferredCopyTextures.Add_GetRef(FLandscapeLayersCopyTextureParams(SourceDebugName, LandscapeScratchRT1->GameThread_GetRenderTargetResource(), DestDebugName, CurrentLayersWeightmapAllMaterialLayersResource));
							CopyTextureParams.DestArrayIndex = LayerIndex;
						}
					}

					ExecuteCopyLayersTexture(MoveTemp(DeferredCopyTextures));
				}

				// Perform the compute shader
				if (ComponentsData.Num() > 0)
				{
					PrintLayersDebugTextureResource(FString::Printf(TEXT("LS Weight: %s WeightmapScratchTexture %s"), *Layer.Name.ToString(), TEXT("WeightmapScratchTextureResource")), WeightmapScratchExtractLayerTextureResource, 0, false);
										
					FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderParameters CSExtractLayersShaderParams;
					CSExtractLayersShaderParams.AtlasWeightmapsPerLayer = CurrentLayersWeightmapAllMaterialLayersResource;
					CSExtractLayersShaderParams.ComponentWeightmapResource = WeightmapScratchExtractLayerTextureResource;
					CSExtractLayersShaderParams.ComputeShaderResource = new FLandscapeLayerWeightmapExtractMaterialLayersComputeShaderResource(ComponentsData);
					CSExtractLayersShaderParams.ComponentSize = (SubsectionSizeQuads + 1) * NumSubsections;

					BeginInitResource(CSExtractLayersShaderParams.ComputeShaderResource);

					FLandscapeLayerWeightmapExtractMaterialLayersCSDispatch_RenderThread CSDispatch(CSExtractLayersShaderParams);

					ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_ExtractLayers)(
						[CSDispatch](FRHICommandListImmediate& RHICmdList) mutable
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RT_ExtractLayers);
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
					SourceDebugName = FString::Printf(TEXT("Weight: %s PaintLayer: %s, CurrentProcLayerWeightmapAllLayersResource"), *Layer.Name.ToString(), *LayerInfoObj->LayerName.ToString());
					DestDebugName = LandscapeScratchRT1->GetName();

					{
						FLandscapeLayersCopyTextureParams CopyTextureParams(SourceDebugName, CurrentLayersWeightmapAllMaterialLayersResource, DestDebugName, LandscapeScratchRT1->GameThread_GetRenderTargetResource());
						CopyTextureParams.SourceArrayIndex = LayerIndex;
						ExecuteCopyLayersTexture({ MoveTemp(CopyTextureParams) });
					}
					PrintLayersDebugRT(FString::Printf(TEXT("LS Weight: %s CurrentProcLayerWeightmapAllLayersResource -> Paint Layer RT %s"), *Layer.Name.ToString(), *LandscapeScratchRT1->GetName()), LandscapeScratchRT1, 0, false);

					PSShaderParams.ApplyLayerModifiers = true;
					PSShaderParams.LayerVisible = Layer.bVisible;
					PSShaderParams.LayerAlpha = LayerInfoObj == ALandscapeProxy::VisibilityLayer ? 1.0f : Layer.WeightmapAlpha; // visibility can't be affected by weight

					DrawWeightmapComponentsToRenderTarget(FString::Printf(TEXT("LS Weight: %s Paint: %s += -> %s"), *Layer.Name.ToString(), *LayerInfoObj->LayerName.ToString(), *LandscapeScratchRT1->GetName(), *LandscapeScratchRT2->GetName()),
														  InLandscapeComponentsToRender, LandscapeExtent.Min, LandscapeScratchRT1, nullptr, LandscapeScratchRT2, ERTDrawingType::RTAtlas, true, PSShaderParams, 0);

					PSShaderParams.ApplyLayerModifiers = false;

					// Combined Layer data with current stack
					SourceDebugName = FString::Printf(TEXT("Weight: %s PaintLayer: %s CombinedProcLayerWeightmap"), *Layer.Name.ToString(), *LayerInfoObj->LayerName.ToString());
					DestDebugName = LandscapeScratchRT1->GetName();

					{
						FLandscapeLayersCopyTextureParams CopyTextureParams(SourceDebugName, CombinedLayersWeightmapAllMaterialLayersResource, DestDebugName, LandscapeScratchRT1->GameThread_GetRenderTargetResource());
						CopyTextureParams.SourceArrayIndex = LayerIndex;
						ExecuteCopyLayersTexture({ MoveTemp(CopyTextureParams) });
					}
					PrintLayersDebugRT(FString::Printf(TEXT("LS Weight: %s CombinedProcLayerWeightmap -> Paint Layer RT %s"), *Layer.Name.ToString(), *LandscapeScratchRT1->GetName()), LandscapeScratchRT1, 0, false);

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

					DrawWeightmapComponentsToRenderTarget(FString::Printf(TEXT("LS Weight: %s PaintLayer: %s, %s += -> Combined %s"), *Layer.Name.ToString(), *LayerInfoObj->LayerName.ToString(), *LandscapeScratchRT2->GetName(), *LandscapeScratchRT3->GetName()),
														  InLandscapeComponentsToRender, LandscapeExtent.Min, LandscapeScratchRT2, bFirstLayer ? nullptr : LandscapeScratchRT1, LandscapeScratchRT3, ERTDrawingType::RTAtlasToNonAtlas, true, PSShaderParams, 0);

					PSShaderParams.OutputAsSubstractive = false;

					SourceDebugName = FString::Printf(TEXT("Weight: %s PaintLayer: %s %s"), *Layer.Name.ToString(), *LayerInfoObj->LayerName.ToString(), *LandscapeScratchRT3->GetName());
					DestDebugName = TEXT("CombinedProcLayerWeightmap");

					// Handle brush blending
					if (Layer.bVisible && !bSkipBrush)
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

							ALandscapeBlueprintBrushBase* LandscapeBrush = Brush.GetBrush();
							check(LandscapeBrush != nullptr); // If we managed to render, the brush should be valid

							BrushRequiredAllocations.AddUnique(LayerInfoObj);

							INC_DWORD_STAT(STAT_LandscapeLayersRegenerateDrawCalls); // Brush RenderInitialize

							PrintLayersDebugRT(FString::Printf(TEXT("LS Weight: %s %s -> Brush %s"), *Layer.Name.ToString(), *LandscapeBrush->GetName(), *BrushOutputRT->GetName()), BrushOutputRT);

							SourceDebugName = FString::Printf(TEXT("Weight: %s PaintLayer: %s Brush: %s"), *Layer.Name.ToString(), *LayerInfoObj->LayerName.ToString(), *BrushOutputRT->GetName());
							DestDebugName = LandscapeScratchRT3->GetName();

							ExecuteCopyLayersTexture({ FLandscapeLayersCopyTextureParams(SourceDebugName, BrushOutputRT->GameThread_GetRenderTargetResource(), DestDebugName, LandscapeScratchRT3->GameThread_GetRenderTargetResource()) });
							PrintLayersDebugRT(FString::Printf(TEXT("LS Weight: %s Component %s += -> Combined %s"), *Layer.Name.ToString(), *BrushOutputRT->GetName(), *LandscapeScratchRT3->GetName()), LandscapeScratchRT3);
						}

						PrintLayersDebugRT(FString::Printf(TEXT("LS Weight: %s CombinedPostBrushProcLayerWeightmap -> Paint Layer RT %s"), *Layer.Name.ToString(), *LandscapeScratchRT3->GetName()), LandscapeScratchRT3, 0, false);

						SourceDebugName = FString::Printf(TEXT("Weight: %s PaintLayer: %s %s"), *Layer.Name.ToString(), *LayerInfoObj->LayerName.ToString(), *LandscapeScratchRT3->GetName());
						DestDebugName = TEXT("CombinedProcLayerWeightmap");

						FLandscapeLayersCopyTextureParams CopyTextureParams(SourceDebugName, LandscapeScratchRT3->GameThread_GetRenderTargetResource(), DestDebugName, CombinedLayersWeightmapAllMaterialLayersResource);
						CopyTextureParams.DestArrayIndex = LayerIndex;
						ExecuteCopyLayersTexture({ MoveTemp(CopyTextureParams) });
					}

					DrawWeightmapComponentsToRenderTarget(FString::Printf(TEXT("LS Weight: %s Combined Scratch No Border to %s Combined Scratch with Border"), *LandscapeScratchRT3->GetName(), *LandscapeScratchRT1->GetName()),
						InLandscapeComponentsToRender, LandscapeExtent.Min, LandscapeScratchRT3, nullptr, LandscapeScratchRT1, ERTDrawingType::RTNonAtlasToAtlas, true, PSShaderParams, 0);

					FLandscapeLayersCopyTextureParams CopyTextureParams(SourceDebugName, LandscapeScratchRT1->GameThread_GetRenderTargetResource(), DestDebugName, CombinedLayersWeightmapAllMaterialLayersResource);
					CopyTextureParams.DestArrayIndex = LayerIndex;
					ExecuteCopyLayersTexture({ MoveTemp(CopyTextureParams) });
				}

				PSShaderParams.ApplyLayerModifiers = false;
			}

			bFirstLayer = false;
		}

		ReallocateLayersWeightmaps(InLandscapeComponentsToResolve, BrushRequiredAllocations);
		TSet<UTexture2D*> ToResolve;
		for (ULandscapeComponent* LandscapeComponentToResolve : InLandscapeComponentsToResolve)
		{
			const TArray<UTexture2D*>& ComponentWeightmapTextures = LandscapeComponentToResolve->GetWeightmapTextures();
			for (UTexture2D* WeightmapTexture : ComponentWeightmapTextures)
			{
				ToResolve.Add(WeightmapTexture);
			}
		}

		if (bHasWeightmapData)
		{
			// Lazily create CPU read back objects as required
			for (ULandscapeComponent* Component : InLandscapeComponentsToRender)
			{
				const TArray<UTexture2D*>& ComponentWeightmapTextures = Component->GetWeightmapTextures();

				for (UTexture2D* WeightmapTexture : ComponentWeightmapTextures)
				{
					ALandscapeProxy* Proxy = Component->GetLandscapeProxy();
					FLandscapeEditLayerReadback** CPUReadback = Proxy->WeightmapsCPUReadback.Find(WeightmapTexture);

					if (CPUReadback == nullptr)
					{
						FLandscapeEditLayerReadback* NewCPUReadback = new FLandscapeEditLayerReadback();
						const uint8* LockedMip = WeightmapTexture->Source.LockMip(0);
						const uint32 Hash = FLandscapeEditLayerReadback::CalculateHash(LockedMip, WeightmapTexture->GetSizeX() * WeightmapTexture->GetSizeY() * sizeof(FColor));
						NewCPUReadback->SetHash(Hash);
						WeightmapTexture->Source.UnlockMip(0);
						Proxy->WeightmapsCPUReadback.Add(WeightmapTexture, NewCPUReadback);
					}
				}
			}
			
			int8 CurrentWeightmapToProcessIndex = 0;
			bool HasFoundWeightmapToProcess = true; // try processing at least once

			TArray<float> WeightmapLayerWeightBlend;
			TArray<UTexture2D*> ProcessedWeightmaps;
			TArray<FLandscapeEditLayerReadback*> ProcessedCPUReadbackTextures;
			int32 NextTextureIndexToProcess = 0;

			// Generate the component data from the weightmap allocation that were done earlier and weight blend them if required (i.e renormalize)
			while (HasFoundWeightmapToProcess)
			{
				TArray<FLandscapeLayerWeightmapPackMaterialLayersComponentData> PackLayersComponentsData;
				PrepareComponentDataToPackMaterialLayersCS(CurrentWeightmapToProcessIndex, LandscapeExtent.Min, InLandscapeComponentsToRender, ProcessedWeightmaps, ProcessedCPUReadbackTextures, PackLayersComponentsData);
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
					SourceDebugName = *EmptyRT->GetName();
					DestDebugName = TEXT("Weight: Clear WeightmapScratchPackLayerTextureResource");

					CopyTexturePS(SourceDebugName, EmptyRT->GameThread_GetRenderTargetResource(), DestDebugName, WeightmapScratchPackLayerTextureResource);

					FLandscapeLayerWeightmapPackMaterialLayersComputeShaderParameters CSPackLayersShaderParams;
					CSPackLayersShaderParams.AtlasWeightmapsPerLayer = CombinedLayersWeightmapAllMaterialLayersResource;
					CSPackLayersShaderParams.ComponentWeightmapResource = WeightmapScratchPackLayerTextureResource;
					CSPackLayersShaderParams.ComputeShaderResource = new FLandscapeLayerWeightmapPackMaterialLayersComputeShaderResource(PackLayersComponentsData, WeightmapLayerWeightBlend, WeightmapTextureOutputOffset);
					CSPackLayersShaderParams.ComponentSize = ComponentSize;
					BeginInitResource(CSPackLayersShaderParams.ComputeShaderResource);

					FLandscapeLayerWeightmapPackMaterialLayersCSDispatch_RenderThread CSDispatch(CSPackLayersShaderParams);

					ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_PackLayers)(
						[CSDispatch](FRHICommandListImmediate& RHICmdList) mutable
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RT_PackLayers);
						CSDispatch.PackLayers(RHICmdList);
					});

					UTextureRenderTarget2D* CurrentRT = WeightmapRTList[(int32)EWeightmapRTType::WeightmapRT_Mip0];

					SourceDebugName = TEXT("WeightmapScratchTexture");
					DestDebugName = CurrentRT->GetName();
					
					CopyTexturePS(SourceDebugName, WeightmapScratchPackLayerTextureResource, DestDebugName, CurrentRT->GameThread_GetRenderTargetResource());
					DrawWeightmapComponentToRenderTargetMips(WeightmapTextureOutputOffset, CurrentRT, true, PSShaderParams);

					int32 StartTextureIndex = NextTextureIndexToProcess;

					TArray<FLandscapeLayersCopyTextureParams> DeferredCopyTextures;
					TArray<FLandscapeLayersCopyReadbackTextureParams> DeferredCopyReadbackTextures;
					for (; NextTextureIndexToProcess < ProcessedWeightmaps.Num(); ++NextTextureIndexToProcess)
					{
						UTexture2D* WeightmapTexture = ProcessedWeightmaps[NextTextureIndexToProcess];
						if (!ToResolve.Contains(WeightmapTexture))
						{
							continue;
						}

						FIntPoint TextureTopLeftPositionInAtlas(WeightmapTextureOutputOffset[NextTextureIndexToProcess - StartTextureIndex].X, WeightmapTextureOutputOffset[NextTextureIndexToProcess - StartTextureIndex].Y);

						int32 CurrentMip = 0;
						const int32 TextureSizeX = WeightmapTexture->Resource->GetSizeX();
						const int32 TextureSizeY = WeightmapTexture->Resource->GetSizeY();
						for (int32 MipRTIndex = (int32)EWeightmapRTType::WeightmapRT_Mip0; MipRTIndex < (int32)EWeightmapRTType::WeightmapRT_Count; ++MipRTIndex)
						{
							CurrentRT = WeightmapRTList[MipRTIndex];

							if (CurrentRT != nullptr)
							{
								SourceDebugName = CurrentRT->GetName();
								DestDebugName = FString::Printf(TEXT("Weightmap Mip: %d"), CurrentMip);

								FLandscapeLayersCopyTextureParams& CopyTextureParams = DeferredCopyTextures.Add_GetRef(FLandscapeLayersCopyTextureParams(SourceDebugName, CurrentRT->GameThread_GetRenderTargetResource(), DestDebugName, WeightmapTexture->Resource));
								// Only copy the size that's actually needed : 
								CopyTextureParams.CopySize.X = TextureSizeX >> CurrentMip;
								CopyTextureParams.CopySize.Y = TextureSizeY >> CurrentMip;
								// Copy from the composited texture's position to the top-left corner of the heightmap
								CopyTextureParams.SourcePosition.X = TextureTopLeftPositionInAtlas.X >> CurrentMip;
								CopyTextureParams.SourcePosition.Y = TextureTopLeftPositionInAtlas.Y >> CurrentMip;
								CopyTextureParams.DestMip = CurrentMip;
								++CurrentMip;
							}
						}

						// Init the CPU read back parameters. This includes a context containing the current component states.
						const int32 ReadbackIndex = DeferredCopyReadbackTextures.Add(FLandscapeLayersCopyReadbackTextureParams(WeightmapTexture, ProcessedCPUReadbackTextures[NextTextureIndexToProcess]));
						TArray<ULandscapeComponent*> const* Components = MapHelper.WeightmapToComponents.Find(WeightmapTexture);
						if (Components != nullptr)
						{
							for (ULandscapeComponent* Component : *Components)
							{
								const FIntPoint ComponentKey = Component->GetSectionBase() / Component->ComponentSizeQuads;
								const int32 ComponentFlags = Component->GetLayerUpdateFlagPerMode();
								DeferredCopyReadbackTextures[ReadbackIndex].Context.Add(FLandscapeEditLayerReadback::FComponentReadbackContext(ComponentKey, ComponentFlags));
							}
						}
					}
					ExecuteCopyLayersTexture(MoveTemp(DeferredCopyTextures));
					ExecuteCopyToReadbackTexture(DeferredCopyReadbackTextures);
				}

				++CurrentWeightmapToProcessIndex;
			}
		}

		UpdateLayersMaterialInstances(InLandscapeComponentsToResolve);

		RenderCaptureLayersNextWeightmapDraws = 0;
	}

	return WeightmapUpdateModes;
}

void ALandscape::UpdateForChangedWeightmaps(ULandscapeComponent* InComponent, const FLandscapeEditLayerReadbackResult& InReadbackResult)
{
	// If the source data has changed, mark the component as needing a collision layer data update:
	//  - If ELandscapeComponentUpdateFlag::Component_Update_Weightmap_Collision is passed, it will be done immediately
	//  - If not, at least the component's collision layer data will still get updated eventually, when the flag is finally passed :
	if (InReadbackResult.bModified)
	{
		InComponent->SetPendingLayerCollisionDataUpdate(true);
	}

	const int32 WeightUpdateMode = InReadbackResult.UpdateModes & (ELandscapeLayerUpdateMode::Update_Weightmap_All | ELandscapeLayerUpdateMode::Update_Weightmap_Editing | ELandscapeLayerUpdateMode::Update_Weightmap_Editing_NoCollision);

	if (IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag::Component_Update_Weightmap_Collision, WeightUpdateMode))
	{
		// Only update collision data if there was an actual change performed on the source data : 
		if (InComponent->GetPendingLayerCollisionDataUpdate())
		{
			InComponent->UpdateCollisionLayerData();
			InComponent->SetPendingLayerCollisionDataUpdate(false);
		}
	}
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

		Component->MaterialInstances.SetNumZeroed(Component->MaterialPerLOD.Num() * 2); // over allocate in case we are using tessellation
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

				// Setup material instance with disabled tessellation
PRAGMA_DISABLE_DEPRECATION_WARNINGS
				if (CombinationMaterialInstance->GetMaterial()->D3D11TessellationMode != EMaterialTessellationMode::MTM_NoTessellation)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
				{
					int32 TessellatedMaterialIndex = Component->MaterialPerLOD.Num() + TessellatedMaterialCount++;
					ULandscapeMaterialInstanceConstant* TessellationMaterialInstance = Cast<ULandscapeMaterialInstanceConstant>(Component->MaterialInstances[TessellatedMaterialIndex]);

					NeedToCreateMIC |= TessellationMaterialInstance == nullptr;
					if (NeedToCreateMIC)
					{
						TessellationMaterialInstance = NewObject<ULandscapeMaterialInstanceConstant>(this);
						Component->MaterialInstances[TessellatedMaterialIndex] = TessellationMaterialInstance;
					}

					Component->MaterialIndexToDisabledTessellationMaterial[MaterialIndex] = TessellatedMaterialIndex;
					TessellationMaterialInstance->bDisableTessellation = true;

					TessellationMaterialInstance->SetParentEditorOnly(MaterialInstance);

					MaterialUpdateContext.GetValue().AddMaterialInstance(TessellationMaterialInstance); // must be done after SetParent

					if (NeedToCreateMIC)
					{
						TessellationMaterialInstance->PostEditChange();
					}
					else
					{
						bHasUniformExpressionUpdatePending = true;
						TessellationMaterialInstance->RecacheUniformExpressions(true);
					}
				}
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
		ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_UpdateMaterial)(
			[](FRHICommandList& RHICmdList)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_RT_UpdateMaterial);
			FMaterialRenderProxy::UpdateDeferredCachedUniformExpressions();
		});
	}
}

void ALandscape::ResolveLayersWeightmapTexture(
	FTextureToComponentHelper const& MapHelper,
	TSet<UTexture2D*> const& WeightmapsToResolve,
	bool bIntermediateRender,
	bool bFlushRender,
	TMap<ULandscapeComponent*, FLandscapeEditLayerReadbackResult>& InOutComponents)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_ResolveLayersWeightmapTexture);
	SCOPE_CYCLE_COUNTER(STAT_LandscapeLayersResolveWeightmaps);

	ULandscapeInfo* Info = GetLandscapeInfo();
	if (Info == nullptr)
	{
		return;
	}

	FDirtyDelegate OnDirtyDelegate = [this, &MapHelper](UTexture2D const* Weightmap, FColor const* OldData, FColor const* NewData)
	{
		OnDirtyWeightmap(MapHelper, Weightmap, OldData, NewData);
	};

	TArray<ULandscapeComponent*> ChangedComponents;
	for (UTexture2D* Weightmap : WeightmapsToResolve)
	{
		ALandscapeProxy* LandscapeProxy = Weightmap->GetTypedOuter<ALandscapeProxy>();
		check(LandscapeProxy);
		if (FLandscapeEditLayerReadback** CPUReadback = LandscapeProxy->WeightmapsCPUReadback.Find(Weightmap))
		{
			const bool bChanged = ResolveLayersTexture(MapHelper, *CPUReadback, Weightmap, bIntermediateRender, bFlushRender, InOutComponents, OnDirtyDelegate);
			if (bChanged)
			{
				ChangedComponents.Append(MapHelper.WeightmapToComponents[Weightmap]);
				Weightmap->MarkPackageDirty();
			}
		}
	}

	// Weightmaps shouldn't invalidate lighting
	const bool bInvalidateLightingCache = false;
	InvalidateGeneratedComponentData(ChangedComponents, bInvalidateLightingCache);
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

void ALandscape::RequestSplineLayerUpdate()
{
	if (HasLayersContent() && GetLandscapeSplinesReservedLayer() != nullptr)
	{
		bSplineLayerUpdateRequested = true;
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
				for (ULandscapeComponent* Component : Proxy->LandscapeComponents)
				{
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

bool ALandscape::IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag InFlag, uint32 InUpdateModes)
{
	if (InUpdateModes & ELandscapeLayerUpdateMode::Update_Heightmap_All)
	{
		const uint32 HeightmapAllFlags = ELandscapeComponentUpdateFlag::Component_Update_Heightmap_Collision | ELandscapeComponentUpdateFlag::Component_Update_Recreate_Collision | ELandscapeComponentUpdateFlag::Component_Update_Client;
		if (HeightmapAllFlags & InFlag)
		{
			return true;
		}
	}
		
	if (InUpdateModes & ELandscapeLayerUpdateMode::Update_Heightmap_Editing)
	{
		const uint32 HeightmapEditingFlags = ELandscapeComponentUpdateFlag::Component_Update_Heightmap_Collision | ELandscapeComponentUpdateFlag::Component_Update_Client_Editing;
		if (HeightmapEditingFlags & InFlag)
		{
			return true;
		}
	}

	if (InUpdateModes & ELandscapeLayerUpdateMode::Update_Weightmap_All)
	{
		const uint32 WeightmapAllFlags = ELandscapeComponentUpdateFlag::Component_Update_Weightmap_Collision | ELandscapeComponentUpdateFlag::Component_Update_Recreate_Collision | ELandscapeComponentUpdateFlag::Component_Update_Client;
		if (WeightmapAllFlags & InFlag)
		{
			return true;
		}
	}

	if (InUpdateModes & ELandscapeLayerUpdateMode::Update_Weightmap_Editing)
	{
		const uint32 WeightmapEditingFlags = ELandscapeComponentUpdateFlag::Component_Update_Weightmap_Collision | ELandscapeComponentUpdateFlag::Component_Update_Client_Editing;
		if (WeightmapEditingFlags & InFlag)
		{
			return true;
		}
	}

	if (InUpdateModes & ELandscapeLayerUpdateMode::Update_Client_Editing)
	{
		const uint32 WeightmapEditingFlags = ELandscapeComponentUpdateFlag::Component_Update_Client_Editing;
		if (WeightmapEditingFlags & InFlag)
		{
			return true;
		}
	}

	if (InUpdateModes & ELandscapeLayerUpdateMode::Update_Client_Deferred)
	{
		const uint32 DeferredClientUpdateFlags = ELandscapeComponentUpdateFlag::Component_Update_Client;
		if (DeferredClientUpdateFlags & InFlag)
		{
			return true;
		}
	}

	if (InUpdateModes & (ELandscapeLayerUpdateMode::Update_Heightmap_Editing_NoCollision | ELandscapeLayerUpdateMode::Update_Weightmap_Editing_NoCollision))
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
	// Do not monitor changes when not editing Landscape
	if (!LandscapeEdMode)
	{
		return;
	}

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

void ALandscape::GetLandscapeComponentNeighborsToRender(ULandscapeComponent* LandscapeComponent, TSet<ULandscapeComponent*>& NeighborComponents) const
{
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	FIntPoint ComponentKey = LandscapeComponent->GetSectionBase() / ComponentSizeQuads;
	
	for (int32 IndexX = ComponentKey.X-1; IndexX <= ComponentKey.X+1; ++IndexX)
	{
		for (int32 IndexY = ComponentKey.Y-1; IndexY <= ComponentKey.Y+1; ++IndexY)
		{
			ULandscapeComponent* Result = LandscapeInfo->XYtoComponentMap.FindRef(FIntPoint(IndexX, IndexY));
			if (Result && Result != LandscapeComponent)
			{
				NeighborComponents.Add(Result);
			}
		}
	}
}

void ALandscape::GetLandscapeComponentWeightmapsToRender(ULandscapeComponent* LandscapeComponent, TSet<ULandscapeComponent*>& WeightmapComponents) const
{
	// Fill with Components that share the same weightmaps so that the Resolve of Weightmap Texture doesn't resolve null data.
	for (ULandscapeWeightmapUsage* Usage : LandscapeComponent->GetWeightmapTexturesUsage(false))
	{
		for (int32 Channel = 0; Channel < ULandscapeWeightmapUsage::NumChannels; ++Channel)
		{
			if (Usage != nullptr && Usage->ChannelUsage[Channel] != nullptr)
			{
				ULandscapeComponent* Component = Usage->ChannelUsage[Channel];
				Component->ForEachLayer([&](const FGuid& LayerGuid, FLandscapeLayerComponentData& LayerData)
				{
					for (ULandscapeWeightmapUsage* Usage : LayerData.WeightmapData.TextureUsages)
					{
						for (int32 Channel = 0; Channel < ULandscapeWeightmapUsage::NumChannels; ++Channel)
						{
							if (Usage != nullptr && Usage->ChannelUsage[Channel] != nullptr)
							{
								WeightmapComponents.Add(Usage->ChannelUsage[Channel]);
							}
						}
					}
				});
			}
		}
	}
}

bool ALandscape::AreLayersResourcesReady(bool bInWaitForStreaming) const
{
	const bool bHeightmapReady = PrepareLayersHeightmapTextureResources(bInWaitForStreaming);
	const bool bWeightmapReady = PrepareLayersWeightmapTextureResources(bInWaitForStreaming);
	const bool bBrushHeightmapReady = PrepareLayersBrushResources(bInWaitForStreaming, /* bHeightmap = */ true);
	const bool bBrushWeightmapReady = PrepareLayersBrushResources(bInWaitForStreaming, /* bHeightmap = */ false);
	return bHeightmapReady && bWeightmapReady && bBrushHeightmapReady && bBrushWeightmapReady;
}

void ALandscape::UpdateLayersContent(bool bInWaitForStreaming, bool bInSkipMonitorLandscapeEdModeChanges, bool bIntermediateRender, bool bFlushRender)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_UpdateLayersContent);

	bool bHideEditLayerResourcesNotification = true;
	ON_SCOPE_EXIT
	{
		// Make sure to hide the notification if necessary when we early-out :
		if (bHideEditLayerResourcesNotification)
		{
			HideEditLayersResourcesNotification(EditLayersResourcesNotification);
			WaitingForResourcesStartTime = -1.0;
		}
	};

	// Remove this command line switch after fixes for D3D12 RHI
	if (FParse::Param(FCommandLine::Get(), TEXT("nolandscapelayerupdate")))
	{
		return;
	}

	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	if (LandscapeInfo == nullptr || !CanHaveLayersContent() || !LandscapeInfo->AreAllComponentsRegistered())
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

	// Make sure Brush get a chance to request an update of the landscape
	for (FLandscapeLayer& Layer : LandscapeLayers)
	{
		for (FLandscapeLayerBrush& Brush : Layer.Brushes)
		{
			if (ALandscapeBlueprintBrushBase* LandscapeBrush = Brush.GetBrush())
			{
				LandscapeBrush->PushDeferredLayersContentUpdate();
			}
		}
	}

	if (bSplineLayerUpdateRequested)
	{
		UpdateLandscapeSplines();
		bSplineLayerUpdateRequested = false;
	}

	const bool bProcessReadbacks = FLandscapeEditLayerReadback::HasWork();
	const bool bForceRender = CVarForceLayersUpdate.GetValueOnAnyThread() != 0;

	if (LayerContentUpdateModes == 0 && !bForceRender && !bProcessReadbacks)
	{
		return;
	}

	if (!AreLayersResourcesReady(bInWaitForStreaming))
	{
		if (!bInWaitForStreaming)
		{
			static constexpr double TimeBeforeDisplayingNotification = 3.0;
			if (WaitingForResourcesStartTime < 0.0)
			{
				WaitingForResourcesStartTime = FSlateApplicationBase::Get().GetCurrentTime();
			}

			if ((FSlateApplicationBase::Get().GetCurrentTime() - WaitingForResourcesStartTime) > TimeBeforeDisplayingNotification)
			{
				// let the user know we are waiting for resources :
				static const FText NotificationText(LOCTEXT("WaitForLayersResources", "Landscape edition waiting for edit layers resources to be ready."));
				ShowEditLayersResourcesNotification(NotificationText, EditLayersResourcesNotification);
			}
			// The notification may not be visible yet (because of the initial delay) but it should not be hidden and the initial delay timer shouldn't be reset : 
			bHideEditLayerResourcesNotification = false;
		}
		return;
	}
	
	// Gather mappings between heightmaps/weightmaps and components
	FTextureToComponentHelper MapHelper(*LandscapeInfo);

	// Poll and complete any outstanding resolve work
	// If bIntermediateRender then we want to flush all work here before we do the intermediate render later on
	// if bFlushRender then we skip this because we will flush later anyway
	if (bProcessReadbacks && (bIntermediateRender || !bFlushRender))
	{
		// These flags might look like they're being mixed up but they're not!
		const bool bDoIntermediateRender = false; // bIntermediateRender flag is for the work queued up this frame not the delayed resolves
		const bool bDoFlushRender = bIntermediateRender; // Flush before we do an intermediate render later in this frame
		
		TMap<ULandscapeComponent*, FLandscapeEditLayerReadbackResult> ModifiedComponentInfo;
		ResolveLayersHeightmapTexture(MapHelper, MapHelper.Heightmaps, bDoIntermediateRender, bDoFlushRender, ModifiedComponentInfo);
		ResolveLayersWeightmapTexture(MapHelper, MapHelper.Weightmaps, bDoIntermediateRender, bDoFlushRender, ModifiedComponentInfo);

		LayerContentUpdateModes |= UpdateAfterReadbackResolves(ModifiedComponentInfo);
	}

	if (LayerContentUpdateModes == 0 && !bForceRender)
	{
		return;
	}

	bool bUpdateAll = LayerContentUpdateModes & Update_All;
	bool bPartialUpdate = !bForceRender && !bUpdateAll && CVarLandscapeLayerOptim.GetValueOnAnyThread() == 1;

	TSet<UTexture2D*> Heightmaps;
	TSet<UTexture2D*> HeightmapsToRender;
	TSet<UTexture2D*> Weightmaps;
	TSet<ULandscapeComponent*> NeighborsComponents;
	TSet<ULandscapeComponent*> WeightmapsComponents;
	TArray<ULandscapeComponent*> LandscapeComponentsHeightmapsToRender;
	TArray<ULandscapeComponent*> LandscapeComponentsHeightmapsToResolve;
	TArray<ULandscapeComponent*> LandscapeComponentsWeightmapsToRender;
	TArray<ULandscapeComponent*> LandscapeComponentsWeightmapsToResolve;
	TMap<ULandscapeComponent*, FLandscapeEditLayerReadbackResult> LandscapeComponentsToUpdate;
	TArray<ULandscapeComponent*> AllLandscapeComponents;

	TArray<ULandscapeComponent*> SkippedComponents;
	for (ULandscapeComponent* Component : MapHelper.LandscapeComponents)
	{
		if (!bPartialUpdate || Component->GetLayerUpdateFlagPerMode() != 0)
		{
			AllLandscapeComponents.Add(Component);
		
			if (bPartialUpdate)
			{
				// Gather Neighbors (Neighbors need to be Rendered but not resolved so that the resolved Components have valid normals on edges)
				GetLandscapeComponentNeighborsToRender(Component, NeighborsComponents);
				// Gather Heightmaps (All Components sharing Heightmap textures need to be rendered and resolved)
				Heightmaps.Add(Component->GetHeightmap(false));
				Component->ForEachLayer([&](const FGuid&, FLandscapeLayerComponentData& LayerData)
				{
					HeightmapsToRender.Add(LayerData.HeightmapData.Texture);
				});
				// Gather Weightmaps
				TArray<UTexture2D*>& WeightmapTextures = Component->GetWeightmapTextures();
				for (FWeightmapLayerAllocationInfo const& AllocInfo : Component->GetWeightmapLayerAllocations())
				{
					if (AllocInfo.IsAllocated() && AllocInfo.WeightmapTextureIndex < WeightmapTextures.Num())
					{
						Weightmaps.Add(WeightmapTextures[AllocInfo.WeightmapTextureIndex]);
					}
				}
				// Gather WeightmapUsages (Components sharing weightmap usages with the resolved Components need to be rendered so that the resolving is valid)
				GetLandscapeComponentWeightmapsToRender(Component, WeightmapsComponents);
			}
		}
		else
		{
			SkippedComponents.Add(Component);
		}
	}

	// Because of Heightmap Sharing anytime we render a heightmap we need to render all the components that use it
	for (ULandscapeComponent* NeighborsComponent : NeighborsComponents)
	{
		NeighborsComponent->ForEachLayer([&](const FGuid&, FLandscapeLayerComponentData& LayerData)
		{
			HeightmapsToRender.Add(LayerData.HeightmapData.Texture);
		});
	}

	// Copy first list into others
	LandscapeComponentsHeightmapsToResolve.Append(AllLandscapeComponents);
	LandscapeComponentsHeightmapsToRender.Append(AllLandscapeComponents);
	LandscapeComponentsWeightmapsToResolve.Append(AllLandscapeComponents);
	LandscapeComponentsWeightmapsToRender.Append(AllLandscapeComponents);
		
	if (bPartialUpdate)
	{
		for (ULandscapeComponent* Component : SkippedComponents)
		{
			if (Heightmaps.Contains(Component->GetHeightmap(false)))
			{
				AllLandscapeComponents.Add(Component);
				LandscapeComponentsHeightmapsToRender.Add(Component);
				LandscapeComponentsHeightmapsToResolve.Add(Component);
			}
			else if (NeighborsComponents.Contains(Component))
			{
				LandscapeComponentsHeightmapsToRender.Add(Component);
			}
			else
			{
				bool bAdd = false;
				Component->ForEachLayer([&](const FGuid&, FLandscapeLayerComponentData& LayerData)
				{
					if (HeightmapsToRender.Contains(LayerData.HeightmapData.Texture))
					{
						bAdd = true;
					}
				});
				if (bAdd)
				{
					LandscapeComponentsHeightmapsToRender.Add(Component);
				}
			}

			if (WeightmapsComponents.Contains(Component))
			{
				LandscapeComponentsWeightmapsToRender.Add(Component);
			}
		}
	}

	// Add components with deferred flag to update list
	for (ULandscapeComponent* Component : AllLandscapeComponents)
	{
		if (Component->GetLayerUpdateFlagPerMode() & ELandscapeLayerUpdateMode::Update_Client_Deferred)
		{
			LandscapeComponentsToUpdate.Add(Component, FLandscapeEditLayerReadbackResult(ELandscapeLayerUpdateMode::Update_Client_Deferred, false));
		}
	}

	// Regenerate any heightmaps and weightmaps
	int32 ProcessedModes = 0;
	ProcessedModes |= RegenerateLayersHeightmaps(MapHelper, LandscapeComponentsHeightmapsToRender, LandscapeComponentsHeightmapsToResolve);
	ProcessedModes |= RegenerateLayersWeightmaps(MapHelper, LandscapeComponentsWeightmapsToRender, LandscapeComponentsWeightmapsToResolve);
	ProcessedModes |= (LayerContentUpdateModes & ELandscapeLayerUpdateMode::Update_Client_Deferred);
	ProcessedModes |= (LayerContentUpdateModes & ELandscapeLayerUpdateMode::Update_Client_Editing);

	// If we are flushing then read back resolved textures immediately
	if (bFlushRender || CVarLandscapeForceFlush.GetValueOnGameThread() != 0)
	{
		const bool bDoFlushRender = true;
		ResolveLayersHeightmapTexture(MapHelper, Heightmaps, bIntermediateRender, bDoFlushRender, LandscapeComponentsToUpdate);
		ResolveLayersWeightmapTexture(MapHelper, Weightmaps, bIntermediateRender, bDoFlushRender, LandscapeComponentsToUpdate);
	}

	// Clear processed mode flags
	LayerContentUpdateModes &= ~ProcessedModes;
	for (ULandscapeComponent* Component : AllLandscapeComponents)
	{
		Component->ClearUpdateFlagsForModes(ProcessedModes);
	}

	// Apply post resolve updates
	const int32 ToProcessModes = UpdateAfterReadbackResolves(LandscapeComponentsToUpdate);
	LayerContentUpdateModes |= ToProcessModes;

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

int32 ALandscape::UpdateCollisionAndClients(TMap<ULandscapeComponent*, FLandscapeEditLayerReadbackResult> const& Components)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_PostResolve_CollisionAndClients);

	bool bAllClientsUpdated = true;

	const uint16 DefaultHeightValue = LandscapeDataAccess::GetTexHeight(0.f);
	const uint8 MaxLayerContributingValue = UINT8_MAX;
	const float HeightValueNormalizationFactor = 1.f / (0.5f * UINT16_MAX);
	TArray<uint16> HeightData;
	TArray<uint8> LayerContributionMaskData;

	for (auto Pair : Components)
	{
		ULandscapeComponent* LandscapeComponent = Pair.Key;
		const int32 UpdateModes = Pair.Value.UpdateModes;

		bool bDeferClientUpdateForComponent = false;
		bool bDoUpdateClient = true;
		if (IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag::Component_Update_Recreate_Collision, UpdateModes))
		{
			if (ULandscapeHeightfieldCollisionComponent* CollisionComp = LandscapeComponent->CollisionComponent.Get())
			{
				FEnableCollisionHashOptimScope Scope(CollisionComp);
				bDoUpdateClient = CollisionComp->RecreateCollision();
			}
		}

		if (bDoUpdateClient && IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag::Component_Update_Client, UpdateModes))
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

		if (IsUpdateFlagEnabledForModes(ELandscapeComponentUpdateFlag::Component_Update_Client_Editing, UpdateModes))
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
							LayerContributionMaskData[i] = HeightData[i] != DefaultHeightValue ? (uint8)(FMath::Pow(FMath::Clamp((HeightValueNormalizationFactor * FMath::Abs(HeightData[i] - DefaultHeightValue)), 0.f, (float)1.f), 0.25f) * MaxLayerContributingValue) : 0;
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

		if (bDeferClientUpdateForComponent)
		{
			LandscapeComponent->RequestDeferredClientUpdate();
		}
	}

	// Some clients not updated so return the Deferred flag to trigger processing next update.
	return bAllClientsUpdated ? 0 : ELandscapeLayerUpdateMode::Update_Client_Deferred;
}

int32 ALandscape::UpdateAfterReadbackResolves(TMap<ULandscapeComponent*, FLandscapeEditLayerReadbackResult> const& Components)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_PostResolve_Updates);

	int32 NewUpdateFlags = 0;

	if (Components.Num())
	{
		for (auto Pair : Components)
		{
			UpdateForChangedHeightmaps(Pair.Key, Pair.Value);
			UpdateForChangedWeightmaps(Pair.Key, Pair.Value);
		}

		GetLandscapeInfo()->UpdateAllAddCollisions();

		NewUpdateFlags |= UpdateCollisionAndClients(Components);
	}

	return NewUpdateFlags;
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

void ALandscape::ForceUpdateLayersContent(bool bIntermediateRender)
{
	const bool bWaitForStreaming = true;
	const bool bInSkipMonitorLandscapeEdModeChanges = true;
	const bool bFlushRender = true;

	UpdateLayersContent(bWaitForStreaming, bInSkipMonitorLandscapeEdModeChanges, bIntermediateRender, bFlushRender);
}

void ALandscape::TickLayers(float DeltaTime)
{
	check(GIsEditor);

	UWorld* World = GetWorld();
	if (World && !World->IsPlayInEditor() && GetLandscapeInfo() && GEditor->PlayWorld == nullptr)
	{
		if (CVarLandscapeSimulatePhysics.GetValueOnAnyThread() == 1)
		{
			World->bShouldSimulatePhysics = true;
		}

		UpdateLayersContent();
	}

	FLandscapeEditLayerReadback::GarbageCollectTasks();
}

#endif

void ALandscapeProxy::BeginDestroy()
{
#if WITH_EDITORONLY_DATA
	if (CanHaveLayersContent())
	{
		// Note that this fence and the BeginDestroy/FinishDestroy is only actually used by the derived class ALandscape
		// It could be moved there
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

		for (auto& ItPair : HeightmapsCPUReadback)
		{
			FLandscapeEditLayerReadback* HeightmapCPUReadback = ItPair.Value;
			delete HeightmapCPUReadback;
		}
		HeightmapsCPUReadback.Empty();

		for (auto& ItPair : WeightmapsCPUReadback)
		{
			FLandscapeEditLayerReadback* WeightmapCPUReadback = ItPair.Value;
			delete WeightmapCPUReadback;
		}
		WeightmapsCPUReadback.Empty();
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
	return bHasLayersContent || (GetLandscapeActor() != nullptr && GetLandscapeActor()->HasLayersContent());
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
			FFormatNamedArguments Arguments;
			Arguments.Add(TEXT("LayerGuid"), FText::FromString(LayerGuid.ToString(EGuidFormats::HexValuesInBraces)));
			Arguments.Add(TEXT("ProxyPackage"), FText::FromString(GetOutermost()->GetName()));

			FMessageLog("MapCheck").Warning()
				->AddToken(FUObjectToken::Create(this))
				->AddToken(FTextToken::Create(FText::Format(LOCTEXT("MapCheck_Message_LandscapeProxyObsoleteLayer","Layer '{LayerGuid}' was removed from LandscapeProxy because it doesn't match any of the LandscapeActor Layers. Please resave '{ProxyPackage}'."), Arguments)))
				->AddToken(FMapErrorToken::Create(FMapErrors::LandscapeComponentPostLoad_Warning));

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
	if (CanHaveLayersContent() && GetWorld() != nullptr && !GetWorld()->IsGameWorld())
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

	Modify(false);
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

const FLandscapeLayer* ALandscape::GetLayer(const FName& InLayerName) const
{
	return LandscapeLayers.FindByPredicate([InLayerName](const FLandscapeLayer& Layer) { return Layer.Name == InLayerName; });
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

void ALandscape::CollapseLayer(int32 InLayerIndex)
{
	FScopedSlowTask SlowTask(GetLandscapeInfo()->XYtoComponentMap.Num(), LOCTEXT("Landscape_CollapseLayer_SlowWork", "Collapsing Layer..."));
	SlowTask.MakeDialog();
	TArray<bool> BackupVisibility;
	TArray<bool> BackupBrushVisibility;
	for (int32 i = 0; i < LandscapeLayers.Num(); ++i)
	{
		BackupVisibility.Add(LandscapeLayers[i].bVisible);
		LandscapeLayers[i].bVisible = i == InLayerIndex || i == InLayerIndex - 1;
	}

	for (int32 i = 0; i < LandscapeLayers[InLayerIndex].Brushes.Num(); ++i)
	{
		BackupBrushVisibility.Add(LandscapeLayers[InLayerIndex].Brushes[i].GetBrush()->IsVisible());
		LandscapeLayers[InLayerIndex].Brushes[i].GetBrush()->SetIsVisible(false);
	}

	// Call Request Update on all components...
	GetLandscapeInfo()->ForAllLandscapeComponents([](ULandscapeComponent* LandscapeComponent)
	{
		LandscapeComponent->RequestWeightmapUpdate(false, false);
		LandscapeComponent->RequestHeightmapUpdate(false, false);
	});

	const bool bLocalIntermediateRender = true;
	ForceUpdateLayersContent(bLocalIntermediateRender);

	// Do copy
	{
		FLandscapeEditDataInterface DataInterface(GetLandscapeInfo());
		DataInterface.SetShouldDirtyPackage(true);

		TSet<UTexture2D*> ProcessedHeightmaps;
		FScopedSetLandscapeEditingLayer ScopeEditingLayer(this, LandscapeLayers[InLayerIndex - 1].Guid);
		GetLandscapeInfo()->ForAllLandscapeComponents([&](ULandscapeComponent* LandscapeComponent)
		{
			SlowTask.EnterProgressFrame(1.f);
			LandscapeComponent->CopyFinalLayerIntoEditingLayer(DataInterface, ProcessedHeightmaps);
		});
	}
	
	TArray<ALandscapeBlueprintBrushBase*> BrushesToMove;
	for (int32 i = 0; i < LandscapeLayers[InLayerIndex].Brushes.Num(); ++i)
	{
		ALandscapeBlueprintBrushBase* CurrentBrush = LandscapeLayers[InLayerIndex].Brushes[i].GetBrush();
		CurrentBrush->SetIsVisible(BackupBrushVisibility[i]);
		BrushesToMove.Add(CurrentBrush);
	}

	for (ALandscapeBlueprintBrushBase* Brush : BrushesToMove)
	{
		RemoveBrushFromLayer(InLayerIndex, Brush);
		AddBrushToLayer(InLayerIndex - 1, Brush);
	}

	for (int32 i = 0; i < LandscapeLayers.Num(); ++i)
	{
		LandscapeLayers[i].bVisible = BackupVisibility[i];
	}

	DeleteLayer(InLayerIndex);
	
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

void ALandscape::ClearLayer(const FGuid& InLayerGuid, TSet<ULandscapeComponent*>* InComponents, ELandscapeClearMode InClearMode, bool bMarkPackageDirty)
{
	ensure(HasLayersContent());

	const FLandscapeLayer* Layer = GetLayer(InLayerGuid);
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	if (!LandscapeInfo || !Layer)
	{
		return;
	}

	Modify(bMarkPackageDirty);
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
					Proxy->Modify(bMarkPackageDirty);
				}
			}
		}
	}
	else
	{
		LandscapeInfo->ForAllLandscapeProxies([&](ALandscapeProxy* Proxy)
		{
			Proxy->Modify(bMarkPackageDirty);
			Components.Append(Proxy->LandscapeComponents);
		});
	}

	FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo);
	FLandscapeDoNotDirtyScope DoNotDirtyScope(LandscapeEdit, !bMarkPackageDirty);
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

uint32 ULandscapeComponent::ComputeLayerHash() const
{
	UTexture2D* Heightmap = GetHeightmap(true);
	const uint8* MipData = Heightmap->Source.LockMip(0);
	uint32 Hash = FCrc::MemCrc32(MipData, Heightmap->GetSizeX()*Heightmap->GetSizeY() * sizeof(FColor));
	Heightmap->Source.UnlockMip(0);

	// Copy to sort
	const TArray<UTexture2D*>& Weightmaps = GetWeightmapTextures(true);
	TArray<FWeightmapLayerAllocationInfo> AllocationInfos = GetWeightmapLayerAllocations(true);
	
    // Sort allocations infos by LayerInfo Path so the Weightmaps hahses get ordered properly
    AllocationInfos.Sort([](const FWeightmapLayerAllocationInfo& A, const FWeightmapLayerAllocationInfo& B)
	{
		FString PathA(A.LayerInfo ? A.LayerInfo->GetPathName() : FString());
		FString PathB(B.LayerInfo ? B.LayerInfo->GetPathName() : FString());

		return PathA < PathB;
	});

	for (const FWeightmapLayerAllocationInfo& AllocationInfo : AllocationInfos)
	{
		if (AllocationInfo.IsAllocated())
		{
            // Compute hash of actual data of the texture that is owned by the component (per Texture Channel)
			UTexture2D* Weightmap = Weightmaps[AllocationInfo.WeightmapTextureIndex];
			MipData = Weightmap->Source.LockMip(0) + ChannelOffsets[AllocationInfo.WeightmapTextureChannel];
			TArray<uint8> ChannelData;
			ChannelData.AddDefaulted(Weightmap->GetSizeX()*Weightmap->GetSizeY());
			int32 TexSize = (SubsectionSizeQuads + 1)*NumSubsections;
			for (int32 TexY = 0; TexY < TexSize; TexY++)
			{
				for (int32 TexX = 0; TexX < TexSize; TexX++)
				{
					const int32 Index = (TexX + TexY * TexSize);
				
					ChannelData.GetData()[Index] = MipData[4 * Index];
				}
			}

			Hash = FCrc::MemCrc32(ChannelData.GetData(), Weightmap->GetSizeX()*Weightmap->GetSizeY(), Hash);
			Weightmap->Source.UnlockMip(0);
		}
	}

	return Hash;
}

void ALandscape::UpdateLandscapeSplines(const FGuid& InTargetLayer, bool bInUpdateOnlySelected, bool bInForceUpdateAllCompoments)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_UpdateLandscapeSplines);
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
			// Check that we can modify data
			if (!LandscapeInfo->AreAllComponentsRegistered())
			{
				return;
			}
						
			TMap<ULandscapeComponent*, uint32> PreviousHashes;
			{
				FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo);
				
				LandscapeInfo->ForAllLandscapeComponents([&](ULandscapeComponent* Component)
				{
					// Was never computed
					if (Component->SplineHash == 0)
					{
						Component->Modify(); // mark package dirty
						Component->SplineHash = DefaultSplineHash;
					}

					PreviousHashes.Add(Component, Component->SplineHash);
					Component->Modify(false);
					Component->SplineHash = DefaultSplineHash;
				});
			}

			// Clear layers without affecting weightmap allocations
			const bool bMarkPackageDirty = false;
			ClearLayer(LandscapeSplinesTargetLayerGuid, (!bInForceUpdateAllCompoments && LandscapeSplinesAffectedComponents.Num()) ? &LandscapeSplinesAffectedComponents : nullptr, Clear_All, bMarkPackageDirty);
			LandscapeSplinesAffectedComponents.Empty();
			ModifiedComponent = &LandscapeSplinesAffectedComponents;
			// For now, in Landscape Layer System Mode with a reserved layer for splines, we always update all the splines since we clear the whole layer first
			bInUpdateOnlySelected = false;

			// Apply splines without clearing up weightmap allocations
			LandscapeInfo->ApplySplines(bInUpdateOnlySelected, ModifiedComponent, bMarkPackageDirty);		
		
			for (const TPair<ULandscapeComponent*, uint32>& Pair : PreviousHashes)
			{
				if (LandscapeSplinesAffectedComponents.Contains(Pair.Key))
				{
					uint32 NewHash = Pair.Key->ComputeLayerHash();
					if (NewHash != Pair.Value)
					{
						Pair.Key->MarkPackageDirty();
					}
					Pair.Key->SplineHash = NewHash;
				}
				else if (Pair.Key->SplineHash == DefaultSplineHash && Pair.Value != DefaultSplineHash)
				{
					Pair.Key->MarkPackageDirty();
				}
			}
		}
		else
		{
			LandscapeInfo->ApplySplines(bInUpdateOnlySelected, ModifiedComponent);
		}
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

int32 ALandscape::GetBrushLayer(class ALandscapeBlueprintBrushBase* InBrush) const
{
	for (int32 LayerIndex = 0; LayerIndex < LandscapeLayers.Num(); ++LayerIndex)
	{
		for (const FLandscapeLayerBrush& Brush : LandscapeLayers[LayerIndex].Brushes)
		{
			if (Brush.GetBrush() == InBrush)
			{
				return LayerIndex;
			}
		}
	}

	return INDEX_NONE;
}

void ALandscape::AddBrushToLayer(int32 InLayerIndex, ALandscapeBlueprintBrushBase* InBrush)
{
	check(GetBrushLayer(InBrush) == INDEX_NONE);
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
	int32 LayerIndex = GetBrushLayer(InBrush);
	if (LayerIndex != INDEX_NONE)
	{
		RemoveBrushFromLayer(LayerIndex, InBrush);
	}
}

void ALandscape::RemoveBrushFromLayer(int32 InLayerIndex, ALandscapeBlueprintBrushBase* InBrush)
{
	int32 BrushIndex = GetBrushIndexForLayer(InLayerIndex, InBrush);
	if (BrushIndex != INDEX_NONE)
	{
		RemoveBrushFromLayer(InLayerIndex, BrushIndex);
	}
}

void ALandscape::RemoveBrushFromLayer(int32 InLayerIndex, int32 InBrushIndex)
{
	if (FLandscapeLayer* Layer = GetLayer(InLayerIndex))
	{
		if (Layer->Brushes.IsValidIndex(InBrushIndex))
		{
			Modify();
			ALandscapeBlueprintBrushBase* Brush = Layer->Brushes[InBrushIndex].GetBrush();
			Layer->Brushes.RemoveAt(InBrushIndex);
			if (Brush != nullptr)
			{
				Brush->SetOwningLandscape(nullptr);
			}
			RequestLayersContentUpdateForceAll();
		}
	}
}

int32 ALandscape::GetBrushIndexForLayer(int32 InLayerIndex, ALandscapeBlueprintBrushBase* InBrush)
{
	if (FLandscapeLayer* Layer = GetLayer(InLayerIndex))
	{
		for (int32 i = 0; i < Layer->Brushes.Num(); ++i)
		{
			if (Layer->Brushes[i].GetBrush() == InBrush)
			{
				return i;
			}
		}
	}

	return INDEX_NONE;
}


void ALandscape::OnBlueprintBrushChanged()
{
#if WITH_EDITORONLY_DATA
	LandscapeBlueprintBrushChangedDelegate.Broadcast();
	RequestLayersContentUpdateForceAll();
#endif
}

void ALandscape::OnLayerInfoSplineFalloffModulationChanged(ULandscapeLayerInfoObject* InLayerInfo)
{
	ULandscapeInfo* LandscapeInfo = GetLandscapeInfo();
	
	if (!LandscapeInfo)
	{
		return;
	}
	
	ALandscape* Landscape = LandscapeInfo->LandscapeActor.Get();
	if (!Landscape || !Landscape->HasLayersContent())
	{
		return;
	}

	bool bUsedForSplines = false;
	LandscapeInfo->ForAllLandscapeProxies([&](ALandscapeProxy* Proxy)
	{
		bUsedForSplines |= (Proxy->SplineComponent && Proxy->SplineComponent->IsUsingLayerInfo(InLayerInfo));
	});

	if (bUsedForSplines)
	{
		Landscape->RequestSplineLayerUpdate();
	}
}

ALandscapeBlueprintBrushBase* ALandscape::GetBrushForLayer(int32 InLayerIndex, int32 InBrushIndex) const
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
	TRACE_CPUPROFILER_EVENT_SCOPE(LandscapeLayers_LayerBrushRender);
	if ((InIsHeightmap && !IsAffectingHeightmap()) ||
		(!InIsHeightmap && !IsAffectingWeightmapLayer(InWeightmapLayerName)))
	{
		return nullptr;
	}
	if (Initialize(InLandscapeExtent, InLandscapeRenderTarget))
	{
		FString ProfilingEventName = FString::Format(TEXT("LandscapeLayers_RenderLayerBrush {0}: {1}"), { InIsHeightmap ? TEXT("LS Height") : TEXT("LS Weight"), BlueprintBrush->GetName() });
		TSharedPtr<FDrawEvent> ProfilingEvent = MakeShared<FDrawEvent>();

		ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_BeginRenderLayerBrush)(
			[ProfilingEvent, ProfilingEventName](FRHICommandListImmediate& RHICmdList) mutable
		{
			BEGIN_DRAW_EVENTF(RHICmdList, LandscapeLayers, *ProfilingEvent.Get(), *ProfilingEventName);
		});

		TGuardValue<bool> AutoRestore(GAllowActorScriptExecutionInEditor, true);
		UTextureRenderTarget2D* Result = BlueprintBrush->Render(InIsHeightmap, InLandscapeRenderTarget, InWeightmapLayerName);

		ENQUEUE_RENDER_COMMAND(LandscapeLayers_Cmd_StopRenderLayerBrush)(
			[ProfilingEvent](FRHICommandList& RHICmdList)
		{
			STOP_DRAW_EVENT(*ProfilingEvent.Get());
		});

		return Result;
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
