// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceHairStrands.h"
#include "NiagaraShader.h"
#include "NiagaraComponent.h"
#include "NiagaraRenderer.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraEmitterInstanceBatcher.h"

#include "ShaderParameterUtils.h"
#include "ClearQuad.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "GlobalShader.h"

#include "GroomComponent.h"
#include "GroomAsset.h"  

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceHairStrands"

static const int32 MaxDelay = 8;

//------------------------------------------------------------------------------------------------------------

static const FName GetStrandSizeName(TEXT("GetStrandSize"));
static const FName GetNumStrandsName(TEXT("GetNumStrands"));
static const FName GetWorldTransformName(TEXT("GetWorldTransform"));
static const FName GetWorldInverseName(TEXT("GetWorldInverse"));
static const FName GetPointPositionName(TEXT("GetPointPosition"));

//------------------------------------------------------------------------------------------------------------

static const FName ComputeNodePositionName(TEXT("ComputeNodePosition"));
static const FName ComputeNodeOrientationName(TEXT("ComputeNodeOrientation"));
static const FName ComputeNodeMassName(TEXT("ComputeNodeMass"));
static const FName ComputeNodeInertiaName(TEXT("ComputeNodeInertia"));
static const FName ComputeEdgeLengthName(TEXT("ComputeEdgeLength"));
static const FName ComputeEdgeRotationName(TEXT("ComputeEdgeRotation"));

//------------------------------------------------------------------------------------------------------------

static const FName ComputeRestPositionName(TEXT("ComputeRestPosition"));
static const FName ComputeRestOrientationName(TEXT("ComputeRestOrientation"));
static const FName ComputeLocalStateName(TEXT("ComputeLocalState"));

//------------------------------------------------------------------------------------------------------------

static const FName AdvectNodePositionName(TEXT("AdvectNodePosition"));
static const FName AdvectNodeOrientationName(TEXT("AdvectNodeOrientation"));
static const FName UpdateLinearVelocityName(TEXT("UpdateLinearVelocity"));
static const FName UpdateAngularVelocityName(TEXT("UpdateAngularVelocity"));

//------------------------------------------------------------------------------------------------------------

static const FName AttachNodePositionName(TEXT("AttachNodePosition"));
static const FName AttachNodeOrientationName(TEXT("AttachNodeOrientation"));
static const FName AttachNodeStateName(TEXT("AttachNodeState"));

//------------------------------------------------------------------------------------------------------------

static const FName UpdatePointPositionName(TEXT("UpdatePointPosition"));
static const FName ResetPointPositionName(TEXT("ResetPointPosition"));

//------------------------------------------------------------------------------------------------------------

static const FName GetBoxCenterName(TEXT("GetBoxCenter"));
static const FName GetBoxExtentName(TEXT("GetBoxExtent"));
static const FName BuildBoundingBoxName(TEXT("BuildBoundingBox"));

//------------------------------------------------------------------------------------------------------------

static const FName SetupDistanceSpringMaterialName(TEXT("SetupDistanceSpringMaterial"));
static const FName SolveDistanceSpringMaterialName(TEXT("SolveDistanceSpringMaterial"));
static const FName ProjectDistanceSpringMaterialName(TEXT("ProjectDistanceSpringMaterial"));

//------------------------------------------------------------------------------------------------------------

static const FName SetupAngularSpringMaterialName(TEXT("SetupAngularSpringMaterial"));
static const FName SolveAngularSpringMaterialName(TEXT("SolveAngularSpringMaterial"));
static const FName ProjectAngularSpringMaterialName(TEXT("ProjectAngularSpringMaterial"));

//------------------------------------------------------------------------------------------------------------

static const FName SetupStretchRodMaterialName(TEXT("SetupStretchRodMaterial"));
static const FName SolveStretchRodMaterialName(TEXT("SolveStretchRodMaterial"));
static const FName ProjectStretchRodMaterialName(TEXT("ProjectStretchRodMaterial"));

//------------------------------------------------------------------------------------------------------------

static const FName SetupBendRodMaterialName(TEXT("SetupBendRodMaterial"));
static const FName SolveBendRodMaterialName(TEXT("SolveBendRodMaterial"));
static const FName ProjectBendRodMaterialName(TEXT("ProjectBendRodMaterial"));
//------------------------------------------------------------------------------------------------------------

static const FName SolveStaticCollisionConstraintName(TEXT("SolveStaticCollisionConstraint"));
static const FName ProjectStaticCollisionConstraintName(TEXT("ProjectStaticCollisionConstraint"));

//------------------------------------------------------------------------------------------------------------

static const FName ComputeRestDirectionName(TEXT("ComputeRestDirection"));
static const FName UpdateNodeOrientationName(TEXT("UpdateNodeOrientation"));

//------------------------------------------------------------------------------------------------------------

static const FName ComputeAirDragForceName(TEXT("ComputeAirDragForce"));  

//------------------------------------------------------------------------------------------------------------

static const FName NeedSimulationResetName(TEXT("NeedSimulationReset"));

//------------------------------------------------------------------------------------------------------------

const FString UNiagaraDataInterfaceHairStrands::NumStrandsName(TEXT("NumStrands_"));
const FString UNiagaraDataInterfaceHairStrands::StrandSizeName(TEXT("StrandSize_"));
const FString UNiagaraDataInterfaceHairStrands::WorldTransformName(TEXT("WorldTransform_"));
const FString UNiagaraDataInterfaceHairStrands::WorldInverseName(TEXT("WorldInverse_"));
const FString UNiagaraDataInterfaceHairStrands::WorldRotationName(TEXT("WorldRotation_"));
const FString UNiagaraDataInterfaceHairStrands::BoxCenterName(TEXT("BoxCenter_"));
const FString UNiagaraDataInterfaceHairStrands::BoxExtentName(TEXT("BoxExtent_"));

const FString UNiagaraDataInterfaceHairStrands::DeformedPositionBufferName(TEXT("DeformedPositionBuffer_"));
const FString UNiagaraDataInterfaceHairStrands::CurvesOffsetsBufferName(TEXT("CurvesOffsetsBuffer_"));
const FString UNiagaraDataInterfaceHairStrands::RestPositionBufferName(TEXT("RestPositionBuffer_"));

const FString UNiagaraDataInterfaceHairStrands::ResetSimulationName(TEXT("ResetSimulation_"));
const FString UNiagaraDataInterfaceHairStrands::HasRootAttachedName(TEXT("HasRootAttached_"));
const FString UNiagaraDataInterfaceHairStrands::RootBarycentricCoordinatesName(TEXT("RootBarycentricCoordinatesBuffer_"));

const FString UNiagaraDataInterfaceHairStrands::RestRootOffsetName(TEXT("RestRootOffset_"));
const FString UNiagaraDataInterfaceHairStrands::RestTrianglePositionAName(TEXT("RestTrianglePositionABuffer_"));
const FString UNiagaraDataInterfaceHairStrands::RestTrianglePositionBName(TEXT("RestTrianglePositionBBuffer_"));
const FString UNiagaraDataInterfaceHairStrands::RestTrianglePositionCName(TEXT("RestTrianglePositionCBuffer_"));

const FString UNiagaraDataInterfaceHairStrands::DeformedRootOffsetName(TEXT("DeformedRootOffset_"));
const FString UNiagaraDataInterfaceHairStrands::DeformedTrianglePositionAName(TEXT("DeformedTrianglePositionABuffer_"));
const FString UNiagaraDataInterfaceHairStrands::DeformedTrianglePositionBName(TEXT("DeformedTrianglePositionBBuffer_"));
const FString UNiagaraDataInterfaceHairStrands::DeformedTrianglePositionCName(TEXT("DeformedTrianglePositionCBuffer_"));

const FString UNiagaraDataInterfaceHairStrands::RestPositionOffsetName(TEXT("RestPositionOffset_"));
const FString UNiagaraDataInterfaceHairStrands::DeformedPositionOffsetName(TEXT("DeformedPositionOffset_"));

const FString UNiagaraDataInterfaceHairStrands::BoundingBoxBufferName(TEXT("BoundingBoxBuffer_"));
const FString UNiagaraDataInterfaceHairStrands::NodeBoundBufferName(TEXT("NodeBoundBuffer_"));

//------------------------------------------------------------------------------------------------------------

struct FNDIHairStrandsParametersName
{
	FNDIHairStrandsParametersName(const FString& Suffix)
	{
		NumStrandsName = UNiagaraDataInterfaceHairStrands::NumStrandsName + Suffix;
		StrandSizeName = UNiagaraDataInterfaceHairStrands::StrandSizeName + Suffix;
		WorldTransformName = UNiagaraDataInterfaceHairStrands::WorldTransformName + Suffix;
		WorldInverseName = UNiagaraDataInterfaceHairStrands::WorldInverseName + Suffix;
		WorldRotationName = UNiagaraDataInterfaceHairStrands::WorldRotationName + Suffix;
		BoxCenterName = UNiagaraDataInterfaceHairStrands::BoxCenterName + Suffix;
		BoxExtentName = UNiagaraDataInterfaceHairStrands::BoxExtentName + Suffix;

		DeformedPositionBufferName = UNiagaraDataInterfaceHairStrands::DeformedPositionBufferName + Suffix;
		CurvesOffsetsBufferName = UNiagaraDataInterfaceHairStrands::CurvesOffsetsBufferName + Suffix;
		RestPositionBufferName = UNiagaraDataInterfaceHairStrands::RestPositionBufferName + Suffix;

		HasRootAttachedName = UNiagaraDataInterfaceHairStrands::HasRootAttachedName + Suffix;
		ResetSimulationName = UNiagaraDataInterfaceHairStrands::ResetSimulationName + Suffix;
		RootBarycentricCoordinatesName = UNiagaraDataInterfaceHairStrands::RootBarycentricCoordinatesName + Suffix;

		RestRootOffsetName = UNiagaraDataInterfaceHairStrands::RestRootOffsetName + Suffix;
		RestTrianglePositionAName = UNiagaraDataInterfaceHairStrands::RestTrianglePositionAName + Suffix;
		RestTrianglePositionBName = UNiagaraDataInterfaceHairStrands::RestTrianglePositionBName + Suffix;
		RestTrianglePositionCName = UNiagaraDataInterfaceHairStrands::RestTrianglePositionCName + Suffix;

		DeformedRootOffsetName = UNiagaraDataInterfaceHairStrands::DeformedRootOffsetName + Suffix;
		DeformedTrianglePositionAName = UNiagaraDataInterfaceHairStrands::DeformedTrianglePositionAName + Suffix;
		DeformedTrianglePositionBName = UNiagaraDataInterfaceHairStrands::DeformedTrianglePositionBName + Suffix;
		DeformedTrianglePositionCName = UNiagaraDataInterfaceHairStrands::DeformedTrianglePositionCName + Suffix;

		RestPositionOffsetName = UNiagaraDataInterfaceHairStrands::RestPositionOffsetName + Suffix;
		DeformedPositionOffsetName = UNiagaraDataInterfaceHairStrands::DeformedPositionOffsetName + Suffix;

		BoundingBoxBufferName = UNiagaraDataInterfaceHairStrands::BoundingBoxBufferName + Suffix;
		NodeBoundBufferName = UNiagaraDataInterfaceHairStrands::NodeBoundBufferName + Suffix;
	}

	FString NumStrandsName;
	FString StrandSizeName;
	FString WorldTransformName;
	FString WorldInverseName;
	FString WorldRotationName;
	FString BoxCenterName;
	FString BoxExtentName;

	FString DeformedPositionBufferName;
	FString CurvesOffsetsBufferName;
	FString RestPositionBufferName;

	FString ResetSimulationName;
	FString HasRootAttachedName;
	FString RootBarycentricCoordinatesName;

	FString RestRootOffsetName;
	FString RestTrianglePositionAName;
	FString RestTrianglePositionBName;
	FString RestTrianglePositionCName;

