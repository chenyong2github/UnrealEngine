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
#include "GroomCache.h"
#include "HairStrandsInterface.h"
#include "SceneView.h"
#include "Containers/ResourceArray.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "HAL/ConsoleManager.h"
#include "GpuDebugRendering.h"
#include "Async/ParallelFor.h"
#include "RenderTargetPool.h"
#include "GroomTextureBuilder.h"
#include "GroomBindingBuilder.h"
#include "GroomAsset.h" 
#include "GroomManager.h"
#include "GroomInstance.h"

static int32 GHairDeformationType = 0;
static FAutoConsoleVariableRef CVarHairDeformationType(TEXT("r.HairStrands.DeformationType"), GHairDeformationType, TEXT("Type of procedural deformation applied on hair strands (0:use simulation's output, 1:use rest strands, 2: use rest guides, 3:wave pattern, 4:follow root normal)"));

static float GHairRaytracingRadiusScale = 0;
static FAutoConsoleVariableRef CVarHairRaytracingRadiusScale(TEXT("r.HairStrands.RaytracingRadiusScale"), GHairRaytracingRadiusScale, TEXT("Override the per instance scale factor for raytracing hair strands geometry (0: disabled, >0:enabled)"));

static float GStrandHairWidth = 0.0f;
static FAutoConsoleVariableRef CVarStrandHairWidth(TEXT("r.HairStrands.StrandWidth"), GStrandHairWidth, TEXT("Width of hair strand"));

static int32 GStrandHairInterpolationDebug = 0;
static FAutoConsoleVariableRef CVarStrandHairInterpolationDebug(TEXT("r.HairStrands.Interpolation.Debug"), GStrandHairInterpolationDebug, TEXT("Enable debug rendering for hair interpolation"));

static int32 GHairStrandsUseSingleGuideInterpolation = 0;
static FAutoConsoleVariableRef CVarHairStrandsUseSingleGuideInterpolation(TEXT("r.HairStrands.Interpolation.UseSingleGuide"), GHairStrandsUseSingleGuideInterpolation, TEXT("Hair interpolation will use a single guide for interpolating hair motion instead of 3. Save performance cost"), ECVF_Scalability | ECVF_RenderThreadSafe);

static int32 GHairCardsInterpolationType = 1;
static FAutoConsoleVariableRef CVarHairCardsInterpolationType(TEXT("r.HairStrands.Cards.InterpolationType"), GHairCardsInterpolationType, TEXT("Hair cards interpolation type: 0: None, 1:physics simulation, 2: RBF deformation"));

static int32 GHairStrandsTransferPositionOnLODChange = 0;
static FAutoConsoleVariableRef CVarHairStrandsTransferPositionOnLODChange(TEXT("r.HairStrands.Strands.TransferPrevPos"), GHairStrandsTransferPositionOnLODChange, TEXT("Transfer strands prev. position to current position on LOD switching to avoid large discrepancy causing large motion vector"));

enum class EHairCardsSimulationType
{
	None,
	Guide,
	RBF
};

EHairCardsSimulationType GetHairCardsSimulationType()
{
	return GHairCardsInterpolationType >= 2 ? 
		EHairCardsSimulationType::RBF : 
		(GHairCardsInterpolationType >= 1 ? EHairCardsSimulationType::Guide : EHairCardsSimulationType::None);
}

bool IsHairLODSimulationEnabled(const int32 LODIndex);

bool NeedsUpdateCardsMeshTriangles()
{
	return GetHairCardsSimulationType() == EHairCardsSimulationType::Guide;
}

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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FTransferVelocityPassCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FTransferVelocityPassCS);
	SHADER_USE_PARAMETER_STRUCT(FTransferVelocityPassCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ElementCount)
		SHADER_PARAMETER(uint32, DispatchCountX)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::All, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_HAIRTRANSFER_PREV_POSITION"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FTransferVelocityPassCS, "/Engine/Private/HairStrands/HairStrandsInterpolation.usf", "MainCS", SF_Compute);

static void AddTransferPositionPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const uint32 ElementCount,
	FRDGBufferSRVRef InBuffer,
	FRDGBufferUAVRef OutBuffer)
{
	if (ElementCount == 0) return;

	const uint32 GroupSize = 64;
	const uint32 DispatchCount = FMath::DivideAndRoundUp(ElementCount, GroupSize);
	const uint32 DispatchCountX = 128;
	const uint32 DispatchCountY = FMath::DivideAndRoundUp(DispatchCount, DispatchCountX);

	FTransferVelocityPassCS::FParameters* Parameters = GraphBuilder.AllocParameters<FTransferVelocityPassCS::FParameters>();
	Parameters->ElementCount = ElementCount;
	Parameters->InBuffer = InBuffer;
	Parameters->OutBuffer = OutBuffer;

	TShaderMapRef<FTransferVelocityPassCS> ComputeShader(ShaderMap);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("TransferPositionForVelocity"),
		ComputeShader,
		Parameters,
		FIntVector(DispatchCountX, DispatchCountY, 1));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FGroomCacheUpdatePassCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FGroomCacheUpdatePassCS);
	SHADER_USE_PARAMETER_STRUCT(FGroomCacheUpdatePassCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, ElementCount)
		SHADER_PARAMETER(uint32, DispatchCountX)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InAnimatedBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InRestPoseBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, InDeformedOffsetBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutDeformedBuffer)
		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::All, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_GROOMCACHE_UPDATE"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FGroomCacheUpdatePassCS, "/Engine/Private/HairStrands/HairStrandsInterpolation.usf", "MainCS", SF_Compute);

static void AddGroomCacheUpdatePass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	uint32 ElementCount,
	const FGroomCacheGroupData& GroomCacheData,
	FRDGBufferSRVRef InBuffer,
	FRDGBufferSRVRef InDeformedOffsetBuffer,
	FRDGBufferUAVRef OutBuffer
	)
{
	if (ElementCount == 0) return;

	const uint32 GroupSize = 64;
	const FIntVector DispatchCount = ComputeDispatchCount(ElementCount, GroupSize);

	FRDGBufferRef VertexBuffer = nullptr;

	const uint32 DataCount = GroomCacheData.VertexData.PointsPosition.Num();
	const uint32 DataSizeInBytes = sizeof(FVector) * DataCount;
	if (DataSizeInBytes != 0)
	{
		// Deformation are upload into a Buffer<float> as the original position are float3 which is is both
		// 1) incompatible with structure buffer alignment (128bits), and 2) incompatible with vertex buffer 
		// as R32G32B32_FLOAT format is not well supported for SRV accross HW.
		// So instead the positions are uploaded into vertex buffer Buffer<float>
		VertexBuffer = CreateVertexBuffer(
			GraphBuilder,
			TEXT("GroomCache_PositionBuffer"),
			FRDGBufferDesc::CreateBufferDesc(sizeof(float), GroomCacheData.VertexData.PointsPosition.Num() * 3),
			GroomCacheData.VertexData.PointsPosition.GetData(),
			DataSizeInBytes,
			ERDGInitialDataFlags::None);
	}
	else
	{
		return;
	}

	FGroomCacheUpdatePassCS::FParameters* Parameters = GraphBuilder.AllocParameters<FGroomCacheUpdatePassCS::FParameters>();
	Parameters->DispatchCountX = DispatchCount.X;
	Parameters->ElementCount = ElementCount;
	Parameters->InAnimatedBuffer = GraphBuilder.CreateSRV(VertexBuffer, PF_R32_FLOAT);
	Parameters->InRestPoseBuffer = InBuffer;
	Parameters->InDeformedOffsetBuffer = InDeformedOffsetBuffer;
	Parameters->OutDeformedBuffer = OutBuffer;

	TShaderMapRef<FGroomCacheUpdatePassCS> ComputeShader(ShaderMap);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("GroomCacheUpdate"),
		ComputeShader,
		Parameters,
		DispatchCount);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
		SHADER_PARAMETER(uint32, DispatchCountX)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, SimRestPosition0Buffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, SimRestPosition1Buffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, SimRestPosition2Buffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, SimDeformedPosition0Buffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, SimDeformedPosition1Buffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, SimDeformedPosition2Buffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, SimRootBarycentricBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, SimVertexToRootIndexBuffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, SimDeformedOffsetBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, SimRestPosePositionBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutSimDeformedPositionBuffer)

		SHADER_PARAMETER(uint32, SampleCount)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RestSamplePositionsBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, MeshSampleWeightsBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::All, Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FDeformGuideCS, "/Engine/Private/HairStrands/HairStrandsGuideDeform.usf", "MainCS", SF_Compute);

static void AddDeformSimHairStrandsPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	EDeformationType DeformationType,
	const uint32 MeshLODIndex,
	const uint32 VertexCount,
	FHairStrandsRestRootResource* SimRestRootResources,
	FHairStrandsDeformedRootResource* SimDeformedRootResources,
	FRDGBufferSRVRef SimRestPosePositionBuffer,
	FRDGImportedBuffer OutSimDeformedPositionBuffer,
	const FVector& SimRestOffset,
	FRDGBufferSRVRef SimDeformedOffsetBuffer,
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
	Parameters->OutSimDeformedPositionBuffer = OutSimDeformedPositionBuffer.UAV;
	Parameters->VertexCount = VertexCount;
	Parameters->IterationCount = IterationCount % 10000;
	Parameters->SimDeformedOffsetBuffer = SimDeformedOffsetBuffer;
	Parameters->SimRestOffset = SimRestOffset;
	Parameters->DispatchCountX = DispatchCountX;

	if (DeformationType == EDeformationType::OffsetGuide)
	{
		const bool bIsVertexToCurveBuffersValid = SimRestRootResources && SimRestRootResources->VertexToCurveIndexBuffer.Buffer != nullptr;
		if (bIsVertexToCurveBuffersValid)
		{
			Parameters->SimVertexToRootIndexBuffer = RegisterAsSRV(GraphBuilder, SimRestRootResources->VertexToCurveIndexBuffer);
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
				Parameters->SimRestPosition0Buffer = RegisterAsSRV(GraphBuilder, RestLODDatas.RestRootTrianglePosition0Buffer);
				Parameters->SimRestPosition1Buffer = RegisterAsSRV(GraphBuilder, RestLODDatas.RestRootTrianglePosition1Buffer);
				Parameters->SimRestPosition2Buffer = RegisterAsSRV(GraphBuilder, RestLODDatas.RestRootTrianglePosition2Buffer);

				Parameters->SimDeformedPosition0Buffer = RegisterAsSRV(GraphBuilder, DeformedLODDatas.DeformedRootTrianglePosition0Buffer);
				Parameters->SimDeformedPosition1Buffer = RegisterAsSRV(GraphBuilder, DeformedLODDatas.DeformedRootTrianglePosition1Buffer);
				Parameters->SimDeformedPosition2Buffer = RegisterAsSRV(GraphBuilder, DeformedLODDatas.DeformedRootTrianglePosition2Buffer);

				Parameters->SimRootBarycentricBuffer = RegisterAsSRV(GraphBuilder, RestLODDatas.RootTriangleBarycentricBuffer);
			}
			else
			{
				InternalDeformationType = 5;
				Parameters->MeshSampleWeightsBuffer = RegisterAsSRV(GraphBuilder, DeformedLODDatas.MeshSampleWeightsBuffer);
				Parameters->RestSamplePositionsBuffer = RegisterAsSRV(GraphBuilder, RestLODDatas.RestSamplePositionsBuffer);
				Parameters->SampleCount = RestLODDatas.SampleCount;
			}
		}
	}

	FDeformGuideCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FDeformGuideCS::FGroupSize>(GetGroupSizePermutation(GroupSize));
	PermutationVector.Set<FDeformGuideCS::FDeformationType>(InternalDeformationType);

	TShaderMapRef<FDeformGuideCS> ComputeShader(ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("DeformSimHairStrands"),
		ComputeShader,
		Parameters,
		FIntVector(DispatchCountX, DispatchCountY, 1));

	GraphBuilder.SetBufferAccessFinal(OutSimDeformedPositionBuffer.Buffer, ERHIAccess::SRVMask);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

struct FRDGHairStrandsCullingData
{
	bool bCullingResultAvailable = false;
	FRDGImportedBuffer HairStrandsVF_CullingIndirectBuffer;
	FRDGImportedBuffer HairStrandsVF_CullingIndexBuffer;
	FRDGImportedBuffer HairStrandsVF_CullingRadiusScaleBuffer;

	uint32 ClusterCount = 0;
	FRDGImportedBuffer ClusterAABBBuffer;
	FRDGImportedBuffer GroupAABBBuffer;
};

FRDGHairStrandsCullingData ImportCullingData(FRDGBuilder& GraphBuilder, FHairGroupPublicData* In)
{
	FRDGHairStrandsCullingData Out;
	Out.bCullingResultAvailable					= In->GetCullingResultAvailable();

	Out.HairStrandsVF_CullingIndirectBuffer		= Register(GraphBuilder, In->GetDrawIndirectRasterComputeBuffer(), ERDGImportedBufferFlags::CreateViews);
	Out.HairStrandsVF_CullingIndexBuffer		= Register(GraphBuilder, In->GetCulledVertexIdBuffer(), ERDGImportedBufferFlags::CreateViews);
	Out.HairStrandsVF_CullingRadiusScaleBuffer	= Register(GraphBuilder, In->GetCulledVertexRadiusScaleBuffer(), ERDGImportedBufferFlags::CreateViews);

	Out.ClusterCount		= In->GetClusterCount();
	Out.ClusterAABBBuffer	= Register(GraphBuilder, In->GetClusterAABBBuffer(), ERDGImportedBufferFlags::CreateViews);
	Out.GroupAABBBuffer		= Register(GraphBuilder, In->GetGroupAABBBuffer(), ERDGImportedBufferFlags::CreateViews);

	return Out;
}

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
	class FSingleGuide : SHADER_PERMUTATION_INT("PERMUTATION_USE_SINGLE_GUIDE", 2);
	class FCulling : SHADER_PERMUTATION_INT("PERMUTATION_CULLING", 2);
	using FPermutationDomain = TShaderPermutationDomain<FGroupSize, FDebug, FDynamicGeometry, FSimulation, FSingleGuide, FCulling>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
		SHADER_PARAMETER(uint32, VertexCount)
		SHADER_PARAMETER(uint32, DispatchCountX)
		SHADER_PARAMETER(uint32, HairDebugMode)
		SHADER_PARAMETER(FVector, InRenderHairPositionOffset)
		SHADER_PARAMETER(FVector, InSimHairPositionOffset)
		SHADER_PARAMETER(FIntPoint, HairStrandsCullIndex)

		SHADER_PARAMETER(float, InHairLength)
		SHADER_PARAMETER(float, InHairRadius)
		SHADER_PARAMETER(float, OutHairRadius)
		SHADER_PARAMETER(float, MaxOutHairRadius)
		SHADER_PARAMETER(float, HairRadiusRootScale)
		SHADER_PARAMETER(float, HairRadiusTipScale)
		SHADER_PARAMETER(float, HairLengthClip)
		SHADER_PARAMETER(uint32, HairLengthClipEnable)
		SHADER_PARAMETER(uint32,  HairStrandsVF_bIsCullingEnable)

		SHADER_PARAMETER(FMatrix, LocalToWorldMatrix)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RenderRestPosePositionBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutRenderDeformedPositionBuffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, VertexToClusterIdBuffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, SimRestPosePositionBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, DeformedSimPositionBuffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, Interpolation0Buffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, Interpolation1Buffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, AttributeBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, SimAttributeBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutRenderAttributeBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, RestPosition0Buffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, RestPosition1Buffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, RestPosition2Buffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, DeformedPosition0Buffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, DeformedPosition1Buffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, DeformedPosition2Buffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, RootBarycentricBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, RenVertexToRootIndexBuffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, SimRestPosition0Buffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, SimRestPosition1Buffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, SimRestPosition2Buffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, SimDeformedPosition0Buffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, SimDeformedPosition1Buffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, SimDeformedPosition2Buffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, SimRootBarycentricBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, SimVertexToRootIndexBuffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, SimRootPointIndexBuffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, OutSimHairPositionOffsetBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, OutRenHairPositionOffsetBuffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, HairStrandsVF_CullingIndirectBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, HairStrandsVF_CullingIndexBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float>, HairStrandsVF_CullingRadiusScaleBuffer)
		SHADER_PARAMETER_RDG_BUFFER(Buffer<uint>, HairStrandsVF_CullingIndirectBufferArgs)
		
		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::All, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_HAIRINTERPOLATION"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairInterpolationCS, "/Engine/Private/HairStrands/HairStrandsInterpolation.usf", "MainCS", SF_Compute);

static void AddHairStrandsInterpolationPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const FShaderDrawDebugData* ShaderDrawData,
	FHairGroupInstance* Instance,
	const uint32 VertexCount,
	const FHairScaleAndClipDesc ScaleAndClipDesc,
	const int32 MeshLODIndex,
	const bool bPatchedAttributeBuffer, 	
	const uint32 HairInterpolationType,
	const EHairGeometryType InstanceGeometryType,
	FRDGHairStrandsCullingData& CullingData,
	const FVector& InRenderHairWorldOffset,
	const FVector& InSimHairWorldOffset,
	const FRDGBufferSRVRef& OutRenHairPositionOffsetBuffer,
	const FRDGBufferSRVRef& OutSimHairPositionOffsetBuffer,
	const FHairStrandsRestRootResource* RenRestRootResources,
	const FHairStrandsRestRootResource* SimRestRootResources,
	const FHairStrandsDeformedRootResource* RenDeformedRootResources,
	const FHairStrandsDeformedRootResource* SimDeformedRootResources,
	const FRDGBufferSRVRef& RenderRestPosePositionBuffer,
	const FRDGBufferSRVRef& RenderAttributeBuffer,
	const FRDGBufferSRVRef& Interpolation0Buffer,
	const FRDGBufferSRVRef& Interpolation1Buffer,
	const FRDGBufferSRVRef& SimRestPosePositionBuffer,
	const FRDGBufferSRVRef& SimDeformedPositionBuffer,
	const FRDGBufferSRVRef& SimAttributeBuffer,
	FRDGImportedBuffer& OutRenderPositionBuffer,
	FRDGImportedBuffer* OutRenderPrevPositionBuffer,
	FRDGImportedBuffer* OutRenderAttributeBuffer,
	const FRDGBufferSRVRef& VertexToClusterIdBuffer,
	const FRDGBufferSRVRef& SimRootPointIndexBuffer)
{
	const uint32 GroupSize = ComputeGroupSize();
	const FIntVector DispatchCount = ComputeDispatchCount(VertexCount, GroupSize);


	FHairInterpolationCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairInterpolationCS::FParameters>();
	Parameters->RenderRestPosePositionBuffer = RenderRestPosePositionBuffer;
	Parameters->SimRestPosePositionBuffer = SimRestPosePositionBuffer;
	Parameters->DeformedSimPositionBuffer = SimDeformedPositionBuffer;
	Parameters->Interpolation0Buffer = Interpolation0Buffer;
	Parameters->Interpolation1Buffer = Interpolation1Buffer;
	Parameters->OutRenderDeformedPositionBuffer = OutRenderPositionBuffer.UAV;
	Parameters->HairStrandsCullIndex = FIntPoint(-1, -1);
	Parameters->VertexCount = VertexCount;
	Parameters->InRenderHairPositionOffset = InRenderHairWorldOffset;
	Parameters->InSimHairPositionOffset =  InSimHairWorldOffset;

	Parameters->OutSimHairPositionOffsetBuffer = OutSimHairPositionOffsetBuffer;
	Parameters->OutRenHairPositionOffsetBuffer = OutRenHairPositionOffsetBuffer;

	Parameters->DispatchCountX = DispatchCount.X;
	Parameters->SimRootPointIndexBuffer = SimRootPointIndexBuffer;
	
	Parameters->InHairLength = ScaleAndClipDesc.InHairLength;
	Parameters->InHairRadius = ScaleAndClipDesc.InHairRadius;
	Parameters->OutHairRadius = ScaleAndClipDesc.OutHairRadius;
	Parameters->MaxOutHairRadius = ScaleAndClipDesc.MaxOutHairRadius;
	Parameters->HairRadiusRootScale = ScaleAndClipDesc.HairRadiusRootScale;
	Parameters->HairRadiusTipScale = ScaleAndClipDesc.HairRadiusTipScale;
	Parameters->HairLengthClip = ScaleAndClipDesc.HairLengthClip * ScaleAndClipDesc.InHairLength;
	Parameters->HairLengthClipEnable = ScaleAndClipDesc.IsEnable() ? 1 : 0;
	Parameters->AttributeBuffer = RenderAttributeBuffer;	
	
	const bool bIsVertexToCurveBuffersValid =
		SimRestRootResources &&
		SimRestRootResources->VertexToCurveIndexBuffer.Buffer != nullptr &&
		RenRestRootResources &&
		RenRestRootResources->VertexToCurveIndexBuffer.Buffer != nullptr;

	if (bIsVertexToCurveBuffersValid)
	{
		Parameters->RenVertexToRootIndexBuffer = RegisterAsSRV(GraphBuilder, RenRestRootResources->VertexToCurveIndexBuffer);
		Parameters->SimVertexToRootIndexBuffer = RegisterAsSRV(GraphBuilder, SimRestRootResources->VertexToCurveIndexBuffer);
	}

	Parameters->VertexToClusterIdBuffer = VertexToClusterIdBuffer;
	
	Parameters->LocalToWorldMatrix = Instance->LocalToWorld.ToMatrixWithScale();

	// Debug rendering
	Parameters->HairDebugMode = 0;
	{
		const FHairCullInfo Info = GetHairStrandsCullInfo();
		const bool bCullingEnable = Info.CullMode != EHairCullMode::None && bIsVertexToCurveBuffersValid;

		if (bPatchedAttributeBuffer && OutRenderAttributeBuffer)
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

		if (Parameters->HairDebugMode > 0 && OutRenderAttributeBuffer)
		{
			Parameters->SimAttributeBuffer = SimAttributeBuffer;
			Parameters->OutRenderAttributeBuffer = OutRenderAttributeBuffer->UAV;
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
			Parameters->RestPosition0Buffer = RegisterAsSRV(GraphBuilder, Ren_RestLODDatas.RestRootTrianglePosition0Buffer);
			Parameters->RestPosition1Buffer = RegisterAsSRV(GraphBuilder, Ren_RestLODDatas.RestRootTrianglePosition1Buffer);
			Parameters->RestPosition2Buffer = RegisterAsSRV(GraphBuilder, Ren_RestLODDatas.RestRootTrianglePosition2Buffer);

			Parameters->RootBarycentricBuffer = RegisterAsSRV(GraphBuilder, Ren_RestLODDatas.RootTriangleBarycentricBuffer);

			Parameters->SimRestPosition0Buffer = RegisterAsSRV(GraphBuilder, Sim_RestLODDatas.RestRootTrianglePosition0Buffer);
			Parameters->SimRestPosition1Buffer = RegisterAsSRV(GraphBuilder, Sim_RestLODDatas.RestRootTrianglePosition1Buffer);
			Parameters->SimRestPosition2Buffer = RegisterAsSRV(GraphBuilder, Sim_RestLODDatas.RestRootTrianglePosition2Buffer);

			Parameters->SimRootBarycentricBuffer = RegisterAsSRV(GraphBuilder, Sim_RestLODDatas.RootTriangleBarycentricBuffer);
		}
		{
			Parameters->DeformedPosition0Buffer = RegisterAsSRV(GraphBuilder, Ren_DeformedLODDatas.DeformedRootTrianglePosition0Buffer);
			Parameters->DeformedPosition1Buffer = RegisterAsSRV(GraphBuilder, Ren_DeformedLODDatas.DeformedRootTrianglePosition1Buffer);
			Parameters->DeformedPosition2Buffer = RegisterAsSRV(GraphBuilder, Ren_DeformedLODDatas.DeformedRootTrianglePosition2Buffer);

			Parameters->SimDeformedPosition0Buffer = RegisterAsSRV(GraphBuilder, Sim_DeformedLODDatas.DeformedRootTrianglePosition0Buffer);
			Parameters->SimDeformedPosition1Buffer = RegisterAsSRV(GraphBuilder, Sim_DeformedLODDatas.DeformedRootTrianglePosition1Buffer);
			Parameters->SimDeformedPosition2Buffer = RegisterAsSRV(GraphBuilder, Sim_DeformedLODDatas.DeformedRootTrianglePosition2Buffer);
		}
	}

	if (ShaderDrawDebug::IsShaderDrawDebugEnabled() && ShaderDrawData)
	{
		ShaderDrawDebug::SetParameters(GraphBuilder, *ShaderDrawData, Parameters->ShaderDrawParameters);
	}

	const bool bUseSingleGuide = GHairStrandsUseSingleGuideInterpolation > 0;
	const bool bHasLocalDeformation = (Instance->Guides.bIsSimulationEnable && ((MeshLODIndex < 0) || ((MeshLODIndex >= 0) && IsHairLODSimulationEnabled(MeshLODIndex)))) || bSupportGlobalInterpolation;
	const bool bCullingEnable = InstanceGeometryType == EHairGeometryType::Strands && CullingData.bCullingResultAvailable;
	Parameters->HairStrandsVF_bIsCullingEnable = bCullingEnable ? 1 : 0;

	FHairInterpolationCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairInterpolationCS::FGroupSize>(GroupSize);
	PermutationVector.Set<FHairInterpolationCS::FDebug>(Parameters->HairDebugMode > 0 ? 1 : 0);
	PermutationVector.Set<FHairInterpolationCS::FDynamicGeometry>((bSupportDynamicMesh && bHasLocalDeformation) ? HairInterpolationType+1 :
							(bSupportDynamicMesh && !bHasLocalDeformation) ? 1 : 0);
	PermutationVector.Set<FHairInterpolationCS::FSimulation>(bHasLocalDeformation ? 1 : 0);
	PermutationVector.Set<FHairInterpolationCS::FSingleGuide>(bUseSingleGuide ? 1 : 0);
	PermutationVector.Set<FHairInterpolationCS::FCulling>(bCullingEnable ? 1 : 0);

	TShaderMapRef<FHairInterpolationCS> ComputeShader(ShaderMap, PermutationVector);

	if (bCullingEnable)
	{
		Parameters->HairStrandsVF_CullingIndirectBuffer = CullingData.HairStrandsVF_CullingIndirectBuffer.SRV;
		Parameters->HairStrandsVF_CullingIndexBuffer = CullingData.HairStrandsVF_CullingIndexBuffer.SRV;
		Parameters->HairStrandsVF_CullingRadiusScaleBuffer = CullingData.HairStrandsVF_CullingRadiusScaleBuffer.SRV;
		Parameters->HairStrandsVF_CullingIndirectBufferArgs = CullingData.HairStrandsVF_CullingIndirectBuffer.Buffer;

		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("HairStrandsInterpolation(culling=on)"),
			ComputeShader, 
			Parameters,
			CullingData.HairStrandsVF_CullingIndirectBuffer.Buffer,
			0);
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

	GraphBuilder.SetBufferAccessFinal(OutRenderPositionBuffer.Buffer, ERHIAccess::SRVMask);
	if (OutRenderAttributeBuffer)
	{
		GraphBuilder.SetBufferAccessFinal(OutRenderAttributeBuffer->Buffer, ERHIAccess::SRVMask);
	}

	if (Instance->HairGroupPublicData->VFInput.bHasLODSwitch && OutRenderPrevPositionBuffer && GHairStrandsTransferPositionOnLODChange > 0)
	{
		AddTransferPositionPass(GraphBuilder, ShaderMap, VertexCount, OutRenderPositionBuffer.SRV, OutRenderPrevPositionBuffer->UAV);
		GraphBuilder.SetBufferAccessFinal(OutRenderPrevPositionBuffer->Buffer, ERHIAccess::SRVMask);
	}
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
		SHADER_PARAMETER(FMatrix, LocalToWorldMatrix)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RenderDeformedPositionBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, RenderDeformedOffsetBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, ClusterVertexIdBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, ClusterIdBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, ClusterIndexOffsetBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, ClusterIndexCountBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, HairStrandsVF_CullingIndirectBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutClusterAABBBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutGroupAABBBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_CLUSTERAABB"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FHairClusterAABBCS, "/Engine/Private/HairStrands/HairStrandsInterpolation.usf", "ClusterAABBEvaluationCS", SF_Compute);

static void AddHairClusterAABBPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const FTransform& InRenLocalToWorld,
	FRDGBufferSRVRef RenderDeformedOffsetBuffer,
	FHairStrandClusterData::FHairGroup& ClusterData,

	FRDGHairStrandsCullingData& ClusterAABBData,
	FRDGImportedBuffer& RenderPositionBuffer,
	FRDGImportedBuffer& DrawIndirectRasterComputeBuffer)
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
	Parameters->RenderDeformedPositionBuffer = RenderPositionBuffer.SRV;
	Parameters->RenderDeformedOffsetBuffer = RenderDeformedOffsetBuffer;
	Parameters->ClusterVertexIdBuffer = RegisterAsSRV(GraphBuilder, *ClusterData.ClusterVertexIdBuffer);
	Parameters->ClusterIdBuffer = GraphBuilder.CreateSRV(ClusterIdBuffer, PF_R32_UINT);
	Parameters->ClusterIndexOffsetBuffer = GraphBuilder.CreateSRV(ClusterIndexOffsetBuffer, PF_R32_UINT);
	Parameters->ClusterIndexCountBuffer = GraphBuilder.CreateSRV(ClusterIndexCountBuffer, PF_R32_UINT);
	Parameters->HairStrandsVF_CullingIndirectBuffer = DrawIndirectRasterComputeBuffer.SRV; // Used for checking max vertex count
	Parameters->OutClusterAABBBuffer = ClusterAABBData.ClusterAABBBuffer.UAV;
	Parameters->OutGroupAABBBuffer = ClusterAABBData.GroupAABBBuffer.UAV;

	// Sanity check
	check(ClusterData.ClusterCount == ClusterIdBuffer->Desc.NumElements);
	check(ClusterData.ClusterCount == ClusterIndexOffsetBuffer->Desc.NumElements);
	check(ClusterData.ClusterCount == ClusterIndexCountBuffer->Desc.NumElements);
	check(ClusterData.ClusterCount * 6 == ClusterAABBData.ClusterAABBBuffer.Buffer->Desc.NumElements);

	FHairClusterAABBCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairClusterAABBCS::FGroupSize>(GroupSize);
	TShaderMapRef<FHairClusterAABBCS> ComputeShader(ShaderMap, PermutationVector);

	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsClusterAABB"),
		ComputeShader,
		Parameters,
		DispatchCount);

	GraphBuilder.SetBufferAccessFinal(ClusterAABBData.ClusterAABBBuffer.Buffer, ERHIAccess::SRVMask);
	GraphBuilder.SetBufferAccessFinal(ClusterAABBData.GroupAABBBuffer.Buffer, ERHIAccess::SRVMask);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairCardsDeformationCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairCardsDeformationCS);
	SHADER_USE_PARAMETER_STRUCT(FHairCardsDeformationCS, FGlobalShader);

	class FGroupSize : SHADER_PERMUTATION_INT("PERMUTATION_GROUP_SIZE", 2);
	class FDynamicGeometry : SHADER_PERMUTATION_INT("PERMUTATION_DYNAMIC_GEOMETRY", 2);
	using FPermutationDomain = TShaderPermutationDomain<FGroupSize, FDynamicGeometry>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, CardsVertexCount)
		SHADER_PARAMETER(uint32, GuideVertexCount)
		SHADER_PARAMETER(FVector, GuideRestPositionOffset)
		SHADER_PARAMETER(FVector, GuideDeformedPositionOffset)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, GuideRestPositionBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, GuideDeformedPositionBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, GuideDeformedPositionOffsetBuffer)
		SHADER_PARAMETER_SRV(Buffer, CardsRestPositionBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, CardsInterpolationBuffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, TriangleRestPosition0Buffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, TriangleRestPosition1Buffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, TriangleRestPosition2Buffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, TriangleDeformedPosition0Buffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, TriangleDeformedPosition1Buffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, TriangleDeformedPosition2Buffer)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, GuideRootBarycentricBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, GuideVertexToRootIndexBuffer)


		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, CardsDeformedPositionBuffer)


		SHADER_PARAMETER_STRUCT_INCLUDE(ShaderDrawDebug::FShaderDrawDebugParameters, ShaderDrawParameters)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Cards, Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FHairCardsDeformationCS, "/Engine/Private/HairStrands/HairCardsDeformation.usf", "MainCS", SF_Compute);

