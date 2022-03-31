// Copyright Epic Games, Inc. All Rights Reserved.

#include "Strata.h"
#include "HAL/IConsoleManager.h"
#include "PixelShaderUtils.h"
#include "SceneView.h"
#include "ScenePrivate.h"
#include "SceneRendering.h"
#include "RendererInterface.h"
#include "UniformBuffer.h"
#include "SceneTextureParameters.h"
#include "ScreenPass.h"
#include "ShaderCompiler.h"

//PRAGMA_DISABLE_OPTIMIZATION

// The project setting for Strata
static TAutoConsoleVariable<int32> CVarStrata(
	TEXT("r.Strata"),
	0,
	TEXT("Enable Strata materials (Beta)."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarStrataBackCompatibility(
	TEXT("r.StrataBackCompatibility"),
	0,
	TEXT("Disables Strata multiple scattering and replaces Chan diffuse by Lambert."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarStrataBytePerPixel(
	TEXT("r.Strata.BytesPerPixel"),
	80,
	TEXT("Strata allocated byte per pixel to store materials data. Higher value means more complex material can be represented."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarStrataClassificationDebug(
	TEXT("r.Strata.Classification.Debug"),
	0,
	TEXT("Enable strata classification visualization: 1 shows simple material tiles in green and complex material tiles in red."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarStrataRoughDiffuse(
	TEXT("r.Strata.RoughDiffuse"),
	1,
	TEXT("Enable Strata rough diffuse model (works only if r.Material.RoughDiffuse is enabled in the project settings). Togglable at runtime"),
	ECVF_RenderThreadSafe);

// Transition render settings that will disapear when strata gets enabled

static TAutoConsoleVariable<int32> CVarMaterialRoughDiffuse(
	TEXT("r.Material.RoughDiffuse"),
	0,
	TEXT("Enable rough diffuse material."),
	ECVF_ReadOnly | ECVF_RenderThreadSafe);

// STRATA_TODO we keep this for now and can remove it once battletested.
static TAutoConsoleVariable<int32> CVarClearDuringCategorization(
	TEXT("r.Strata.ClearDuringCategorization"),
	1,
	TEXT("TEST."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarStrataTileOverflow(
	TEXT("r.Strata.TileOverflow"),
	1.f,
	TEXT("Scale the number of Strata tile for overflowing tiles containing multi-BSDFs pixels. (0: 0%, 1: 100%. Default 1.0f)."),
	ECVF_RenderThreadSafe);

IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FStrataGlobalUniformParameters, "Strata");

void FStrataSceneData::Reset()
{
	TopLayerTexture = nullptr;
	TopLayerTextureUAV = nullptr;
	SSSTexture = nullptr;
	SSSTextureUAV = nullptr;
	OpaqueRoughRefractionTexture = nullptr;
	OpaqueRoughRefractionTextureUAV = nullptr;

	MaterialTextureArray = nullptr;
	MaterialTextureArrayUAVWithoutRTs = nullptr;
	MaterialTextureArrayUAV = nullptr;
	MaterialTextureArraySRV = nullptr;

	ClassificationTileDrawIndirectBuffer = nullptr;
	ClassificationTileDrawIndirectBufferUAV = nullptr;

	ClassificationTileDispatchIndirectBuffer = nullptr;
	ClassificationTileDispatchIndirectBufferUAV = nullptr;

	for (uint32 i = 0; i < EStrataTileType::ECount; ++i)
	{
		ClassificationTileListBuffer[i] = nullptr;
		ClassificationTileListBufferUAV[i] = nullptr;
		ClassificationTileListBufferSRV[i] = nullptr;
	}

	SeparatedSubSurfaceSceneColor = nullptr;
	SeparatedOpaqueRoughRefractionSceneColor = nullptr;

	BSDFOffsetTexture = nullptr;
	BSDFTileTexture = nullptr;

	TileCount_Total = FIntPoint(0,0);
	TileCount_Primary = FIntPoint(0,0);
	TileCount_Overflow = FIntPoint(0,0);

	StrataGlobalUniformParameters = nullptr;
}

const TCHAR* ToString(EStrataTileType Type)
{
	switch (Type)
	{
	case EStrataTileType::ESimple:							return TEXT("Simple");
	case EStrataTileType::ESingle:							return TEXT("Single");
	case EStrataTileType::EComplex:							return TEXT("Complex");
	case EStrataTileType::EOpaqueRoughRefraction:			return TEXT("OpaqueRoughRefraction");
	case EStrataTileType::ESSSWithoutOpaqueRoughRefraction:	return TEXT("SSSWithoutOpaqueRoughRefraction");
	}
	return TEXT("Unknown");
}

FORCEINLINE bool ClearDuringCategorization()
{
	return CVarClearDuringCategorization.GetValueOnRenderThread() > 0;
}

namespace Strata
{

// Forward declaration
static void AddStrataClearMaterialBufferPass(
	FRDGBuilder& GraphBuilder, 
	FRDGTextureUAVRef MaterialTextureArrayUAV,
	FRDGTextureUAVRef SSSTextureUAV,
	uint32 MaxBytesPerPixel, 
	FIntPoint TiledViewBufferResolution);

bool IsStrataEnabled()
{
	return CVarStrata.GetValueOnAnyThread() > 0;
}

static FIntPoint GetStrataTextureTileResolution(const FIntPoint& InResolution, float Overflow)
{
	FIntPoint Out = InResolution;
	if (Strata::IsStrataEnabled())
	{
		Out.X = FMath::DivideAndRoundUp(Out.X, STRATA_TILE_SIZE);
		Out.Y = FMath::DivideAndRoundUp(Out.Y, STRATA_TILE_SIZE);
		Out.Y += FMath::CeilToInt(Out.Y * FMath::Clamp(Overflow, 0.f, 1.0f));
	}
	return Out;
}

static FIntPoint GetStrataTextureTileResolution(const FIntPoint& InResolution)
{
	return GetStrataTextureTileResolution(InResolution, CVarStrataTileOverflow.GetValueOnRenderThread());
}

FIntPoint GetStrataTextureResolution(const FIntPoint& InResolution)
{
	return GetStrataTextureTileResolution(InResolution) * STRATA_TILE_SIZE;
}

void InitialiseStrataFrameSceneData(FSceneRenderer& SceneRenderer, FRDGBuilder& GraphBuilder)
{
	FStrataSceneData& StrataSceneData = SceneRenderer.Scene->StrataSceneData;
	StrataSceneData.Reset();

	auto UpdateMaterialBufferToTiledResolution = [](FIntPoint InBufferSizeXY, FIntPoint& OutMaterialBufferSizeXY)
	{
		// We need to allocate enough for the tiled memory addressing to always work
		OutMaterialBufferSizeXY.X = FMath::DivideAndRoundUp(InBufferSizeXY.X, STRATA_TILE_SIZE) * STRATA_TILE_SIZE;
		OutMaterialBufferSizeXY.Y = FMath::DivideAndRoundUp(InBufferSizeXY.Y, STRATA_TILE_SIZE) * STRATA_TILE_SIZE;
	};

	FIntPoint MaterialBufferSizeXY;
	UpdateMaterialBufferToTiledResolution(FIntPoint(1, 1), MaterialBufferSizeXY);
	if (IsStrataEnabled())
	{
		FIntPoint SceneTextureExtent = GetSceneTextureExtent();
		
		// We need to allocate enough for the tiled memory addressing of material data to always work
		UpdateMaterialBufferToTiledResolution(SceneTextureExtent, MaterialBufferSizeXY);

		const uint32 MaterialConservativeByteCountPerPixel = CVarStrataBytePerPixel.GetValueOnAnyThread();
		const uint32 RoundToValue = 4u;
		StrataSceneData.MaxBytesPerPixel = FMath::DivideAndRoundUp(MaterialConservativeByteCountPerPixel, RoundToValue) * RoundToValue;

		const FIntPoint TileResolution(FMath::DivideAndRoundUp(SceneTextureExtent.X, STRATA_TILE_SIZE), FMath::DivideAndRoundUp(SceneTextureExtent.Y, STRATA_TILE_SIZE));

		const TCHAR* StrataTileListBufferNames[EStrataTileType::ECount] =
		{
			TEXT("Strata.StrataTileListBuffer(Simple)"),
			TEXT("Strata.StrataTileListBuffer(Single)"),
			TEXT("Strata.StrataTileListBuffer(Complex)"),
			TEXT("Strata.StrataTileListBuffer(OpaqueRoughRefraction)"),
			TEXT("Strata.StrataTileListBuffer(SSSWithoutOpaqueRoughRefraction)")
		};

		// Tile classification buffers
		{
			// Indirect draw
			StrataSceneData.ClassificationTileDrawIndirectBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDrawIndirectParameters>(EStrataTileType::ECount), TEXT("Strata.StrataTileDrawIndirectBuffer"));
			StrataSceneData.ClassificationTileDrawIndirectBufferUAV = GraphBuilder.CreateUAV(StrataSceneData.ClassificationTileDrawIndirectBuffer, PF_R32_UINT);
			AddClearUAVPass(GraphBuilder, StrataSceneData.ClassificationTileDrawIndirectBufferUAV, 0);

			// Indirect dispatch
			StrataSceneData.ClassificationTileDispatchIndirectBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(EStrataTileType::ECount), TEXT("Strata.StrataTileDispatchIndirectBuffer"));
			StrataSceneData.ClassificationTileDispatchIndirectBufferUAV = GraphBuilder.CreateUAV(StrataSceneData.ClassificationTileDispatchIndirectBuffer, PF_R32_UINT);
			AddClearUAVPass(GraphBuilder, StrataSceneData.ClassificationTileDispatchIndirectBufferUAV, 0);

			for (uint32 i = 0; i <= EStrataTileType::EComplex; ++i)
			{
				StrataSceneData.ClassificationTileListBuffer[i] = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), TileResolution.X * TileResolution.Y), StrataTileListBufferNames[i]);
				StrataSceneData.ClassificationTileListBufferSRV[i] = GraphBuilder.CreateSRV(StrataSceneData.ClassificationTileListBuffer[i], PF_R32_UINT);
				StrataSceneData.ClassificationTileListBufferUAV[i] = GraphBuilder.CreateUAV(StrataSceneData.ClassificationTileListBuffer[i], PF_R32_UINT);
			}
		}

		// Top layer texture
		{
			StrataSceneData.TopLayerTexture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(SceneTextureExtent, PF_R32_UINT, FClearValueBinding::Black, TexCreate_RenderTargetable | TexCreate_ShaderResource), TEXT("Strata.TopLayerTexture"));
		}

		// SSS texture
		{
			StrataSceneData.SSSTexture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(SceneTextureExtent, PF_R32G32_UINT, FClearValueBinding::Black, TexCreate_DisableDCC | TexCreate_NoFastClear | TexCreate_ShaderResource | TexCreate_UAV), TEXT("Strata.SSSTexture"));
			StrataSceneData.SSSTextureUAV = GraphBuilder.CreateUAV(StrataSceneData.SSSTexture);
		}

		// Separated subsurface and rough refraction textures
		{
			const bool bIsStrataOpaqueMaterialRoughRefractionEnabled= IsStrataOpaqueMaterialRoughRefractionEnabled();
			const int32 TileListBufferElementCount					= bIsStrataOpaqueMaterialRoughRefractionEnabled ? TileResolution.X * TileResolution.Y : 4;
			const FIntPoint OpaqueRoughRefractionSceneExtent		= bIsStrataOpaqueMaterialRoughRefractionEnabled ? SceneTextureExtent : FIntPoint(4, 4);
			
			StrataSceneData.ClassificationTileListBuffer[EStrataTileType::EOpaqueRoughRefraction] = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), TileListBufferElementCount), StrataTileListBufferNames[EStrataTileType::EOpaqueRoughRefraction]);
			StrataSceneData.ClassificationTileListBufferSRV[EStrataTileType::EOpaqueRoughRefraction] = GraphBuilder.CreateSRV(StrataSceneData.ClassificationTileListBuffer[EStrataTileType::EOpaqueRoughRefraction], PF_R32_UINT);
			StrataSceneData.ClassificationTileListBufferUAV[EStrataTileType::EOpaqueRoughRefraction] = GraphBuilder.CreateUAV(StrataSceneData.ClassificationTileListBuffer[EStrataTileType::EOpaqueRoughRefraction], PF_R32_UINT);

			StrataSceneData.ClassificationTileListBuffer[EStrataTileType::ESSSWithoutOpaqueRoughRefraction] = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), TileListBufferElementCount), StrataTileListBufferNames[EStrataTileType::ESSSWithoutOpaqueRoughRefraction]);
			StrataSceneData.ClassificationTileListBufferSRV[EStrataTileType::ESSSWithoutOpaqueRoughRefraction] = GraphBuilder.CreateSRV(StrataSceneData.ClassificationTileListBuffer[EStrataTileType::ESSSWithoutOpaqueRoughRefraction], PF_R32_UINT);
			StrataSceneData.ClassificationTileListBufferUAV[EStrataTileType::ESSSWithoutOpaqueRoughRefraction] = GraphBuilder.CreateUAV(StrataSceneData.ClassificationTileListBuffer[EStrataTileType::ESSSWithoutOpaqueRoughRefraction], PF_R32_UINT);

			StrataSceneData.OpaqueRoughRefractionTexture = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(OpaqueRoughRefractionSceneExtent, PF_FloatR11G11B10, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable), TEXT("Strata.OpaqueRoughRefractionTexture"));
			StrataSceneData.OpaqueRoughRefractionTextureUAV = GraphBuilder.CreateUAV(StrataSceneData.OpaqueRoughRefractionTexture);
			
			StrataSceneData.SeparatedSubSurfaceSceneColor			= GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(OpaqueRoughRefractionSceneExtent, PF_FloatR11G11B10, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable), TEXT("Strata.SeparatedSubSurfaceSceneColor"));
			StrataSceneData.SeparatedOpaqueRoughRefractionSceneColor= GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(OpaqueRoughRefractionSceneExtent, PF_FloatR11G11B10, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable), TEXT("Strata.SeparatedOpaqueRoughRefractionSceneColor"));

			if (bIsStrataOpaqueMaterialRoughRefractionEnabled)
			{
				// Fast clears
				AddClearRenderTargetPass(GraphBuilder, StrataSceneData.OpaqueRoughRefractionTexture, StrataSceneData.OpaqueRoughRefractionTexture->Desc.ClearValue.GetClearColor());
				AddClearRenderTargetPass(GraphBuilder, StrataSceneData.SeparatedSubSurfaceSceneColor, StrataSceneData.SeparatedSubSurfaceSceneColor->Desc.ClearValue.GetClearColor());
				AddClearRenderTargetPass(GraphBuilder, StrataSceneData.SeparatedOpaqueRoughRefractionSceneColor, StrataSceneData.SeparatedOpaqueRoughRefractionSceneColor->Desc.ClearValue.GetClearColor());
			}
		}

		// BSDF tiles/offsets
		{
			StrataSceneData.TileCount_Total		= GetStrataTextureTileResolution(SceneTextureExtent);
			StrataSceneData.TileCount_Primary	= GetStrataTextureTileResolution(SceneTextureExtent, 0.f);
			StrataSceneData.TileCount_Overflow	= StrataSceneData.TileCount_Total - StrataSceneData.TileCount_Primary;

			StrataSceneData.BSDFTileTexture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(StrataSceneData.TileCount_Total, PF_R32_UINT, FClearValueBinding::None, TexCreate_UAV | TexCreate_ShaderResource), TEXT("Strata.BSDFTiles"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(StrataSceneData.BSDFTileTexture), 0u);

			StrataSceneData.BSDFOffsetTexture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(StrataSceneData.TileCount_Primary * STRATA_TILE_SIZE, PF_R32_UINT, FClearValueBinding::None, TexCreate_UAV | TexCreate_ShaderResource), TEXT("Strata.BSDFOffsets"));
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(StrataSceneData.BSDFOffsetTexture), 0u);

			StrataSceneData.BSDFTileDispatchIndirectBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateIndirectDesc<FRHIDispatchIndirectParameters>(1), TEXT("Strata.StrataBSDFTileDispatchIndirectBuffer"));
			StrataSceneData.BSDFTileCountBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(4, 1), TEXT("Strata.BSDFTileCount"));
		}
	}
	else
	{
		StrataSceneData.MaxBytesPerPixel = 4u * STRATA_BASE_PASS_MRT_OUTPUT_COUNT;
	}

	// Create the material data container
	FIntPoint SceneTextureExtent = IsStrataEnabled() ? GetSceneTextureExtent() : FIntPoint(2, 2);

	const uint32 SliceCount = FMath::DivideAndRoundUp(StrataSceneData.MaxBytesPerPixel, 4u);
	const FRDGTextureDesc MaterialTextureDesc = FRDGTextureDesc::Create2DArray(SceneTextureExtent, PF_R32_UINT, FClearValueBinding::Transparent,
		TexCreate_TargetArraySlicesIndependently | TexCreate_DisableDCC | TexCreate_NoFastClear | TexCreate_RenderTargetable | TexCreate_ShaderResource | TexCreate_UAV, SliceCount, 1, 1);
	StrataSceneData.MaterialTextureArray = GraphBuilder.CreateTexture(MaterialTextureDesc, TEXT("Strata.Material"));
	StrataSceneData.MaterialTextureArraySRV = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(StrataSceneData.MaterialTextureArray));
	StrataSceneData.MaterialTextureArrayUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(StrataSceneData.MaterialTextureArray, 0));

	// See AppendStrataMRTs
	check(STRATA_BASE_PASS_MRT_OUTPUT_COUNT <= SliceCount);
	StrataSceneData.MaterialTextureArrayUAVWithoutRTs = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(StrataSceneData.MaterialTextureArray, 0, PF_Unknown, STRATA_BASE_PASS_MRT_OUTPUT_COUNT, SliceCount - STRATA_BASE_PASS_MRT_OUTPUT_COUNT));

	// Rough diffuse model
	StrataSceneData.bRoughDiffuse = CVarStrataRoughDiffuse.GetValueOnRenderThread() > 0 ? 1u : 0u;

	// Set reference to the Strata data from each view
	for (int32 ViewIndex = 0; ViewIndex < SceneRenderer.Views.Num(); ViewIndex++)
	{
		FViewInfo& View = SceneRenderer.Views[ViewIndex];
		View.StrataSceneData = &SceneRenderer.Scene->StrataSceneData;
	}

	if (IsStrataEnabled())
	{
		AddStrataClearMaterialBufferPass(
			GraphBuilder, 
			GraphBuilder.CreateUAV(FRDGTextureUAVDesc(StrataSceneData.MaterialTextureArray, 0)),
			StrataSceneData.SSSTextureUAV,
			StrataSceneData.MaxBytesPerPixel, 
			MaterialBufferSizeXY);
	}

	// Create the readable uniform buffers for each views once for all (it is view independent and all the views should be tiled into the render target textures & material buffer)
	if (IsStrataEnabled())
	{
		FStrataGlobalUniformParameters* StrataUniformParameters = GraphBuilder.AllocParameters<FStrataGlobalUniformParameters>();
		BindStrataGlobalUniformParameters(GraphBuilder, &StrataSceneData, *StrataUniformParameters);
		StrataSceneData.StrataGlobalUniformParameters = GraphBuilder.CreateUniformBuffer(StrataUniformParameters);
	}
}

