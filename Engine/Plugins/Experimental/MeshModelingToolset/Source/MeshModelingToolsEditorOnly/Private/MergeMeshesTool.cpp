// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MergeMeshesTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "ToolSetupUtil.h"
#include "AssetGenerationUtil.h"
#include "Selection/ToolSelectionUtil.h"

#include "DynamicMesh3.h"
#include "MeshDescriptionToDynamicMesh.h"

#include "CompositionOps/VoxelMergeMeshesOp.h"

#define LOCTEXT_NAMESPACE "UMergeMeshesTool"

/*
 * ToolBuilder
 */


bool UMergeMeshesToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	const bool bHasBuildAPI = (this->AssetAPI != nullptr);
	const int32 MinRequiredComponents = 1;
	const bool bHasComponents = ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) >= MinRequiredComponents;
	return ( bHasBuildAPI && bHasComponents );
}

UInteractiveTool* UMergeMeshesToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UMergeMeshesTool* NewTool = NewObject<UMergeMeshesTool>(SceneState.ToolManager);

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

UMergeMeshesTool::UMergeMeshesTool()
{
}

void UMergeMeshesTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UMergeMeshesTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}



void UMergeMeshesTool::Setup()
{
	UInteractiveTool::Setup();

	MergeProps = NewObject<UMergeMeshesToolProperties>();
	AddToolPropertySource(MergeProps);

	MeshStatisticsProperties = NewObject<UMeshStatisticsProperties>(this);
	AddToolPropertySource(MeshStatisticsProperties);

	// Hide the source meshes
	for (auto& ComponentTarget : ComponentTargets)
	{
		ComponentTarget->SetOwnerVisibility(false);
	}

	// save transformed version of input meshes (maybe this could happen in the Operator?)
	CacheInputMeshes();

	// initialize the PreviewMesh+BackgroundCompute object
	Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(this, "Preview");
	Preview->Setup(this->TargetWorld, this);
	Preview->OnMeshUpdated.AddLambda([this](UMeshOpPreviewWithBackgroundCompute* Compute) {
		MeshStatisticsProperties->Update(*Compute->PreviewMesh->GetPreviewDynamicMesh());
	});

	CreateLowQualityPreview(); // update the preview with a low-quality result
	
	Preview->ConfigureMaterials(
		// TODO: This is is very likely a bug waiting to happen
		ToolSetupUtil::GetDefaultMaterial(GetToolManager(), ComponentTargets[0]->GetMaterial(0)),
		ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
	);

	Preview->InvalidateResult();    // start compute

}


void UMergeMeshesTool::Shutdown(EToolShutdownType ShutdownType)
{
	FDynamicMeshOpResult Result = Preview->Shutdown();
	if (ShutdownType == EToolShutdownType::Accept)
	{
		// Generate the result
		{
			GetToolManager()->BeginUndoTransaction(LOCTEXT("MergeMeshes", "Merge Meshes"));

			GenerateAsset(Result);

			GetToolManager()->EndUndoTransaction();
		}

		// Hide or destroy the sources
		{
			if (MergeProps->bDeleteInputActors) 
				GetToolManager()->BeginUndoTransaction(LOCTEXT("MergeMeshes", "Remove Sources"));
			
			for (auto& ComponentTarget : ComponentTargets)
			{
				ComponentTarget->SetOwnerVisibility(true);
				AActor* Actor = ComponentTarget->GetOwnerActor();
				if (MergeProps->bDeleteInputActors)
				{
					Actor->Destroy();
				}
				else
				{
					// just hide the result.
					Actor->SetIsTemporarilyHiddenInEditor(true);
				}
			}

			if (MergeProps->bDeleteInputActors) 
				GetToolManager()->EndUndoTransaction();
		}
		

	}
	else
	{
		// Restore (unhide) the source meshes
		for (auto& ComponentTarget : ComponentTargets)
		{
			ComponentTarget->SetOwnerVisibility(true);
		}
	}
}



void UMergeMeshesTool::Tick(float DeltaTime)
{
	Preview->Tick(DeltaTime);
}


void UMergeMeshesTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}


bool UMergeMeshesTool::HasAccept() const
{
	return true;
}

bool UMergeMeshesTool::CanAccept() const
{
	return Preview->HaveValidResult();
}

void UMergeMeshesTool::OnPropertyModified(UObject* PropertySet, UProperty* Property)
{
	Preview->InvalidateResult();
}




TUniquePtr<FDynamicMeshOperator> UMergeMeshesTool::MakeNewOperator()
{
	TUniquePtr<FVoxelMergeMeshesOp> MergeOp = MakeUnique<FVoxelMergeMeshesOp>();
	MergeOp->VoxelCount     = MergeProps->VoxelCount;
	MergeOp->AdaptivityD    = MergeProps->MeshAdaptivity;
	MergeOp->IsoSurfaceD    = MergeProps->OffsetDistance;
	MergeOp->bAutoSimplify  = MergeProps->bAutoSimplify;
	MergeOp->InputMeshArray = InputMeshes;
	return MergeOp;
}



void UMergeMeshesTool::CacheInputMeshes()
{
	InputMeshes = MakeShared<TArray<IVoxelBasedCSG::FPlacedMesh>>();

	// Package the selected meshes and transforms for consumption by the CSGTool
	for (auto& ComponentTarget : ComponentTargets)
	{
		IVoxelBasedCSG::FPlacedMesh PlacedMesh;
		PlacedMesh.Mesh = ComponentTarget->GetMesh();
		PlacedMesh.Transform = ComponentTarget->GetWorldTransform();
		InputMeshes->Add(PlacedMesh);
	}
}

void UMergeMeshesTool::CreateLowQualityPreview()
{

	FProgressCancel NullInterrupter;
	FVoxelMergeMeshesOp MergeMeshesOp;

	MergeMeshesOp.VoxelCount = 12;
	MergeMeshesOp.AdaptivityD = 0.001;
	MergeMeshesOp.bAutoSimplify = true;
	MergeMeshesOp.InputMeshArray = InputMeshes;
	
	MergeMeshesOp.CalculateResult(&NullInterrupter);
	TUniquePtr<FDynamicMesh3> FastPreviewMesh = MergeMeshesOp.ExtractResult();


	Preview->PreviewMesh->SetTransform((FTransform)MergeMeshesOp.GetResultTransform());
	Preview->PreviewMesh->UpdatePreview(FastPreviewMesh.Get());  // copies the mesh @todo we could just give ownership to the Preview!
	Preview->SetVisibility(true);
}


void UMergeMeshesTool::GenerateAsset(const FDynamicMeshOpResult& Result)
{
	check(Result.Mesh.Get() != nullptr);

	

	AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
		AssetAPI, TargetWorld,
		Result.Mesh.Get(), Result.Transform, TEXT("MergedMesh"),
		AssetGenerationUtil::GetDefaultAutoGeneratedAssetPath());

	// select newly-created object
	ToolSelectionUtil::SetNewActorSelection(GetToolManager(), NewActor);

	
}



#undef LOCTEXT_NAMESPACE
