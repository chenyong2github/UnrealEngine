// Copyright Epic Games, Inc. All Rights Reserved.

#include "Niagara/NiagaraDataInterfaceHairStrands.h"
#include "NiagaraShader.h"
#include "NiagaraComponent.h"
#include "NiagaraRenderer.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraEmitterInstanceBatcher.h"

#include "Components/SkeletalMeshComponent.h"
#include "ShaderParameterUtils.h"
#include "ClearQuad.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ShaderParameterStruct.h"
#include "GlobalShader.h"

#include "GroomComponent.h"
#include "GroomAsset.h"  

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceHairStrands"

//------------------------------------------------------------------------------------------------------------

static const FName GetPointPositionName(TEXT("GetPointPosition"));

static const FName GetStrandSizeName(TEXT("GetStrandSize"));
static const FName GetNumStrandsName(TEXT("GetNumStrands"));

static const FName GetWorldTransformName(TEXT("GetWorldTransform"));
static const FName GetWorldInverseName(TEXT("GetWorldInverse"));

static const FName GetSubStepsName("GetSubSteps");
static const FName GetIterationCountName("GetIterationCount");

static const FName GetGravityVectorName("GetGravityVector");
static const FName GetAirDragName("GetAirDrag");
static const FName GetAirVelocityName("GetAirVelocity");

static const FName GetSolveBendName("GetSolveBend");
static const FName GetProjectBendName("GetProjectBend");
static const FName GetBendDampingName("GetBendDamping");
static const FName GetBendStiffnessName("GetBendStiffness");
static const FName GetBendScaleName("GetBendScale");

static const FName GetSolveStretchName("GetSolveStretch");
static const FName GetProjectStretchName("GetProjectStretch");
static const FName GetStretchDampingName("GetStretchDamping");
static const FName GetStretchStiffnessName("GetStretchStiffness");
static const FName GetStretchScaleName("GetStretchScale");

static const FName GetSolveCollisionName("GetSolveCollision");
static const FName GetProjectCollisionName("GetProjectCollision");
static const FName GetStaticFrictionName("GetStaticFriction");
static const FName GetKineticFrictionName("GetKineticFriction");
static const FName GetStrandsViscosityName("GetStrandsViscosity");
static const FName GetGridDimensionName("GetGridDimension");
static const FName GetCollisionRadiusName("GetCollisionRadius");
static const FName GetRadiusScaleName("GetRadiusScale");

static const FName GetStrandsDensityName("GetStrandsDensity");
static const FName GetStrandsSmoothingName("GetStrandsSmoothing");
static const FName GetStrandsThicknessName("GetStrandsThickness");
static const FName GetThicknessScaleName("GetThicknessScale");

//------------------------------------------------------------------------------------------------------------

static const FName ComputeNodePositionName(TEXT("ComputeNodePosition"));
static const FName ComputeNodeOrientationName(TEXT("ComputeNodeOrientation"));
static const FName ComputeNodeMassName(TEXT("ComputeNodeMass"));
static const FName ComputeNodeInertiaName(TEXT("ComputeNodeInertia"));

//------------------------------------------------------------------------------------------------------------

static const FName ComputeEdgeLengthName(TEXT("ComputeEdgeLength"));
static const FName ComputeEdgeRotationName(TEXT("ComputeEdgeRotation"));
static const FName ComputeEdgeDirectionName(TEXT("ComputeEdgeDirection"));

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
static const FName UpdateNodeStateName(TEXT("UpdateNodeState"));

//------------------------------------------------------------------------------------------------------------

static const FName UpdatePointPositionName(TEXT("UpdatePointPosition"));
static const FName ResetPointPositionName(TEXT("ResetPointPosition"));

//------------------------------------------------------------------------------------------------------------

static const FName GetBoundingBoxName(TEXT("GetBoundingBox"));
static const FName ResetBoundingBoxName(TEXT("ResetBoundingBox"));
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

static const FName SolveHardCollisionConstraintName(TEXT("SolveHardCollisionConstraint"));
static const FName ProjectHardCollisionConstraintName(TEXT("ProjectHardCollisionConstraint"));

static const FName SetupSoftCollisionConstraintName(TEXT("SetupSoftCollisionConstraint"));
static const FName SolveSoftCollisionConstraintName(TEXT("SolveSoftCollisionConstraint"));
static const FName ProjectSoftCollisionConstraintName(TEXT("ProjectSoftCollisionConstraint"));

//------------------------------------------------------------------------------------------------------------

static const FName UpdateMaterialFrameName(TEXT("UpdateMaterialFrame"));
static const FName ComputeMaterialFrameName(TEXT("ComputeMaterialFrame"));

//------------------------------------------------------------------------------------------------------------

static const FName ComputeAirDragForceName(TEXT("ComputeAirDragForce"));  

//------------------------------------------------------------------------------------------------------------

static const FName NeedSimulationResetName(TEXT("NeedSimulationReset"));
static const FName HasGlobalInterpolationName(TEXT("HasGlobalInterpolation"));
static const FName NeedRestUpdateName(TEXT("NeedRestUpdate"));

//------------------------------------------------------------------------------------------------------------

static const FName InitGridSamplesName(TEXT("InitGridSamples"));
static const FName GetSampleStateName(TEXT("GetSampleState"));

//------------------------------------------------------------------------------------------------------------

const FString UNiagaraDataInterfaceHairStrands::NumStrandsName(TEXT("NumStrands_"));
const FString UNiagaraDataInterfaceHairStrands::StrandSizeName(TEXT("StrandSize_"));
const FString UNiagaraDataInterfaceHairStrands::WorldTransformName(TEXT("WorldTransform_"));
const FString UNiagaraDataInterfaceHairStrands::WorldInverseName(TEXT("WorldInverse_"));
const FString UNiagaraDataInterfaceHairStrands::WorldRotationName(TEXT("WorldRotation_"));

const FString UNiagaraDataInterfaceHairStrands::DeformedPositionBufferName(TEXT("DeformedPositionBuffer_"));
const FString UNiagaraDataInterfaceHairStrands::CurvesOffsetsBufferName(TEXT("CurvesOffsetsBuffer_"));
const FString UNiagaraDataInterfaceHairStrands::RestPositionBufferName(TEXT("RestPositionBuffer_"));

const FString UNiagaraDataInterfaceHairStrands::ResetSimulationName(TEXT("ResetSimulation_"));
const FString UNiagaraDataInterfaceHairStrands::InterpolationModeName(TEXT("InterpolationMode_"));
const FString UNiagaraDataInterfaceHairStrands::RestUpdateName(TEXT("RestUpdate_"));
const FString UNiagaraDataInterfaceHairStrands::RootBarycentricCoordinatesName(TEXT("RootBarycentricCoordinatesBuffer_"));

const FString UNiagaraDataInterfaceHairStrands::RestRootOffsetName(TEXT("RestRootOffset_"));
const FString UNiagaraDataInterfaceHairStrands::RestTrianglePositionAName(TEXT("RestTrianglePositionABuffer_"));
const FString UNiagaraDataInterfaceHairStrands::RestTrianglePositionBName(TEXT("RestTrianglePositionBBuffer_"));
const FString UNiagaraDataInterfaceHairStrands::RestTrianglePositionCName(TEXT("RestTrianglePositionCBuffer_"));

const FString UNiagaraDataInterfaceHairStrands::DeformedRootOffsetName(TEXT("DeformedRootOffset_"));
const FString UNiagaraDataInterfaceHairStrands::DeformedTrianglePositionAName(TEXT("DeformedTrianglePositionABuffer_"));
const FString UNiagaraDataInterfaceHairStrands::DeformedTrianglePositionBName(TEXT("DeformedTrianglePositionBBuffer_"));
const FString UNiagaraDataInterfaceHairStrands::DeformedTrianglePositionCName(TEXT("DeformedTrianglePositionCBuffer_"));

const FString UNiagaraDataInterfaceHairStrands::SampleCountName(TEXT("SampleCount_"));
const FString UNiagaraDataInterfaceHairStrands::RestSamplePositionsName(TEXT("RestSamplePositionsBuffer_"));
const FString UNiagaraDataInterfaceHairStrands::MeshSampleWeightsName(TEXT("MeshSampleWeightsBuffer_"));

const FString UNiagaraDataInterfaceHairStrands::RestPositionOffsetName(TEXT("RestPositionOffset_"));
const FString UNiagaraDataInterfaceHairStrands::DeformedPositionOffsetName(TEXT("DeformedPositionOffset_"));

const FString UNiagaraDataInterfaceHairStrands::BoundingBoxOffsetsName(TEXT("BoundingBoxOffsets_"));
const FString UNiagaraDataInterfaceHairStrands::BoundingBoxBufferName(TEXT("BoundingBoxBuffer_"));
const FString UNiagaraDataInterfaceHairStrands::ParamsScaleBufferName(TEXT("ParamsScaleBuffer_"));

//------------------------------------------------------------------------------------------------------------

static int32 GHairSimulationMaxDelay = 4;
static FAutoConsoleVariableRef CVarHairSimulationMaxDelay(TEXT("r.HairStrands.SimulationMaxDelay"), GHairSimulationMaxDelay, TEXT("Maximum tick Delay before starting the simulation"));

static int32 GHairSimulationRestUpdate = false;
static FAutoConsoleVariableRef CVarHairSimulationRestUpdate(TEXT("r.HairStrands.SimulationRestUpdate"), GHairSimulationRestUpdate, TEXT("Update the simulation rest pose"));

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

		DeformedPositionBufferName = UNiagaraDataInterfaceHairStrands::DeformedPositionBufferName + Suffix;
		CurvesOffsetsBufferName = UNiagaraDataInterfaceHairStrands::CurvesOffsetsBufferName + Suffix;
		RestPositionBufferName = UNiagaraDataInterfaceHairStrands::RestPositionBufferName + Suffix;

		InterpolationModeName = UNiagaraDataInterfaceHairStrands::InterpolationModeName + Suffix;
		ResetSimulationName = UNiagaraDataInterfaceHairStrands::ResetSimulationName + Suffix;
		RestUpdateName = UNiagaraDataInterfaceHairStrands::RestUpdateName + Suffix;
		RootBarycentricCoordinatesName = UNiagaraDataInterfaceHairStrands::RootBarycentricCoordinatesName + Suffix;

		RestRootOffsetName = UNiagaraDataInterfaceHairStrands::RestRootOffsetName + Suffix;
		RestTrianglePositionAName = UNiagaraDataInterfaceHairStrands::RestTrianglePositionAName + Suffix;
		RestTrianglePositionBName = UNiagaraDataInterfaceHairStrands::RestTrianglePositionBName + Suffix;
		RestTrianglePositionCName = UNiagaraDataInterfaceHairStrands::RestTrianglePositionCName + Suffix;

		DeformedRootOffsetName = UNiagaraDataInterfaceHairStrands::DeformedRootOffsetName + Suffix;
		DeformedTrianglePositionAName = UNiagaraDataInterfaceHairStrands::DeformedTrianglePositionAName + Suffix;
		DeformedTrianglePositionBName = UNiagaraDataInterfaceHairStrands::DeformedTrianglePositionBName + Suffix;
		DeformedTrianglePositionCName = UNiagaraDataInterfaceHairStrands::DeformedTrianglePositionCName + Suffix;

		SampleCountName = UNiagaraDataInterfaceHairStrands::SampleCountName + Suffix;
		RestSamplePositionsName = UNiagaraDataInterfaceHairStrands::RestSamplePositionsName + Suffix;
		MeshSampleWeightsName = UNiagaraDataInterfaceHairStrands::MeshSampleWeightsName + Suffix;

		RestPositionOffsetName = UNiagaraDataInterfaceHairStrands::RestPositionOffsetName + Suffix;
		DeformedPositionOffsetName = UNiagaraDataInterfaceHairStrands::DeformedPositionOffsetName + Suffix;

		BoundingBoxOffsetsName = UNiagaraDataInterfaceHairStrands::BoundingBoxOffsetsName + Suffix;
		BoundingBoxBufferName = UNiagaraDataInterfaceHairStrands::BoundingBoxBufferName + Suffix;
		ParamsScaleBufferName = UNiagaraDataInterfaceHairStrands::ParamsScaleBufferName + Suffix;
	}

	FString NumStrandsName;
	FString StrandSizeName;
	FString WorldTransformName;
	FString WorldInverseName;
	FString WorldRotationName;

	FString DeformedPositionBufferName;
	FString CurvesOffsetsBufferName;
	FString RestPositionBufferName;

	FString ResetSimulationName;
	FString InterpolationModeName;
	FString RestUpdateName;
	FString RootBarycentricCoordinatesName;

	FString RestRootOffsetName;
	FString RestTrianglePositionAName;
	FString RestTrianglePositionBName;
	FString RestTrianglePositionCName;

	FString DeformedRootOffsetName;
	FString DeformedTrianglePositionAName;
	FString DeformedTrianglePositionBName;
	FString DeformedTrianglePositionCName;

	FString SampleCountName;
	FString RestSamplePositionsName;
	FString MeshSampleWeightsName;

	FString RestPositionOffsetName;
	FString DeformedPositionOffsetName;

	FString BoundingBoxBufferName;
	FString BoundingBoxOffsetsName;
	FString ParamsScaleBufferName;
};

