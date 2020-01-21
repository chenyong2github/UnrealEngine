// Copyright Epic Games, Inc. All Rights Reserved. 

#include "HairStrandsRendering.h"
#include "HairStrandsDatas.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "GlobalShader.h"
#include "HairStrandsInterface.h"
#include "SceneView.h"
#include "Containers/ResourceArray.h"
#include "HAL/ConsoleManager.h"
#include "GpuDebugRendering.h"

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
	class FDeformationType : SHADER_PERMUTATION_INT("PERMUTATION_DEFORMATION", 4);
	using FPermutationDomain = TShaderPermutationDomain<FGroupSize, FDeformationType>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, VertexCount)
		SHADER_PARAMETER(uint32, IterationCount)
		SHADER_PARAMETER(FVector, SimRestOffset)
		SHADER_PARAMETER(FVector, SimDeformedOffset)
		SHADER_PARAMETER(uint32, DispatchCountX)
		SHADER_PARAMETER_SRV(Buffer, SimRestPosePositionBuffer)
		SHADER_PARAMETER_SRV(Buffer, SimRootIndexBuffer)
		SHADER_PARAMETER_UAV(RWBuffer, OutSimDeformedPositionBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FDeformGuideCS, "/Engine/Private/HairStrands/HairStrandsGuideDeform.usf", "MainCS", SF_Compute);

