// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenCardFroxelGrid.cpp
=============================================================================*/

#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "LumenSceneUtils.h"
#include "PixelShaderUtils.h"
#include "LumenSceneBVH.h"

int32 GCardFroxelGridPixelSize = 64;
FAutoConsoleVariableRef CVarLumenDiffuseFroxelGridPixelSize(
	TEXT("r.Lumen.DiffuseIndirect.CullGridPixelSize"),
	GCardFroxelGridPixelSize,
	TEXT("Size of a cell in the card grid, in pixels."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GCardGridDistributionLogZScale = .01f;
FAutoConsoleVariableRef CCardGridDistributionLogZScale(
	TEXT("r.Lumen.DiffuseIndirect.CullGridDistributionLogZScale"),
	GCardGridDistributionLogZScale,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GCardGridDistributionLogZOffset = 1.0f;
FAutoConsoleVariableRef CCardGridDistributionLogZOffset(
	TEXT("r.Lumen.DiffuseIndirect.CullGridDistributionLogZOffset"),
	GCardGridDistributionLogZOffset,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GCardGridDistributionZScale = 4.0f;
FAutoConsoleVariableRef CVarCardGridDistributionZScale(
	TEXT("r.Lumen.DiffuseIndirect.CullGridDistributionZScale"),
	GCardGridDistributionZScale,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GCardGridCullToGBuffer = 1;
FAutoConsoleVariableRef CVarCardGridCullToGBuffer(
	TEXT("r.Lumen.DiffuseIndirect.CullGridUseGBuffer"),
	GCardGridCullToGBuffer,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GLumenGIDiffuseIndirectBVHCulling = 1;
FAutoConsoleVariableRef CVarLumenGIDiffuseIndirectBVHCulling(
	TEXT("r.LumenScene.DiffuseIndirectBVHCulling"),
	GLumenGIDiffuseIndirectBVHCulling,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GCardGridUseBVH = 1;
FAutoConsoleVariableRef CVarCardGridUseBVH(
	TEXT("r.Lumen.DiffuseIndirect.CullGridUseBVH"),
	GCardGridUseBVH,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

class FFroxelGridBVHCullingCS : public FBVHCullingBaseCS
{
	DECLARE_GLOBAL_SHADER(FFroxelGridBVHCullingCS)
	SHADER_USE_PARAMETER_STRUCT(FFroxelGridBVHCullingCS, FBVHCullingBaseCS)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FBVHCullingParameters, BVHCullingParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER(FVector, CardGridZParams)
		SHADER_PARAMETER(uint32, CardGridPixelSizeShift)
		SHADER_PARAMETER(float, TanConeAngle)
		SHADER_PARAMETER(float, MinTraceDistance)
		SHADER_PARAMETER(float, MaxTraceDistance)
		SHADER_PARAMETER(float, MaxCardTraceDistance)
		SHADER_PARAMETER(float, CardTraceEndDistanceFromCamera)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FFroxelGridBVHCullingCS, "/Engine/Private/Lumen/LumenCardFroxelGrid.usf", "BVHCullingCS", SF_Compute);

uint32 MarkUsedLinksGroupSize = 8;

class FCardGridMarkUsedByGBufferCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCardGridMarkUsedByGBufferCS)
	SHADER_USE_PARAMETER_STRUCT(FCardGridMarkUsedByGBufferCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWUsedCardData)
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, DownsampledDepth)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardFroxelGridParameters, FroxelGridParameters)
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_STRUCT_REF(FSceneTexturesUniformParameters, SceneTexturesStruct)
		SHADER_PARAMETER(FIntPoint, DownsampledViewSize)
		SHADER_PARAMETER(float, DownsampleFactor)
		SHADER_PARAMETER(uint32, NumCullGridCells)
		SHADER_PARAMETER(uint32, MaxCulledCardsPerCell)
		SHADER_PARAMETER(float, TanConeAngle)
		SHADER_PARAMETER(float, MinTraceDistance)
		SHADER_PARAMETER(float, MaxTraceDistance)
		SHADER_PARAMETER(float, MaxCardTraceDistance)
		SHADER_PARAMETER(float, CardTraceEndDistanceFromCamera)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), MarkUsedLinksGroupSize);
	}
};

IMPLEMENT_GLOBAL_SHADER(FCardGridMarkUsedByGBufferCS, "/Engine/Private/Lumen/LumenCardFroxelGrid.usf", "CardGridMarkUsedByGBufferCS", SF_Compute);

class FCardGridCompactUsedByGBufferCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCardGridCompactUsedByGBufferCS)
	SHADER_USE_PARAMETER_STRUCT(FCardGridCompactUsedByGBufferCS, FGlobalShader)

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCulledCardGridHeader)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWCulledCardGridData)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, UsedCardData)
		SHADER_PARAMETER(FIntVector, CullGridSize)
	END_SHADER_PARAMETER_STRUCT()

	using FPermutationDomain = TShaderPermutationDomain<>;

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static uint32 GetGroupSize()
	{
		return 8;
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize());
	}
};

