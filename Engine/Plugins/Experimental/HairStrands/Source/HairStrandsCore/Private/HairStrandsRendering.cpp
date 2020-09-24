// Copyright Epic Games, Inc. All Rights Reserved. 

#include "HairStrandsRendering.h"
#include "HairStrandsDatas.h"
#include "HairCardsBuilder.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "GlobalShader.h"
#include "GroomAsset.h"
#include "GroomBindingAsset.h"
#include "HairStrandsInterface.h"
#include "SceneView.h"
#include "Containers/ResourceArray.h"
#include "Rendering\SkeletalMeshRenderData.h"
#include "HAL/ConsoleManager.h"
#include "GpuDebugRendering.h"
#include "Async/ParallelFor.h"
#include "RenderTargetPool.h"
#include "GroomTextureBuilder.h"
#include "GroomBindingBuilder.h"
#include "GroomAsset.h" 
#include "GroomManager.h"
#include "GroomInstance.h"

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
	const uint32 MeshLODIndex,
	const uint32 VertexCount,
	FHairStrandsRestRootResource* SimRestRootResources,
	FHairStrandsDeformedRootResource* SimDeformedRootResources,
	FRHIShaderResourceView* SimRestPosePositionBuffer,
	FRHIShaderResourceView* SimRootIndexBuffer,
	FRHIUnorderedAccessView* OutSimDeformedPositionBuffer,
	const FVector& SimRestOffset,
	const FVector& SimDeformedOffset,
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
		const bool bIsVertexToCurveBuffersValid = SimRestRootResources && SimRestRootResources->VertexToCurveIndexBuffer.SRV != nullptr;
		if (bIsVertexToCurveBuffersValid)
		{
			Parameters->SimVertexToRootIndexBuffer = SimRestRootResources->VertexToCurveIndexBuffer.SRV;
		}

		const uint32 RootCount = SimRestRootResources ? SimRestRootResources->RootData.RootCount : 0;
		const bool bSupportDynamicMesh = 
			RootCount > 0 && 
			MeshLODIndex >= 0 && 
			MeshLODIndex < uint32(SimRestRootResources->LODs.Num()) &&
			MeshLODIndex < uint32(SimDeformedRootResources->LODs.Num()) &&
			SimRestRootResources->LODs[MeshLODIndex].IsValid() &&
			SimDeformedRootResources->LODs[MeshLODIndex].IsValid() &&
			bIsVertexToCurveBuffersValid;
		
		bool bSupportGlobalInterpolation = false;
		if (bSupportDynamicMesh)
		{
			FHairStrandsRestRootResource::FLOD& RestLODDatas = SimRestRootResources->LODs[MeshLODIndex];
			FHairStrandsDeformedRootResource::FLOD& DeformedLODDatas = SimDeformedRootResources->LODs[MeshLODIndex];

			bSupportGlobalInterpolation = bHasGlobalInterpolation && (RestLODDatas.SampleCount > 0);
			if (!bSupportGlobalInterpolation) 
			{
				InternalDeformationType = 4;
				Parameters->SimRestPosition0Buffer = RestLODDatas.RestRootTrianglePosition0Buffer.SRV;
				Parameters->SimRestPosition1Buffer = RestLODDatas.RestRootTrianglePosition1Buffer.SRV;
				Parameters->SimRestPosition2Buffer = RestLODDatas.RestRootTrianglePosition2Buffer.SRV;

				Parameters->SimDeformedPosition0Buffer = DeformedLODDatas.DeformedRootTrianglePosition0Buffer.SRV;
				Parameters->SimDeformedPosition1Buffer = DeformedLODDatas.DeformedRootTrianglePosition1Buffer.SRV;
				Parameters->SimDeformedPosition2Buffer = DeformedLODDatas.DeformedRootTrianglePosition2Buffer.SRV;

				Parameters->SimRootBarycentricBuffer = RestLODDatas.RootTriangleBarycentricBuffer.SRV;
			}
			else
			{
				InternalDeformationType = 5;
				Parameters->MeshSampleWeightsBuffer = DeformedLODDatas.MeshSampleWeightsBuffer.SRV;
				Parameters->RestSamplePositionsBuffer = RestLODDatas.RestSamplePositionsBuffer.SRV;
				Parameters->SampleCount = RestLODDatas.SampleCount;
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
	float InHairLength = 0;
	float InHairRadius = 0;
	float OutHairRadius = 0;
	float MaxOutHairRadius = 0;
	float HairRadiusRootScale = 1;
	float HairRadiusTipScale = 1;
	float HairLengthClip = 1;
	bool  bEnable = true;

	bool IsEnable() const 
	{
		return 
			bEnable && (
			InHairRadius != OutHairRadius ||
			HairRadiusRootScale != 1 ||
			HairRadiusTipScale != 1 ||
			HairLengthClip < 1);
	}
};

class FHairInterpolationCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairInterpolationCS);
	SHADER_USE_PARAMETER_STRUCT(FHairInterpolationCS, FGlobalShader);

	class FGroupSize : SHADER_PERMUTATION_SPARSE_INT("PERMUTATION_GROUP_SIZE", 32, 64);
	class FDebug : SHADER_PERMUTATION_INT("PERMUTATION_DEBUG", 2);
	class FDynamicGeometry : SHADER_PERMUTATION_INT("PERMUTATION_DYNAMIC_GEOMETRY", 5);
	class FSimulation : SHADER_PERMUTATION_INT("PERMUTATION_SIMULATION", 2);
	class FScaleAndClip : SHADER_PERMUTATION_INT("PERMUTATION_SCALE_AND_CLIP", 2);
	class FCulling : SHADER_PERMUTATION_INT("PERMUTATION_CULLING", 2);
	using FPermutationDomain = TShaderPermutationDomain<FGroupSize, FDebug, FDynamicGeometry, FSimulation, FScaleAndClip, FCulling>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
		SHADER_PARAMETER(uint32, VertexCount)
		SHADER_PARAMETER(uint32, DispatchCountX)
		SHADER_PARAMETER(uint32, HairDebugMode)
		SHADER_PARAMETER(FVector, InRenderHairPositionOffset)
		SHADER_PARAMETER(FVector, InSimHairPositionOffset)
		SHADER_PARAMETER(FVector, OutRenderHairPositionOffset)
		SHADER_PARAMETER(FVector, OutSimHairPositionOffset)
		SHADER_PARAMETER(FIntPoint, HairStrandsCullIndex)

		SHADER_PARAMETER(float, InHairLength)
		SHADER_PARAMETER(float, InHairRadius)
		SHADER_PARAMETER(float, OutHairRadius)
		SHADER_PARAMETER(float, MaxOutHairRadius)
		SHADER_PARAMETER(float, HairRadiusRootScale)
		SHADER_PARAMETER(float, HairRadiusTipScale)
		SHADER_PARAMETER(float, HairLengthClip)
		SHADER_PARAMETER(uint32,  HairStrandsVF_bIsCullingEnable)

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

		SHADER_PARAMETER_SRV(Buffer<uint>,	HairStrandsVF_CullingIndirectBuffer)
		SHADER_PARAMETER_SRV(Buffer<uint>,	HairStrandsVF_CullingIndexBuffer)
		SHADER_PARAMETER_SRV(Buffer<float>,	HairStrandsVF_CullingRadiusScaleBuffer)

		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_HAIRINTERPOLATION"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairInterpolationCS, "/Engine/Private/HairStrands/HairStrandsInterpolation.usf", "MainCS", SF_Compute);

