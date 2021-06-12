// Copyright Epic Games, Inc. All Rights Reserved.

#include "LatticeDeformerTool.h"

#include "Mechanics/LatticeControlPointsMechanic.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "BaseGizmos/GizmoRenderingUtil.h"
#include "BaseBehaviors/SingleClickBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "DeformationOps/LatticeDeformerOp.h"
#include "Properties/MeshMaterialProperties.h"
#include "Selection/ToolSelectionUtil.h"
#include "MeshOpPreviewHelpers.h" //FDynamicMeshOpResult
#include "ToolSceneQueriesUtil.h"
#include "ToolBuilderUtil.h"
#include "ToolSetupUtil.h"
#include "DynamicMeshToMeshDescription.h"
#include "MeshTransforms.h"
#include "Algo/ForEach.h"
#include "Operations/FFDLattice.h"

#include "TargetInterfaces/MaterialProvider.h"
#include "TargetInterfaces/MeshDescriptionCommitter.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "ULatticeDeformerTool"

// Tool builder

USingleSelectionMeshEditingTool* ULatticeDeformerToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	return NewObject<ULatticeDeformerTool>(SceneState.ToolManager);
}


// Operator factory

TUniquePtr<FDynamicMeshOperator> ULatticeDeformerOperatorFactory::MakeNewOperator()
{
	ELatticeInterpolation OpInterpolationType =
		(LatticeDeformerTool->Settings->InterpolationType == ELatticeInterpolationType::Cubic) ?
		ELatticeInterpolation::Cubic :
		ELatticeInterpolation::Linear;

	TUniquePtr<FLatticeDeformerOp> LatticeDeformOp = MakeUnique<FLatticeDeformerOp>(
		LatticeDeformerTool->OriginalMesh,
		LatticeDeformerTool->Lattice,
		LatticeDeformerTool->ControlPointsMechanic->GetControlPoints(),
		OpInterpolationType,
		LatticeDeformerTool->Settings->bDeformNormals);

	return LatticeDeformOp;
}


// Tool itself

FVector3i ULatticeDeformerTool::GetLatticeResolution() const
{
	return FVector3i{ Settings->XAxisResolution, Settings->YAxisResolution, Settings->ZAxisResolution };
}

void ULatticeDeformerTool::DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
	ControlPointsMechanic->DrawHUD(Canvas, RenderAPI);
}

bool ULatticeDeformerTool::CanAccept() const
{
	return Preview != nullptr && Preview->HaveValidResult();
}

void ULatticeDeformerTool::InitializeLattice(TArray<FVector3d>& OutLatticePoints, TArray<FVector2i>& OutLatticeEdges)
{
	Lattice = MakeShared<FFFDLattice, ESPMode::ThreadSafe>(GetLatticeResolution(), *OriginalMesh, Settings->Padding);

	Lattice->GenerateInitialLatticePositions(OutLatticePoints);

	// Put the lattice in world space
	UE::Geometry::FTransform3d LocalToWorld(Cast<IPrimitiveComponentBackedTarget>(Target)->GetWorldTransform());
	Algo::ForEach(OutLatticePoints, [&LocalToWorld](FVector3d& Point) {
		Point = LocalToWorld.TransformPosition(Point);
	});

	Lattice->GenerateLatticeEdges(OutLatticeEdges);
}

void ULatticeDeformerTool::Setup()
{
	UInteractiveTool::Setup();

	SetToolDisplayName(LOCTEXT("ToolName", "Lattice Deform"));
	GetToolManager()->DisplayMessage(LOCTEXT("LatticeDeformerToolMessage", 
		"Drag the lattice control points to deform the mesh"), EToolMessageLevel::UserNotification);

	OriginalMesh = MakeShared<FDynamicMesh3, ESPMode::ThreadSafe>();
	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(Cast<IMeshDescriptionProvider>(Target)->GetMeshDescription(), *OriginalMesh);

	Settings = NewObject<ULatticeDeformerToolProperties>(this, TEXT("Lattice Deformer Tool Settings"));
	Settings->RestoreProperties(this);
	AddToolPropertySource(Settings);

	// Watch for property changes
	Settings->WatchProperty(Settings->XAxisResolution, [this](int) { bShouldRebuild = true; });
	Settings->WatchProperty(Settings->YAxisResolution, [this](int) { bShouldRebuild = true; });
	Settings->WatchProperty(Settings->ZAxisResolution, [this](int) { bShouldRebuild = true; });
	Settings->WatchProperty(Settings->Padding, [this](float) { bShouldRebuild = true; });
	Settings->WatchProperty(Settings->InterpolationType, [this](ELatticeInterpolationType)
	{
		Preview->InvalidateResult();
	});
	Settings->WatchProperty(Settings->bDeformNormals, [this](bool)
	{
		Preview->InvalidateResult();
	});
	Settings->WatchProperty(Settings->GizmoCoordinateSystem, [this](EToolContextCoordinateSystem)
	{
		ControlPointsMechanic->SetCoordinateSystem(Settings->GizmoCoordinateSystem);
	});
	Settings->WatchProperty(Settings->bSetPivotMode, [this](bool)
	{
		ControlPointsMechanic->UpdateSetPivotMode(Settings->bSetPivotMode);
	});
	

	TArray<FVector3d> LatticePoints;
	TArray<FVector2i> LatticeEdges;
	InitializeLattice(LatticePoints, LatticeEdges);

	// Set up control points mechanic
	ControlPointsMechanic = NewObject<ULatticeControlPointsMechanic>(this);
	ControlPointsMechanic->Setup(this);
	ControlPointsMechanic->SetWorld(TargetWorld);
	UE::Geometry::FTransform3d LocalToWorld(Cast<IPrimitiveComponentBackedTarget>(Target)->GetWorldTransform());
	ControlPointsMechanic->Initialize(LatticePoints, LatticeEdges, LocalToWorld);

	auto OnPointsChangedLambda = [this]()
	{
		Preview->InvalidateResult();
		Settings->bCanChangeResolution = !ControlPointsMechanic->bHasChanged;
	};
	ControlPointsMechanic->OnPointsChanged.AddLambda(OnPointsChangedLambda);

	ControlPointsMechanic->SetCoordinateSystem(Settings->GizmoCoordinateSystem);
	ControlPointsMechanic->UpdateSetPivotMode(Settings->bSetPivotMode);

	StartPreview();
}