IMPLEMENT_GLOBAL_SHADER(FCardGridCompactUsedByGBufferCS, "/Engine/Private/Lumen/LumenCardFroxelGrid.usf", "CardGridCompactUsedByGBufferCS", SF_Compute);


void GetCardGridZParams(float NearPlane, float FarPlane, FVector& OutZParams, int32& OutGridSizeZ)
{
	OutGridSizeZ = FMath::TruncToInt(FMath::Log2((FarPlane - NearPlane) * GCardGridDistributionLogZScale) * GCardGridDistributionZScale) + 1;
	OutZParams = FVector(GCardGridDistributionLogZScale, GCardGridDistributionLogZOffset, GCardGridDistributionZScale);
}

void CullLumenCardsToFroxelGrid(
	const FViewInfo& View,
	const FLumenCardTracingInputs& TracingInputs,
	float TanConeAngle, 
	float MinTraceDistance,
	float MaxTraceDistance,
	float MaxCardTraceDistance,
	float CardTraceEndDistanceFromCamera,
	int32 ScreenDownsampleFactor,
	FRDGTextureRef DownsampledDepth,
	FRDGBuilder& GraphBuilder,
	FLumenCardFroxelGridParameters& OutGridParameters)
{
	LLM_SCOPE(ELLMTag::Lumen);

	const FIntPoint CardGridSizeXY = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), GCardFroxelGridPixelSize);
	const float FarPlane = CardTraceEndDistanceFromCamera;

	FVector ZParams;
	int32 CardGridSizeZ;
	GetCardGridZParams(View.NearClippingDistance, FarPlane, ZParams, CardGridSizeZ);

	const FIntVector CullGridSize(CardGridSizeXY.X, CardGridSizeXY.Y, CardGridSizeZ);
	const uint32 NumCullGridCells = CullGridSize.X * CullGridSize.Y * CullGridSize.Z;

	FRDGBufferRef UsedCardData = nullptr;
	FRDGBufferUAVRef UsedCardDataUAV = nullptr;

	FBVHCulling BVHCulling;
	if (GLumenGIDiffuseIndirectBVHCulling)
	{
		RDG_EVENT_SCOPE(GraphBuilder, "DiffuseIndirectBVHCulling");

		BVHCulling.Init(GraphBuilder, View.ShaderMap, CullGridSize);

		for (int32 BVHLevel = 0; BVHLevel < FMath::Max(1, TracingInputs.BVHDepth); ++BVHLevel)
		{
			BVHCulling.InitNextPass(GraphBuilder, View.ShaderMap, BVHLevel);

			// Run pass for the current BVH level.
			{
				FFroxelGridBVHCullingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FFroxelGridBVHCullingCS::FParameters>();
				PassParameters->BVHCullingParameters = BVHCulling.BVHCullingParameters;

				GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters);
				PassParameters->CardGridZParams = ZParams;
				PassParameters->CardGridPixelSizeShift = FMath::FloorLog2(GCardFroxelGridPixelSize);
				PassParameters->TanConeAngle = TanConeAngle;
				PassParameters->MinTraceDistance = MinTraceDistance;
				PassParameters->MaxTraceDistance = MaxTraceDistance;
				PassParameters->MaxCardTraceDistance = MaxCardTraceDistance;
				PassParameters->CardTraceEndDistanceFromCamera = CardTraceEndDistanceFromCamera;

				BVHCulling.NextPass<FFroxelGridBVHCullingCS>(GraphBuilder, View.ShaderMap, BVHLevel, PassParameters);
			}
		}

		UsedCardData = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), NumCullGridCells * BVHCulling.MaxCulledCardsPerCell), TEXT("UsedCardData"));
		UsedCardDataUAV = GraphBuilder.CreateUAV(UsedCardData, PF_R32_UINT);

		BVHCulling.CompactListIntoGrid(GraphBuilder, View.ShaderMap, UsedCardDataUAV);
	}

	OutGridParameters.CulledCardGridHeader = BVHCulling.CulledCardGridHeaderSRV;
	OutGridParameters.CulledCardGridData = BVHCulling.CulledCardGridDataSRV;

	if (GCardGridCullToGBuffer && GLumenGIDiffuseIndirectBVHCulling)
	{
		{
			FCardGridMarkUsedByGBufferCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCardGridMarkUsedByGBufferCS::FParameters>();
			PassParameters->RWUsedCardData = UsedCardDataUAV;
			PassParameters->FroxelGridParameters = OutGridParameters;
			PassParameters->DownsampledDepth = DownsampledDepth;
			PassParameters->DownsampledViewSize = FIntPoint::DivideAndRoundDown(View.ViewRect.Size(), ScreenDownsampleFactor);
			PassParameters->DownsampleFactor = ScreenDownsampleFactor;
			GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters);
			PassParameters->SceneTexturesStruct = CreateSceneTextureUniformBufferSingleDraw(GraphBuilder.RHICmdList, ESceneTextureSetupMode::None, View.FeatureLevel);
			PassParameters->NumCullGridCells = NumCullGridCells;
			PassParameters->MaxCulledCardsPerCell = BVHCulling.MaxCulledCardsPerCell;
			PassParameters->TanConeAngle = TanConeAngle;
			PassParameters->MinTraceDistance = MinTraceDistance;
			PassParameters->MaxTraceDistance = MaxTraceDistance;
			PassParameters->MaxCardTraceDistance = MaxCardTraceDistance;
			PassParameters->CardTraceEndDistanceFromCamera = CardTraceEndDistanceFromCamera;

			auto ComputeShader = View.ShaderMap->GetShader<FCardGridMarkUsedByGBufferCS>();

			FIntPoint GroupSize(FIntPoint::DivideAndRoundUp(FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), ScreenDownsampleFactor), MarkUsedLinksGroupSize));

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("MarkUsedByGBuffer"),
				ComputeShader,
				PassParameters,
				FIntVector(GroupSize.X, GroupSize.Y, 1));
		}

		{
			FRDGBufferSRVRef UsedCardDataSRV = GraphBuilder.CreateSRV(UsedCardData, PF_R32_UINT);

			FCardGridCompactUsedByGBufferCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FCardGridCompactUsedByGBufferCS::FParameters>();
			PassParameters->RWCulledCardGridHeader = BVHCulling.CulledCardGridHeaderUAV;
			PassParameters->RWCulledCardGridData = BVHCulling.CulledCardGridDataUAV;

			PassParameters->UsedCardData = UsedCardDataSRV;
			PassParameters->CullGridSize = CullGridSize;

			auto ComputeShader = View.ShaderMap->GetShader<FCardGridCompactUsedByGBufferCS>(0);

			const FIntVector GroupSize = FIntVector::DivideAndRoundUp(CullGridSize, FCardGridCompactUsedByGBufferCS::GetGroupSize());

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("CompactUsedByGBuffer"),
				ComputeShader,
				PassParameters,
				GroupSize);
		}
	}
}