void BindStrataBasePassUniformParameters(FRDGBuilder& GraphBuilder, FStrataSceneData* StrataSceneData, FStrataBasePassUniformParameters& OutStrataUniformParameters)
{
	if (IsStrataEnabled() && StrataSceneData)
	{
		OutStrataUniformParameters.bRoughDiffuse = StrataSceneData->bRoughDiffuse ? 1u : 0u;
		OutStrataUniformParameters.MaxBytesPerPixel = StrataSceneData->MaxBytesPerPixel;
		OutStrataUniformParameters.MaterialTextureArrayUAVWithoutRTs = StrataSceneData->MaterialTextureArrayUAVWithoutRTs;
		OutStrataUniformParameters.SSSTextureUAV = StrataSceneData->SSSTextureUAV;
		OutStrataUniformParameters.OpaqueRoughRefractionTextureUAV = StrataSceneData->OpaqueRoughRefractionTextureUAV;
	}
	else
	{
		FRDGTextureRef DummyWritableSSSTexture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(FIntPoint(1, 1), PF_R32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), TEXT("Strata.DummyWritableTexture"));
		FRDGTextureUAVRef DummyWritableSSSTextureUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(DummyWritableSSSTexture));

		FRDGTextureRef DummyWritableRefracTexture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(FIntPoint(1, 1), PF_R8, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), TEXT("Strata.DummyWritableTexture"));
		FRDGTextureUAVRef DummyWritableRefracTextureUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(DummyWritableRefracTexture));

		FRDGTextureRef DummyWritableTextureArray = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2DArray(FIntPoint(1, 1), PF_R32_UINT, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV, 1), TEXT("Strata.DummyWritableTexture"));
		FRDGTextureUAVRef DummyWritableTextureArrayUAV = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(DummyWritableTextureArray));

		const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);
		OutStrataUniformParameters.bRoughDiffuse = 0u;
		OutStrataUniformParameters.MaxBytesPerPixel = 0;
		OutStrataUniformParameters.MaterialTextureArrayUAVWithoutRTs = DummyWritableTextureArrayUAV;
		OutStrataUniformParameters.SSSTextureUAV = DummyWritableSSSTextureUAV;
		OutStrataUniformParameters.OpaqueRoughRefractionTextureUAV = DummyWritableRefracTextureUAV;
	}
}

