// Copyright Epic Games, Inc. All Rights Reserved. 

#include "HairStrandsRendering.h"
#include "HairStrandsDatas.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "GlobalShader.h"
#include "GroomAsset.h"
#include "HairStrandsInterface.h"
#include "SceneView.h"
#include "Containers/ResourceArray.h"
#include "Rendering\SkeletalMeshRenderData.h"
#include "HAL/ConsoleManager.h"
#include "GpuDebugRendering.h"
#include "Async/ParallelFor.h"


// Just to be sure, also added this in Eigen.Build.cs
#ifndef EIGEN_MPL2_ONLY
#define EIGEN_MPL2_ONLY
#endif

#if defined(_MSC_VER) && USING_CODE_ANALYSIS
#pragma warning(push)
#pragma warning(disable:6294) // Ill-defined for-loop:  initial condition does not satisfy test.  Loop body not executed.
#endif
PRAGMA_DEFAULT_VISIBILITY_START
THIRD_PARTY_INCLUDES_START
#include <Eigen/Core>
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <Eigen/SparseLU>
THIRD_PARTY_INCLUDES_END
PRAGMA_DEFAULT_VISIBILITY_END
#if defined(_MSC_VER) && USING_CODE_ANALYSIS
#pragma warning(pop)
#endif

static int32 GHairDeformationType = 0;
static FAutoConsoleVariableRef CVarHairDeformationType(TEXT("r.HairStrands.DeformationType"), GHairDeformationType, TEXT("Type of procedural deformation applied on hair strands (0:use simulation's output, 1:use rest strands, 2: use rest guides, 3:wave pattern, 4:follow root normal)"));

static float GHairRaytracingRadiusScale = 0;
static FAutoConsoleVariableRef CVarHairRaytracingRadiusScale(TEXT("r.HairStrands.RaytracingRadiusScale"), GHairRaytracingRadiusScale, TEXT("Override the per instance scale factor for raytracing hair strands geometry (0: disabled, >0:enabled)"));

static int32 GHairStrandsInterpolateSimulation = 1;
static FAutoConsoleVariableRef CVarHairInterpolateSimulation(TEXT("r.HairStrands.InterpolateSimulation"), GHairStrandsInterpolateSimulation, TEXT("Enable/disable simulation output during the hair interpolation"));

static float GStrandHairWidth = 0.0f;
static FAutoConsoleVariableRef CVarStrandHairWidth(TEXT("r.HairStrands.StrandWidth"), GStrandHairWidth, TEXT("Width of hair strand"));

static int32 GStrandHairInterpolationDebug = 0;
static FAutoConsoleVariableRef CVarStrandHairInterpolationDebug(TEXT("r.HairStrands.Interpolation.Debug"), GStrandHairInterpolationDebug, TEXT("Enable debug rendering for hair interpolation"));

static FIntVector ComputeDispatchCount(uint32 ItemCount, uint32 GroupSize)
{
	const uint32 BatchCount = FMath::DivideAndRoundUp(ItemCount, GroupSize);
	const uint32 DispatchCountX = FMath::FloorToInt(FMath::Sqrt(BatchCount));
	const uint32 DispatchCountY = DispatchCountX + FMath::DivideAndRoundUp(BatchCount - DispatchCountX * DispatchCountX, DispatchCountX);

	check(DispatchCountX <= 65535);
	check(DispatchCountY <= 65535);
	check(BatchCount <= DispatchCountX * DispatchCountY);
	return FIntVector(DispatchCountX, DispatchCountY, 1);
}

// Same as above but the group count is what matters and is preserved
static FIntVector ComputeDispatchGroupCount2D(uint32 GroupCount)
{
	const uint32 DispatchCountX = FMath::FloorToInt(FMath::Sqrt(GroupCount));
	const uint32 DispatchCountY = DispatchCountX + FMath::DivideAndRoundUp(GroupCount - DispatchCountX * DispatchCountX, DispatchCountX);

	check(DispatchCountX <= 65535);
	check(DispatchCountY <= 65535);
	check(GroupCount <= DispatchCountX * DispatchCountY);
	return FIntVector(DispatchCountX, DispatchCountY, 1);
}

inline uint32 ComputeGroupSize()
{
	const uint32 GroupSize = IsRHIDeviceAMD() ? 64 : (IsRHIDeviceNVIDIA() ? 32 : 64);
	check(GroupSize == 64 || GroupSize == 32);
	return GroupSize;
}

inline uint32 GetGroupSizePermutation(uint32 GroupSize)
{
	return GroupSize == 64 ? 0 : (GroupSize == 32 ? 1 : 2);
}

enum class EDeformationType : uint8
{
	Simulation,		// Use the output of the hair simulation
	RestStrands,	// Use the rest strands position (no weighted interpolation)
	RestGuide,		// Use the rest guide as input of the interpolation (no deformation), only weighted interpolation
	Wave,			// Apply a wave pattern to deform the guides
	NormalDirection,// Apply a stretch pattern aligned with the guide root's normal
	OffsetGuide		// Offset the guides
};

static EDeformationType GetDeformationType()
{
	switch (GHairDeformationType)
	{
	case 0: return EDeformationType::Simulation;
	case 1: return EDeformationType::RestStrands;
	case 2: return EDeformationType::RestGuide;
	case 3: return EDeformationType::Wave;
	case 4: return EDeformationType::NormalDirection;
	}

	return EDeformationType::Simulation;
}

class FDeformGuideCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FDeformGuideCS);
	SHADER_USE_PARAMETER_STRUCT(FDeformGuideCS, FGlobalShader);

	class FGroupSize : SHADER_PERMUTATION_INT("PERMUTATION_GROUP_SIZE", 2);
	class FDeformationType : SHADER_PERMUTATION_INT("PERMUTATION_DEFORMATION", 6);
	using FPermutationDomain = TShaderPermutationDomain<FGroupSize, FDeformationType>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, VertexCount)
		SHADER_PARAMETER(uint32, IterationCount)
		SHADER_PARAMETER(FVector, SimRestOffset)
		SHADER_PARAMETER(FVector, SimDeformedOffset)
		SHADER_PARAMETER(uint32, DispatchCountX)

		SHADER_PARAMETER_SRV(Buffer<float4>, SimRestPosition0Buffer)
		SHADER_PARAMETER_SRV(Buffer<float4>, SimRestPosition1Buffer)
		SHADER_PARAMETER_SRV(Buffer<float4>, SimRestPosition2Buffer)

		SHADER_PARAMETER_SRV(Buffer<float4>, SimDeformedPosition0Buffer)
		SHADER_PARAMETER_SRV(Buffer<float4>, SimDeformedPosition1Buffer)
		SHADER_PARAMETER_SRV(Buffer<float4>, SimDeformedPosition2Buffer)

		SHADER_PARAMETER_SRV(Buffer<uint>, SimRootBarycentricBuffer)
		SHADER_PARAMETER_SRV(Buffer<uint>, SimVertexToRootIndexBuffer)

		SHADER_PARAMETER_SRV(Buffer, SimRestPosePositionBuffer)
		SHADER_PARAMETER_SRV(Buffer, SimRootIndexBuffer)
		SHADER_PARAMETER_UAV(RWBuffer, OutSimDeformedPositionBuffer)

		SHADER_PARAMETER(uint32, SampleCount)
		SHADER_PARAMETER_SRV(Buffer, RestSamplePositionsBuffer)
		SHADER_PARAMETER_SRV(Buffer, MeshSampleWeightsBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FDeformGuideCS, "/Engine/Private/HairStrands/HairStrandsGuideDeform.usf", "MainCS", SF_Compute);