static void AddHairStrandsInterpolationPass(
	FRDGBuilder& GraphBuilder,
	const FShaderDrawDebugData* ShaderDrawData,
	FHairGroupInstance* Instance,
	const uint32 VertexCount,
	const FHairScaleAndClipDesc ScaleAndClipDesc,
	const int32 MeshLODIndex,
	const bool bPatchedAttributeBuffer, 	
	FBufferTransitionQueue& OutTransitionQueue,
	const uint32 HairInterpolationType,
	FHairGroupPublicData* HairGroupPublicData,
	const FVector& InRenderHairWorldOffset,
	const FVector& InSimHairWorldOffset,
	const FVector& OutRenderHairWorldOffset,
	const FVector& OutSimHairWorldOffset,
	const FHairStrandsRestRootResource* RenRestRootResources,
	const FHairStrandsRestRootResource* SimRestRootResources,
	const FHairStrandsDeformedRootResource* RenDeformedRootResources,
	const FHairStrandsDeformedRootResource* SimDeformedRootResources,
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
	const FShaderResourceViewRHIRef& SimRootPointIndexBuffer)
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
	Parameters->InSimHairPositionOffset =  InSimHairWorldOffset;
	Parameters->OutRenderHairPositionOffset = OutRenderHairWorldOffset;
	Parameters->OutSimHairPositionOffset = OutSimHairWorldOffset;
	Parameters->DispatchCountX = DispatchCount.X;
	Parameters->SimRootPointIndexBuffer = SimRootPointIndexBuffer;
	
	const bool bNeedScaleOrClip = ScaleAndClipDesc.IsEnable();

	Parameters->InHairLength = ScaleAndClipDesc.InHairLength;
	Parameters->InHairRadius = ScaleAndClipDesc.InHairRadius;
	Parameters->OutHairRadius = ScaleAndClipDesc.OutHairRadius;
	Parameters->MaxOutHairRadius = ScaleAndClipDesc.MaxOutHairRadius;
	Parameters->HairRadiusRootScale = ScaleAndClipDesc.HairRadiusRootScale;
	Parameters->HairRadiusTipScale = ScaleAndClipDesc.HairRadiusTipScale;
	Parameters->HairLengthClip = ScaleAndClipDesc.HairLengthClip * ScaleAndClipDesc.InHairLength;
	if (bNeedScaleOrClip)
	{
		Parameters->AttributeBuffer = RenderAttributeBuffer;
	}
	
	const bool bIsVertexToCurveBuffersValid =
		SimRestRootResources &&
		SimRestRootResources->VertexToCurveIndexBuffer.SRV != nullptr &&
		RenRestRootResources &&
		RenRestRootResources->VertexToCurveIndexBuffer.SRV != nullptr;

	if (bIsVertexToCurveBuffersValid)
	{
		Parameters->RenVertexToRootIndexBuffer = RenRestRootResources->VertexToCurveIndexBuffer.SRV;
		Parameters->SimVertexToRootIndexBuffer = SimRestRootResources->VertexToCurveIndexBuffer.SRV;
	}

	Parameters->VertexToClusterIdBuffer = VertexToClusterIdBuffer;
	
	Parameters->LocalToWorldMatrix = Instance->LocalToWorld.ToMatrixWithScale();

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
				Parameters->HairStrandsCullIndex.Y = Info.ExplicitIndex >= 0 ? Info.ExplicitIndex : FMath::Clamp(uint32(Info.NormalizedIndex * SimRestRootResources->RootData.RootCount), 0u, SimRestRootResources->RootData.RootCount - 1);
			if (Info.CullMode == EHairCullMode::Render)
				Parameters->HairStrandsCullIndex.X = Info.ExplicitIndex >= 0 ? Info.ExplicitIndex : FMath::Clamp(uint32(Info.NormalizedIndex * RenRestRootResources->RootData.RootCount), 0u, RenRestRootResources->RootData.RootCount - 1);
		}

		if (Parameters->HairDebugMode > 0)
		{
			Parameters->SimAttributeBuffer = SimAttributeBuffer;
			Parameters->OutRenderAttributeBuffer = OutRenderAttributeBuffer;
			OutTransitionQueue.Add(OutRenderAttributeBuffer);
		}
	}

	const bool bSupportDynamicMesh = 
		bIsVertexToCurveBuffersValid &&
		RenRestRootResources &&
		RenRestRootResources->RootData.RootCount > 0 && 
		MeshLODIndex >= 0 && 
		MeshLODIndex < RenRestRootResources->LODs.Num() &&
		MeshLODIndex < RenDeformedRootResources->LODs.Num() &&
		RenRestRootResources->LODs[MeshLODIndex].IsValid() &&
		RenDeformedRootResources->LODs[MeshLODIndex].IsValid();

	bool bSupportGlobalInterpolation = false;
	if (bSupportDynamicMesh)
	{
		const FHairStrandsRestRootResource::FLOD& Sim_RestLODDatas = SimRestRootResources->LODs[MeshLODIndex];
		const FHairStrandsRestRootResource::FLOD& Ren_RestLODDatas = RenRestRootResources->LODs[MeshLODIndex];
		const FHairStrandsDeformedRootResource::FLOD& Sim_DeformedLODDatas = SimDeformedRootResources->LODs[MeshLODIndex];
		const FHairStrandsDeformedRootResource::FLOD& Ren_DeformedLODDatas = RenDeformedRootResources->LODs[MeshLODIndex];

		bSupportGlobalInterpolation = Instance->Guides.bHasGlobalInterpolation && (Sim_RestLODDatas.SampleCount > 0);
		{
			Parameters->RestPosition0Buffer = Ren_RestLODDatas.RestRootTrianglePosition0Buffer.SRV;
			Parameters->RestPosition1Buffer = Ren_RestLODDatas.RestRootTrianglePosition1Buffer.SRV;
			Parameters->RestPosition2Buffer = Ren_RestLODDatas.RestRootTrianglePosition2Buffer.SRV;

			Parameters->RootBarycentricBuffer = Ren_RestLODDatas.RootTriangleBarycentricBuffer.SRV;

			Parameters->SimRestPosition0Buffer = Sim_RestLODDatas.RestRootTrianglePosition0Buffer.SRV;
			Parameters->SimRestPosition1Buffer = Sim_RestLODDatas.RestRootTrianglePosition1Buffer.SRV;
			Parameters->SimRestPosition2Buffer = Sim_RestLODDatas.RestRootTrianglePosition2Buffer.SRV;

			Parameters->SimRootBarycentricBuffer = Sim_RestLODDatas.RootTriangleBarycentricBuffer.SRV;
		}
		{
			Parameters->DeformedPosition0Buffer = Ren_DeformedLODDatas.DeformedRootTrianglePosition0Buffer.SRV;
			Parameters->DeformedPosition1Buffer = Ren_DeformedLODDatas.DeformedRootTrianglePosition1Buffer.SRV;
			Parameters->DeformedPosition2Buffer = Ren_DeformedLODDatas.DeformedRootTrianglePosition2Buffer.SRV;

			Parameters->SimDeformedPosition0Buffer = Sim_DeformedLODDatas.DeformedRootTrianglePosition0Buffer.SRV;
			Parameters->SimDeformedPosition1Buffer = Sim_DeformedLODDatas.DeformedRootTrianglePosition1Buffer.SRV;
			Parameters->SimDeformedPosition2Buffer = Sim_DeformedLODDatas.DeformedRootTrianglePosition2Buffer.SRV;
		}
	}

	if (ShaderDrawDebug::IsShaderDrawDebugEnabled() && ShaderDrawData)
	{
		ShaderDrawDebug::SetParameters(GraphBuilder, *ShaderDrawData, Parameters->ShaderDrawParameters);
	}

	const bool bHasLocalDeformation = Instance->Guides.bIsSimulationEnable || bSupportGlobalInterpolation;
	const bool bCullingEnable = Instance->GeometryType == EHairGeometryType::Strands && HairGroupPublicData->GetCullingResultAvailable();
	Parameters->HairStrandsVF_bIsCullingEnable = bCullingEnable ? 1 : 0;

	FHairInterpolationCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairInterpolationCS::FGroupSize>(GroupSize);
	PermutationVector.Set<FHairInterpolationCS::FDebug>(Parameters->HairDebugMode > 0 ? 1 : 0);
	PermutationVector.Set<FHairInterpolationCS::FDynamicGeometry>((bSupportDynamicMesh && bHasLocalDeformation) ? HairInterpolationType+1 :
							(bSupportDynamicMesh && !bHasLocalDeformation) ? 1 : 0);
	PermutationVector.Set<FHairInterpolationCS::FSimulation>(bHasLocalDeformation ? 1 : 0);
	PermutationVector.Set<FHairInterpolationCS::FScaleAndClip>(bNeedScaleOrClip ? 1 : 0);
	PermutationVector.Set<FHairInterpolationCS::FCulling>(bCullingEnable ? 1 : 0);

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
	TShaderMapRef<FHairInterpolationCS> ComputeShader(ShaderMap, PermutationVector);

	if (bCullingEnable)
	{
		Parameters->HairStrandsVF_CullingIndirectBuffer = HairGroupPublicData->GetDrawIndirectRasterComputeBuffer().SRV;
		Parameters->HairStrandsVF_CullingIndexBuffer = HairGroupPublicData->GetCulledVertexIdBuffer().SRV;
		Parameters->HairStrandsVF_CullingRadiusScaleBuffer = HairGroupPublicData->GetCulledVertexRadiusScaleBuffer().SRV;

		FVertexBufferRHIRef IndirectArgsBuffer = HairGroupPublicData->GetDrawIndirectRasterComputeBuffer().Buffer.GetReference();
		ClearUnusedGraphResources(ComputeShader, Parameters);
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HairStrandsInterpolation(culling=on)"),
			Parameters,
			ERDGPassFlags::Compute,
			[Parameters, ComputeShader, IndirectArgsBuffer](FRHICommandList& RHICmdList)
			{
				uint32 IndirectArgOffset = 0;
				FRHIComputeShader* ShaderRHI = ComputeShader.GetComputeShader();
				RHICmdList.SetComputeShader(ShaderRHI);
				SetShaderParameters(RHICmdList, ComputeShader, ShaderRHI, *Parameters);
				RHICmdList.DispatchIndirectComputeShader(IndirectArgsBuffer, IndirectArgOffset);
				UnsetShaderUAVs(RHICmdList, ComputeShader, ShaderRHI);
			});
	}
	else
	{
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("HairStrandsInterpolation(culling=off)"),
			ComputeShader,
			Parameters,
			DispatchCount);
	}


	OutTransitionQueue.Add(OutRenderPositionBuffer);	
}