	FString DeformedRootOffsetName;
	FString DeformedTrianglePositionAName;
	FString DeformedTrianglePositionBName;
	FString DeformedTrianglePositionCName;

	FString RestPositionOffsetName;
	FString DeformedPositionOffsetName;

	FString BoundingBoxBufferName;
	FString NodeBoundBufferName;
};

//------------------------------------------------------------------------------------------------------------

#define NIAGARA_HAIR_STRANDS_THREAD_COUNT 64

class FCopyBoundingBoxCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FCopyBoundingBoxCS);
	SHADER_USE_PARAMETER_STRUCT(FCopyBoundingBoxCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumElements)
		SHADER_PARAMETER_SRV(Buffer, BoundingBoxBuffer)
		SHADER_PARAMETER_UAV(RWBuffer, OutNodeBoundBuffer)
		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsComputeShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), NIAGARA_HAIR_STRANDS_THREAD_COUNT);
	}
};

class FResetBoundingBoxCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FResetBoundingBoxCS);
	SHADER_USE_PARAMETER_STRUCT(FResetBoundingBoxCS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(uint32, NumElements)
		SHADER_PARAMETER_UAV(RWBuffer, OutBoundingBoxBuffer)
		END_SHADER_PARAMETER_STRUCT()

public:
	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return RHISupportsComputeShaders(Parameters.Platform);
	}

	static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
	{
		FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		OutEnvironment.SetDefine(TEXT("THREAD_COUNT"), NIAGARA_HAIR_STRANDS_THREAD_COUNT);
	}
};


IMPLEMENT_GLOBAL_SHADER(FResetBoundingBoxCS, "/Plugin/Experimental/HairStrands/Private/NiagaraResetBoundingBox.usf", "MainCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FCopyBoundingBoxCS, "/Plugin/Experimental/HairStrands/Private/NiagaraCopyBoundingBox.usf", "MainCS", SF_Compute);

static void AddResetBoundingBoxPass(
	FRDGBuilder& GraphBuilder,
	FRHIUnorderedAccessView* OutBoundingBoxBuffer)
{
	const uint32 GroupSize = NIAGARA_HAIR_STRANDS_THREAD_COUNT;
	const uint32 NumElements = 1;

	FResetBoundingBoxCS::FParameters* Parameters = GraphBuilder.AllocParameters<FResetBoundingBoxCS::FParameters>();
	Parameters->OutBoundingBoxBuffer = OutBoundingBoxBuffer;
	Parameters->NumElements = NumElements;
	
	TShaderMap<FGlobalShaderType>* ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);

	const uint32 DispatchCount = FMath::DivideAndRoundUp(NumElements, GroupSize);
	check(DispatchCount <= 65535);

	TShaderMapRef<FResetBoundingBoxCS> ComputeShader(ShaderMap);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("ResetBoundingBox"),
		*ComputeShader,
		Parameters,
		FIntVector(DispatchCount, 1, 1));
}

static void AddCopyBoundingBoxPass(
	FRDGBuilder& GraphBuilder,
	FRHIShaderResourceView* BoundingBoxBuffer,
	FRHIUnorderedAccessView* OutNodeBoundBuffer)
{
	const uint32 GroupSize = NIAGARA_HAIR_STRANDS_THREAD_COUNT;
	const uint32 NumElements = 1;

	FCopyBoundingBoxCS::FParameters* Parameters = GraphBuilder.AllocParameters<FCopyBoundingBoxCS::FParameters>();
	Parameters->BoundingBoxBuffer = BoundingBoxBuffer;
	Parameters->OutNodeBoundBuffer = OutNodeBoundBuffer;
	Parameters->NumElements = NumElements;

	TShaderMap<FGlobalShaderType>* ShaderMap = GetGlobalShaderMap(ERHIFeatureLevel::SM5);

	const uint32 DispatchCount = FMath::DivideAndRoundUp(NumElements, GroupSize);
	check(DispatchCount <= 65535);

	TShaderMapRef<FCopyBoundingBoxCS> ComputeShader(ShaderMap);
	FComputeShaderUtils::AddPass(
		GraphBuilder,
		RDG_EVENT_NAME("CopyBoundingBox"),
		*ComputeShader,
		Parameters,
		FIntVector(DispatchCount, 1, 1));
}

//------------------------------------------------------------------------------------------------------------

void FNDIHairStrandsBuffer::SetHairAsset(const FHairStrandsDatas*  HairStrandsDatas, const FHairStrandsRestResource* HairStrandsRestResource, 
	const FHairStrandsDeformedResource*  HairStrandsDeformedResource, const FHairStrandsRootResource* HairStrandsRootResource)
{
	SourceDatas = HairStrandsDatas;
	SourceRestResources = HairStrandsRestResource;
	SourceDeformedResources = HairStrandsDeformedResource;
	SourceRootResources = HairStrandsRootResource;
}

void FNDIHairStrandsBuffer::ClearBuffer(FRHICommandList& RHICmdList)
{
	FRHIUnorderedAccessView* BoundingBoxBufferUAV = BoundingBoxBuffer.UAV;
	FRHIShaderResourceView* BoundingBoxBufferSRV = BoundingBoxBuffer.SRV;
	FRHIUnorderedAccessView* NodeBoundBufferUAV = NodeBoundBuffer.UAV;

	ENQUEUE_RENDER_COMMAND(FNDIHairStrandsCopyBoundingBox)(
		[BoundingBoxBufferUAV, NodeBoundBufferUAV, BoundingBoxBufferSRV]
	(FRHICommandListImmediate& RHICmdListImm)
	{
		FRDGBuilder GraphBuilder(RHICmdListImm);

		RHICmdListImm.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, BoundingBoxBufferUAV);
		RHICmdListImm.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, NodeBoundBufferUAV);

		AddCopyBoundingBoxPass(
			GraphBuilder,
			BoundingBoxBufferSRV, NodeBoundBufferUAV);

		GraphBuilder.Execute();
	});

	ENQUEUE_RENDER_COMMAND(FNDIHairStrandsResetBoundingBox)(
		[BoundingBoxBufferUAV, NodeBoundBufferUAV, BoundingBoxBufferSRV]
	(FRHICommandListImmediate& RHICmdListImm)
	{
		FRDGBuilder GraphBuilder(RHICmdListImm);

		RHICmdListImm.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, BoundingBoxBufferUAV);
		RHICmdListImm.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, NodeBoundBufferUAV);

		AddResetBoundingBoxPass(
			GraphBuilder,
			BoundingBoxBufferUAV);

		GraphBuilder.Execute();
	});
}

void FNDIHairStrandsBuffer::InitRHI()
{
	if (SourceDatas != nullptr && SourceRestResources != nullptr)
	{
		{
			const uint32 OffsetCount = SourceDatas->GetNumCurves() + 1;
			const uint32 OffsetBytes = sizeof(uint32)*OffsetCount;

			CurvesOffsetsBuffer.Initialize(sizeof(uint32), OffsetCount, EPixelFormat::PF_R32_UINT, BUF_Static);
			void* OffsetBufferData = RHILockVertexBuffer(CurvesOffsetsBuffer.Buffer, 0, OffsetBytes, RLM_WriteOnly);

			FMemory::Memcpy(OffsetBufferData, SourceDatas->StrandsCurves.CurvesOffset.GetData(), OffsetBytes);
			RHIUnlockVertexBuffer(CurvesOffsetsBuffer.Buffer);
		}
		{
			static const TArray<uint32> ZeroData = {UINT_MAX,UINT_MAX,UINT_MAX,0,0,0};

			const uint32 BoundCount = 6;
			const uint32 BoundBytes = sizeof(uint32)*BoundCount;

			BoundingBoxBuffer.Initialize(sizeof(uint32), BoundCount, EPixelFormat::PF_R32_UINT, BUF_Static);
			void* BoundBufferData = RHILockVertexBuffer(BoundingBoxBuffer.Buffer, 0, BoundBytes, RLM_WriteOnly);
			
			FMemory::Memcpy(BoundBufferData, ZeroData.GetData(), BoundBytes);
			RHIUnlockVertexBuffer(BoundingBoxBuffer.Buffer);
		}

		{
			static const TArray<uint32> ZeroData = { UINT_MAX,UINT_MAX,UINT_MAX,0,0,0};

			const uint32 BoundCount = 6;
			const uint32 BoundBytes = sizeof(uint32)*BoundCount;

			NodeBoundBuffer.Initialize(sizeof(uint32), BoundCount, EPixelFormat::PF_R32_UINT, BUF_Static);
			void* BoundBufferData = RHILockVertexBuffer(NodeBoundBuffer.Buffer, 0, BoundBytes, RLM_WriteOnly);

			FMemory::Memcpy(BoundBufferData, ZeroData.GetData(), BoundBytes);
			RHIUnlockVertexBuffer(NodeBoundBuffer.Buffer);
		}
		if (SourceDeformedResources == nullptr)
		{
			const uint32 PositionsCount = SourceDatas->GetNumPoints();
			DeformedPositionBuffer.Initialize(FHairStrandsPositionFormat::SizeInByte, PositionsCount, FHairStrandsPositionFormat::Format, BUF_Static);
		}
		if (SourceRootResources == nullptr)
		{
			const uint32 PositionsCount = SourceDatas->GetNumCurves();
			RestTrianglePositionABuffer.Initialize(FHairStrandsMeshTrianglePositionFormat::SizeInByte, PositionsCount, FHairStrandsMeshTrianglePositionFormat::Format, BUF_Static);
			RestTrianglePositionBBuffer.Initialize(FHairStrandsMeshTrianglePositionFormat::SizeInByte, PositionsCount, FHairStrandsMeshTrianglePositionFormat::Format, BUF_Static);
			RestTrianglePositionCBuffer.Initialize(FHairStrandsMeshTrianglePositionFormat::SizeInByte, PositionsCount, FHairStrandsMeshTrianglePositionFormat::Format, BUF_Static);

			DeformedTrianglePositionABuffer.Initialize(FHairStrandsMeshTrianglePositionFormat::SizeInByte, PositionsCount, FHairStrandsMeshTrianglePositionFormat::Format, BUF_Static);
			DeformedTrianglePositionBBuffer.Initialize(FHairStrandsMeshTrianglePositionFormat::SizeInByte, PositionsCount, FHairStrandsMeshTrianglePositionFormat::Format, BUF_Static);
			DeformedTrianglePositionCBuffer.Initialize(FHairStrandsMeshTrianglePositionFormat::SizeInByte, PositionsCount, FHairStrandsMeshTrianglePositionFormat::Format, BUF_Static);

			RootBarycentricCoordinatesBuffer.Initialize(FHairStrandsCurveTriangleBarycentricFormat::SizeInByte, PositionsCount, FHairStrandsCurveTriangleBarycentricFormat::Format, BUF_Static);
		}
	}
}

void FNDIHairStrandsBuffer::ReleaseRHI()
{
	CurvesOffsetsBuffer.Release();
	DeformedPositionBuffer.Release();

	RestTrianglePositionABuffer.Release();
	RestTrianglePositionBBuffer.Release();
	RestTrianglePositionCBuffer.Release();

	DeformedTrianglePositionABuffer.Release();
	DeformedTrianglePositionBBuffer.Release();
	DeformedTrianglePositionCBuffer.Release();

	RootBarycentricCoordinatesBuffer.Release();
	BoundingBoxBuffer.Release();
	NodeBoundBuffer.Release();
}

//------------------------------------------------------------------------------------------------------------