//------------------------------------------------------------------------------------------------------------

void FNDIHairStrandsBuffer::Initialize(
	const FHairStrandsDatas*  HairStrandsDatas, 
	const FHairStrandsRestResource* HairStrandsRestResource, 
	const FHairStrandsDeformedResource*  HairStrandsDeformedResource, 
	const FHairStrandsRestRootResource* HairStrandsRestRootResource,
	const FHairStrandsDeformedRootResource* HairStrandsDeformedRootResource,
	const TStaticArray<float, 32 * NumScales>& InParamsScale)
{
	SourceDatas = HairStrandsDatas;
	SourceRestResources = HairStrandsRestResource;
	SourceDeformedResources = HairStrandsDeformedResource;
	SourceRestRootResources = HairStrandsRestRootResource;
	SourceDeformedRootResources = HairStrandsDeformedRootResource;
	ParamsScale = InParamsScale;
	BoundingBoxOffsets = FIntVector4(0,1,2,3);
}

void FNDIHairStrandsBuffer::Update(
	const FHairStrandsDatas* HairStrandsDatas,
	const FHairStrandsRestResource* HairStrandsRestResource,
	const FHairStrandsDeformedResource* HairStrandsDeformedResource,
	const FHairStrandsRestRootResource* HairStrandsRestRootResource,
	const FHairStrandsDeformedRootResource* HairStrandsDeformedRootResource)
{
	SourceDatas = HairStrandsDatas;
	SourceRestResources = HairStrandsRestResource;
	SourceDeformedResources = HairStrandsDeformedResource;
	SourceRestRootResources = HairStrandsRestRootResource;
	SourceDeformedRootResources = HairStrandsDeformedRootResource;
}

void FNDIHairStrandsBuffer::Transfer(const TStaticArray<float, 32 * NumScales>& InParamsScale)
{
	if (SourceDatas != nullptr && SourceRestResources != nullptr)
	{
		const uint32 ScaleCount = 32 * NumScales;
		const uint32 ScaleBytes = sizeof(float) * ScaleCount;

		void* ScaleBufferData = RHILockVertexBuffer(ParamsScaleBuffer.Buffer, 0, ScaleBytes, RLM_WriteOnly);
		FMemory::Memcpy(ScaleBufferData, InParamsScale.GetData(), ScaleBytes);
		RHIUnlockVertexBuffer(ParamsScaleBuffer.Buffer);
	}
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
			static const TArray<uint32> ZeroData = { UINT_MAX,UINT_MAX,UINT_MAX,0,0,0,
													 UINT_MAX,UINT_MAX,UINT_MAX,0,0,0,
													 UINT_MAX,UINT_MAX,UINT_MAX,0,0,0,
													 UINT_MAX,UINT_MAX,UINT_MAX,0,0,0 };

			const uint32 BoundCount = 24;
			const uint32 BoundBytes = sizeof(uint32)*BoundCount;

			BoundingBoxBuffer.Initialize(sizeof(uint32), BoundCount, EPixelFormat::PF_R32_UINT, BUF_Static);
			void* BoundBufferData = RHILockVertexBuffer(BoundingBoxBuffer.Buffer, 0, BoundBytes, RLM_WriteOnly);
			
			FMemory::Memcpy(BoundBufferData, ZeroData.GetData(), BoundBytes);
			RHIUnlockVertexBuffer(BoundingBoxBuffer.Buffer);
		}
		{
			const uint32 ScaleCount = 32 * NumScales;
			const uint32 ScaleBytes = sizeof(float) * ScaleCount;  

			ParamsScaleBuffer.Initialize(sizeof(float), ScaleCount, EPixelFormat::PF_R32_FLOAT, BUF_Static);
			void* ScaleBufferData = RHILockVertexBuffer(ParamsScaleBuffer.Buffer, 0, ScaleBytes, RLM_WriteOnly);

			FMemory::Memcpy(ScaleBufferData, ParamsScale.GetData(), ScaleBytes);
			RHIUnlockVertexBuffer(ParamsScaleBuffer.Buffer);
		}
		if (SourceDeformedResources == nullptr)
		{
			const uint32 PositionsCount = SourceDatas->GetNumPoints();
			DeformedPositionBuffer.Initialize(FHairStrandsPositionFormat::SizeInByte, PositionsCount, FHairStrandsPositionFormat::Format, BUF_Static);
		}
	}
}

void FNDIHairStrandsBuffer::ReleaseRHI()
{
	CurvesOffsetsBuffer.Release();
	DeformedPositionBuffer.Release();
	BoundingBoxBuffer.Release();
	ParamsScaleBuffer.Release();
}

//------------------------------------------------------------------------------------------------------------

ETickingGroup ComputeTickingGroup(const TWeakObjectPtr<UGroomComponent> GroomComponent)
{
	ETickingGroup TickingGroup = NiagaraFirstTickGroup;
	
	if (GroomComponent.Get() != nullptr)
	{
		const ETickingGroup ComponentTickGroup = FMath::Max(GroomComponent->PrimaryComponentTick.TickGroup, GroomComponent->PrimaryComponentTick.EndTickGroup);
		const ETickingGroup ClampedTickGroup = FMath::Clamp(ETickingGroup(ComponentTickGroup + 1), NiagaraFirstTickGroup, NiagaraLastTickGroup);

		TickingGroup = FMath::Max(TickingGroup, ClampedTickGroup);
	}
	return TickingGroup;
}


void FNDIHairStrandsData::Release()
{
	if (HairStrandsBuffer)
	{
		BeginReleaseResource(HairStrandsBuffer);
		ENQUEUE_RENDER_COMMAND(DeleteResource)(
			[ParamPointerToRelease = HairStrandsBuffer](FRHICommandListImmediate& RHICmdList)
		{
			delete ParamPointerToRelease;
		});
		HairStrandsBuffer = nullptr;
	}
}

void FNDIHairStrandsData::Update(UNiagaraDataInterfaceHairStrands* Interface, FNiagaraSystemInstance* SystemInstance, const FHairStrandsDatas* StrandsDatas, 
	UGroomAsset* GroomAsset, const int32 GroupIndex, const FTransform& LocalToWorld)
{
	if (Interface != nullptr)
	{
		WorldTransform = LocalToWorld;

		GlobalInterpolation = (Interface->IsComponentValid() && Interface->SourceComponent->BindingAsset && Interface->SourceComponent->GroomAsset) ?
			Interface->SourceComponent->GroomAsset->EnableGlobalInterpolation : false;

		TickingGroup = Interface->IsComponentValid() ? ComputeTickingGroup(Interface->SourceComponent) : NiagaraFirstTickGroup;

		if (StrandsDatas != nullptr && GroomAsset != nullptr && GroupIndex >= 0 && GroupIndex < GroomAsset->HairGroupsPhysics.Num() &&
			GroomAsset->HairGroupsPhysics[GroupIndex].SolverSettings.EnableSimulation)
		{
			FHairGroupsPhysics& HairPhysics = GroomAsset->HairGroupsPhysics[GroupIndex];
			StrandsSize = static_cast<uint8>(HairPhysics.StrandsParameters.StrandsSize);

			SubSteps = HairPhysics.SolverSettings.SubSteps;
			IterationCount = HairPhysics.SolverSettings.IterationCount;

			GravityVector = HairPhysics.ExternalForces.GravityVector;
			AirDrag = HairPhysics.ExternalForces.AirDrag;
			AirVelocity = HairPhysics.ExternalForces.AirVelocity;

			SolveBend = HairPhysics.MaterialConstraints.BendConstraint.SolveBend;
			ProjectBend = HairPhysics.MaterialConstraints.BendConstraint.ProjectBend;
			BendDamping = HairPhysics.MaterialConstraints.BendConstraint.BendDamping;
			BendStiffness = HairPhysics.MaterialConstraints.BendConstraint.BendStiffness;

			SolveStretch = HairPhysics.MaterialConstraints.StretchConstraint.SolveStretch;
			ProjectStretch = HairPhysics.MaterialConstraints.StretchConstraint.ProjectStretch;
			StretchDamping = HairPhysics.MaterialConstraints.StretchConstraint.StretchDamping;
			StretchStiffness = HairPhysics.MaterialConstraints.StretchConstraint.StretchStiffness;

			SolveCollision = HairPhysics.MaterialConstraints.CollisionConstraint.SolveCollision;
			ProjectCollision = HairPhysics.MaterialConstraints.CollisionConstraint.ProjectCollision;
			StaticFriction = HairPhysics.MaterialConstraints.CollisionConstraint.StaticFriction;
			KineticFriction = HairPhysics.MaterialConstraints.CollisionConstraint.KineticFriction;
			StrandsViscosity = HairPhysics.MaterialConstraints.CollisionConstraint.StrandsViscosity;
			GridDimension = HairPhysics.MaterialConstraints.CollisionConstraint.GridDimension;
			CollisionRadius = HairPhysics.MaterialConstraints.CollisionConstraint.CollisionRadius;

			StrandsDensity = HairPhysics.StrandsParameters.StrandsDensity;
			StrandsSmoothing = HairPhysics.StrandsParameters.StrandsSmoothing;
			StrandsThickness = HairPhysics.StrandsParameters.StrandsThickness;

			for (int32 i = 0; i < StrandsSize; ++i)
			{
				const float VertexCoord = static_cast<float>(i) / (StrandsSize-1.0);
				ParamsScale[32 * BendOffset + i] = HairPhysics.MaterialConstraints.BendConstraint.BendScale.GetRichCurve()->Eval(VertexCoord);
				ParamsScale[32 * StretchOffset + i] = HairPhysics.MaterialConstraints.StretchConstraint.StretchScale.GetRichCurve()->Eval(VertexCoord);
				ParamsScale[32 * RadiusOffset + i] = HairPhysics.MaterialConstraints.CollisionConstraint.RadiusScale.GetRichCurve()->Eval(VertexCoord);
				ParamsScale[32 * ThicknessOffset + i] = HairPhysics.StrandsParameters.ThicknessScale.GetRichCurve()->Eval(VertexCoord);
			}

			const FBox& StrandsBox = StrandsDatas->BoundingBox;

			NumStrands = StrandsDatas->GetNumCurves();
		}
		else
		{
			ResetDatas();
		}
	}
}

bool FNDIHairStrandsData::Init(UNiagaraDataInterfaceHairStrands* Interface, FNiagaraSystemInstance* SystemInstance)
{
	HairStrandsBuffer = nullptr;

	if (Interface != nullptr)
	{
		FHairStrandsDatas* StrandsDatas = nullptr;
		FHairStrandsRestResource* StrandsRestResource = nullptr;
		FHairStrandsDeformedResource* StrandsDeformedResource = nullptr;
		FHairStrandsRestRootResource* StrandsRestRootResource = nullptr;
		FHairStrandsDeformedRootResource* StrandsDeformedRootResource = nullptr;
		UGroomAsset* GroomAsset = nullptr;
		int32 GroupIndex = 0;

		{
			FTransform LocalToWorld = FTransform::Identity;
			Interface->ExtractDatasAndResources(SystemInstance, StrandsDatas, StrandsRestResource, StrandsDeformedResource, StrandsRestRootResource, StrandsDeformedRootResource, GroomAsset, GroupIndex, LocalToWorld);
			Update(Interface, SystemInstance, StrandsDatas, GroomAsset, GroupIndex, LocalToWorld);

			HairStrandsBuffer = new FNDIHairStrandsBuffer();
			HairStrandsBuffer->Initialize(StrandsDatas, StrandsRestResource, StrandsDeformedResource, StrandsRestRootResource, StrandsDeformedRootResource, ParamsScale);

			BeginInitResource(HairStrandsBuffer);
		
			TickCount = 0;
			ForceReset = true;
		}
	}

	return true;
}