static void AddHairCardsDeformationPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	const FShaderDrawDebugData* ShaderDrawData,
	FHairGroupInstance* Instance,
	const int32 MeshLODIndex)
{
	const int32 HairLODIndex = Instance->HairGroupPublicData->GetIntLODIndex();
	if (!Instance->Cards.IsValid(HairLODIndex))
		return;

	FHairGroupInstance::FCards::FLOD& LOD = Instance->Cards.LODs[HairLODIndex];
	
	FRDGImportedBuffer CardsDeformedPositionBuffer = Register(GraphBuilder, LOD.DeformedResource->GetBuffer(FHairCardsDeformedResource::Current), ERDGImportedBufferFlags::CreateUAV);

	FHairCardsDeformationCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairCardsDeformationCS::FParameters>();
	Parameters->GuideVertexCount			= LOD.Guides.RestResource->GetVertexCount();
	Parameters->GuideRestPositionOffset		= LOD.Guides.RestResource->PositionOffset;
	Parameters->GuideDeformedPositionOffset = LOD.Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::Current);
	Parameters->GuideRestPositionBuffer		= RegisterAsSRV(GraphBuilder, LOD.Guides.RestResource->RestPositionBuffer);
	Parameters->GuideDeformedPositionBuffer = RegisterAsSRV(GraphBuilder, LOD.Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current));
	Parameters->GuideDeformedPositionOffsetBuffer = RegisterAsSRV(GraphBuilder, LOD.Guides.DeformedResource->GetPositionOffsetBuffer(FHairStrandsDeformedResource::Current));

	Parameters->CardsVertexCount			= LOD.RestResource->VertexCount;
	Parameters->CardsRestPositionBuffer		= LOD.RestResource->RestPositionBuffer.ShaderResourceViewRHI;
	Parameters->CardsDeformedPositionBuffer = CardsDeformedPositionBuffer.UAV;

	Parameters->CardsInterpolationBuffer	= RegisterAsSRV(GraphBuilder, LOD.InterpolationResource->InterpolationBuffer);

	const FHairStrandsRestRootResource* RestRootResources = LOD.Guides.RestRootResource;
	const FHairStrandsDeformedRootResource* DeformedRootResources = LOD.Guides.DeformedRootResource;

	const bool bIsVertexToCurveBuffersValid = RestRootResources && RestRootResources->VertexToCurveIndexBuffer.Buffer != nullptr;
	const bool bSupportDynamicMesh =
		bIsVertexToCurveBuffersValid &&
		RestRootResources &&
		RestRootResources->RootData.RootCount > 0 &&
		MeshLODIndex >= 0 &&
		MeshLODIndex < RestRootResources->LODs.Num() &&
		MeshLODIndex < DeformedRootResources->LODs.Num() &&
		RestRootResources->LODs[MeshLODIndex].IsValid() &&
		DeformedRootResources->LODs[MeshLODIndex].IsValid();
	if (bSupportDynamicMesh)
	{
		Parameters->GuideVertexToRootIndexBuffer = RegisterAsSRV(GraphBuilder, RestRootResources->VertexToCurveIndexBuffer);
		const FHairStrandsRestRootResource::FLOD& RestLODDatas = RestRootResources->LODs[MeshLODIndex];
		const FHairStrandsDeformedRootResource::FLOD& DeformedLODDatas = DeformedRootResources->LODs[MeshLODIndex];

		Parameters->GuideRootBarycentricBuffer = RegisterAsSRV(GraphBuilder, RestLODDatas.RootTriangleBarycentricBuffer);

		Parameters->TriangleRestPosition0Buffer = RegisterAsSRV(GraphBuilder, RestLODDatas.RestRootTrianglePosition0Buffer);
		Parameters->TriangleRestPosition1Buffer = RegisterAsSRV(GraphBuilder, RestLODDatas.RestRootTrianglePosition1Buffer);
		Parameters->TriangleRestPosition2Buffer = RegisterAsSRV(GraphBuilder, RestLODDatas.RestRootTrianglePosition2Buffer);

		Parameters->TriangleDeformedPosition0Buffer = RegisterAsSRV(GraphBuilder, DeformedLODDatas.DeformedRootTrianglePosition0Buffer);
		Parameters->TriangleDeformedPosition1Buffer = RegisterAsSRV(GraphBuilder, DeformedLODDatas.DeformedRootTrianglePosition1Buffer);
		Parameters->TriangleDeformedPosition2Buffer = RegisterAsSRV(GraphBuilder, DeformedLODDatas.DeformedRootTrianglePosition2Buffer);
	}

	if (ShaderDrawDebug::IsShaderDrawDebugEnabled() && ShaderDrawData)
	{
		ShaderDrawDebug::SetParameters(GraphBuilder, *ShaderDrawData, Parameters->ShaderDrawParameters);
	}

	const uint32 GroupSize = ComputeGroupSize();
	FHairCardsDeformationCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairCardsDeformationCS::FDynamicGeometry>(bSupportDynamicMesh ? 1 : 0);
	PermutationVector.Set<FHairCardsDeformationCS::FGroupSize>(GetGroupSizePermutation(GroupSize));

	TShaderMapRef<FHairCardsDeformationCS> ComputeShader(ShaderMap, PermutationVector);

	const int32 DispatchCountX = FMath::DivideAndRoundUp(Parameters->CardsVertexCount, GroupSize);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairCardsDeformation"),
		ComputeShader,
		Parameters,
		FIntVector(DispatchCountX,1,1));

	GraphBuilder.SetBufferAccessFinal(CardsDeformedPositionBuffer.Buffer, ERHIAccess::SRVMask);
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
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, PositionBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>,	HairStrandsVF_CullingIndirectBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>,	HairStrandsVF_CullingIndexBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutputTangentBuffer)
		SHADER_PARAMETER_RDG_BUFFER(Buffer, IndirectBufferArgs)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FHairTangentCS, "/Engine/Private/HairStrands/HairStrandsTangent.usf", "MainCS", SF_Compute);