struct FNDIHairStrandsParametersCS : public FNiagaraDataInterfaceParametersCS
{
	virtual void Bind(const FNiagaraDataInterfaceParamRef& ParamRef, const class FShaderParameterMap& ParameterMap) override
	{
		FNDIHairStrandsParametersName ParamNames(ParamRef.ParameterInfo.DataInterfaceHLSLSymbol);

		WorldTransform.Bind(ParameterMap, *ParamNames.WorldTransformName);
		WorldInverse.Bind(ParameterMap, *ParamNames.WorldInverseName);
		WorldRotation.Bind(ParameterMap, *ParamNames.WorldRotationName);
		NumStrands.Bind(ParameterMap, *ParamNames.NumStrandsName);
		StrandSize.Bind(ParameterMap, *ParamNames.StrandSizeName);
		BoxCenter.Bind(ParameterMap, *ParamNames.BoxCenterName);
		BoxExtent.Bind(ParameterMap, *ParamNames.BoxExtentName);

		DeformedPositionBuffer.Bind(ParameterMap, *ParamNames.DeformedPositionBufferName);
		CurvesOffsetsBuffer.Bind(ParameterMap, *ParamNames.CurvesOffsetsBufferName);
		RestPositionBuffer.Bind(ParameterMap, *ParamNames.RestPositionBufferName);

		ResetSimulation.Bind(ParameterMap, *ParamNames.ResetSimulationName);
		HasRootAttached.Bind(ParameterMap,*ParamNames.HasRootAttachedName);
		RestRootOffset.Bind(ParameterMap, *ParamNames.RestRootOffsetName);
		DeformedRootOffset.Bind(ParameterMap, *ParamNames.DeformedRootOffsetName);

		RestPositionOffset.Bind(ParameterMap, *ParamNames.RestPositionOffsetName);
		DeformedPositionOffset.Bind(ParameterMap, *ParamNames.DeformedPositionOffsetName);

		RootBarycentricCoordinatesBuffer.Bind(ParameterMap, *ParamNames.RootBarycentricCoordinatesName);

		RestTrianglePositionABuffer.Bind(ParameterMap, *ParamNames.RestTrianglePositionAName);
		RestTrianglePositionBBuffer.Bind(ParameterMap, *ParamNames.RestTrianglePositionBName);
		RestTrianglePositionCBuffer.Bind(ParameterMap, *ParamNames.RestTrianglePositionCName);

		DeformedTrianglePositionABuffer.Bind(ParameterMap, *ParamNames.DeformedTrianglePositionAName);
		DeformedTrianglePositionBBuffer.Bind(ParameterMap, *ParamNames.DeformedTrianglePositionBName);
		DeformedTrianglePositionCBuffer.Bind(ParameterMap, *ParamNames.DeformedTrianglePositionCName);

		BoundingBoxBuffer.Bind(ParameterMap, *ParamNames.BoundingBoxBufferName);
		NodeBoundBuffer.Bind(ParameterMap, *ParamNames.NodeBoundBufferName);

		if (!DeformedPositionBuffer.IsBound())
		{
			UE_LOG(LogHairStrands, Warning, TEXT("Binding failed for FNDIHairStrandsParametersCS %s. Was it optimized out?"), *ParamNames.DeformedPositionBufferName)
		}
		if (!CurvesOffsetsBuffer.IsBound())
		{
			UE_LOG(LogHairStrands, Warning, TEXT("Binding failed for FNDIHairStrandsParametersCS %s. Was it optimized out?"), *ParamNames.CurvesOffsetsBufferName)
		}
		if (!RestPositionBuffer.IsBound())
		{
			UE_LOG(LogHairStrands, Warning, TEXT("Binding failed for FNDIHairStrandsParametersCS %s. Was it optimized out?"), *ParamNames.RestPositionBufferName)
		}

		if (!RootBarycentricCoordinatesBuffer.IsBound())
		{
			UE_LOG(LogHairStrands, Warning, TEXT("Binding failed for FNDIHairStrandsParametersCS %s. Was it optimized out?"), *ParamNames.RootBarycentricCoordinatesName)
		}

		if (!RestTrianglePositionABuffer.IsBound())
		{
			UE_LOG(LogHairStrands, Warning, TEXT("Binding failed for FNDIHairStrandsParametersCS %s. Was it optimized out?"), *ParamNames.RestTrianglePositionAName)
		}
		if (!RestTrianglePositionBBuffer.IsBound())
		{
			UE_LOG(LogHairStrands, Warning, TEXT("Binding failed for FNDIHairStrandsParametersCS %s. Was it optimized out?"), *ParamNames.RestTrianglePositionBName)
		}
		if (!RestTrianglePositionCBuffer.IsBound())
		{
			UE_LOG(LogHairStrands, Warning, TEXT("Binding failed for FNDIHairStrandsParametersCS %s. Was it optimized out?"), *ParamNames.RestTrianglePositionCName)
		}

		if (!DeformedTrianglePositionABuffer.IsBound())
		{
			UE_LOG(LogHairStrands, Warning, TEXT("Binding failed for FNDIHairStrandsParametersCS %s. Was it optimized out?"), *ParamNames.DeformedTrianglePositionAName)
		}
		if (!DeformedTrianglePositionBBuffer.IsBound())
		{
			UE_LOG(LogHairStrands, Warning, TEXT("Binding failed for FNDIHairStrandsParametersCS %s. Was it optimized out?"), *ParamNames.DeformedTrianglePositionBName)
		}
		if (!DeformedTrianglePositionCBuffer.IsBound())
		{
			UE_LOG(LogHairStrands, Warning, TEXT("Binding failed for FNDIHairStrandsParametersCS %s. Was it optimized out?"), *ParamNames.DeformedTrianglePositionCName)
		}
		if (!BoundingBoxBuffer.IsBound())
		{
			UE_LOG(LogHairStrands, Warning, TEXT("Binding failed for FNDIHairStrandsParametersCS %s. Was it optimized out?"), *ParamNames.BoundingBoxBufferName)
		}
		if (!NodeBoundBuffer.IsBound())
		{
			UE_LOG(LogHairStrands, Warning, TEXT("Binding failed for FNDIHairStrandsParametersCS %s. Was it optimized out?"), *ParamNames.BoundingBoxBufferName)
		}
	}

	virtual void Serialize(FArchive& Ar) override
	{
		Ar << WorldTransform;
		Ar << WorldInverse;
		Ar << WorldRotation;
		Ar << NumStrands;
		Ar << StrandSize;
		Ar << BoxCenter;
		Ar << BoxExtent;

		Ar << DeformedPositionBuffer;
		Ar << CurvesOffsetsBuffer;
		Ar << RestPositionBuffer;

		Ar << ResetSimulation;
		Ar << HasRootAttached;
		Ar << RestRootOffset;
		Ar << DeformedRootOffset;

		Ar << RootBarycentricCoordinatesBuffer;

		Ar << RestTrianglePositionABuffer;
		Ar << RestTrianglePositionBBuffer;
		Ar << RestTrianglePositionCBuffer;

		Ar << DeformedTrianglePositionABuffer;
		Ar << DeformedTrianglePositionBBuffer;
		Ar << DeformedTrianglePositionCBuffer;

		Ar << RestPositionOffset;
		Ar << DeformedPositionOffset;

		Ar << BoundingBoxBuffer;
		Ar << NodeBoundBuffer;
	}

	virtual void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const override
	{
		check(IsInRenderingThread()); 

		FRHIComputeShader* ComputeShaderRHI = Context.Shader->GetComputeShader();

		FNDIHairStrandsProxy* InterfaceProxy =
			static_cast<FNDIHairStrandsProxy*>(Context.DataInterface);
		FNDIHairStrandsData* ProxyData =
			InterfaceProxy->SystemInstancesToProxyData.Find(Context.SystemInstance);

		if (ProxyData != nullptr)
		{
			FNDIHairStrandsBuffer* HairStrandsBuffer = ProxyData->HairStrandsBuffer;

			FUnorderedAccessViewRHIRef PointPositionsUAV = (HairStrandsBuffer->SourceDeformedResources != nullptr) ? HairStrandsBuffer->SourceDeformedResources->DeformedPositionBuffer[0].UAV : 
				HairStrandsBuffer->DeformedPositionBuffer.UAV;

			FShaderResourceViewRHIRef RestTrianglePositionASRV = HairStrandsBuffer->RestTrianglePositionABuffer.SRV;
			FShaderResourceViewRHIRef RestTrianglePositionBSRV = HairStrandsBuffer->RestTrianglePositionBBuffer.SRV;
			FShaderResourceViewRHIRef RestTrianglePositionCSRV = HairStrandsBuffer->RestTrianglePositionCBuffer.SRV;

			FShaderResourceViewRHIRef DeformedTrianglePositionASRV = HairStrandsBuffer->DeformedTrianglePositionABuffer.SRV;
			FShaderResourceViewRHIRef DeformedTrianglePositionBSRV = HairStrandsBuffer->DeformedTrianglePositionBBuffer.SRV;
			FShaderResourceViewRHIRef DeformedTrianglePositionCSRV = HairStrandsBuffer->DeformedTrianglePositionCBuffer.SRV;

			FShaderResourceViewRHIRef RootBarycentricCoordinatesSRV = HairStrandsBuffer->RootBarycentricCoordinatesBuffer.SRV;

			int32 HasRootAttachedValue = 0;
			int32 bNeedSimReset = (ProxyData->TickCount <= ProxyData->ResetTick ? 1 : 0);
			FVector RestRootOffsetValue = FVector(0, 0, 0);
			FVector DeformedRootOffsetValue = FVector(0, 0, 0);
			FVector DeformedPositionOffsetValue = HairStrandsBuffer->SourceRestResources->PositionOffset;
			FVector RestPositionOffsetValue = HairStrandsBuffer->SourceRestResources->PositionOffset;

			//UE_LOG(LogHairStrands, Log, TEXT("Need Reset = %d %d %d"), bNeedSimReset, ProxyData->TickCount, ProxyData->ResetTick);

			if (HairStrandsBuffer->SourceRootResources != nullptr && HairStrandsBuffer->SourceRootResources->MeshProjectionLODs.Num() > 0)
			{
				RestTrianglePositionASRV = HairStrandsBuffer->SourceRootResources->MeshProjectionLODs[0].RestRootTrianglePosition0Buffer.SRV;
				RestTrianglePositionBSRV = HairStrandsBuffer->SourceRootResources->MeshProjectionLODs[0].RestRootTrianglePosition1Buffer.SRV;
				RestTrianglePositionCSRV = HairStrandsBuffer->SourceRootResources->MeshProjectionLODs[0].RestRootTrianglePosition2Buffer.SRV;

				DeformedTrianglePositionASRV = HairStrandsBuffer->SourceRootResources->MeshProjectionLODs[0].DeformedRootTrianglePosition0Buffer.SRV;
				DeformedTrianglePositionBSRV = HairStrandsBuffer->SourceRootResources->MeshProjectionLODs[0].DeformedRootTrianglePosition1Buffer.SRV;
				DeformedTrianglePositionCSRV = HairStrandsBuffer->SourceRootResources->MeshProjectionLODs[0].DeformedRootTrianglePosition2Buffer.SRV;

				RootBarycentricCoordinatesSRV = HairStrandsBuffer->SourceRootResources->MeshProjectionLODs[0].RootTriangleBarycentricBuffer.SRV;

				HasRootAttachedValue = 1;
				//NeedSimReset = (HairStrandsBuffer->SourceRootResources->MeshProjectionLODs[0].Status != FHairStrandsRootResource::FMeshProjectionLOD::EStatus::Completed);
				RestRootOffsetValue = HairStrandsBuffer->SourceRootResources->MeshProjectionLODs[0].RestRootOffset;
				DeformedRootOffsetValue = ProxyData->HairStrandsBuffer->SourceRootResources->MeshProjectionLODs[0].DeformedRootOffset;
			}
			if (HairStrandsBuffer->SourceDeformedResources != nullptr)
			{
				DeformedPositionOffsetValue = HairStrandsBuffer->SourceDeformedResources->PositionOffset;
			}

			//UE_LOG(LogHairStrands, Log, TEXT("Root Offset = %f %f %f | %f %f %f | %f %f %f"), RestRootOffsetValue[0], RestRootOffsetValue[1], RestRootOffsetValue[2],
			//	DeformedRootOffsetValue[0], DeformedRootOffsetValue[1], DeformedRootOffsetValue[2], DeformedPositionOffsetValue[0], DeformedPositionOffsetValue[1], DeformedPositionOffsetValue[2]);

			SetUAVParameter(RHICmdList, ComputeShaderRHI, DeformedPositionBuffer, PointPositionsUAV);
			RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EComputeToCompute, HairStrandsBuffer->BoundingBoxBuffer.UAV);
			SetUAVParameter(RHICmdList, ComputeShaderRHI, BoundingBoxBuffer, HairStrandsBuffer->BoundingBoxBuffer.UAV);
			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToCompute, HairStrandsBuffer->NodeBoundBuffer.UAV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, NodeBoundBuffer, HairStrandsBuffer->NodeBoundBuffer.SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, CurvesOffsetsBuffer, HairStrandsBuffer->CurvesOffsetsBuffer.SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, RestPositionBuffer, HairStrandsBuffer->SourceRestResources->RestPositionBuffer.SRV);

