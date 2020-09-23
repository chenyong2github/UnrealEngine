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
#include "DynamicMeshToMeshDescription.h"
#include "MeshTransforms.h"
#include "Algo/ForEach.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Operations/FFDLattice.h"

#define LOCTEXT_NAMESPACE "ULatticeDeformerTool"


// Tool builder

bool ULatticeDeformerToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	// TODO: Do we want the ability to handle multiple meshes with the same lattice?
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}

UInteractiveTool* ULatticeDeformerToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	ULatticeDeformerTool* NewTool = NewObject<ULatticeDeformerTool>(SceneState.ToolManager);

	UActorComponent* ActorComponent = ToolBuilderUtil::FindFirstComponent(SceneState, CanMakeComponentTarget);
	auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
	check(MeshComponent != nullptr);
	NewTool->SetSelection(MakeComponentTarget(MeshComponent));
	NewTool->SetWorld(SceneState.World);
	return NewTool;
}


// Operator factory

TUniquePtr<FDynamicMeshOperator> ULatticeDeformerOperatorFactory::MakeNewOperator()
{
	bool bUseCubicInterpolation =
		(LatticeDeformerTool->Settings->InterpolationType == ELatticeInterpolationType::Cubic);

	TUniquePtr<FLatticeDeformerOp> LatticeDeformOp = MakeUnique<FLatticeDeformerOp>(
		LatticeDeformerTool->OriginalMesh,
		LatticeDeformerTool->Lattice,
		LatticeDeformerTool->ControlPointsMechanic->GetControlPoints(),
		bUseCubicInterpolation);

	return LatticeDeformOp;
}


// Tool itself

FVector3i ULatticeDeformerTool::GetLatticeResolution() const
{
	return FVector3i{ Settings->XAxisResolution, Settings->YAxisResolution, Settings->ZAxisResolution };
}

void ULatticeDeformerTool::DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
	EViewInteractionState State = RenderAPI->GetViewInteractionState();
	bool bThisViewHasFocus = !!(State & EViewInteractionState::Focused);

	// Draw the drag rectangle if it's active
	if (bThisViewHasFocus && ControlPointsMechanic->bIsDragging)
	{		
		FVector2D Start = ControlPointsMechanic->DragStartScreenPosition;
		FVector2D Curr = ControlPointsMechanic->DragCurrentScreenPosition;

		FCanvasBoxItem BoxItem(Start / Canvas->GetDPIScale(), (Curr - Start) / Canvas->GetDPIScale());
		BoxItem.SetColor(FLinearColor::White);
		Canvas->DrawItem(BoxItem);
	}
}

bool ULatticeDeformerTool::CanAccept() const
{
	return Preview != nullptr && Preview->HaveValidResult();
}

void ULatticeDeformerTool::InitializeLattice(TArray<FVector3d>& OutLatticePoints, TArray<FVector2i>& OutLatticeEdges)
{
	Lattice = MakeShared<FFFDLattice>(GetLatticeResolution(), *OriginalMesh, Settings->Padding);

	Lattice->GenerateInitialLatticePositions(OutLatticePoints);

	// Put the lattice in world space
	FTransform3d LocalToWorld = FTransform3d(ComponentTarget->GetWorldTransform());
	Algo::ForEach(OutLatticePoints, [&LocalToWorld](FVector3d& Point) {
		Point = LocalToWorld.TransformPosition(Point);
	});

	Lattice->GenerateLatticeEdges(OutLatticeEdges);
}

void ULatticeDeformerTool::Setup()
{
	UInteractiveTool::Setup();

	GetToolManager()->DisplayMessage(LOCTEXT("LatticeDeformerToolMessage", 
		"Drag the lattice control points to deform the mesh"), EToolMessageLevel::UserNotification);

	OriginalMesh = MakeShared<FDynamicMesh3>();
	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(ComponentTarget->GetMesh(), *OriginalMesh);

	Settings = NewObject<ULatticeDeformerToolProperties>(this, TEXT("Lattice Deformer Tool Settings"));
	Settings->RestoreProperties(this);
	AddToolPropertySource(Settings);

	TArray<FVector3d> LatticePoints;
	TArray<FVector2i> LatticeEdges;
	InitializeLattice(LatticePoints, LatticeEdges);

	// Set up control points mechanic
	ControlPointsMechanic = NewObject<ULatticeControlPointsMechanic>(this);
	ControlPointsMechanic->Setup(this);
	ControlPointsMechanic->SetWorld(TargetWorld);
	ControlPointsMechanic->Initialize(LatticePoints, LatticeEdges);

	auto OnPointsChangedLambda = [this]()
	{
		Preview->InvalidateResult();
		Settings->bCanChangeResolution = !ControlPointsMechanic->bHasChanged;
	};

	ControlPointsMechanic->OnPointsChanged.AddLambda(OnPointsChangedLambda);

	StartPreview();
}

void ULatticeDeformerTool::Shutdown(EToolShutdownType ShutdownType)
{
	Settings->SaveProperties(this);
	ControlPointsMechanic->Shutdown();

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
			FTransform3d LocalToWorld = FTransform3d(ComponentTarget->GetWorldTransform());
			MeshTransforms::ApplyTransformInverse(*DynamicMeshResult, LocalToWorld);

			ComponentTarget->CommitMesh([DynamicMeshResult](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
			{
				FDynamicMeshToMeshDescription Converter;
				Converter.Convert(DynamicMeshResult, *CommitParams.MeshDescription);
			});

			GetToolManager()->EndUndoTransaction();
		}
	}

	ComponentTarget->SetOwnerVisibility(true);
}


void ULatticeDeformerTool::StartPreview()
{
	ULatticeDeformerOperatorFactory* LatticeDeformOpCreator = NewObject<ULatticeDeformerOperatorFactory>();
	LatticeDeformOpCreator->LatticeDeformerTool = this;

	Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(LatticeDeformOpCreator);

	Preview->Setup(TargetWorld, LatticeDeformOpCreator);
	Preview->PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::NoTangents);
	Preview->SetVisibility(true);
	Preview->InvalidateResult();

	ComponentTarget->SetOwnerVisibility(false);
}

void ULatticeDeformerTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	if (Preview)
	{
		bool bShouldRebuild = Property &&
			((Property->GetFName() == GET_MEMBER_NAME_CHECKED(ULatticeDeformerToolProperties, XAxisResolution)) ||
			 (Property->GetFName() == GET_MEMBER_NAME_CHECKED(ULatticeDeformerToolProperties, YAxisResolution)) ||
			 (Property->GetFName() == GET_MEMBER_NAME_CHECKED(ULatticeDeformerToolProperties, ZAxisResolution)) ||
			 (Property->GetFName() == GET_MEMBER_NAME_CHECKED(ULatticeDeformerToolProperties, Padding)));

		if (bShouldRebuild)
		{
			TArray<FVector3d> LatticePoints;
			TArray<FVector2i> LatticeEdges;
			InitializeLattice(LatticePoints, LatticeEdges);
			ControlPointsMechanic->Initialize(LatticePoints, LatticeEdges);
		}

		Preview->InvalidateResult();
	}
}


void ULatticeDeformerTool::OnTick(float DeltaTime)
{
	if (Preview)
	{
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