//------------------------------------------------------------------------------------------------------------

struct FNDIHairStrandsParametersCS : public FNiagaraDataInterfaceParametersCS
{
	DECLARE_TYPE_LAYOUT(FNDIHairStrandsParametersCS, NonVirtual);
	void Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
	{
		FNDIHairStrandsParametersName ParamNames(*ParameterInfo.DataInterfaceHLSLSymbol);

		WorldTransform.Bind(ParameterMap, *ParamNames.WorldTransformName);
		WorldInverse.Bind(ParameterMap, *ParamNames.WorldInverseName);
		WorldRotation.Bind(ParameterMap, *ParamNames.WorldRotationName);
		NumStrands.Bind(ParameterMap, *ParamNames.NumStrandsName);
		StrandSize.Bind(ParameterMap, *ParamNames.StrandSizeName); 

		DeformedPositionBuffer.Bind(ParameterMap, *ParamNames.DeformedPositionBufferName);
		CurvesOffsetsBuffer.Bind(ParameterMap, *ParamNames.CurvesOffsetsBufferName);
		RestPositionBuffer.Bind(ParameterMap, *ParamNames.RestPositionBufferName);

		ResetSimulation.Bind(ParameterMap, *ParamNames.ResetSimulationName);
		InterpolationMode.Bind(ParameterMap,*ParamNames.InterpolationModeName);
		RestUpdate.Bind(ParameterMap, *ParamNames.RestUpdateName);
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

		SampleCount.Bind(ParameterMap, *ParamNames.SampleCountName);
		RestSamplePositionsBuffer.Bind(ParameterMap, *ParamNames.RestSamplePositionsName);
		MeshSampleWeightsBuffer.Bind(ParameterMap, *ParamNames.MeshSampleWeightsName);

		BoundingBoxOffsets.Bind(ParameterMap, *ParamNames.BoundingBoxOffsetsName);
		BoundingBoxBuffer.Bind(ParameterMap, *ParamNames.BoundingBoxBufferName);
		ParamsScaleBuffer.Bind(ParameterMap, *ParamNames.ParamsScaleBufferName);

		
	}

	void Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		check(IsInRenderingThread()); 

		FRHIComputeShader* ComputeShaderRHI = RHICmdList.GetBoundComputeShader();

		FNDIHairStrandsProxy* InterfaceProxy =
			static_cast<FNDIHairStrandsProxy*>(Context.DataInterface);
		FNDIHairStrandsData* ProxyData =
			InterfaceProxy->SystemInstancesToProxyData.Find(Context.SystemInstanceID);

		const bool IsHairValid = ProxyData != nullptr && ProxyData->HairStrandsBuffer != nullptr && ProxyData->HairStrandsBuffer->IsInitialized();
		const bool IsRootValid = IsHairValid && ProxyData->HairStrandsBuffer->SourceDeformedRootResources != nullptr;//&& ProxyData->HairStrandsBuffer->SourceRootResources->IsInitialized();
		const bool IsRestValid = IsHairValid && ProxyData->HairStrandsBuffer->SourceRestResources != nullptr;//&& ProxyData->HairStrandsBuffer->SourceRestResources->IsInitialized();
		const bool IsDeformedValid = IsHairValid && ProxyData->HairStrandsBuffer->SourceDeformedResources != nullptr && ProxyData->HairStrandsBuffer->SourceDeformedResources->IsInitialized();


		if (IsHairValid && IsRestValid)
		{
			FNDIHairStrandsBuffer* HairStrandsBuffer = ProxyData->HairStrandsBuffer;

			FUnorderedAccessViewRHIRef PointPositionsUAV = IsDeformedValid ?
				HairStrandsBuffer->SourceDeformedResources->DeformedPositionBuffer[HairStrandsBuffer->SourceDeformedResources->CurrentIndex].UAV : HairStrandsBuffer->DeformedPositionBuffer.UAV;

			FRHIShaderResourceView* DeformedPositionOffsetSRV = IsDeformedValid ? 
				HairStrandsBuffer->SourceDeformedResources->DeformedOffsetBuffer[HairStrandsBuffer->SourceDeformedResources->CurrentIndex].SRV.GetReference() : FNiagaraRenderer::GetDummyFloatBuffer();

			const int32 MeshLODIndex = IsRootValid ? HairStrandsBuffer->SourceDeformedRootResources->MeshLODIndex : -1;
			int32 InterpolationModeValue = IsRootValid && HairStrandsBuffer->SourceDeformedRootResources->IsValid(MeshLODIndex) ? 1 : 0;
			const int32 RestUpdateValue = GHairSimulationRestUpdate;

			const FHairStrandsRestRootResource::FLOD* RestMeshProjection = (InterpolationModeValue == 1) ? &(HairStrandsBuffer->SourceRestRootResources->LODs[MeshLODIndex]) : nullptr;
			const FHairStrandsDeformedRootResource::FLOD* DeformedMeshProjection = (InterpolationModeValue == 1) ? &(HairStrandsBuffer->SourceDeformedRootResources->LODs[MeshLODIndex]) : nullptr;

			FRHIShaderResourceView* RestTrianglePositionASRV = (InterpolationModeValue == 1 && RestMeshProjection != nullptr) ?
				RestMeshProjection->RestRootTrianglePosition0Buffer.SRV.GetReference() : FNiagaraRenderer::GetDummyFloatBuffer();
			FRHIShaderResourceView* RestTrianglePositionBSRV = (InterpolationModeValue == 1 && RestMeshProjection != nullptr) ?
				RestMeshProjection->RestRootTrianglePosition1Buffer.SRV.GetReference() : FNiagaraRenderer::GetDummyFloatBuffer();
			FRHIShaderResourceView* RestTrianglePositionCSRV = (InterpolationModeValue == 1 && RestMeshProjection != nullptr) ?
				RestMeshProjection->RestRootTrianglePosition2Buffer.SRV.GetReference() : FNiagaraRenderer::GetDummyFloatBuffer();

			FRHIShaderResourceView* DeformedTrianglePositionASRV = (InterpolationModeValue == 1 && DeformedMeshProjection != nullptr) ?
				DeformedMeshProjection->DeformedRootTrianglePosition0Buffer.SRV.GetReference() : FNiagaraRenderer::GetDummyFloatBuffer();
			FRHIShaderResourceView* DeformedTrianglePositionBSRV = (InterpolationModeValue == 1 && DeformedMeshProjection != nullptr) ?
				DeformedMeshProjection->DeformedRootTrianglePosition1Buffer.SRV.GetReference() : FNiagaraRenderer::GetDummyFloatBuffer();
			FRHIShaderResourceView* DeformedTrianglePositionCSRV = (InterpolationModeValue == 1 && DeformedMeshProjection != nullptr) ?
				DeformedMeshProjection->DeformedRootTrianglePosition2Buffer.SRV.GetReference() : FNiagaraRenderer::GetDummyFloatBuffer();

			const bool bHasSamples = (RestMeshProjection != nullptr && RestMeshProjection->SampleCount > 0);
			const int32 SampleCountValue = bHasSamples ? RestMeshProjection->SampleCount : 0;
			FShaderResourceViewRHIRef RestSamplePositionsBufferSRV = bHasSamples ?
				RestMeshProjection->RestSamplePositionsBuffer.SRV.GetReference() : FNiagaraRenderer::GetDummyFloatBuffer();
			FShaderResourceViewRHIRef MeshSampleWeightsBufferSRV = bHasSamples && DeformedMeshProjection != nullptr ?
				DeformedMeshProjection->MeshSampleWeightsBuffer.SRV.GetReference() : FNiagaraRenderer::GetDummyFloatBuffer();

			FRHIShaderResourceView* RootBarycentricCoordinatesSRV = (InterpolationModeValue == 1 && RestMeshProjection != nullptr) ?
				RestMeshProjection->RootTriangleBarycentricBuffer.SRV.GetReference() : FNiagaraRenderer::GetDummyFloatBuffer();

			int32 bNeedSimReset = (ProxyData->TickCount <= GHairSimulationMaxDelay ? 1 : 0);

			if ((InterpolationModeValue == 1) && ProxyData->GlobalInterpolation)
			{
				InterpolationModeValue = 2;
			}

			//FUnorderedAccessViewRHIRef DummyFloatUAV(Context.Batcher->GetEmptyUAVFromPool(RHICmdList, PF_R32_FLOAT, ENiagaraEmptyUAVType::Buffer), false);
			//FUnorderedAccessViewRHIRef MeshSampleWeightsBufferUAV = (DeformedMeshProjection != nullptr && RestMeshProjection->SampleCount > 0) ?
			//	DeformedMeshProjection->MeshSampleWeightsBuffer.UAV : Context.Batcher->GetEmptyUAVFromPool(RHICmdList, PF_R32_FLOAT, ENiagaraEmptyUAVType::Buffer);

			//UE_LOG(LogHairStrands, Log, TEXT("Shader Reset : %d %d %d"), bNeedSimReset, IsRootValid, InterpolationModeValue);
			FVector RestRootOffsetValue = FVector::ZeroVector;
			FVector DeformedRootOffsetValue = FVector::ZeroVector;
			
			FVector RestPositionOffsetValue = ProxyData->HairStrandsBuffer->SourceRestResources->PositionOffset;

			FRHITransitionInfo Transitions[] = {
				FRHITransitionInfo(PointPositionsUAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
				FRHITransitionInfo(HairStrandsBuffer->BoundingBoxBuffer.UAV, ERHIAccess::Unknown, ERHIAccess::UAVCompute),
			};
			RHICmdList.Transition(MakeArrayView(Transitions, UE_ARRAY_COUNT(Transitions)));

			SetUAVParameter(RHICmdList, ComputeShaderRHI, DeformedPositionBuffer, PointPositionsUAV);
	
			SetUAVParameter(RHICmdList, ComputeShaderRHI, BoundingBoxBuffer, HairStrandsBuffer->BoundingBoxBuffer.UAV);

			SetSRVParameter(RHICmdList, ComputeShaderRHI, CurvesOffsetsBuffer, HairStrandsBuffer->CurvesOffsetsBuffer.SRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, RestPositionBuffer, HairStrandsBuffer->SourceRestResources->RestPositionBuffer.SRV);

			SetShaderValue(RHICmdList, ComputeShaderRHI, BoundingBoxOffsets, HairStrandsBuffer->BoundingBoxOffsets);
			SetShaderValue(RHICmdList, ComputeShaderRHI, WorldTransform, ProxyData->WorldTransform.ToMatrixWithScale());
			SetShaderValue(RHICmdList, ComputeShaderRHI, WorldInverse, ProxyData->WorldTransform.ToMatrixWithScale().Inverse());
			SetShaderValue(RHICmdList, ComputeShaderRHI, WorldRotation, ProxyData->WorldTransform.ToMatrixNoScale().ToQuat());
			SetShaderValue(RHICmdList, ComputeShaderRHI, NumStrands, ProxyData->NumStrands);
			SetShaderValue(RHICmdList, ComputeShaderRHI, StrandSize, ProxyData->StrandsSize);

			SetShaderValue(RHICmdList, ComputeShaderRHI, ResetSimulation, bNeedSimReset);
			SetShaderValue(RHICmdList, ComputeShaderRHI, InterpolationMode, InterpolationModeValue);
			SetShaderValue(RHICmdList, ComputeShaderRHI, RestUpdate, RestUpdateValue);
			SetShaderValue(RHICmdList, ComputeShaderRHI, RestRootOffset, RestRootOffsetValue);
			SetShaderValue(RHICmdList, ComputeShaderRHI, DeformedRootOffset, DeformedRootOffsetValue);

			SetShaderValue(RHICmdList, ComputeShaderRHI, RestPositionOffset, RestPositionOffsetValue);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, DeformedPositionOffset, DeformedPositionOffsetSRV);

			SetSRVParameter(RHICmdList, ComputeShaderRHI, RestTrianglePositionABuffer, RestTrianglePositionASRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, RestTrianglePositionBBuffer, RestTrianglePositionBSRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, RestTrianglePositionCBuffer, RestTrianglePositionCSRV);

			SetSRVParameter(RHICmdList, ComputeShaderRHI, DeformedTrianglePositionABuffer, DeformedTrianglePositionASRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, DeformedTrianglePositionBBuffer, DeformedTrianglePositionBSRV);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, DeformedTrianglePositionCBuffer, DeformedTrianglePositionCSRV);