			SetShaderValue(RHICmdList, ComputeShaderRHI, WorldTransform, ProxyData->WorldTransform.ToMatrixWithScale());
			SetShaderValue(RHICmdList, ComputeShaderRHI, WorldInverse, ProxyData->WorldTransform.ToMatrixWithScale().Inverse());
			SetShaderValue(RHICmdList, ComputeShaderRHI, WorldRotation, ProxyData->WorldTransform.ToMatrixNoScale().ToQuat());
			SetShaderValue(RHICmdList, ComputeShaderRHI, NumStrands, ProxyData->NumStrands);
			SetShaderValue(RHICmdList, ComputeShaderRHI, StrandSize, ProxyData->StrandSize);
			SetShaderValue(RHICmdList, ComputeShaderRHI, BoxCenter, ProxyData->BoxCenter);
			SetShaderValue(RHICmdList, ComputeShaderRHI, BoxExtent, ProxyData->BoxExtent);

			SetShaderValue(RHICmdList, ComputeShaderRHI, ResetSimulation, bNeedSimReset);
			SetShaderValue(RHICmdList, ComputeShaderRHI, HasRootAttached, HasRootAttachedValue);
			SetShaderValue(RHICmdList, ComputeShaderRHI, RestRootOffset, RestRootOffsetValue);
			SetShaderValue(RHICmdList, ComputeShaderRHI, DeformedRootOffset, DeformedRootOffsetValue);

			SetShaderValue(RHICmdList, ComputeShaderRHI, RestPositionOffset, RestPositionOffsetValue);
			SetShaderValue(RHICmdList, ComputeShaderRHI, DeformedPositionOffset, DeformedPositionOffsetValue);

			SetSRVParameter(RHICmdList, ComputeShaderRHI, RestTrianglePositionABuffer, RestTrianglePositionASRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, RestTrianglePositionBBuffer, RestTrianglePositionBSRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, RestTrianglePositionCBuffer, RestTrianglePositionCSRV);

			SetSRVParameter(RHICmdList, ComputeShaderRHI, DeformedTrianglePositionABuffer, DeformedTrianglePositionASRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, DeformedTrianglePositionBBuffer, DeformedTrianglePositionBSRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, DeformedTrianglePositionCBuffer, DeformedTrianglePositionCSRV);

			SetSRVParameter(RHICmdList, ComputeShaderRHI, RootBarycentricCoordinatesBuffer, RootBarycentricCoordinatesSRV);
		}
		else
		{
			SetUAVParameter(RHICmdList, ComputeShaderRHI, DeformedPositionBuffer, FNiagaraRenderer::GetDummyFloatBuffer().UAV);
			SetUAVParameter(RHICmdList, ComputeShaderRHI, BoundingBoxBuffer, FNiagaraRenderer::GetDummyUIntBuffer().UAV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, NodeBoundBuffer, FNiagaraRenderer::GetDummyUIntBuffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, CurvesOffsetsBuffer, FNiagaraRenderer::GetDummyUIntBuffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, RestPositionBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);

			SetShaderValue(RHICmdList, ComputeShaderRHI, WorldTransform, FMatrix::Identity);
			SetShaderValue(RHICmdList, ComputeShaderRHI, WorldInverse, FMatrix::Identity);
			SetShaderValue(RHICmdList, ComputeShaderRHI, WorldRotation, FQuat::Identity);
			SetShaderValue(RHICmdList, ComputeShaderRHI, NumStrands, 1);
			SetShaderValue(RHICmdList, ComputeShaderRHI, StrandSize, 1);
			SetShaderValue(RHICmdList, ComputeShaderRHI, BoxCenter, FVector(0,0,0));
			SetShaderValue(RHICmdList, ComputeShaderRHI, BoxExtent, FVector(0,0,0));

			SetShaderValue(RHICmdList, ComputeShaderRHI, ResetSimulation, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, HasRootAttached, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, RestRootOffset, FVector4(0, 0, 0, 0));
			SetShaderValue(RHICmdList, ComputeShaderRHI, DeformedRootOffset, FVector4(0, 0, 0, 0));

			SetShaderValue(RHICmdList, ComputeShaderRHI, RestPositionOffset, FVector(0,0,0));
			SetShaderValue(RHICmdList, ComputeShaderRHI, DeformedPositionOffset, FVector(0,0,0));

			SetSRVParameter(RHICmdList, ComputeShaderRHI, RestTrianglePositionABuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, RestTrianglePositionBBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, RestTrianglePositionCBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);

			SetSRVParameter(RHICmdList, ComputeShaderRHI, DeformedTrianglePositionABuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, DeformedTrianglePositionBBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, DeformedTrianglePositionCBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);

			SetSRVParameter(RHICmdList, ComputeShaderRHI, RootBarycentricCoordinatesBuffer, FNiagaraRenderer::GetDummyFloatBuffer().SRV);
		}
	}

	virtual void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const override
	{
		SetUAVParameter(RHICmdList, Context.Shader->GetComputeShader(), DeformedPositionBuffer, nullptr);
		SetUAVParameter(RHICmdList, Context.Shader->GetComputeShader(), BoundingBoxBuffer, nullptr);
	}

private:

	FShaderParameter WorldTransform;
	FShaderParameter WorldInverse;
	FShaderParameter WorldRotation;
	FShaderParameter NumStrands;
	FShaderParameter StrandSize;
	FShaderParameter BoxCenter;
	FShaderParameter BoxExtent;

	FShaderResourceParameter DeformedPositionBuffer;
	FShaderResourceParameter CurvesOffsetsBuffer;
	FShaderResourceParameter RestPositionBuffer;

	FShaderParameter ResetSimulation;
	FShaderParameter HasRootAttached;
	FShaderParameter RestRootOffset;
	FShaderParameter DeformedRootOffset;

	FShaderResourceParameter RootBarycentricCoordinatesBuffer;

	FShaderResourceParameter RestTrianglePositionABuffer;
	FShaderResourceParameter RestTrianglePositionBBuffer;
	FShaderResourceParameter RestTrianglePositionCBuffer;

	FShaderResourceParameter DeformedTrianglePositionABuffer;
	FShaderResourceParameter DeformedTrianglePositionBBuffer;
	FShaderResourceParameter DeformedTrianglePositionCBuffer;

	FShaderParameter RestPositionOffset;
	FShaderParameter DeformedPositionOffset;

	FShaderResourceParameter BoundingBoxBuffer;
	FShaderResourceParameter NodeBoundBuffer;
};

//------------------------------------------------------------------------------------------------------------

void FNDIHairStrandsProxy::DeferredDestroy()
{
	for (const FNiagaraSystemInstanceID& Sys : DeferredDestroyList)
	{
		SystemInstancesToProxyData.Remove(Sys);
	}

	DeferredDestroyList.Empty();
}

void FNDIHairStrandsProxy::ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance)
{
	FNDIHairStrandsData* SourceData = static_cast<FNDIHairStrandsData*>(PerInstanceData);
	FNDIHairStrandsData& TargetData = SystemInstancesToProxyData.FindOrAdd(Instance);

	TargetData.WorldTransform = SourceData->WorldTransform;
	TargetData.NumStrands = SourceData->NumStrands;
	TargetData.StrandSize = SourceData->StrandSize;
	TargetData.BoxCenter = SourceData->BoxCenter;
	TargetData.BoxExtent = SourceData->BoxExtent;
	TargetData.TickCount = SourceData->TickCount;
	TargetData.ResetTick = SourceData->ResetTick;
	TargetData.ForceReset = SourceData->ForceReset;
	TargetData.HairStrandsBuffer = SourceData->HairStrandsBuffer;
}

void FNDIHairStrandsProxy::InitializePerInstanceData(const FNiagaraSystemInstanceID& SystemInstance, FNDIHairStrandsBuffer* HairStrandsBuffer, 
	const uint32 NumStrands, const uint8 StrandSize, const FVector& BoxCenter, const FVector& BoxExtent, const FTransform& WorldTransform)
{
	check(IsInRenderingThread());

	FNDIHairStrandsData& TargetData = SystemInstancesToProxyData.FindOrAdd(SystemInstance);
	TargetData.HairStrandsBuffer = HairStrandsBuffer;
	TargetData.NumStrands = NumStrands;
	TargetData.StrandSize = StrandSize;
	TargetData.BoxCenter = BoxCenter;
	TargetData.BoxExtent = BoxExtent;
	TargetData.TickCount = 0;
	TargetData.ResetTick = MaxDelay;
	TargetData.ForceReset = true;
	TargetData.WorldTransform = WorldTransform;
}

void FNDIHairStrandsProxy::DestroyPerInstanceData(NiagaraEmitterInstanceBatcher* Batcher, const FNiagaraSystemInstanceID& SystemInstance)
{
	check(IsInRenderingThread());

	DeferredDestroyList.Add(SystemInstance);
	Batcher->EnqueueDeferredDeletesForDI_RenderThread(this->AsShared());
}

//------------------------------------------------------------------------------------------------------------

