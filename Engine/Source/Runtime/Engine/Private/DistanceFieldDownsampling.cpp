// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DistanceFieldSampling.cpp
=============================================================================*/

#include "DistanceFieldDownsampling.h"
#include "RHIStaticStates.h"
#include "RenderGraph.h"
#include "GlobalShader.h"
#include "DistanceFieldAtlas.h"

// --------------------------------------------------------------------------------------------------------------------
DECLARE_GPU_STAT(DFMeshDownsampling);

// --------------------------------------------------------------------------------------------------------------------
class FDistanceFieldDownsamplingCS : public FGlobalShader
{
public:
	DECLARE_GLOBAL_SHADER(FDistanceFieldDownsamplingCS);
	SHADER_USE_PARAMETER_STRUCT(FDistanceFieldDownsamplingCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FVector, TexelSrcSize)
		SHADER_PARAMETER(FIntVector4, DstSize)
		SHADER_PARAMETER(FIntVector4, OffsetInAtlas)
		SHADER_PARAMETER_TEXTURE(Texture3D<float>, MeshDF)
		SHADER_PARAMETER_SAMPLER(SamplerState, MeshDFSampler)
		SHADER_PARAMETER_UAV(RWTexture3D<float>, DFAtlas)
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(FGlobalShaderPermutationParameters const& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};

// --------------------------------------------------------------------------------------------------------------------
IMPLEMENT_GLOBAL_SHADER(FDistanceFieldDownsamplingCS, "/Engine/Private/DistanceFieldDownsampling.usf", "DistanceFieldDownsamplingCS", SF_Compute);

// --------------------------------------------------------------------------------------------------------------------
bool FDistanceFieldDownsampling::CanDownsample()
{
	static const auto CVarEightBit = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DistanceFieldBuild.EightBit"));
	return (CVarEightBit->GetValueOnAnyThread() != 0);
}

// --------------------------------------------------------------------------------------------------------------------
void FDistanceFieldDownsampling::GetDownsampledSize(const FIntVector& Size, float Factor, FIntVector& OutDownsampledSize)
{
	if (Size.X <= 6 || Size.Y <= 6 || Size.Z <= 6)
	{
		OutDownsampledSize = Size;
		return;
	}

	FVector DstSizeFloat = FVector(Size);
	DstSizeFloat *= Factor;

	OutDownsampledSize.X = FMath::TruncToInt(DstSizeFloat.X);
	OutDownsampledSize.Y = FMath::TruncToInt(DstSizeFloat.Y);
	OutDownsampledSize.Z = FMath::TruncToInt(DstSizeFloat.Z);
}

// --------------------------------------------------------------------------------------------------------------------
void FDistanceFieldDownsampling::FillDownsamplingTask
(
	const FIntVector& SrcSize, 
	const FIntVector& DstSize, 
	const FIntVector& OffsetInAtlas, 
	EPixelFormat Format,
	FDistanceFieldDownsamplingDataTask& OutDataTask,
	FUpdateTexture3DData& OutUpdateTextureData
)
{
	FRHIResourceCreateInfo CreateInfo;
	OutDataTask.VolumeTextureRHI = RHICreateTexture3D
	(
		SrcSize.X,
		SrcSize.Y,
		SrcSize.Z,
		Format,
		1,
		TexCreate_ShaderResource,
		CreateInfo
	);

	OutDataTask.TexelSrcSize = FVector::OneVector / FVector(float(SrcSize.X), float(SrcSize.Y), float(SrcSize.Z));
	OutDataTask.DstSize = DstSize;
	OutDataTask.OffsetInAtlas = OffsetInAtlas;

	const FUpdateTextureRegion3D UpdateRegion(FIntVector::ZeroValue, FIntVector::ZeroValue, SrcSize);
	OutUpdateTextureData = RHIBeginUpdateTexture3D(OutDataTask.VolumeTextureRHI, 0, UpdateRegion);
}

// --------------------------------------------------------------------------------------------------------------------
void FDistanceFieldDownsampling::DispatchDownsampleTasks(FRHICommandListImmediate& RHICmdList, FRHIUnorderedAccessView* DFAtlasUAV, ERHIFeatureLevel::Type FeatureLevel, TArray<FDistanceFieldDownsamplingDataTask>& DownsamplingTasks, TArray<FUpdateTexture3DData>& UpdateTextureDataArray)
{
	SCOPED_GPU_STAT(RHICmdList, DFMeshDownsampling);

	check(DownsamplingTasks.Num()==UpdateTextureDataArray.Num());

	// End Update texture 3D
	for (int32 Index = 0; Index < DownsamplingTasks.Num(); Index++)
	{
		FUpdateTexture3DData& UpdateTextureData = UpdateTextureDataArray[Index];
		RHIEndUpdateTexture3D(UpdateTextureData);
	}
		
	// Dispatch CS downsample tasks
	FRDGBuilder GraphBuilder(RHICmdList);

	for (int32 Index=0; Index<DownsamplingTasks.Num(); Index++)
	{
		FDistanceFieldDownsamplingDataTask& Task = DownsamplingTasks[Index];

		FDistanceFieldDownsamplingCS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDistanceFieldDownsamplingCS::FParameters>();
		PassParameters->TexelSrcSize = Task.TexelSrcSize;
		PassParameters->DstSize = FIntVector4(Task.DstSize.X, Task.DstSize.Y, Task.DstSize.Z, 0);
		PassParameters->OffsetInAtlas = FIntVector4(Task.OffsetInAtlas.X, Task.OffsetInAtlas.Y, Task.OffsetInAtlas.Z, 0);
		PassParameters->MeshDF = Task.VolumeTextureRHI;
		PassParameters->MeshDFSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		PassParameters->DFAtlas = DFAtlasUAV;	

		TShaderMap<FGlobalShaderType>* GlobalShaderMap = GetGlobalShaderMap(FeatureLevel);
		TShaderMapRef< FDistanceFieldDownsamplingCS > ComputeShader(GlobalShaderMap);
		FDistanceFieldDownsamplingCS* ComputeShaderPtr = *ComputeShader;
		const FIntVector GroupCount(FMath::DivideAndRoundUp(Task.DstSize.X, 8), FMath::DivideAndRoundUp(Task.DstSize.Y, 8), Task.DstSize.Z);

		GraphBuilder.AddPass
		(
			RDG_EVENT_NAME("DownsampleMeshDF"),
			PassParameters, 
			ERDGPassFlags::Compute,
			[PassParameters, ComputeShaderPtr, GroupCount, &Task, &DFAtlasUAV](FRHICommandList& CmdList)
			{
				FComputeShaderUtils::Dispatch(CmdList, ComputeShaderPtr, *PassParameters, GroupCount);
				CmdList.TransitionResources(EResourceTransitionAccess::ERWNoBarrier, EResourceTransitionPipeline::EComputeToCompute, &DFAtlasUAV, 1); // No barrier needed
				Task.VolumeTextureRHI = nullptr;
			}
		);
	}

	GraphBuilder.Execute();
	RHICmdList.TransitionResources(EResourceTransitionAccess::ERWBarrier, EResourceTransitionPipeline::EComputeToCompute, &DFAtlasUAV, 1);
}
