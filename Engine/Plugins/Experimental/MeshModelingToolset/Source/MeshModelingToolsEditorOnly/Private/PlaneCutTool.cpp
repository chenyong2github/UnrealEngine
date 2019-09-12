// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PlaneCutTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"

#include "DynamicMesh3.h"
#include "BaseBehaviors/MultiClickSequenceInputBehavior.h"
#include "Selection/SelectClickedAction.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "InteractiveGizmoManager.h"

#include "PositionPlaneGizmo.h"
#include "AssetGenerationUtil.h"

#include "CuttingOps/PlaneCutOp.h"

#define LOCTEXT_NAMESPACE "UPlaneCutTool"


/*
 * ToolBuilder
 */


bool UPlaneCutToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}

UInteractiveTool* UPlaneCutToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UPlaneCutTool* NewTool = NewObject<UPlaneCutTool>(SceneState.ToolManager);

	UActorComponent* ActorComponent = ToolBuilderUtil::FindFirstComponent(SceneState, CanMakeComponentTarget);
	auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
	check(MeshComponent != nullptr);
	NewTool->SetSelection(MakeComponentTarget(MeshComponent));

	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);
	return NewTool;
}



/*
 * Tool
 */

UPlaneCutToolProperties::UPlaneCutToolProperties()
{
	bDiscardAttributes = false;
	bKeepBothHalves = false;
	bFillCutHole = true;
	SpacingBetweenHalves = 1;
}

UPlaneCutAdvancedProperties::UPlaneCutAdvancedProperties()
{
	bFillSpans = false;
}


UPlaneCutTool::UPlaneCutTool()
{
	CutPlaneOrigin = FVector::ZeroVector;
	CutPlaneOrientation = FQuat::Identity;
}

void UPlaneCutTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UPlaneCutTool::Setup()
{
	UInteractiveTool::Setup();

	// hide input StaticMeshComponent
	ComponentTarget->SetOwnerVisibility(false);

	// click to set plane behavior
	FSelectClickedAction* SetPlaneAction = new FSelectClickedAction();
	SetPlaneAction->World = this->TargetWorld;
	SetPlaneAction->OnClickedPositionFunc = [this](const FHitResult& Hit)
	{
		SetCutPlaneFromWorldPos(Hit.ImpactPoint, Hit.ImpactNormal);
		for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
		{
			Preview->InvalidateResult();
		}
	};
	SetPointInWorldConnector = SetPlaneAction;

	USingleClickInputBehavior* ClickToSetPlaneBehavior = NewObject<USingleClickInputBehavior>();
	ClickToSetPlaneBehavior->ModifierCheckFunc = FInputDeviceState::IsCtrlKeyDown;
	ClickToSetPlaneBehavior->Initialize(SetPointInWorldConnector);
	AddInputBehavior(ClickToSetPlaneBehavior);


	UPositionPlaneGizmoBuilder* PositionPlaneGizmoBuilder = NewObject<UPositionPlaneGizmoBuilder>();
	UInteractiveGizmoManager* GizmoManager = GetToolManager()->GetPairedGizmoManager();
	GizmoManager->RegisterGizmoType(TEXT("CutPlaneGizmo"), PositionPlaneGizmoBuilder);
	PositionPlaneGizmo = GizmoManager->CreateGizmo(TEXT("CutPlaneGizmo"), TEXT("TestGizmo2"));
	Cast<UPositionPlaneGizmo>(PositionPlaneGizmo)->OnPositionUpdatedFunc = [this](const FFrame3d& WorldFrame)
	{
		UpdateCutPlaneFromGizmo(WorldFrame);
	};

	BasicProperties = NewObject<UPlaneCutToolProperties>(this, TEXT("Plane Cut Settings"));
	AdvancedProperties = NewObject<UPlaneCutAdvancedProperties>(this, TEXT("Advanced Settings"));

	// initialize our properties
	AddToolPropertySource(BasicProperties);
	AddToolPropertySource(AdvancedProperties);


	// initialize the PreviewMesh+BackgroundCompute object
	UpdateNumPreviews();

	FVector DefaultOrigin, Extents;
	ComponentTarget->GetOwnerActor()->GetActorBounds(false, DefaultOrigin, Extents);
	SetCutPlaneFromWorldPos(DefaultOrigin, FVector::UpVector);



	// Convert input mesh description to dynamic mesh
	OriginalDynamicMesh = MakeShared<FDynamicMesh3>();
	FMeshDescriptionToDynamicMesh Converter;
	Converter.bPrintDebugMessages = true;
	Converter.Convert(ComponentTarget->GetMesh(), *OriginalDynamicMesh);

	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}


void UPlaneCutTool::UpdateNumPreviews()
{
	int32 CurrentNumPreview = Previews.Num();
	int32 TargetNumPreview = BasicProperties->bKeepBothHalves ? 2 : 1;
	if (TargetNumPreview < CurrentNumPreview)
	{
		for (int32 PreviewIdx = CurrentNumPreview - 1; PreviewIdx >= TargetNumPreview; PreviewIdx--)
		{
			Previews[PreviewIdx]->Cancel();
		}
		Previews.SetNum(TargetNumPreview);
	}
	else
	{
		for (int32 PreviewIdx = CurrentNumPreview; PreviewIdx < TargetNumPreview; PreviewIdx++)
		{
			UPlaneCutOperatorFactory *CutSide = NewObject<UPlaneCutOperatorFactory>();
			CutSide->CutTool = this;
			CutSide->bCutBackSide = PreviewIdx == 1;
			UMeshOpPreviewWithBackgroundCompute* Preview = Previews.Add_GetRef(NewObject<UMeshOpPreviewWithBackgroundCompute>(CutSide, "Preview"));
			Preview->Setup(this->TargetWorld, CutSide);
			Preview->ConfigureMaterials(
				ToolSetupUtil::GetDefaultMaterial(GetToolManager(), ComponentTarget->GetMaterial(0)),
				ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
			);
			Preview->SetVisibility(true);
		}
	}
}