			SetShaderValue(RHICmdList, ComputeShaderRHI, SampleCount, SampleCountValue);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, RestSamplePositionsBuffer, RestSamplePositionsBufferSRV);

			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshSampleWeightsBuffer, MeshSampleWeightsBufferSRV);

			SetSRVParameter(RHICmdList, ComputeShaderRHI, RootBarycentricCoordinatesBuffer, RootBarycentricCoordinatesSRV);

			SetSRVParameter(RHICmdList, ComputeShaderRHI, ParamsScaleBuffer, HairStrandsBuffer->ParamsScaleBuffer.SRV);
		}
		else
		{
			SetUAVParameter(RHICmdList, ComputeShaderRHI, DeformedPositionBuffer, Context.Batcher->GetEmptyUAVFromPool(RHICmdList, PF_R32_FLOAT, ENiagaraEmptyUAVType::Buffer));
			SetUAVParameter(RHICmdList, ComputeShaderRHI, BoundingBoxBuffer, Context.Batcher->GetEmptyUAVFromPool(RHICmdList, PF_R32_UINT, ENiagaraEmptyUAVType::Buffer));
			SetSRVParameter(RHICmdList, ComputeShaderRHI, CurvesOffsetsBuffer, FNiagaraRenderer::GetDummyUIntBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, RestPositionBuffer, FNiagaraRenderer::GetDummyFloatBuffer());

			SetShaderValue(RHICmdList, ComputeShaderRHI, BoundingBoxOffsets, FIntVector4(0,1,2,3));
			SetShaderValue(RHICmdList, ComputeShaderRHI, WorldTransform, FMatrix::Identity);
			SetShaderValue(RHICmdList, ComputeShaderRHI, WorldInverse, FMatrix::Identity);
			SetShaderValue(RHICmdList, ComputeShaderRHI, WorldRotation, FQuat::Identity);
			SetShaderValue(RHICmdList, ComputeShaderRHI, NumStrands, 1);
			SetShaderValue(RHICmdList, ComputeShaderRHI, StrandSize, 1);

			SetShaderValue(RHICmdList, ComputeShaderRHI, ResetSimulation, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, InterpolationMode, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, RestUpdate, 0);
			SetShaderValue(RHICmdList, ComputeShaderRHI, RestRootOffset, FVector4(0, 0, 0, 0));
			SetShaderValue(RHICmdList, ComputeShaderRHI, DeformedRootOffset, FVector4(0, 0, 0, 0));

			SetShaderValue(RHICmdList, ComputeShaderRHI, RestPositionOffset, FVector(0,0,0));
			SetSRVParameter(RHICmdList, ComputeShaderRHI, DeformedPositionOffset, FNiagaraRenderer::GetDummyFloatBuffer());

			SetSRVParameter(RHICmdList, ComputeShaderRHI, RestTrianglePositionABuffer, FNiagaraRenderer::GetDummyFloatBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, RestTrianglePositionBBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, RestTrianglePositionCBuffer, FNiagaraRenderer::GetDummyFloatBuffer());

			SetSRVParameter(RHICmdList, ComputeShaderRHI, DeformedTrianglePositionABuffer, FNiagaraRenderer::GetDummyFloatBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, DeformedTrianglePositionBBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, DeformedTrianglePositionCBuffer, FNiagaraRenderer::GetDummyFloatBuffer());

			SetShaderValue(RHICmdList, ComputeShaderRHI, SampleCount, 0);
			SetSRVParameter(RHICmdList, ComputeShaderRHI, RestSamplePositionsBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, MeshSampleWeightsBuffer, FNiagaraRenderer::GetDummyFloatBuffer());

			SetSRVParameter(RHICmdList, ComputeShaderRHI, RootBarycentricCoordinatesBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
			SetSRVParameter(RHICmdList, ComputeShaderRHI, ParamsScaleBuffer, FNiagaraRenderer::GetDummyFloatBuffer());
		}
	}

	void Unset(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
	{
		FRHIComputeShader* ShaderRHI = RHICmdList.GetBoundComputeShader();
		SetUAVParameter(RHICmdList, ShaderRHI, DeformedPositionBuffer, nullptr);
		SetUAVParameter(RHICmdList, ShaderRHI, BoundingBoxBuffer, nullptr);
	}

private:

	LAYOUT_FIELD(FShaderParameter, WorldTransform);
	LAYOUT_FIELD(FShaderParameter, WorldInverse);
	LAYOUT_FIELD(FShaderParameter, WorldRotation);
	LAYOUT_FIELD(FShaderParameter, NumStrands);
	LAYOUT_FIELD(FShaderParameter, StrandSize);

	LAYOUT_FIELD(FShaderResourceParameter, DeformedPositionBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, CurvesOffsetsBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RestPositionBuffer);

	LAYOUT_FIELD(FShaderParameter, ResetSimulation);
	LAYOUT_FIELD(FShaderParameter, InterpolationMode);
	LAYOUT_FIELD(FShaderParameter, RestUpdate);
	LAYOUT_FIELD(FShaderParameter, RestRootOffset);
	LAYOUT_FIELD(FShaderParameter, DeformedRootOffset);

	LAYOUT_FIELD(FShaderResourceParameter, RootBarycentricCoordinatesBuffer);

	LAYOUT_FIELD(FShaderResourceParameter, RestTrianglePositionABuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RestTrianglePositionBBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, RestTrianglePositionCBuffer);

	LAYOUT_FIELD(FShaderResourceParameter, DeformedTrianglePositionABuffer);
	LAYOUT_FIELD(FShaderResourceParameter, DeformedTrianglePositionBBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, DeformedTrianglePositionCBuffer);

	LAYOUT_FIELD(FShaderParameter, SampleCount);
	LAYOUT_FIELD(FShaderResourceParameter, RestSamplePositionsBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, MeshSampleWeightsBuffer);

	LAYOUT_FIELD(FShaderParameter, RestPositionOffset);
	LAYOUT_FIELD(FShaderResourceParameter, DeformedPositionOffset);

	LAYOUT_FIELD(FShaderResourceParameter, ParamsScaleBuffer);
	LAYOUT_FIELD(FShaderResourceParameter, BoundingBoxBuffer);
	LAYOUT_FIELD(FShaderParameter, BoundingBoxOffsets);
};

IMPLEMENT_TYPE_LAYOUT(FNDIHairStrandsParametersCS);

IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceHairStrands, FNDIHairStrandsParametersCS);

//------------------------------------------------------------------------------------------------------------

void FNDIHairStrandsProxy::ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& Instance)
{
	FNDIHairStrandsData* SourceData = static_cast<FNDIHairStrandsData*>(PerInstanceData);
	FNDIHairStrandsData* TargetData = &(SystemInstancesToProxyData.FindOrAdd(Instance));

	ensure(TargetData);
	if (TargetData)
	{
		TargetData->CopyDatas(SourceData);
	}
	else
	{
		UE_LOG(LogHairStrands, Log, TEXT("ConsumePerInstanceDataFromGameThread() ... could not find %s"), *FNiagaraUtilities::SystemInstanceIDToString(Instance));
	}
	SourceData->~FNDIHairStrandsData();
}

void FNDIHairStrandsProxy::InitializePerInstanceData(const FNiagaraSystemInstanceID& SystemInstance)
{
	check(IsInRenderingThread());

	FNDIHairStrandsData* TargetData = SystemInstancesToProxyData.Find(SystemInstance);
	TargetData = &SystemInstancesToProxyData.Add(SystemInstance);
}

void FNDIHairStrandsProxy::DestroyPerInstanceData(NiagaraEmitterInstanceBatcher* Batcher, const FNiagaraSystemInstanceID& SystemInstance)
{
	check(IsInRenderingThread());
	//check(SystemInstancesToProxyData.Contains(SystemInstance));
	SystemInstancesToProxyData.Remove(SystemInstance);
}

//------------------------------------------------------------------------------------------------------------

FORCEINLINE bool RequiresSimulationReset(FNiagaraSystemInstance* SystemInstance, uint32& OldSkeletalMeshes)
{
	uint32 NewSkeletalMeshes = 0;
	if (USceneComponent* AttachComponent = SystemInstance->GetAttachComponent())
	{
		if (AActor* RootActor = AttachComponent->GetAttachmentRootActor())
		{
			for (UActorComponent* ActorComp : RootActor->GetComponents())
			{
				USkeletalMeshComponent* SkelMeshComp = Cast<USkeletalMeshComponent>(ActorComp);
				if (SkelMeshComp && SkelMeshComp->SkeletalMesh)
				{
					NewSkeletalMeshes += GetTypeHash(SkelMeshComp->SkeletalMesh->GetName());
				}
			}
		}
	}
	bool bNeedReset = NewSkeletalMeshes != OldSkeletalMeshes;
	OldSkeletalMeshes = NewSkeletalMeshes;
	return bNeedReset;
}

//------------------------------------------------------------------------------------------------------------