UNiagaraDataInterfaceHairStrands::UNiagaraDataInterfaceHairStrands(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, StrandSize(EHairStrandsSize::Size4)
	, DefaultSource(nullptr)
	, SourceActor(nullptr)
	, SourceComponent(nullptr)
	, GroupIndex(0)
{

	Proxy = MakeShared<FNDIHairStrandsProxy, ESPMode::ThreadSafe>();
}

bool UNiagaraDataInterfaceHairStrands::IsComponentValid() const
{
	return (SourceComponent.IsValid() && SourceComponent != nullptr);
}

void UNiagaraDataInterfaceHairStrands::UpdateSourceComponent(FNiagaraSystemInstance* SystemInstance)
{
	SourceComponent = nullptr;
	if (SourceActor)
	{
		AGroomActor* HairStrandsActor = Cast<AGroomActor>(SourceActor);
		if (HairStrandsActor != nullptr)
		{
			SourceComponent = HairStrandsActor->GetGroomComponent();
		}
		else
		{
			SourceComponent = SourceActor->FindComponentByClass<UGroomComponent>();
		}
	}
	else
	{
		if (UNiagaraComponent* SimComp = SystemInstance->GetComponent())
		{
			if (UGroomComponent* ParentComp = Cast<UGroomComponent>(SimComp->GetAttachParent()))
			{
				SourceComponent = ParentComp;
				SourceComponent = ParentComp;
			}
			else if (UGroomComponent* OuterComp = SimComp->GetTypedOuter<UGroomComponent>())
			{
				SourceComponent = OuterComp;
			}
			else if (AActor* Owner = SimComp->GetAttachmentRootActor())
			{
				for (UActorComponent* ActorComp : Owner->GetComponents())
				{
					UGroomComponent* SourceComp = Cast<UGroomComponent>(ActorComp);
					if (SourceComp && SourceComp->GroomAsset)
					{
						SourceComponent = SourceComp;
						break;
					}
				}
			}
		}
	}
}

void UNiagaraDataInterfaceHairStrands::ExtractDatasAndResources(FNiagaraSystemInstance* SystemInstance,FHairStrandsDatas*& OutStrandsDatas, 
	FHairStrandsRestResource*& OutStrandsRestResource, FHairStrandsDeformedResource*& OutStrandsDeformedResource, FHairStrandsRootResource*& OutStrandsRootResource)
{
	UpdateSourceComponent(SystemInstance);

	OutStrandsDatas = nullptr;
	OutStrandsRestResource = nullptr;
	OutStrandsDeformedResource = nullptr;
	OutStrandsRootResource = nullptr;

	if (IsComponentValid())
	{
		OutStrandsDatas = SourceComponent->GetGuideStrandsDatas(GroupIndex);
		OutStrandsRestResource = SourceComponent->GetGuideStrandsRestResource(GroupIndex);
		OutStrandsDeformedResource = SourceComponent->GetGuideStrandsDeformedResource(GroupIndex);
		OutStrandsRootResource = SourceComponent->GetGuideStrandsRootResource(GroupIndex);
	}
	else if (DefaultSource != nullptr && (GroupIndex < DefaultSource->GetNumHairGroups()))
	{
		OutStrandsDatas = &DefaultSource->HairGroupsData[GroupIndex].HairRenderData;
		OutStrandsRestResource = DefaultSource->HairGroupsData[GroupIndex].HairStrandsRestResource;
	}
}

bool UNiagaraDataInterfaceHairStrands::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIHairStrandsData* InstanceData = new (PerInstanceData) FNDIHairStrandsData();
	check(InstanceData);

	FHairStrandsDatas* StrandsDatas =  nullptr;
	FHairStrandsRestResource* StrandsRestResource = nullptr;
	FHairStrandsDeformedResource* StrandsDeformedResource = nullptr;
	FHairStrandsRootResource* StrandsRootResource = nullptr;

	ExtractDatasAndResources(SystemInstance, StrandsDatas, StrandsRestResource, StrandsDeformedResource, StrandsRootResource);

	if (StrandsDatas == nullptr || StrandsRestResource == nullptr)
	{
		UE_LOG(LogHairStrands, Log, TEXT("Hair Strands data interface has no valid asset. Failed InitPerInstanceData - %s %d"), *GetFullName(), DefaultSource);
		return false;
	}
	else
	{ 
		FNDIHairStrandsBuffer* HairStrandsBuffer = new FNDIHairStrandsBuffer;
		HairStrandsBuffer->SetHairAsset(StrandsDatas, StrandsRestResource, StrandsDeformedResource, StrandsRootResource);

		// Push instance data to RT
		{
			const uint32 NumStrands = StrandsDatas->GetNumCurves();
			const uint8 StrandsSize = static_cast<uint8>(StrandSize);
			const FBox& StrandsBox = StrandsDatas->BoundingBox;

			const FVector BoxExtent = StrandsBox.GetExtent();
			const FVector BoxCenter = StrandsBox.GetCenter(); 

			const FTransform WorldTransform = IsComponentValid() ? SourceComponent->GetComponentToWorld():
				SystemInstance->GetComponent()->GetComponentToWorld();

			InstanceData->WorldTransform = WorldTransform;
			InstanceData->StrandSize = StrandsSize;
			InstanceData->NumStrands = NumStrands;
			InstanceData->HairStrandsBuffer = HairStrandsBuffer;
			InstanceData->BoxCenter = BoxCenter;
			InstanceData->BoxExtent = BoxExtent;
			InstanceData->TickCount = 0;
			InstanceData->ForceReset = true;
			InstanceData->ResetTick = MaxDelay;

			//UE_LOG(LogHairStrands, Log, TEXT("Num Strands = %d | Strand Size = %d | Num Vertices = %d | Min = %f %f %f | Max = %f %f %f | Transform = %s"), NumStrands, StrandSize,
			//	StrandsDatas->GetNumPoints(), StrandsBox.Min[0], StrandsBox.Min[1], StrandsBox.Min[2], StrandsBox.Max[0], StrandsBox.Max[1], StrandsBox.Max[2], *InstanceData->WorldTransform.ToString());

			FNDIHairStrandsProxy* ThisProxy = GetProxyAs<FNDIHairStrandsProxy>();
			ENQUEUE_RENDER_COMMAND(FNiagaraDIPushInitialInstanceDataToRT) (
				[ThisProxy, InstanceID = SystemInstance->GetId(), HairStrandsBuffer, NumStrands, StrandsSize, BoxCenter, BoxExtent, WorldTransform](FRHICommandListImmediate& CmdList)
			{
				HairStrandsBuffer->InitResource();
				ThisProxy->InitializePerInstanceData(InstanceID, HairStrandsBuffer, NumStrands, StrandsSize, BoxCenter, BoxExtent, WorldTransform);
			}
			);
		}
	}
	return true;
}

void UNiagaraDataInterfaceHairStrands::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIHairStrandsData* InstanceData = static_cast<FNDIHairStrandsData*>(PerInstanceData);
	InstanceData->~FNDIHairStrandsData();

	if (InstanceData->HairStrandsBuffer)
	{
		FNDIHairStrandsProxy* ThisProxy = GetProxyAs<FNDIHairStrandsProxy>();
		FNDIHairStrandsBuffer* HairStrandsBuffer = InstanceData->HairStrandsBuffer;
		ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
			[ThisProxy, HairStrandsBuffer, InstanceID = SystemInstance->GetId(), Batcher = SystemInstance->GetBatcher()](FRHICommandListImmediate& CmdList)
		{
			HairStrandsBuffer->ReleaseResource();
			ThisProxy->SystemInstancesToProxyData.Remove(InstanceID);
			delete HairStrandsBuffer;
		}
		);
		InstanceData->HairStrandsBuffer = nullptr;
	}
}

bool UNiagaraDataInterfaceHairStrands::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float InDeltaSeconds)
{
	FNDIHairStrandsData* InstanceData = static_cast<FNDIHairStrandsData*>(PerInstanceData);

	FHairStrandsDatas* StrandsDatas = nullptr;
	FHairStrandsRestResource* StrandsRestResource = nullptr;
	FHairStrandsDeformedResource* StrandsDeformedResource = nullptr;
	FHairStrandsRootResource* StrandsRootResource = nullptr;
	InstanceData->TickCount = FMath::Min(MaxDelay+1,InstanceData->TickCount+1);

	ExtractDatasAndResources(SystemInstance, StrandsDatas, StrandsRestResource, StrandsDeformedResource, StrandsRootResource);
	InstanceData->HairStrandsBuffer->SetHairAsset(StrandsDatas, StrandsRestResource, StrandsDeformedResource, StrandsRootResource);

	if (SourceComponent != nullptr)
	{
		if (!InstanceData->ForceReset && !SourceComponent->bResetSimulation && (InstanceData->ResetTick == MaxDelay))
		{
			InstanceData->ResetTick = FMath::Min(MaxDelay, InstanceData->TickCount+1);
		}
		//UE_LOG(LogHairStrands, Warning, TEXT("Reset Simulation : %d %d %d %d"), InstanceData->ForceReset, SourceComponent->bResetSimulation, InstanceData->TickCount, InstanceData->ResetTick);
		InstanceData->ForceReset = SourceComponent->bResetSimulation;
	}

	bool RequireReset = false;
	if (StrandsDatas)
	{
		if (StrandsDeformedResource != nullptr)
		{
			InstanceData->BoxCenter = StrandsDeformedResource->PositionOffset;
		}
		if (IsComponentValid())
		{
			InstanceData->WorldTransform = SourceComponent->GetComponentToWorld();
			//UE_LOG(LogHairStrands, Warning, TEXT("Get Component Transform : %s"), *InstanceData->WorldTransform.ToString());
		}
		else
		{
			InstanceData->WorldTransform = SystemInstance->GetComponent()->GetComponentToWorld();
			//UE_LOG(LogHairStrands, Warning, TEXT("Get Instance Transform : %s"), *InstanceData->WorldTransform.ToString());
		}
	}
	return RequireReset;
}

bool UNiagaraDataInterfaceHairStrands::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceHairStrands* OtherTyped = CastChecked<UNiagaraDataInterfaceHairStrands>(Destination);
	OtherTyped->StrandSize = StrandSize;
	OtherTyped->SourceActor= SourceActor;
	OtherTyped->SourceComponent = SourceComponent;
	OtherTyped->DefaultSource = DefaultSource;
	OtherTyped->GroupIndex = GroupIndex;

	return true;
}

bool UNiagaraDataInterfaceHairStrands::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceHairStrands* OtherTyped = CastChecked<const UNiagaraDataInterfaceHairStrands>(Other);

	return  (OtherTyped->StrandSize == StrandSize)
		&& (OtherTyped->SourceActor == SourceActor) && (OtherTyped->SourceComponent == SourceComponent)
		&& (OtherTyped->DefaultSource == DefaultSource) && (OtherTyped->GroupIndex == GroupIndex);
}

void UNiagaraDataInterfaceHairStrands::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), true, false, false);
	}
}

