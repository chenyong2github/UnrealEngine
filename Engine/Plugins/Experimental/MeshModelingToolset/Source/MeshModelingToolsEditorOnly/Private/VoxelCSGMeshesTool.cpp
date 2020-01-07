// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "VoxelCSGMeshesTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "ToolSetupUtil.h"
#include "AssetGenerationUtil.h"
#include "Selection/ToolSelectionUtil.h"

#include "DynamicMesh3.h"
#include "MeshDescriptionToDynamicMesh.h"

#include "CompositionOps/VoxelBooleanMeshesOp.h"

#define LOCTEXT_NAMESPACE "UVoxelCSGMeshesTool"


/*
 * ToolBuilder
 */
bool UVoxelCSGMeshesToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	bool bHasBuildAPI = (this->AssetAPI != nullptr);
	bool bHasComponents = ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 2;
	return (bHasBuildAPI && bHasComponents);
}

UInteractiveTool* UVoxelCSGMeshesToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UVoxelCSGMeshesTool* NewTool = NewObject<UVoxelCSGMeshesTool>(SceneState.ToolManager);

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

UVoxelCSGMeshesTool::UVoxelCSGMeshesTool()
{
}

void UVoxelCSGMeshesTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UVoxelCSGMeshesTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}



void UVoxelCSGMeshesTool::Setup()
{
	UInteractiveTool::Setup();

	CSGProps = NewObject<UVoxelCSGMeshesToolProperties>();
	CSGProps->VoxelCount = 128;
	CSGProps->MeshAdaptivity = 0.01f;
	CSGProps->OffsetDistance = 0.0f;
	AddToolPropertySource(CSGProps);

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

	CreateLowQualityPreview();

	Preview->ConfigureMaterials(
		// TODO : Probably multi-select bug - MCD
		ToolSetupUtil::GetDefaultMaterial(GetToolManager(), ComponentTargets[0]->GetMaterial(0)),
		ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
	);
	
	Preview->InvalidateResult();    // start compute
}


void UVoxelCSGMeshesTool::Shutdown(EToolShutdownType ShutdownType)
{
	FDynamicMeshOpResult Result = Preview->Shutdown();
	if (ShutdownType == EToolShutdownType::Accept)
	{
		// Generate the result
		{
			GetToolManager()->BeginUndoTransaction(LOCTEXT("VoxelCSGMeshes", "Boolean Meshes"));

			GenerateAsset(Result);

			GetToolManager()->EndUndoTransaction();
		}

		// Hide or destroy the sources
		{
			
			if (CSGProps->bDeleteInputActors) 
				GetToolManager()->BeginUndoTransaction(LOCTEXT("VoxelCSGMeshes", "Remove Sources"));
			
			for (auto& ComponentTarget : ComponentTargets)
			{
				ComponentTarget->SetOwnerVisibility(true);
				AActor* Actor = ComponentTarget->GetOwnerActor();
				if (CSGProps->bDeleteInputActors)
				{
					Actor->Destroy();
				}
				else
				{
					Actor->SetIsTemporarilyHiddenInEditor(true);
				}
			}

			if (CSGProps->bDeleteInputActors) 
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



void UVoxelCSGMeshesTool::Tick(float DeltaTime)
{
	Preview->Tick(DeltaTime);
}

void UVoxelCSGMeshesTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}

bool UVoxelCSGMeshesTool::HasAccept() const
{
	return true;
}

bool UVoxelCSGMeshesTool::CanAccept() const
{
	return Preview->HaveValidResult();
}

void UVoxelCSGMeshesTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	Preview->InvalidateResult();
}

TUniquePtr<FDynamicMeshOperator> UVoxelCSGMeshesTool::MakeNewOperator()
{
	TUniquePtr<FVoxelBooleanMeshesOp> CSGOp = MakeUnique<FVoxelBooleanMeshesOp>();
	CSGOp->Operation      = (FVoxelBooleanMeshesOp::EBooleanOperation)(int)CSGProps->Operation;
	CSGOp->VoxelCount     = CSGProps->VoxelCount;
	CSGOp->AdaptivityD    = CSGProps->MeshAdaptivity;
	CSGOp->IsoSurfaceD    = CSGProps->OffsetDistance;
	CSGOp->bAutoSimplify  = CSGProps->bAutoSimplify;
	CSGOp->InputMeshArray = InputMeshes;
	return CSGOp;
}

void UVoxelCSGMeshesTool::CacheInputMeshes()
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

void UVoxelCSGMeshesTool::CreateLowQualityPreview()
{

	FProgressCancel NullInterrupter;
	FVoxelBooleanMeshesOp BooleanOp;

	BooleanOp.Operation = (FVoxelBooleanMeshesOp::EBooleanOperation)(int)CSGProps->Operation;
	BooleanOp.VoxelCount = 12;
	BooleanOp.AdaptivityD = 0.01;
	BooleanOp.bAutoSimplify = true;
	BooleanOp.InputMeshArray = InputMeshes;
	
	BooleanOp.CalculateResult(&NullInterrupter);
	TUniquePtr<FDynamicMesh3> FastPreviewMesh = BooleanOp.ExtractResult();


	Preview->PreviewMesh->SetTransform((FTransform)BooleanOp.GetResultTransform());
	Preview->PreviewMesh->UpdatePreview(FastPreviewMesh.Get());  // copies the mesh @todo we could just give ownership to the Preview!
	Preview->SetVisibility(true);
}

void UVoxelCSGMeshesTool::GenerateAsset(const FDynamicMeshOpResult& Result)
{
	check(Result.Mesh.Get() != nullptr);

	//GetToolManager()->BeginUndoTransaction(LOCTEXT("VoxelCSGMeshes", "Boolean Meshes"));

	AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
		AssetAPI, TargetWorld,
		Result.Mesh.Get(), Result.Transform, TEXT("CSGMesh"),
		AssetGenerationUtil::GetDefaultAutoGeneratedAssetPath());

	// select newly-created object
	ToolSelectionUtil::SetNewActorSelection(GetToolManager(), NewActor);

	//GetToolManager()->EndUndoTransaction();
}

#undef LOCTEXT_NAMESPACE