static void AddHairTangentPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	uint32 VertexCount,
	FHairGroupPublicData* HairGroupPublicData,
	FRDGBufferSRVRef PositionBuffer,
	FRDGImportedBuffer OutTangentBuffer)
{
	const uint32 GroupSize = ComputeGroupSize();
	const FIntVector DispatchCount = ComputeDispatchCount(VertexCount, GroupSize);
	const bool bCullingEnable = HairGroupPublicData->GetCullingResultAvailable();

	FHairTangentCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairTangentCS::FParameters>();
	Parameters->PositionBuffer = PositionBuffer;
	Parameters->OutputTangentBuffer = OutTangentBuffer.UAV;
	Parameters->VertexCount = VertexCount;
	Parameters->DispatchCountX = DispatchCount.X;
	Parameters->HairStrandsVF_bIsCullingEnable = bCullingEnable ? 1 : 0;

	FHairTangentCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairTangentCS::FGroupSize>(GetGroupSizePermutation(GroupSize));
	PermutationVector.Set<FHairTangentCS::FCulling>(bCullingEnable ? 1 : 0);

	TShaderMapRef<FHairTangentCS> ComputeShader(ShaderMap, PermutationVector);

	if (bCullingEnable)
	{
		FRDGImportedBuffer DrawIndirectRasterComputeBuffer  = Register(GraphBuilder, HairGroupPublicData->GetDrawIndirectRasterComputeBuffer(), ERDGImportedBufferFlags::CreateSRV);
		Parameters->HairStrandsVF_CullingIndirectBuffer		= DrawIndirectRasterComputeBuffer.SRV;
		Parameters->HairStrandsVF_CullingIndexBuffer		= RegisterAsSRV(GraphBuilder, HairGroupPublicData->GetCulledVertexIdBuffer());
		Parameters->IndirectBufferArgs						= DrawIndirectRasterComputeBuffer.Buffer;
		
		FComputeShaderUtils::AddPass(
			GraphBuilder,
			RDG_EVENT_NAME("HairStrandsTangent(culling=on)"),
			ComputeShader,
			Parameters,
			DrawIndirectRasterComputeBuffer.Buffer, 0);
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

	GraphBuilder.SetBufferAccessFinal(OutTangentBuffer.Buffer, ERHIAccess::SRVMask);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FHairRaytracingGeometryCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FHairRaytracingGeometryCS);
	SHADER_USE_PARAMETER_STRUCT(FHairRaytracingGeometryCS, FGlobalShader);

	class FGroupSize : SHADER_PERMUTATION_INT("PERMUTATION_GROUP_SIZ", 2);
	class FCulling : SHADER_PERMUTATION_INT("PERMUTATION_CULLING", 2);
	using FPermutationDomain = TShaderPermutationDomain<FGroupSize, FCulling>;

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, VertexCount)
		SHADER_PARAMETER(uint32, DispatchCountX)
		SHADER_PARAMETER(float, StrandHairRadius)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, PositionOffsetBuffer)
		SHADER_PARAMETER(uint32, HairStrandsVF_bIsCullingEnable)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer,	HairStrandsVF_CullingIndirectBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer,	HairStrandsVF_CullingIndexBuffer)
		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer,	HairStrandsVF_CullingRadiusScaleBuffer)
		SHADER_PARAMETER_RDG_BUFFER(Buffer,	HairStrandsVF_CullingIndirectBufferArgs)

		SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer, PositionBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutputPositionBuffer)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
};

IMPLEMENT_GLOBAL_SHADER(FHairRaytracingGeometryCS, "/Engine/Private/HairStrands/HairStrandsRaytracingGeometry.usf", "MainCS", SF_Compute);

static void AddGenerateRaytracingGeometryPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	uint32 VertexCount,
	float HairRadius,
	const FRDGBufferSRVRef& HairWorldOffsetBuffer,
	FRDGHairStrandsCullingData& CullingData,
	const FRDGBufferSRVRef& PositionBuffer,
	const FRDGBufferUAVRef& OutPositionBuffer)
{
	const uint32 GroupSize = ComputeGroupSize();
	const FIntVector DispatchCount = ComputeDispatchCount(VertexCount, GroupSize);

	FHairRaytracingGeometryCS::FParameters* Parameters = GraphBuilder.AllocParameters<FHairRaytracingGeometryCS::FParameters>();
	Parameters->VertexCount = VertexCount;
	Parameters->DispatchCountX = DispatchCount.X;
	Parameters->PositionOffsetBuffer = HairWorldOffsetBuffer;
	Parameters->StrandHairRadius = HairRadius;
	Parameters->PositionBuffer = PositionBuffer;
	Parameters->OutputPositionBuffer = OutPositionBuffer;

	const bool bCullingEnable = CullingData.bCullingResultAvailable;
	Parameters->HairStrandsVF_bIsCullingEnable = bCullingEnable ? 1 : 0;
	if (bCullingEnable)
	{
		Parameters->HairStrandsVF_CullingIndirectBuffer = CullingData.HairStrandsVF_CullingIndirectBuffer.SRV;
		Parameters->HairStrandsVF_CullingIndexBuffer = CullingData.HairStrandsVF_CullingIndexBuffer.SRV;
		Parameters->HairStrandsVF_CullingRadiusScaleBuffer = CullingData.HairStrandsVF_CullingRadiusScaleBuffer.SRV;
		Parameters->HairStrandsVF_CullingIndirectBufferArgs = CullingData.HairStrandsVF_CullingIndirectBuffer.Buffer;
	}

	FHairRaytracingGeometryCS::FPermutationDomain PermutationVector;
	PermutationVector.Set<FHairRaytracingGeometryCS::FGroupSize>(GetGroupSizePermutation(GroupSize));
	PermutationVector.Set<FHairRaytracingGeometryCS::FCulling>(bCullingEnable ? 1 : 0);

	TShaderMapRef<FHairRaytracingGeometryCS> ComputeShader(ShaderMap, PermutationVector);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsRaytracingGeometry"),
		ComputeShader,
		Parameters,
		DispatchCount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

class FClearClusterAABBCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FClearClusterAABBCS);
	SHADER_USE_PARAMETER_STRUCT(FClearClusterAABBCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutClusterAABBBuffer)
		SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer, OutGroupAABBBuffer)
		SHADER_PARAMETER(uint32, ClusterCount)
	END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return IsHairStrandsSupported(EHairStrandsShaderType::Strands, Parameters.Platform); }
	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("SHADER_CLEARCLUSTERAABB"), 1);
	}
};

IMPLEMENT_GLOBAL_SHADER(FClearClusterAABBCS, "/Engine/Private/HairStrands/HairStrandsClusterCulling.usf", "MainClearClusterAABBCS", SF_Compute);

static void AddClearClusterAABBPass(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	uint32 ClusterCount,
	FRDGImportedBuffer& OutClusterAABBBuffer,
	FRDGImportedBuffer& OutGroupAABBBuffer)
{
	check(OutClusterAABBBuffer.Buffer);

	FClearClusterAABBCS::FParameters* Parameters = GraphBuilder.AllocParameters<FClearClusterAABBCS::FParameters>();
	Parameters->ClusterCount = ClusterCount;
	Parameters->OutClusterAABBBuffer = OutClusterAABBBuffer.UAV;
	Parameters->OutGroupAABBBuffer = OutGroupAABBBuffer.UAV;

	TShaderMapRef<FClearClusterAABBCS> ComputeShader(ShaderMap);

	const FIntVector DispatchCount = DispatchCount.DivideAndRoundUp(FIntVector(ClusterCount * 6, 1, 1) , FIntVector(64, 1, 1));
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("HairStrandsClearClusterAABB"),
		ComputeShader,
		Parameters,
		DispatchCount);

	GraphBuilder.SetBufferAccessFinal(OutClusterAABBBuffer.Buffer, ERHIAccess::SRVMask),
	GraphBuilder.SetBufferAccessFinal(OutGroupAABBBuffer.Buffer, ERHIAccess::SRVMask);
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

static void BuildHairAccelerationStructure_Strands(FRHICommandList& RHICmdList, uint32 RaytracingVertexCount, FVertexBufferRHIRef& PositionBuffer, FRayTracingGeometry* OutRayTracingGeometry)
{
	FRayTracingGeometryInitializer Initializer;
	static const FName DebugName("HairStrands");
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
	OutRayTracingGeometry->CreateRayTracingGeometry(ERTAccelerationStructureBuildPriority::Immediate);
}

static void BuildHairAccelerationStructure_Cards(FRHICommandList& RHICmdList, 	
	FHairCardsRestResource* RestResource,
	FHairCardsDeformedResource* DeformedResource,
	FRayTracingGeometry* OutRayTracingGeometry)
{
	FRayTracingGeometryInitializer Initializer;
	static const FName DebugName("HairCards");
	static int32 DebugNumber = 0;
	Initializer.DebugName = FName(DebugName, DebugNumber++);
	Initializer.IndexBuffer = RestResource->RestIndexBuffer.IndexBufferRHI;
	Initializer.IndexBufferOffset = 0;
	Initializer.GeometryType = RTGT_Triangles;
	Initializer.TotalPrimitiveCount = RestResource->PrimitiveCount;
	Initializer.bFastBuild = true;
	Initializer.bAllowUpdate = true;

	FVertexBufferRHIRef PositionBuffer(DeformedResource->GetBuffer(FHairCardsDeformedResource::Current).Buffer->GetVertexBufferRHI()); // This will likely flicker result in half speed motion as everyother frame will use the wrong buffer

	FRayTracingGeometrySegment Segment;
	Segment.VertexBuffer = PositionBuffer;
	Segment.VertexBufferStride = FHairCardsPositionFormat::SizeInByte;
	Segment.VertexBufferElementType = FHairCardsPositionFormat::VertexElementType;
	Segment.NumPrimitives = RestResource->PrimitiveCount;
	Initializer.Segments.Add(Segment);

	OutRayTracingGeometry->SetInitializer(Initializer);
	OutRayTracingGeometry->CreateRayTracingGeometry(ERTAccelerationStructureBuildPriority::Immediate);
}

