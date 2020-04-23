// Copyright Epic Games, Inc. All Rights Reserved.

#include "CSGMeshesTool.h"
#include "CompositionOps/BooleanMeshesOp.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"

#include "Selection/ToolSelectionUtil.h"

#include "DynamicMesh3.h"
#include "MeshTransforms.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "InteractiveGizmoManager.h"

#include "BaseGizmos/GizmoComponents.h"
#include "BaseGizmos/TransformGizmo.h"

#include "AssetGenerationUtil.h"


#define LOCTEXT_NAMESPACE "UCSGMeshesTool"


/*
 * ToolBuilder
 */


bool UCSGMeshesToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return AssetAPI != nullptr && ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 2;
}

UInteractiveTool* UCSGMeshesToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UCSGMeshesTool* NewTool = NewObject<UCSGMeshesTool>(SceneState.ToolManager);

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
	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);

	return NewTool;
}



/*
 * Tool
 */
UCSGMeshesTool::UCSGMeshesTool()
{
}

void UCSGMeshesTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UCSGMeshesTool::Setup()
{
	UInteractiveTool::Setup();

	// hide input StaticMeshComponents
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(false);
	}


	// initialize our properties
	CSGProperties = NewObject<UCSGMeshesToolProperties>(this);
	CSGProperties->RestoreProperties(this);
	AddToolPropertySource(CSGProperties);
	HandleSourcesProperties = NewObject<UOnAcceptHandleSourcesProperties>(this);
	HandleSourcesProperties->RestoreProperties(this);
	AddToolPropertySource(HandleSourcesProperties);

	// initialize the PreviewMesh+BackgroundCompute object
	SetupPreview();

	Preview->InvalidateResult();
}




void UCSGMeshesTool::SetupPreview()
{
	FComponentMaterialSet AllMaterialSet;
	TMap<UMaterialInterface*, int> KnownMaterials;
	TArray<TArray<int>> MaterialRemap; MaterialRemap.SetNum(ComponentTargets.Num());
	for (int ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
	{
		FComponentMaterialSet ComponentMaterialSet;
		ComponentTargets[ComponentIdx]->GetMaterialSet(ComponentMaterialSet);
		for (UMaterialInterface* Mat : ComponentMaterialSet.Materials)
		{
			int* FoundMatIdx = KnownMaterials.Find(Mat);
			int MatIdx;
			if (FoundMatIdx)
			{
				MatIdx = *FoundMatIdx;
			}
			else
			{
				MatIdx = AllMaterialSet.Materials.Add(Mat);
				KnownMaterials.Add(Mat, MatIdx);
			}
			MaterialRemap[ComponentIdx].Add(MatIdx);
		}
	}

	OriginalDynamicMeshes.SetNum(ComponentTargets.Num());

	for (int ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
	{
		OriginalDynamicMeshes[ComponentIdx] = MakeShared<FDynamicMesh3>();
		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(ComponentTargets[ComponentIdx]->GetMesh(), *OriginalDynamicMeshes[ComponentIdx]);

		// ensure materials and attributes are always enabled
		OriginalDynamicMeshes[ComponentIdx]->EnableAttributes();
		OriginalDynamicMeshes[ComponentIdx]->Attributes()->EnableMaterialID();
		FDynamicMeshMaterialAttribute* MaterialIDs = OriginalDynamicMeshes[ComponentIdx]->Attributes()->GetMaterialID();
		for (int TID : OriginalDynamicMeshes[ComponentIdx]->TriangleIndicesItr())
		{
			MaterialIDs->SetValue(TID, MaterialRemap[ComponentIdx][MaterialIDs->GetValue(TID)]);
		}
	}

	Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(this, "Preview");
	Preview->Setup(this->TargetWorld, this);
	Preview->ConfigureMaterials(AllMaterialSet.Materials, ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager()));

	
	DrawnLineSet = NewObject<ULineSetComponent>(Preview->PreviewMesh->GetRootComponent());
	DrawnLineSet->SetupAttachment(Preview->PreviewMesh->GetRootComponent());
	DrawnLineSet->SetLineMaterial(ToolSetupUtil::GetDefaultLineComponentMaterial(GetToolManager()));
	DrawnLineSet->RegisterComponent();

	Preview->OnOpCompleted.AddLambda(
		[this](const FDynamicMeshOperator* Op)
		{
			const FBooleanMeshesOp* BooleanOp = (const FBooleanMeshesOp*)(Op);
			CreatedBoundaryEdges = BooleanOp->GetCreatedBoundaryEdges();
		}
	);
	Preview->OnMeshUpdated.AddLambda(
		[this](const UMeshOpPreviewWithBackgroundCompute*)
		{
			GetToolManager()->PostInvalidation();
			UpdateVisualization();
		}
	);
	SetTransformGizmos();
}

void UCSGMeshesTool::UpdateVisualization()
{
	FColor BoundaryEdgeColor(240, 15, 15);
	float BoundaryEdgeThickness = 2.0;
	float BoundaryEdgeDepthBias = 2.0f;

	const FDynamicMesh3* TargetMesh = Preview->PreviewMesh->GetPreviewDynamicMesh();
	FVector3d A, B;

	DrawnLineSet->Clear();
	if (CSGProperties->bShowNewBoundaryEdges)
	{
		for (int EID : CreatedBoundaryEdges)
		{
			TargetMesh->GetEdgeV(EID, A, B);
			DrawnLineSet->AddLine((FVector)A, (FVector)B, BoundaryEdgeColor, BoundaryEdgeThickness, BoundaryEdgeDepthBias);
		}
	}
}

void UCSGMeshesTool::UpdateGizmoVisibility()
{
	for (UTransformGizmo* Gizmo : TransformGizmos)
	{
		Gizmo->SetVisibility(CSGProperties->bShowTransformUI);
	}
}

void UCSGMeshesTool::SetTransformGizmos()
{
	UInteractiveGizmoManager* GizmoManager = GetToolManager()->GetPairedGizmoManager();

	for (int ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
	{
		TUniquePtr<FPrimitiveComponentTarget>& Target = ComponentTargets[ComponentIdx];
		UTransformProxy* Proxy = TransformProxies.Add_GetRef(NewObject<UTransformProxy>(this));
		UTransformGizmo* Gizmo = TransformGizmos.Add_GetRef(GizmoManager->Create3AxisTransformGizmo(this));
		Gizmo->SetActiveTarget(Proxy);
		FTransform InitialTransform = Target->GetWorldTransform();
		Gizmo->SetNewGizmoTransform(InitialTransform);
		Proxy->OnTransformChanged.AddUObject(this, &UCSGMeshesTool::TransformChanged);
	}
	UpdateGizmoVisibility();
}

void UCSGMeshesTool::TransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
	Preview->InvalidateResult();
}

void UCSGMeshesTool::Shutdown(EToolShutdownType ShutdownType)
{
	CSGProperties->SaveProperties(this);
	HandleSourcesProperties->SaveProperties(this);

	FDynamicMeshOpResult Result = Preview->Shutdown();
	// Restore (unhide) the source meshes
	for (auto& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(true);
	}
	if (ShutdownType == EToolShutdownType::Accept)
	{
		// Generate the result
		{
			GetToolManager()->BeginUndoTransaction(LOCTEXT("BooleanMeshes", "Boolean Meshes"));

			GenerateAsset(Result);

			GetToolManager()->EndUndoTransaction();
		}

		TArray<AActor*> Actors;
		for (auto& ComponentTarget : ComponentTargets)
		{
			Actors.Add(ComponentTarget->GetOwnerActor());
		}
		HandleSourcesProperties->ApplyMethod(Actors, GetToolManager());
	}

	UInteractiveGizmoManager* GizmoManager = GetToolManager()->GetPairedGizmoManager();
	GizmoManager->DestroyAllGizmosByOwner(this);
}

void UCSGMeshesTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}