UNiagaraDataInterfaceHairStrands::UNiagaraDataInterfaceHairStrands(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
	, DefaultSource(nullptr)
	, SourceActor(nullptr)
	, SourceComponent(nullptr)
{

	Proxy.Reset(new FNDIHairStrandsProxy());
}

bool UNiagaraDataInterfaceHairStrands::IsComponentValid() const
{
	return (SourceComponent.IsValid() && SourceComponent != nullptr);
}

void UNiagaraDataInterfaceHairStrands::ExtractSourceComponent(FNiagaraSystemInstance* SystemInstance)
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
	else if (SystemInstance)
	{
		if (USceneComponent* AttachComponent = SystemInstance->GetAttachComponent())
		{
			// First, look to our attachment hierarchy for the source component
			for (USceneComponent* Curr = AttachComponent; Curr; Curr = Curr->GetAttachParent())
			{
				UGroomComponent* SourceComp = Cast<UGroomComponent>(Curr);
				if (SourceComp && SourceComp->GroomAsset)
				{
					SourceComponent = SourceComp;
					break;
				}
			}

			if (!SourceComponent.IsValid())
			{
				// Next, check out outer chain to look for the component
				if (UGroomComponent* OuterComp = AttachComponent->GetTypedOuter<UGroomComponent>())
				{
					SourceComponent = OuterComp;
				}
				else if (AActor* Owner = AttachComponent->GetAttachmentRootActor())
				{
					// Lastly, look through all our root actor's components for a sibling component
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
}

void UNiagaraDataInterfaceHairStrands::ExtractDatasAndResources(
	FNiagaraSystemInstance* SystemInstance,
	FHairStrandsDatas*& OutStrandsDatas, 
	FHairStrandsRestResource*& OutStrandsRestResource, 
	FHairStrandsDeformedResource*& OutStrandsDeformedResource, 
	FHairStrandsRestRootResource*& OutStrandsRestRootResource, 
	FHairStrandsDeformedRootResource*& OutStrandsDeformedRootResource,
	UGroomAsset*& OutGroomAsset,
	int32& OutGroupIndex, 
	FTransform& OutLocalToWorld)
{
	ExtractSourceComponent(SystemInstance);

	OutStrandsDatas = nullptr;
	OutStrandsRestResource = nullptr;
	OutStrandsDeformedResource = nullptr;
	OutStrandsRestRootResource = nullptr;
	OutStrandsDeformedRootResource = nullptr;
	OutGroupIndex = -1;

	if (IsComponentValid() && SystemInstance)
	{
		for (int32 NiagaraIndex = 0, NiagaraCount = SourceComponent->NiagaraComponents.Num(); NiagaraIndex < NiagaraCount; ++NiagaraIndex)
		{
			if (UNiagaraComponent* NiagaraComponent = SourceComponent->NiagaraComponents[NiagaraIndex])
			{
				if (NiagaraComponent->GetSystemInstance() == SystemInstance)
				{
					OutGroupIndex = NiagaraIndex;
					break;
				}
			}
		}
		if (OutGroupIndex >= 0 && OutGroupIndex < SourceComponent->NiagaraComponents.Num())
		{
			OutStrandsDatas = SourceComponent->GetGuideStrandsDatas(OutGroupIndex);
			OutStrandsRestResource = SourceComponent->GetGuideStrandsRestResource(OutGroupIndex);
			OutStrandsDeformedResource = SourceComponent->GetGuideStrandsDeformedResource(OutGroupIndex);
			OutStrandsRestRootResource = SourceComponent->GetGuideStrandsRestRootResource(OutGroupIndex);
			OutStrandsDeformedRootResource = SourceComponent->GetGuideStrandsDeformedRootResource(OutGroupIndex);
			OutGroomAsset = SourceComponent->GroomAsset;
			OutLocalToWorld = SourceComponent->GetGuideStrandsLocalToWorld(OutGroupIndex);
		}
	}
	else if (DefaultSource != nullptr)
	{
		OutGroupIndex = 0;
		OutLocalToWorld = SystemInstance ? SystemInstance->GetWorldTransform() : FTransform::Identity;
		if (OutGroupIndex < DefaultSource->GetNumHairGroups())
		{
			OutStrandsDatas = &DefaultSource->HairGroupsData[OutGroupIndex].Strands.Data;
			OutStrandsRestResource = DefaultSource->HairGroupsData[OutGroupIndex].Strands.RestResource;
			OutGroomAsset = DefaultSource;
		}
	}
}

ETickingGroup UNiagaraDataInterfaceHairStrands::CalculateTickGroup(const void* PerInstanceData) const
{
	const FNDIHairStrandsData* InstanceData = static_cast<const FNDIHairStrandsData*>(PerInstanceData);

	if (InstanceData)
	{
		return InstanceData->TickingGroup;
	}
	return NiagaraFirstTickGroup;
}

bool UNiagaraDataInterfaceHairStrands::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIHairStrandsData* InstanceData = new (PerInstanceData) FNDIHairStrandsData();
	check(InstanceData);

	return InstanceData->Init(this, SystemInstance);
}

void UNiagaraDataInterfaceHairStrands::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FNDIHairStrandsData* InstanceData = static_cast<FNDIHairStrandsData*>(PerInstanceData);

	InstanceData->Release();
	InstanceData->~FNDIHairStrandsData();

	FNDIHairStrandsProxy* ThisProxy = GetProxyAs<FNDIHairStrandsProxy>();
	ENQUEUE_RENDER_COMMAND(FNiagaraDIDestroyInstanceData) (
		[ThisProxy, InstanceID = SystemInstance->GetId(), Batcher = SystemInstance->GetBatcher()](FRHICommandListImmediate& CmdList)
	{
		ThisProxy->SystemInstancesToProxyData.Remove(InstanceID);
	}
	);
}

bool UNiagaraDataInterfaceHairStrands::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float InDeltaSeconds)
{
	FNDIHairStrandsData* InstanceData = static_cast<FNDIHairStrandsData*>(PerInstanceData);

	FHairStrandsDatas* StrandsDatas = nullptr;
	FHairStrandsRestResource* StrandsRestResource = nullptr;
	FHairStrandsDeformedResource* StrandsDeformedResource = nullptr;
	FHairStrandsRestRootResource* StrandsRestRootResource = nullptr;
	FHairStrandsDeformedRootResource* StrandsDeformedRootResource = nullptr;
	UGroomAsset* GroomAsset = nullptr;
	int32 GroupIndex = 0;

	InstanceData->TickCount = FMath::Min(GHairSimulationMaxDelay + 1, InstanceData->TickCount + 1);

	FTransform LocalToWorld = FTransform::Identity;
	ExtractDatasAndResources(SystemInstance, StrandsDatas, StrandsRestResource, StrandsDeformedResource, StrandsRestRootResource, StrandsDeformedRootResource, GroomAsset, GroupIndex, LocalToWorld);
	InstanceData->HairStrandsBuffer->Update(StrandsDatas, StrandsRestResource, StrandsDeformedResource, StrandsRestRootResource, StrandsDeformedRootResource);

	if (SourceComponent != nullptr)
	{
		const bool bRequiresReset = SourceComponent->bResetSimulation || RequiresSimulationReset(SystemInstance, InstanceData->SkeletalMeshes);
		if (bRequiresReset)
		{
			InstanceData->TickCount = 0;
		}
		InstanceData->ForceReset = bRequiresReset;
	}
	InstanceData->Update(this, SystemInstance, StrandsDatas, GroomAsset, GroupIndex, LocalToWorld);
	return false;
}

bool UNiagaraDataInterfaceHairStrands::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceHairStrands* OtherTyped = CastChecked<UNiagaraDataInterfaceHairStrands>(Destination);
	OtherTyped->SourceActor= SourceActor;
	OtherTyped->SourceComponent = SourceComponent;
	OtherTyped->DefaultSource = DefaultSource;

	return true;
}

bool UNiagaraDataInterfaceHairStrands::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}
	const UNiagaraDataInterfaceHairStrands* OtherTyped = CastChecked<const UNiagaraDataInterfaceHairStrands>(Other);

	return  (OtherTyped->SourceActor == SourceActor) && (OtherTyped->SourceComponent == SourceComponent)
		&& (OtherTyped->DefaultSource == DefaultSource);
}

void UNiagaraDataInterfaceHairStrands::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		ENiagaraTypeRegistryFlags Flags = ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter;
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), Flags);
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
		Sig.Name = GetSubStepsName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Sub Steps")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetIterationCountName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Iteration Count")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetGravityVectorName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Gravity Vector")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetAirDragName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Air Drag")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetAirVelocityName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Air Velocity")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetSolveBendName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Solve Bend")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetProjectBendName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Project Bend")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetBendDampingName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Bend Damping")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetBendStiffnessName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Bend Stiffness")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetBendScaleName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Bend Scale")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetSolveStretchName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Solve Stretch")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetProjectStretchName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Project Stretch")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetStretchDampingName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Stretch Damping")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetStretchStiffnessName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Stretch Stiffness")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetStretchScaleName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Stretch Scale")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetSolveCollisionName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Solve Collision")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetProjectCollisionName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Project Collision")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetStaticFrictionName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Static Fraction")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetKineticFrictionName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Kinetic Friction")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetStrandsViscosityName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Strands Viscosity")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetGridDimensionName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Grid Dimension")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetCollisionRadiusName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Collision Radius")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetRadiusScaleName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Radius Scale")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetStrandsDensityName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Strands Density")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetStrandsSmoothingName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Strands Smoothing")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetStrandsThicknessName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Strands Thickness")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetThicknessScaleName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Thickness Scale")));

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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Strands Density")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Node Thickness")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Node Mass")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ComputeNodeInertiaName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Strands Density")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Node Thickness")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Inertia")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ComputeEdgeLengthName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Local Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Local Orientation")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Node Orientation")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = UpdateNodeStateName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Rest Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Node Orientation")));
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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Rest Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Report Status")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ResetPointPositionName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Report Status")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetBoundingBoxName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Box Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Box Center")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Box Extent")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ResetBoundingBoxName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Function Status")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = BuildBoundingBoxName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Function Status")));

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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Enable Constraint")));
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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Enable Constraint")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Stretch Stiffness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Node Thickness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Rest Length")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Node Offset")));
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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Enable Constraint")));
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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Enable Constraint")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Bend Stiffness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Node Thickness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Rest Length")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Rest Direction")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Enable Constraint")));
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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Enable Constraint")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Stretch Stiffness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Node Thickness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Rest Length")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));

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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Enable Constraint")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Rest Length")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Rest Rotation")));
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
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Enable Constraint")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Bend Stiffness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Node Thickness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Rest Length")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Rest Rotation")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Node Orientation")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SolveHardCollisionConstraintName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Enable Constraint")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Penetration Depth")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Collision Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Collision Velocity")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Collision Normal")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Static Friction")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Kinetic Friction")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Constraint Multiplier")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ProjectHardCollisionConstraintName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Enable Constraint")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Penetration Depth")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Collision Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Collision Velocity")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Collision Normal")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Static Friction")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Kinetic Friction")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SetupSoftCollisionConstraintName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Collision Stiffness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Damping")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Compliance")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Material Weight")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Material Multiplier")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = SolveSoftCollisionConstraintName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Enable Constraint")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Penetration Depth")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Collision Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Collision Velocity")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Collision Normal")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Static Friction")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Kinetic Friction")));
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
		Sig.Name = ProjectSoftCollisionConstraintName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Enable Constraint")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Collision Stiffness")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Penetration Depth")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Collision Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Collision Velocity")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Collision Normal")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Static Friction")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Kinetic Friction")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Delta Time")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ComputeEdgeDirectionName;
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
		Sig.Name = UpdateMaterialFrameName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Node Orientation")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = ComputeMaterialFrameName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetQuatDef(), TEXT("Node Orientation")));

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
		Sig.Name = InitGridSamplesName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Linear Velocity")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Node Mass")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Grid Length")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Num Samples")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Delta Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Delta Velocity")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Sample Mass")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = GetSampleStateName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Node Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Linear Velocity")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Delta Position")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Delta Velocity")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Num Samples")));
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetIntDef(), TEXT("Sample Index")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sample Position")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sample Velocity")));

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
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = HasGlobalInterpolationName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Global Interpolation")));

		OutFunctions.Add(Sig);
	}
	{
		FNiagaraFunctionSignature Sig;
		Sig.Name = NeedRestUpdateName;
		Sig.bMemberFunction = true;
		Sig.bRequiresContext = false;
		Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Hair Strands")));
		Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Rest Update")));

		OutFunctions.Add(Sig);
	}
}

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetNumStrands);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetStrandSize);

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetSubSteps);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetIterationCount);

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetGravityVector);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetAirDrag);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetAirVelocity);

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetSolveBend);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetProjectBend);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetBendDamping);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetBendStiffness);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetBendScale);

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetSolveStretch);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetProjectStretch);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetStretchDamping);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetStretchStiffness);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetStretchScale);

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetSolveCollision);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetProjectCollision);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetStaticFriction);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetKineticFriction);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetStrandsViscosity);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetGridDimension);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetCollisionRadius);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetRadiusScale);

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetStrandsDensity);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetStrandsSmoothing);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetStrandsThickness);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetThicknessScale);

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
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, UpdateNodeState);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, UpdatePointPosition);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ResetPointPosition);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, EvalSkinnedPosition);

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetBoundingBox);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ResetBoundingBox);
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

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SolveHardCollisionConstraint);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ProjectHardCollisionConstraint);

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SetupSoftCollisionConstraint);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ProjectSoftCollisionConstraint);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SolveSoftCollisionConstraint);

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeEdgeDirection);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, UpdateMaterialFrame);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeMaterialFrame);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeAirDragForce);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, NeedSimulationReset);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, HasGlobalInterpolation);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, NeedRestUpdate);

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, InitGridSamples);
DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetSampleState);

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
	else if (BindingInfo.Name == GetSubStepsName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetSubSteps)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetIterationCountName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetIterationCount)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetGravityVectorName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetGravityVector)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetAirDragName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetAirDrag)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetAirVelocityName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetAirVelocity)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetSolveBendName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetSolveBend)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetProjectBendName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetProjectBend)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetBendDampingName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetBendDamping)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetBendStiffnessName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetBendStiffness)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetBendScaleName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetBendScale)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetSolveStretchName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetSolveStretch)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetProjectStretchName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetProjectStretch)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetStretchDampingName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetStretchDamping)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetStretchStiffnessName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetStretchStiffness)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetStretchScaleName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetStretchScale)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetSolveCollisionName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetSolveCollision)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetProjectCollisionName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetProjectCollision)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetStaticFrictionName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetStaticFriction)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetKineticFrictionName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetKineticFriction)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetStrandsViscosityName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetStrandsViscosity)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetGridDimensionName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetGridDimension)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetCollisionRadiusName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetCollisionRadius)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetRadiusScaleName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetRadiusScale)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetStrandsDensityName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetStrandsDensity)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetStrandsSmoothingName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetStrandsSmoothing)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetStrandsThicknessName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetStrandsThickness)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == GetThicknessScaleName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetThicknessScale)::Bind(this, OutFunc);
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
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeNodePosition)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ComputeNodeOrientationName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 4);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeNodeOrientation)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ComputeNodeMassName)
	{
		check(BindingInfo.GetNumInputs() == 3 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeNodeMass)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ComputeNodeInertiaName)
	{
		check(BindingInfo.GetNumInputs() == 3 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeNodeInertia)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ComputeEdgeLengthName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeEdgeLength)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ComputeEdgeRotationName)
	{
		check(BindingInfo.GetNumInputs() == 5 && BindingInfo.GetNumOutputs() == 4);
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
		check(BindingInfo.GetNumInputs() == 8 && BindingInfo.GetNumOutputs() == 7);
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
		check(BindingInfo.GetNumInputs() == 8 && BindingInfo.GetNumOutputs() == 7);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, AttachNodeState)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == UpdateNodeStateName)
	{
		check(BindingInfo.GetNumInputs() == 11 && BindingInfo.GetNumOutputs() == 7);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, UpdateNodeState)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == UpdatePointPositionName)
	{
		check(BindingInfo.GetNumInputs() == 7 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, UpdatePointPosition)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ResetPointPositionName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
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
	else if (BindingInfo.Name == GetBoundingBoxName)
	{
		check(BindingInfo.GetNumInputs() == 2 && BindingInfo.GetNumOutputs() == 6);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetBoundingBox)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ResetBoundingBoxName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ResetBoundingBox)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == BuildBoundingBoxName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, BuildBoundingBox)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SetupDistanceSpringMaterialName)
	{
		check(BindingInfo.GetNumInputs() == 7 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SetupDistanceSpringMaterial)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SolveDistanceSpringMaterialName)
	{
		check(BindingInfo.GetNumInputs() == 9 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SolveDistanceSpringMaterial)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ProjectDistanceSpringMaterialName)
	{
		check(BindingInfo.GetNumInputs() == 7 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ProjectDistanceSpringMaterial)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SetupAngularSpringMaterialName)
	{
		check(BindingInfo.GetNumInputs() == 6 && BindingInfo.GetNumOutputs() == 5);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SetupAngularSpringMaterial)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SolveAngularSpringMaterialName)
	{
		check(BindingInfo.GetNumInputs() == 13 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SolveAngularSpringMaterial)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ProjectAngularSpringMaterialName)
	{
		check(BindingInfo.GetNumInputs() == 9 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ProjectAngularSpringMaterial)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SetupStretchRodMaterialName)
	{
		check(BindingInfo.GetNumInputs() == 6 && BindingInfo.GetNumOutputs() == 5);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SetupStretchRodMaterial)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SolveStretchRodMaterialName)
	{
		check(BindingInfo.GetNumInputs() == 10 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SolveStretchRodMaterial)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ProjectStretchRodMaterialName)
	{
		check(BindingInfo.GetNumInputs() == 6 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ProjectStretchRodMaterial)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SetupBendRodMaterialName)
	{
		check(BindingInfo.GetNumInputs() == 6 && BindingInfo.GetNumOutputs() == 5);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SetupBendRodMaterial)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SolveBendRodMaterialName)
	{
		check(BindingInfo.GetNumInputs() == 14 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SolveBendRodMaterial)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ProjectBendRodMaterialName)
	{
		check(BindingInfo.GetNumInputs() == 10 && BindingInfo.GetNumOutputs() == 4);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ProjectBendRodMaterial)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SolveHardCollisionConstraintName)
	{
		check(BindingInfo.GetNumInputs() == 15 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SolveHardCollisionConstraint)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ProjectHardCollisionConstraintName)
	{
		check(BindingInfo.GetNumInputs() == 15 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ProjectHardCollisionConstraint)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SolveSoftCollisionConstraintName)
	{
		check(BindingInfo.GetNumInputs() == 21 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SolveSoftCollisionConstraint)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ProjectSoftCollisionConstraintName)
	{
		check(BindingInfo.GetNumInputs() == 16 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ProjectSoftCollisionConstraint)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == SetupSoftCollisionConstraintName)
	{
		check(BindingInfo.GetNumInputs() == 4 && BindingInfo.GetNumOutputs() == 5);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, SetupSoftCollisionConstraint)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ComputeEdgeDirectionName)
	{
		check(BindingInfo.GetNumInputs() == 8 && BindingInfo.GetNumOutputs() == 3);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeEdgeDirection)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == UpdateMaterialFrameName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 4);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, UpdateMaterialFrame)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ComputeMaterialFrameName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 4);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeMaterialFrame)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == ComputeAirDragForceName)
	{
		check(BindingInfo.GetNumInputs() == 14 && BindingInfo.GetNumOutputs() == 6);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, ComputeAirDragForce)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == InitGridSamplesName)
	{
		check(BindingInfo.GetNumInputs() == 9 && BindingInfo.GetNumOutputs() == 8);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, InitGridSamples)::Bind(this, OutFunc);
		}
	else if (BindingInfo.Name == GetSampleStateName)
	{
		check(BindingInfo.GetNumInputs() == 15 && BindingInfo.GetNumOutputs() == 6);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, GetSampleState)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == NeedSimulationResetName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, NeedSimulationReset)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == HasGlobalInterpolationName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, HasGlobalInterpolation)::Bind(this, OutFunc);
	}
	else if (BindingInfo.Name == NeedRestUpdateName)
	{
		check(BindingInfo.GetNumInputs() == 1 && BindingInfo.GetNumOutputs() == 1);
		NDI_FUNC_BINDER(UNiagaraDataInterfaceHairStrands, NeedRestUpdate)::Bind(this, OutFunc);
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
		*OutStrandSize.GetDestAndAdvance() = InstData->StrandsSize;
	}
}