static void AddDeformSimHairStrandsPass(
	FRDGBuilder& GraphBuilder,
	EDeformationType DeformationType,
	uint32 VertexCount,
	const uint32 LODIndex,
	const FHairStrandsProjectionHairData::HairGroup& InSimHairData,
	FRHIShaderResourceView* SimRestPosePositionBuffer,
	FRHIShaderResourceView* SimRootIndexBuffer,
	FRHIUnorderedAccessView* OutSimDeformedPositionBuffer,
	FVector& SimRestOffset,
	FVector& SimDeformedOffset,
	FBufferTransitionQueue& OutTransitionQueue,
	const bool bHasGlobalInterpolation)
{
	static uint32 IterationCount = 0;
	++IterationCount;

	int32 InternalDeformationType = -1;
	switch (DeformationType)
	{
	case EDeformationType::RestGuide: InternalDeformationType = 0; break;
	case EDeformationType::Wave: InternalDeformationType = 1; break;
	case EDeformationType::NormalDirection: InternalDeformationType = 2; break;
	case EDeformationType::OffsetGuide : InternalDeformationType = 3; break;
	}

	if (InternalDeformationType < 0) return;

	const uint32 GroupSize = ComputeGroupSize();
	const uint32 DispatchCount = FMath::DivideAndRoundUp(VertexCount, GroupSize);
	const uint32 DispatchCountX = 16;
	const uint32 DispatchCountY = FMath::DivideAndRoundUp(DispatchCount, DispatchCountX);

	FDeformGuideCS::FParameters* Parameters = GraphBuilder.AllocParameters<FDeformGuideCS::FParameters>();
	Parameters->SimRestPosePositionBuffer = SimRestPosePositionBuffer;
	Parameters->SimRootIndexBuffer = SimRootIndexBuffer;
	Parameters->OutSimDeformedPositionBuffer = OutSimDeformedPositionBuffer;
	Parameters->VertexCount = VertexCount;
	Parameters->IterationCount = IterationCount % 10000;
	Parameters->SimDeformedOffset = SimDeformedOffset;
	Parameters->SimRestOffset = SimRestOffset;
	Parameters->DispatchCountX = DispatchCountX;

	if (DeformationType == EDeformationType::OffsetGuide)
	{
		const bool bIsVertexToCurveBuffersValid = InSimHairData.VertexToCurveIndexBuffer != nullptr;
		if (bIsVertexToCurveBuffersValid)
		{
			Parameters->SimVertexToRootIndexBuffer = InSimHairData.VertexToCurveIndexBuffer->SRV;
		}

		const bool bSupportDynamicMesh = 
			InSimHairData.RootCount > 0 && 
			LODIndex >= 0 && 
			LODIndex < uint32(InSimHairData.RestLODDatas.Num()) && 
			LODIndex < uint32(InSimHairData.DeformedLODDatas.Num()) &&
			InSimHairData.RestLODDatas[LODIndex].IsValid() &&
			InSimHairData.DeformedLODDatas[LODIndex].IsValid() &&
			bIsVertexToCurveBuffersValid;
		
		bool bSupportGlobalInterpolation = false;
		if (bSupportDynamicMesh)
		{
			bSupportGlobalInterpolation = bHasGlobalInterpolation && (InSimHairData.RestLODDatas[LODIndex].SampleCount > 0);
			if (!bSupportGlobalInterpolation) 
			{
				InternalDeformationType = 4;
				Parameters->SimRestPosition0Buffer = InSimHairData.RestLODDatas[LODIndex].RestRootTrianglePosition0Buffer->SRV;
				Parameters->SimRestPosition1Buffer = InSimHairData.RestLODDatas[LODIndex].RestRootTrianglePosition1Buffer->SRV;
				Parameters->SimRestPosition2Buffer = InSimHairData.RestLODDatas[LODIndex].RestRootTrianglePosition2Buffer->SRV;

				Parameters->SimDeformedPosition0Buffer = InSimHairData.DeformedLODDatas[LODIndex].DeformedRootTrianglePosition0Buffer->SRV;
				Parameters->SimDeformedPosition1Buffer = InSimHairData.DeformedLODDatas[LODIndex].DeformedRootTrianglePosition1Buffer->SRV;
				Parameters->SimDeformedPosition2Buffer = InSimHairData.DeformedLODDatas[LODIndex].DeformedRootTrianglePosition2Buffer->SRV;

				Parameters->SimRootBarycentricBuffer = InSimHairData.RestLODDatas[LODIndex].RootTriangleBarycentricBuffer->SRV;
			}
			else
			{
				InternalDeformationType = 5;
				Parameters->MeshSampleWeightsBuffer = InSimHairData.DeformedLODDatas[LODIndex].MeshSampleWeightsBuffer->SRV;
				Parameters->RestSamplePositionsBuffer = InSimHairData.RestLODDatas[LODIndex].RestSamplePositionsBuffer->SRV;
				Parameters->SampleCount = InSimHairData.RestLODDatas[LODIndex].SampleCount;
			}
		}
	}

	FDeformGuideCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FDeformGuideCS::FGroupSize>(GetGroupSizePermutation(GroupSize));
	PermutationVector.Set<FDeformGuideCS::FDeformationType>(InternalDeformationType);

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);

	TShaderMapRef<FDeformGuideCS> ComputeShader(ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("DeformSimHairStrands"),
		ComputeShader,
		Parameters,
		FIntVector(DispatchCountX, DispatchCountY, 1));

	OutTransitionQueue.Add(OutSimDeformedPositionBuffer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

struct FHairScaleAndClipDesc
{
	float InHairLength;
	float InHairRadius;
	float OutHairRadius;
	float MaxOutHairRadius;
	float HairRadiusRootScale;
	float HairRadiusTipScale;
	float HairLengthClip;
};

class FHairInterpolationCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairInterpolationCS);
	SHADER_USE_PARAMETER_STRUCT(FHairInterpolationCS, FGlobalShader);

	class FGroupSize : SHADER_PERMUTATION_INT("PERMUTATION_GROUP_SIZE", 2);
	class FDebug : SHADER_PERMUTATION_INT("PERMUTATION_DEBUG", 2);
	class FDynamicGeometry : SHADER_PERMUTATION_INT("PERMUTATION_DYNAMIC_GEOMETRY", 5);
	class FSimulation : SHADER_PERMUTATION_INT("PERMUTATION_SIMULATION", 2);
	class FScaleAndClip : SHADER_PERMUTATION_INT("PERMUTATION_SCALE_AND_CLIP", 2);
	using FPermutationDomain = TShaderPermutationDomain<FGroupSize, FDebug, FDynamicGeometry, FSimulation, FScaleAndClip>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
		SHADER_PARAMETER(uint32, VertexCount)
		SHADER_PARAMETER(uint32, DispatchCountX)
		SHADER_PARAMETER(uint32, HairDebugMode)
		SHADER_PARAMETER(FVector, InRenderHairPositionOffset)
		SHADER_PARAMETER(FVector, InSimHairPositionOffset)
		SHADER_PARAMETER(FVector, OutHairPositionOffset)
		SHADER_PARAMETER(FIntPoint, HairStrandsCullIndex)

		SHADER_PARAMETER(float, InHairLength)
		SHADER_PARAMETER(float, InHairRadius)
		SHADER_PARAMETER(float, OutHairRadius)
		SHADER_PARAMETER(float, MaxOutHairRadius)
		SHADER_PARAMETER(float, HairRadiusRootScale)
		SHADER_PARAMETER(float, HairRadiusTipScale)
		SHADER_PARAMETER(float, HairLengthClip)

		SHADER_PARAMETER(FMatrix, LocalToWorldMatrix)

		SHADER_PARAMETER_SRV(Buffer, RenderRestPosePositionBuffer)
		SHADER_PARAMETER_UAV(RWBuffer, OutRenderDeformedPositionBuffer)

		SHADER_PARAMETER_SRV(Buffer, VertexToClusterIdBuffer)

		SHADER_PARAMETER_SRV(Buffer, SimRestPosePositionBuffer)
		SHADER_PARAMETER_SRV(Buffer, DeformedSimPositionBuffer)

		SHADER_PARAMETER_SRV(Buffer, Interpolation0Buffer)
		SHADER_PARAMETER_SRV(Buffer, Interpolation1Buffer)

		SHADER_PARAMETER_SRV(Buffer, AttributeBuffer)
		SHADER_PARAMETER_SRV(Buffer, SimAttributeBuffer)
		SHADER_PARAMETER_UAV(RWBuffer, OutRenderAttributeBuffer)
		SHADER_PARAMETER_SRV(Buffer<float4>, RestPosition0Buffer)
		SHADER_PARAMETER_SRV(Buffer<float4>, RestPosition1Buffer)
		SHADER_PARAMETER_SRV(Buffer<float4>, RestPosition2Buffer)

		SHADER_PARAMETER_SRV(Buffer<float4>, DeformedPosition0Buffer)
		SHADER_PARAMETER_SRV(Buffer<float4>, DeformedPosition1Buffer)
		SHADER_PARAMETER_SRV(Buffer<float4>, DeformedPosition2Buffer)

		SHADER_PARAMETER_SRV(Buffer<uint>, RootBarycentricBuffer)
		SHADER_PARAMETER_SRV(Buffer<uint>, RenVertexToRootIndexBuffer)

		SHADER_PARAMETER_SRV(Buffer<float4>, SimRestPosition0Buffer)
		SHADER_PARAMETER_SRV(Buffer<float4>, SimRestPosition1Buffer)
		SHADER_PARAMETER_SRV(Buffer<float4>, SimRestPosition2Buffer)

		SHADER_PARAMETER_SRV(Buffer<float4>, SimDeformedPosition0Buffer)
		SHADER_PARAMETER_SRV(Buffer<float4>, SimDeformedPosition1Buffer)
		SHADER_PARAMETER_SRV(Buffer<float4>, SimDeformedPosition2Buffer)

		SHADER_PARAMETER_SRV(Buffer<uint>, SimRootBarycentricBuffer)
		SHADER_PARAMETER_SRV(Buffer<uint>, SimVertexToRootIndexBuffer)

		SHADER_PARAMETER_SRV(Buffer<uint>, SimRootPointIndexBuffer)

		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FHairInterpolationCS, "/Engine/Private/HairStrands/HairStrandsInterpolation.usf", "MainCS", SF_Compute);

