// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrostySensorGridShaders.h"
#include "ShaderPermutation.h"
#include "ShaderParameterStruct.h"

class FFrostySensorGridAggregateBoundsCs : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFrostySensorGridAggregateBoundsCs);
	SHADER_USE_PARAMETER_STRUCT(FFrostySensorGridAggregateBoundsCs, FGlobalShader);

public:
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, FROSTYSENSORGRIDSHADERS_API)
		SHADER_PARAMETER_UAV(Buffer, BoundsHierarchy)
		SHADER_PARAMETER(FIntVector, TargetStrideAndOffset)
		SHADER_PARAMETER(FIntVector, SourceStrideAndOffset)
	END_SHADER_PARAMETER_STRUCT()
};

IMPLEMENT_GLOBAL_SHADER(FFrostySensorGridAggregateBoundsCs, "/FrostySensorGrid/FrostySensorGridAggregateBounds.usf", "AggregateBounds", SF_Compute);

class FFrostySensorGridFindClosestSensorsCs : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FFrostySensorGridFindClosestSensorsCs);
	SHADER_USE_PARAMETER_STRUCT(FFrostySensorGridFindClosestSensorsCs, FGlobalShader);

public:
	static const int32 MaxHierarchyLevels = 16;
	static const int32 NetworkCountPerGroup = 8;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, FROSTYSENSORGRIDSHADERS_API)
		SHADER_PARAMETER_SRV(Buffer<float4>, SensorHierarchy)
		SHADER_PARAMETER_UAV(StructuredBuffer<FSensorInfo>, NearestSensors)
		SHADER_PARAMETER_ARRAY(int32, HierarchyLevelOffsets, [MaxHierarchyLevels])
		SHADER_PARAMETER(FVector2D, DistanceBounds)
		SHADER_PARAMETER(FIntVector, SensorGridDimensions)
		SHADER_PARAMETER(int32, HierarchyLevelCount)
	END_SHADER_PARAMETER_STRUCT()

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("MAX_HIERARCHY_LEVEL_COUNT"), MaxHierarchyLevels);
		OutEnvironment.SetDefine(TEXT("NETWORK_COUNT_PER_GROUP"), NetworkCountPerGroup);
	}
};

IMPLEMENT_GLOBAL_SHADER(FFrostySensorGridFindClosestSensorsCs, "/FrostySensorGrid/FrostySensorGridFindClosest.usf", "FindClosestSensors", SF_Compute);

FFrostySensorGridHelper::FFrostySensorGridHelper(ERHIFeatureLevel::Type InFeatureLevel, const FIntVector& InSensorGridDimensions)
	: FeatureLevel(InFeatureLevel)
	, SensorGridDimensions(InSensorGridDimensions)
{

}

void FFrostySensorGridHelper::BuildBounds(FRHICommandList& RHICmdList, FRHIUnorderedAccessView* HierarchyUAv)
{
	TShaderMapRef<FFrostySensorGridAggregateBoundsCs> AggregateBoundsShader(GetGlobalShaderMap(FeatureLevel));
	FRHIComputeShader* ShaderRHI = AggregateBoundsShader.GetComputeShader();

	const uint32 LayerCount = FMath::FloorLog2(FMath::Max<uint32>(SensorGridDimensions.X, SensorGridDimensions.Y));

	if (LayerCount)
	{
		RHICmdList.SetComputeShader(ShaderRHI);

		const int32 BaseSensorGridSize = SensorGridDimensions.X * SensorGridDimensions.Y * SensorGridDimensions.Z;

		FIntPoint SourceGrid(SensorGridDimensions.X, SensorGridDimensions.Y);
		int32 SourceOffset = 0;

		FIntPoint TargetGrid(SourceGrid.X >> 1, SourceGrid.Y >> 1);
		int32 TargetOffset = SourceGrid.X * SourceGrid.Y * SensorGridDimensions.Z;

		for (uint32 LayerIt = 0; LayerIt < LayerCount; ++LayerIt)
		{
			if (LayerIt)
			{
				RHICmdList.Transition(FRHITransitionInfo(HierarchyUAv, ERHIAccess::Unknown, ERHIAccess::UAVCompute));
			}

			FFrostySensorGridAggregateBoundsCs::FParameters PassParameters;
			PassParameters.BoundsHierarchy = HierarchyUAv;
			PassParameters.SourceStrideAndOffset = FIntVector(SourceGrid.X, SourceGrid.Y, SourceOffset * sizeof(FVector4));
			PassParameters.TargetStrideAndOffset = FIntVector(TargetGrid.X, TargetGrid.Y, TargetOffset * sizeof(FVector4));

			const FIntVector ThreadGroupCount = FIntVector(TargetGrid.X, TargetGrid.Y, SensorGridDimensions.Z);

			SetShaderParameters(RHICmdList, AggregateBoundsShader, ShaderRHI, PassParameters);
			RHICmdList.DispatchComputeShader(ThreadGroupCount.X, ThreadGroupCount.Y, ThreadGroupCount.Z);

			SourceGrid = TargetGrid;
			SourceOffset = TargetOffset;

			TargetGrid = FIntPoint(TargetGrid.X >> 1, TargetGrid.Y >> 1);
			TargetOffset += SourceGrid.X * SourceGrid.Y * SensorGridDimensions.Z;
		}

		UnsetShaderUAVs(RHICmdList, AggregateBoundsShader, ShaderRHI);
	}
}

void FFrostySensorGridHelper::FindNearestSensors(FRHICommandList& RHICmdList, FRHIShaderResourceView* HierarchySrv, const FVector2D& GlobalSensorRange, FRHIUnorderedAccessView* ResultsUav)
{
	TShaderMapRef<FFrostySensorGridFindClosestSensorsCs> FindClosestSensorShader(GetGlobalShaderMap(FeatureLevel));
	FRHIComputeShader* ShaderRHI = FindClosestSensorShader.GetComputeShader();

	FFrostySensorGridFindClosestSensorsCs::FParameters PassParameters;
	PassParameters.DistanceBounds = GlobalSensorRange;
	PassParameters.SensorHierarchy = HierarchySrv;
	PassParameters.NearestSensors = ResultsUav;
	PassParameters.SensorGridDimensions = SensorGridDimensions;
	PassParameters.HierarchyLevelCount = 1 + FMath::FloorLog2(FMath::Max<uint32>(SensorGridDimensions.X, SensorGridDimensions.Y));

	int32 Offset = 0;

	for (int32 LayerIt = 0; LayerIt < PassParameters.HierarchyLevelCount; ++LayerIt)
	{
		PassParameters.HierarchyLevelOffsets[LayerIt] = Offset * sizeof(FVector4);
		Offset += SensorGridDimensions.Z * (SensorGridDimensions.X >> LayerIt) * (SensorGridDimensions.Y >> LayerIt);
	}

	RHICmdList.SetComputeShader(ShaderRHI);
	SetShaderParameters(RHICmdList, FindClosestSensorShader, ShaderRHI, PassParameters);
	RHICmdList.DispatchComputeShader(SensorGridDimensions.X >> 1, SensorGridDimensions.Y >> 1, SensorGridDimensions.Z);
	UnsetShaderUAVs(RHICmdList, FindClosestSensorShader, ShaderRHI);
}