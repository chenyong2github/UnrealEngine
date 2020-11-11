// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LumenTranslucencyVolumeLighting.cpp
=============================================================================*/

#include "LumenTranslucencyVolumeLighting.h"
#include "RendererPrivate.h"
#include "ScenePrivate.h"
#include "SceneUtils.h"
#include "PipelineStateCache.h"
#include "ShaderParameterStruct.h"
#include "VolumeLighting.h"
#include "LumenSceneUtils.h"
#include "DistanceFieldLightingShared.h"
#include "LumenCubeMapTree.h"
#include "Math/Halton.h"
#include "DistanceFieldAmbientOcclusion.h"

int32 GLumenTranslucencyVolume = 1;
FAutoConsoleVariableRef CVarLumenTranslucencyVolume(
	TEXT("r.Lumen.TranslucencyVolume.Enable"),
	GLumenTranslucencyVolume,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GTranslucencyFroxelGridPixelSize = 32;
FAutoConsoleVariableRef CVarTranslucencyFroxelGridPixelSize(
	TEXT("r.Lumen.TranslucencyVolume.GridPixelSize"),
	GTranslucencyFroxelGridPixelSize,
	TEXT("Size of a cell in the translucency grid, in pixels."),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GTranslucencyGridDistributionLogZScale = .01f;
FAutoConsoleVariableRef CVarTranslucencyGridDistributionLogZScale(
	TEXT("r.Lumen.TranslucencyVolume.GridDistributionLogZScale"),
	GTranslucencyGridDistributionLogZScale,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GTranslucencyGridDistributionLogZOffset = 1.0f;
FAutoConsoleVariableRef CVarTranslucencyGridDistributionLogZOffset(
	TEXT("r.Lumen.TranslucencyVolume.GridDistributionLogZOffset"),
	GTranslucencyGridDistributionLogZOffset,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GTranslucencyGridDistributionZScale = 4.0f;
FAutoConsoleVariableRef CVarTranslucencyGridDistributionZScale(
	TEXT("r.Lumen.TranslucencyVolume.GridDistributionZScale"),
	GTranslucencyGridDistributionZScale,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

float GTranslucencyGridEndDistanceFromCamera = 8000;
FAutoConsoleVariableRef CVarTranslucencyGridEndDistanceFromCamera(
	TEXT("r.Lumen.TranslucencyVolume.EndDistanceFromCamera"),
	GTranslucencyGridEndDistanceFromCamera,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
);

int32 GTranslucencyVolumeTemporalReprojection = 1;
FAutoConsoleVariableRef CVarTranslucencyVolumeTemporalReprojection(
	TEXT("r.Lumen.TranslucencyVolume.TemporalReprojection"),
	GTranslucencyVolumeTemporalReprojection,
	TEXT("Whether to use temporal reprojection."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GTranslucencyVolumeJitter = 0;
FAutoConsoleVariableRef CVarTranslucencyVolumeJitter(
	TEXT("r.Lumen.TranslucencyVolume.Jitter"),
	GTranslucencyVolumeJitter,
	TEXT("Whether to apply jitter to each frame's translucency GI computation, achieving temporal super sampling."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GTranslucencyVolumeHistoryWeight = .7f;
FAutoConsoleVariableRef CVarTranslucencyVolumeHistoryWeight(
	TEXT("r.Lumen.TranslucencyVolume.HistoryWeight"),
	GTranslucencyVolumeHistoryWeight,
	TEXT("How much the history value should be weighted each frame.  This is a tradeoff between visible jittering and responsiveness."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GTranslucencyVolumeTraceStepFactor = 2;
FAutoConsoleVariableRef CVarTranslucencyVolumeTraceStepFactor(
	TEXT("r.Lumen.TranslucencyVolume.TraceStepFactor"),
	GTranslucencyVolumeTraceStepFactor,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

int32 GTranslucencyVolumeNumTargetCones = 16;
FAutoConsoleVariableRef CVarTranslucencyVolumeNumTargetCones(
	TEXT("r.Lumen.TranslucencyVolume.NumCones"),
	GTranslucencyVolumeNumTargetCones,
	TEXT(""),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GTranslucencyVolumeConeAngleScale = 1.0f;
FAutoConsoleVariableRef CVarTranslucencyVolumeConeAngleScale(
	TEXT("r.Lumen.TranslucencyVolume.ConeAngleScale"),
	GTranslucencyVolumeConeAngleScale,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GTranslucencyVolumeVoxelStepFactor = 1.0f;
FAutoConsoleVariableRef CVarTranslucencyVolumeVoxelStepFactor(
	TEXT("r.Lumen.TranslucencyVolume.VoxelStepFactor"),
	GTranslucencyVolumeVoxelStepFactor,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

float GTranslucencyVolumeVoxelTraceStartDistanceScale = 1.0f;
FAutoConsoleVariableRef CVarTranslucencyVoxelTraceStartDistanceScale(
	TEXT("r.Lumen.TranslucencyVolume.VoxelTraceStartDistanceScale"),
	GTranslucencyVolumeVoxelTraceStartDistanceScale,
	TEXT("."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

const static uint32 MaxTranslucencyVolumeConeDirections = 64;

FLumenTranslucencyLightingParameters GetLumenTranslucencyLightingParameters(const FLumenTranslucencyGIVolume& LumenTranslucencyGIVolume)
{
	FLumenTranslucencyLightingParameters Parameters;
	Parameters.TranslucencyGIVolume0 = (LumenTranslucencyGIVolume.Texture0 ? LumenTranslucencyGIVolume.Texture0 : GSystemTextures.VolumetricBlackDummy)->GetRenderTargetItem().ShaderResourceTexture;
	Parameters.TranslucencyGIVolume1 = (LumenTranslucencyGIVolume.Texture1 ? LumenTranslucencyGIVolume.Texture1 : GSystemTextures.VolumetricBlackDummy)->GetRenderTargetItem().ShaderResourceTexture;
	Parameters.TranslucencyGIVolumeHistory0 = (LumenTranslucencyGIVolume.HistoryTexture0 ? LumenTranslucencyGIVolume.HistoryTexture0 : GSystemTextures.VolumetricBlackDummy)->GetRenderTargetItem().ShaderResourceTexture;
	Parameters.TranslucencyGIVolumeHistory1 = (LumenTranslucencyGIVolume.HistoryTexture1 ? LumenTranslucencyGIVolume.HistoryTexture1 : GSystemTextures.VolumetricBlackDummy)->GetRenderTargetItem().ShaderResourceTexture;
	Parameters.TranslucencyGIVolumeSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	Parameters.TranslucencyGIGridZParams = LumenTranslucencyGIVolume.GridZParams;
	Parameters.TranslucencyGIGridPixelSizeShift = LumenTranslucencyGIVolume.GridPixelSizeShift;
	Parameters.TranslucencyGIGridSize = LumenTranslucencyGIVolume.GridSize;
	return Parameters;
}

void GetTranslucencyGridZParams(float NearPlane, float FarPlane, FVector& OutZParams, int32& OutGridSizeZ)
{
	OutGridSizeZ = FMath::TruncToInt(FMath::Log2((FarPlane - NearPlane) * GTranslucencyGridDistributionLogZScale) * GTranslucencyGridDistributionZScale) + 1;
	OutZParams = FVector(GTranslucencyGridDistributionLogZScale, GTranslucencyGridDistributionLogZOffset, GTranslucencyGridDistributionZScale);
}

FVector TranslucencyVolumeTemporalRandom(uint32 FrameNumber)
{
	// Center of the voxel
	FVector RandomOffsetValue(.5f, .5f, .5f);

	if (GTranslucencyVolumeJitter)
	{
		RandomOffsetValue = FVector(Halton(FrameNumber & 1023, 2), Halton(FrameNumber & 1023, 3), Halton(FrameNumber & 1023, 5));
	}

	return RandomOffsetValue;
}

class FTranslucencyLightingCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FTranslucencyLightingCS)
	SHADER_USE_PARAMETER_STRUCT(FTranslucencyLightingCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(FLumenCardTracingParameters, TracingParameters)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWTranslucencyGI0)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWTranslucencyGI1)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWTranslucencyGINewHistory0)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWTranslucencyGINewHistory1)
		SHADER_PARAMETER(FVector, TranslucencyGIGridZParams)
		SHADER_PARAMETER(uint32, TranslucencyGIGridPixelSizeShift)
		SHADER_PARAMETER(FIntVector, TranslucencyGIGridSize)
		SHADER_PARAMETER(float, HistoryWeight)
		SHADER_PARAMETER(FVector, FrameJitterOffset)
		SHADER_PARAMETER(FMatrix, UnjitteredClipToTranslatedWorld)
		SHADER_PARAMETER(FMatrix, UnjitteredPrevWorldToClip)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, TranslucencyGIHistory0)
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D, TranslucencyGIHistory1)
		SHADER_PARAMETER_SAMPLER(SamplerState, TranslucencyGIHistorySampler)
		SHADER_PARAMETER(float, StepFactor)
		SHADER_PARAMETER(float, ConeHalfAngle)
		SHADER_PARAMETER(uint32, NumCones)
		SHADER_PARAMETER(float, SampleWeight)
		SHADER_PARAMETER_ARRAY(FVector4, ConeDirections, [MaxTranslucencyVolumeConeDirections])
		SHADER_PARAMETER(float, MaxTraceDistance)
		SHADER_PARAMETER(float, VoxelStepFactor)
		SHADER_PARAMETER(float, VoxelTraceStartDistanceScale)
	END_SHADER_PARAMETER_STRUCT()

	class FDynamicSkyLight : SHADER_PERMUTATION_BOOL("ENABLE_DYNAMIC_SKY_LIGHT");
	class FTemporalReprojection : SHADER_PERMUTATION_BOOL("USE_TEMPORAL_REPROJECTION");

	using FPermutationDomain = TShaderPermutationDomain<FDynamicSkyLight, FTemporalReprojection>;

	static FIntVector GetGroupSize()
	{
		return FIntVector(4, 4, 4);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return DoesPlatformSupportLumenGI(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREADGROUP_SIZE"), GetGroupSize().X);
		OutEnvironment.CompilerFlags.Add(CFLAG_Wave32);
	}
};

IMPLEMENT_GLOBAL_SHADER(FTranslucencyLightingCS, "/Engine/Private/Lumen/LumenTranslucencyVolumeLighting.usf", "TranslucencyLightingCS", SF_Compute);

FHemisphereDirectionSampleGenerator TranslucencyVolumeGIDirections;

void FDeferredShadingSceneRenderer::ComputeLumenTranslucencyGIVolume(
	FRDGBuilder& GraphBuilder,
	FLumenCardTracingInputs& TracingInputs,
	FGlobalShaderMap* GlobalShaderMap)
{
	if (GLumenTranslucencyVolume)
	{
		FViewInfo& View = Views[0];

		RDG_EVENT_SCOPE(GraphBuilder, "TranslucencyLighting");

		const FIntPoint GridSizeXY = FIntPoint::DivideAndRoundUp(View.ViewRect.Size(), GTranslucencyFroxelGridPixelSize);
		const float FarPlane = GTranslucencyGridEndDistanceFromCamera;

		FVector ZParams;
		int32 GridSizeZ;
		GetTranslucencyGridZParams(View.NearClippingDistance, FarPlane, ZParams, GridSizeZ);

		const FIntVector TranslucencyGridSize(GridSizeXY.X, GridSizeXY.Y, GridSizeZ);
		FRDGTextureRef TranslucencyGIVolumeHistory0 = nullptr;
		FRDGTextureRef TranslucencyGIVolumeHistory1 = nullptr;

		if (View.ViewState && View.ViewState->Lumen.TranslucencyVolume0)
		{
			TranslucencyGIVolumeHistory0 = GraphBuilder.RegisterExternalTexture(View.ViewState->Lumen.TranslucencyVolume0);
			TranslucencyGIVolumeHistory1 = GraphBuilder.RegisterExternalTexture(View.ViewState->Lumen.TranslucencyVolume1);
		}

		FRDGTextureDesc LumenTranslucencyGIDesc0(FRDGTextureDesc::Create3D(TranslucencyGridSize, PF_FloatRGB, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV | TexCreate_3DTiling));
		FRDGTextureDesc LumenTranslucencyGIDesc1(FRDGTextureDesc::Create3D(TranslucencyGridSize, PF_FloatRGBA, FClearValueBinding::Black, TexCreate_ShaderResource | TexCreate_UAV | TexCreate_3DTiling));
	
		FRDGTextureRef TranslucencyGIVolume0 = GraphBuilder.CreateTexture(LumenTranslucencyGIDesc0, TEXT("LumenTranslucencyGIVolume0"));
		FRDGTextureRef TranslucencyGIVolume1 = GraphBuilder.CreateTexture(LumenTranslucencyGIDesc1, TEXT("LumenTranslucencyGIVolume1"));
		FRDGTextureUAVRef TranslucencyGIVolume0UAV = GraphBuilder.CreateUAV(TranslucencyGIVolume0);
		FRDGTextureUAVRef TranslucencyGIVolume1UAV = GraphBuilder.CreateUAV(TranslucencyGIVolume1);

		FRDGTextureRef TranslucencyGIVolumeNewHistory0 = GraphBuilder.CreateTexture(LumenTranslucencyGIDesc0, TEXT("LumenTranslucencyGIVolumeNewHistory0"));
		FRDGTextureRef TranslucencyGIVolumeNewHistory1 = GraphBuilder.CreateTexture(LumenTranslucencyGIDesc1, TEXT("LumenTranslucencyGIVolumeNewHistory1"));
		FRDGTextureUAVRef TranslucencyGIVolumeNewHistory0UAV = GraphBuilder.CreateUAV(TranslucencyGIVolumeNewHistory0);
		FRDGTextureUAVRef TranslucencyGIVolumeNewHistory1UAV = GraphBuilder.CreateUAV(TranslucencyGIVolumeNewHistory1);

		TranslucencyVolumeGIDirections.GenerateSamples(
			FMath::Clamp(GTranslucencyVolumeNumTargetCones, 1, (int32)MaxTranslucencyVolumeConeDirections),
			1,
			GTranslucencyVolumeNumTargetCones,
			true);

		const float ConeHalfAngle = TranslucencyVolumeGIDirections.ConeHalfAngle * GTranslucencyVolumeConeAngleScale;
		const float MaxTraceDistance = Lumen::GetMaxTraceDistance();

		{
			FTranslucencyLightingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FTranslucencyLightingCS::FParameters>();
			PassParameters->RWTranslucencyGI0 = TranslucencyGIVolume0UAV;
			PassParameters->RWTranslucencyGI1 = TranslucencyGIVolume1UAV;
			PassParameters->RWTranslucencyGINewHistory0 = TranslucencyGIVolumeNewHistory0UAV;
			PassParameters->RWTranslucencyGINewHistory1 = TranslucencyGIVolumeNewHistory1UAV;

			GetLumenCardTracingParameters(View, TracingInputs, PassParameters->TracingParameters);

			PassParameters->TranslucencyGIGridZParams = ZParams;
			PassParameters->TranslucencyGIGridPixelSizeShift = FMath::FloorLog2(GTranslucencyFroxelGridPixelSize);
			PassParameters->TranslucencyGIGridSize = TranslucencyGridSize;

			const bool bUseTemporalReprojection =
				GTranslucencyVolumeTemporalReprojection
				&& View.ViewState
				&& !View.bCameraCut
				&& !View.bPrevTransformsReset
				&& ViewFamily.bRealtimeUpdate
				&& TranslucencyGIVolumeHistory0
				&& TranslucencyGIVolumeHistory0->Desc == LumenTranslucencyGIDesc0;

			PassParameters->HistoryWeight = GTranslucencyVolumeHistoryWeight;
			PassParameters->FrameJitterOffset = TranslucencyVolumeTemporalRandom(View.ViewState ? View.ViewState->GetFrameIndex() : 0);
			PassParameters->UnjitteredClipToTranslatedWorld = View.ViewMatrices.ComputeInvProjectionNoAAMatrix() * View.ViewMatrices.GetTranslatedViewMatrix().GetTransposed();
			PassParameters->UnjitteredPrevWorldToClip = View.PrevViewInfo.ViewMatrices.GetViewMatrix() * View.PrevViewInfo.ViewMatrices.ComputeProjectionNoAAMatrix();
			PassParameters->TranslucencyGIHistory0 = TranslucencyGIVolumeHistory0;
			PassParameters->TranslucencyGIHistory1 = TranslucencyGIVolumeHistory1;
			PassParameters->TranslucencyGIHistorySampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

			PassParameters->StepFactor = FMath::Clamp(GTranslucencyVolumeTraceStepFactor, .1f, 10.0f);
			PassParameters->MaxTraceDistance = MaxTraceDistance;
			PassParameters->VoxelStepFactor = FMath::Clamp(GTranslucencyVolumeVoxelStepFactor, .1f, 10.0f);
	
			int32 NumSampleDirections = 0;
			const FVector4* SampleDirections = nullptr;
			TranslucencyVolumeGIDirections.GetSampleDirections(SampleDirections, NumSampleDirections);

			PassParameters->ConeHalfAngle = ConeHalfAngle;
			//@todo - why is 2.0 factor needed to match opaque?
			PassParameters->SampleWeight = 2.0f * (PI * 4.0f) / (float)NumSampleDirections;
			PassParameters->VoxelTraceStartDistanceScale = GTranslucencyVolumeVoxelTraceStartDistanceScale;

			check(NumSampleDirections <= MaxTranslucencyVolumeConeDirections);

			PassParameters->NumCones = NumSampleDirections;
			for (int32 i = 0; i < NumSampleDirections; i++)
			{
				PassParameters->ConeDirections[i] = SampleDirections[i];
			}

			FTranslucencyLightingCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FTranslucencyLightingCS::FDynamicSkyLight>(ShouldRenderDynamicSkyLight(Scene, ViewFamily));
			PermutationVector.Set<FTranslucencyLightingCS::FTemporalReprojection>(bUseTemporalReprojection);
			auto ComputeShader = View.ShaderMap->GetShader<FTranslucencyLightingCS>(PermutationVector);

			const FIntVector GroupSize = FComputeShaderUtils::GetGroupCount(TranslucencyGridSize, FTranslucencyLightingCS::GetGroupSize());

			FComputeShaderUtils::AddPass(
				GraphBuilder,
				RDG_EVENT_NAME("TranslucencyGIVolume"),
				ComputeShader,
				PassParameters,
				GroupSize);
		}

		if (View.ViewState)
		{
			ConvertToExternalTexture(GraphBuilder, TranslucencyGIVolumeNewHistory0, View.ViewState->Lumen.TranslucencyVolume0);
			ConvertToExternalTexture(GraphBuilder, TranslucencyGIVolumeNewHistory1, View.ViewState->Lumen.TranslucencyVolume1);
		}

		ConvertToExternalTexture(GraphBuilder, TranslucencyGIVolume0, View.LumenTranslucencyGIVolume.Texture0);
		ConvertToExternalTexture(GraphBuilder, TranslucencyGIVolume1, View.LumenTranslucencyGIVolume.Texture1);

		ConvertToExternalTexture(GraphBuilder, TranslucencyGIVolumeNewHistory0, View.LumenTranslucencyGIVolume.HistoryTexture0);
		ConvertToExternalTexture(GraphBuilder, TranslucencyGIVolumeNewHistory1, View.LumenTranslucencyGIVolume.HistoryTexture1);

		View.LumenTranslucencyGIVolume.GridZParams = ZParams;
		View.LumenTranslucencyGIVolume.GridPixelSizeShift = FMath::FloorLog2(GTranslucencyFroxelGridPixelSize);
		View.LumenTranslucencyGIVolume.GridSize = TranslucencyGridSize;
	}
}