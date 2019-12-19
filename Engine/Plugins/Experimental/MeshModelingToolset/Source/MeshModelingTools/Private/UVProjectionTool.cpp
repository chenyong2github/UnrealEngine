// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "UVProjectionTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"

#include "DynamicMesh3.h"
#include "BaseBehaviors/MultiClickSequenceInputBehavior.h"
#include "Selection/SelectClickedAction.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "InteractiveGizmoManager.h"

#include "AssetGenerationUtil.h"

#include "BaseGizmos/GizmoComponents.h"
#include "BaseGizmos/TransformGizmo.h"


#define LOCTEXT_NAMESPACE "UUVProjectionTool"


/*
 * ToolBuilder
 */


bool UUVProjectionToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	// note most of the code is written to support working on any number > 0, but that seems maybe confusing UI-wise and is not fully tested, so I've limited it to acting on one for now
	// TODO: if enable tool working on multiple components, figure out what to do if we have multiple component targets that point to the same underlying mesh data?
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}

UInteractiveTool* UUVProjectionToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UUVProjectionTool* NewTool = NewObject<UUVProjectionTool>(SceneState.ToolManager);

	TArray<UActorComponent*> Components = ToolBuilderUtil::FindAllComponents(SceneState, CanMakeComponentTarget);
	check(Components.Num() > 0);

	TArray<TUniquePtr<FPrimitiveComponentTarget>> ComponentTargets;
	for (UActorComponent* ActorComponent : Components)
	{
		auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
		if ( MeshComponent )
		{
			ComponentTargets.Add(MakeComponentTarget(MeshComponent));
		}
	}

	NewTool->SetSelection(MoveTemp(ComponentTargets));
	NewTool->SetWorld(SceneState.World, SceneState.GizmoManager);
	NewTool->SetAssetAPI(AssetAPI);

	return NewTool;
}




/*
 * Tool
 */

UUVProjectionToolProperties::UUVProjectionToolProperties()
{
	UVProjectionMethod = EUVProjectionMethod::Cube;
	ProjectionPrimitiveScale = FVector::OneVector;
	UVScale = FVector2D::UnitVector;
	UVOffset = FVector2D::ZeroVector;
}

UUVProjectionAdvancedProperties::UUVProjectionAdvancedProperties()
{
}


UUVProjectionTool::UUVProjectionTool()
{
}

void UUVProjectionTool::SetWorld(UWorld* World, UInteractiveGizmoManager* GizmoManagerIn)
{
	this->TargetWorld = World;
	this->GizmoManager = GizmoManagerIn;
}

void UUVProjectionTool::Setup()
{
	UInteractiveTool::Setup();

	// hide input StaticMeshComponent
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(false);
	}

	BasicProperties = NewObject<UUVProjectionToolProperties>(this, TEXT("UV Projection Settings"));
	AdvancedProperties = NewObject<UUVProjectionAdvancedProperties>(this, TEXT("Advanced Settings"));

	// initialize our properties
	AddToolPropertySource(BasicProperties);
	AddToolPropertySource(AdvancedProperties);

	MaterialSettings = NewObject<UExistingMeshMaterialProperties>(this);
	MaterialSettings->Setup();
	AddToolPropertySource(MaterialSettings);

	// initialize the PreviewMesh+BackgroundCompute object
	UpdateNumPreviews();

	// set up visualizers
	ProjectionShapeVisualizer.LineColor = FLinearColor::Red;
	ProjectionShapeVisualizer.LineThickness = 2.0;

	
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}