void UNiagaraDataInterfaceHairStrands::GetSubSteps(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutSubSteps(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutSubSteps.GetDestAndAdvance() = InstData->SubSteps;
	}
}

void UNiagaraDataInterfaceHairStrands::GetIterationCount(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutIterationCount(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutIterationCount.GetDestAndAdvance() = InstData->IterationCount;
	}
}

void UNiagaraDataInterfaceHairStrands::GetGravityVector(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutGravityVectorX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutGravityVectorY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutGravityVectorZ(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutGravityVectorX.GetDestAndAdvance() = InstData->GravityVector.X;
		*OutGravityVectorY.GetDestAndAdvance() = InstData->GravityVector.Y;
		*OutGravityVectorZ.GetDestAndAdvance() = InstData->GravityVector.Z;
	}
}

void UNiagaraDataInterfaceHairStrands::GetAirDrag(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutAirDrag(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutAirDrag.GetDestAndAdvance() = InstData->AirDrag;
	}
}

void UNiagaraDataInterfaceHairStrands::GetAirVelocity(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutAirVelocityX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutAirVelocityY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutAirVelocityZ(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutAirVelocityX.GetDestAndAdvance() = InstData->AirVelocity.X;
		*OutAirVelocityY.GetDestAndAdvance() = InstData->AirVelocity.Y;
		*OutAirVelocityZ.GetDestAndAdvance() = InstData->AirVelocity.Z;
	}
}

void UNiagaraDataInterfaceHairStrands::GetSolveBend(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutSolveBend(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutSolveBend.GetDestAndAdvance() = InstData->SolveBend;
	}
}

void UNiagaraDataInterfaceHairStrands::GetProjectBend(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutProjectBend(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutProjectBend.GetDestAndAdvance() = InstData->ProjectBend;
	}
}

void UNiagaraDataInterfaceHairStrands::GetBendDamping(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutBendDamping(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutBendDamping.GetDestAndAdvance() = InstData->BendDamping;
	}
}

void UNiagaraDataInterfaceHairStrands::GetBendStiffness(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutBendStiffness(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutBendStiffness.GetDestAndAdvance() = InstData->BendStiffness;
	}
}

void UNiagaraDataInterfaceHairStrands::GetBendScale(FVectorVMContext& Context)
{}

void UNiagaraDataInterfaceHairStrands::GetSolveStretch(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutSolveStretch(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutSolveStretch.GetDestAndAdvance() = InstData->SolveStretch;
	}
}

void UNiagaraDataInterfaceHairStrands::GetProjectStretch(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutProjectStretch(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutProjectStretch.GetDestAndAdvance() = InstData->ProjectStretch;
	}
}

void UNiagaraDataInterfaceHairStrands::GetStretchDamping(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutStretchDamping(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutStretchDamping.GetDestAndAdvance() = InstData->StretchDamping;
	}
}

void UNiagaraDataInterfaceHairStrands::GetStretchStiffness(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutStretchStiffness(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutStretchStiffness.GetDestAndAdvance() = InstData->StretchStiffness;
	}
}

void UNiagaraDataInterfaceHairStrands::GetStretchScale(FVectorVMContext& Context)
{
}


void UNiagaraDataInterfaceHairStrands::GetSolveCollision(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutSolveCollision(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutSolveCollision.GetDestAndAdvance() = InstData->SolveCollision;
	}
}

void UNiagaraDataInterfaceHairStrands::GetProjectCollision(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutProjectCollision(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutProjectCollision.GetDestAndAdvance() = InstData->ProjectCollision;
	}
}

void UNiagaraDataInterfaceHairStrands::GetStaticFriction(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutStaticFriction(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutStaticFriction.GetDestAndAdvance() = InstData->StaticFriction;
	}
}

void UNiagaraDataInterfaceHairStrands::GetKineticFriction(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutKineticFriction(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutKineticFriction.GetDestAndAdvance() = InstData->KineticFriction;
	}
}

void UNiagaraDataInterfaceHairStrands::GetStrandsViscosity(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutStrandsViscosity(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutStrandsViscosity.GetDestAndAdvance() = InstData->StrandsViscosity;
	}
}