void UPlaneCutTool::Shutdown(EToolShutdownType ShutdownType)
{
	// Restore (unhide) the source meshes
	ComponentTarget->SetOwnerVisibility(true);

	TArray<TUniquePtr<FDynamicMeshOpResult>> Results;
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Results.Emplace(Preview->Shutdown());
	}
	if (ShutdownType == EToolShutdownType::Accept)
	{
		GenerateAsset(Results);
	}

	if (SetPointInWorldConnector != nullptr)
	{
		delete SetPointInWorldConnector;
	}
	UInteractiveGizmoManager* GizmoManager = GetToolManager()->GetPairedGizmoManager();
	GizmoManager->DestroyGizmo(PositionPlaneGizmo);
	PositionPlaneGizmo = nullptr;
	GizmoManager->DeregisterGizmoType(TEXT("CutPlaneGizmo"));
}

void UPlaneCutTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}

TSharedPtr<FDynamicMeshOperator> UPlaneCutOperatorFactory::MakeNewOperator()
{
	TSharedPtr<FPlaneCutOp> CutOp = MakeShared<FPlaneCutOp>();
	CutOp->bDiscardAttributes = CutTool->BasicProperties->bDiscardAttributes;
	CutOp->bFillCutHole = CutTool->BasicProperties->bFillCutHole;
	CutOp->bFillSpans = CutTool->AdvancedProperties->bFillSpans;

	FTransform LocalToWorld = CutTool->ComponentTarget->GetWorldTransform();
	FTransform WorldToLocal = LocalToWorld.Inverse();
	FVector LocalOrigin = WorldToLocal.TransformPosition(CutTool->CutPlaneOrigin);
	FVector WorldNormal = CutTool->CutPlaneOrientation.GetAxisZ();
	FVector LocalNormal = WorldToLocal.TransformVectorNoScale(WorldNormal);
	if (bCutBackSide)
	{
		LocalNormal = -LocalNormal;
	}
	CutOp->LocalPlaneOrigin = LocalOrigin;
	CutOp->LocalPlaneNormal = LocalNormal;
	CutOp->OriginalMesh = CutTool->OriginalDynamicMesh;
	
	if (bCutBackSide)
	{
		LocalToWorld *= FTransform(CutTool->BasicProperties->SpacingBetweenHalves * WorldNormal);
	}
	CutOp->SetTransform(LocalToWorld);

	return CutOp;
}



void UPlaneCutTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}

void UPlaneCutTool::Tick(float DeltaTime)
{
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->Tick(DeltaTime);
	}
}



void UPlaneCutTool::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UpdateNumPreviews();
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}

void UPlaneCutTool::OnPropertyModified(UObject* PropertySet, UProperty* Property)
{
	UpdateNumPreviews();
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}




void UPlaneCutTool::SetCutPlaneFromWorldPos(const FVector& Position, const FVector& Normal)
{
	CutPlaneOrigin = Position;

	FFrame3f CutPlane(Position, Normal);
	CutPlaneOrientation = CutPlane.Rotation;

	UPositionPlaneGizmo* Gizmo = Cast<UPositionPlaneGizmo>(PositionPlaneGizmo);
	Gizmo->ExternalUpdatePosition(CutPlaneOrigin, CutPlaneOrientation, false);
}


void UPlaneCutTool::UpdateCutPlaneFromGizmo(const FFrame3d& WorldPosition)
{
	CutPlaneOrientation = WorldPosition.Rotation;
	CutPlaneOrigin = (FVector)WorldPosition.Origin;
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}


bool UPlaneCutTool::HasAccept() const
{
	return true;
}

bool UPlaneCutTool::CanAccept() const
{
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		if (!Preview->HaveValidResult())
		{
			return false;
		}
	}
	return true;
}


void UPlaneCutTool::GenerateAsset(const TArray<TUniquePtr<FDynamicMeshOpResult>>& Results)
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("PlaneCutToolTransactionName", "Plane Cut Tool"));
	

	// currently in-place replaces the first half, and adds a new actor for the second half (if it was generated)
	// TODO: options to support other choices re what should be a new actor
	check(Results.Num() > 0);
	check(Results[0]->Mesh.Get() != nullptr);
	ComponentTarget->CommitMesh([&Results](FMeshDescription* MeshDescription)
	{
		FDynamicMeshToMeshDescription Converter;
		Converter.Convert(Results[0]->Mesh.Get(), *MeshDescription);
	});

	if (Results.Num() == 2)
	{
		FSelectedOjectsChangeList NewSelection;
		NewSelection.ModificationType = ESelectedObjectsModificationType::Replace;
		NewSelection.Actors.Add(ComponentTarget->GetOwnerActor());

		check(Results[1]->Mesh.Get() != nullptr);
		// TODO: copy over material?
		AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
			AssetAPI, TargetWorld,
			Results[1]->Mesh.Get(), Results[1]->Transform, TEXT("Plane Cut Other Half"),
			AssetGenerationUtil::GetDefaultAutoGeneratedAssetPath());
		NewSelection.Actors.Add(NewActor);
		GetToolManager()->RequestSelectionChange(NewSelection);
	}

	GetToolManager()->EndUndoTransaction();
}




#undef LOCTEXT_NAMESPACE