///////////////////////////////////////////////////////////////////////////////////////////////////


class FHairClusterAABBCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairClusterAABBCS);
	SHADER_USE_PARAMETER_STRUCT(FHairClusterAABBCS, FGlobalShader);

	class FGroupSize : SHADER_PERMUTATION_SPARSE_INT("PERMUTATION_GROUP_SIZE", 32, 64);
	using FPermutationDomain = TShaderPermutationDomain<FGroupSize>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, DispatchCountX)
		SHADER_PARAMETER(uint32, ClusterCount)
		SHADER_PARAMETER(FVector, OutHairPositionOffset)
		SHADER_PARAMETER(FMatrix, LocalToWorldMatrix)
		SHADER_PARAMETER_SRV(Buffer, RenderDeformedPositionBuffer)
		SHADER_PARAMETER_SRV(Buffer, ClusterVertexIdBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, ClusterIdBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, ClusterIndexOffsetBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, ClusterIndexCountBuffer)
		SHADER_PARAMETER_UAV(RWBuffer, OutClusterAABBBuffer)
		SHADER_PARAMETER_UAV(RWBuffer, OutGroupAABBBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_CLUSTERAABB"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairClusterAABBCS, "/Engine/Private/HairStrands/HairStrandsInterpolation.usf", "ClusterAABBEvaluationCS", SF_Compute);

static void AddHairClusterAABBPass(
	FRDGBuilder& GraphBuilder,
	const FTransform& InRenLocalToWorld,
	const FVector& OutHairWorldOffset,
	FHairStrandClusterData::FHairGroup& ClusterData,
	const FShaderResourceViewRHIRef& RenderPositionBuffer,
	FBufferTransitionQueue& OutTransitionQueue)
{
	const uint32 GroupSize = ComputeGroupSize();
	const FIntVector DispatchCount = ComputeDispatchGroupCount2D(ClusterData.ClusterCount);

	FRDGBufferRef ClusterIdBuffer = GraphBuilder.RegisterExternalBuffer(ClusterData.ClusterIdBuffer);
	FRDGBufferRef ClusterIndexOffsetBuffer = GraphBuilder.RegisterExternalBuffer(ClusterData.ClusterIndexOffsetBuffer);
	FRDGBufferRef ClusterIndexCountBuffer  = GraphBuilder.RegisterExternalBuffer(ClusterData.ClusterIndexCountBuffer);
	FHairClusterAABBCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairClusterAABBCS::FParameters>();
	Parameters->DispatchCountX = DispatchCount.X;
	Parameters->ClusterCount = ClusterData.ClusterCount;
	Parameters->LocalToWorldMatrix = InRenLocalToWorld.ToMatrixWithScale();
	Parameters->OutHairPositionOffset = OutHairWorldOffset;
	Parameters->RenderDeformedPositionBuffer = RenderPositionBuffer;
	Parameters->ClusterVertexIdBuffer = ClusterData.ClusterVertexIdBuffer->SRV;
	Parameters->ClusterIdBuffer = GraphBuilder.CreateSRV(ClusterIdBuffer, PF_R32_UINT);
	Parameters->ClusterIndexOffsetBuffer = GraphBuilder.CreateSRV(ClusterIndexOffsetBuffer, PF_R32_UINT);
	Parameters->ClusterIndexCountBuffer = GraphBuilder.CreateSRV(ClusterIndexCountBuffer, PF_R32_UINT);
	Parameters->OutClusterAABBBuffer = ClusterData.HairGroupPublicPtr->GetClusterAABBBuffer().UAV;
	Parameters->OutGroupAABBBuffer = ClusterData.HairGroupPublicPtr->GetGroupAABBBuffer().UAV;

	FHairClusterAABBCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairClusterAABBCS::FGroupSize>(GroupSize);
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

class FHairCardsDeformationCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairCardsDeformationCS);
	SHADER_USE_PARAMETER_STRUCT(FHairCardsDeformationCS, FGlobalShader);

	class FGroupSize : SHADER_PERMUTATION_INT("PERMUTATION_GROUP_SIZE", 2);
	using FPermutationDomain = TShaderPermutationDomain<FGroupSize>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, CardsVertexCount)
		SHADER_PARAMETER(uint32, GuideVertexCount)
		SHADER_PARAMETER(FVector, GuideRestPositionOffset)
		SHADER_PARAMETER(FVector, GuideDeformedPositionOffset)
		SHADER_PARAMETER_SRV(Buffer, GuideRestPositionBuffer)
		SHADER_PARAMETER_SRV(Buffer, GuideDeformedPositionBuffer)
		SHADER_PARAMETER_SRV(Buffer,   CardsRestPositionBuffer)
		SHADER_PARAMETER_SRV(Buffer,   CardsInterpolationBuffer)
		SHADER_PARAMETER_UAV(RWBuffer, CardsDeformedPositionBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FHairCardsDeformationCS, "/Engine/Private/HairStrands/HairCardsDeformation.usf", "MainCS", SF_Compute);