void BindStrataGlobalUniformParameters(FRDGBuilder& GraphBuilder, FStrataSceneData* StrataSceneData, FStrataGlobalUniformParameters& OutStrataUniformParameters)
{
	if (IsStrataEnabled() && StrataSceneData)
	{
		OutStrataUniformParameters.bRoughDiffuse = StrataSceneData->bRoughDiffuse ? 1u : 0u;
		OutStrataUniformParameters.MaxBytesPerPixel = StrataSceneData->MaxBytesPerPixel;
		OutStrataUniformParameters.TileSize = STRATA_TILE_SIZE;
		OutStrataUniformParameters.TileSizeLog2 = STRATA_TILE_SIZE_DIV_AS_SHIFT;
		OutStrataUniformParameters.TileCount = StrataSceneData->TileCount_Primary;
		OutStrataUniformParameters.MaterialTextureArray = StrataSceneData->MaterialTextureArray;
		OutStrataUniformParameters.TopLayerTexture = StrataSceneData->TopLayerTexture;
		OutStrataUniformParameters.SSSTexture = StrataSceneData->SSSTexture;
		OutStrataUniformParameters.OpaqueRoughRefractionTexture = StrataSceneData->OpaqueRoughRefractionTexture;
		OutStrataUniformParameters.BSDFTileTexture = StrataSceneData->BSDFTileTexture;
		OutStrataUniformParameters.BSDFOffsetTexture = StrataSceneData->BSDFOffsetTexture;
		OutStrataUniformParameters.BSDFTileCountBuffer = GraphBuilder.CreateSRV(StrataSceneData->BSDFTileCountBuffer, PF_R32_UINT);
	}
	else
	{
		const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::Get(GraphBuilder);
		FRDGTextureRef DefaultTextureArray = GSystemTextures.GetDefaultTexture(GraphBuilder, ETextureDimension::Texture2DArray, EPixelFormat::PF_R32_UINT, FClearValueBinding::Transparent);
		FRDGBufferSRVRef DefaultBuffer = GraphBuilder.CreateSRV(GSystemTextures.GetDefaultBuffer(GraphBuilder, 4, 0u), PF_R32_UINT);
		OutStrataUniformParameters.bRoughDiffuse = 0;
		OutStrataUniformParameters.MaxBytesPerPixel = 0;
		OutStrataUniformParameters.TileSize = 0;
		OutStrataUniformParameters.TileSizeLog2 = 0;
		OutStrataUniformParameters.TileCount = 0;
		OutStrataUniformParameters.MaterialTextureArray = DefaultTextureArray;
		OutStrataUniformParameters.TopLayerTexture = SystemTextures.DefaultNormal8Bit;
		OutStrataUniformParameters.SSSTexture = SystemTextures.Black;
		OutStrataUniformParameters.OpaqueRoughRefractionTexture = SystemTextures.Black;
		OutStrataUniformParameters.BSDFTileTexture = SystemTextures.Black;
		OutStrataUniformParameters.BSDFOffsetTexture = SystemTextures.Black;
		OutStrataUniformParameters.BSDFTileCountBuffer = DefaultBuffer;
	}
}