void UNiagaraDataInterfaceHairStrands::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetNumStrandsName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Num Strands")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetStrandSizeName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Strand Size")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetWorldTransformName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("World Transform")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetWorldInverseName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetMatrix4Def(), TEXT("World Inverse")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetPointPositionName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Vertex Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Vertex Position")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ComputeNodePositionName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Node Index")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Smoothing Filter")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ComputeNodeOrientationName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Node Index")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Node Orientation")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ComputeNodeMassName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Node Index")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Strands Density")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Root Thickness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Tip Thickness")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Node Mass")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ComputeNodeInertiaName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Node Index")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Strands Density")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Root Thickness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Tip Thickness")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Inertia")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ComputeEdgeLengthName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Node Index")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Node Offset")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Edge Length")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ComputeEdgeRotationName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Node Index")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Node Orientation")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Edge Rotation")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ComputeRestPositionName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Rest Position")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ComputeRestOrientationName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Node Orientation")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Rest Orientation")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ComputeLocalStateName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Node Index")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Rest Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Rest Orientation")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Local Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Local Orientation")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = AdvectNodePositionName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Node Mass")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Position Mobile")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("External Force")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Force Gradient")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Linear Velocity")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Linear Velocity")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = AdvectNodeOrientationName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Inertia")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Orientation Mobile")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("External Torque")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Torque Gradient")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Angular Velocity")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Node Orientation")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Angular Velocity")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Node Orientation"))); 

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = UpdateLinearVelocityName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Previous Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Linear Velocity")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = UpdateAngularVelocityName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Previous Orientation")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Node Orientation")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Angular Velocity")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = AttachNodePositionName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Rest Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = AttachNodeOrientationName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Rest Orientation")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Node Orientation")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = AttachNodeStateName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Node Index")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Local Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Local Orientation")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Node Orientation")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = UpdatePointPositionName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Node Index")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Displace")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Report Status")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ResetPointPositionName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Node Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Report Status")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetBoxCenterName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Grid Center")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetBoxExtentName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Grid Extent")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = BuildBoundingBoxName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Build Status")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Bound Min")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Bound Max")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SetupDistanceSpringMaterialName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Stretch Stiffness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Node Thickness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Rest Length")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Node Offset")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Damping")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Compliance")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Weight")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Multiplier")));
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SolveDistanceSpringMaterialName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Rest Length")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Node Offset")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Damping")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Compliance")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Weight")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Multiplier")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Multiplier")));
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ProjectDistanceSpringMaterialName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Stretch Stiffness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Node Thickness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Rest Length")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Node Mass")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Node Offset")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SetupAngularSpringMaterialName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Bend Stiffness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Node Thickness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Rest Length")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Damping")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Compliance")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Weight")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Material Multiplier")));
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SolveAngularSpringMaterialName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Rest Length")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Rest Direction")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Damping")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Compliance")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Weight")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Material Multiplier")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Material Multiplier")));
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ProjectAngularSpringMaterialName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Bend Stiffness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Node Thickness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Rest Length")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Rest Direction")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Node Mass")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));

		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SetupStretchRodMaterialName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Stretch Stiffness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Node Thickness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Rest Length")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Damping")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Compliance")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Weight")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Material Multiplier")));
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SolveStretchRodMaterialName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Rest Length")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Damping")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Compliance")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Weight")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Material Multiplier")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Material Multiplier")));
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ProjectStretchRodMaterialName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Stretch Stiffness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Node Thickness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Rest Length")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Material Multiplier")));

		OutFunctions.Add(Sig);
	}

	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SetupBendRodMaterialName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Bend Stiffness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Node Thickness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Rest Length")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Damping")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Compliance")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Weight")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Material Multiplier")));
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SolveBendRodMaterialName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Rest Length")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Rest Darboux")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Damping")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Compliance")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Weight")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Material Multiplier")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Material Multiplier")));
		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ProjectBendRodMaterialName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Stretch Stiffness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Node Thickness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Rest Length")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Rest Darboux")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Material Multiplier")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SolveStaticCollisionConstraintName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Penetration Depth")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Collision Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Collision Velocity")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Collision Normal")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Static Friction")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Kinetic Friction")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Previous Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Constraint Multiplier")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ProjectStaticCollisionConstraintName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Penetration Depth")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Collision Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Collision Velocity")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Collision Normal")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Static Friction")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Kinetic Friction")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Previous Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ComputeRestDirectionName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Node Orientation")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Rest Direction")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = UpdateNodeOrientationName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Update Status")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ComputeAirDragForceName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Air Density")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Air Viscosity")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Air Drag")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Air Velocity")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Node Thickness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Velocity")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Drag Force")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Drag Gradient")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = NeedSimulationResetName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Reset Simulation")));

		OutFunctions.Add(Sig);
	}
}

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetNumStrands);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetStrandSize);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetWorldTransform);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetWorldInverse);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetPointPosition);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeNodePosition);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeNodeOrientation);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeNodeMass);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeNodeInertia);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeEdgeLength);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeEdgeRotation);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeRestPosition);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeRestOrientation);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeLocalState);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, AttachNodePosition);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, AttachNodeOrientation);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, AttachNodeState);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, UpdatePointPosition);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ResetPointPosition);

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetBoxExtent);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetBoxCenter);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, BuildBoundingBox);

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, AdvectNodePosition);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, AdvectNodeOrientation);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, UpdateLinearVelocity);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, UpdateAngularVelocity);

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SetupDistanceSpringMaterial);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SolveDistanceSpringMaterial);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ProjectDistanceSpringMaterial);

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SetupAngularSpringMaterial);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SolveAngularSpringMaterial);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ProjectAngularSpringMaterial);

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SetupStretchRodMaterial);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SolveStretchRodMaterial);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ProjectStretchRodMaterial);

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SetupBendRodMaterial);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SolveBendRodMaterial);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ProjectBendRodMaterial);

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SolveStaticCollisionConstraint);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ProjectStaticCollisionConstraint);

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeRestDirection);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, UpdateNodeOrientation);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeAirDragForce);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, NeedSimulationReset);

void UNiagaraDataInterfaceHairStrands::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == GetNumStrandsName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetNumStrands)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetStrandSizeName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetStrandSize)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetWorldTransformName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 16);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetWorldTransform)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetWorldInverseName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 16);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetWorldInverse)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetPointPositionName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetPointPosition)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ComputeNodePositionName)
	{
		check(BindingInfo.GetNumInputs() == 3 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeNodePosition)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ComputeNodeOrientationName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 4);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeNodeOrientation)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ComputeNodeMassName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeNodeMass)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ComputeNodeInertiaName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeNodeInertia)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ComputeEdgeLengthName)
	{
		check(BindingInfo.GetNumInputs() == 6 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeEdgeLength)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ComputeEdgeRotationName)
	{
		check(BindingInfo.GetNumInputs() == 6 && BindingInfo.GetNumOutputs() == 4);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeEdgeRotation)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ComputeRestPositionName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeRestPosition)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ComputeRestOrientationName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 4);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeRestOrientation)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ComputeLocalStateName)
	{
		check(BindingInfo.GetNumInputs() == 9 && BindingInfo.GetNumOutputs() == 7);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeLocalState)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == AttachNodePositionName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, AttachNodePosition)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == AttachNodeOrientationName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 4);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, AttachNodeOrientation)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == AttachNodeStateName)
	{
		check(BindingInfo.GetNumInputs() == 9 && BindingInfo.GetNumOutputs() == 7);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, AttachNodeState)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == UpdatePointPositionName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, UpdatePointPosition)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ResetPointPositionName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ResetPointPosition)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == AdvectNodePositionName)
	{
		check(BindingInfo.GetNumInputs() == 16 && BindingInfo.GetNumOutputs() == 6);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, AdvectNodePosition)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == AdvectNodeOrientationName)
	{
		check(BindingInfo.GetNumInputs() == 19 && BindingInfo.GetNumOutputs() == 7);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, AdvectNodeOrientation)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == UpdateLinearVelocityName)
	{
		check(BindingInfo.GetNumInputs() == 8 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, UpdateLinearVelocity)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == UpdateAngularVelocityName)
	{
		check(BindingInfo.GetNumInputs() == 10 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, UpdateAngularVelocity)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetBoxExtentName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetBoxExtent)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetBoxCenterName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetBoxCenter)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == BuildBoundingBoxName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 7);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, BuildBoundingBox)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SetupDistanceSpringMaterialName)
	{
		check(BindingInfo.GetNumInputs() == 7 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SetupDistanceSpringMaterial)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SolveDistanceSpringMaterialName)
	{
		check(BindingInfo.GetNumInputs() == 8 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SolveDistanceSpringMaterial)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ProjectDistanceSpringMaterialName)
	{
		check(BindingInfo.GetNumInputs() == 10 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ProjectDistanceSpringMaterial)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SetupAngularSpringMaterialName)
	{
		check(BindingInfo.GetNumInputs() == 6 && BindingInfo.GetNumOutputs() == 5);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SetupAngularSpringMaterial)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SolveAngularSpringMaterialName)
	{
		check(BindingInfo.GetNumInputs() == 12 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SolveAngularSpringMaterial)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ProjectAngularSpringMaterialName)
	{
		check(BindingInfo.GetNumInputs() == 12 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ProjectAngularSpringMaterial)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SetupStretchRodMaterialName)
	{
		check(BindingInfo.GetNumInputs() == 6 && BindingInfo.GetNumOutputs() == 5);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SetupStretchRodMaterial)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SolveStretchRodMaterialName)
	{
		check(BindingInfo.GetNumInputs() == 9 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SolveStretchRodMaterial)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ProjectStretchRodMaterialName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ProjectStretchRodMaterial)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SetupBendRodMaterialName)
	{
		check(BindingInfo.GetNumInputs() == 6 && BindingInfo.GetNumOutputs() == 5);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SetupBendRodMaterial)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SolveBendRodMaterialName)
	{
		check(BindingInfo.GetNumInputs() == 13 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SolveBendRodMaterial)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ProjectBendRodMaterialName)
	{
		check(BindingInfo.GetNumInputs() == 9 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ProjectBendRodMaterial)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SolveStaticCollisionConstraintName)
	{
		check(BindingInfo.GetNumInputs() == 17 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SolveStaticCollisionConstraint)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ProjectStaticCollisionConstraintName)
	{
		check(BindingInfo.GetNumInputs() == 20 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ProjectStaticCollisionConstraint)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ComputeRestDirectionName)
	{
		check(BindingInfo.GetNumInputs() == 8 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeRestDirection)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == UpdateNodeOrientationName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, UpdateNodeOrientation)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ComputeAirDragForceName)
	{
		check(BindingInfo.GetNumInputs() == 14 && BindingInfo.GetNumOutputs() == 6);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeAirDragForce)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == NeedSimulationResetName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, NeedSimulationReset)::Bind(this, OutFunc);
	}
}

