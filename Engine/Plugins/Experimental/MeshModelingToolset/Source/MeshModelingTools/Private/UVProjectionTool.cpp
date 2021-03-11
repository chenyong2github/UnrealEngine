// Copyright Epic Games, Inc. All Rights Reserved.

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

#include "TargetInterfaces/MaterialProvider.h"
#include "TargetInterfaces/MeshDescriptionCommitter.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "ToolTargetManager.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UUVProjectionTool"

/*
 * ToolBuilder
 */


const FToolTargetTypeRequirements& UUVProjectionToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements({
		UMaterialProvider::StaticClass(),
		UMeshDescriptionCommitter::StaticClass(),
		UMeshDescriptionProvider::StaticClass(),
		UPrimitiveComponentBackedTarget::StaticClass()
		});
	return TypeRequirements;
}

bool UUVProjectionToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	// note most of the code is written to support working on any number > 0, but that seems maybe confusing UI-wise and is not fully tested, so I've limited it to acting on one for now
	// TODO: if enable tool working on multiple components, figure out what to do if we have multiple component targets that point to the same underlying mesh data?
	return SceneState.TargetManager->CountSelectedAndTargetable(SceneState, GetTargetRequirements()) == 1;
}

UInteractiveTool* UUVProjectionToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UUVProjectionTool* NewTool = NewObject<UUVProjectionTool>(SceneState.ToolManager);

	TArray<TObjectPtr<UToolTarget>> Targets = SceneState.TargetManager->BuildAllSelectedTargetable(SceneState, GetTargetRequirements());
	NewTool->SetTargets(MoveTemp(Targets));
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
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		TargetComponentInterface(ComponentIdx)->SetOwnerVisibility(false);
	}

	BasicProperties = NewObject<UUVProjectionToolProperties>(this, TEXT("UV Projection Settings"));
	BasicProperties->RestoreProperties(this);
	AdvancedProperties = NewObject<UUVProjectionAdvancedProperties>(this, TEXT("Advanced Settings"));

	// initialize our properties
	AddToolPropertySource(BasicProperties);
	AddToolPropertySource(AdvancedProperties);

	MaterialSettings = NewObject<UExistingMeshMaterialProperties>(this);
	MaterialSettings->RestoreProperties(this);

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
	UpdateVisualization();

	SetToolDisplayName(LOCTEXT("ToolName", "UV Projection"));
	GetToolManager()->DisplayMessage(
		LOCTEXT("UVProjectionToolDescription", "Generate UVs for a Mesh by projecting onto simple geometric shapes."),
		EToolMessageLevel::UserNotification);
}


void UUVProjectionTool::UpdateNumPreviews()
{
	int32 CurrentNumPreview = Previews.Num();
	int32 TargetNumPreview = Targets.Num();
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
			OriginalDynamicMeshes[PreviewIdx] = MakeShared<FDynamicMesh3, ESPMode::ThreadSafe>();
			FMeshDescriptionToDynamicMesh Converter;
			Converter.Convert(TargetMeshProviderInterface(PreviewIdx)->GetMeshDescription(), *OriginalDynamicMeshes[PreviewIdx]);

			FVector Center, Extents;
			FBoxSphereBounds Bounds = TargetComponentInterface(PreviewIdx)->GetOwnerComponent()->CalcLocalBounds();
			
			FTransform LocalXF(Bounds.Origin);
			LocalXF.SetScale3D(Bounds.BoxExtent);

			UMeshOpPreviewWithBackgroundCompute* Preview = Previews.Add_GetRef(NewObject<UMeshOpPreviewWithBackgroundCompute>(OpFactory, "Preview"));
			Preview->Setup(this->TargetWorld, OpFactory);
			Preview->PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::AutoCalculated);

			FComponentMaterialSet MaterialSet;
			TargetMaterialInterface(PreviewIdx)->GetMaterialSet(MaterialSet);
			Preview->ConfigureMaterials(MaterialSet.Materials,
				ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
			);
			Preview->PreviewMesh->UpdatePreview(OriginalDynamicMeshes[PreviewIdx].Get());
			Preview->PreviewMesh->SetTransform(TargetComponentInterface(PreviewIdx)->GetWorldTransform());

			Preview->SetVisibility(true);

			UTransformProxy* TransformProxy = TransformProxies.Add_GetRef(NewObject<UTransformProxy>(this));
			TransformProxy->SetTransform(LocalXF * TargetComponentInterface(PreviewIdx)->GetWorldTransform());
			TransformProxy->OnTransformChanged.AddUObject(this, &UUVProjectionTool::TransformChanged);

			UTransformGizmo* TransformGizmo = TransformGizmos.Add_GetRef(GizmoManager->Create3AxisTransformGizmo(this));
			TransformGizmo->SetActiveTarget(TransformProxy, GetToolManager());
		}
		check(TransformProxies.Num() == TargetNumPreview);
		check(TransformGizmos.Num() == TargetNumPreview);
	}
}


void UUVProjectionTool::Shutdown(EToolShutdownType ShutdownType)
{
	BasicProperties->SaveProperties(this);
	MaterialSettings->SaveProperties(this);

	// Restore (unhide) the source meshes
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		TargetComponentInterface(ComponentIdx)->SetOwnerVisibility(true);
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

void UUVProjectionTool::SetAssetAPI(IAssetGenerationAPI* AssetAPIIn)
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

	FTransform LocalToWorld = Tool->TargetComponentInterface(ComponentIndex)->GetWorldTransform();
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

void UUVProjectionTool::OnTick(float DeltaTime)
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

void UUVProjectionTool::UpdateVisualization()
{
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

void UUVProjectionTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	UpdateVisualization();
}


void UUVProjectionTool::TransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
	// TODO: if multi-select is re-enabled, only invalidate the preview that actually needs it?
	for (UMeshOpPreviewWithBackgroundCompute* Preview : Previews)
	{
		Preview->InvalidateResult();
	}
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
	return Super::CanAccept();
}


void UUVProjectionTool::GenerateAsset(const TArray<FDynamicMeshOpResult>& Results)
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("UVProjectionToolTransactionName", "UV Projection Tool"));

	check(Results.Num() == Targets.Num());
	
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		check(Results[ComponentIdx].Mesh.Get() != nullptr);
		TargetMeshCommitterInterface(ComponentIdx)->CommitMeshDescription([&Results, &ComponentIdx, this](const IMeshDescriptionCommitter::FCommitterParams& CommitParams)
		{
			FDynamicMesh3* DynamicMesh = Results[ComponentIdx].Mesh.Get();
			FMeshDescription* MeshDescription = CommitParams.MeshDescriptionOut;

			bool bVerticesOnly = false;
			bool bAttributesOnly = true;
			if (FDynamicMeshToMeshDescription::HaveMatchingElementCounts(DynamicMesh, MeshDescription, bVerticesOnly, bAttributesOnly))
			{
				FDynamicMeshToMeshDescription Converter;
				Converter.UpdateAttributes(DynamicMesh, *MeshDescription, false, false, true/*update uvs*/);
			}
			else
			{
				// must have been duplicate tris in the mesh description; we can't count on 1-to-1 mapping of TriangleIDs.  Just convert 
				FDynamicMeshToMeshDescription Converter;
				Converter.Convert(DynamicMesh, *MeshDescription);
			}
		});
	}

	GetToolManager()->EndUndoTransaction();
}




#undef LOCTEXT_NAMESPACE