void UUVProjectionTool::UpdateNumPreviews()
{
	int32 CurrentNumPreview = Previews.Num();
	int32 TargetNumPreview = ComponentTargets.Num();
	if (TargetNumPreview < CurrentNumPreview)
	{
		for (int32 PreviewIdx = CurrentNumPreview - 1; PreviewIdx >= TargetNumPreview; PreviewIdx--)
		{
			Previews[PreviewIdx]->Cancel();
			GizmoManager->DestroyGizmo(TransformGizmos[PreviewIdx]);
		}
		Previews.SetNum(TargetNumPreview);
		TransformGizmos.SetNum(TargetNumPreview);
		TransformProxies.SetNum(TargetNumPreview);
		OriginalDynamicMeshes.SetNum(TargetNumPreview);
	}
	else
	{
		OriginalDynamicMeshes.SetNum(TargetNumPreview);
		for (int32 PreviewIdx = CurrentNumPreview; PreviewIdx < TargetNumPreview; PreviewIdx++)
		{
			UUVProjectionOperatorFactory *OpFactory = NewObject<UUVProjectionOperatorFactory>();
			OpFactory->Tool = this;
			OpFactory->ComponentIndex = PreviewIdx;
			OriginalDynamicMeshes[PreviewIdx] = MakeShared<FDynamicMesh3>();
			FMeshDescriptionToDynamicMesh Converter;
			Converter.bPrintDebugMessages = true;
			Converter.Convert(ComponentTargets[PreviewIdx]->GetMesh(), *OriginalDynamicMeshes[PreviewIdx]);

			FVector Center, Extents;
			FBoxSphereBounds Bounds = ComponentTargets[PreviewIdx]->GetOwnerComponent()->CalcLocalBounds();
			
			FTransform LocalXF(Bounds.Origin);
			LocalXF.SetScale3D(Bounds.BoxExtent);

			UMeshOpPreviewWithBackgroundCompute* Preview = Previews.Add_GetRef(NewObject<UMeshOpPreviewWithBackgroundCompute>(OpFactory, "Preview"));
			Preview->Setup(this->TargetWorld, OpFactory);

			FComponentMaterialSet MaterialSet;
			ComponentTargets[PreviewIdx]->GetMaterialSet(MaterialSet);
			Preview->ConfigureMaterials(MaterialSet.Materials,
				ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
			);

			Preview->SetVisibility(true);

			UTransformProxy* TransformProxy = TransformProxies.Add_GetRef(NewObject<UTransformProxy>(this));
			TransformProxy->SetTransform(LocalXF * ComponentTargets[PreviewIdx]->GetWorldTransform());
			TransformProxy->OnTransformChanged.AddUObject(this, &UUVProjectionTool::TransformChanged);

			UTransformGizmo* TransformGizmo = TransformGizmos.Add_GetRef(GizmoManager->Create3AxisTransformGizmo(this));
			TransformGizmo->SetActiveTarget(TransformProxy);
		}
		check(TransformProxies.Num() == TargetNumPreview);
		check(TransformGizmos.Num() == TargetNumPreview);
	}
}


void UUVProjectionTool::Shutdown(EToolShutdownType ShutdownType)
{
	// Restore (unhide) the source meshes
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(true);
	}

	TArray<FDynamicMeshOpResult> Results;
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Results.Emplace(Preview->Shutdown());
	}
	if (ShutdownType == EToolShutdownType::Accept)
	{
		GenerateAsset(Results);
	}

	GizmoManager->DestroyAllGizmosByOwner(this);
	TransformGizmos.Empty();
}

void UUVProjectionTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}

TUniquePtr<FDynamicMeshOperator> UUVProjectionOperatorFactory::MakeNewOperator()
{
	TUniquePtr<FUVProjectionOp> Op = MakeUnique<FUVProjectionOp>();
	Op->ProjectionMethod = Tool->BasicProperties->UVProjectionMethod;

	// TODO: de-dupe this logic (it's also in Render, below)
	FTransform LocalScale = FTransform::Identity;
	LocalScale.SetScale3D(Tool->BasicProperties->ProjectionPrimitiveScale);
	Op->ProjectionTransform = LocalScale * Tool->TransformProxies[ComponentIndex]->GetTransform();
	Op->CylinderProjectToTopOrBottomAngleThreshold = Tool->BasicProperties->CylinderProjectToTopOrBottomAngleThreshold;
	Op->UVScale = (FVector2f)Tool->BasicProperties->UVScale;
	Op->UVOffset = (FVector2f)Tool->BasicProperties->UVOffset;
	Op->bWorldSpaceUVScale = Tool->BasicProperties->bWorldSpaceUVScale;

	FTransform LocalToWorld = Tool->ComponentTargets[ComponentIndex]->GetWorldTransform();
	Op->OriginalMesh = Tool->OriginalDynamicMeshes[ComponentIndex];
	
	Op->SetTransform(LocalToWorld);

	return Op;
}



void UUVProjectionTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	ProjectionShapeVisualizer.bDepthTested = false;
	ProjectionShapeVisualizer.BeginFrame(RenderAPI, CameraState);
	FTransform LocalScale = FTransform::Identity;
	LocalScale.SetScale3D(BasicProperties->ProjectionPrimitiveScale);
	for (UTransformProxy* TransformProxy : TransformProxies)
	{
		ProjectionShapeVisualizer.SetTransform(LocalScale * TransformProxy->GetTransform());

		switch (BasicProperties->UVProjectionMethod)
		{
		case EUVProjectionMethod::Cube:
			ProjectionShapeVisualizer.DrawWireBox(FBox(FVector(-1, -1, -1), FVector(1, 1, 1)));
			break;
		case EUVProjectionMethod::Cylinder:
			ProjectionShapeVisualizer.DrawWireCylinder(FVector(0, 0, -1), FVector(0, 0, 1), 1, 2, 20);
			break;
		case EUVProjectionMethod::Plane:
			ProjectionShapeVisualizer.DrawSquare(FVector(0, 0, 0), FVector(2, 0, 0), FVector(0, 2, 0));
			break;
		}
	}
	
	ProjectionShapeVisualizer.EndFrame();
}

void UUVProjectionTool::Tick(float DeltaTime)
{
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->Tick(DeltaTime);
	}
}



#if WITH_EDITOR
void UUVProjectionTool::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UpdateNumPreviews();
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}
#endif

void UUVProjectionTool::OnPropertyModified(UObject* PropertySet, UProperty* Property)
{
	// if we don't know what changed, or we know checker density changed, update checker material
	MaterialSettings->UpdateMaterials();
	for (int PreviewIdx = 0; PreviewIdx < Previews.Num(); PreviewIdx++)
	{
		UMeshOpPreviewWithBackgroundCompute* Preview = Previews[PreviewIdx];
		Preview->OverrideMaterial = MaterialSettings->GetActiveOverrideMaterial();
	}
	
	UpdateNumPreviews();
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}


void UUVProjectionTool::TransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
	// TODO: if multi-select is re-enabled, only invalidate the preview that actually needs it?
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
}


bool UUVProjectionTool::HasAccept() const
{
	return true;
}

bool UUVProjectionTool::CanAccept() const
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


void UUVProjectionTool::GenerateAsset(const TArray<FDynamicMeshOpResult>& Results)
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("UVProjectionToolTransactionName", "UV Projection Tool"));

	check(Results.Num() == ComponentTargets.Num());
	
	for (int32 ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
	{
		check(Results[ComponentIdx].Mesh.Get() != nullptr);
		ComponentTargets[ComponentIdx]->CommitMesh([&Results, &ComponentIdx, this](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
		{
			FDynamicMeshToMeshDescription Converter;
			//if (??) // TODO check if UV topology changed?  or remove all traces of this if statement (may be safe to assume it almost always changes w/ a uv projection op)
			{
				// full conversion if topology changed
				Converter.Convert(Results[ComponentIdx].Mesh.Get(), *CommitParams.MeshDescription);
			}
			//else
			//{
			//	// otherwise just copy attributes
			//	Converter.UpdateAttributes(Results[ComponentIdx]->Mesh.Get(), *MeshDescription, false, true);
			//}
		});
	}

	GetToolManager()->EndUndoTransaction();
}




#undef LOCTEXT_NAMESPACE