static void AddHairStrandsInterpolationPass(
	FRDGBuilder& GraphBuilder,
	const FShaderDrawDebugData* ShaderDrawData,
	const FHairStrandsProjectionHairData::HairGroup& InRenHairData,
	const FHairStrandsProjectionHairData::HairGroup& InSimHairData,
	const FVector& InRenderHairWorldOffset,
	const FVector& InSimHairWorldOffset,
	const FVector& OutHairWorldOffset,
	const FHairScaleAndClipDesc ScaleAndClipDesc,
	const int32 LODIndex,
	const bool bHasSimulationEnable,
	const bool bPatchedAttributeBuffer, 
	const uint32 VertexCount,
	const FShaderResourceViewRHIRef& RenderRestPosePositionBuffer,
	const FShaderResourceViewRHIRef& RenderAttributeBuffer,
	const FShaderResourceViewRHIRef& Interpolation0Buffer,
	const FShaderResourceViewRHIRef& Interpolation1Buffer,
	const FShaderResourceViewRHIRef& SimRestPosePositionBuffer,
	const FShaderResourceViewRHIRef& SimDeformedPositionBuffer,
	const FShaderResourceViewRHIRef& SimAttributeBuffer,
	const FUnorderedAccessViewRHIRef& OutRenderPositionBuffer,
	const FUnorderedAccessViewRHIRef& OutRenderAttributeBuffer,
	const FShaderResourceViewRHIRef& VertexToClusterIdBuffer,
	const FShaderResourceViewRHIRef& SimRootPointIndexBuffer,
	FBufferTransitionQueue& OutTransitionQueue,
	const bool bHasGlobalInterpolation,
	const uint32 HairInterpolationType)
{
	const uint32 GroupSize = ComputeGroupSize();
	const FIntVector DispatchCount = ComputeDispatchCount(VertexCount, GroupSize);

	FHairInterpolationCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairInterpolationCS::FParameters>();
	Parameters->RenderRestPosePositionBuffer = RenderRestPosePositionBuffer;
	Parameters->SimRestPosePositionBuffer = SimRestPosePositionBuffer;
	Parameters->DeformedSimPositionBuffer = SimDeformedPositionBuffer;
	Parameters->Interpolation0Buffer = Interpolation0Buffer;
	Parameters->Interpolation1Buffer = Interpolation1Buffer;
	Parameters->OutRenderDeformedPositionBuffer = OutRenderPositionBuffer;
	Parameters->HairStrandsCullIndex = FIntPoint(-1, -1);
	Parameters->VertexCount = VertexCount;
	Parameters->InRenderHairPositionOffset = InRenderHairWorldOffset;
	Parameters->InSimHairPositionOffset = InSimHairWorldOffset;
	Parameters->OutHairPositionOffset = OutHairWorldOffset;
	Parameters->DispatchCountX = DispatchCount.X;
	Parameters->SimRootPointIndexBuffer = SimRootPointIndexBuffer;
	
	const bool bNeedScaleOrClip = 
		ScaleAndClipDesc.InHairRadius != ScaleAndClipDesc.OutHairRadius || 
		ScaleAndClipDesc.HairRadiusRootScale != 1 || 
		ScaleAndClipDesc.HairRadiusTipScale != 1 ||
		ScaleAndClipDesc.HairLengthClip < 1;

	Parameters->InHairLength = ScaleAndClipDesc.InHairLength;
	Parameters->InHairRadius = ScaleAndClipDesc.InHairRadius;
	Parameters->OutHairRadius = ScaleAndClipDesc.OutHairRadius;
	Parameters->MaxOutHairRadius = ScaleAndClipDesc.MaxOutHairRadius;
	Parameters->HairRadiusRootScale = ScaleAndClipDesc.HairRadiusRootScale;
	Parameters->HairRadiusTipScale = ScaleAndClipDesc.HairRadiusTipScale;
	Parameters->HairLengthClip = ScaleAndClipDesc.HairLengthClip * ScaleAndClipDesc.InHairLength; // HairLengthClip is the normalized length and we convert it to world length
	if (bNeedScaleOrClip)
	{
		Parameters->AttributeBuffer = RenderAttributeBuffer;
	}
	const bool bIsVertexToCurveBuffersValid = InRenHairData.VertexToCurveIndexBuffer && InSimHairData.VertexToCurveIndexBuffer;
	if (bIsVertexToCurveBuffersValid)
	{
		Parameters->RenVertexToRootIndexBuffer = InRenHairData.VertexToCurveIndexBuffer->SRV;
		Parameters->SimVertexToRootIndexBuffer = InSimHairData.VertexToCurveIndexBuffer->SRV;
	}

	Parameters->VertexToClusterIdBuffer = VertexToClusterIdBuffer;
	
	Parameters->LocalToWorldMatrix = InRenHairData.LocalToWorld.ToMatrixWithScale();

	// Debug rendering
	Parameters->HairDebugMode = 0;
	{
		const FHairCullInfo Info = GetHairStrandsCullInfo();
		const bool bCullingEnable = Info.CullMode != EHairCullMode::None && bIsVertexToCurveBuffersValid;

		if (bPatchedAttributeBuffer)
		{
			Parameters->HairDebugMode = 1;
			check(SimAttributeBuffer != nullptr);
			check(OutRenderAttributeBuffer != nullptr);
		}
		else if (GStrandHairInterpolationDebug > 0)
		{
			Parameters->HairDebugMode = 4;
		}
		else if (GetHairStrandsDebugStrandsMode() == EHairStrandsDebugMode::RenderVisCluster)
		{
			Parameters->HairDebugMode = 3;
		}
		else if (bCullingEnable)
		{
			Parameters->HairDebugMode = 2;
			
			if (Info.CullMode == EHairCullMode::Sim)
				Parameters->HairStrandsCullIndex.Y = Info.ExplicitIndex >= 0 ? Info.ExplicitIndex : FMath::Clamp(uint32(Info.NormalizedIndex * InSimHairData.RootCount), 0u, InSimHairData.RootCount - 1);
			if (Info.CullMode == EHairCullMode::Render)
				Parameters->HairStrandsCullIndex.X = Info.ExplicitIndex >= 0 ? Info.ExplicitIndex : FMath::Clamp(uint32(Info.NormalizedIndex * InRenHairData.RootCount), 0u, InRenHairData.RootCount - 1);
		}

		if (Parameters->HairDebugMode > 0)
		{
			Parameters->SimAttributeBuffer = SimAttributeBuffer;
			Parameters->OutRenderAttributeBuffer = OutRenderAttributeBuffer;
			OutTransitionQueue.Add(OutRenderAttributeBuffer);
		}
	}

	const bool bSupportDynamicMesh = 
		InRenHairData.RootCount > 0 && 
		LODIndex >= 0 && 
		LODIndex < InRenHairData.RestLODDatas.Num() && 
		LODIndex < InRenHairData.DeformedLODDatas.Num() &&
		InRenHairData.RestLODDatas[LODIndex].IsValid() &&
		InRenHairData.DeformedLODDatas[LODIndex].IsValid() &&
		bIsVertexToCurveBuffersValid;

	bool bSupportGlobalInterpolation = false;
	if (bSupportDynamicMesh)
	{
		bSupportGlobalInterpolation = bHasGlobalInterpolation && (InSimHairData.RestLODDatas[LODIndex].SampleCount > 0);
		{
			Parameters->RestPosition0Buffer = InRenHairData.RestLODDatas[LODIndex].RestRootTrianglePosition0Buffer->SRV;
			Parameters->RestPosition1Buffer = InRenHairData.RestLODDatas[LODIndex].RestRootTrianglePosition1Buffer->SRV;
			Parameters->RestPosition2Buffer = InRenHairData.RestLODDatas[LODIndex].RestRootTrianglePosition2Buffer->SRV;

			Parameters->RootBarycentricBuffer = InRenHairData.RestLODDatas[LODIndex].RootTriangleBarycentricBuffer->SRV;

			Parameters->SimRestPosition0Buffer = InSimHairData.RestLODDatas[LODIndex].RestRootTrianglePosition0Buffer->SRV;
			Parameters->SimRestPosition1Buffer = InSimHairData.RestLODDatas[LODIndex].RestRootTrianglePosition1Buffer->SRV;
			Parameters->SimRestPosition2Buffer = InSimHairData.RestLODDatas[LODIndex].RestRootTrianglePosition2Buffer->SRV;

			Parameters->SimRootBarycentricBuffer = InSimHairData.RestLODDatas[LODIndex].RootTriangleBarycentricBuffer->SRV;
		}
		{
			Parameters->DeformedPosition0Buffer = InRenHairData.DeformedLODDatas[LODIndex].DeformedRootTrianglePosition0Buffer->SRV;
			Parameters->DeformedPosition1Buffer = InRenHairData.DeformedLODDatas[LODIndex].DeformedRootTrianglePosition1Buffer->SRV;
			Parameters->DeformedPosition2Buffer = InRenHairData.DeformedLODDatas[LODIndex].DeformedRootTrianglePosition2Buffer->SRV;

			Parameters->SimDeformedPosition0Buffer = InSimHairData.DeformedLODDatas[LODIndex].DeformedRootTrianglePosition0Buffer->SRV;
			Parameters->SimDeformedPosition1Buffer = InSimHairData.DeformedLODDatas[LODIndex].DeformedRootTrianglePosition1Buffer->SRV;
			Parameters->SimDeformedPosition2Buffer = InSimHairData.DeformedLODDatas[LODIndex].DeformedRootTrianglePosition2Buffer->SRV;
		}
	}

	if (ShaderDrawDebug::IsShaderDrawDebugEnabled() && ShaderDrawData)
	{
		ShaderDrawDebug::SetParameters(GraphBuilder, *ShaderDrawData, Parameters->ShaderDrawParameters);
	}

	const bool bHasLocalDeformation = (bHasSimulationEnable || bSupportGlobalInterpolation);

	FHairInterpolationCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairInterpolationCS::FGroupSize>(GetGroupSizePermutation(GroupSize));
	PermutationVector.Set<FHairInterpolationCS::FDebug>(Parameters->HairDebugMode > 0 ? 1 : 0);
	PermutationVector.Set<FHairInterpolationCS::FDynamicGeometry>((bSupportDynamicMesh && bHasLocalDeformation) ? HairInterpolationType+1 :
							(bSupportDynamicMesh && !bHasLocalDeformation) ? 1 : 0);
	PermutationVector.Set<FHairInterpolationCS::FSimulation>(bHasLocalDeformation ? 1 : 0);
	PermutationVector.Set<FHairInterpolationCS::FScaleAndClip>(bNeedScaleOrClip ? 1 : 0);

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);

	TShaderMapRef<FHairInterpolationCS> ComputeShader(ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsInterpolationDirect"),
		ComputeShader,
		Parameters,
		DispatchCount);

	OutTransitionQueue.Add(OutRenderPositionBuffer);	
}

///////////////////////////////////////////////////////////////////////////////////////////////////


class FHairClusterAABBCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairClusterAABBCS);
	SHADER_USE_PARAMETER_STRUCT(FHairClusterAABBCS, FGlobalShader);

	class FGroupSize : SHADER_PERMUTATION_INT("PERMUTATION_GROUP_SIZE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FGroupSize>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, DispatchCountX)
		SHADER_PARAMETER(uint32, ClusterCount)
		SHADER_PARAMETER(FVector, OutHairPositionOffset)
		SHADER_PARAMETER(FMatrix, LocalToWorldMatrix)
		SHADER_PARAMETER_SRV(Buffer, RenderDeformedPositionBuffer)
		SHADER_PARAMETER_SRV(Buffer, ClusterVertexIdBuffer)
		SHADER_PARAMETER_SRV(Buffer, ClusterInfoBuffer)
		SHADER_PARAMETER_UAV(RWBuffer, OutClusterAABBBuffer)
		SHADER_PARAMETER_UAV(RWBuffer, OutGroupAABBBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FHairClusterAABBCS, "/Engine/Private/HairStrands/HairStrandsInterpolation.usf", "ClusterAABBEvaluationCS", SF_Compute);

static void AddHairClusterAABBPass(
	FRDGBuilder& GraphBuilder,
	const FHairStrandsProjectionHairData::HairGroup& InRenHairData,
	const FVector& OutHairWorldOffset,
	FHairStrandClusterData::FHairGroup& ClusterData,
	const FShaderResourceViewRHIRef& RenderPositionBuffer,
	FBufferTransitionQueue& OutTransitionQueue)
{
	const uint32 GroupSize = ComputeGroupSize();
	const FIntVector DispatchCount = ComputeDispatchGroupCount2D(ClusterData.ClusterCount);

	FHairClusterAABBCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairClusterAABBCS::FParameters>();
	Parameters->DispatchCountX = DispatchCount.X;
	Parameters->ClusterCount = ClusterData.ClusterCount;
	Parameters->LocalToWorldMatrix = InRenHairData.LocalToWorld.ToMatrixWithScale();
	Parameters->OutHairPositionOffset = OutHairWorldOffset;
	Parameters->RenderDeformedPositionBuffer = RenderPositionBuffer;
	Parameters->ClusterVertexIdBuffer = ClusterData.ClusterVertexIdBuffer->SRV;
	Parameters->ClusterInfoBuffer = ClusterData.ClusterInfoBuffer->SRV;
	Parameters->OutClusterAABBBuffer = ClusterData.HairGroupPublicPtr->GetClusterAABBBuffer().UAV;
	Parameters->OutGroupAABBBuffer = ClusterData.HairGroupPublicPtr->GetGroupAABBBuffer().UAV;

	FHairClusterAABBCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairClusterAABBCS::FGroupSize>(GetGroupSizePermutation(GroupSize));
	TShaderMapRef<FHairClusterAABBCS> ComputeShader(GetGlobalShaderMap(ERHIFeatureLevel::SM5), PermutationVector);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsClusterAABB"),
		ComputeShader,
		Parameters,
		DispatchCount);

	OutTransitionQueue.Add(Parameters->OutClusterAABBBuffer);
	OutTransitionQueue.Add(Parameters->OutGroupAABBBuffer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairTangentCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairTangentCS);
	SHADER_USE_PARAMETER_STRUCT(FHairTangentCS, FGlobalShader);

	class FGroupSize : SHADER_PERMUTATION_INT("PERMUTATION_GROUP_SIZE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FGroupSize>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, VertexCount)
		SHADER_PARAMETER(uint32, DispatchCountX)
		SHADER_PARAMETER_SRV(Buffer, PositionBuffer)
		SHADER_PARAMETER_UAV(RWBuffer, OutputTangentBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FHairTangentCS, "/Engine/Private/HairStrands/HairStrandsTangent.usf", "MainCS", SF_Compute);

static void AddHairTangentPass(
	FRDGBuilder& GraphBuilder,
	uint32 VertexCount,
	const FShaderResourceViewRHIRef& PositionBuffer,
	const FUnorderedAccessViewRHIRef& OutTangentBuffer,
	FBufferTransitionQueue& OutTransitionQueue)
{
	const uint32 GroupSize = ComputeGroupSize();
	const FIntVector DispatchCount = ComputeDispatchCount(VertexCount, GroupSize);

	FHairTangentCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairTangentCS::FParameters>();
	Parameters->PositionBuffer = PositionBuffer;
	Parameters->OutputTangentBuffer = OutTangentBuffer;
	Parameters->VertexCount = VertexCount;
	Parameters->DispatchCountX = DispatchCount.X;

	FHairTangentCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairTangentCS::FGroupSize>(GetGroupSizePermutation(GroupSize));

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);

	TShaderMapRef<FHairTangentCS> ComputeShader(ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsTangent"),
		ComputeShader,
		Parameters,
		DispatchCount);

	OutTransitionQueue.Add(OutTangentBuffer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairRaytracingGeometryCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairRaytracingGeometryCS);
	SHADER_USE_PARAMETER_STRUCT(FHairRaytracingGeometryCS, FGlobalShader);

	class FGroupSize : SHADER_PERMUTATION_INT("PERMUTATION_GROUP_SIZE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FGroupSize>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, VertexCount)
		SHADER_PARAMETER(uint32, DispatchCountX)
		SHADER_PARAMETER(FVector, StrandHairWorldOffset)
		SHADER_PARAMETER(float, StrandHairRadius)
		SHADER_PARAMETER_SRV(Buffer, PositionBuffer)
		SHADER_PARAMETER_UAV(RWBuffer, OutputPositionBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FHairRaytracingGeometryCS, "/Engine/Private/HairStrands/HairStrandsRaytracingGeometry.usf", "MainCS", SF_Compute);

static void AddGenerateRaytracingGeometryPass(
	FRDGBuilder& GraphBuilder,
	uint32 VertexCount,
	float HairRadius,
	const FVector& HairWorldOffset,
	const FShaderResourceViewRHIRef& PositionBuffer,
	const FUnorderedAccessViewRHIRef& OutPositionBuffer,
	FBufferTransitionQueue& OutTransitionQueue)
{
	const uint32 GroupSize = ComputeGroupSize();
	const FIntVector DispatchCount = ComputeDispatchCount(VertexCount, GroupSize);

	FHairRaytracingGeometryCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairRaytracingGeometryCS::FParameters>();
	Parameters->VertexCount = VertexCount;
	Parameters->DispatchCountX = DispatchCount.X;
	Parameters->StrandHairWorldOffset = HairWorldOffset;
	Parameters->StrandHairRadius = HairRadius;
	Parameters->PositionBuffer = PositionBuffer;
	Parameters->OutputPositionBuffer = OutPositionBuffer;

	FHairRaytracingGeometryCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairRaytracingGeometryCS::FGroupSize>(GetGroupSizePermutation(GroupSize));

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);

	TShaderMapRef<FHairRaytracingGeometryCS> ComputeShader(ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsRaytracingGeometry"),
		ComputeShader,
		Parameters,
		DispatchCount);

	OutTransitionQueue.Add(OutPositionBuffer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FClearClusterAABBCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearClusterAABBCS);
	SHADER_USE_PARAMETER_STRUCT(FClearClusterAABBCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_UAV(RWBuffer, OutClusterAABBBuffer)
		SHADER_PARAMETER_UAV(RWBuffer, OutGroupAABBBuffer)
		SHADER_PARAMETER(uint32, ClusterCount)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_CLEARCLUSTERAABB"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearClusterAABBCS, "/Engine/Private/HairStrands/HairStrandsClusterCulling.usf", "MainClearClusterAABBCS", SF_Compute);

static void AddClearClusterAABBPass(
	FRDGBuilder& GraphBuilder,
	uint32 ClusterCount,
	FRHIUnorderedAccessView* OutClusterAABBuffer,
	FRHIUnorderedAccessView* OutGroupAABBuffer,
	FBufferTransitionQueue& OutTransitionQueue)
{
	check(OutClusterAABBuffer);

	FClearClusterAABBCS::FParameters* Parameters = GraphBuilder.AllocParameters<FClearClusterAABBCS::FParameters>();
	Parameters->ClusterCount = ClusterCount;
	Parameters->OutClusterAABBBuffer = OutClusterAABBuffer;
	Parameters->OutGroupAABBBuffer = OutGroupAABBuffer;

	
	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
	TShaderMapRef<FClearClusterAABBCS> ComputeShader(ShaderMap);

	const FIntVector DispatchCount = DispatchCount.DivideAndRoundUp(FIntVector(ClusterCount * 6, 1, 1) , FIntVector(64, 1, 1));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsClearClusterAABB"),
		ComputeShader,
		Parameters,
		DispatchCount);

	OutTransitionQueue.Add(OutClusterAABBuffer);
	OutTransitionQueue.Add(OutGroupAABBuffer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

#if RHI_RAYTRACING
static void UpdateHairAccelerationStructure(FRHICommandList& RHICmdList, FRayTracingGeometry* RayTracingGeometry)
{
	SCOPED_DRAW_EVENT(RHICmdList, CommitHairRayTracingGeometryUpdates);

	FAccelerationStructureBuildParams Params;
	Params.BuildMode = EAccelerationStructureBuildMode::Update;
	Params.Geometry = RayTracingGeometry->RayTracingGeometryRHI;
	Params.Segments = RayTracingGeometry->Initializer.Segments;

	RHICmdList.BuildAccelerationStructures(MakeArrayView(&Params, 1));
}

static void BuildHairAccelerationStructure(FRHICommandList& RHICmdList, uint32 RaytracingVertexCount, FVertexBufferRHIRef& PositionBuffer, FRayTracingGeometry* OutRayTracingGeometry)
{
	FRayTracingGeometryInitializer Initializer;
	Initializer.IndexBuffer = nullptr;
	Initializer.IndexBufferOffset = 0;
	Initializer.GeometryType = RTGT_Triangles;
	Initializer.TotalPrimitiveCount = RaytracingVertexCount / 3;
	Initializer.bFastBuild = true;
	Initializer.bAllowUpdate = true;

	FRayTracingGeometrySegment Segment;
	Segment.VertexBuffer = PositionBuffer;
	Segment.VertexBufferStride = FHairStrandsRaytracingFormat::SizeInByte;
	Segment.VertexBufferElementType = FHairStrandsRaytracingFormat::VertexElementType;
	Segment.NumPrimitives = RaytracingVertexCount / 3;
	Initializer.Segments.Add(Segment);

	OutRayTracingGeometry->SetInitializer(Initializer);
	OutRayTracingGeometry->RayTracingGeometryRHI = RHICreateRayTracingGeometry(Initializer);
	RHICmdList.BuildAccelerationStructure(OutRayTracingGeometry->RayTracingGeometryRHI);
}
#endif // RHI_RAYTRACING

void ComputeHairStrandsInterpolation(
	FRHICommandListImmediate& RHICmdList,
	const FShaderDrawDebugData* ShaderDrawData,
	const FTransform& LocalToWorld,
	FHairStrandsInterpolationInput* InInput,
	FHairStrandsInterpolationOutput* InOutput,
	FHairStrandsProjectionHairData& InRenHairDatas,
	FHairStrandsProjectionHairData& InSimHairDatas,
	int32 LODIndex,
	FHairStrandClusterData* ClusterData)
{
	// Note: We are breaking this code up into several, larger for loops. In the previous version, the typical code path was:
	// for each group:
	//     AddClearClusterAABBPass()
	//     AddHairStrandsInterpolationPass()
	//     AddHairClusterAABBPass()
	//     AddHairTangentPass()
	//     AddGenerateRaytracingGeometryPass()
	// 
	// The problem is that it creates bubbles in the GPU, since each pass was dependent on the previous one. So it has been
	// modified to be of the form:
	//
	// for each group:
	//     AddClearClusterAABBPass()
	// for each group:
	//     AddHairStrandsInterpolationPass()
	// ...

	if (!InInput || !InOutput) return;

	// The previous loop would return if both Input and Output were not valid. Instead, count the number
	// of valid groups first.
	const uint32 ExpectedGroupCount = InOutput->HairGroups.Num();
	uint32 GroupCount = ExpectedGroupCount;
	for (uint32 GroupIndex = 0; GroupIndex < ExpectedGroupCount; ++GroupIndex)
	{
		FHairStrandsInterpolationInput::FHairGroup& Input  = InInput->HairGroups[GroupIndex];
		FHairStrandsInterpolationOutput::HairGroup& Output = InOutput->HairGroups[GroupIndex];
		Output.VFInput.Reset();

		if (!Input.IsValid() || !Output.IsValid())
		{
			GroupCount = FMath::Min(ExpectedGroupCount, GroupIndex);
			break;
		}
	}

	DECLARE_GPU_STAT(HairStrandsInterpolationCluster);
	SCOPED_DRAW_EVENT(RHICmdList, HairStrandsInterpolationCluster);
	SCOPED_GPU_STAT(RHICmdList, HairStrandsInterpolationCluster);

	const EDeformationType DeformationType = GetDeformationType();

	// Debug mode:
	// * None	: Display hair normally
	// * Sim	: Show sim strands
	// * Render : Show rendering strands with sim color influence
	const EHairStrandsDebugMode DebugMode = GetHairStrandsDebugStrandsMode();
	const bool bDebugModePatchedAttributeBuffer = DebugMode == EHairStrandsDebugMode::RenderHairStrands || DebugMode == EHairStrandsDebugMode::RenderVisCluster;

	if (DeformationType != EDeformationType::RestStrands && DeformationType != EDeformationType::Simulation)
	{
		FBufferTransitionQueue TransitionQueue;
		FRDGBuilder GraphBuilder(RHICmdList);
		for (uint32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
		{
			FHairStrandsInterpolationInput::FHairGroup& Input = InInput->HairGroups[GroupIndex];
			FHairStrandsInterpolationOutput::HairGroup& Output = InOutput->HairGroups[GroupIndex];

			const uint32 CurrIndex = *Output.CurrentIndex;
			const uint32 PrevIndex = (CurrIndex + 1) % 2;

			AddDeformSimHairStrandsPass(
				GraphBuilder,
				DeformationType,
				Input.SimVertexCount,
				LODIndex,
				InSimHairDatas.HairGroups[GroupIndex],
				Input.SimRestPosePositionBuffer->SRV,
				Input.SimRootPointIndexBuffer ? Input.SimRootPointIndexBuffer->SRV : nullptr,
				Output.SimDeformedPositionBuffer[CurrIndex]->UAV, Input.InSimHairPositionOffset,
				Input.OutHairPositionOffset,
				TransitionQueue,
				Input.bHasGlobalInterpolation);
		}
		GraphBuilder.Execute();
		TransitBufferToReadable(RHICmdList, TransitionQueue);
	}

	if (DebugMode == EHairStrandsDebugMode::SimHairStrands)
	{
		FBufferTransitionQueue TransitionQueue;
		FRDGBuilder GraphBuilder(RHICmdList);
		for (uint32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
		{
			FHairStrandsInterpolationInput::FHairGroup& Input = InInput->HairGroups[GroupIndex];
			FHairStrandsInterpolationOutput::HairGroup& Output = InOutput->HairGroups[GroupIndex];

			const uint32 CurrIndex = *Output.CurrentIndex;
			const uint32 PrevIndex = (CurrIndex + 1) % 2;

			AddHairTangentPass(
				GraphBuilder,
				Input.SimVertexCount,
				Output.SimDeformedPositionBuffer[CurrIndex]->SRV,
				Output.SimTangentBuffer->UAV,
				TransitionQueue);

			const bool bHasSimulationEnabled = Input.bIsSimulationEnable && GHairStrandsInterpolateSimulation && DeformationType != EDeformationType::RestStrands;

			Output.VFInput.HairPositionBuffer = Output.SimDeformedPositionBuffer[CurrIndex]->SRV;
			Output.VFInput.HairPreviousPositionBuffer = Output.SimDeformedPositionBuffer[CurrIndex]->SRV;
			Output.VFInput.HairTangentBuffer = Output.SimTangentBuffer->SRV;
			Output.VFInput.HairAttributeBuffer = Input.SimAttributeBuffer->SRV;
			Output.VFInput.HairMaterialBuffer = Output.RenderMaterialBuffer->SRV;
			Output.VFInput.HairPositionOffset = Input.OutHairPositionOffset;// bHasSimulationEnabled ? Input.OutHairPositionOffset : Input.InSimHairPositionOffset;
			Output.VFInput.HairPreviousPositionOffset = Input.OutHairPreviousPositionOffset;// bHasSimulationEnabled ? Input.OutHairPreviousPositionOffset : Input.InSimHairPositionOffset;
			Output.VFInput.VertexCount = Input.SimVertexCount;
			Output.VFInput.HairRadius = (GStrandHairWidth > 0 ? GStrandHairWidth : Input.GroupDesc.HairWidth) * 0.5f;
			Output.VFInput.HairLength = Input.GroupDesc.HairLength;
			Output.VFInput.HairDensity = Input.GroupDesc.HairShadowDensity;
			Output.VFInput.bUseStableRasterization = Input.GroupDesc.bUseStableRasterization;
			Output.VFInput.bScatterSceneLighting = Input.GroupDesc.bScatterSceneLighting;
		}
		GraphBuilder.Execute();
		TransitBufferToReadable(RHICmdList, TransitionQueue);
	}
	else
	{
		{
			FBufferTransitionQueue TransitionQueue;
			FRDGBuilder GraphBuilder(RHICmdList);
			for (uint32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
			{
				FHairStrandsInterpolationInput::FHairGroup& Input = InInput->HairGroups[GroupIndex];
				FHairStrandsInterpolationOutput::HairGroup& Output = InOutput->HairGroups[GroupIndex];

				const uint32 CurrIndex = *Output.CurrentIndex;
				const uint32 PrevIndex = (CurrIndex + 1) % 2;

				// If the deformation is driven by the physics simulation, then the output is always the 0 index
				const uint32 SimIndex = CurrIndex;// GHairDeformationType == 0 ? 0 : CurrIndex;


				check(ClusterData);

				const uint32 BufferSizeInBytes = Input.RenderVertexCount * FHairStrandsAttributeFormat::SizeInByte;
				if (bDebugModePatchedAttributeBuffer && Output.RenderPatchedAttributeBuffer.NumBytes != BufferSizeInBytes)
				{
					Output.RenderPatchedAttributeBuffer.Release();
					Output.RenderPatchedAttributeBuffer.Initialize(FHairStrandsAttributeFormat::SizeInByte, Input.RenderVertexCount, FHairStrandsAttributeFormat::Format, BUF_Static);
				}

				AddClearClusterAABBPass(
					GraphBuilder,
					Input.ClusterCount,
					Output.RenderClusterAABBBuffer->UAV,
					Output.RenderGroupAABBBuffer->UAV,
					TransitionQueue);
			}
			GraphBuilder.Execute();
			TransitBufferToReadable(RHICmdList, TransitionQueue);
		}

		{
			FBufferTransitionQueue TransitionQueue;
			FRDGBuilder GraphBuilder(RHICmdList);
			for (uint32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
			{
				FHairStrandsInterpolationInput::FHairGroup& Input = InInput->HairGroups[GroupIndex];
				FHairStrandsInterpolationOutput::HairGroup& Output = InOutput->HairGroups[GroupIndex];

				const uint32 CurrIndex = *Output.CurrentIndex;
				const uint32 PrevIndex = (CurrIndex + 1) % 2;

				// If the deformation is driven by the physics simulation, then the output is always the 0 index
				const uint32 SimIndex = CurrIndex;// GHairDeformationType == 0 ? 0 : CurrIndex;

				FHairScaleAndClipDesc ScaleAndClipDesc;
				ScaleAndClipDesc.InHairLength = Input.GroupDesc.HairLength;
				ScaleAndClipDesc.InHairRadius = Input.GroupDesc.HairWidth * 0.5f;
				ScaleAndClipDesc.OutHairRadius = (GStrandHairWidth > 0 ? GStrandHairWidth : Input.GroupDesc.HairWidth) * 0.5f;
				ScaleAndClipDesc.MaxOutHairRadius = ScaleAndClipDesc.OutHairRadius * FMath::Max(1.f, FMath::Max(Input.GroupDesc.HairRootScale, Input.GroupDesc.HairTipScale));
				ScaleAndClipDesc.HairRadiusRootScale = Input.GroupDesc.HairRootScale;
				ScaleAndClipDesc.HairRadiusTipScale = Input.GroupDesc.HairTipScale;
				ScaleAndClipDesc.HairLengthClip = Input.GroupDesc.HairClipLength;

				const bool bHasSimulationEnabled = Input.bIsSimulationEnable && GHairStrandsInterpolateSimulation && DeformationType != EDeformationType::RestStrands;
				check(GroupIndex < uint32(InRenHairDatas.HairGroups.Num()));
				check(GroupIndex < uint32(InSimHairDatas.HairGroups.Num()));
				AddHairStrandsInterpolationPass(
					GraphBuilder,
					ShaderDrawData,
					InRenHairDatas.HairGroups[GroupIndex],
					InSimHairDatas.HairGroups[GroupIndex],
					Input.InRenderHairPositionOffset,
					Input.InSimHairPositionOffset,
					Input.OutHairPositionOffset,
					ScaleAndClipDesc,
					LODIndex,
					bHasSimulationEnabled,
					bDebugModePatchedAttributeBuffer,
					Input.RenderVertexCount,
					Input.RenderRestPosePositionBuffer->SRV,
					Input.RenderAttributeBuffer->SRV,
					Input.Interpolation0Buffer->SRV,
					Input.Interpolation1Buffer->SRV,
					Input.SimRestPosePositionBuffer->SRV,
					Output.SimDeformedPositionBuffer[SimIndex]->SRV,
					Input.SimAttributeBuffer->SRV,
					Output.RenderDeformedPositionBuffer[CurrIndex]->UAV,
					Output.RenderPatchedAttributeBuffer.UAV,
					Input.VertexToClusterIdBuffer->SRV,
					Input.SimRootPointIndexBuffer->SRV,
					TransitionQueue,
					Input.bHasGlobalInterpolation,
					Input.HairInterpolationType);

			}
			GraphBuilder.Execute();
			TransitBufferToReadable(RHICmdList, TransitionQueue);
		}

		{		
			FBufferTransitionQueue TransitionQueue;
			FRDGBuilder GraphBuilder(RHICmdList);
			for (uint32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
			{
				FHairStrandsInterpolationInput::FHairGroup& Input = InInput->HairGroups[GroupIndex];
				FHairStrandsInterpolationOutput::HairGroup& Output = InOutput->HairGroups[GroupIndex];

				const uint32 CurrIndex = *Output.CurrentIndex;
				const uint32 PrevIndex = (CurrIndex + 1) % 2;

				// If the deformation is driven by the physics simulation, then the output is always the 0 index
				const uint32 SimIndex = CurrIndex;// GHairDeformationType == 0 ? 0 : CurrIndex;

				// Initialize group cluster data for culling by the renderer
				FHairStrandClusterData::FHairGroup& HairGroupCluster = ClusterData->HairGroups.Emplace_GetRef();
				HairGroupCluster.ClusterCount = Input.ClusterCount;
				HairGroupCluster.VertexCount = Input.ClusterVertexCount;
				HairGroupCluster.GroupAABBBuffer = Output.RenderGroupAABBBuffer;
				HairGroupCluster.ClusterAABBBuffer = Output.RenderClusterAABBBuffer;
				HairGroupCluster.ClusterInfoBuffer = Output.ClusterInfoBuffer;
				HairGroupCluster.VertexToClusterIdBuffer = Input.VertexToClusterIdBuffer;
				HairGroupCluster.ClusterVertexIdBuffer = Input.ClusterVertexIdBuffer;
				HairGroupCluster.ClusterIndexRadiusScaleInfoBuffer = Input.ClusterIndexRadiusScaleInfoBuffer;
				HairGroupCluster.HairGroupPublicPtr = Output.HairGroupPublicData;
				HairGroupCluster.LodBias = Input.GroupDesc.LodBias;
				HairGroupCluster.LodAverageVertexPerPixel = Input.GroupDesc.LodAverageVertexPerPixel;

				// Note: This code needs to exactly match the values FHairScaleAndClipDesc set int the previous loop.
				const float OutHairRadius = (GStrandHairWidth > 0 ? GStrandHairWidth : Input.GroupDesc.HairWidth) * 0.5f;
				const float MaxOutHairRadius = OutHairRadius * FMath::Max(1.f, FMath::Max(Input.GroupDesc.HairRootScale, Input.GroupDesc.HairTipScale));

				Output.VFInput.HairRadius = MaxOutHairRadius;
				Output.VFInput.HairLength = Input.GroupDesc.HairLength;
				Output.VFInput.HairDensity = Input.GroupDesc.HairShadowDensity;
				Output.VFInput.HairPositionBuffer = Output.RenderDeformedPositionBuffer[CurrIndex]->SRV;
				Output.VFInput.HairPreviousPositionBuffer = Output.RenderDeformedPositionBuffer[PrevIndex]->SRV;
				Output.VFInput.bUseStableRasterization = Input.GroupDesc.bUseStableRasterization;
				Output.VFInput.bScatterSceneLighting = Input.GroupDesc.bScatterSceneLighting;

				AddHairClusterAABBPass(
					GraphBuilder,
					InRenHairDatas.HairGroups[GroupIndex],
					Input.OutHairPositionOffset,
					HairGroupCluster,
					Output.RenderDeformedPositionBuffer[CurrIndex]->SRV,
					TransitionQueue);
			}
			GraphBuilder.Execute();
			TransitBufferToReadable(RHICmdList, TransitionQueue);
		}

		{
			FBufferTransitionQueue TransitionQueue;
			FRDGBuilder GraphBuilder(RHICmdList);
			for (uint32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
			{
				FHairStrandsInterpolationInput::FHairGroup& Input = InInput->HairGroups[GroupIndex];
				FHairStrandsInterpolationOutput::HairGroup& Output = InOutput->HairGroups[GroupIndex];

				const uint32 CurrIndex = *Output.CurrentIndex;
				const uint32 PrevIndex = (CurrIndex + 1) % 2;

				AddHairTangentPass(
					GraphBuilder,
					Input.RenderVertexCount,
					Output.VFInput.HairPositionBuffer,
					Output.RenderTangentBuffer->UAV,
					TransitionQueue);
			}
			GraphBuilder.Execute();
			TransitBufferToReadable(RHICmdList, TransitionQueue);
		}

		{
			FBufferTransitionQueue TransitionQueue;
			FRDGBuilder GraphBuilder(RHICmdList);
			for (uint32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
			{
				FHairStrandsInterpolationInput::FHairGroup& Input = InInput->HairGroups[GroupIndex];
				FHairStrandsInterpolationOutput::HairGroup& Output = InOutput->HairGroups[GroupIndex];

				const uint32 CurrIndex = *Output.CurrentIndex;
				const uint32 PrevIndex = (CurrIndex + 1) % 2;

				#if RHI_RAYTRACING
				if (IsHairRayTracingEnabled())
				{
					const float HairRadiusScaleRT = (GHairRaytracingRadiusScale > 0 ? GHairRaytracingRadiusScale : Input.GroupDesc.HairRaytracingRadiusScale);
					AddGenerateRaytracingGeometryPass(
						GraphBuilder,
						Input.RenderVertexCount,
						Output.VFInput.HairRadius * HairRadiusScaleRT,
						Input.OutHairPositionOffset,
						Output.VFInput.HairPositionBuffer,
						Input.RaytracingPositionBuffer->UAV,
						TransitionQueue);
				}
				#endif

				Output.VFInput.HairTangentBuffer = Output.RenderTangentBuffer->SRV;
				Output.VFInput.HairAttributeBuffer = bDebugModePatchedAttributeBuffer ? Output.RenderPatchedAttributeBuffer.SRV : Input.RenderAttributeBuffer->SRV;
				Output.VFInput.HairMaterialBuffer = Output.RenderMaterialBuffer->SRV;
				Output.VFInput.HairPositionOffset = Input.OutHairPositionOffset;
				Output.VFInput.HairPreviousPositionOffset = Input.OutHairPreviousPositionOffset;
				Output.VFInput.VertexCount = Input.RenderVertexCount;
			}
			GraphBuilder.Execute();
			TransitBufferToReadable(RHICmdList, TransitionQueue);
		}

		{
			FRDGBuilder GraphBuilder(RHICmdList);
			for (uint32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
			{
				FHairStrandsInterpolationInput::FHairGroup& Input = InInput->HairGroups[GroupIndex];
				FHairStrandsInterpolationOutput::HairGroup& Output = InOutput->HairGroups[GroupIndex];

				const uint32 CurrIndex = *Output.CurrentIndex;
				const uint32 PrevIndex = (CurrIndex + 1) % 2;

				// TODO: find a more robust way to handle parameters passing to compute raster.
				// At the moment there is a loose compling which will break if the vertex factor change.
				Output.HairGroupPublicData->VFInput.HairPositionBuffer = Output.VFInput.HairPositionBuffer;
				Output.HairGroupPublicData->VFInput.HairPositionOffset = Output.VFInput.HairPositionOffset;
				Output.HairGroupPublicData->VFInput.VertexCount = Output.VFInput.VertexCount;
				Output.HairGroupPublicData->VFInput.HairRadius = Output.VFInput.HairRadius;
				Output.HairGroupPublicData->VFInput.HairLength = Output.VFInput.HairLength;
				Output.HairGroupPublicData->VFInput.bUseStableRasterization = Output.VFInput.bUseStableRasterization;
				Output.HairGroupPublicData->VFInput.bScatterSceneLighting = Output.VFInput.bScatterSceneLighting;
				Output.HairGroupPublicData->VFInput.HairDensity = Output.VFInput.HairDensity;
				Output.HairGroupPublicData->VFInput.LocalToWorldTransform = LocalToWorld;

	#if RHI_RAYTRACING
				if (IsHairRayTracingEnabled())
				{
					FRHIUnorderedAccessView* UAV = Input.RaytracingPositionBuffer->UAV;
					RHICmdList.TransitionResources(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, &UAV, 1);

					const bool bNeedFullBuild = !Input.bIsRTGeometryInitialized;
					if (bNeedFullBuild)
						BuildHairAccelerationStructure(RHICmdList, Input.RaytracingVertexCount, Input.RaytracingPositionBuffer->Buffer, Input.RaytracingGeometry);
					else
						UpdateHairAccelerationStructure(RHICmdList, Input.RaytracingGeometry);
					Input.bIsRTGeometryInitialized = true;
				}
	#endif
			}
			GraphBuilder.Execute();
		}
	}

	// update the current index
	for (uint32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
	{
		FHairStrandsInterpolationInput::FHairGroup& Input  = InInput->HairGroups[GroupIndex];
		FHairStrandsInterpolationOutput::HairGroup& Output = InOutput->HairGroups[GroupIndex];

		const uint32 CurrIndex = *Output.CurrentIndex;
		const uint32 PrevIndex = (CurrIndex + 1) % 2;

		*Output.CurrentIndex = PrevIndex;
	}
}

void ResetHairStrandsInterpolation(
	FRHICommandListImmediate& RHICmdList,
	FHairStrandsInterpolationInput* InInput,
	FHairStrandsInterpolationOutput* InOutput,
	FHairStrandsProjectionHairData& InSimHairDatas,
	int32 LODIndex)
{
	if (!InInput || !InOutput) return;

	const uint32 GroupCount = InOutput->HairGroups.Num();
	for (uint32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
	{
		FHairStrandsInterpolationInput::FHairGroup& Input = InInput->HairGroups[GroupIndex];
		FHairStrandsInterpolationOutput::HairGroup& Output = InOutput->HairGroups[GroupIndex];
		if (!Input.IsValid() || !Output.IsValid()) return;

		if (!Input.bIsSimulationEnable)
		{
			DECLARE_GPU_STAT(HairStrandsResetInterpolation);
			SCOPED_DRAW_EVENT(RHICmdList, HairStrandsResetInterpolation);
			SCOPED_GPU_STAT(RHICmdList, HairStrandsResetInterpolation);

			const uint32 CurrIndex = *Output.CurrentIndex;
			const uint32 PrevIndex = (CurrIndex + 1) % 2;

			FBufferTransitionQueue TransitionQueue;
			FRDGBuilder GraphBuilder(RHICmdList);
			AddDeformSimHairStrandsPass(
				GraphBuilder,
				EDeformationType::OffsetGuide,
				Input.SimVertexCount,
				LODIndex,
				InSimHairDatas.HairGroups[GroupIndex],
				Input.SimRestPosePositionBuffer->SRV,
				Input.SimRootPointIndexBuffer ? Input.SimRootPointIndexBuffer->SRV : nullptr,
				Output.SimDeformedPositionBuffer[CurrIndex]->UAV, Input.InSimHairPositionOffset,
				Input.OutHairPositionOffset, 
				TransitionQueue,
				Input.bHasGlobalInterpolation);
			GraphBuilder.Execute();
			TransitBufferToReadable(RHICmdList, TransitionQueue);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////

template<typename ReadBackType>
void ReadbackBuffer(TArray<ReadBackType>& OutData, FRWBuffer& InBuffer)
{
	ReadBackType* Data = (ReadBackType*)RHILockVertexBuffer(InBuffer.Buffer, 0, InBuffer.Buffer->GetSize(), RLM_ReadOnly);
	const uint32 ElementCount = InBuffer.Buffer->GetSize() / sizeof(ReadBackType);
	OutData.SetNum(ElementCount);
	for (uint32 ElementIt = 0; ElementIt < ElementCount; ++ElementIt)
	{
		OutData[ElementIt] = Data[ElementIt];
	}
	RHIUnlockVertexBuffer(InBuffer.Buffer);
}

template<typename WriteBackType>
void WritebackBuffer(TArray<WriteBackType>& InData, FRWBuffer& OutBuffer)
{
	const uint32 DataSize = sizeof(WriteBackType) * InData.Num();
	check(DataSize == OutBuffer.Buffer->GetSize() );

	WriteBackType* Data = (WriteBackType*)RHILockVertexBuffer(OutBuffer.Buffer, 0, DataSize, RLM_WriteOnly);
	FMemory::Memcpy(Data, InData.GetData(), DataSize);
	RHIUnlockVertexBuffer(OutBuffer.Buffer);
}

static void ReadbackGroupData(
	FHairStrandsRootData& OutCPUData,
	FHairStrandsRestRootResource* InGPUData)
{
	if (!InGPUData)
	{
		return;
	}

	check(InGPUData->MeshProjectionLODs.Num() == OutCPUData.MeshProjectionLODs.Num());

	const uint32 LODCount = InGPUData->MeshProjectionLODs.Num();
	for (uint32 LODIt = 0; LODIt < LODCount; ++LODIt)
	{
		FHairStrandsRootData::FMeshProjectionLOD& CPULOD = OutCPUData.MeshProjectionLODs[LODIt];
		FHairStrandsRestRootResource::FMeshProjectionLOD& GPULOD = InGPUData->MeshProjectionLODs[LODIt];
		check(CPULOD.LODIndex == GPULOD.LODIndex);

		ReadbackBuffer(CPULOD.RootTriangleIndexBuffer, GPULOD.RootTriangleIndexBuffer);
		ReadbackBuffer(CPULOD.RootTriangleBarycentricBuffer, GPULOD.RootTriangleBarycentricBuffer);
		ReadbackBuffer(CPULOD.RestRootTrianglePosition0Buffer, GPULOD.RestRootTrianglePosition0Buffer);
		ReadbackBuffer(CPULOD.RestRootTrianglePosition1Buffer, GPULOD.RestRootTrianglePosition1Buffer);
		ReadbackBuffer(CPULOD.RestRootTrianglePosition2Buffer, GPULOD.RestRootTrianglePosition2Buffer);

		InGPUData->RootData.MeshProjectionLODs[LODIt].RootTriangleIndexBuffer = CPULOD.RootTriangleIndexBuffer;
		InGPUData->RootData.MeshProjectionLODs[LODIt].RootTriangleBarycentricBuffer = CPULOD.RootTriangleBarycentricBuffer;
		InGPUData->RootData.MeshProjectionLODs[LODIt].RestRootTrianglePosition0Buffer = CPULOD.RestRootTrianglePosition0Buffer;
		InGPUData->RootData.MeshProjectionLODs[LODIt].RestRootTrianglePosition1Buffer = CPULOD.RestRootTrianglePosition1Buffer;
		InGPUData->RootData.MeshProjectionLODs[LODIt].RestRootTrianglePosition2Buffer = CPULOD.RestRootTrianglePosition2Buffer;
	}
}

void WritebackGroupData(FHairStrandsRootData& InCPUData, FHairStrandsRestRootResource* OutGPUData)
{
	if (!OutGPUData)
	{
		return;
	}
	check(OutGPUData->MeshProjectionLODs.Num() == InCPUData.MeshProjectionLODs.Num());
	const uint32 LODCount = OutGPUData->MeshProjectionLODs.Num();
	for (uint32 LODIt = 0; LODIt < LODCount; ++LODIt)
	{
		FHairStrandsRootData::FMeshProjectionLOD& CPULOD = InCPUData.MeshProjectionLODs[LODIt];
		FHairStrandsRestRootResource::FMeshProjectionLOD& GPULOD = OutGPUData->MeshProjectionLODs[LODIt];
		check(CPULOD.LODIndex == GPULOD.LODIndex);

		if (CPULOD.SampleCount > 0)
		{
			WritebackBuffer(CPULOD.MeshInterpolationWeightsBuffer, GPULOD.MeshInterpolationWeightsBuffer);
			WritebackBuffer(CPULOD.MeshSampleIndicesBuffer, GPULOD.MeshSampleIndicesBuffer);
			WritebackBuffer(CPULOD.RestSamplePositionsBuffer, GPULOD.RestSamplePositionsBuffer);

			OutGPUData->RootData.MeshProjectionLODs[LODIt].SampleCount = CPULOD.SampleCount;
			OutGPUData->RootData.MeshProjectionLODs[LODIt].MeshInterpolationWeightsBuffer = CPULOD.MeshInterpolationWeightsBuffer;
			OutGPUData->RootData.MeshProjectionLODs[LODIt].RestSamplePositionsBuffer = CPULOD.RestSamplePositionsBuffer;
			OutGPUData->RootData.MeshProjectionLODs[LODIt].MeshSampleIndicesBuffer = CPULOD.MeshSampleIndicesBuffer;
		}
	}
}

struct FPointsSampler
{
	FPointsSampler(TArray<bool>& ValidPoints, const FVector* PointPositions, const int32 NumSamples);

	/** Build the sample position from the sample indices */
	void BuildPositions(const FVector* PointPositions);

	/** Compute the furthest point */
	void FurthestPoint(const int32 NumPoints, const FVector* PointPositions, const uint32 SampleIndex, TArray<bool>& ValidPoints, TArray<float>& PointsDistance);

	/** Compute the starting point */
	int32 StartingPoint(const TArray<bool>& ValidPoints, int32& NumPoints) const;

	/** List of sampled points */
	TArray<uint32> SampleIndices;

	/** List of sampled positions */
	TArray<FVector> SamplePositions;
};

int32 FPointsSampler::StartingPoint(const TArray<bool>& ValidPoints, int32& NumPoints) const
{
	int32 StartIndex = -1;
	NumPoints = 0;
	for (int32 i = 0; i < ValidPoints.Num(); ++i)
	{
		if (ValidPoints[i])
		{
			++NumPoints;
			if (StartIndex == -1)
			{
				StartIndex = i;
			}
		}
	}
	return StartIndex;
}

void FPointsSampler::BuildPositions(const FVector* PointPositions)
{
	SamplePositions.SetNum(SampleIndices.Num());
	for (int32 i = 0; i < SampleIndices.Num(); ++i)
	{
		SamplePositions[i] = PointPositions[SampleIndices[i]];
	}
}

void FPointsSampler::FurthestPoint(const int32 NumPoints, const FVector* PointPositions, const uint32 SampleIndex, TArray<bool>& ValidPoints, TArray<float>& PointsDistance)
{
	float FurthestDistance = 0.0;
	uint32 PointIndex = 0;
	for (int32 j = 0; j < NumPoints; ++j)
	{
		if (ValidPoints[j])
		{
			PointsDistance[j] = FMath::Min((PointPositions[SampleIndices[SampleIndex - 1]] - PointPositions[j]).Size(), PointsDistance[j]);
			if (PointsDistance[j] >= FurthestDistance)
			{
				PointIndex = j;
				FurthestDistance = PointsDistance[j];
			}
		}
	}
	ValidPoints[PointIndex] = false;
	SampleIndices[SampleIndex] = PointIndex;
}

FPointsSampler::FPointsSampler(TArray<bool>& ValidPoints, const FVector* PointPositions, const int32 NumSamples)
{
	int32 NumPoints = 0;
	int32 StartIndex = StartingPoint(ValidPoints, NumPoints);

	const int32 SamplesCount = FMath::Min(NumPoints, NumSamples);
	if (SamplesCount != 0)
	{
		SampleIndices.SetNum(SamplesCount);
		SampleIndices[0] = StartIndex;
		ValidPoints[StartIndex] = false;

		TArray<float> PointsDistance;
		PointsDistance.Init(MAX_FLT, ValidPoints.Num());

		for (int32 i = 1; i < SamplesCount; ++i)
		{
			FurthestPoint(ValidPoints.Num(), PointPositions, i, ValidPoints, PointsDistance);
		}
		BuildPositions(PointPositions);
	}
}

struct FWeightsBuilder
{
	FWeightsBuilder(const uint32 NumRows, const uint32 NumColumns,
		const FVector* SourcePositions, const FVector* TargetPositions);

	using EigenMatrix = Eigen::Map<Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>;

	/** Compute the weights by inverting the matrix*/
	void ComputeWeights(const uint32 NumRows, const uint32 NumColumns);

	/** Entries in the dense structure */
	TArray<float> MatrixEntries;

	/** Entries of the matrix inverse */
	TArray<float> InverseEntries;
};

FWeightsBuilder::FWeightsBuilder(const uint32 NumRows, const uint32 NumColumns,
	const FVector* SourcePositions, const FVector* TargetPositions)
{
	const uint32 PolyRows = NumRows + 4;
	const uint32 PolyColumns = NumColumns + 4;

	MatrixEntries.Init(0.0, PolyRows * PolyColumns);
	InverseEntries.Init(0.0, PolyRows * PolyColumns);
	TArray<float>& LocalEntries = MatrixEntries;
	ParallelFor(NumRows,
		[
			NumRows,
			NumColumns,
			PolyRows,
			PolyColumns,
			SourcePositions,
			TargetPositions,
			&LocalEntries
		] (uint32 RowIndex)
	{
		int32 EntryIndex = RowIndex * PolyColumns;
		for (uint32 j = 0; j < NumColumns; ++j)
		{
			const float FunctionScale = (SourcePositions[RowIndex] - TargetPositions[j]).Size();
			LocalEntries[EntryIndex++] = FMath::Sqrt(FunctionScale*FunctionScale + 1.0);
		}
		LocalEntries[EntryIndex++] = 1.0;
		LocalEntries[EntryIndex++] = SourcePositions[RowIndex].X;
		LocalEntries[EntryIndex++] = SourcePositions[RowIndex].Y;
		LocalEntries[EntryIndex++] = SourcePositions[RowIndex].Z;

		EntryIndex = NumRows* PolyColumns + RowIndex;
		LocalEntries[EntryIndex] = 1.0;

		EntryIndex += PolyColumns;
		LocalEntries[EntryIndex] = SourcePositions[RowIndex].X;

		EntryIndex += PolyColumns;
		LocalEntries[EntryIndex] = SourcePositions[RowIndex].Y;

		EntryIndex += PolyColumns;
		LocalEntries[EntryIndex] = SourcePositions[RowIndex].Z;

		const float REGUL_VALUE = 1e-4;
		EntryIndex = NumRows * PolyColumns + NumColumns;
		LocalEntries[EntryIndex] = REGUL_VALUE;

		EntryIndex += PolyColumns+1;
		LocalEntries[EntryIndex] = REGUL_VALUE;

		EntryIndex += PolyColumns+1;
		LocalEntries[EntryIndex] = REGUL_VALUE;

		EntryIndex += PolyColumns+1;
		LocalEntries[EntryIndex] = REGUL_VALUE;

	});
	ComputeWeights(PolyRows, PolyColumns);
}

void FWeightsBuilder::ComputeWeights(const uint32 NumRows, const uint32 NumColumns)
{
	EigenMatrix WeightsMatrix(MatrixEntries.GetData(), NumRows, NumColumns);
	EigenMatrix WeightsInverse(InverseEntries.GetData(), NumColumns, NumRows);

	WeightsInverse = WeightsMatrix.inverse();
}

void UpdateInterpolationWeights(const FWeightsBuilder& InterpolationWeights, const FPointsSampler& PointsSampler, const uint32 LODIndex, FHairStrandsRootData& RootDatas)
{
	FHairStrandsRootData::FMeshProjectionLOD& CPULOD = RootDatas.MeshProjectionLODs[LODIndex];
	CPULOD.MeshSampleIndicesBuffer.SetNum(PointsSampler.SampleIndices.Num());
	CPULOD.MeshInterpolationWeightsBuffer.SetNum(InterpolationWeights.InverseEntries.Num());
	CPULOD.RestSamplePositionsBuffer.SetNum(PointsSampler.SampleIndices.Num());

	CPULOD.SampleCount = PointsSampler.SampleIndices.Num();
	CPULOD.MeshSampleIndicesBuffer = PointsSampler.SampleIndices;
	CPULOD.MeshInterpolationWeightsBuffer = InterpolationWeights.InverseEntries;
	for (int32 i = 0; i < PointsSampler.SamplePositions.Num(); ++i)
	{
		CPULOD.RestSamplePositionsBuffer[i] = FVector4(PointsSampler.SamplePositions[i], 1.0f );
	}
}

static void InternalProcessGroomBindingTask(FRHICommandListImmediate& RHICmdList, void* Asset)
{
	UGroomBindingAsset* BindingAsset = (UGroomBindingAsset*)Asset;
	if (!BindingAsset ||
		!BindingAsset->Groom ||
		!BindingAsset->TargetSkeletalMesh ||
		 BindingAsset->Groom->GetNumHairGroups() == 0)
	{
		UE_LOG(LogHairStrands, Warning, TEXT("[Groom] Error - Binding asset can be created/rebuilt."));
		return;
	}

	const int32 NumInterpolationPoints	= BindingAsset->NumInterpolationPoints;
	UGroomAsset* GroomAsset				= BindingAsset->Groom;
	USkeletalMesh* SourceSkeletalMesh	= BindingAsset->SourceSkeletalMesh;
	USkeletalMesh* TargetSkeletalMesh	= BindingAsset->TargetSkeletalMesh;

	const uint32 LODCount = BindingAsset->TargetSkeletalMesh->GetLODNum();
	UGroomBindingAsset::FHairGroupDatas& OutHairGroupDatas = BindingAsset->HairGroupDatas;
	OutHairGroupDatas.Empty();
	TArray<uint32> NumSamples;
	NumSamples.Init(NumInterpolationPoints, LODCount);
	for (const FHairGroupData& GroupData : GroomAsset->HairGroupsData)
	{
		UGroomBindingAsset::FHairGroupData& Data = OutHairGroupDatas.AddDefaulted_GetRef();
		Data.RenRootData = FHairStrandsRootData(&GroupData.HairRenderData, LODCount, NumSamples);
		Data.SimRootData = FHairStrandsRootData(&GroupData.HairSimulationData, LODCount, NumSamples);
	}

	UGroomBindingAsset::FHairGroupResources& OutHairGroupResources = BindingAsset->HairGroupResources;
	if (BindingAsset->HairGroupResources.Num() > 0)
	{
		for (UGroomBindingAsset::FHairGroupResource& GroupResrouces : OutHairGroupResources)
		{
			BindingAsset->HairGroupResourcesToDelete.Enqueue(GroupResrouces);
		}
		OutHairGroupResources.Empty();
	}

	check(OutHairGroupResources.Num() == 0);
	for (UGroomBindingAsset::FHairGroupData& GroupData : OutHairGroupDatas)
	{
		UGroomBindingAsset::FHairGroupResource& Resource = OutHairGroupResources.AddDefaulted_GetRef();
		Resource.SimRootResources = new FHairStrandsRestRootResource(GroupData.SimRootData);
		Resource.RenRootResources = new FHairStrandsRestRootResource(GroupData.RenRootData);

		Resource.SimRootResources->InitRHI();
		Resource.RenRootResources->InitRHI();
	}

	TArray<FGoomBindingGroupInfo>& OutGroupInfos = BindingAsset->GroupInfos;
	OutGroupInfos.Empty();
	for (const UGroomBindingAsset::FHairGroupData& Data : OutHairGroupDatas)
	{
		FGoomBindingGroupInfo& Info = OutGroupInfos.AddDefaulted_GetRef();
		Info.SimRootCount	= Data.SimRootData.RootCount;
		Info.SimLODCount	= Data.SimRootData.MeshProjectionLODs.Num();
		Info.RenRootCount	= Data.RenRootData.RootCount;
		Info.RenLODCount	= Data.RenRootData.MeshProjectionLODs.Num();
	}

	FHairStrandsProjectionHairData RenProjectionDatas;
	FHairStrandsProjectionHairData SimProjectionDatas;
	const uint32 GroupCount = OutHairGroupResources.Num();
	for (const UGroomBindingAsset::FHairGroupResource& GroupResources : OutHairGroupResources)
	{
		RenProjectionDatas.HairGroups.Add(ToProjectionHairData(GroupResources.RenRootResources, nullptr));
		SimProjectionDatas.HairGroups.Add(ToProjectionHairData(GroupResources.SimRootResources, nullptr));
	}

	FSkeletalMeshRenderData* TargetRenderData = TargetSkeletalMesh->GetResourceForRendering();
	FHairStrandsProjectionMeshData TargetMeshData = ExtractMeshData(TargetRenderData);

	// Create mapping between the source & target using their UV
	// The lifetime of 'TransferredPositions' needs to encompass RunProjection
	TArray<FRWBuffer> TransferredPositions;

	if (FSkeletalMeshRenderData* SourceRenderData = SourceSkeletalMesh ? SourceSkeletalMesh->GetResourceForRendering() : nullptr)
	{
		FHairStrandsProjectionMeshData SourceMeshData = ExtractMeshData(SourceRenderData);
		RunMeshTransfer(
			RHICmdList,
			SourceMeshData,
			TargetMeshData,
			TransferredPositions);

		for (uint32 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
		{
			for (FHairStrandsProjectionMeshData::Section& Section : TargetMeshData.LODs[LODIndex].Sections)
			{
				Section.PositionBuffer = TransferredPositions[LODIndex].SRV;
			}
		}
	}

	RunProjection(
		RHICmdList,
		FTransform::Identity,
		TargetMeshData,
		RenProjectionDatas,
		SimProjectionDatas);

	ComputeInterpolationWeights(BindingAsset, TargetRenderData, TransferredPositions);
	BindingAsset->QueryStatus = UGroomBindingAsset::EQueryStatus::Completed;
}

void FillLocalValidPoints(FSkeletalMeshLODRenderData& LODRenderData, const uint32 TargetSection, 
	const FHairStrandsRootData::FMeshProjectionLOD& ProjectionLOD, TArray<bool>& ValidPoints )
{
	TArray<uint32> TriangleIndices; TriangleIndices.SetNum(LODRenderData.MultiSizeIndexContainer.GetIndexBuffer()->Num());
	LODRenderData.MultiSizeIndexContainer.GetIndexBuffer(TriangleIndices);

	ValidPoints.Init(false, LODRenderData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices());

	const TArray<uint32>& RootBuffers = ProjectionLOD.RootTriangleIndexBuffer;
	for (int32 RootIt = 0; RootIt < RootBuffers.Num(); ++RootIt)
	{
		const uint32 SectionIndex = (RootBuffers[RootIt] >> 28) & 0xF;
		const uint32 TriangleIndex = RootBuffers[RootIt] & 0xFFFFFFF;
		if (SectionIndex == TargetSection)
		{
			for (uint32 VertexIt = 0; VertexIt < 3; ++VertexIt)
			{
				const uint32 VertexIndex = TriangleIndices[LODRenderData.RenderSections[SectionIndex].BaseIndex + 3 * TriangleIndex + VertexIt];
				ValidPoints[VertexIndex] = true;
			}
		}
	}
}

void FillGlobalValidPoints(FSkeletalMeshLODRenderData& LODRenderData, const uint32 TargetSection, TArray<bool>& ValidPoints)
{
	TArray<uint32> TriangleIndices; TriangleIndices.SetNum(LODRenderData.MultiSizeIndexContainer.GetIndexBuffer()->Num());
	LODRenderData.MultiSizeIndexContainer.GetIndexBuffer(TriangleIndices);

	ValidPoints.Init(false, LODRenderData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices());
	
	for (uint32 TriangleIt = 0; TriangleIt < LODRenderData.RenderSections[TargetSection].NumTriangles; ++TriangleIt)
	{
		for (uint32 VertexIt = 0; VertexIt < 3; ++VertexIt)
		{
			const uint32 VertexIndex = TriangleIndices[LODRenderData.RenderSections[TargetSection].BaseIndex + 3 * TriangleIt + VertexIt];
			ValidPoints[VertexIndex] = true;
		}
	}
}

void ComputeInterpolationWeights(UGroomBindingAsset* BindingAsset, FSkeletalMeshRenderData* TargetRenderData, TArray<FRWBuffer>& TransferedPositions)
{
	UGroomAsset* GroomAsset = BindingAsset->Groom;
	// Enforce GPU sync to read back data on CPU
	GDynamicRHI->RHISubmitCommandsAndFlushGPU();
	GDynamicRHI->RHIBlockUntilGPUIdle();

	UGroomBindingAsset::FHairGroupDatas& OutHairGroupDatas = BindingAsset->HairGroupDatas;
	UGroomBindingAsset::FHairGroupResources& OutHairGroupResources = BindingAsset->HairGroupResources;

	const uint32 GroupCount = OutHairGroupResources.Num();
	const uint32 LODCount = BindingAsset->TargetSkeletalMesh->GetLODNum();
	const uint32 MaxSamples = BindingAsset->NumInterpolationPoints;

	for (uint32 GroupIt = 0; GroupIt < GroupCount; ++GroupIt)
	{
		ReadbackGroupData(OutHairGroupDatas[GroupIt].SimRootData, OutHairGroupResources[GroupIt].SimRootResources);
		ReadbackGroupData(OutHairGroupDatas[GroupIt].RenRootData, OutHairGroupResources[GroupIt].RenRootResources);
	}

	const uint32 TargetSection = 0;
	const bool LocalSamples = false;
	for (uint32 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
	{
		FSkeletalMeshLODRenderData& LODRenderData = TargetRenderData->LODRenderData[LODIndex];

		TArray<FSkelMeshRenderSection>& RenderSections = LODRenderData.RenderSections;
		const int32 NumVertices = (RenderSections.Num() > TargetSection) ? RenderSections[TargetSection].NumVertices : 0;

		TArray<FVector> SourcePositions;
		FVector* PositionsPointer = nullptr;
		if (TransferedPositions.Num() == LODCount)
		{
			ReadbackBuffer(SourcePositions, TransferedPositions[LODIndex]);
			PositionsPointer = SourcePositions.GetData();
		}
		else
		{
			FPositionVertexBuffer& VertexBuffer = LODRenderData.StaticVertexBuffers.PositionVertexBuffer;
			PositionsPointer = static_cast<FVector*>(VertexBuffer.GetVertexData());
		}

		if (LocalSamples)
		{
			TArray<bool> ValidPoints;
			for (uint32 GroupIt = 0; GroupIt < GroupCount; ++GroupIt)
			{
				FillLocalValidPoints(LODRenderData, TargetSection, OutHairGroupDatas[GroupIt].RenRootData.MeshProjectionLODs[LODIndex], ValidPoints);

				FPointsSampler PointsSampler(ValidPoints, PositionsPointer, MaxSamples);
				const uint32 SampleCount = PointsSampler.SamplePositions.Num();

				FWeightsBuilder InterpolationWeights(SampleCount, SampleCount,
					PointsSampler.SamplePositions.GetData(), PointsSampler.SamplePositions.GetData());

				//const FVector Displace(0.0,0.0,10.0);
				//TArray<FVector> Deltas; Deltas.Init(FVector::ZeroVector, SampleCount );
				//for (uint32 i = 0; i < SampleCount; ++i)
				//{
				//	Deltas[i] = FVector(0,0,0);
				//	for (uint32 j = 0; j < SampleCount; ++j)
				//	{
				//		Deltas[i] += InterpolationWeights.InverseEntries[SampleCount * i + j] * Displace;
				//	}
				//	UE_LOG(LogHairStrands, Log, TEXT("[Groom] Sample Deltas[%d] = %s"), i, *Deltas[i].ToString());
				//}
				//for (uint32 i = 0; i < GroomAsset->HairGroupsData[GroupIt].HairSimulationData.StrandsPoints.Num(); ++i)
				//{
				//	FVector Offset(0,0,0);
				//	for (uint32 j = 0; j < SampleCount; ++j)
				//	{
				//		const FVector DeltaPosition = GroomAsset->HairGroupsData[GroupIt].HairSimulationData.StrandsPoints.PointsPosition[i] - PointsSampler.SamplePositions[j];
				//		const float FunctionValue = FMath::Sqrt(FVector::DotProduct(DeltaPosition, DeltaPosition)+1.0);
				//		Offset += FunctionValue * Deltas[j];
				//	}
				//	UE_LOG(LogHairStrands, Log, TEXT("[Groom] Sample Displace[%d] = %s"), i, *Offset.ToString());
				//}

				UpdateInterpolationWeights(InterpolationWeights, PointsSampler, LODIndex, OutHairGroupDatas[GroupIt].SimRootData);
				UpdateInterpolationWeights(InterpolationWeights, PointsSampler, LODIndex, OutHairGroupDatas[GroupIt].RenRootData);
			}
		}
		else
		{
			TArray<bool> ValidPoints;

			FillGlobalValidPoints(LODRenderData, TargetSection, ValidPoints);

			FPointsSampler PointsSampler(ValidPoints, PositionsPointer, MaxSamples);
			const uint32 SampleCount = PointsSampler.SamplePositions.Num();

			FWeightsBuilder InterpolationWeights(SampleCount, SampleCount,
				PointsSampler.SamplePositions.GetData(), PointsSampler.SamplePositions.GetData());

			for (uint32 GroupIt = 0; GroupIt < GroupCount; ++GroupIt)
			{
				UpdateInterpolationWeights(InterpolationWeights, PointsSampler, LODIndex, OutHairGroupDatas[GroupIt].SimRootData);
				UpdateInterpolationWeights(InterpolationWeights, PointsSampler, LODIndex, OutHairGroupDatas[GroupIt].RenRootData);
			}
		}
	}
	for (uint32 GroupIt = 0; GroupIt < GroupCount; ++GroupIt)
	{
		WritebackGroupData(OutHairGroupDatas[GroupIt].SimRootData, OutHairGroupResources[GroupIt].SimRootResources);
		WritebackGroupData(OutHairGroupDatas[GroupIt].RenRootData, OutHairGroupResources[GroupIt].RenRootResources);
	}
}

void AddGroomBindingTask(UGroomBindingAsset* BindingAsset)
{
	BindingAsset->QueryStatus = UGroomBindingAsset::EQueryStatus::Submitted;
	EnqueueGroomBindingQuery((void*)BindingAsset, InternalProcessGroomBindingTask);
}

FHairStrandsProjectionHairData::HairGroup ToProjectionHairData(
	FHairStrandsRestRootResource* InRest, 
	FHairStrandsDeformedRootResource* InDeformed)
{
	check(IsInRenderingThread());

	FHairStrandsProjectionHairData::HairGroup Out = {};
	if (!InRest)
		return Out;

	Out.RootCount = InRest->RootData.RootCount;
	Out.RootPositionBuffer = InRest->RootPositionBuffer.SRV;
	Out.RootNormalBuffer = InRest->RootNormalBuffer.SRV;
	Out.VertexToCurveIndexBuffer = &InRest->VertexToCurveIndexBuffer;

	if (InRest && InDeformed)
	{
		check(InRest->MeshProjectionLODs.Num() == InRest->MeshProjectionLODs.Num());
	}
	const uint32 LODCount = InRest->MeshProjectionLODs.Num();
	for (uint32 LODIt=0; LODIt<LODCount; ++LODIt)
	{
		{
			FHairStrandsRestRootResource::FMeshProjectionLOD& Rest = InRest->MeshProjectionLODs[LODIt];
			FHairStrandsProjectionHairData::RestLODData& OutRest = Out.RestLODDatas.AddDefaulted_GetRef();

			OutRest.Status = &Rest.Status;
			OutRest.LODIndex = Rest.LODIndex;

			OutRest.RootTriangleIndexBuffer = &Rest.RootTriangleIndexBuffer;
			OutRest.RootTriangleBarycentricBuffer = &Rest.RootTriangleBarycentricBuffer;

			OutRest.RestRootTrianglePosition0Buffer = &Rest.RestRootTrianglePosition0Buffer;
			OutRest.RestRootTrianglePosition1Buffer = &Rest.RestRootTrianglePosition1Buffer;
			OutRest.RestRootTrianglePosition2Buffer = &Rest.RestRootTrianglePosition2Buffer;


			OutRest.SampleCount	= Rest.SampleCount;
			OutRest.MeshInterpolationWeightsBuffer	= &Rest.MeshInterpolationWeightsBuffer;
			OutRest.MeshSampleIndicesBuffer			= &Rest.MeshSampleIndicesBuffer;
			OutRest.RestSamplePositionsBuffer		= &Rest.RestSamplePositionsBuffer;
		}

		if (InDeformed)
		{
			FHairStrandsDeformedRootResource::FMeshProjectionLOD& Deformed = InDeformed->MeshProjectionLODs[LODIt];
			FHairStrandsProjectionHairData::DeformedLODData& OutDeformed = Out.DeformedLODDatas.AddDefaulted_GetRef();

			OutDeformed.Status = &Deformed.Status;
			OutDeformed.LODIndex = Deformed.LODIndex;

			OutDeformed.DeformedRootTrianglePosition0Buffer = &Deformed.DeformedRootTrianglePosition0Buffer;
			OutDeformed.DeformedRootTrianglePosition1Buffer = &Deformed.DeformedRootTrianglePosition1Buffer;
			OutDeformed.DeformedRootTrianglePosition2Buffer = &Deformed.DeformedRootTrianglePosition2Buffer;

			OutDeformed.DeformedSamplePositionsBuffer	= &Deformed.DeformedSamplePositionsBuffer;
			OutDeformed.MeshSampleWeightsBuffer			= &Deformed.MeshSampleWeightsBuffer;
		}
	}
	return Out;
}
