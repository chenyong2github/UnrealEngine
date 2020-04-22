// Copyright Epic Games, Inc. All Rights Reserved.

#include "SelfUnionMeshesTool.h"
#include "CompositionOps/SelfUnionMeshesOp.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "ToolSetupUtil.h"

#include "Selection/ToolSelectionUtil.h"

#include "DynamicMesh3.h"
#include "DynamicMeshEditor.h"
#include "MeshTransforms.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "AssetGenerationUtil.h"


#define LOCTEXT_NAMESPACE "USelfUnionMeshesTool"


/*
 * ToolBuilder
 */


bool USelfUnionMeshesToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return AssetAPI != nullptr && ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) > 0;
}

UInteractiveTool* USelfUnionMeshesToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	USelfUnionMeshesTool* NewTool = NewObject<USelfUnionMeshesTool>(SceneState.ToolManager);

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
USelfUnionMeshesTool::USelfUnionMeshesTool()
{
}

void USelfUnionMeshesTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void USelfUnionMeshesTool::Setup()
{
	UInteractiveTool::Setup();

	// hide input StaticMeshComponents
	for (TUniquePtr<FPrimitiveComponentTarget>& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(false);
	}


	// initialize our properties
	Properties = NewObject<USelfUnionMeshesToolProperties>(this);
	Properties->RestoreProperties(this);
	AddToolPropertySource(Properties);
	HandleSourcesProperties = NewObject<UOnAcceptHandleSourcesProperties>(this);
	HandleSourcesProperties->RestoreProperties(this);
	AddToolPropertySource(HandleSourcesProperties);


	// initialize the PreviewMesh+BackgroundCompute object
	SetupPreview();

	Preview->InvalidateResult();
}




void USelfUnionMeshesTool::SetupPreview()
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

	CombinedSourceMeshes = MakeShared<FDynamicMesh3>();
	CombinedSourceMeshes->EnableAttributes();
	CombinedSourceMeshes->Attributes()->EnableMaterialID();
	FDynamicMeshEditor AppendEditor(CombinedSourceMeshes.Get());

	for (int ComponentIdx = 0; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
	{
		FDynamicMesh3 ComponentMesh;
		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(ComponentTargets[ComponentIdx]->GetMesh(), ComponentMesh);

		// ensure materials and attributes are always enabled
		ComponentMesh.EnableAttributes();
		ComponentMesh.Attributes()->EnableMaterialID();
		FDynamicMeshMaterialAttribute* MaterialIDs = ComponentMesh.Attributes()->GetMaterialID();
		for (int TID : ComponentMesh.TriangleIndicesItr())
		{
			MaterialIDs->SetValue(TID, MaterialRemap[ComponentIdx][MaterialIDs->GetValue(TID)]);
		}
		// TODO: center the meshes
		FTransform3d WorldTransform = (FTransform3d)ComponentTargets[ComponentIdx]->GetWorldTransform();
		FMeshIndexMappings IndexMaps;
		AppendEditor.AppendMesh(&ComponentMesh, IndexMaps,
			[WorldTransform](int VID, const FVector3d& Pos)
			{
				return WorldTransform.TransformPosition(Pos);
			},
			[WorldTransform](int VID, const FVector3d& Normal)
			{
				return WorldTransform.TransformNormal(Normal);
			}
		);
	}

	Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(this, "Preview");
	Preview->Setup(this->TargetWorld, this);
	Preview->PreviewMesh->UpdatePreview(CombinedSourceMeshes.Get());
	Preview->ConfigureMaterials(AllMaterialSet.Materials, ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager()));


	DrawnLineSet = NewObject<ULineSetComponent>(Preview->PreviewMesh->GetRootComponent());
	DrawnLineSet->SetupAttachment(Preview->PreviewMesh->GetRootComponent());
	DrawnLineSet->SetLineMaterial(ToolSetupUtil::GetDefaultLineComponentMaterial(GetToolManager()));
	DrawnLineSet->RegisterComponent();

	Preview->OnOpCompleted.AddLambda(
		[this](const FDynamicMeshOperator* Op)
		{
			const FSelfUnionMeshesOp* UnionOp = (const FSelfUnionMeshesOp*)(Op);
			CreatedBoundaryEdges = UnionOp->GetCreatedBoundaryEdges();
		}
	);
	Preview->OnMeshUpdated.AddLambda(
		[this](const UMeshOpPreviewWithBackgroundCompute*)
		{
			GetToolManager()->PostInvalidation();
			UpdateVisualization();
		}
	);
}