void BindStrataForwardPasslUniformParameters(FRDGBuilder& GraphBuilder, FStrataSceneData* StrataSceneData, FStrataForwardPassUniformParameters& OutStrataUniformParameters)
{
	if (IsStrataEnabled() && StrataSceneData)
	{
		OutStrataUniformParameters.bRoughDiffuse = StrataSceneData->bRoughDiffuse ? 1u : 0u;
	}
	else
	{
		OutStrataUniformParameters.bRoughDiffuse = 0;
	}
}

TRDGUniformBufferRef<FStrataGlobalUniformParameters> BindStrataGlobalUniformParameters(FStrataSceneData* StrataSceneData)
{
	check(StrataSceneData);
	check(StrataSceneData->StrataGlobalUniformParameters != nullptr || !IsStrataEnabled());
	return StrataSceneData->StrataGlobalUniformParameters;
}

////////////////////////////////////////////////////////////////////////// 
// Debug

#define VISUALIZE_MATERIAL_PASS_COUNT 3

class FVisualizeMaterialPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FVisualizeMaterialPS);
	SHADER_USE_PARAMETER_STRUCT(FVisualizeMaterialPS, FGlobalShader);

	class FBSDFPass : SHADER_PERMUTATION_INT("PERMUTATION_BSDF_PASS", VISUALIZE_MATERIAL_PASS_COUNT);
	using FPermutationDomain = TShaderPermutationDomain<FBSDFPass>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FStrataGlobalUniformParameters, Strata)
		SHADER_PARAMETER_TEXTURE(Texture2D, MiniFontTexture)
		SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureParameters, SceneTextures)
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderPrint::FShaderParameters, ShaderPrintParameters)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool CanRunStrataVizualizeMaterial(EShaderPlatform Platform)
	{
		// On some consoles, this ALU heavy shader (and with optimisation disables for the sake of low compilation time) would spill registers. So only keep it for the editor.
		return IsPCPlatform(Platform);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5 && Strata::IsStrataEnabled()
			&& CanRunStrataVizualizeMaterial(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);

		// Stay debug and skip optimizations to reduce compilation time on this long shader.
		OutEnvironment.CompilerFlags.Add(CFLAG_Debug);
	}
};
IMPLEMENT_GLOBAL_SHADER(FVisualizeMaterialPS, "/Engine/Private/Strata/StrataVisualize.usf", "VisualizeMaterialPS", SF_Pixel);