static void AddDeformSimHairStrandsPass(
	FRDGBuilder& GraphBuilder,
	EDeformationType DeformationType,
	uint32 VertexCount,
	FRHIShaderResourceView* SimRestPosePositionBuffer,
	FRHIShaderResourceView* SimRootIndexBuffer,
	FRHIUnorderedAccessView* OutSimDeformedPositionBuffer,
	FVector& SimRestOffset,
	FVector& SimDeformedOffset)
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

	FDeformGuideCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FDeformGuideCS::FGroupSize>(GetGroupSizePermutation(GroupSize));
	PermutationVector.Set<FDeformGuideCS::FDeformationType>(InternalDeformationType);

	TShaderMap<FGlobalShaderType>* ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);

	TShaderMapRef<FDeformGuideCS> ComputeShader(ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("DeformSimHairStrands"),
		*ComputeShader,
		Parameters,
		FIntVector(DispatchCountX, DispatchCountY, 1));
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
	class FDynamicGeometry : SHADER_PERMUTATION_INT("PERMUTATION_DYNAMIC_GEOMETRY", 2);
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

		SHADER_PARAMETER(FVector, RestPositionOffset)
		SHADER_PARAMETER(FVector, DeformedPositionOffset)

		SHADER_PARAMETER(FVector, SimRestPositionOffset)
		SHADER_PARAMETER(FVector, SimDeformedPositionOffset)

		SHADER_PARAMETER(FMatrix, LocalToWorldMatrix)

		SHADER_PARAMETER_SRV(Buffer, RenderRestPosePositionBuffer)
		SHADER_PARAMETER_UAV(RWBuffer, OutRenderDeformedPositionBuffer)

		SHADER_PARAMETER_UAV(RWBuffer, OutClusterBuffer)
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
		SHADER_PARAMETER_SRV(Buffer<uint>, RootToTriangleIndex)
		SHADER_PARAMETER_SRV(Buffer<uint>, RenVertexToRootIndexBuffer)

		SHADER_PARAMETER_SRV(Buffer<float4>, SimRestPosition0Buffer)
		SHADER_PARAMETER_SRV(Buffer<float4>, SimRestPosition1Buffer)
		SHADER_PARAMETER_SRV(Buffer<float4>, SimRestPosition2Buffer)

		SHADER_PARAMETER_SRV(Buffer<float4>, SimDeformedPosition0Buffer)
		SHADER_PARAMETER_SRV(Buffer<float4>, SimDeformedPosition1Buffer)
		SHADER_PARAMETER_SRV(Buffer<float4>, SimDeformedPosition2Buffer)

		SHADER_PARAMETER_SRV(Buffer<uint>, SimRootBarycentricBuffer)
		SHADER_PARAMETER_SRV(Buffer<uint>, SimRootToTriangleIndex)
		SHADER_PARAMETER_SRV(Buffer<uint>, SimVertexToRootIndexBuffer)

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
	const FShaderResourceViewRHIRef& VertexToClusterIdBuffer)
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
		}
	}

	const bool bSupportDynamicMesh = InRenHairData.RootCount > 0 && LODIndex >= 0 && LODIndex < InRenHairData.LODDatas.Num() && InRenHairData.LODDatas[LODIndex].bIsValid && bIsVertexToCurveBuffersValid;
	if (bSupportDynamicMesh)
	{
		Parameters->RestPositionOffset = *InRenHairData.LODDatas[LODIndex].RestPositionOffset;
		Parameters->RestPosition0Buffer = InRenHairData.LODDatas[LODIndex].RestRootTrianglePosition0Buffer->SRV;
		Parameters->RestPosition1Buffer = InRenHairData.LODDatas[LODIndex].RestRootTrianglePosition1Buffer->SRV;
		Parameters->RestPosition2Buffer = InRenHairData.LODDatas[LODIndex].RestRootTrianglePosition2Buffer->SRV;

		Parameters->DeformedPositionOffset = *InRenHairData.LODDatas[LODIndex].DeformedPositionOffset;
		Parameters->DeformedPosition0Buffer = InRenHairData.LODDatas[LODIndex].DeformedRootTrianglePosition0Buffer->SRV;
		Parameters->DeformedPosition1Buffer = InRenHairData.LODDatas[LODIndex].DeformedRootTrianglePosition1Buffer->SRV;
		Parameters->DeformedPosition2Buffer = InRenHairData.LODDatas[LODIndex].DeformedRootTrianglePosition2Buffer->SRV;

		Parameters->RootToTriangleIndex = InRenHairData.LODDatas[LODIndex].RootTriangleIndexBuffer->SRV;
		Parameters->RootBarycentricBuffer = InRenHairData.LODDatas[LODIndex].RootTriangleBarycentricBuffer->SRV;

		Parameters->SimRestPositionOffset = *InSimHairData.LODDatas[LODIndex].RestPositionOffset;
		Parameters->SimRestPosition0Buffer = InSimHairData.LODDatas[LODIndex].RestRootTrianglePosition0Buffer->SRV;
		Parameters->SimRestPosition1Buffer = InSimHairData.LODDatas[LODIndex].RestRootTrianglePosition1Buffer->SRV;
		Parameters->SimRestPosition2Buffer = InSimHairData.LODDatas[LODIndex].RestRootTrianglePosition2Buffer->SRV;

		Parameters->SimDeformedPositionOffset = *InSimHairData.LODDatas[LODIndex].DeformedPositionOffset;
		Parameters->SimDeformedPosition0Buffer = InSimHairData.LODDatas[LODIndex].DeformedRootTrianglePosition0Buffer->SRV;
		Parameters->SimDeformedPosition1Buffer = InSimHairData.LODDatas[LODIndex].DeformedRootTrianglePosition1Buffer->SRV;
		Parameters->SimDeformedPosition2Buffer = InSimHairData.LODDatas[LODIndex].DeformedRootTrianglePosition2Buffer->SRV;

		Parameters->SimRootToTriangleIndex = InSimHairData.LODDatas[LODIndex].RootTriangleIndexBuffer->SRV;
		Parameters->SimRootBarycentricBuffer = InSimHairData.LODDatas[LODIndex].RootTriangleBarycentricBuffer->SRV;
	}

	if (ShaderDrawDebug::IsShaderDrawDebugEnabled() && ShaderDrawData)
	{
		ShaderDrawDebug::SetParameters(GraphBuilder, *ShaderDrawData, Parameters->ShaderDrawParameters);
	}

	FHairInterpolationCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairInterpolationCS::FGroupSize>(GetGroupSizePermutation(GroupSize));
	PermutationVector.Set<FHairInterpolationCS::FDebug>(Parameters->HairDebugMode > 0 ? 1 : 0);
	PermutationVector.Set<FHairInterpolationCS::FDynamicGeometry>(bSupportDynamicMesh ? 1 : 0);
	PermutationVector.Set<FHairInterpolationCS::FSimulation>(bHasSimulationEnable ? 1 : 0);
	PermutationVector.Set<FHairInterpolationCS::FScaleAndClip>(bNeedScaleOrClip ? 1 : 0);

	TShaderMap<FGlobalShaderType>* ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);

	TShaderMapRef<FHairInterpolationCS> ComputeShader(ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsInterpolation"),
		*ComputeShader,
		Parameters,
		DispatchCount);
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
	const FShaderResourceViewRHIRef& RenderPositionBuffer)
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
		*ComputeShader,
		Parameters,
		DispatchCount);
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
	const FUnorderedAccessViewRHIRef& OutTangentBuffer)
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

	TShaderMap<FGlobalShaderType>* ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);

	TShaderMapRef<FHairTangentCS> ComputeShader(ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsTangent"),
		*ComputeShader,
		Parameters,
		DispatchCount);
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
	const FUnorderedAccessViewRHIRef& OutPositionBuffer)
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

	TShaderMap<FGlobalShaderType>* ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);

	TShaderMapRef<FHairRaytracingGeometryCS> ComputeShader(ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsRaytracingGeometry"),
		*ComputeShader,
		Parameters,
		DispatchCount);
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
	FRHIUnorderedAccessView* OutGroupAABBuffer)
{
	check(OutClusterAABBuffer);

	FClearClusterAABBCS::FParameters* Parameters = GraphBuilder.AllocParameters<FClearClusterAABBCS::FParameters>();
	Parameters->ClusterCount = ClusterCount;
	Parameters->OutClusterAABBBuffer = OutClusterAABBuffer;
	Parameters->OutGroupAABBBuffer = OutGroupAABBuffer;

	
	TShaderMap<FGlobalShaderType>* ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
	TShaderMapRef<FClearClusterAABBCS> ComputeShader(ShaderMap);

	const FIntVector DispatchCount = DispatchCount.DivideAndRoundUp(FIntVector(ClusterCount * 6, 1, 1) , FIntVector(64, 1, 1));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsClearClusterAABB"),
		*ComputeShader,
		Parameters,
		DispatchCount);
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
	FHairStrandsInterpolationInput* InInput,
	FHairStrandsInterpolationOutput* InOutput,
	FHairStrandsProjectionHairData& InRenHairDatas,
	FHairStrandsProjectionHairData& InSimHairDatas,
	int32 LODIndex,
	FHairStrandClusterData* ClusterData)
{
	if (!InInput || !InOutput) return;


	const uint32 GroupCount = InOutput->HairGroups.Num();
	for (uint32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
	{
		FHairStrandsInterpolationInput::FHairGroup& Input  = InInput->HairGroups[GroupIndex];
		FHairStrandsInterpolationOutput::HairGroup& Output = InOutput->HairGroups[GroupIndex];
		Output.VFInput.Reset();

		if (!Input.IsValid() || !Output.IsValid()) return;

		DECLARE_GPU_STAT(HairStrandsInterpolation);
		SCOPED_DRAW_EVENT(RHICmdList, HairStrandsInterpolation);
		SCOPED_GPU_STAT(RHICmdList, HairStrandsInterpolation);

		FRDGBuilder GraphBuilder(RHICmdList);

		const uint32 CurrIndex = Output.CurrentIndex;
		const uint32 PrevIndex = (Output.CurrentIndex + 1) % 2;

		const EDeformationType DeformationType = GetDeformationType();
		if (DeformationType != EDeformationType::RestStrands && DeformationType != EDeformationType::Simulation)
		{
			AddDeformSimHairStrandsPass(
				GraphBuilder,
				DeformationType,
				Input.SimVertexCount,
				Input.SimRestPosePositionBuffer->SRV,
				Input.SimRootPointIndexBuffer ? Input.SimRootPointIndexBuffer->SRV : nullptr,
				Output.SimDeformedPositionBuffer[CurrIndex]->UAV, Input.InSimHairPositionOffset,
				Input.OutHairPositionOffset );
		}

		// If the deformation is driven by the physics simulation, then the output is always the 0 index
		const uint32 SimIndex = GHairDeformationType == 0 ? 0 : CurrIndex;

		// Debug mode:
		// * None	: Display hair normally
		// * Sim	: Show sim strands
		// * Render : Show rendering strands with sim color influence
		const EHairStrandsDebugMode DebugMode = GetHairStrandsDebugStrandsMode();
		if (DebugMode == EHairStrandsDebugMode::SimHairStrands)
		{
			AddHairTangentPass(
				GraphBuilder,
				Input.SimVertexCount,
				Output.SimDeformedPositionBuffer[SimIndex]->SRV,
				Output.SimTangentBuffer->UAV);

			GraphBuilder.Execute();

			const bool bHasSimulationEnabled = Input.bIsSimulationEnable && GHairStrandsInterpolateSimulation && DeformationType != EDeformationType::RestStrands;

			Output.VFInput.HairPositionBuffer = Output.SimDeformedPositionBuffer[SimIndex]->SRV;
			Output.VFInput.HairPreviousPositionBuffer = Output.SimDeformedPositionBuffer[SimIndex]->SRV;
			Output.VFInput.HairTangentBuffer = Output.SimTangentBuffer->SRV;
			Output.VFInput.HairAttributeBuffer = Input.SimAttributeBuffer->SRV;
			Output.VFInput.HairMaterialBuffer = Output.RenderMaterialBuffer->SRV;
			Output.VFInput.HairPositionOffset = bHasSimulationEnabled ? Input.OutHairPositionOffset : Input.InSimHairPositionOffset;
			Output.VFInput.HairPreviousPositionOffset = bHasSimulationEnabled ? Input.OutHairPreviousPositionOffset : Input.InSimHairPositionOffset;
			Output.VFInput.VertexCount = Input.SimVertexCount;
			Output.VFInput.HairRadius = (GStrandHairWidth > 0 ? GStrandHairWidth : Input.GroupDesc.HairWidth) * 0.5f;
			Output.VFInput.HairLength = Input.GroupDesc.HairLength;
			Output.VFInput.HairDensity = Input.GroupDesc.HairShadowDensity;
		}
		else
		{
			check(ClusterData);
			const bool bDebugModePatchedAttributeBuffer = DebugMode == EHairStrandsDebugMode::RenderHairStrands || DebugMode == EHairStrandsDebugMode::RenderVisCluster;

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
				Output.RenderGroupAABBBuffer->UAV);

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
				Input.VertexToClusterIdBuffer->SRV);

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

			Output.VFInput.HairRadius = ScaleAndClipDesc.MaxOutHairRadius;
			Output.VFInput.HairLength = Input.GroupDesc.HairLength;
			Output.VFInput.HairDensity = Input.GroupDesc.HairShadowDensity;
			Output.VFInput.HairPositionBuffer = Output.RenderDeformedPositionBuffer[CurrIndex]->SRV;
			Output.VFInput.HairPreviousPositionBuffer = Output.RenderDeformedPositionBuffer[PrevIndex]->SRV;

			AddHairClusterAABBPass(
				GraphBuilder,
				InRenHairDatas.HairGroups[GroupIndex],
				Input.OutHairPositionOffset,
				HairGroupCluster,
				Output.RenderDeformedPositionBuffer[CurrIndex]->SRV);
			
			AddHairTangentPass(
				GraphBuilder,
				Input.RenderVertexCount,
				Output.VFInput.HairPositionBuffer,
				Output.RenderTangentBuffer->UAV);

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
					Input.RaytracingPositionBuffer->UAV);
			}
			#endif
			GraphBuilder.Execute();

			Output.VFInput.HairTangentBuffer			= Output.RenderTangentBuffer->SRV;
			Output.VFInput.HairAttributeBuffer			= bDebugModePatchedAttributeBuffer ? Output.RenderPatchedAttributeBuffer.SRV : Input.RenderAttributeBuffer->SRV;
			Output.VFInput.HairMaterialBuffer			= Output.RenderMaterialBuffer->SRV;
			Output.VFInput.HairPositionOffset			= Input.OutHairPositionOffset;
			Output.VFInput.HairPreviousPositionOffset	= Input.OutHairPreviousPositionOffset;
			Output.VFInput.VertexCount					= Input.RenderVertexCount;

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

		Output.CurrentIndex = PrevIndex;
	}
}