void USelfUnionMeshesTool::UpdateVisualization()
{
	FColor BoundaryEdgeColor(240, 15, 15);
	float BoundaryEdgeThickness = 2.0;
	float BoundaryEdgeDepthBias = 2.0f;

	const FDynamicMesh3* TargetMesh = Preview->PreviewMesh->GetPreviewDynamicMesh();
	FVector3d A, B;

	DrawnLineSet->Clear();
	if (Properties->bShowNewBoundaryEdges)
	{
		for (int EID : CreatedBoundaryEdges)
		{
			TargetMesh->GetEdgeV(EID, A, B);
			DrawnLineSet->AddLine((FVector)A, (FVector)B, BoundaryEdgeColor, BoundaryEdgeThickness, BoundaryEdgeDepthBias);
		}
	}
}


void USelfUnionMeshesTool::Shutdown(EToolShutdownType ShutdownType)
{
	Properties->SaveProperties(this);
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
			GetToolManager()->BeginUndoTransaction(LOCTEXT("SelfUnionMeshes", "Boolean Meshes"));

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
}

void USelfUnionMeshesTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}

TUniquePtr<FDynamicMeshOperator> USelfUnionMeshesTool::MakeNewOperator()
{
	TUniquePtr<FSelfUnionMeshesOp> Op = MakeUnique<FSelfUnionMeshesOp>();
	
	Op->bAttemptFixHoles = Properties->bAttemptFixHoles;

	Op->SetResultTransform(FTransform3d::Identity()); // TODO Center the combined meshes (when building them) and change this transform accordingly
	Op->CombinedMesh = CombinedSourceMeshes;

	return Op;
}



void USelfUnionMeshesTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}

void USelfUnionMeshesTool::OnTick(float DeltaTime)
{
	Preview->Tick(DeltaTime);
}


#if WITH_EDITOR
void USelfUnionMeshesTool::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Preview->InvalidateResult();
}
#endif

void USelfUnionMeshesTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	if (PropertySet == HandleSourcesProperties)
	{
		// nothing
	}
	else if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(USelfUnionMeshesToolProperties, bShowNewBoundaryEdges)))
	{
		GetToolManager()->PostInvalidation();
		UpdateVisualization();
	}
	else
	{
		Preview->InvalidateResult();
	}
}


bool USelfUnionMeshesTool::HasAccept() const
{
	return true;
}

bool USelfUnionMeshesTool::CanAccept() const
{
	return Preview->HaveValidResult();
}


void USelfUnionMeshesTool::GenerateAsset(const FDynamicMeshOpResult& Result)
{
	check(Result.Mesh.Get() != nullptr);

	FVector3d Center = Result.Mesh->GetCachedBounds().Center();
	MeshTransforms::Translate(*Result.Mesh, -Center);
	FTransform3d CenteredTransform = Result.Transform;
	CenteredTransform.SetTranslation(CenteredTransform.GetTranslation() + Result.Transform.TransformVector(Center));
	AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
		AssetAPI, TargetWorld,
		Result.Mesh.Get(), CenteredTransform, TEXT("Merged Mesh"), Preview->StandardMaterials);
	if (NewActor != nullptr)
	{
		ToolSelectionUtil::SetNewActorSelection(GetToolManager(), NewActor);
	}
}




#undef LOCTEXT_NAMESPACE
