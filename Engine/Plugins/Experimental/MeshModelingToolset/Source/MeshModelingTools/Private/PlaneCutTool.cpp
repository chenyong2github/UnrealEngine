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

#include "BaseGizmos/GizmoComponents.h"
#include "BaseGizmos/TransformGizmo.h"

#include "Drawing/MeshDebugDrawing.h"
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
	bShowPreview = true;
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

	// create proxy and gizmo (but don't attach yet)
	UInteractiveGizmoManager* GizmoManager = GetToolManager()->GetPairedGizmoManager();
	PlaneTransformProxy = NewObject<UTransformProxy>(this);
	PlaneTransformGizmo = GizmoManager->Create3AxisTransformGizmo(this);

	// initialize our properties
	BasicProperties = NewObject<UPlaneCutToolProperties>(this, TEXT("Plane Cut Settings"));
	AddToolPropertySource(BasicProperties);

	// initialize the PreviewMesh+BackgroundCompute object
	UpdateNumPreviews();

	// set initial cut plane (also attaches gizmo/proxy)
	FVector DefaultOrigin, Extents;
	ComponentTarget->GetOwnerActor()->GetActorBounds(false, DefaultOrigin, Extents);
	SetCutPlaneFromWorldPos(DefaultOrigin, FVector::UpVector);
	// hook up callback so further changes trigger recut
	PlaneTransformProxy->OnTransformChanged.AddUObject(this, &UPlaneCutTool::TransformChanged);



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
	GizmoManager->DestroyAllGizmosByOwner(this);
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
	CutOp->bFillSpans = CutTool->BasicProperties->bFillSpans;

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
	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	FColor GridColor(128, 128, 128, 32);
	float GridThickness = 0.5f;
	float GridLineSpacing = 25.0f;   // @todo should be relative to view
	int NumGridLines = 10;
	
	FFrame3f DrawFrame(CutPlaneOrigin, CutPlaneOrientation);
	MeshDebugDraw::DrawSimpleGrid(DrawFrame, NumGridLines, GridLineSpacing, GridThickness, GridColor, false, PDI, FTransform::Identity);
}

void UPlaneCutTool::Tick(float DeltaTime)
{
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->Tick(DeltaTime);
	}
}


#if WITH_EDITOR
void UPlaneCutTool::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UpdateNumPreviews();
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}
#endif

void UPlaneCutTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UPlaneCutToolProperties, bShowPreview)))
	{
		ComponentTarget->SetOwnerVisibility(!BasicProperties->bShowPreview);
		for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
		{
			Preview->SetVisibility(BasicProperties->bShowPreview);
		}
	}

	UpdateNumPreviews();
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}


void UPlaneCutTool::TransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
	// TODO: if multi-select is re-enabled, only invalidate the preview that actually needs it?
	CutPlaneOrientation = Transform.GetRotation();
	CutPlaneOrigin = (FVector)Transform.GetTranslation();
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

	PlaneTransformGizmo->SetActiveTarget(PlaneTransformProxy);
	PlaneTransformGizmo->SetNewGizmoTransform(CutPlane.ToFTransform());
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
	if (Results.Num() == 0 
		|| Results[0]->Mesh.IsValid() == false
		|| Results[0]->Mesh->TriangleCount() == 0 )
	{
		return;
	}

	GetToolManager()->BeginUndoTransaction(LOCTEXT("PlaneCutToolTransactionName", "Plane Cut Tool"));
	

	// currently in-place replaces the first half, and adds a new actor for the second half (if it was generated)
	// TODO: options to support other choices re what should be a new actor
	ComponentTarget->CommitMesh([&Results](FMeshDescription* MeshDescription)
	{
		FDynamicMeshToMeshDescription Converter;
		Converter.Convert(Results[0]->Mesh.Get(), *MeshDescription);
	});


	// The method for creating a new mesh (AssetGenerationUtil::GenerateStaticMeshActor) is editor-only; just creating the other half if not in editor
#if WITH_EDITOR
	if (Results.Num() == 2 && Results[1]->Mesh.IsValid() && Results[1]->Mesh->TriangleCount() > 0)
	{
		FSelectedOjectsChangeList NewSelection;
		NewSelection.ModificationType = ESelectedObjectsModificationType::Replace;
		NewSelection.Actors.Add(ComponentTarget->GetOwnerActor());

		TArray<UMaterialInterface*> Materials;
		for (int MaterialIdx = 0, NumMaterials = ComponentTarget->GetNumMaterials(); MaterialIdx < NumMaterials; MaterialIdx++)
		{
			Materials.Add(ComponentTarget->GetMaterial(MaterialIdx));
		}
		
		AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
			AssetAPI, TargetWorld,
			Results[1]->Mesh.Get(), Results[1]->Transform, TEXT("PlaneCutOtherHalf"),
			AssetGenerationUtil::GetDefaultAutoGeneratedAssetPath(), Materials);
		NewSelection.Actors.Add(NewActor);
		GetToolManager()->RequestSelectionChange(NewSelection);
	}
#endif

	GetToolManager()->EndUndoTransaction();
}




#undef LOCTEXT_NAMESPACE