static void AddHairCardsDeformationPass(
	FRDGBuilder& GraphBuilder,
	FHairGroupInstance* Instance,
	FBufferTransitionQueue& OutTransitionQueue)
{
	const int32 HairLODIndex = Instance->HairGroupPublicData->GetIntLODIndex();
	if (!Instance->Cards.IsValid(HairLODIndex))
		return;

	FHairGroupInstance::FCards::FLOD& LOD = Instance->Cards.LODs[HairLODIndex];
	
	FHairCardsDeformationCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairCardsDeformationCS::FParameters>();
	Parameters->GuideVertexCount = LOD.Guides.RestResource->GetVertexCount();
	Parameters->GuideRestPositionOffset = LOD.Guides.RestResource->PositionOffset;
	Parameters->GuideDeformedPositionOffset = LOD.Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::Current);
	Parameters->GuideRestPositionBuffer = LOD.Guides.RestResource->RestPositionBuffer.SRV;
	Parameters->GuideDeformedPositionBuffer = LOD.Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current).SRV;
	
	Parameters->CardsVertexCount = LOD.RestResource->VertexCount;
	Parameters->CardsRestPositionBuffer = LOD.RestResource->RestPositionBuffer.SRV;
	Parameters->CardsDeformedPositionBuffer = LOD.DeformedResource->GetBuffer(FHairCardsDeformedResource::Current).UAV;

	Parameters->CardsInterpolationBuffer = LOD.InterpolationResource->InterpolationBuffer.SRV;

	const uint32 GroupSize = ComputeGroupSize();
	FHairCardsDeformationCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairCardsDeformationCS::FGroupSize>(GetGroupSizePermutation(GroupSize));

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
	TShaderMapRef<FHairCardsDeformationCS> ComputeShader(ShaderMap, PermutationVector);

	const int32 DispatchCountX = FMath::DivideAndRoundUp(Parameters->CardsVertexCount, GroupSize);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairCardsDeformation"),
		ComputeShader,
		Parameters,
		FIntVector(DispatchCountX,1,1));

	OutTransitionQueue.Add(Parameters->CardsDeformedPositionBuffer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairTangentCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairTangentCS);
	SHADER_USE_PARAMETER_STRUCT(FHairTangentCS, FGlobalShader);

	class FGroupSize : SHADER_PERMUTATION_INT("PERMUTATION_GROUP_SIZE", 2);
	class FCulling : SHADER_PERMUTATION_INT("PERMUTATION_CULLING", 2);
	using FPermutationDomain = TShaderPermutationDomain<FGroupSize, FCulling>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, VertexCount)
		SHADER_PARAMETER(uint32, DispatchCountX)
		SHADER_PARAMETER(uint32, HairStrandsVF_bIsCullingEnable)
		SHADER_PARAMETER_SRV(Buffer, PositionBuffer)
		SHADER_PARAMETER_SRV(Buffer<uint>,	HairStrandsVF_CullingIndirectBuffer)
		SHADER_PARAMETER_SRV(Buffer<uint>,	HairStrandsVF_CullingIndexBuffer)
		SHADER_PARAMETER_SRV(Buffer<float>,	HairStrandsVF_CullingRadiusScaleBuffer)
		SHADER_PARAMETER_UAV(RWBuffer, OutputTangentBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FHairTangentCS, "/Engine/Private/HairStrands/HairStrandsTangent.usf", "MainCS", SF_Compute);

