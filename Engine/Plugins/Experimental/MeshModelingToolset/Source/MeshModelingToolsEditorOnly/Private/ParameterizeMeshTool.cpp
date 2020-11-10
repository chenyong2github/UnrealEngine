// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParameterizeMeshTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh3.h"
#include "DynamicMeshToMeshDescription.h"
#include "FaceGroupUtil.h"

#include "SimpleDynamicMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "MeshDescriptionToDynamicMesh.h"

#include "ParameterizationOps/ParameterizeMeshOp.h"
#include "ToolSetupUtil.h"

#define LOCTEXT_NAMESPACE "UParameterizeMeshTool"


DEFINE_LOG_CATEGORY_STATIC(LogParameterizeMeshTool, Log, All);

/*
 * ToolBuilder
 */

bool UParameterizeMeshToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}

UInteractiveTool* UParameterizeMeshToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UParameterizeMeshTool* NewTool = NewObject<UParameterizeMeshTool>(SceneState.ToolManager);

	UActorComponent* ActorComponent = ToolBuilderUtil::FindFirstComponent(SceneState, CanMakeComponentTarget);
	auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
	check(MeshComponent != nullptr);
	NewTool->SetSelection(MakeComponentTarget(MeshComponent));
	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);
	NewTool->SetUseAutoGlobalParameterizationMode(bDoAutomaticGlobalUnwrap);

	return NewTool;
}

/*
 * Tool
 */
UParameterizeMeshTool::UParameterizeMeshTool()
{
}

void UParameterizeMeshTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UParameterizeMeshTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}

void UParameterizeMeshTool::SetUseAutoGlobalParameterizationMode(bool bEnable)
{
	bDoAutomaticGlobalUnwrap = bEnable;
}


void UParameterizeMeshTool::Setup()
{
	UInteractiveTool::Setup();

	// Copy existing material if there is one	
	DefaultMaterial = ComponentTarget->GetMaterial(0);
	if (DefaultMaterial == nullptr)
	{
		DefaultMaterial = LoadObject<UMaterial>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial"));  
	}

	// hide input StaticMeshComponent
	ComponentTarget->SetOwnerVisibility(false);

	// Construct the preview object and set the material on it
	Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(this, "Preview");
	Preview->Setup(this->TargetWorld, this);
	Preview->PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::AutoCalculated);

	Preview->PreviewMesh->InitializeMesh(ComponentTarget->GetMesh());

	// Initialize the preview mesh with a copy of the source mesh.
	bool bHasGroups = false;
	{
		InputMesh = MakeShared<FDynamicMesh3>( * Preview->PreviewMesh->GetMesh() );
		FMeshDescriptionToDynamicMesh Converter;
		bHasGroups = FaceGroupUtil::HasMultipleGroups(*InputMesh);

		FComponentMaterialSet MaterialSet;
		ComponentTarget->GetMaterialSet(MaterialSet);
		Preview->ConfigureMaterials(MaterialSet.Materials,
			ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
		);

		Preview->PreviewMesh->SetTransform(ComponentTarget->GetWorldTransform());
	}

	if (bDoAutomaticGlobalUnwrap == false && bHasGroups == false)
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("NoGroupsWarning", "This mesh has no PolyGroups!"),
			EToolMessageLevel::UserWarning);
		//bDoAutomaticGlobalUnwrap = true;
	}

	// initialize our properties
	Settings = NewObject<UParameterizeMeshToolProperties>(this);
	Settings->RestoreProperties(this);
	Settings->bIsGlobalMode = bDoAutomaticGlobalUnwrap;
	AddToolPropertySource(Settings);


	MaterialSettings = NewObject<UExistingMeshMaterialProperties>(this);
	MaterialSettings->RestoreProperties(this);
	AddToolPropertySource(MaterialSettings);
	// force update
	MaterialSettings->UpdateMaterials();
	Preview->OverrideMaterial = MaterialSettings->GetActiveOverrideMaterial();


	Preview->SetVisibility(true);
	Preview->InvalidateResult();    // start compute


	if (bDoAutomaticGlobalUnwrap)
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("OnStartTool_Global", "Automatically partition the selected Mesh into UV islands, flatten, and pack into a single UV chart"),
			EToolMessageLevel::UserNotification);
	}
	else
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("OnStartTool_Regions", "Generate UVs for polygroups or existing UV charts of the Mesh using various strategies. Does not calculate layout/packing."),
			EToolMessageLevel::UserNotification);
	}
}

void UParameterizeMeshTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	if (PropertySet == MaterialSettings)
	{
		MaterialSettings->UpdateMaterials();
		Preview->OverrideMaterial = MaterialSettings->GetActiveOverrideMaterial();
	}
	if (PropertySet == Settings)
	{
		// One of the UV generation properties must have changed.  Dirty the result to force a recompute
		Preview->InvalidateResult();
	}
}


void UParameterizeMeshTool::Shutdown(EToolShutdownType ShutdownType)
{
	Settings->SaveProperties(this);
	MaterialSettings->SaveProperties(this);
	FDynamicMeshOpResult Result = Preview->Shutdown();
	if (ShutdownType == EToolShutdownType::Accept)
	{
		FDynamicMesh3* DynamicMeshResult = Result.Mesh.Get();
		check(DynamicMeshResult != nullptr);
		GetToolManager()->BeginUndoTransaction(LOCTEXT("ParameterizeMesh", "Parameterize Mesh"));

		ComponentTarget->CommitMesh([DynamicMeshResult](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
		{
			FDynamicMeshToMeshDescription Converter;
			Converter.Convert(DynamicMeshResult, *CommitParams.MeshDescription);
		});
		GetToolManager()->EndUndoTransaction();
	}

	// Restore (unhide) the source meshes
	ComponentTarget->SetOwnerVisibility(true);

}

void UParameterizeMeshTool::OnTick(float DeltaTime)
{
	Preview->Tick(DeltaTime);
}

bool UParameterizeMeshTool::CanAccept() const
{
	return Super::CanAccept() && Preview->HaveValidResult();
}

TUniquePtr<FDynamicMeshOperator> UParameterizeMeshTool::MakeNewOperator()
{
	FAxisAlignedBox3d MeshBounds = Preview->PreviewMesh->GetMesh()->GetBounds();
	TUniquePtr<FParameterizeMeshOp> ParameterizeMeshOp = MakeUnique<FParameterizeMeshOp>();
	ParameterizeMeshOp->Stretch   = Settings->ChartStretch;
	ParameterizeMeshOp->NumCharts = 0;
	ParameterizeMeshOp->InputMesh = InputMesh;
	
	if (bDoAutomaticGlobalUnwrap)
	{
		ParameterizeMeshOp->IslandMode = EParamOpIslandMode::Auto;
		ParameterizeMeshOp->UnwrapType = EParamOpUnwrapType::MinStretch;
	}
	else
	{
		ParameterizeMeshOp->IslandMode = (EParamOpIslandMode)(int)Settings->IslandMode;
		ParameterizeMeshOp->UnwrapType = (EParamOpUnwrapType)(int)Settings->UnwrapType;
	}

	switch (Settings->UVScaleMode)
	{
		case EParameterizeMeshToolUVScaleMode::NoScaling:
			ParameterizeMeshOp->bNormalizeAreas = false;
			ParameterizeMeshOp->AreaScaling = 1.0;
			break;
		case EParameterizeMeshToolUVScaleMode::NormalizeToBounds:
			ParameterizeMeshOp->bNormalizeAreas = true;
			ParameterizeMeshOp->AreaScaling = Settings->UVScale / MeshBounds.MaxDim();
			break;
		case EParameterizeMeshToolUVScaleMode::NormalizeToWorld:
			ParameterizeMeshOp->bNormalizeAreas = true;
			ParameterizeMeshOp->AreaScaling = Settings->UVScale;
			break;
	}

	const FTransform XForm = ComponentTarget->GetWorldTransform();
	FTransform3d XForm3d(XForm);
	ParameterizeMeshOp->SetTransform(XForm3d);

	return ParameterizeMeshOp;
}



#undef LOCTEXT_NAMESPACE