static void BuildHairAccelerationStructure_Meshes(FRHICommandList& RHICmdList,
	FHairMeshesRestResource* RestResource,
	FHairMeshesDeformedResource* DeformedResource,
	FRayTracingGeometry* OutRayTracingGeometry)
{
	FRayTracingGeometryInitializer Initializer;
	static const FName DebugName("HairCards");
	static int32 DebugNumber = 0;
	Initializer.DebugName = FName(DebugName, DebugNumber++);
	Initializer.IndexBuffer = RestResource->IndexBuffer.IndexBufferRHI;
	Initializer.IndexBufferOffset = 0;
	Initializer.GeometryType = RTGT_Triangles;
	Initializer.TotalPrimitiveCount = RestResource->PrimitiveCount;
	Initializer.bFastBuild = true;
	Initializer.bAllowUpdate = true;

	FVertexBufferRHIRef PositionBuffer(DeformedResource->GetBuffer(FHairMeshesDeformedResource::Current).Buffer->GetVertexBufferRHI()); // This will likely flicker result in half speed motion as everyother frame will use the wrong buffer

	FRayTracingGeometrySegment Segment;
	Segment.VertexBuffer = PositionBuffer;
	Segment.VertexBufferStride = FHairCardsPositionFormat::SizeInByte;
	Segment.VertexBufferElementType = FHairCardsPositionFormat::VertexElementType;
	Segment.NumPrimitives = RestResource->PrimitiveCount;
	Initializer.Segments.Add(Segment);

	OutRayTracingGeometry->SetInitializer(Initializer);
	OutRayTracingGeometry->CreateRayTracingGeometry(ERTAccelerationStructureBuildPriority::Immediate);
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

EHairStrandsDebugMode GetHairStrandsGeometryDebugMode(FHairGroupInstance* Instance)
{
	return Instance->Debug.DebugMode != EHairStrandsDebugMode::NoneDebug ? Instance->Debug.DebugMode : GetHairStrandsDebugStrandsMode();
}

FHairScaleAndClipDesc ComputeHairScaleAndClipDesc(FHairGroupInstance* Instance)
{
	FHairScaleAndClipDesc Out;
	Out.bEnable = true;
	Out.InHairLength = Instance->Strands.Data->StrandsCurves.MaxLength;
	Out.InHairRadius = Instance->Strands.Modifier.HairWidth * 0.5f;
	Out.OutHairRadius = (GStrandHairWidth > 0 ? GStrandHairWidth : Instance->Strands.Modifier.HairWidth) * 0.5f;
	Out.MaxOutHairRadius = Out.OutHairRadius * FMath::Max(1.f, FMath::Max(Instance->Strands.Modifier.HairRootScale, Instance->Strands.Modifier.HairTipScale));
	Out.HairRadiusRootScale = Instance->Strands.Modifier.HairRootScale;
	Out.HairRadiusTipScale = Instance->Strands.Modifier.HairTipScale;
	Out.HairLengthClip = FMath::Clamp(Instance->Strands.Modifier.HairClipScale, 0.f, 1.f);
	
	return Out;
}

bool NeedsPatchAttributeBuffer(EHairStrandsDebugMode DebugMode)
{
	return DebugMode == EHairStrandsDebugMode::RenderHairStrands || DebugMode == EHairStrandsDebugMode::RenderVisCluster;
}

FHairGroupPublicData::FVertexFactoryInput ComputeHairStrandsVertexInputData(FHairGroupInstance* Instance)
{
	FHairGroupPublicData::FVertexFactoryInput OutVFInput;
	if (!Instance || Instance->GeometryType != EHairGeometryType::Strands)
		return OutVFInput;

	const FHairScaleAndClipDesc ScaleAndClipDesc = ComputeHairScaleAndClipDesc(Instance);

	const EHairStrandsDebugMode DebugMode = GetHairStrandsGeometryDebugMode(Instance);
	const bool bDebugModePatchedAttributeBuffer = NeedsPatchAttributeBuffer(DebugMode);

	if (DebugMode == EHairStrandsDebugMode::SimHairStrands)
	{
		OutVFInput.Strands.PositionBuffer = Instance->Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::EFrameType::Current).SRV;
		OutVFInput.Strands.PrevPositionBuffer = Instance->Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::EFrameType::Previous).SRV;
		OutVFInput.Strands.TangentBuffer = Instance->Guides.DeformedResource->TangentBuffer.SRV;
		OutVFInput.Strands.AttributeBuffer = Instance->Guides.RestResource->AttributeBuffer.SRV;
		OutVFInput.Strands.MaterialBuffer = Instance->Guides.RestResource->MaterialBuffer.SRV;

		OutVFInput.Strands.PositionOffset = Instance->Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::EFrameType::Current);
		OutVFInput.Strands.PrevPositionOffset = Instance->Guides.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::EFrameType::Previous);

		OutVFInput.Strands.PositionOffsetBuffer = Instance->Guides.DeformedResource->GetPositionOffsetBuffer(FHairStrandsDeformedResource::EFrameType::Current).SRV;
		OutVFInput.Strands.PrevPositionOffsetBuffer = Instance->Guides.DeformedResource->GetPositionOffsetBuffer(FHairStrandsDeformedResource::EFrameType::Previous).SRV;

		OutVFInput.Strands.VertexCount = Instance->Guides.RestResource->GetVertexCount();
		OutVFInput.Strands.HairRadius = (GStrandHairWidth > 0 ? GStrandHairWidth : Instance->Strands.Modifier.HairWidth) * 0.5f;
		OutVFInput.Strands.HairLength = Instance->Strands.Modifier.HairLength;
		OutVFInput.Strands.HairDensity = Instance->Strands.Modifier.HairShadowDensity;
		OutVFInput.Strands.bUseStableRasterization = Instance->Strands.Modifier.bUseStableRasterization;
		OutVFInput.Strands.bScatterSceneLighting = Instance->Strands.Modifier.bScatterSceneLighting;
	}
	else
	{
		OutVFInput.Strands.PositionBuffer = Instance->Strands.DeformedResource->GetBuffer(FHairStrandsDeformedResource::EFrameType::Current).SRV;
		OutVFInput.Strands.PrevPositionBuffer = Instance->Strands.DeformedResource->GetBuffer(FHairStrandsDeformedResource::EFrameType::Previous).SRV;
		OutVFInput.Strands.TangentBuffer = Instance->Strands.DeformedResource->TangentBuffer.SRV;
		OutVFInput.Strands.AttributeBuffer = bDebugModePatchedAttributeBuffer ? Instance->Strands.DebugAttributeBuffer.SRV : Instance->Strands.RestResource->AttributeBuffer.SRV;
		OutVFInput.Strands.MaterialBuffer = Instance->Strands.RestResource->MaterialBuffer.SRV;

		OutVFInput.Strands.PositionOffsetBuffer = Instance->Strands.DeformedResource->GetPositionOffsetBuffer(FHairStrandsDeformedResource::EFrameType::Current).SRV;
		OutVFInput.Strands.PrevPositionOffsetBuffer = Instance->Strands.DeformedResource->GetPositionOffsetBuffer(FHairStrandsDeformedResource::EFrameType::Previous).SRV;

		OutVFInput.Strands.PositionOffset = Instance->Strands.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::EFrameType::Current);
		OutVFInput.Strands.PrevPositionOffset = Instance->Strands.DeformedResource->GetPositionOffset(FHairStrandsDeformedResource::EFrameType::Previous);
		OutVFInput.Strands.VertexCount = Instance->Strands.RestResource->GetVertexCount();
		OutVFInput.Strands.HairRadius = ScaleAndClipDesc.MaxOutHairRadius;
		OutVFInput.Strands.HairLength = Instance->Strands.Modifier.HairLength;
		OutVFInput.Strands.HairDensity = Instance->Strands.Modifier.HairShadowDensity;
		OutVFInput.Strands.bScatterSceneLighting = Instance->Strands.Modifier.bScatterSceneLighting;
		OutVFInput.Strands.bUseStableRasterization = Instance->Strands.Modifier.bUseStableRasterization;
	}

	return OutVFInput;
}

void AddBufferTransitionToReadablePass(FRDGBuilder& GraphBuilder, FRHIUnorderedAccessView* UAV)
{
	AddPass(GraphBuilder, [UAV](FRHICommandList& RHICmdList)
	{
		RHICmdList.Transition(FRHITransitionInfo(UAV, ERHIAccess::UAVCompute, ERHIAccess::SRVMask));
	});
}