static void AddVisualizeMaterialPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, FRDGTextureRef SceneColorTexture, EShaderPlatform Platform)
{
	FRHIBlendState* PreMultipliedColorTransmittanceBlend = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();
	if (View.Family->EngineShowFlags.VisualizeStrataMaterial)
	{
		if (!ShaderPrint::IsEnabled(View)) { ShaderPrint::SetEnabled(true); }
		ShaderPrint::RequestSpaceForLines(64);

		FSceneTextureParameters SceneTextureParameters = GetSceneTextureParameters(GraphBuilder);
		FVisualizeMaterialPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FVisualizeMaterialPS::FParameters>();
		PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
		PassParameters->Strata = Strata::BindStrataGlobalUniformParameters(View.StrataSceneData);
		PassParameters->MiniFontTexture = GetMiniFontTexture();
		PassParameters->SceneTextures = GetSceneTextureParameters(GraphBuilder);
		PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorTexture, ERenderTargetLoadAction::ELoad);
		ShaderPrint::SetParameters(GraphBuilder, View.ShaderPrintData, PassParameters->ShaderPrintParameters);

		for (uint32 j = 0; j < VISUALIZE_MATERIAL_PASS_COUNT; ++j)
		{
			FVisualizeMaterialPS::FPermutationDomain PermutationVector;
			PermutationVector.Set<typename FVisualizeMaterialPS::FBSDFPass>(j);
			TShaderMapRef<FVisualizeMaterialPS> PixelShader(View.ShaderMap, PermutationVector);

			FPixelShaderUtils::AddFullscreenPass<FVisualizeMaterialPS>(GraphBuilder, View.ShaderMap, RDG_EVENT_NAME("Strata::VisualizeMaterial"),
				PixelShader, PassParameters, View.ViewRect, PreMultipliedColorTransmittanceBlend);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FStrataClearMaterialBufferCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FStrataClearMaterialBufferCS);
	SHADER_USE_PARAMETER_STRUCT(FStrataClearMaterialBufferCS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<uint>, MaterialTextureArrayUAV)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint2>, SSSTextureUAV)
		SHADER_PARAMETER(uint32, MaxBytesPerPixel)
		SHADER_PARAMETER(FIntPoint, TiledViewBufferResolution)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5 && Strata::IsStrataEnabled();
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_CLEAR_MATERIAL_BUFFER"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FStrataClearMaterialBufferCS, "/Engine/Private/Strata/StrataMaterialClassification.usf", "ClearMaterialBufferMainCS", SF_Compute);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FStrataBSDFTilePassCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FStrataBSDFTilePassCS);
	SHADER_USE_PARAMETER_STRUCT(FStrataBSDFTilePassCS, FGlobalShader);

	class FWaveOps : SHADER_PERMUTATION_BOOL("PERMUTATION_WAVE_OPS");
	using FPermutationDomain = TShaderPermutationDomain<FWaveOps>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(int32, bRectPrimitive)
		SHADER_PARAMETER(int32, TileSizeLog2)
		SHADER_PARAMETER(FIntPoint, TileCount_Primary)
		SHADER_PARAMETER(FIntPoint, ViewResolution)
		SHADER_PARAMETER(uint32, MaxBytesPerPixel)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TopLayerTexture)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<uint>, MaterialTextureArray)

		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RWBSDFTileTexture)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint>, RWBSDFOffsetTexture)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWBSDFTileCountBuffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, TileListBuffer)
		RDG_BUFFER_ACCESS(TileIndirectBuffer, ERHIAccess::IndirectArgs)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		const bool bUseWaveIntrinsics = FDataDrivenShaderPlatformInfo::GetSupportsWaveOperations(Parameters.Platform);
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FWaveOps>() && !bUseWaveIntrinsics)
		{
			return false;
		}
		return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5 && Strata::IsStrataEnabled();
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_BSDF_TILE"), 1);

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FWaveOps>())
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
		}
	}
};
IMPLEMENT_GLOBAL_SHADER(FStrataBSDFTilePassCS, "/Engine/Private/Strata/StrataMaterialClassification.usf", "BSDFTileMainCS", SF_Compute);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FStrataMaterialTileClassificationPassCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FStrataMaterialTileClassificationPassCS);
	SHADER_USE_PARAMETER_STRUCT(FStrataMaterialTileClassificationPassCS, FGlobalShader);

	class FStrataClearDuringCategorization : SHADER_PERMUTATION_BOOL("PERMUTATION_STRATA_CLEAR_DURING_CATEGORIZATION");
	class FWaveOps : SHADER_PERMUTATION_BOOL("PERMUTATION_WAVE_OPS");
	using FPermutationDomain = TShaderPermutationDomain<FWaveOps, FStrataClearDuringCategorization>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, ViewUniformBuffer)
		SHADER_PARAMETER(int32, bRectPrimitive)
		SHADER_PARAMETER(FIntPoint, ViewResolution)
		SHADER_PARAMETER(uint32, MaxBytesPerPixel)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, TopLayerTexture)
		SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<uint>, MaterialTextureArray)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, TileDrawIndirectDataBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, SimpleTileListDataBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, SingleTileListDataBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, ComplexTileListDataBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OpaqueRoughRefractionTileListDataBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, SSSWithoutOpaqueRoughRefractionTileListDataBuffer)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D<uint2>, SSSTextureUAV)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, OpaqueRoughRefractionTexture)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		const bool bUseWaveIntrinsics = FDataDrivenShaderPlatformInfo::GetSupportsWaveOperations(Parameters.Platform);
		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FWaveOps>() && !bUseWaveIntrinsics)
		{
			return false;
		}
		return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5 && Strata::IsStrataEnabled();
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_TILE_CATEGORIZATION"), 1);

		FPermutationDomain PermutationVector(Parameters.PermutationId);
		if (PermutationVector.Get<FWaveOps>())
		{
			OutEnvironment.CompilerFlags.Add(CFLAG_WaveOperations);
		}
	}
};
IMPLEMENT_GLOBAL_SHADER(FStrataMaterialTileClassificationPassCS, "/Engine/Private/Strata/StrataMaterialClassification.usf", "TileMainCS", SF_Compute);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FStrataMaterialTilePrepareArgsPassCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FStrataMaterialTilePrepareArgsPassCS);
	SHADER_USE_PARAMETER_STRUCT(FStrataMaterialTilePrepareArgsPassCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer,   TileDrawIndirectDataBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, TileDispatchIndirectDataBuffer)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5 && Strata::IsStrataEnabled();
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_MATERIAL_TILE_PREPARE_ARGS"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FStrataMaterialTilePrepareArgsPassCS, "/Engine/Private/Strata/StrataMaterialClassification.usf", "ArgsMainCS", SF_Compute);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FStrataBSDFTilePrepareArgsPassCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FStrataBSDFTilePrepareArgsPassCS);
	SHADER_USE_PARAMETER_STRUCT(FStrataBSDFTilePrepareArgsPassCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FIntPoint, TileCount_Primary)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, TileDrawIndirectDataBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, TileDispatchIndirectDataBuffer)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5 && Strata::IsStrataEnabled();
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_BSDF_TILE_PREPARE_ARGS"), 1);
	}
};
IMPLEMENT_GLOBAL_SHADER(FStrataBSDFTilePrepareArgsPassCS, "/Engine/Private/Strata/StrataMaterialClassification.usf", "ArgsMainCS", SF_Compute);

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FStrataMaterialStencilTaggingPassPS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FStrataMaterialStencilTaggingPassPS);
	SHADER_USE_PARAMETER_STRUCT(FStrataMaterialStencilTaggingPassPS, FGlobalShader);

	using FPermutationDomain = TShaderPermutationDomain<>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(Strata::FStrataTilePassVS::FParameters, VS)
		SHADER_PARAMETER(FVector4f, DebugTileColor)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static FPermutationDomain RemapPermutation(FPermutationDomain PermutationVector)
	{
		return PermutationVector;
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return GetMaxSupportedFeatureLevel(Parameters.Platform) >= ERHIFeatureLevel::SM5 && Strata::IsStrataEnabled();
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_STENCIL_TAGGING_PS"), 1); 
	}
};

