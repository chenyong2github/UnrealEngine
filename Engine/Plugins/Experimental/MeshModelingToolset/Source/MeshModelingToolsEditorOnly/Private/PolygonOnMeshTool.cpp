// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "PolygonOnMeshTool.h"
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

#include "AssetGenerationUtil.h"


#define LOCTEXT_NAMESPACE "UPolygonOnMeshTool"


/*
 * ToolBuilder
 */


bool UPolygonOnMeshToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}

UInteractiveTool* UPolygonOnMeshToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UPolygonOnMeshTool* NewTool = NewObject<UPolygonOnMeshTool>(SceneState.ToolManager);

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

UPolygonOnMeshToolProperties::UPolygonOnMeshToolProperties()
{
	PolygonOperation = EEmbeddedPolygonOpMethod::CutThrough;
	PolygonScale = 10;
	// ExtrudeDistance = 10;
	bDiscardAttributes = false;
}

UPolygonOnMeshAdvancedProperties::UPolygonOnMeshAdvancedProperties()
{
}


UPolygonOnMeshTool::UPolygonOnMeshTool()
{
	EmbedPolygonOrigin = FVector::ZeroVector;
	EmbedPolygonOrientation = FQuat::Identity;
}

void UPolygonOnMeshTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UPolygonOnMeshTool::Setup()
{
	UInteractiveTool::Setup();

	// hide input StaticMeshComponent
	ComponentTarget->SetOwnerVisibility(false);

	// click to set plane behavior
	FSelectClickedAction* SetPlaneAction = new FSelectClickedAction();
	SetPlaneAction->World = this->TargetWorld;
	SetPlaneAction->OnClickedPositionFunc = [this](const FHitResult& Hit)
	{
		SetPlaneFromWorldPos(Hit.ImpactPoint, Hit.ImpactNormal);
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

	BasicProperties = NewObject<UPolygonOnMeshToolProperties>(this, TEXT("Polygon On Mesh Settings"));
	AdvancedProperties = NewObject<UPolygonOnMeshAdvancedProperties>(this, TEXT("Advanced Settings"));

	// initialize our properties
	AddToolPropertySource(BasicProperties);
	AddToolPropertySource(AdvancedProperties);


	// initialize the PreviewMesh+BackgroundCompute object
	UpdateNumPreviews();

	// set initial cut plane (also attaches gizmo/proxy)
	FVector DefaultOrigin, Extents;
	ComponentTarget->GetOwnerActor()->GetActorBounds(false, DefaultOrigin, Extents);
	SetPlaneFromWorldPos(DefaultOrigin, FVector::UpVector);
	// hook up callback so further changes trigger recut
	PlaneTransformProxy->OnTransformChanged.AddUObject(this, &UPolygonOnMeshTool::TransformChanged);


	// Convert input mesh description to dynamic mesh
	OriginalDynamicMesh = MakeShared<FDynamicMesh3>();
	FMeshDescriptionToDynamicMesh Converter;
	Converter.bPrintDebugMessages = true;
	Converter.Convert(ComponentTarget->GetMesh(), *OriginalDynamicMesh);
	// TODO: consider adding an AABB tree construction here?  tradeoff vs doing a raycast against full every time a param change happens ...

	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}


void UPolygonOnMeshTool::UpdateNumPreviews()
{
	int32 CurrentNumPreview = Previews.Num();
	int32 TargetNumPreview = 1; // currently only have single preview use case; TODO consider cases where multiple meshes are generated with separate preview objects
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
			UPolygonOnMeshOperatorFactory *OpFactory = NewObject<UPolygonOnMeshOperatorFactory>();
			OpFactory->Tool = this;
			// TODO: give the OpFactory more info about what specifically it is doing differently vs the other previews
			UMeshOpPreviewWithBackgroundCompute* Preview = Previews.Add_GetRef(NewObject<UMeshOpPreviewWithBackgroundCompute>(OpFactory, "Preview"));
			Preview->Setup(this->TargetWorld, OpFactory);
			Preview->ConfigureMaterials(
				ToolSetupUtil::GetDefaultMaterial(GetToolManager(), ComponentTarget->GetMaterial(0)),
				ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
			);
			Preview->SetVisibility(true);
		}
	}
}


void UPolygonOnMeshTool::Shutdown(EToolShutdownType ShutdownType)
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

void UPolygonOnMeshTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}