static void AddHairTangentPass(
	FRDGBuilder& GraphBuilder,
	uint32 VertexCount,
	FHairGroupPublicData* HairGroupPublicData,
	const FShaderResourceViewRHIRef& PositionBuffer,
	const FUnorderedAccessViewRHIRef& OutTangentBuffer,
	FBufferTransitionQueue& OutTransitionQueue)
{
	const uint32 GroupSize = ComputeGroupSize();
	const FIntVector DispatchCount = ComputeDispatchCount(VertexCount, GroupSize);
	const bool bCullingEnable = HairGroupPublicData->GetCullingResultAvailable();

	FHairTangentCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairTangentCS::FParameters>();
	Parameters->PositionBuffer = PositionBuffer;
	Parameters->OutputTangentBuffer = OutTangentBuffer;
	Parameters->VertexCount = VertexCount;
	Parameters->DispatchCountX = DispatchCount.X;
	Parameters->HairStrandsVF_bIsCullingEnable = bCullingEnable ? 1 : 0;

	FHairTangentCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairTangentCS::FGroupSize>(GetGroupSizePermutation(GroupSize));
	PermutationVector.Set<FHairTangentCS::FCulling>(bCullingEnable ? 1 : 0);

	FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);
	TShaderMapRef<FHairTangentCS> ComputeShader(ShaderMap, PermutationVector);

	if (bCullingEnable)
	{
		Parameters->HairStrandsVF_CullingIndirectBuffer = HairGroupPublicData->GetDrawIndirectRasterComputeBuffer().SRV;
		Parameters->HairStrandsVF_CullingIndexBuffer = HairGroupPublicData->GetCulledVertexIdBuffer().SRV;
		Parameters->HairStrandsVF_CullingRadiusScaleBuffer = HairGroupPublicData->GetCulledVertexRadiusScaleBuffer().SRV;

		FVertexBufferRHIRef IndirectArgsBuffer = HairGroupPublicData->GetDrawIndirectRasterComputeBuffer().Buffer.GetReference();
		ClearUnusedGraphResources(ComputeShader, Parameters);
		GraphBuilder.AddPass(
			RDG_EVENT_NAME("HairStrandsTangent(culling=on)"),
			Parameters,
			ERDGPassFlags::Compute,
			[Parameters, ComputeShader, IndirectArgsBuffer](FRHICommandList& RHICmdList)
			{
				uint32 IndirectArgOffset = 0;
				FRHIComputeShader* ShaderRHI = ComputeShader.GetComputeShader();
				RHICmdList.SetComputeShader(ShaderRHI);
				SetShaderParameters(RHICmdList, ComputeShader, ShaderRHI, *Parameters);
				RHICmdList.DispatchIndirectComputeShader(IndirectArgsBuffer, IndirectArgOffset);
				UnsetShaderUAVs(RHICmdList, ComputeShader, ShaderRHI);
			});
	}
	else
	{
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("HairStrandsTangent(culling=off)"),
			ComputeShader,
			Parameters,
			DispatchCount);
	}

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
	static const FName DebugName("Hair");
	static int32 DebugNumber = 0;
	Initializer.DebugName = FName(DebugName, DebugNumber++);
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


void RegisterClusterData(FHairGroupInstance* Instance, FHairStrandClusterData* InClusterData)
{
	// Initialize group cluster data for culling by the renderer
	const int32 ClusterDataGroupIndex = InClusterData->HairGroups.Num();
	FHairStrandClusterData::FHairGroup& HairGroupCluster = InClusterData->HairGroups.Emplace_GetRef();
	HairGroupCluster.ClusterCount = Instance->HairGroupPublicData->GetClusterCount();
	HairGroupCluster.VertexCount = Instance->HairGroupPublicData->GetGroupInstanceVertexCount();
	HairGroupCluster.GroupAABBBuffer = &Instance->HairGroupPublicData->GetGroupAABBBuffer();
	HairGroupCluster.ClusterAABBBuffer = &Instance->HairGroupPublicData->GetClusterAABBBuffer();

	HairGroupCluster.ClusterInfoBuffer = &Instance->Strands.ClusterCullingResource->ClusterInfoBuffer;
	HairGroupCluster.ClusterLODInfoBuffer = &Instance->Strands.ClusterCullingResource->ClusterLODInfoBuffer;
	HairGroupCluster.VertexToClusterIdBuffer = &Instance->Strands.ClusterCullingResource->VertexToClusterIdBuffer;
	HairGroupCluster.ClusterVertexIdBuffer = &Instance->Strands.ClusterCullingResource->ClusterVertexIdBuffer;

	HairGroupCluster.HairGroupPublicPtr = Instance->HairGroupPublicData;
	HairGroupCluster.LODBias  = Instance->HairGroupPublicData->GetLODBias();
	HairGroupCluster.LODIndex = Instance->HairGroupPublicData->GetLODIndex();
	HairGroupCluster.bVisible = Instance->HairGroupPublicData->GetLODVisibility();

	// These buffer are create during the culling pass
	// HairGroupCluster.ClusterIdBuffer = nullptr;
	// HairGroupCluster.ClusterIndexOffsetBuffer = nullptr;
	// HairGroupCluster.ClusterIndexCountBuffer = nullptr;

	HairGroupCluster.HairGroupPublicPtr->ClusterDataIndex = ClusterDataGroupIndex;
}