IMPLEMENT_GLOBAL_SHADER(FStrataTilePassVS, "/Engine/Private/Strata/StrataTile.usf", "StrataTilePassVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FStrataMaterialStencilTaggingPassPS, "/Engine/Private/Strata/StrataTile.usf", "StencilTaggingMainPS", SF_Pixel);

static FStrataTileParameter InternalSetTileParameters(FRDGBuilder* GraphBuilder, const FViewInfo& View, const EStrataTileType TileType)
{
	FStrataTileParameter Out;
	if (View.StrataSceneData && TileType != EStrataTileType::ECount)
	{
		Out.TileListBuffer = View.StrataSceneData->ClassificationTileListBufferSRV[TileType];
		Out.TileIndirectBuffer = View.StrataSceneData->ClassificationTileDrawIndirectBuffer;
	}
	else if (GraphBuilder)
	{
		FRDGBufferRef BufferDummy = GSystemTextures.GetDefaultBuffer(*GraphBuilder, 4, 0u);
		FRDGBufferSRVRef BufferDummySRV = GraphBuilder->CreateSRV(BufferDummy, PF_R32_UINT);
		Out.TileListBuffer = BufferDummySRV;
		Out.TileIndirectBuffer = BufferDummy;
	}
	return Out;
}

FStrataTilePassVS::FParameters SetTileParameters(
	const FViewInfo& View,
	const EStrataTileType TileType,
	EPrimitiveType& PrimitiveType)
{
	FStrataTileParameter Temp = InternalSetTileParameters(nullptr, View, TileType);
	PrimitiveType = GRHISupportsRectTopology ? PT_RectList : PT_TriangleList;

	FStrataTilePassVS::FParameters Out;
	Out.OutputViewSizeAndInvSize = View.CachedViewUniformShaderParameters->ViewSizeAndInvSize;
	Out.OutputBufferSizeAndInvSize = View.CachedViewUniformShaderParameters->BufferSizeAndInvSize;
	Out.ViewScreenToTranslatedWorld = View.CachedViewUniformShaderParameters->ScreenToTranslatedWorld;
	Out.TileListBuffer = Temp.TileListBuffer;
	Out.TileIndirectBuffer = Temp.TileIndirectBuffer;
	return Out;
}

FStrataTilePassVS::FParameters SetTileParameters(
	FRDGBuilder& GraphBuilder, 
	const FViewInfo& View, 
	const EStrataTileType TileType,
	EPrimitiveType& PrimitiveType)
{
	FStrataTileParameter Temp = InternalSetTileParameters(&GraphBuilder, View, TileType);
	PrimitiveType = GRHISupportsRectTopology ? PT_RectList : PT_TriangleList;

	FStrataTilePassVS::FParameters Out;
	Out.OutputViewSizeAndInvSize = View.CachedViewUniformShaderParameters->ViewSizeAndInvSize;
	Out.OutputBufferSizeAndInvSize = View.CachedViewUniformShaderParameters->BufferSizeAndInvSize;
	Out.ViewScreenToTranslatedWorld = View.CachedViewUniformShaderParameters->ScreenToTranslatedWorld;
	Out.TileListBuffer = Temp.TileListBuffer;
	Out.TileIndirectBuffer = Temp.TileIndirectBuffer;
	return Out;
}

FStrataTileParameter SetTileParameters(FRDGBuilder& GraphBuilder, const FViewInfo& View, const EStrataTileType TileType)
{
	return InternalSetTileParameters(&GraphBuilder, View, TileType);
}

uint32 TileTypeDrawIndirectArgOffset(const EStrataTileType Type)
{
	check(Type >= 0 && Type < EStrataTileType::ECount);
	return GetStrataTileTypeDrawIndirectArgOffset_Byte(Type);
}

uint32 TileTypeDispatchIndirectArgOffset(const EStrataTileType Type)
{
	check(Type >= 0 && Type < EStrataTileType::ECount);
	return GetStrataTileTypeDispatchIndirectArgOffset_Byte(Type);
}

// Add additionnaly bits for filling/clearing stencil to ensure that the 'Strata' bits are not corrupted by the stencil shadows 
// when generating shadow mask. Withouth these 'trailing' bits, the incr./decr. operation would change/corrupt the 'Strata' bits
constexpr uint32 StencilBit_Fast_1	  = 0x07u | StencilBit_Fast;
constexpr uint32 StencilBit_Single_1  = 0x07u | StencilBit_Single;
constexpr uint32 StencilBit_Complex_1 = 0x07u | StencilBit_Complex; 

static void AddStrataInternalClassificationTilePass(
	FRDGBuilder& GraphBuilder,
	const FViewInfo& View,
	const FRDGTextureRef* DepthTexture,
	const FRDGTextureRef* ColorTexture,
	EStrataTileType TileMaterialType,
	const bool bDebug = false)
{
	EPrimitiveType StrataTilePrimitiveType = PT_TriangleList;
	const FIntPoint OutputResolution = View.ViewRect.Size();
	FVector4f OutputResolutionAndInv = FVector4f(OutputResolution.X, OutputResolution.Y, 1.0f / float(OutputResolution.X), 1.0f / float(OutputResolution.Y));

	FStrataMaterialStencilTaggingPassPS::FParameters* ParametersPS = GraphBuilder.AllocParameters<FStrataMaterialStencilTaggingPassPS::FParameters>();
	ParametersPS->VS = Strata::SetTileParameters(GraphBuilder, View, TileMaterialType, StrataTilePrimitiveType);

	FStrataTilePassVS::FPermutationDomain VSPermutationVector;
	VSPermutationVector.Set< FStrataTilePassVS::FEnableDebug >(bDebug);
	VSPermutationVector.Set< FStrataTilePassVS::FEnableTexCoordScreenVector >(false);
	TShaderMapRef<FStrataTilePassVS> VertexShader(View.ShaderMap, VSPermutationVector);
	TShaderMapRef<FStrataMaterialStencilTaggingPassPS> PixelShader(View.ShaderMap);

	// For debug purpose
	if (bDebug)
	{
		check(ColorTexture);
		ParametersPS->RenderTargets[0] = FRenderTargetBinding(*ColorTexture, ERenderTargetLoadAction::ELoad);
		switch (TileMaterialType)
		{
		case EStrataTileType::ESimple:							ParametersPS->DebugTileColor = FVector4f(0.0f, 1.0f, 0.0f, 1.0); break;
		case EStrataTileType::ESingle:							ParametersPS->DebugTileColor = FVector4f(1.0f, 1.0f, 0.0f, 1.0); break;
		case EStrataTileType::EComplex:							ParametersPS->DebugTileColor = FVector4f(1.0f, 0.0f, 0.0f, 1.0); break;
		case EStrataTileType::EOpaqueRoughRefraction:			ParametersPS->DebugTileColor = FVector4f(0.0f, 1.0f, 1.0f, 1.0); break;
		case EStrataTileType::ESSSWithoutOpaqueRoughRefraction:	ParametersPS->DebugTileColor = FVector4f(0.0f, 0.0f, 1.0f, 1.0); break;
		default: check(false);
		}
	}
	else
	{
		check(DepthTexture);
		ParametersPS->RenderTargets.DepthStencil = FDepthStencilBinding(
			*DepthTexture,
			ERenderTargetLoadAction::ELoad,
			ERenderTargetLoadAction::ELoad,
			FExclusiveDepthStencil::DepthNop_StencilWrite);
		ParametersPS->DebugTileColor = FVector4f(ForceInitToZero);
	}
	
	GraphBuilder.AddPass(
		RDG_EVENT_NAME("Strata::%sClassificationPass(%s)", bDebug ? TEXT("Debug") : TEXT("Stencil"), ToString(TileMaterialType)),
		ParametersPS,
		ERDGPassFlags::Raster,
		[ParametersPS, VertexShader, PixelShader, OutputResolution, StrataTilePrimitiveType, TileMaterialType, bDebug](FRHICommandList& RHICmdList)
		{
			FGraphicsPipelineStateInitializer GraphicsPSOInit;
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			uint32 StencilRef = 0xFF;
			if (bDebug)
			{
				// Use premultiplied alpha blending, pixel shader and depth/stencil is off
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
				GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_InverseSourceAlpha, BO_Add, BF_Zero, BF_One>::GetRHI();
				GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			}
			else
			{
				check(TileMaterialType != EStrataTileType::ECount && TileMaterialType != EStrataTileType::EOpaqueRoughRefraction && TileMaterialType != EStrataTileType::ESSSWithoutOpaqueRoughRefraction);

				// No blending and no pixel shader required. Stencil will be written to.
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = nullptr;
				GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
				switch (TileMaterialType)
				{
				case EStrataTileType::ESimple:
				{
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
						false, CF_Always,
						true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
						false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
						0xFF, StencilBit_Fast_1>::GetRHI();
					StencilRef = StencilBit_Fast_1;
				}
				break;
				case EStrataTileType::ESingle:
				{
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
						false, CF_Always,
						true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
						false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
						0xFF, StencilBit_Single_1>::GetRHI();
					StencilRef = StencilBit_Single_1;
				}
				break;
				case EStrataTileType::EComplex:
				{
					GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<
						false, CF_Always,
						true, CF_Always, SO_Keep, SO_Keep, SO_Replace,
						false, CF_Always, SO_Keep, SO_Keep, SO_Keep,
						0xFF, StencilBit_Complex_1>::GetRHI();
					StencilRef = StencilBit_Complex_1;
				}
				break;
				}
			}
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.PrimitiveType = StrataTilePrimitiveType;
			SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, StencilRef);
			SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), ParametersPS->VS);
			if (bDebug)
			{
				SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *ParametersPS);
			}

			RHICmdList.SetViewport(0, 0, 0.0f, OutputResolution.X, OutputResolution.Y, 1.0f);
			RHICmdList.SetStreamSource(0, nullptr, 0);
			RHICmdList.DrawPrimitiveIndirect(ParametersPS->VS.TileIndirectBuffer->GetIndirectRHICallBuffer(), TileTypeDrawIndirectArgOffset(TileMaterialType));
		});
}