TUniquePtr<FDynamicMeshOperator> UCSGMeshesTool::MakeNewOperator()
{
	TUniquePtr<FBooleanMeshesOp> BooleanOp = MakeUnique<FBooleanMeshesOp>();
	
	BooleanOp->Operation = CSGProperties->Operation;
	BooleanOp->bAttemptFixHoles = CSGProperties->bAttemptFixHoles;

	check(OriginalDynamicMeshes.Num() == 2);
	check(ComponentTargets.Num() == 2);
	BooleanOp->Transforms.SetNum(2);
	BooleanOp->Meshes.SetNum(2);
	for (int Idx = 0; Idx < 2; Idx++)
	{
		BooleanOp->Meshes[Idx] = OriginalDynamicMeshes[Idx];
		BooleanOp->Transforms[Idx] = TransformProxies[Idx]->GetTransform();
	}

	return BooleanOp;
}



void UCSGMeshesTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}

void UCSGMeshesTool::OnTick(float DeltaTime)
{
	for (UTransformGizmo* Gizmo : TransformGizmos)
	{
		Gizmo->bSnapToWorldGrid = CSGProperties->bSnapToWorldGrid;
	}

	Preview->Tick(DeltaTime);
}


#if WITH_EDITOR
void UCSGMeshesTool::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Preview->InvalidateResult();
	UpdateGizmoVisibility();
}
#endif

void UCSGMeshesTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UCSGMeshesToolProperties, bShowTransformUI)))
	{
		UpdateGizmoVisibility();
	}
	else if (Property && 
		(  PropertySet == HandleSourcesProperties
		|| Property->GetFName() == GET_MEMBER_NAME_CHECKED(UCSGMeshesToolProperties, bSnapToWorldGrid)
		))
	{
		// nothing
	}
	else if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UCSGMeshesToolProperties, bShowNewBoundaryEdges)))
	{
		GetToolManager()->PostInvalidation();
		UpdateVisualization();
	}
	else
	{
		Preview->InvalidateResult();
	}
}


bool UCSGMeshesTool::HasAccept() const
{
	return true;
}

bool UCSGMeshesTool::CanAccept() const
{
	return Preview->HaveValidResult();
}


void UCSGMeshesTool::GenerateAsset(const FDynamicMeshOpResult& Result)
{
	check(Result.Mesh.Get() != nullptr);

	FVector3d Center = Result.Mesh->GetCachedBounds().Center();
	MeshTransforms::Translate(*Result.Mesh, -Center);
	FTransform3d CenteredTransform = Result.Transform;
	CenteredTransform.SetTranslation(CenteredTransform.GetTranslation() + Result.Transform.TransformVector(Center));
	AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
		AssetAPI, TargetWorld,
		Result.Mesh.Get(), CenteredTransform, TEXT("CSGMesh"), Preview->StandardMaterials);
	if (NewActor != nullptr)
	{
		ToolSelectionUtil::SetNewActorSelection(GetToolManager(), NewActor);
	}
}




#undef LOCTEXT_NAMESPACE