void ComputeHairStrandsInterpolation(
	FRDGBuilder& GraphBuilder,
	const FShaderDrawDebugData* ShaderDrawData,
	FHairGroupInstance* Instance,
	int32 MeshLODIndex,
	FHairStrandClusterData* InClusterData)
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

	if (!Instance) return;


	// Reset
	Instance->HairGroupPublicData->VFInput.Strands	= FHairGroupPublicData::FVertexFactoryInput::FStrands();
	Instance->HairGroupPublicData->VFInput.Cards	= FHairGroupPublicData::FVertexFactoryInput::FCards();
	Instance->HairGroupPublicData->VFInput.Meshes	= FHairGroupPublicData::FVertexFactoryInput::FMeshes();

	DECLARE_GPU_STAT(HairStrandsInterpolationCluster);
	RDG_EVENT_SCOPE(GraphBuilder, "HairStrandsInterpolationCluster");
	RDG_GPU_STAT_SCOPE(GraphBuilder, HairStrandsInterpolationCluster);

	// Debug mode:
	// * None	: Display hair normally
	// * Sim	: Show sim strands
	// * Render : Show rendering strands with sim color influence
	const EDeformationType DeformationType = GetDeformationType();
	const EHairStrandsDebugMode DebugMode = Instance->Debug.DebugMode != EHairStrandsDebugMode::NoneDebug ? Instance->Debug.DebugMode : GetHairStrandsDebugStrandsMode();
	const bool bDebugModePatchedAttributeBuffer = DebugMode == EHairStrandsDebugMode::RenderHairStrands || DebugMode == EHairStrandsDebugMode::RenderVisCluster;
	const bool bHasSimulationEnabled = Instance->Guides.bIsSimulationEnable && GHairStrandsInterpolateSimulation && DeformationType != EDeformationType::RestStrands;

	if (DeformationType != EDeformationType::RestStrands && DeformationType != EDeformationType::Simulation)
	{
		FBufferTransitionQueue TransitionQueue;
		AddDeformSimHairStrandsPass(
			GraphBuilder,
			DeformationType,
			MeshLODIndex,
			Instance->Guides.RestResource->GetVertexCount(),
			Instance->Guides.RestRootResource,
			Instance->Guides.DeformedRootResource,
			Instance->Guides.RestResource->RestPositionBuffer.SRV,
			Instance->Strands.InterpolationResource->SimRootPointIndexBuffer.SRV,
			Instance->Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current).UAV, 
			Instance->Guides.RestResource->PositionOffset,
			Instance->Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::Current),
			TransitionQueue,
			Instance->Guides.bHasGlobalInterpolation);
		TransitBufferToReadable(GraphBuilder, TransitionQueue);
	}

	if (DebugMode == EHairStrandsDebugMode::SimHairStrands)
	{
		FBufferTransitionQueue TransitionQueue;
		AddHairTangentPass(
			GraphBuilder,
			Instance->Guides.RestResource->GetVertexCount(),
			Instance->HairGroupPublicData,
			Instance->Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current).SRV,
			Instance->Guides.DeformedResource->TangentBuffer.UAV,
			TransitionQueue);

		Instance->HairGroupPublicData->VFInput.Strands.PositionBuffer		= Instance->Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::EFrameType::Current).SRV;
		Instance->HairGroupPublicData->VFInput.Strands.PrevPositionBuffer	= Instance->Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::EFrameType::Previous).SRV;
		Instance->HairGroupPublicData->VFInput.Strands.TangentBuffer		= Instance->Guides.DeformedResource->TangentBuffer.SRV;

		Instance->HairGroupPublicData->VFInput.Strands.AttributeBuffer		= Instance->Guides.RestResource->AttributeBuffer.SRV;
		Instance->HairGroupPublicData->VFInput.Strands.MaterialBuffer		= Instance->Guides.RestResource->MaterialBuffer.SRV;

		Instance->HairGroupPublicData->VFInput.Strands.PositionOffset			= Instance->Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::EFrameType::Current);
		Instance->HairGroupPublicData->VFInput.Strands.PrevPositionOffset		= Instance->Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::EFrameType::Previous);
		Instance->HairGroupPublicData->VFInput.Strands.VertexCount				= Instance->Guides.RestResource->GetVertexCount();
		Instance->HairGroupPublicData->VFInput.Strands.HairRadius				= (GStrandHairWidth > 0 ? GStrandHairWidth : Instance->Strands.Modifier.HairWidth) * 0.5f;
		Instance->HairGroupPublicData->VFInput.Strands.HairLength				= Instance->Strands.Modifier.HairLength;
		Instance->HairGroupPublicData->VFInput.Strands.HairDensity				= Instance->Strands.Modifier.HairShadowDensity;
		Instance->HairGroupPublicData->VFInput.Strands.bUseStableRasterization	= Instance->Strands.Modifier.bUseStableRasterization;
		Instance->HairGroupPublicData->VFInput.Strands.bScatterSceneLighting	= Instance->Strands.Modifier.bScatterSceneLighting;

		TransitBufferToReadable(GraphBuilder, TransitionQueue);
	}
	else if (Instance->GeometryType == EHairGeometryType::Strands)
	{
		{
			FBufferTransitionQueue TransitionQueue;
			check(InClusterData);
				 
			const uint32 VertexCount = Instance->Strands.RestResource->GetVertexCount();
			const uint32 BufferSizeInBytes = VertexCount * FHairStrandsAttributeFormat::SizeInByte;
			if (bDebugModePatchedAttributeBuffer && Instance->Strands.DebugAttributeBuffer.NumBytes != BufferSizeInBytes)
			{
				Instance->Strands.DebugAttributeBuffer.Release();
				Instance->Strands.DebugAttributeBuffer.Initialize(FHairStrandsAttributeFormat::SizeInByte, VertexCount, FHairStrandsAttributeFormat::Format, BUF_Static);
			}

			AddClearClusterAABBPass(
				GraphBuilder,
				Instance->HairGroupPublicData->GetClusterCount(),
				Instance->HairGroupPublicData->GetClusterAABBBuffer().UAV,
				Instance->HairGroupPublicData->GetGroupAABBBuffer().UAV,
				TransitionQueue);
			//TransitBufferToReadable(RHICmdList, TransitionQueue);
		}

		// Note: This code needs to exactly match the values FHairScaleAndClipDesc set int the previous loop.
		const float OutHairRadius = (GStrandHairWidth > 0 ? GStrandHairWidth : Instance->Strands.Modifier.HairWidth) * 0.5f;
		const float MaxOutHairRadius = OutHairRadius * FMath::Max(1.f, FMath::Max(Instance->Strands.Modifier.HairRootScale, Instance->Strands.Modifier.HairTipScale));
		{
			FBufferTransitionQueue TransitionQueue;
			{
				FHairScaleAndClipDesc ScaleAndClipDesc;
				ScaleAndClipDesc.bEnable				= true;
				ScaleAndClipDesc.InHairLength			= Instance->Strands.Data->StrandsCurves.MaxLength;
				ScaleAndClipDesc.InHairRadius			= Instance->Strands.Modifier.HairWidth * 0.5f;
				ScaleAndClipDesc.OutHairRadius			= (GStrandHairWidth > 0 ? GStrandHairWidth : Instance->Strands.Modifier.HairWidth) * 0.5f;
				ScaleAndClipDesc.MaxOutHairRadius		= ScaleAndClipDesc.OutHairRadius * FMath::Max(1.f, FMath::Max(Instance->Strands.Modifier.HairRootScale, Instance->Strands.Modifier.HairTipScale));
				ScaleAndClipDesc.HairRadiusRootScale	= Instance->Strands.Modifier.HairRootScale;
				ScaleAndClipDesc.HairRadiusTipScale		= Instance->Strands.Modifier.HairTipScale;
				ScaleAndClipDesc.HairLengthClip			= FMath::Clamp(Instance->Strands.Modifier.HairClipLength / Instance->Strands.Data->StrandsCurves.MaxLength, 0.f, 1.f);

				AddHairStrandsInterpolationPass(
					GraphBuilder,
					ShaderDrawData,
					Instance, 
					Instance->Strands.RestResource->GetVertexCount(),
					ScaleAndClipDesc,
					MeshLODIndex,
					bDebugModePatchedAttributeBuffer,
					TransitionQueue,
					Instance->Strands.HairInterpolationType,
					Instance->HairGroupPublicData,
					Instance->Strands.RestResource->PositionOffset,
					Instance->Guides.RestResource->PositionOffset,
					Instance->Strands.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::Current),
					Instance->Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::Current),
					Instance->Strands.RestRootResource,
					Instance->Guides.RestRootResource,
					Instance->Strands.DeformedRootResource,
					Instance->Guides.DeformedRootResource,
					Instance->Strands.RestResource->RestPositionBuffer.SRV,
					Instance->Strands.RestResource->AttributeBuffer.SRV,
					Instance->Strands.InterpolationResource->Interpolation0Buffer.SRV,
					Instance->Strands.InterpolationResource->Interpolation1Buffer.SRV,
					Instance->Guides.RestResource->RestPositionBuffer.SRV,
					Instance->Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current).SRV,
					Instance->Guides.RestResource->AttributeBuffer.SRV,
					Instance->Strands.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current).UAV,
					Instance->Strands.DebugAttributeBuffer.UAV,
					Instance->Strands.ClusterCullingResource->VertexToClusterIdBuffer.SRV,
					Instance->Strands.InterpolationResource->SimRootPointIndexBuffer.SRV);

			}
			TransitBufferToReadable(GraphBuilder, TransitionQueue);
		}

		{		
			FBufferTransitionQueue TransitionQueue;

			assert(Instance->HairGroupPublicPtr->ClusterDataIndex > 0);
			FHairStrandClusterData::FHairGroup& HairGroupCluster =  InClusterData->HairGroups[Instance->HairGroupPublicData->ClusterDataIndex];

			if (HairGroupCluster.bVisible)
			{
				AddHairClusterAABBPass(
					GraphBuilder,
					Instance->LocalToWorld,
					Instance->Strands.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::Current),
					HairGroupCluster,
					Instance->Strands.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current).SRV,
					TransitionQueue);
			}
			TransitBufferToReadable(GraphBuilder, TransitionQueue);
		}

		{
			FBufferTransitionQueue TransitionQueue;

			AddHairTangentPass(
				GraphBuilder,
				Instance->Strands.RestResource->GetVertexCount(),
				Instance->HairGroupPublicData,
				Instance->Strands.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current).SRV, //Output.VFInput.Strands.PositionBuffer,
				Instance->Strands.DeformedResource->TangentBuffer.UAV,// Output.RenderTangentBuffer->UAV,
				TransitionQueue);

			TransitBufferToReadable(GraphBuilder, TransitionQueue);
		}

		#if RHI_RAYTRACING
		if (IsHairRayTracingEnabled() && Instance->GeometryType == EHairGeometryType::Strands)
		{
			// #hair_todo: make it work again
			//FBufferTransitionQueue TransitionQueue;
			//FRDGBuilder GraphBuilder(RHICmdList);
			//// #hair_todo: move this somewhere else?
			//const float HairRadiusScaleRT = (GHairRaytracingRadiusScale > 0 ? GHairRaytracingRadiusScale : Instance->Strands.Modifier.HairRaytracingRadiusScale);
			//AddGenerateRaytracingGeometryPass(
			//	GraphBuilder,
			//	Instance->Strands.RestResource->GetVertexCount(),// Input.RenderVertexCount,
			//	MaxOutHairRadius* HairRadiusScaleRT,
			//	Instance->Strands.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::Current),// Input.OutHairPositionOffset,
			//	Instance->Strands.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current).SRV,// Output.VFInput.Strands.PositionBuffer,
			//	Instance->Strands.RenRaytracingResource->PositionBuffer.UAV,// Input.RaytracingPositionBuffer->UAV,
			//	TransitionQueue);
			//
			//	GraphBuilder.Execute();
			//	TransitBufferToReadable(RHICmdList, TransitionQueue);
			//
			//FRHIUnorderedAccessView* UAV = Input.RaytracingPositionBuffer->UAV;
			//RHICmdList.Transition(FRHITransitionInfo(Input.RaytracingPositionBuffer->UAV, ERHIAccess::Unknown, ERHIAccess::SRVCompute));
			//
			//const bool bNeedFullBuild = !Input.bIsRTGeometryInitialized;
			//if (bNeedFullBuild)
			//	BuildHairAccelerationStructure(RHICmdList, Input.RaytracingVertexCount, Input.RaytracingPositionBuffer->Buffer, Input.RaytracingGeometry);
			//else
			//	UpdateHairAccelerationStructure(RHICmdList, Input.RaytracingGeometry);
			//Input.bIsRTGeometryInitialized = true;
		}
		#endif

		Instance->HairGroupPublicData->VFInput.Strands.PositionBuffer		= Instance->Strands.DeformedResource->GetBuffer(FHairStrandsDeformedResource::EFrameType::Current).SRV;
		Instance->HairGroupPublicData->VFInput.Strands.PrevPositionBuffer	= Instance->Strands.DeformedResource->GetBuffer(FHairStrandsDeformedResource::EFrameType::Previous).SRV;
		Instance->HairGroupPublicData->VFInput.Strands.TangentBuffer		= Instance->Strands.DeformedResource->TangentBuffer.SRV;
		Instance->HairGroupPublicData->VFInput.Strands.AttributeBuffer		= bDebugModePatchedAttributeBuffer ? Instance->Strands.DebugAttributeBuffer.SRV : Instance->Strands.RestResource->AttributeBuffer.SRV;
		Instance->HairGroupPublicData->VFInput.Strands.MaterialBuffer		= Instance->Strands.RestResource->MaterialBuffer.SRV;

		Instance->HairGroupPublicData->VFInput.Strands.PositionOffset		= Instance->Strands.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::EFrameType::Current);
		Instance->HairGroupPublicData->VFInput.Strands.PrevPositionOffset	= Instance->Strands.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::EFrameType::Previous);
		Instance->HairGroupPublicData->VFInput.Strands.VertexCount			= Instance->Strands.RestResource->GetVertexCount();
		Instance->HairGroupPublicData->VFInput.Strands.HairRadius			= MaxOutHairRadius;
		Instance->HairGroupPublicData->VFInput.Strands.HairLength			= Instance->Strands.Modifier.HairLength;
		Instance->HairGroupPublicData->VFInput.Strands.HairDensity			= Instance->Strands.Modifier.HairShadowDensity;
		Instance->HairGroupPublicData->VFInput.Strands.bScatterSceneLighting= Instance->Strands.Modifier.bScatterSceneLighting;
		Instance->HairGroupPublicData->VFInput.Strands.bUseStableRasterization = Instance->Strands.Modifier.bUseStableRasterization;
	}
	else if (Instance->GeometryType == EHairGeometryType::Cards)
	{	
		const uint32 HairLODIndex = Instance->HairGroupPublicData->GetIntLODIndex();
		const bool bIsCardsValid = Instance->Cards.IsValid(HairLODIndex);
		if (bIsCardsValid)
		{
			FHairGroupInstance::FCards::FLOD& LOD = Instance->Cards.LODs[HairLODIndex];
			FBufferTransitionQueue TransitionQueue;
			{
				FHairScaleAndClipDesc ScaleAndClipDesc;
				ScaleAndClipDesc.bEnable = false;
				ScaleAndClipDesc.InHairLength = LOD.Guides.Data->StrandsCurves.MaxLength;
				ScaleAndClipDesc.InHairRadius = LOD.Guides.Data->StrandsCurves.MaxRadius;
				ScaleAndClipDesc.OutHairRadius = (GStrandHairWidth > 0 ? GStrandHairWidth * 0.5f : ScaleAndClipDesc.InHairRadius);
				ScaleAndClipDesc.MaxOutHairRadius = ScaleAndClipDesc.OutHairRadius;
				ScaleAndClipDesc.HairRadiusRootScale = 1;
				ScaleAndClipDesc.HairRadiusTipScale = 1;
				ScaleAndClipDesc.HairLengthClip = 1;

				AddHairStrandsInterpolationPass(
					GraphBuilder,
					ShaderDrawData,
					Instance,
					LOD.Guides.RestResource->GetVertexCount(),
					ScaleAndClipDesc,
					MeshLODIndex,
					false,
					TransitionQueue,
					LOD.Guides.HairInterpolationType,
					Instance->HairGroupPublicData,
					LOD.Guides.RestResource->PositionOffset,
					Instance->Guides.RestResource->PositionOffset,
					LOD.Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::Current),
					Instance->Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::Current),
					LOD.Guides.RestRootResource,
					Instance->Guides.RestRootResource,
					LOD.Guides.DeformedRootResource,
					Instance->Guides.DeformedRootResource,
					LOD.Guides.RestResource->RestPositionBuffer.SRV,
					LOD.Guides.RestResource->AttributeBuffer.SRV,
					LOD.Guides.InterpolationResource->Interpolation0Buffer.SRV,
					LOD.Guides.InterpolationResource->Interpolation1Buffer.SRV,
					Instance->Guides.RestResource->RestPositionBuffer.SRV,
					Instance->Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current).SRV,
					Instance->Guides.RestResource->AttributeBuffer.SRV,
					LOD.Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current).UAV,
					nullptr,
					nullptr,
					LOD.Guides.InterpolationResource->SimRootPointIndexBuffer.SRV);

			}
			TransitBufferToReadable(GraphBuilder, TransitionQueue);
		}

		// Deform cards geometry
		if (bIsCardsValid)
		{
			FBufferTransitionQueue TransitionQueue;
		
			AddHairCardsDeformationPass(
				GraphBuilder,
				Instance,
				TransitionQueue);
		
			TransitBufferToReadable(GraphBuilder, TransitionQueue);
		}
	}
	else if (Instance->GeometryType == EHairGeometryType::Meshes)
	{
		// Not needed
	}

	Instance->HairGroupPublicData->VFInput.GeometryType = Instance->GeometryType;
	Instance->HairGroupPublicData->VFInput.LocalToWorldTransform = Instance->LocalToWorld;
	Instance->HairGroupPublicData->bSupportVoxelization = Instance->Strands.Modifier.bSupportVoxelization;
}