void ResetHairStrandsInterpolation(
	FRHICommandListImmediate& RHICmdList,
	FHairStrandsInterpolationInput* InInput,
	FHairStrandsInterpolationOutput* InOutput)
{
	if (!InInput || !InOutput) return;

	const uint32 GroupCount = InOutput->HairGroups.Num();
	for (uint32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
	{
		FHairStrandsInterpolationInput::FHairGroup& Input = InInput->HairGroups[GroupIndex];
		FHairStrandsInterpolationOutput::HairGroup& Output = InOutput->HairGroups[GroupIndex];
		if (!Input.IsValid() || !Output.IsValid()) return;

		DECLARE_GPU_STAT(HairStrandsResetInterpolation);
		SCOPED_DRAW_EVENT(RHICmdList, HairStrandsResetInterpolation);
		SCOPED_GPU_STAT(RHICmdList, HairStrandsResetInterpolation);

		const uint32 CurrIndex = Output.CurrentIndex;
		const uint32 PrevIndex = (Output.CurrentIndex + 1) % 2;
		FRDGBuilder GraphBuilder(RHICmdList);
		AddDeformSimHairStrandsPass(
			GraphBuilder,
			EDeformationType::OffsetGuide,
			Input.SimVertexCount,
			Input.SimRestPosePositionBuffer->SRV,
			Input.SimRootPointIndexBuffer ? Input.SimRootPointIndexBuffer->SRV : nullptr,
			Output.SimDeformedPositionBuffer[CurrIndex]->UAV, Input.InSimHairPositionOffset,
			Input.OutHairPositionOffset);
		GraphBuilder.Execute();
	}
}