void AddStrataStencilPass(
	FRDGBuilder& GraphBuilder,
	const TArray<FViewInfo>& Views,
	const FMinimalSceneTextures& SceneTextures)
{
	for (int32 i = 0; i < Views.Num(); ++i)
	{
		const FViewInfo& View = Views[i];
		AddStrataInternalClassificationTilePass(GraphBuilder, View, &SceneTextures.Depth.Target, nullptr, EStrataTileType::ESimple);
		AddStrataInternalClassificationTilePass(GraphBuilder, View, &SceneTextures.Depth.Target, nullptr, EStrataTileType::ESingle);
		AddStrataInternalClassificationTilePass(GraphBuilder, View, &SceneTextures.Depth.Target, nullptr, EStrataTileType::EComplex);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void AppendStrataMRTs(FSceneRenderer& SceneRenderer, uint32& RenderTargetCount, TStaticArray<FTextureRenderTargetBinding, MaxSimultaneousRenderTargets>& RenderTargets)
{
	if (Strata::IsStrataEnabled() && SceneRenderer.Scene)
	{
		// If this function changes, update Strata::SetBasePassRenderTargetOutputFormat()
		 
		// Add 2 uint for Strata fast path. 
		// - We must clear the first uint to 0 to identify pixels that have not been written to.
		// - We must never clear the second uint, it will only be written/read if needed.
		auto AddStrataOutputTarget = [&](int16 StrataMaterialArraySlice, bool bNeverClear = false)
		{
			RenderTargets[RenderTargetCount] = FTextureRenderTargetBinding(SceneRenderer.Scene->StrataSceneData.MaterialTextureArray, StrataMaterialArraySlice, bNeverClear);
			RenderTargetCount++;
		};
		for (int i = 0; i < STRATA_BASE_PASS_MRT_OUTPUT_COUNT; ++i)
		{
			const bool bNeverClear = i != 0; // Only allow clearing the first slice containing the header
			AddStrataOutputTarget(i, bNeverClear);
		}

		// Add another MRT for Strata top layer information. We want to follow the usual clear process which can leverage fast clear.
		{
			RenderTargets[RenderTargetCount] = FTextureRenderTargetBinding(SceneRenderer.Scene->StrataSceneData.TopLayerTexture);
			RenderTargetCount++;
		};
	}
}

void SetBasePassRenderTargetOutputFormat(const EShaderPlatform Platform, FShaderCompilerEnvironment& OutEnvironment)
{
	if (Strata::IsStrataEnabled())
	{
		const FGBufferParams GBufferParams = FShaderCompileUtilities::FetchGBufferParamsRuntime(Platform);
		const FGBufferInfo BufferInfo = FetchFullGBufferInfo(GBufferParams);

		// Add 2 uint for Strata fast path
		OutEnvironment.SetRenderTargetOutputFormat(BufferInfo.NumTargets + 0, PF_R32_UINT);
		OutEnvironment.SetRenderTargetOutputFormat(BufferInfo.NumTargets + 1, PF_R32_UINT);

		// Add another MRT for Strata top layer information
		OutEnvironment.SetRenderTargetOutputFormat(BufferInfo.NumTargets + 2, PF_R32_UINT);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void AddStrataMaterialClassificationPass(FRDGBuilder& GraphBuilder, const FMinimalSceneTextures& SceneTextures, const TArray<FViewInfo>& Views)
{
	RDG_EVENT_SCOPE_CONDITIONAL(GraphBuilder, IsStrataEnabled() && Views.Num() > 0, "Strata::MaterialClassification");
	if (!IsStrataEnabled())
	{
		return;
	}

	for (int32 i = 0; i < Views.Num(); ++i)
	{
		const FViewInfo& View = Views[i];
		bool bWaveOps = GRHISupportsWaveOperations && FDataDrivenShaderPlatformInfo::GetSupportsWaveOperations(View.GetShaderPlatform());
	#if PLATFORM_WINDOWS
		// Tile reduction requires 64-wide wave
		bWaveOps = bWaveOps && !IsRHIDeviceNVIDIA();
	#endif
		
		// Tile reduction
		{
			const bool bClear = ClearDuringCategorization();
			FStrataMaterialTileClassificationPassCS::FPermutationDomain PermutationVector;
			PermutationVector.Set< FStrataMaterialTileClassificationPassCS::FStrataClearDuringCategorization >(bClear);
			PermutationVector.Set< FStrataMaterialTileClassificationPassCS::FWaveOps >(bWaveOps);
			TShaderMapRef<FStrataMaterialTileClassificationPassCS> ComputeShader(View.ShaderMap, PermutationVector);
			FStrataMaterialTileClassificationPassCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FStrataMaterialTileClassificationPassCS::FParameters>();
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
			PassParameters->bRectPrimitive = GRHISupportsRectTopology ? 1 : 0;
			PassParameters->ViewResolution = View.ViewRect.Size();
			PassParameters->MaxBytesPerPixel = View.StrataSceneData->MaxBytesPerPixel;
			PassParameters->TopLayerTexture = View.StrataSceneData->TopLayerTexture;
			PassParameters->MaterialTextureArray = View.StrataSceneData->MaterialTextureArraySRV;
			PassParameters->SSSTextureUAV = View.StrataSceneData->SSSTextureUAV;
			PassParameters->OpaqueRoughRefractionTexture = View.StrataSceneData->OpaqueRoughRefractionTexture;
			PassParameters->TileDrawIndirectDataBuffer = View.StrataSceneData->ClassificationTileDrawIndirectBufferUAV;
			PassParameters->SimpleTileListDataBuffer = View.StrataSceneData->ClassificationTileListBufferUAV[EStrataTileType::ESimple];
			PassParameters->SingleTileListDataBuffer = View.StrataSceneData->ClassificationTileListBufferUAV[EStrataTileType::ESingle];
			PassParameters->ComplexTileListDataBuffer = View.StrataSceneData->ClassificationTileListBufferUAV[EStrataTileType::EComplex];
			PassParameters->OpaqueRoughRefractionTileListDataBuffer = View.StrataSceneData->ClassificationTileListBufferUAV[EStrataTileType::EOpaqueRoughRefraction];
			PassParameters->SSSWithoutOpaqueRoughRefractionTileListDataBuffer = View.StrataSceneData->ClassificationTileListBufferUAV[EStrataTileType::ESSSWithoutOpaqueRoughRefraction];

			const uint32 GroupSize = 8;
			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Strata::MaterialTileClassification(%s%s)", bWaveOps ? TEXT("Wave") : TEXT("SharedMemory"), bClear ? TEXT(", Clear") : TEXT("")),
				ComputeShader,
				PassParameters,
				FComputeShaderUtils::GetGroupCount(PassParameters->ViewResolution, GroupSize));
		}

		// Tile indirect dispatch args conversion
		{
			TShaderMapRef<FStrataMaterialTilePrepareArgsPassCS> ComputeShader(View.ShaderMap);
			FStrataMaterialTilePrepareArgsPassCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FStrataMaterialTilePrepareArgsPassCS::FParameters>();
			PassParameters->TileDrawIndirectDataBuffer = GraphBuilder.CreateSRV(View.StrataSceneData->ClassificationTileDrawIndirectBuffer, PF_R32_UINT);
			PassParameters->TileDispatchIndirectDataBuffer = View.StrataSceneData->ClassificationTileDispatchIndirectBufferUAV;

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Strata::MaterialTilePrepareArgs"),
				ComputeShader,
				PassParameters,
				FIntVector(1,1,1));
		}

		// Compute BSDF tile index and material read offset
		{
			FRDGBufferUAVRef RWBSDFTileCountBuffer = GraphBuilder.CreateUAV(View.StrataSceneData->BSDFTileCountBuffer, PF_R32_UINT);
			AddClearUAVPass(GraphBuilder, RWBSDFTileCountBuffer, 0u);

			FStrataBSDFTilePassCS::FPermutationDomain PermutationVector;
			PermutationVector.Set< FStrataBSDFTilePassCS::FWaveOps >(bWaveOps);
			TShaderMapRef<FStrataBSDFTilePassCS> ComputeShader(View.ShaderMap, PermutationVector);
			FStrataBSDFTilePassCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FStrataBSDFTilePassCS::FParameters>();
			PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
			PassParameters->TileSizeLog2 = STRATA_TILE_SIZE_DIV_AS_SHIFT;
			PassParameters->TileCount_Primary = View.StrataSceneData->TileCount_Primary;
			PassParameters->ViewResolution = View.ViewRect.Size();
			PassParameters->MaxBytesPerPixel = View.StrataSceneData->MaxBytesPerPixel;
			PassParameters->TopLayerTexture = View.StrataSceneData->TopLayerTexture;
			PassParameters->MaterialTextureArray = View.StrataSceneData->MaterialTextureArraySRV;
			PassParameters->TileListBuffer = View.StrataSceneData->ClassificationTileListBufferSRV[EStrataTileType::EComplex];
			PassParameters->TileIndirectBuffer = View.StrataSceneData->ClassificationTileDispatchIndirectBuffer;

			PassParameters->RWBSDFOffsetTexture = GraphBuilder.CreateUAV(View.StrataSceneData->BSDFOffsetTexture);
			PassParameters->RWBSDFTileTexture = GraphBuilder.CreateUAV(View.StrataSceneData->BSDFTileTexture);
			PassParameters->RWBSDFTileCountBuffer = RWBSDFTileCountBuffer;

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Strata::BSDFTileAndOffsets(%s)", bWaveOps ? TEXT("Wave") : TEXT("SharedMemory")),
				ComputeShader,
				PassParameters,
				PassParameters->TileIndirectBuffer,
				TileTypeDispatchIndirectArgOffset(EStrataTileType::EComplex));
		}

		// Tile indirect dispatch args conversion
		{
			TShaderMapRef<FStrataBSDFTilePrepareArgsPassCS> ComputeShader(View.ShaderMap);
			FStrataBSDFTilePrepareArgsPassCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FStrataBSDFTilePrepareArgsPassCS::FParameters>();
			PassParameters->TileCount_Primary = View.StrataSceneData->TileCount_Primary;
			PassParameters->TileDrawIndirectDataBuffer = GraphBuilder.CreateSRV(View.StrataSceneData->BSDFTileCountBuffer, PF_R32_UINT);
			PassParameters->TileDispatchIndirectDataBuffer = GraphBuilder.CreateUAV(View.StrataSceneData->BSDFTileDispatchIndirectBuffer, PF_R32_UINT);

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("Strata::BSDFTilePrepareArgs"),
				ComputeShader,
				PassParameters,
				FIntVector(1, 1, 1));
		}
	}
}

