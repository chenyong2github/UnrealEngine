// Copyright Epic Games, Inc. All Rights Reserved.

#include "VoxelCSGMeshesTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "ToolSetupUtil.h"
#include "AssetGenerationUtil.h"
#include "Selection/ToolSelectionUtil.h"

#include "DynamicMesh3.h"
#include "MeshDescriptionToDynamicMesh.h"

#include "CompositionOps/VoxelBooleanMeshesOp.h"

#include "TargetInterfaces/MeshDescriptionCommitter.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "ToolTargetManager.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UVoxelCSGMeshesTool"


/*
 * ToolBuilder
 */
const FToolTargetTypeRequirements& UVoxelCSGMeshesToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements({
		UMeshDescriptionCommitter::StaticClass(),
		UMeshDescriptionProvider::StaticClass(),
		UPrimitiveComponentBackedTarget::StaticClass()
		});
	return TypeRequirements;
}

bool UVoxelCSGMeshesToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	bool bHasBuildAPI = (this->AssetAPI != nullptr);
	bool bHasComponents = SceneState.TargetManager->CountSelectedAndTargetable(SceneState, GetTargetRequirements()) == 2;
	return (bHasBuildAPI && bHasComponents);
}

UInteractiveTool* UVoxelCSGMeshesToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UVoxelCSGMeshesTool* NewTool = NewObject<UVoxelCSGMeshesTool>(SceneState.ToolManager);

	TArray<TObjectPtr<UToolTarget>> Targets = SceneState.TargetManager->BuildAllSelectedTargetable(SceneState, GetTargetRequirements());
	NewTool->SetTargets(MoveTemp(Targets));
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

void UVoxelCSGMeshesTool::SetAssetAPI(IAssetGenerationAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}



void UVoxelCSGMeshesTool::Setup()
{
	UInteractiveTool::Setup();

	CSGProps = NewObject<UVoxelCSGMeshesToolProperties>();
	CSGProps->RestoreProperties(this);
	AddToolPropertySource(CSGProps);

	MeshStatisticsProperties = NewObject<UMeshStatisticsProperties>(this);
	AddToolPropertySource(MeshStatisticsProperties);

	HandleSourcesProperties = NewObject<UOnAcceptHandleSourcesProperties>(this);
	HandleSourcesProperties->RestoreProperties(this);
	AddToolPropertySource(HandleSourcesProperties);


	// Hide the source meshes
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		TargetComponentInterface(ComponentIdx)->SetOwnerVisibility(false);
	}

	// save transformed version of input meshes (maybe this could happen in the Operator?)
	CacheInputMeshes();

	// initialize the PreviewMesh+BackgroundCompute object
	Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(this, "Preview");
	Preview->Setup(this->TargetWorld, this);
	Preview->OnMeshUpdated.AddLambda([this](UMeshOpPreviewWithBackgroundCompute* Compute) {
		MeshStatisticsProperties->Update(*Compute->PreviewMesh->GetPreviewDynamicMesh());
		UpdateAcceptWarnings(Compute->HaveEmptyResult() ? EAcceptWarning::EmptyForbidden : EAcceptWarning::NoWarning);
	});

	CreateLowQualityPreview();

	Preview->ConfigureMaterials(
		ToolSetupUtil::GetDefaultSculptMaterial(GetToolManager()),
		ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
	);
	
	Preview->InvalidateResult();    // start compute

	SetToolDisplayName(LOCTEXT("ToolName", "Voxel Boolean"));
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool", "Compute a CSG Boolean of the input meshes using voxelization techniques. UVs, sharp edges, and small/thin features will be lost. Increase Voxel Count to enhance accuracy."),
		EToolMessageLevel::UserNotification);
}


void UVoxelCSGMeshesTool::Shutdown(EToolShutdownType ShutdownType)
{
	CSGProps->SaveProperties(this);
	HandleSourcesProperties->SaveProperties(this);

	FDynamicMeshOpResult Result = Preview->Shutdown();
	// Restore (unhide) the source meshes
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		TargetComponentInterface(ComponentIdx)->SetOwnerVisibility(true);
	}
	if (ShutdownType == EToolShutdownType::Accept)
	{
		GetToolManager()->BeginUndoTransaction(LOCTEXT("BooleanMeshes", "Boolean Meshes"));

		// Generate the result
		GenerateAsset(Result);

		TArray<AActor*> Actors;
		for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
		{
			Actors.Add(TargetComponentInterface(ComponentIdx)->GetOwnerActor());
		}
		HandleSourcesProperties->ApplyMethod(Actors, GetToolManager());

		GetToolManager()->EndUndoTransaction();
	}
}



void UVoxelCSGMeshesTool::OnTick(float DeltaTime)
{
	Preview->Tick(DeltaTime);
}

void UVoxelCSGMeshesTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}

bool UVoxelCSGMeshesTool::CanAccept() const
{
	return Super::CanAccept() && Preview->HaveValidNonEmptyResult();
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
	for (int32 ComponentIdx = 0; ComponentIdx < Targets.Num(); ComponentIdx++)
	{
		IVoxelBasedCSG::FPlacedMesh PlacedMesh;
		PlacedMesh.Mesh = TargetMeshProviderInterface(ComponentIdx)->GetMeshDescription();
		PlacedMesh.Transform = TargetComponentInterface(ComponentIdx)->GetWorldTransform();
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

	AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
		AssetAPI, TargetWorld,
		Result.Mesh.Get(), Result.Transform, TEXT("CSGMesh"));
	if (NewActor != nullptr)
	{
		ToolSelectionUtil::SetNewActorSelection(GetToolManager(), NewActor);
	}
}

#undef LOCTEXT_NAMESPACE
