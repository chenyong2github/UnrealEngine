// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ParameterizeMeshTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh3.h"
#include "DynamicMeshToMeshDescription.h"



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

	return NewTool;
}


/*
 * Tool
 */

UParameterizeMeshTool::UParameterizeMeshTool()
{
	ChartStretch = 0.11f;
}

void UParameterizeMeshTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UParameterizeMeshTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}

void UParameterizeMeshTool::Setup()
{
	UInteractiveTool::Setup();

	// Deep copy of input mesh to be shared with the UV generation tool.
	InputMesh = MakeShared<FMeshDescription>(*ComponentTarget->GetMesh());


	// Copy existing material if there is one	
	DefaultMaterial = ComponentTarget->GetMaterial(0);
	if (DefaultMaterial == nullptr)
	{
		DefaultMaterial = LoadObject<UMaterial>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial"));  
	}

	

	// hide input StaticMeshComponent
	ComponentTarget->SetOwnerVisibility(false);

	// initialize our properties
	ToolPropertyObjects.Add(this);

	
	// Construct the preview object and set the material on it
	Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(this, "Preview");
	Preview->Setup(this->TargetWorld, this);

	// Initialize the preview mesh with a copy of the source mesh.
	{
		FDynamicMesh3 Mesh;
		FMeshDescriptionToDynamicMesh Converter;
		Converter.bPrintDebugMessages = false;
		Converter.Convert(InputMesh.Get(), Mesh);

		Preview->PreviewMesh->UpdatePreview(&Mesh);
		Preview->PreviewMesh->SetTransform(ComponentTarget->GetWorldTransform());
	}
	Preview->ConfigureMaterials(
		ToolSetupUtil::GetDefaultMaterial(GetToolManager(), DefaultMaterial),
		ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
	);


	MaterialSettings = NewObject<UExistingMeshMaterialProperties>(this);
	MaterialSettings->Setup();
	AddToolPropertySource(MaterialSettings);


	Preview->SetVisibility(true);
	Preview->InvalidateResult();    // start compute
}

void UParameterizeMeshTool::OnPropertyModified(UObject* PropertySet, UProperty* Property)
{
	if (PropertySet == MaterialSettings)
	{
		MaterialSettings->UpdateMaterials();
		MaterialSettings->SetMaterialIfChanged(ComponentTarget->GetMaterial(0), Preview->StandardMaterial, [this](UMaterialInterface* Material)
		{
			Preview->ConfigureMaterials(ToolSetupUtil::GetDefaultMaterial(GetToolManager(), Material), Preview->WorkingMaterial);
		});
	}
}


void UParameterizeMeshTool::Shutdown(EToolShutdownType ShutdownType)
{

	TUniquePtr<FDynamicMeshOpResult> Result = Preview->Shutdown();
	if (ShutdownType == EToolShutdownType::Accept)
	{
		FDynamicMesh3* DynamicMeshResult = Result->Mesh.Get();
		check(DynamicMeshResult != nullptr);
		GetToolManager()->BeginUndoTransaction(LOCTEXT("ParameterizeMesh", "Parameterize Mesh"));

		ComponentTarget->CommitMesh([DynamicMeshResult](FMeshDescription* MeshDescription)
		{
			FDynamicMeshToMeshDescription Converter;
			Converter.Convert(DynamicMeshResult, *MeshDescription);
		});
		GetToolManager()->EndUndoTransaction();
	}

	// Restore (unhide) the source meshes
	ComponentTarget->SetOwnerVisibility(true);

}

void UParameterizeMeshTool::Tick(float DeltaTime)
{
	Preview->Tick(DeltaTime);
}

void UParameterizeMeshTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}

bool UParameterizeMeshTool::HasAccept() const
{
	return true;
}

bool UParameterizeMeshTool::CanAccept() const
{
	return Preview->HaveValidResult();
}

void UParameterizeMeshTool::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// One of the UV generation properties must have changed.  Dirty the result to force a recompute
	Preview->InvalidateResult();
}

TSharedPtr<FDynamicMeshOperator> UParameterizeMeshTool::MakeNewOperator()
{
	TSharedPtr<FParameterizeMeshOp> ParamertizeMeshOp = MakeShared<FParameterizeMeshOp>();
	ParamertizeMeshOp->Stretch   = ChartStretch;
	ParamertizeMeshOp->NumCharts = 0;
	ParamertizeMeshOp->InputMesh = InputMesh;

	const FTransform XForm = ComponentTarget->GetWorldTransform();
	FTransform3d XForm3d(XForm);
	ParamertizeMeshOp->SetTransform(XForm3d);

	return ParamertizeMeshOp;
}



#undef LOCTEXT_NAMESPACE