static void AddStrataClearMaterialBufferPass(
	FRDGBuilder& GraphBuilder,
	FRDGTextureUAVRef MaterialTextureArrayUAV,
	FRDGTextureUAVRef SSSTextureUAV,
	uint32 MaxBytesPerPixel,
	FIntPoint TiledViewBufferResolution)
{
	if (ClearDuringCategorization())
	{
		return;
	}

	TShaderMapRef<FStrataClearMaterialBufferCS> ComputeShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
	FStrataClearMaterialBufferCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FStrataClearMaterialBufferCS::FParameters>();
	PassParameters->MaterialTextureArrayUAV = MaterialTextureArrayUAV;
	PassParameters->SSSTextureUAV = SSSTextureUAV;
	PassParameters->MaxBytesPerPixel = MaxBytesPerPixel;
	PassParameters->TiledViewBufferResolution = TiledViewBufferResolution;

	const uint32 GroupSize = 8;
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("Strata::ClearMaterialBuffer"),
		ComputeShader,
		PassParameters,
		FComputeShaderUtils::GetGroupCount(TiledViewBufferResolution, GroupSize));
}

bool ShouldRenderStrataDebugPasses(const FViewInfo& View)
{
	return IsStrataEnabled() &&
		(
		   (FVisualizeMaterialPS::CanRunStrataVizualizeMaterial(View.GetShaderPlatform()) && View.Family && View.Family->EngineShowFlags.VisualizeStrataMaterial)
		|| (CVarStrataClassificationDebug.GetValueOnAnyThread() > 0)
		|| ShouldRenderStrataRoughRefractionRnD()
		);
}

FScreenPassTexture AddStrataDebugPasses(FRDGBuilder& GraphBuilder, const FViewInfo& View, FScreenPassTexture& ScreenPassSceneColor)
{
	check(IsStrataEnabled());
	EShaderPlatform Platform = View.GetShaderPlatform();

	if (FVisualizeMaterialPS::CanRunStrataVizualizeMaterial(Platform))
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Strata::VisualizeMaterial");
		AddVisualizeMaterialPasses(GraphBuilder, View, ScreenPassSceneColor.Texture, Platform);
	}

	const int32 StrataClassificationDebug = CVarStrataClassificationDebug.GetValueOnAnyThread();
	if (StrataClassificationDebug > 0)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "Strata::VisualizeClassification");
		const bool bDebugPass = true;
		if (StrataClassificationDebug > 1)
		{
			AddStrataInternalClassificationTilePass(GraphBuilder, View, nullptr, &ScreenPassSceneColor.Texture, EStrataTileType::EOpaqueRoughRefraction, bDebugPass);
			AddStrataInternalClassificationTilePass(GraphBuilder, View, nullptr, &ScreenPassSceneColor.Texture, EStrataTileType::ESSSWithoutOpaqueRoughRefraction, bDebugPass);
		}
		else
		{
			AddStrataInternalClassificationTilePass(GraphBuilder, View, nullptr, &ScreenPassSceneColor.Texture, EStrataTileType::ESimple, bDebugPass);
			AddStrataInternalClassificationTilePass(GraphBuilder, View, nullptr, &ScreenPassSceneColor.Texture, EStrataTileType::ESingle, bDebugPass);
			AddStrataInternalClassificationTilePass(GraphBuilder, View, nullptr, &ScreenPassSceneColor.Texture, EStrataTileType::EComplex, bDebugPass);
		}
	}

	StrataRoughRefractionRnD(GraphBuilder, View, ScreenPassSceneColor);

	return MoveTemp(ScreenPassSceneColor);
}

} // namespace Strata