void ResetHairStrandsInterpolation(
	FRDGBuilder& GraphBuilder,
	FHairGroupInstance* Instance,
	int32 MeshLODIndex)
{
	if (!Instance || (Instance && Instance->Guides.bIsSimulationEnable)) return;

	DECLARE_GPU_STAT(HairStrandsResetInterpolation);
	RDG_EVENT_SCOPE(GraphBuilder, "HairStrandsResetInterpolation");
	RDG_GPU_STAT_SCOPE(GraphBuilder, HairStrandsResetInterpolation);

	FBufferTransitionQueue TransitionQueue;
	AddDeformSimHairStrandsPass(
		GraphBuilder,
		EDeformationType::OffsetGuide,
		MeshLODIndex,
		Instance->Guides.RestResource->GetVertexCount(),
		Instance->Guides.RestRootResource,
		Instance->Guides.DeformedRootResource,
		Instance->Guides.RestResource->RestPositionBuffer.SRV,
		Instance->Strands.InterpolationResource->SimRootPointIndexBuffer.SRV,
		Instance->Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current).UAV, 
		Instance->Guides.RestResource->PositionOffset,
		Instance->Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::Current), 
		TransitionQueue,
		Instance->Guides.bHasGlobalInterpolation);
	TransitBufferToReadable(GraphBuilder, TransitionQueue);
}