void WriteTransform(const FMatrix& ToWrite, FVectorVMContext& Context)
{
	VectorVM::FExternalFuncRegisterHandler<float> Out00(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out01(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out02(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out03(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out04(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out05(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out06(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out07(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out08(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out09(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out10(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out11(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out12(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out13(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out14(Context);
	VectorVM::FExternalFuncRegisterHandler<float> Out15(Context);

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		*Out00.GetDest() = ToWrite.M[0][0]; Out00.Advance();
		*Out01.GetDest() = ToWrite.M[0][1]; Out01.Advance();
		*Out02.GetDest() = ToWrite.M[0][2]; Out02.Advance();
		*Out03.GetDest() = ToWrite.M[0][3]; Out03.Advance();
		*Out04.GetDest() = ToWrite.M[1][0]; Out04.Advance();
		*Out05.GetDest() = ToWrite.M[1][1]; Out05.Advance();
		*Out06.GetDest() = ToWrite.M[1][2]; Out06.Advance();
		*Out07.GetDest() = ToWrite.M[1][3]; Out07.Advance();
		*Out08.GetDest() = ToWrite.M[2][0]; Out08.Advance();
		*Out09.GetDest() = ToWrite.M[2][1]; Out09.Advance();
		*Out10.GetDest() = ToWrite.M[2][2]; Out10.Advance();
		*Out11.GetDest() = ToWrite.M[2][3]; Out11.Advance();
		*Out12.GetDest() = ToWrite.M[3][0]; Out12.Advance();
		*Out13.GetDest() = ToWrite.M[3][1]; Out13.Advance();
		*Out14.GetDest() = ToWrite.M[3][2]; Out14.Advance();
		*Out15.GetDest() = ToWrite.M[3][3]; Out15.Advance();
	}
}

void UNiagaraDataInterfaceHairStrands::GetNumStrands(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutNumStrands(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutNumStrands.GetDestAndAdvance() = InstData->NumStrands;
	}
}

void UNiagaraDataInterfaceHairStrands::GetStrandSize(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutStrandSize(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutStrandSize.GetDestAndAdvance() = InstData->StrandSize;
	}
}

void UNiagaraDataInterfaceHairStrands::GetWorldTransform(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	FMatrix WorldTransform = InstData->WorldTransform.ToMatrixWithScale();

	WriteTransform(WorldTransform, Context);
}

void UNiagaraDataInterfaceHairStrands::GetWorldInverse(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	FMatrix WorldInverse = InstData->WorldTransform.ToMatrixWithScale().Inverse();

	WriteTransform(WorldInverse, Context);
}

void UNiagaraDataInterfaceHairStrands::BuildBoundingBox(FVectorVMContext& Context)
{}

void UNiagaraDataInterfaceHairStrands::GetBoxCenter(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutBoxCenterX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutBoxCenterY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutBoxCenterZ(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutBoxCenterX.GetDestAndAdvance() = InstData->BoxCenter.X;
		*OutBoxCenterY.GetDestAndAdvance() = InstData->BoxCenter.Y;
		*OutBoxCenterZ.GetDestAndAdvance() = InstData->BoxCenter.Z;
	}
}

void UNiagaraDataInterfaceHairStrands::GetBoxExtent(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutBoxExtentX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutBoxExtentY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutBoxExtentZ(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutBoxExtentX.GetDestAndAdvance() = InstData->BoxExtent.X;
		*OutBoxExtentY.GetDestAndAdvance() = InstData->BoxExtent.Y;
		*OutBoxExtentZ.GetDestAndAdvance() = InstData->BoxExtent.Z;
	}
}

void UNiagaraDataInterfaceHairStrands::GetPointPosition(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::ComputeNodePosition(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::ComputeNodeOrientation(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::ComputeNodeMass(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::ComputeNodeInertia(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::ComputeEdgeLength(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::ComputeEdgeRotation(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::ComputeRestPosition(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::ComputeRestOrientation(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::ComputeLocalState(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::UpdatePointPosition(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::ResetPointPosition(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::AttachNodePosition(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::AttachNodeOrientation(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::AttachNodeState(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::AdvectNodePosition(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::AdvectNodeOrientation(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::UpdateLinearVelocity(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::UpdateAngularVelocity(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::SetupDistanceSpringMaterial(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::SolveDistanceSpringMaterial(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::ProjectDistanceSpringMaterial(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::SetupAngularSpringMaterial(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::SolveAngularSpringMaterial(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::ProjectAngularSpringMaterial(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}


void UNiagaraDataInterfaceHairStrands::SetupStretchRodMaterial(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::SolveStretchRodMaterial(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::ProjectStretchRodMaterial(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::SetupBendRodMaterial(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::SolveBendRodMaterial(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::ProjectBendRodMaterial(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::ComputeRestDirection(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::UpdateNodeOrientation(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::SolveStaticCollisionConstraint(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::ProjectStaticCollisionConstraint(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::ComputeAirDragForce(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::NeedSimulationReset(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

bool UNiagaraDataInterfaceHairStrands::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, FString& OutHLSL)
{
	FNDIHairStrandsParametersName ParamNames(ParamInfo.DataInterfaceHLSLSymbol);

	TMap<FString, FStringFormatArg> ArgsSample = {
		{TEXT("InstanceFunctionName"), InstanceFunctionName},
		{TEXT("NumStrandsName"), ParamNames.NumStrandsName},
		{TEXT("StrandSizeName"), ParamNames.StrandSizeName},
		{TEXT("WorldTransformName"), ParamNames.WorldTransformName},
		{TEXT("WorldInverseName"), ParamNames.WorldInverseName},
		{TEXT("WorldRotationName"), ParamNames.WorldRotationName},
		{TEXT("DeformedPositionBufferName"), ParamNames.DeformedPositionBufferName},
		{TEXT("CurvesOffsetsBufferName"), ParamNames.CurvesOffsetsBufferName},
		{TEXT("RestPositionBufferName"), ParamNames.RestPositionBufferName},
		{TEXT("BoxCenterName"), ParamNames.BoxCenterName},
		{TEXT("BoxExtentName"), ParamNames.BoxExtentName},
		{TEXT("HairStrandsContextName"), TEXT("DIHAIRSTRANDS_MAKE_CONTEXT(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")")},
	};

	if (DefinitionFunctionName == GetStrandSizeName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
		void {InstanceFunctionName}(out int OutStrandSize)
		{
			OutStrandSize = {StrandSizeName};
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == GetNumStrandsName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
		void {InstanceFunctionName}(out int OutNumStrands)
		{
			OutNumStrands = {NumStrandsName};
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == GetWorldTransformName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
		void {InstanceFunctionName}(out float4x4 OutWorldTransform)
		{
			OutWorldTransform = {WorldTransformName};
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == GetWorldInverseName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
		void {InstanceFunctionName}(out float4x4 OutWorldInverse)
		{
			OutWorldInverse = {WorldInverseName};
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == GetPointPositionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in int PointIndex, out float3 OutPointPosition)
			{
				{HairStrandsContextName} DIHairStrands_GetPointPosition(DIContext,PointIndex,OutPointPosition);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == ComputeNodePositionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in int NodeIndex, in float SmoothingFilter, out float3 OutNodePosition)
			{
				{HairStrandsContextName} DIHairStrands_ComputeNodePosition(DIContext,NodeIndex,OutNodePosition);
				DIHairStrands_SmoothNodePosition(DIContext,NodeIndex,SmoothingFilter,OutNodePosition);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == ComputeNodeOrientationName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in int NodeIndex, in float3 NodePosition, out float4 OutNodeOrientation)
			{
				{HairStrandsContextName} DIHairStrands_ComputeNodeOrientation(DIContext,NodeIndex,NodePosition,OutNodeOrientation);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == ComputeNodeMassName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in int NodeIndex, in float StrandsDensity, in float RootThickness, in float TipThickness, out float OutNodeMass)
			{
				{HairStrandsContextName} DIHairStrands_ComputeNodeMass(DIContext,NodeIndex,StrandsDensity,RootThickness,TipThickness,OutNodeMass);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == ComputeNodeInertiaName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in int NodeIndex, in float StrandsDensity, in float RootThickness, in float TipThickness, out float3 OutNodeInertia)
			{
				{HairStrandsContextName} DIHairStrands_ComputeNodeInertia(DIContext,NodeIndex,StrandsDensity,RootThickness,TipThickness,OutNodeInertia);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == ComputeEdgeLengthName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in int NodeIndex, in float3 NodePosition, in int NodeOffset, out float OutEdgeLength)
			{
				{HairStrandsContextName} DIHairStrands_ComputeEdgeLength(DIContext,NodeIndex,NodePosition,NodeOffset,OutEdgeLength);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == ComputeEdgeRotationName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in int NodeIndex, in float4 NodeOrientation, out float4 OutEdgeRotation)
			{
				{HairStrandsContextName} DIHairStrands_ComputeEdgeRotation(DIContext,NodeIndex,NodeOrientation,OutEdgeRotation);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == ComputeRestPositionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in float3 NodePosition, out float3 OutRestPosition)
			{
				{HairStrandsContextName} DIHairStrands_ComputeRestPosition(DIContext,NodePosition,OutRestPosition);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == ComputeRestOrientationName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in float4 NodeOrientation, out float4 OutRestOrientation)
			{
				{HairStrandsContextName} DIHairStrands_ComputeRestOrientation(DIContext,NodeOrientation,OutRestOrientation);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == ComputeLocalStateName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in int NodeIndex, in float3 RestPosition, in float4 RestOrientation, out float3 LocalPosition, out float4 LocalOrientation)
			{
				{HairStrandsContextName} DIHairStrands_ComputeLocalState(DIContext,NodeIndex,RestPosition,RestOrientation,LocalPosition,LocalOrientation);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == AttachNodePositionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in float3 RestPosition, out float3 NodePosition)
			{
				{HairStrandsContextName} DIHairStrands_AttachNodePosition(DIContext,RestPosition,NodePosition);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == AttachNodeOrientationName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} (in float4 RestOrientation, out float4 NodeOrientation)
				{
					{HairStrandsContextName} DIHairStrands_AttachNodeOrientation(DIContext,RestOrientation,NodeOrientation);
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}	
	else if (DefinitionFunctionName == AttachNodeStateName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} (in int NodeIndex, in float3 LocalPosition, in float4 LocalOrientation, out float3 NodePosition, out float4 NodeOrientation)
				{
					{HairStrandsContextName} DIHairStrands_AttachNodeState(DIContext,NodeIndex,LocalPosition,LocalOrientation,NodePosition,NodeOrientation);
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == UpdatePointPositionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in int NodeIndex, in float3 NodeDisplace, out bool OutReportStatus)
			{
				{HairStrandsContextName} DIHairStrands_UpdatePointPosition(DIContext,NodeIndex,NodeDisplace,OutReportStatus);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == ResetPointPositionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in int NodeIndex, out bool OutReportStatus)
			{
				{HairStrandsContextName} DIHairStrands_ResetPointPosition(DIContext,NodeIndex,OutReportStatus);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == AdvectNodePositionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in float NodeMass, in bool IsPositionMobile, in float3 ExternalForce, in float3 ForceGradient, in float DeltaTime, 
									     in float3 LinearVelocity, in float3 NodePosition, out float3 OutLinearVelocity, out float3 OutNodePosition)
			{
				OutLinearVelocity = LinearVelocity;
				OutNodePosition = NodePosition;
				{HairStrandsContextName} DIHairStrands_AdvectNodePosition(DIContext,NodeMass,IsPositionMobile,ExternalForce,ForceGradient,DeltaTime,OutLinearVelocity,OutNodePosition);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == AdvectNodeOrientationName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in float3 NodeInertia, in bool IsOrientationMobile, in float3 ExternalTorque, in float3 TorqueGradient, in float DeltaTime, 
										 in float3 AngularVelocity, in float4 NodeOrientation, out float3 OutAngularVelocity, out float4 OutNodeOrientation)
			{
				OutAngularVelocity = AngularVelocity;
				OutNodeOrientation = NodeOrientation;
				{HairStrandsContextName} DIHairStrands_AdvectNodeOrientation(DIContext,NodeInertia,IsOrientationMobile,ExternalTorque,TorqueGradient,DeltaTime,OutAngularVelocity,OutNodeOrientation);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == UpdateLinearVelocityName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in float3 PreviousPosition, in float3 NodePosition, in float DeltaTime, out float3 OutLinearVelocity)
			{
				{HairStrandsContextName} DIHairStrands_UpdateLinearVelocity(DIContext,PreviousPosition,NodePosition,DeltaTime,OutLinearVelocity);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == UpdateAngularVelocityName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in float4 PreviousOrientation, in float4 NodeOrientation, in float DeltaTime, out float3 OutAngularVelocity)
			{
				{HairStrandsContextName} DIHairStrands_UpdateAngularVelocity(DIContext,PreviousOrientation,NodeOrientation,DeltaTime,OutAngularVelocity);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == GetBoxExtentName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} (out float3 OutBoxExtent)
				{
					{HairStrandsContextName} DIHairStrands_GetBoxExtent(DIContext,OutBoxExtent);
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == GetBoxCenterName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} (out float3 OutBoxCenter)
				{
					{HairStrandsContextName} DIHairStrands_GetBoxCenter(DIContext,OutBoxCenter);
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == BuildBoundingBoxName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
					void {InstanceFunctionName} (in float3 NodePosition, out bool OutBuildStatus, out float3 OutBoundMin, out float3 OutBoundMax)
					{
						{HairStrandsContextName} DIHairStrands_BuildBoundingBox(DIContext,NodePosition,OutBuildStatus,OutBoundMin,OutBoundMax);
					}
					)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == SetupDistanceSpringMaterialName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} (in float YoungModulus, in float RodThickness, 
in float RestLength, in float DeltaTime, in int NodeOffset, in float MaterialDamping, out float OutMaterialCompliance, out float OutMaterialWeight, out float OutMaterialMultiplier)
				{
					{HairStrandsContextName} 
					if(NodeOffset == 0)
					{
						SetupStretchSpringMaterial(DIContext.StrandSize,YoungModulus,RodThickness,RestLength,DeltaTime,false,MaterialDamping,OutMaterialCompliance,OutMaterialWeight,OutMaterialMultiplier);
					}
					else if( NodeOffset == 1)
					{
						SetupBendSpringMaterial(DIContext.StrandSize,YoungModulus,RodThickness,RestLength,DeltaTime,false,MaterialDamping,OutMaterialCompliance,OutMaterialWeight,OutMaterialMultiplier);
					}
					else if( NodeOffset == 2)
					{
						SetupTwistSpringMaterial(DIContext.StrandSize,YoungModulus,RodThickness,RestLength,DeltaTime,false,MaterialDamping,OutMaterialCompliance,OutMaterialWeight,OutMaterialMultiplier);
					}
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == SolveDistanceSpringMaterialName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} (in float RestLength, in float DeltaTime, in int NodeOffset, in float MaterialDamping, 
		in float MaterialCompliance, in float MaterialWeight, in float MaterialMultiplier, out float OutMaterialMultiplier)
				{
					{HairStrandsContextName} 
					if(NodeOffset == 0)
					{
						SolveStretchSpringMaterial(DIContext.StrandSize,RestLength,DeltaTime,MaterialDamping,MaterialCompliance,MaterialWeight,MaterialMultiplier,OutMaterialMultiplier);
					}
					else if(NodeOffset == 1)
					{
						SolveBendSpringMaterial(DIContext.StrandSize,RestLength,DeltaTime,MaterialDamping,MaterialCompliance,MaterialWeight,MaterialMultiplier,OutMaterialMultiplier);
					}
					else if(NodeOffset == 2)
					{
						SolveTwistSpringMaterial(DIContext.StrandSize,RestLength,DeltaTime,MaterialDamping,MaterialCompliance,MaterialWeight,MaterialMultiplier,OutMaterialMultiplier);
					}
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == ProjectDistanceSpringMaterialName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} (in float YoungModulus, in float RodThickness, in float RestLength, in float NodeMass, in float DeltaTime, in int NodeOffset, in float3 NodePosition, out float3 OutNodePosition)
				{
					{HairStrandsContextName} 
					if(NodeOffset == 0)
					{
						ProjectStretchSpringMaterial(DIContext.StrandSize,YoungModulus,RodThickness,RestLength,NodeMass,DeltaTime,NodePosition,OutNodePosition);
					}
					if(NodeOffset == 1)
					{
						ProjectBendSpringMaterial(DIContext.StrandSize,YoungModulus,RodThickness,RestLength,NodeMass,DeltaTime,NodePosition,OutNodePosition);
					}
					if(NodeOffset == 2)
					{
						ProjectTwistSpringMaterial(DIContext.StrandSize,YoungModulus,RodThickness,RestLength,NodeMass,DeltaTime,NodePosition,OutNodePosition);
					}
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == SetupAngularSpringMaterialName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
					void {InstanceFunctionName} (in float YoungModulus, in float RodThickness, 
	in float RestLength, in float DeltaTime, in float MaterialDamping, out float OutMaterialCompliance, out float OutMaterialWeight, out float3 OutMaterialMultiplier)
					{
						{HairStrandsContextName} SetupAngularSpringMaterial(DIContext.StrandSize,YoungModulus,RodThickness,RestLength,DeltaTime,false,MaterialDamping,OutMaterialCompliance,OutMaterialWeight,OutMaterialMultiplier);
					}
					)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == SolveAngularSpringMaterialName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
					void {InstanceFunctionName} (in float RestLength, in float3 RestDirection, in float DeltaTime, in float MaterialDamping, 
			in float MaterialCompliance, in float MaterialWeight, in float3 MaterialMultiplier, out float3 OutMaterialMultiplier)
					{
						{HairStrandsContextName} SolveAngularSpringMaterial(DIContext.StrandSize,RestLength, RestDirection,DeltaTime,MaterialDamping,MaterialCompliance,MaterialWeight,MaterialMultiplier,OutMaterialMultiplier);
					}
					)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == ProjectAngularSpringMaterialName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
					void {InstanceFunctionName} (in float YoungModulus, in float RodThickness, in float RestLength, in float3 RestDirection, in float NodeMass, in float DeltaTime, in float3 NodePosition, out float3 OutNodePosition)
					{
						{HairStrandsContextName} ProjectAngularSpringMaterial(DIContext.StrandSize,YoungModulus,RodThickness,RestLength,RestDirection,NodeMass,DeltaTime,NodePosition,OutNodePosition);
					}
					)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == SetupStretchRodMaterialName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
					void {InstanceFunctionName} (in float YoungModulus, in float RodThickness, 
	in float RestLength, in float DeltaTime, in float MaterialDamping, out float OutMaterialCompliance, out float OutMaterialWeight, out float3 OutMaterialMultiplier)
					{
						{HairStrandsContextName} SetupStretchRodMaterial(DIContext.StrandSize,YoungModulus,RodThickness,RestLength,DeltaTime,false,MaterialDamping,OutMaterialCompliance,OutMaterialWeight,OutMaterialMultiplier);
					}
					)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == SolveStretchRodMaterialName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
					void {InstanceFunctionName} (in float RestLength, in float DeltaTime, in float MaterialDamping, 
			in float MaterialCompliance, in float MaterialWeight, in float3 MaterialMultiplier, out float3 OutMaterialMultiplier)
					{
						{HairStrandsContextName} SolveStretchRodMaterial(DIContext.StrandSize,RestLength,DeltaTime,MaterialDamping,MaterialCompliance,MaterialWeight,MaterialMultiplier,OutMaterialMultiplier);
					}
					)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == ProjectStretchRodMaterialName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
					void {InstanceFunctionName} (in float YoungModulus, in float RodThickness, in float RestLength, in float DeltaTime, out float3 OutMaterialMultiplier)
					{
						{HairStrandsContextName} ProjectStretchRodMaterial(DIContext.StrandSize,YoungModulus,RodThickness,RestLength,DeltaTime,OutMaterialMultiplier);
					}
					)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == SetupBendRodMaterialName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
						void {InstanceFunctionName} (in float YoungModulus, in float RodThickness, 
		in float RestLength, in float DeltaTime, in float MaterialDamping, out float OutMaterialCompliance, out float OutMaterialWeight, out float3 OutMaterialMultiplier)
						{
							{HairStrandsContextName} SetupBendRodMaterial(DIContext.StrandSize,YoungModulus,RodThickness,RestLength,DeltaTime,false,MaterialDamping,OutMaterialCompliance,OutMaterialWeight,OutMaterialMultiplier);
						}
						)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == SolveBendRodMaterialName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
						void {InstanceFunctionName} (in float RestLength, in float4 RestDarboux, in float DeltaTime, in float MaterialDamping, 
				in float MaterialCompliance, in float MaterialWeight, in float3 MaterialMultiplier, out float3 OutMaterialMultiplier)
						{
							{HairStrandsContextName} SolveBendRodMaterial(DIContext.StrandSize,RestLength,RestDarboux,DeltaTime,MaterialDamping,MaterialCompliance,MaterialWeight,MaterialMultiplier,OutMaterialMultiplier);
						}
						)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == ProjectBendRodMaterialName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
						void {InstanceFunctionName} (in float YoungModulus, in float RodThickness, in float RestLength, in float4 RestDarboux, in float DeltaTime, out float3 OutMaterialMultiplier)
						{
							{HairStrandsContextName} ProjectBendRodMaterial(DIContext.StrandSize,YoungModulus,RodThickness,RestLength,RestDarboux,DeltaTime,OutMaterialMultiplier);
						}
						)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == SolveStaticCollisionConstraintName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
							void {InstanceFunctionName} (in float PenetrationDepth, in float3 CollisionPosition, in float3 CollisionVelocity, in float3 CollisionNormal, 
				in float StaticFriction, in float KineticFriction, in float DeltaTime, in float3 PreviousPosition, out float3 OutMaterialMultiplier )
							{
								OutMaterialMultiplier = float3(0,0,0);
								{HairStrandsContextName} SolveStaticCollisionConstraint(DIContext.StrandSize,PenetrationDepth,
									CollisionPosition,CollisionVelocity,CollisionNormal,StaticFriction,KineticFriction,DeltaTime,false,PreviousPosition,SharedNodePosition[GGroupThreadId.x]);
							}
							)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == ProjectStaticCollisionConstraintName)
	{
			static const TCHAR *FormatSample = TEXT(R"(
						void {InstanceFunctionName} (in float PenetrationDepth, in float3 CollisionPosition, in float3 CollisionVelocity, in float3 CollisionNormal, 
			in float StaticFriction, in float KineticFriction, in float DeltaTime, in float3 PreviousPosition, in float3 NodePosition, out float3 OutNodePosition )
						{
							OutNodePosition = NodePosition;
							{HairStrandsContextName} SolveStaticCollisionConstraint(DIContext.StrandSize,PenetrationDepth,
								CollisionPosition,CollisionVelocity,CollisionNormal,StaticFriction,KineticFriction,DeltaTime,true,PreviousPosition,OutNodePosition);
						}
						)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == ComputeRestDirectionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
							void {InstanceFunctionName} (in float3 NodePosition, in float4 NodeOrientation, out float3 OutRestDirection)
							{
								{HairStrandsContextName} ComputeRestDirection(DIContext.StrandSize,NodePosition,NodeOrientation,OutRestDirection);
							}
							)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == UpdateNodeOrientationName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
					void {InstanceFunctionName} ( out bool OutUpdateStatus)
					{
						{HairStrandsContextName} UpdateMaterialFrame(DIContext.StrandSize);
						OutUpdateStatus = true;
					}
					)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == ComputeAirDragForceName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
						void {InstanceFunctionName} ( in float AirDensity, in float AirViscosity, in float AirDrag, 
	in float3 AirVelocity, in float NodeThickness, in float3 NodePosition, in float3 NodeVelocity, out float3 OutAirDrag, out float3 OutDragGradient )
						{
							{HairStrandsContextName} ComputeAirDragForce(DIContext.StrandSize,AirDensity,AirViscosity,AirDrag,AirVelocity,NodeThickness,NodePosition,NodeVelocity,OutAirDrag,OutDragGradient);
						}
						)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (DefinitionFunctionName == NeedSimulationResetName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} ( out bool ResetSimulation)
				{
					{HairStrandsContextName} ResetSimulation  = DIContext.ResetSimulation;
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}

	OutHLSL += TEXT("\n");
	return false;
}

void UNiagaraDataInterfaceHairStrands::GetCommonHLSL(FString& OutHLSL)
{
	OutHLSL += TEXT("#include \"/Plugin/Experimental/HairStrands/Private/NiagaraQuaternionUtils.ush\"\n");
	OutHLSL += TEXT("#include \"/Plugin/Experimental/HairStrands/Private/NiagaraStrandsExternalForce.ush\"\n");
	OutHLSL += TEXT("#include \"/Plugin/Experimental/HairStrands/Private/NiagaraHookeSpringMaterial.ush\"\n");
	OutHLSL += TEXT("#include \"/Plugin/Experimental/HairStrands/Private/NiagaraCosseratRodMaterial.ush\"\n");
	OutHLSL += TEXT("#include \"/Plugin/Experimental/HairStrands/Private/NiagaraStaticCollisionConstraint.ush\"\n");
	OutHLSL += TEXT("#include \"/Plugin/Experimental/HairStrands/Private/NiagaraDataInterfaceHairStrands.ush\"\n");
}

void UNiagaraDataInterfaceHairStrands::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	OutHLSL += TEXT("DIHAIRSTRANDS_DECLARE_CONSTANTS(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")\n");
}

void UNiagaraDataInterfaceHairStrands::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
{
	check(Proxy);

	FNDIHairStrandsData* GameThreadData = static_cast<FNDIHairStrandsData*>(PerInstanceData);
	FNDIHairStrandsData* RenderThreadData = static_cast<FNDIHairStrandsData*>(DataForRenderThread);

	RenderThreadData->WorldTransform = GameThreadData->WorldTransform;
	RenderThreadData->HairStrandsBuffer = GameThreadData->HairStrandsBuffer;
	RenderThreadData->NumStrands = GameThreadData->NumStrands;
	RenderThreadData->StrandSize = GameThreadData->StrandSize;
	RenderThreadData->BoxCenter = GameThreadData->BoxCenter;
	RenderThreadData->BoxExtent = GameThreadData->BoxExtent;
	RenderThreadData->TickCount = GameThreadData->TickCount;
	RenderThreadData->ResetTick = GameThreadData->ResetTick;
	RenderThreadData->ForceReset = GameThreadData->ForceReset;
}

FNiagaraDataInterfaceParametersCS*
UNiagaraDataInterfaceHairStrands::ConstructComputeParameters() const
{
	return new FNDIHairStrandsParametersCS();
}

void FNDIHairStrandsProxy::PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context)
{
	if (Context.ShaderStageIndex == 0)
	{
		FNDIHairStrandsData* ProxyData =
			SystemInstancesToProxyData.Find(Context.SystemInstance);

		if (ProxyData != nullptr && ProxyData->HairStrandsBuffer != nullptr)
		{
			ProxyData->HairStrandsBuffer->ClearBuffer(RHICmdList);
		}
	}
}

void FNDIHairStrandsProxy::PostStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context)
{}

void FNDIHairStrandsProxy::ResetData(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context)
{}


#undef LOCTEXT_NAMESPACE