TSharedPtr<FDynamicMeshOperator> UPolygonOnMeshOperatorFactory::MakeNewOperator()
{
	TSharedPtr<FEmbedPolygonsOp> EmbedOp = MakeShared<FEmbedPolygonsOp>();
	EmbedOp->bDiscardAttributes = Tool->BasicProperties->bDiscardAttributes;
	EmbedOp->Operation = Tool->BasicProperties->PolygonOperation;
	EmbedOp->PolygonScale = Tool->BasicProperties->PolygonScale;
	// EmbedOp->ExtrudeDistance = Tool->BasicProperties->ExtrudeDistance;
	// TODO: also put the polygon info, etc, into the EmbedOp

	FTransform LocalToWorld = Tool->ComponentTarget->GetWorldTransform();
	FTransform WorldToLocal = LocalToWorld.Inverse();
	FVector LocalOrigin = WorldToLocal.TransformPosition(Tool->EmbedPolygonOrigin);
	FVector WorldNormal = Tool->EmbedPolygonOrientation.GetAxisZ();
	FVector LocalNormal = WorldToLocal.TransformVectorNoScale(WorldNormal);
	EmbedOp->LocalPlaneOrigin = LocalOrigin;
	EmbedOp->LocalPlaneNormal = LocalNormal;
	EmbedOp->OriginalMesh = Tool->OriginalDynamicMesh;
	
	EmbedOp->SetTransform(LocalToWorld);

	return EmbedOp;
}



void UPolygonOnMeshTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}

void UPolygonOnMeshTool::Tick(float DeltaTime)
{
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->Tick(DeltaTime);
	}
}



void UPolygonOnMeshTool::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UpdateNumPreviews();
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}

void UPolygonOnMeshTool::OnPropertyModified(UObject* PropertySet, UProperty* Property)
{
	UpdateNumPreviews();
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}





void UPolygonOnMeshTool::TransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
	// TODO: if multi-select is re-enabled, only invalidate the preview that actually needs it?
	EmbedPolygonOrientation = Transform.GetRotation();
	EmbedPolygonOrigin = (FVector)Transform.GetTranslation();
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}


void UPolygonOnMeshTool::SetPlaneFromWorldPos(const FVector& Position, const FVector& Normal)
{
	EmbedPolygonOrigin = Position;

	FFrame3f CutPlane(Position, Normal);
	EmbedPolygonOrientation = CutPlane.Rotation;

	PlaneTransformGizmo->SetActiveTarget(PlaneTransformProxy);
	PlaneTransformGizmo->SetNewGizmoTransform(CutPlane.ToFTransform());
}


bool UPolygonOnMeshTool::HasAccept() const
{
	return true;
}

bool UPolygonOnMeshTool::CanAccept() const
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


void UPolygonOnMeshTool::GenerateAsset(const TArray<TUniquePtr<FDynamicMeshOpResult>>& Results)
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("PolygonOnMeshToolTransactionName", "Polygon On Mesh Tool"));
	

	// first preview always replaces, and any subsequent previews can add new actors
	// TODO: options to support other choices re what should be a new actor
	check(Results.Num() > 0);
	check(Results[0]->Mesh.Get() != nullptr);
	ComponentTarget->CommitMesh([&Results](FMeshDescription* MeshDescription)
	{
		FDynamicMeshToMeshDescription Converter;
		Converter.Convert(Results[0]->Mesh.Get(), *MeshDescription);
	});

	// currently nothing generates more than the single result for this tool
	if (Results.Num() > 1)
	{
		unimplemented();
		//FSelectedOjectsChangeList NewSelection;
		//NewSelection.ModificationType = ESelectedObjectsModificationType::Replace;
		//NewSelection.Actors.Add(ComponentTarget->GetOwnerActor());

		//check(Results[1]->Mesh.Get() != nullptr);
		//// TODO: copy over material?
		//AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
		//	AssetAPI, TargetWorld,
		//	Results[1]->Mesh.Get(), Results[1]->Transform, TEXT("Additional Polygon On Mesh Result"),
		//	AssetGenerationUtil::GetDefaultAutoGeneratedAssetPath());
		//NewSelection.Actors.Add(NewActor);
		//GetToolManager()->RequestSelectionChange(NewSelection);
	}

	GetToolManager()->EndUndoTransaction();
}




#undef LOCTEXT_NAMESPACE