void ComputeHairStrandsInterpolation(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
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
	const EHairGeometryType InstanceGeometryType = Instance->GeometryType;

	DECLARE_GPU_STAT(HairStrandsInterpolation);
	RDG_EVENT_SCOPE(GraphBuilder, "HairStrandsInterpolation");
	RDG_GPU_STAT_SCOPE(GraphBuilder, HairStrandsInterpolation);

	// Debug mode:
	// * None	: Display hair normally
	// * Sim	: Show sim strands
	// * Render : Show rendering strands with sim color influence
	const EDeformationType DeformationType = GetDeformationType();
	const EHairStrandsDebugMode DebugMode = GetHairStrandsGeometryDebugMode(Instance);
	const bool bDebugModePatchedAttributeBuffer = NeedsPatchAttributeBuffer(DebugMode);

	if (DeformationType != EDeformationType::RestStrands && DeformationType != EDeformationType::Simulation)
	{
		AddDeformSimHairStrandsPass(
			GraphBuilder,
			ShaderMap,
			DeformationType,
			MeshLODIndex,
			Instance->Guides.RestResource->GetVertexCount(),
			Instance->Guides.RestRootResource,
			Instance->Guides.DeformedRootResource,
			RegisterAsSRV(GraphBuilder, Instance->Guides.RestResource->RestPositionBuffer),
			Register(GraphBuilder, Instance->Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current), ERDGImportedBufferFlags::CreateUAV),
			Instance->Guides.RestResource->PositionOffset,
			RegisterAsSRV(GraphBuilder, Instance->Guides.DeformedResource->GetPositionOffsetBuffer(FHairStrandsDeformedResource::Current)),
			Instance->Guides.bHasGlobalInterpolation);
	}

	if (DebugMode == EHairStrandsDebugMode::SimHairStrands && InstanceGeometryType == EHairGeometryType::Strands)
	{
		if (Instance->Guides.DeformedResource->NeedsToUpdateTangent())
		{
			AddHairTangentPass(
				GraphBuilder,
				ShaderMap,
				Instance->Guides.RestResource->GetVertexCount(),
				Instance->HairGroupPublicData,
				RegisterAsSRV(GraphBuilder, Instance->Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current)),
				Register(GraphBuilder, Instance->Guides.DeformedResource->TangentBuffer, ERDGImportedBufferFlags::CreateUAV));
		}

		Instance->HairGroupPublicData->VFInput = ComputeHairStrandsVertexInputData(Instance);
	}
	else if (InstanceGeometryType == EHairGeometryType::Strands)
	{

		FRDGHairStrandsCullingData CullingData = ImportCullingData(GraphBuilder, Instance->HairGroupPublicData);

		{
			AddClearClusterAABBPass(
				GraphBuilder,
				ShaderMap,
				CullingData.ClusterCount,
				CullingData.ClusterAABBBuffer,
				CullingData.GroupAABBBuffer);
		}

		FRDGImportedBuffer Strands_DeformedPosition		= Register(GraphBuilder, Instance->Strands.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current), ERDGImportedBufferFlags::CreateViews);
		FRDGImportedBuffer Strands_DeformedPrevPosition = Register(GraphBuilder, Instance->Strands.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Previous), ERDGImportedBufferFlags::CreateViews);
		FRDGImportedBuffer Strands_DeformedTangent		= Register(GraphBuilder, Instance->Strands.DeformedResource->TangentBuffer, ERDGImportedBufferFlags::CreateViews);		
		#if WITH_EDITOR
		FRDGImportedBuffer Strands_DebugAttributeReg	= Register(GraphBuilder, Instance->Strands.DebugAttributeBuffer, ERDGImportedBufferFlags::CreateUAV);
		FRDGImportedBuffer*Strands_DebugAttribute		= &Strands_DebugAttributeReg;
		#else
		FRDGImportedBuffer*Strands_DebugAttribute		= nullptr;
		#endif

		// Note: This code needs to exactly match the values FHairScaleAndClipDesc set int the previous loop.
		const FHairScaleAndClipDesc ScaleAndClipDesc = ComputeHairScaleAndClipDesc(Instance);
		Instance->HairGroupPublicData->VFInput = ComputeHairStrandsVertexInputData(Instance);
		{
			bool bHasValidGroomCacheBuffers = Instance->Debug.GroomCacheBuffers.IsValid() && Instance->Debug.GroomCacheBuffers->GetInterpolatedFrameBuffer().GroupsData.IsValidIndex(Instance->Debug.GroupIndex);
			if (bHasValidGroomCacheBuffers && Instance->Debug.GroomCacheType == EGroomCacheType::Guides)
			{
				FScopeLock Lock(Instance->Debug.GroomCacheBuffers->GetCriticalSection());
				const FGroomCacheGroupData& GroomCacheGroupData = Instance->Debug.GroomCacheBuffers->GetInterpolatedFrameBuffer().GroupsData[Instance->Debug.GroupIndex];

				Instance->Guides.DeformedResource->SetPositionOffset(FHairStrandsDeformedResource::EFrameType::Current, GroomCacheGroupData.BoundingBox.GetCenter());

				AddHairStrandUpdatePositionOffsetPass(
					GraphBuilder,
					ShaderMap,
					MeshLODIndex,
					Instance->Guides.DeformedRootResource,
					Instance->Guides.DeformedResource);

				// Pass to upload GroomCache guide positions
				AddGroomCacheUpdatePass(
					GraphBuilder,
					ShaderMap,
					Instance->Guides.RestResource->GetVertexCount(),
					GroomCacheGroupData,
					RegisterAsSRV(GraphBuilder, Instance->Guides.RestResource->RestPositionBuffer),
					RegisterAsSRV(GraphBuilder, Instance->Guides.DeformedResource->GetPositionOffsetBuffer(FHairStrandsDeformedResource::EFrameType::Current)),
					RegisterAsUAV(GraphBuilder, Instance->Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current)));
			}

			if (Instance->Debug.GroomCacheType != EGroomCacheType::Strands)
			{
				AddHairStrandsInterpolationPass(
					GraphBuilder,
					ShaderMap,
					ShaderDrawData,
					Instance, 
					Instance->Strands.RestResource->GetVertexCount(),
					ScaleAndClipDesc,
					MeshLODIndex,
					bDebugModePatchedAttributeBuffer,
					Instance->Strands.HairInterpolationType,
					InstanceGeometryType,
					CullingData,
					Instance->Strands.RestResource->PositionOffset,
					Instance->Guides.RestResource->PositionOffset,
					RegisterAsSRV(GraphBuilder, Instance->Strands.DeformedResource->GetPositionOffsetBuffer(FHairStrandsDeformedResource::Current)),
					RegisterAsSRV(GraphBuilder, Instance->Guides.DeformedResource->GetPositionOffsetBuffer(FHairStrandsDeformedResource::Current)),
					Instance->Strands.RestRootResource,
					Instance->Guides.RestRootResource,
					Instance->Strands.DeformedRootResource,
					Instance->Guides.DeformedRootResource,
					RegisterAsSRV(GraphBuilder, Instance->Strands.RestResource->RestPositionBuffer),
					RegisterAsSRV(GraphBuilder, Instance->Strands.RestResource->AttributeBuffer),
					RegisterAsSRV(GraphBuilder, Instance->Strands.InterpolationResource->Interpolation0Buffer),
					RegisterAsSRV(GraphBuilder, Instance->Strands.InterpolationResource->Interpolation1Buffer),
					RegisterAsSRV(GraphBuilder, Instance->Guides.RestResource->RestPositionBuffer),
					RegisterAsSRV(GraphBuilder, Instance->Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current)),
					RegisterAsSRV(GraphBuilder, Instance->Guides.RestResource->AttributeBuffer),
					Strands_DeformedPosition,
					&Strands_DeformedPrevPosition, 
					Strands_DebugAttribute,
					RegisterAsSRV(GraphBuilder, Instance->Strands.ClusterCullingResource->VertexToClusterIdBuffer),
					RegisterAsSRV(GraphBuilder, Instance->Strands.InterpolationResource->SimRootPointIndexBuffer));

			}
			else if (bHasValidGroomCacheBuffers)
			{
				FScopeLock Lock(Instance->Debug.GroomCacheBuffers->GetCriticalSection());
				const FGroomCacheGroupData& GroomCacheGroupData = Instance->Debug.GroomCacheBuffers->GetInterpolatedFrameBuffer().GroupsData[Instance->Debug.GroupIndex];

				Instance->Strands.DeformedResource->SetPositionOffset(FHairStrandsDeformedResource::EFrameType::Current, GroomCacheGroupData.BoundingBox.GetCenter());

				AddHairStrandUpdatePositionOffsetPass(
					GraphBuilder,
					ShaderMap,
					MeshLODIndex,
					Instance->Strands.DeformedRootResource,
					Instance->Strands.DeformedResource);

				// Pass to upload GroomCache strands positions
				AddGroomCacheUpdatePass(
					GraphBuilder,
					ShaderMap,
					Instance->Strands.RestResource->GetVertexCount(),
					GroomCacheGroupData,
					RegisterAsSRV(GraphBuilder, Instance->Strands.RestResource->RestPositionBuffer),
					RegisterAsSRV(GraphBuilder, Instance->Strands.DeformedResource->GetPositionOffsetBuffer(FHairStrandsDeformedResource::EFrameType::Current)),
					Strands_DeformedPosition.UAV
					);
			}
		}

		{		
			FHairStrandClusterData::FHairGroup& HairGroupCluster =  InClusterData->HairGroups[Instance->HairGroupPublicData->ClusterDataIndex];
			if (HairGroupCluster.bVisible)
			{
				// Optim: If an instance is using CPU selection and does not voxelize it's data, then there is no need for having valid AABB
				const bool bNeedAABB = !Instance->bUseCPULODSelection || (Instance->Strands.Modifier.bSupportVoxelization && Instance->bCastShadow);

				if (bNeedAABB)
				{
					FRDGImportedBuffer Strands_CulledVertexCount = Register(GraphBuilder, Instance->HairGroupPublicData->GetDrawIndirectRasterComputeBuffer(), ERDGImportedBufferFlags::CreateSRV);
					AddHairClusterAABBPass(
						GraphBuilder,
						ShaderMap,
						Instance->LocalToWorld,
						RegisterAsSRV(GraphBuilder, Instance->Strands.DeformedResource->GetPositionOffsetBuffer(FHairStrandsDeformedResource::EFrameType::Current)),
						HairGroupCluster,
						CullingData,
						Strands_DeformedPosition,
						Strands_CulledVertexCount);
				}
			}
		}

		if (Instance->Strands.DeformedResource->NeedsToUpdateTangent())
		{
			AddHairTangentPass(
				GraphBuilder,
				ShaderMap,
				Instance->Strands.RestResource->GetVertexCount(),
				Instance->HairGroupPublicData,
				Strands_DeformedPosition.SRV,
				Strands_DeformedTangent);
		}

		#if RHI_RAYTRACING
		if (Instance->Strands.RenRaytracingResource)
		{
			const float HairRadiusScaleRT = (GHairRaytracingRadiusScale > 0 ? GHairRaytracingRadiusScale : Instance->Strands.Modifier.HairRaytracingRadiusScale);

			FRDGImportedBuffer Raytracing_PositionBuffer = Register(GraphBuilder, Instance->Strands.RenRaytracingResource->PositionBuffer, ERDGImportedBufferFlags::CreateViews);
			AddGenerateRaytracingGeometryPass(
				GraphBuilder,
				ShaderMap,
				Instance->Strands.RestResource->GetVertexCount(),
				ScaleAndClipDesc.MaxOutHairRadius * HairRadiusScaleRT,
				RegisterAsSRV(GraphBuilder, Instance->Strands.DeformedResource->GetPositionOffsetBuffer(FHairStrandsDeformedResource::EFrameType::Current)),
				CullingData,
				Strands_DeformedPosition.SRV,
				Raytracing_PositionBuffer.UAV);

			GraphBuilder.AddPass(
			RDG_EVENT_NAME("HairStrandsUpdateBLAS"),
			ERDGPassFlags::NeverCull,
			[Instance](FRHICommandList& RHICmdList)
			{
				SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::All());

				const bool bNeedFullBuild = !Instance->Strands.RenRaytracingResource->bIsRTGeometryInitialized;
				if (bNeedFullBuild)
				{
					FVertexBufferRHIRef PositionBuffer(Instance->Strands.RenRaytracingResource->PositionBuffer.Buffer->GetVertexBufferRHI());
					BuildHairAccelerationStructure_Strands(RHICmdList, Instance->Strands.RenRaytracingResource->VertexCount, PositionBuffer, &Instance->Strands.RenRaytracingResource->RayTracingGeometry);
				}
				else
				{
					UpdateHairAccelerationStructure(RHICmdList, &Instance->Strands.RenRaytracingResource->RayTracingGeometry);
				}
				Instance->Strands.RenRaytracingResource->bIsRTGeometryInitialized = true;
			});
		}
		#endif
	}
	else if (InstanceGeometryType == EHairGeometryType::Cards)
	{	
		const uint32 HairLODIndex = Instance->HairGroupPublicData->GetIntLODIndex();
		const bool bIsCardsValid = Instance->Cards.IsValid(HairLODIndex);
		if (bIsCardsValid)
		{
			FHairGroupInstance::FCards::FLOD& LOD = Instance->Cards.LODs[HairLODIndex];
			const EHairCardsSimulationType CardsSimulationType = GetHairCardsSimulationType();
			if (LOD.Guides.Data && CardsSimulationType == EHairCardsSimulationType::Guide)
			{
				FRDGImportedBuffer Guides_DeformedPositionBuffer = Register(GraphBuilder, LOD.Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current), ERDGImportedBufferFlags::CreateViews);

				FHairScaleAndClipDesc ScaleAndClipDesc;
				ScaleAndClipDesc.bEnable = false;
				ScaleAndClipDesc.InHairLength = LOD.Guides.Data->StrandsCurves.MaxLength;
				ScaleAndClipDesc.InHairRadius = LOD.Guides.Data->StrandsCurves.MaxRadius;
				ScaleAndClipDesc.OutHairRadius = (GStrandHairWidth > 0 ? GStrandHairWidth * 0.5f : ScaleAndClipDesc.InHairRadius);
				ScaleAndClipDesc.MaxOutHairRadius = ScaleAndClipDesc.OutHairRadius;
				ScaleAndClipDesc.HairRadiusRootScale = 1;
				ScaleAndClipDesc.HairRadiusTipScale = 1;
				ScaleAndClipDesc.HairLengthClip = 1;

				FRDGHairStrandsCullingData CullingData;
				AddHairStrandsInterpolationPass(
					GraphBuilder,
					ShaderMap,
					ShaderDrawData,
					Instance,
					LOD.Guides.RestResource->GetVertexCount(),
					ScaleAndClipDesc,
					MeshLODIndex,
					false,
					LOD.Guides.HairInterpolationType,
					InstanceGeometryType,
					CullingData,
					LOD.Guides.RestResource->PositionOffset,
					Instance->Guides.RestResource->PositionOffset,
					RegisterAsSRV(GraphBuilder, LOD.Guides.DeformedResource->GetPositionOffsetBuffer(FHairStrandsDeformedResource::Current)),
					RegisterAsSRV(GraphBuilder, Instance->Guides.DeformedResource->GetPositionOffsetBuffer(FHairStrandsDeformedResource::Current)),
					LOD.Guides.RestRootResource,
					Instance->Guides.RestRootResource,
					LOD.Guides.DeformedRootResource,
					Instance->Guides.DeformedRootResource,
					RegisterAsSRV(GraphBuilder, LOD.Guides.RestResource->RestPositionBuffer),
					RegisterAsSRV(GraphBuilder, LOD.Guides.RestResource->AttributeBuffer),
					RegisterAsSRV(GraphBuilder, LOD.Guides.InterpolationResource->Interpolation0Buffer),
					RegisterAsSRV(GraphBuilder, LOD.Guides.InterpolationResource->Interpolation1Buffer),
					RegisterAsSRV(GraphBuilder, Instance->Guides.RestResource->RestPositionBuffer),
					RegisterAsSRV(GraphBuilder, Instance->Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current)),
					RegisterAsSRV(GraphBuilder, Instance->Guides.RestResource->AttributeBuffer),
					Guides_DeformedPositionBuffer,
					nullptr,
					nullptr,
					nullptr,
					RegisterAsSRV(GraphBuilder, LOD.Guides.InterpolationResource->SimRootPointIndexBuffer));

				AddHairCardsDeformationPass(
					GraphBuilder,
					ShaderMap,
					ShaderDrawData,
					Instance,
					MeshLODIndex);
			}
			else if (LOD.Guides.Data && CardsSimulationType == EHairCardsSimulationType::RBF)
			{
				AddHairCardsRBFInterpolationPass(
					GraphBuilder,
					ShaderMap,
					MeshLODIndex,
					LOD.RestResource,
					LOD.DeformedResource,
					Instance->Guides.RestRootResource,
					Instance->Guides.DeformedRootResource);
			}

			#if RHI_RAYTRACING
			if (LOD.RaytracingResource)
			{
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("HairCardsUpdateBLAS"),
					ERDGPassFlags::NeverCull,
					[Instance, HairLODIndex](FRHICommandList& RHICmdList)
					{
						SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::All());

						FHairGroupInstance::FCards::FLOD& LocalLOD = Instance->Cards.LODs[HairLODIndex];

						const bool bNeedFullBuild = !LocalLOD.RaytracingResource->bIsRTGeometryInitialized;
						if (bNeedFullBuild)
						{
							BuildHairAccelerationStructure_Cards(RHICmdList, LocalLOD.RestResource, LocalLOD.DeformedResource,  &LocalLOD.RaytracingResource->RayTracingGeometry);
						}
						else
						{
							UpdateHairAccelerationStructure(RHICmdList, &LocalLOD.RaytracingResource->RayTracingGeometry);
						}
						LocalLOD.RaytracingResource->bIsRTGeometryInitialized = true;
					});
			}
			#endif
		}
	}
	else if (InstanceGeometryType == EHairGeometryType::Meshes)
	{
		const uint32 HairLODIndex = Instance->HairGroupPublicData->GetIntLODIndex();
		if (Instance->Meshes.IsValid(HairLODIndex))
		{
			FHairGroupInstance::FMeshes::FLOD& MeshesInstance = Instance->Meshes.LODs[HairLODIndex];
			if (Instance->Guides.IsValid() && Instance->Guides.HasValidRootData() && Instance->Guides.DeformedRootResource->IsValid(MeshLODIndex))
			{
				AddHairMeshesRBFInterpolationPass(
					GraphBuilder,
					ShaderMap,
					MeshLODIndex,
					MeshesInstance.RestResource,
					MeshesInstance.DeformedResource,
					Instance->Guides.RestRootResource,
					Instance->Guides.DeformedRootResource);
			}
		}

		#if RHI_RAYTRACING
		FHairGroupInstance::FMeshes::FLOD& LOD = Instance->Meshes.LODs[HairLODIndex];
		if (LOD.RaytracingResource && !LOD.RaytracingResource->bIsRTGeometryInitialized)
		{
			GraphBuilder.AddPass(
				RDG_EVENT_NAME("HairMeshesUpdateBLAS"),
				ERDGPassFlags::NeverCull,
				[Instance, HairLODIndex](FRHICommandList& RHICmdList)
			{
				SCOPED_GPU_MASK(RHICmdList, FRHIGPUMask::All());

				FHairGroupInstance::FMeshes::FLOD& LocalLOD = Instance->Meshes.LODs[HairLODIndex];
				
				const bool bNeedFullBuild = !LocalLOD.RaytracingResource->bIsRTGeometryInitialized;
				if (bNeedFullBuild)
				{
					BuildHairAccelerationStructure_Meshes(RHICmdList, LocalLOD.RestResource, LocalLOD.DeformedResource,  &LocalLOD.RaytracingResource->RayTracingGeometry);
				}
				else
				{
					UpdateHairAccelerationStructure(RHICmdList, &LocalLOD.RaytracingResource->RayTracingGeometry);
				}
				LocalLOD.RaytracingResource->bIsRTGeometryInitialized = true;
			});
		}
		#endif
	}

	Instance->HairGroupPublicData->VFInput.GeometryType = InstanceGeometryType;
	Instance->HairGroupPublicData->VFInput.LocalToWorldTransform = Instance->LocalToWorld;
	Instance->HairGroupPublicData->bSupportVoxelization = Instance->Strands.Modifier.bSupportVoxelization && Instance->bCastShadow;
}