void ULatticeDeformerTool::Shutdown(EToolShutdownType ShutdownType)
{
	Settings->SaveProperties(this);
	ControlPointsMechanic->Shutdown();

	IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(Target);
	TargetComponent->SetOwnerVisibility(true);

	if (Preview)
	{
		FDynamicMeshOpResult Result = Preview->Shutdown();

		if (ShutdownType == EToolShutdownType::Accept)
		{
			GetToolManager()->BeginUndoTransaction(LOCTEXT("LatticeDeformerTool", "Lattice Deformer"));

			FDynamicMesh3* DynamicMeshResult = Result.Mesh.Get();
			check(DynamicMeshResult != nullptr);

			// The lattice and its output mesh are in world space, so get them in local space.
			// TODO: Would it make more sense to do all the lattice computation in local space?
			UE::Geometry::FTransform3d LocalToWorld(TargetComponent->GetWorldTransform());
			MeshTransforms::ApplyTransformInverse(*DynamicMeshResult, LocalToWorld);

			Cast<IMeshDescriptionCommitter>(Target)->CommitMeshDescription([DynamicMeshResult](const IMeshDescriptionCommitter::FCommitterParams& CommitParams)
			{
				FDynamicMeshToMeshDescription Converter;
				Converter.Convert(DynamicMeshResult, *CommitParams.MeshDescriptionOut);
			});

			GetToolManager()->EndUndoTransaction();
		}
	}
}


void ULatticeDeformerTool::StartPreview()
{
	ULatticeDeformerOperatorFactory* LatticeDeformOpCreator = NewObject<ULatticeDeformerOperatorFactory>();
	LatticeDeformOpCreator->LatticeDeformerTool = this;

	Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(LatticeDeformOpCreator);

	Preview->Setup(TargetWorld, LatticeDeformOpCreator);

	Preview->SetIsMeshTopologyConstant(true, EMeshRenderAttributeFlags::Positions | EMeshRenderAttributeFlags::VertexNormals);

	FComponentMaterialSet MaterialSet;
	Cast<IMaterialProvider>(Target)->GetMaterialSet(MaterialSet);
	Preview->ConfigureMaterials(MaterialSet.Materials,
								ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
	);

	// configure secondary render material
	UMaterialInterface* SelectionMaterial = ToolSetupUtil::GetSelectionMaterial(FLinearColor(0.8f, 0.75f, 0.0f), GetToolManager());
	if (SelectionMaterial != nullptr)
	{
		Preview->PreviewMesh->SetSecondaryRenderMaterial(SelectionMaterial);
	}

	Preview->PreviewMesh->SetTangentsMode(EDynamicMeshComponentTangentsMode::NoTangents);
	Preview->SetVisibility(true);
	Preview->InvalidateResult();

	Cast<IPrimitiveComponentBackedTarget>(Target)->SetOwnerVisibility(false);
}

void ULatticeDeformerTool::OnTick(float DeltaTime)
{
	if (Preview)
	{
		if (bShouldRebuild)
		{
			TArray<FVector3d> LatticePoints;
			TArray<FVector2i> LatticeEdges;
			InitializeLattice(LatticePoints, LatticeEdges);
			UE::Geometry::FTransform3d LocalToWorld(Cast<IPrimitiveComponentBackedTarget>(Target)->GetWorldTransform());
			ControlPointsMechanic->Initialize(LatticePoints, LatticeEdges, LocalToWorld);
			Preview->InvalidateResult();
			bShouldRebuild = false;
		}

		Preview->Tick(DeltaTime);
	}
}


void ULatticeDeformerTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (ControlPointsMechanic != nullptr)
	{
		ControlPointsMechanic->Render(RenderAPI);
	}
}

#undef LOCTEXT_NAMESPACE