void UNiagaraDataInterfaceHairStrands::GetGridDimension(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutGridDimensionX(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutGridDimensionY(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutGridDimensionZ(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutGridDimensionX.GetDestAndAdvance() = InstData->GridDimension.X;
		*OutGridDimensionY.GetDestAndAdvance() = InstData->GridDimension.Y;
		*OutGridDimensionZ.GetDestAndAdvance() = InstData->GridDimension.Z;
	}
}
void UNiagaraDataInterfaceHairStrands::GetCollisionRadius(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutCollisionRadius(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutCollisionRadius.GetDestAndAdvance() = InstData->CollisionRadius;
	}
}

void UNiagaraDataInterfaceHairStrands::GetRadiusScale(FVectorVMContext& Context)
{
}

void UNiagaraDataInterfaceHairStrands::GetStrandsSmoothing(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutStrandsSmoothing(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutStrandsSmoothing.GetDestAndAdvance() = InstData->StrandsSmoothing;
	}
}

void UNiagaraDataInterfaceHairStrands::GetStrandsDensity(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutStrandsDensity(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutStrandsDensity.GetDestAndAdvance() = InstData->StrandsDensity;
	}
}

void UNiagaraDataInterfaceHairStrands::GetStrandsThickness(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<float> OutStrandsThickness(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutStrandsThickness.GetDestAndAdvance() = InstData->StrandsThickness;
	}
}

void UNiagaraDataInterfaceHairStrands::GetThicknessScale(FVectorVMContext& Context)
{
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

void UNiagaraDataInterfaceHairStrands::GetBoundingBox(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::ResetBoundingBox(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::BuildBoundingBox(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
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

void UNiagaraDataInterfaceHairStrands::EvalSkinnedPosition(FVectorVMContext& Context)
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

void UNiagaraDataInterfaceHairStrands::UpdateNodeState(FVectorVMContext& Context)
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

void UNiagaraDataInterfaceHairStrands::ComputeEdgeDirection(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::UpdateMaterialFrame(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::ComputeMaterialFrame(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::SolveHardCollisionConstraint(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::ProjectHardCollisionConstraint(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::SolveSoftCollisionConstraint(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::ProjectSoftCollisionConstraint(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}
void UNiagaraDataInterfaceHairStrands::SetupSoftCollisionConstraint(FVectorVMContext& Context)
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

void UNiagaraDataInterfaceHairStrands::InitGridSamples(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::GetSampleState(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

void UNiagaraDataInterfaceHairStrands::HasGlobalInterpolation(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FNDIHairStrandsData> InstData(Context);
	VectorVM::FExternalFuncRegisterHandler<int32> OutGlobalInterpolation(Context);

	for (int32 InstanceIdx = 0; InstanceIdx < Context.NumInstances; ++InstanceIdx)
	{
		*OutGlobalInterpolation.GetDestAndAdvance() = InstData->GlobalInterpolation;
	}
}

void UNiagaraDataInterfaceHairStrands::NeedRestUpdate(FVectorVMContext& Context)
{
	// @todo : implement function for cpu 
}

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceHairStrands::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	FNDIHairStrandsParametersName ParamNames(ParamInfo.DataInterfaceHLSLSymbol);

	TMap<FString, FStringFormatArg> ArgsSample = {
		{TEXT("InstanceFunctionName"), FunctionInfo.InstanceName},
		{TEXT("NumStrandsName"), ParamNames.NumStrandsName},
		{TEXT("StrandSizeName"), ParamNames.StrandSizeName},
		{TEXT("WorldTransformName"), ParamNames.WorldTransformName},
		{TEXT("WorldInverseName"), ParamNames.WorldInverseName},
		{TEXT("WorldRotationName"), ParamNames.WorldRotationName},
		{TEXT("DeformedPositionBufferName"), ParamNames.DeformedPositionBufferName},
		{TEXT("CurvesOffsetsBufferName"), ParamNames.CurvesOffsetsBufferName},
		{TEXT("RestPositionBufferName"), ParamNames.RestPositionBufferName},
		{TEXT("HairStrandsContextName"), TEXT("DIHAIRSTRANDS_MAKE_CONTEXT(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")")},
	};

	if (FunctionInfo.DefinitionName == GetStrandSizeName)
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
	else if (FunctionInfo.DefinitionName == GetNumStrandsName)
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
	else if (FunctionInfo.DefinitionName == GetWorldTransformName)
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
	else if (FunctionInfo.DefinitionName == GetWorldInverseName)
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
	if (FunctionInfo.DefinitionName == GetStretchScaleName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(out float OutStretchScale)
		{
			{HairStrandsContextName} OutStretchScale = DIContext.ParamsScaleBuffer[GGroupThreadId.x % DIContext.StrandSize];
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	if (FunctionInfo.DefinitionName == GetBendScaleName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(out float OutBendScale)
		{
			{HairStrandsContextName} OutBendScale = DIContext.ParamsScaleBuffer[32 + GGroupThreadId.x % DIContext.StrandSize];
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	if (FunctionInfo.DefinitionName == GetRadiusScaleName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(out float OutRadiusScale)
		{
			{HairStrandsContextName} OutRadiusScale = DIContext.ParamsScaleBuffer[64 + GGroupThreadId.x % DIContext.StrandSize];
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	if (FunctionInfo.DefinitionName == GetThicknessScaleName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
		void {InstanceFunctionName}(out float OutThicknessScale)
		{
			{HairStrandsContextName} OutThicknessScale = DIContext.ParamsScaleBuffer[96 + GGroupThreadId.x % DIContext.StrandSize];
		}
		)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetPointPositionName)
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
	else if (FunctionInfo.DefinitionName == ComputeNodePositionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in float SmoothingFilter, out float3 OutNodePosition)
			{
				{HairStrandsContextName} DIHairStrands_ComputeNodePosition(DIContext,OutNodePosition);
				DIHairStrands_SmoothNodePosition(DIContext,SmoothingFilter,OutNodePosition);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == ComputeNodeOrientationName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in float3 NodePosition, out float4 OutNodeOrientation)
			{
				{HairStrandsContextName} DIHairStrands_ComputeNodeOrientation(DIContext,NodePosition,OutNodeOrientation);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == ComputeNodeMassName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in float StrandsDensity, in float NodeThickness, out float OutNodeMass)
			{
				{HairStrandsContextName} DIHairStrands_ComputeNodeMass(DIContext,StrandsDensity,NodeThickness,OutNodeMass);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == ComputeNodeInertiaName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in float StrandsDensity, in float NodeThickness, out float3 OutNodeInertia)
			{
				{HairStrandsContextName} DIHairStrands_ComputeNodeInertia(DIContext,StrandsDensity,NodeThickness,OutNodeInertia);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == ComputeEdgeLengthName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in float3 NodePosition, in int NodeOffset, out float OutEdgeLength)
			{
				{HairStrandsContextName} 
				if(NodeOffset == 2)
				{
					DIHairStrands_ComputeEdgeVolume(DIContext,NodePosition,OutEdgeLength);
				}
				else
				{
					DIHairStrands_ComputeEdgeLength(DIContext,NodePosition,NodeOffset,OutEdgeLength);
				}
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == ComputeEdgeRotationName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in float4 NodeOrientation, out float4 OutEdgeRotation)
			{
				{HairStrandsContextName} DIHairStrands_ComputeEdgeRotation(DIContext,NodeOrientation,OutEdgeRotation);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == ComputeRestPositionName)
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
	else if (FunctionInfo.DefinitionName == ComputeRestOrientationName)
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
	else if (FunctionInfo.DefinitionName == ComputeLocalStateName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in float3 RestPosition, in float4 RestOrientation, out float3 LocalPosition, out float4 LocalOrientation)
			{
				{HairStrandsContextName} DIHairStrands_ComputeLocalState(DIContext,RestPosition,RestOrientation,LocalPosition,LocalOrientation);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == AttachNodePositionName)
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
	else if (FunctionInfo.DefinitionName == AttachNodeOrientationName)
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
	else if (FunctionInfo.DefinitionName == AttachNodeStateName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} ( in float3 LocalPosition, in float4 LocalOrientation, out float3 NodePosition, out float4 NodeOrientation)
				{
					{HairStrandsContextName} DIHairStrands_AttachNodeState(DIContext,LocalPosition,LocalOrientation,NodePosition,NodeOrientation);
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == UpdateNodeStateName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
					void {InstanceFunctionName} ( in float3 RestPosition, in float3 NodePosition, in float4 NodeOrientation, out float3 OutNodePosition, out float4 OutNodeOrientation)
					{
						{HairStrandsContextName} DIHairStrands_UpdateNodeState(DIContext,RestPosition,NodePosition,NodeOrientation,OutNodePosition,OutNodeOrientation);
					}
					)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == UpdatePointPositionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (in float3 NodePosition, in float3 RestPosition, out bool OutReportStatus)
			{
				{HairStrandsContextName} DIHairStrands_UpdatePointPosition(DIContext,NodePosition, RestPosition ,OutReportStatus);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == ResetPointPositionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
			void {InstanceFunctionName} (out bool OutReportStatus)
			{
				{HairStrandsContextName} DIHairStrands_ResetPointPosition(DIContext,OutReportStatus);
			}
			)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == AdvectNodePositionName)
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
	else if (FunctionInfo.DefinitionName == AdvectNodeOrientationName)
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
	else if (FunctionInfo.DefinitionName == UpdateLinearVelocityName)
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
	else if (FunctionInfo.DefinitionName == UpdateAngularVelocityName)
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
	else if (FunctionInfo.DefinitionName == GetBoundingBoxName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} (in int BoxIndex, out float3 OutBoxCenter, out float3 OutBoxExtent)
				{
					{HairStrandsContextName} DIHairStrands_GetBoundingBox(DIContext,DIContext_BoundingBoxBuffer,BoxIndex,OutBoxCenter,OutBoxExtent);
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == ResetBoundingBoxName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} (out bool FunctionStatus)
				{
					{HairStrandsContextName} DIHairStrands_ResetBoundingBox(DIContext,DIContext_BoundingBoxBuffer,FunctionStatus);
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == BuildBoundingBoxName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
					void {InstanceFunctionName} (in float3 NodePosition, out bool OutFunctionStatus)
					{
						{HairStrandsContextName} DIHairStrands_BuildBoundingBox(DIContext,DIContext_BoundingBoxBuffer,NodePosition,OutFunctionStatus);
					}
					)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SetupDistanceSpringMaterialName)
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
	else if (FunctionInfo.DefinitionName == SolveDistanceSpringMaterialName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} (in bool EnableConstraint, in float RestLength, in float DeltaTime, in int NodeOffset, in float MaterialDamping, 
		in float MaterialCompliance, in float MaterialWeight, in float MaterialMultiplier, out float OutMaterialMultiplier)
				{
					{HairStrandsContextName} 
					if(NodeOffset == 0)
					{
						SolveStretchSpringMaterial(EnableConstraint,DIContext.StrandSize,RestLength,DeltaTime,MaterialDamping,MaterialCompliance,MaterialWeight,MaterialMultiplier,OutMaterialMultiplier);
					}
					else if(NodeOffset == 1)
					{
						SolveBendSpringMaterial(EnableConstraint,DIContext.StrandSize,RestLength,DeltaTime,MaterialDamping,MaterialCompliance,MaterialWeight,MaterialMultiplier,OutMaterialMultiplier);
					}
					else if(NodeOffset == 2)
					{
						SolveTwistSpringMaterial(EnableConstraint,DIContext.StrandSize,RestLength,DeltaTime,MaterialDamping,MaterialCompliance,MaterialWeight,MaterialMultiplier,OutMaterialMultiplier);
					}
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == ProjectDistanceSpringMaterialName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
				void {InstanceFunctionName} (in bool EnableConstraint, in float YoungModulus, in float RodThickness, in float RestLength, in float DeltaTime, in int NodeOffset, out float3 OutNodePosition)
				{
					{HairStrandsContextName} 
					if(NodeOffset == 0)
					{
						ProjectStretchSpringMaterial(EnableConstraint,DIContext.StrandSize,YoungModulus,RodThickness,RestLength,DeltaTime,OutNodePosition);
					}
					if(NodeOffset == 1)
					{
						ProjectBendSpringMaterial(EnableConstraint,DIContext.StrandSize,YoungModulus,RodThickness,RestLength,DeltaTime,OutNodePosition);
					}
					if(NodeOffset == 2)
					{
						ProjectTwistSpringMaterial(EnableConstraint,DIContext.StrandSize,YoungModulus,RodThickness,RestLength,DeltaTime,OutNodePosition);
					}
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SetupAngularSpringMaterialName)
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
	else if (FunctionInfo.DefinitionName == SolveAngularSpringMaterialName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
					void {InstanceFunctionName} (in bool EnableConstraint, in float RestLength, in float3 RestDirection, in float DeltaTime, in float MaterialDamping, 
			in float MaterialCompliance, in float MaterialWeight, in float3 MaterialMultiplier, out float3 OutMaterialMultiplier)
					{
						{HairStrandsContextName} SolveAngularSpringMaterial(EnableConstraint,DIContext.StrandSize,RestLength, RestDirection,DeltaTime,MaterialDamping,MaterialCompliance,MaterialWeight,MaterialMultiplier,OutMaterialMultiplier);
					}
					)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == ProjectAngularSpringMaterialName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
					void {InstanceFunctionName} (in bool EnableConstraint, in float YoungModulus, in float RodThickness, in float RestLength, in float3 RestDirection, in float DeltaTime, out float3 OutNodePosition)
					{
						{HairStrandsContextName} ProjectAngularSpringMaterial(EnableConstraint,DIContext.StrandSize,YoungModulus,RodThickness,RestLength,RestDirection,DeltaTime,OutNodePosition);
					}
					)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SetupStretchRodMaterialName)
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
	else if (FunctionInfo.DefinitionName == SolveStretchRodMaterialName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
					void {InstanceFunctionName} (in bool EnableConstraint, in float RestLength, in float DeltaTime, in float MaterialDamping, 
			in float MaterialCompliance, in float MaterialWeight, in float3 MaterialMultiplier, out float3 OutMaterialMultiplier)
					{
						{HairStrandsContextName} SolveStretchRodMaterial(EnableConstraint,DIContext.StrandSize,RestLength,DeltaTime,MaterialDamping,MaterialCompliance,MaterialWeight,MaterialMultiplier,OutMaterialMultiplier);
					}
					)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == ProjectStretchRodMaterialName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
					void {InstanceFunctionName} (in bool EnableConstraint, in float YoungModulus, in float RodThickness, in float RestLength, in float DeltaTime, out float3 OutNodePosition)
					{
						{HairStrandsContextName} ProjectStretchRodMaterial(EnableConstraint,DIContext.StrandSize,YoungModulus,RodThickness,RestLength,DeltaTime,OutNodePosition);
					}
					)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SetupBendRodMaterialName)
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
	else if (FunctionInfo.DefinitionName == SolveBendRodMaterialName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
						void {InstanceFunctionName} (in bool EnableConstraint, in float RestLength, in float4 RestRotation, in float DeltaTime, in float MaterialDamping, 
				in float MaterialCompliance, in float MaterialWeight, in float3 MaterialMultiplier, out float3 OutMaterialMultiplier)
						{
							{HairStrandsContextName} SolveBendRodMaterial(EnableConstraint,DIContext.StrandSize,RestLength,RestRotation,DeltaTime,MaterialDamping,MaterialCompliance,MaterialWeight,MaterialMultiplier,OutMaterialMultiplier);
						}
						)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == ProjectBendRodMaterialName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
						void {InstanceFunctionName} (in bool EnableConstraint, in float YoungModulus, in float RodThickness, in float RestLength, in float4 RestRotation, in float DeltaTime, out float4 OutNodeOrientation)
						{
							{HairStrandsContextName} ProjectBendRodMaterial(EnableConstraint,DIContext.StrandSize,YoungModulus,RodThickness,RestLength,RestRotation,DeltaTime,OutNodeOrientation);
						}
						)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SolveHardCollisionConstraintName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
							void {InstanceFunctionName} (in bool EnableConstraint, in float PenetrationDepth, in float3 CollisionPosition, in float3 CollisionVelocity, in float3 CollisionNormal, 
				in float StaticFriction, in float KineticFriction, in float DeltaTime, out float3 OutMaterialMultiplier )
							{
								OutMaterialMultiplier = float3(0,0,0);
								{HairStrandsContextName} SolveHardCollisionConstraint(EnableConstraint,DIContext.StrandSize,PenetrationDepth,
									CollisionPosition,CollisionVelocity,CollisionNormal,StaticFriction,KineticFriction,false,DeltaTime);
							}
							)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == ProjectHardCollisionConstraintName)
	{
			static const TCHAR *FormatSample = TEXT(R"(
						void {InstanceFunctionName} (in bool EnableConstraint, in float PenetrationDepth, in float3 CollisionPosition, in float3 CollisionVelocity, in float3 CollisionNormal, 
			in float StaticFriction, in float KineticFriction, in float DeltaTime, out float3 OutNodePosition )
						{
							{HairStrandsContextName} ProjectHardCollisionConstraint(EnableConstraint,DIContext.StrandSize,PenetrationDepth,
								CollisionPosition,CollisionVelocity,CollisionNormal,StaticFriction,KineticFriction,DeltaTime,OutNodePosition);
						}
						)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SolveSoftCollisionConstraintName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
								void {InstanceFunctionName} (in bool EnableConstraint, in float PenetrationDepth, in float3 CollisionPosition, in float3 CollisionVelocity, in float3 CollisionNormal, 
					in float StaticFriction, in float KineticFriction, in float DeltaTime, in float MaterialDamping, 
			in float MaterialCompliance, in float MaterialWeight, in float3 MaterialMultiplier, out float3 OutMaterialMultiplier )
								{
									{HairStrandsContextName} SolveSoftCollisionConstraint(EnableConstraint,DIContext.StrandSize,PenetrationDepth,
										CollisionPosition,CollisionVelocity,CollisionNormal,StaticFriction,KineticFriction,false,DeltaTime,MaterialDamping,
											MaterialCompliance,MaterialWeight,MaterialMultiplier,OutMaterialMultiplier);
								}
								)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == ProjectSoftCollisionConstraintName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
							void {InstanceFunctionName} (in bool EnableConstraint, in float ConstraintStiffness, in float PenetrationDepth, in float3 CollisionPosition, in float3 CollisionVelocity, in float3 CollisionNormal, 
					in float StaticFriction, in float KineticFriction, in float DeltaTime, out float3 OutNodePosition )
							{
								{HairStrandsContextName} ProjectSoftCollisionConstraint(EnableConstraint,DIContext.StrandSize,ConstraintStiffness,PenetrationDepth,
									CollisionPosition,CollisionVelocity,CollisionNormal,StaticFriction,KineticFriction,DeltaTime,OutNodePosition);
							}
							)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == SetupSoftCollisionConstraintName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
								void {InstanceFunctionName} (in float ConstraintStiffness, in float DeltaTime, in float MaterialDamping, out float OutMaterialCompliance, out float OutMaterialWeight, out float3 OutMaterialMultiplier )
								{
									{HairStrandsContextName} SetupSoftCollisionConstraint(DIContext.StrandSize,ConstraintStiffness,DeltaTime,MaterialDamping,OutMaterialCompliance,OutMaterialWeight,OutMaterialMultiplier);
								}
								)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == ComputeEdgeDirectionName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
							void {InstanceFunctionName} (in float3 NodePosition, in float4 NodeOrientation, out float3 OutRestDirection)
							{
								{HairStrandsContextName} DIHairStrands_ComputeEdgeDirection(DIContext,NodePosition,NodeOrientation,OutRestDirection);
							}
							)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == UpdateMaterialFrameName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
					void {InstanceFunctionName} (out float4 OutNodeOrientation)
					{
						{HairStrandsContextName} UpdateMaterialFrame(DIContext.StrandSize);
						OutNodeOrientation = SharedNodeOrientation[GGroupThreadId.x];
					}
					)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == ComputeMaterialFrameName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
						void {InstanceFunctionName} ( out float4 OutNodeOrientation)
						{
							{HairStrandsContextName} ComputeMaterialFrame(DIContext.StrandSize);
							OutNodeOrientation = SharedNodeOrientation[GGroupThreadId.x];
						}
						)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == ComputeAirDragForceName)
	{
		static const TCHAR *FormatSample = TEXT(R"(
						void {InstanceFunctionName} (  in float AirDensity, in float AirViscosity, in float AirDrag, 
		in float3 AirVelocity, in float NodeThickness, in float3 NodePosition, in float3 NodeVelocity, out float3 OutAirDrag, out float3 OutDragGradient )
						{
							{HairStrandsContextName} ComputeAirDragForce(DIContext.StrandSize,AirDensity,AirViscosity,AirDrag,AirVelocity,NodeThickness,NodePosition,NodeVelocity,OutAirDrag,OutDragGradient);
						}
						)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == InitGridSamplesName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
							void {InstanceFunctionName} ( in float3 NodePosition, in float3 NodeVelocity, 
	in float NodeMass, in float GridLength, out int OutNumSamples,
						out float3 OutDeltaPosition, out float3 OutDeltaVelocity, out float OutSampleMass)
							{
								{HairStrandsContextName} DIHairStrands_InitGridSamples(DIContext,
										 NodePosition, NodeVelocity, NodeMass, GridLength, 
											OutNumSamples, OutDeltaPosition, OutDeltaVelocity, OutSampleMass);
							}
							)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == GetSampleStateName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
								void {InstanceFunctionName} ( in float3 NodePosition, in float3 NodeVelocity, in float3 DeltaPosition, in float3 DeltaVelocity, 
			in int NumSamples, in int SampleIndex, out float3 OutSamplePosition, out float3 OutSampleVelocity)
								{
									{HairStrandsContextName} DIHairStrands_GetSampleState(DIContext,
											 NodePosition, NodeVelocity, DeltaPosition, DeltaVelocity, 
											 NumSamples, SampleIndex, OutSamplePosition, OutSampleVelocity);
								}
								)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == NeedSimulationResetName)
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
	else if (FunctionInfo.DefinitionName == HasGlobalInterpolationName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
				void {InstanceFunctionName} ( out bool GlobalInterpolation)
				{
					{HairStrandsContextName} GlobalInterpolation  = (DIContext.InterpolationMode == 2);
				}
				)");
		OutHLSL += FString::Format(FormatSample, ArgsSample);
		return true;
	}
	else if (FunctionInfo.DefinitionName == NeedRestUpdateName)
	{
		static const TCHAR* FormatSample = TEXT(R"(
				void {InstanceFunctionName} ( out bool RestUpdate)
				{
					{HairStrandsContextName} RestUpdate  = DIContext.RestUpdate;
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
	OutHLSL += TEXT("#include \"/Plugin/Runtime/HairStrands/Private/NiagaraQuaternionUtils.ush\"\n");
	//OutHLSL += TEXT("#include \"/Plugin/Runtime/HairStrands/Private/NiagaraDirectSolver.ush\"\n");
	OutHLSL += TEXT("#include \"/Plugin/Runtime/HairStrands/Private/NiagaraStrandsExternalForce.ush\"\n");
	OutHLSL += TEXT("#include \"/Plugin/Runtime/HairStrands/Private/NiagaraHookeSpringMaterial.ush\"\n");
	OutHLSL += TEXT("#include \"/Plugin/Runtime/HairStrands/Private/NiagaraAngularSpringMaterial.ush\"\n");
	OutHLSL += TEXT("#include \"/Plugin/Runtime/HairStrands/Private/NiagaraConstantVolumeMaterial.ush\"\n");
	OutHLSL += TEXT("#include \"/Plugin/Runtime/HairStrands/Private/NiagaraCosseratRodMaterial.ush\"\n");
	OutHLSL += TEXT("#include \"/Plugin/Runtime/HairStrands/Private/NiagaraStaticCollisionConstraint.ush\"\n");
	OutHLSL += TEXT("#include \"/Plugin/Runtime/HairStrands/Private/NiagaraDataInterfaceHairStrands.ush\"\n");
}

bool UNiagaraDataInterfaceHairStrands::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const
{
	if (!Super::AppendCompileHash(InVisitor))
		return false;

	for (const TCHAR* VirtualFilePath :
		{
			TEXT("/Plugin/Runtime/HairStrands/Private/NiagaraQuaternionUtils.ush"),
			TEXT("/Plugin/Runtime/HairStrands/Private/NiagaraStrandsExternalForce.ush"),
			TEXT("/Plugin/Runtime/HairStrands/Private/NiagaraHookeSpringMaterial.ush"),
			TEXT("/Plugin/Runtime/HairStrands/Private/NiagaraAngularSpringMaterial.ush"),
			TEXT("/Plugin/Runtime/HairStrands/Private/NiagaraConstantVolumeMaterial.ush"),
			TEXT("/Plugin/Runtime/HairStrands/Private/NiagaraCosseratRodMaterial.ush"),
			TEXT("/Plugin/Runtime/HairStrands/Private/NiagaraStaticCollisionConstraint.ush"),
			TEXT("/Plugin/Runtime/HairStrands/Private/NiagaraDataInterfaceHairStrands.ush")
		})
	{
		FSHAHash Hash = GetShaderFileHash(VirtualFilePath, EShaderPlatform::SP_PCD3D_SM5);
		InVisitor->UpdateString(TEXT("NiagaraDataInterfaceHairStrandsHLSLSource"), Hash.ToString());
	}

	return true;
}

void UNiagaraDataInterfaceHairStrands::GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL)
{
	OutHLSL += TEXT("DIHAIRSTRANDS_DECLARE_CONSTANTS(") + ParamInfo.DataInterfaceHLSLSymbol + TEXT(")\n");
}
#endif

void UNiagaraDataInterfaceHairStrands::ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance)
{
	check(Proxy);

	FNDIHairStrandsData* GameThreadData = static_cast<FNDIHairStrandsData*>(PerInstanceData);
	FNDIHairStrandsData* RenderThreadData = static_cast<FNDIHairStrandsData*>(DataForRenderThread);

	if (GameThreadData && RenderThreadData)
	{
		RenderThreadData->CopyDatas(GameThreadData);
	}
}

void FNDIHairStrandsProxy::PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context)
{
	if (Context.SimStageData->bFirstStage)
	{
		FNDIHairStrandsData* ProxyData =
			SystemInstancesToProxyData.Find(Context.SystemInstanceID);

		if (ProxyData != nullptr && ProxyData->HairStrandsBuffer != nullptr)
		{
			FIntVector4& BoundingBoxOffsets = ProxyData->HairStrandsBuffer->BoundingBoxOffsets;
			const int32 FirstOffset = BoundingBoxOffsets[0];

			BoundingBoxOffsets[0] = BoundingBoxOffsets[1];
			BoundingBoxOffsets[1] = BoundingBoxOffsets[2];
			BoundingBoxOffsets[2] = BoundingBoxOffsets[3];
			BoundingBoxOffsets[3] = FirstOffset;

			ProxyData->HairStrandsBuffer->Transfer(ProxyData->ParamsScale);
		}
	}
}

#undef LOCTEXT_NAMESPACE