bool HasHairInstanceSimulationEnable(FHairGroupInstance* Instance, int32 MeshLODIndex)
{
	const bool bHasNoSimulation = !Instance || (Instance && Instance->Guides.bIsSimulationEnable && IsHairLODSimulationEnabled(MeshLODIndex)) || !IsHairStrandsBindingEnable();
	return !bHasNoSimulation;
}

void ResetHairStrandsInterpolation(
	FRDGBuilder& GraphBuilder,
	FGlobalShaderMap* ShaderMap,
	FHairGroupInstance* Instance,
	int32 MeshLODIndex)
{
	if (!HasHairInstanceSimulationEnable(Instance, MeshLODIndex))
	{
		return;
	}

	DECLARE_GPU_STAT(HairStrandsGuideDeform);
	RDG_EVENT_SCOPE(GraphBuilder, "HairStrandsGuideDeform");
	RDG_GPU_STAT_SCOPE(GraphBuilder, HairStrandsGuideDeform);

	FRDGExternalBuffer RawDeformedPositionBuffer = Instance->Guides.DeformedResource->GetBuffer(FHairStrandsDeformedResource::Current);
	FRDGImportedBuffer DeformedPositionBuffer = Register(GraphBuilder, RawDeformedPositionBuffer, ERDGImportedBufferFlags::CreateUAV);

	AddDeformSimHairStrandsPass(
		GraphBuilder,
		ShaderMap,
		EDeformationType::OffsetGuide,
		MeshLODIndex,
		Instance->Guides.RestResource->GetVertexCount(),
		Instance->Guides.RestRootResource,
		Instance->Guides.DeformedRootResource,
		RegisterAsSRV(GraphBuilder, Instance->Guides.RestResource->RestPositionBuffer),
		DeformedPositionBuffer,
		Instance->Guides.RestResource->PositionOffset,
		RegisterAsSRV(GraphBuilder, Instance->Guides.DeformedResource->GetPositionOffsetBuffer(FHairStrandsDeformedResource::Current)),
		Instance->Guides.bHasGlobalInterpolation);
}
