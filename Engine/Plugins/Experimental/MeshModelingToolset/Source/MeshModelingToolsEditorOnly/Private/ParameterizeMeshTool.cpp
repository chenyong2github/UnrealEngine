// Copyright Epic Games, Inc. All Rights Reserved.

#include "ParameterizeMeshTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh3.h"
#include "DynamicMeshToMeshDescription.h"
#include "FaceGroupUtil.h"

#include "Components/DynamicMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MeshDescription.h"
#include "MeshDescriptionToDynamicMesh.h"

#include "ParameterizationOps/ParameterizeMeshOp.h"
#include "ToolSetupUtil.h"
#include "ModelingToolTargetUtil.h"
#include "TargetInterfaces/MaterialProvider.h"
#include "TargetInterfaces/MeshDescriptionCommitter.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UParameterizeMeshTool"

DEFINE_LOG_CATEGORY_STATIC(LogParameterizeMeshTool, Log, All);

/*
 * ToolBuilder
 */

USingleSelectionMeshEditingTool* UParameterizeMeshToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	UParameterizeMeshTool* NewTool = NewObject<UParameterizeMeshTool>(SceneState.ToolManager);
	NewTool->SetUseAutoGlobalParameterizationMode(bDoAutomaticGlobalUnwrap);
	return NewTool;
}

/*
 * Tool
 */
UParameterizeMeshTool::UParameterizeMeshTool()
{
}

void UParameterizeMeshTool::SetUseAutoGlobalParameterizationMode(bool bEnable)
{
	bDoAutomaticGlobalUnwrap = bEnable;
}


void UParameterizeMeshTool::Setup()
{
	UInteractiveTool::Setup();

	// Copy existing material if there is one	
	IMaterialProvider* TargetMaterial = Cast<IMaterialProvider>(Target);
	DefaultMaterial = TargetMaterial->GetMaterial(0);
	if (DefaultMaterial == nullptr)
	{
		DefaultMaterial = LoadObject<UMaterial>(nullptr, TEXT("/Engine/EngineMaterials/DefaultMaterial"));  
	}

	// hide input StaticMeshComponent
	IPrimitiveComponentBackedTarget* TargetComponent = Cast<IPrimitiveComponentBackedTarget>(Target);
	TargetComponent->SetOwnerVisibility(false);

	// Construct the preview object and set the material on it
	IMeshDescriptionProvider* TargetMeshProvider = Cast<IMeshDescriptionProvider>(Target);
	Preview = NewObject<UMeshOpPreviewWithBackgroundCompute>(this, "Preview");
	Preview->Setup(this->TargetWorld, this);
	Preview->PreviewMesh->SetTangentsMode(EDynamicMeshComponentTangentsMode::AutoCalculated);
	Preview->PreviewMesh->ReplaceMesh(UE::ToolTarget::GetDynamicMeshCopy(Target));

	Preview->OnMeshUpdated.AddLambda([this](UMeshOpPreviewWithBackgroundCompute* Op)
	{
		MaterialSettings->UpdateMaterials();
	});

	// Initialize the preview mesh with a copy of the source mesh.
	bool bHasGroups = false;
	{
		InputMesh = MakeShared<FDynamicMesh3, ESPMode::ThreadSafe>( * Preview->PreviewMesh->GetMesh() );
		FMeshDescriptionToDynamicMesh Converter;
		bHasGroups = FaceGroupUtil::HasMultipleGroups(*InputMesh);

		FComponentMaterialSet MaterialSet;
		TargetMaterial->GetMaterialSet(MaterialSet);
		Preview->ConfigureMaterials(MaterialSet.Materials,
			ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
		);

		Preview->PreviewMesh->SetTransform(TargetComponent->GetWorldTransform());
	}

	if (bDoAutomaticGlobalUnwrap == false && bHasGroups == false)
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("NoGroupsWarning", "This mesh has no PolyGroups!"),
			EToolMessageLevel::UserWarning);
		//bDoAutomaticGlobalUnwrap = true;
	}

	// initialize our properties

	UVChannelProperties = NewObject<UMeshUVChannelProperties>(this);
	UVChannelProperties->RestoreProperties(this);
	UVChannelProperties->Initialize(TargetMeshProvider->GetMeshDescription(), false);
	UVChannelProperties->ValidateSelection(true);
	UVChannelProperties->WatchProperty(UVChannelProperties->UVChannel, [this](const FString& NewValue) 
	{
		MaterialSettings->UVChannel = UVChannelProperties->GetSelectedChannelIndex(true);
	});

	AddToolPropertySource(UVChannelProperties);

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
		SetToolDisplayName(LOCTEXT("ToolNameGlobal", "AutoUV"));
		GetToolManager()->DisplayMessage(
			LOCTEXT("OnStartTool_Global", "Automatically partition the selected Mesh into UV islands, flatten, and pack into a single UV chart"),
			EToolMessageLevel::UserNotification);
	}
	else
	{
		SetToolDisplayName(LOCTEXT("ToolNameLocal", "UV Unwrap"));
		GetToolManager()->DisplayMessage(
			LOCTEXT("OnStartTool_Regions", "Generate UVs for polygroups or existing UV charts of the Mesh using various strategies. Does not calculate layout/packing."),
			EToolMessageLevel::UserNotification);
	}
}

void UParameterizeMeshTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	bool bForceMaterialUpdate = false;
	if (PropertySet == Settings || PropertySet == UVChannelProperties)
	{
		// One of the UV generation properties must have changed.  Dirty the result to force a recompute
		Preview->InvalidateResult();
		bForceMaterialUpdate = true;
	}

	if (PropertySet == MaterialSettings || bForceMaterialUpdate)
	{
		MaterialSettings->UpdateMaterials();
		Preview->OverrideMaterial = MaterialSettings->GetActiveOverrideMaterial();
	}
}


void UParameterizeMeshTool::Shutdown(EToolShutdownType ShutdownType)
{
	UVChannelProperties->SaveProperties(this);
	Settings->SaveProperties(this);
	MaterialSettings->SaveProperties(this);

	FDynamicMeshOpResult Result = Preview->Shutdown();
	
	// Restore (unhide) the source meshes
	Cast<IPrimitiveComponentBackedTarget>(Target)->SetOwnerVisibility(true);

	if (ShutdownType == EToolShutdownType::Accept)
	{
		FDynamicMesh3* DynamicMeshResult = Result.Mesh.Get();
		check(DynamicMeshResult != nullptr);
		GetToolManager()->BeginUndoTransaction(LOCTEXT("ParameterizeMesh", "Parameterize Mesh"));

		Cast<IMeshDescriptionCommitter>(Target)->CommitMeshDescription([DynamicMeshResult](const IMeshDescriptionCommitter::FCommitterParams& CommitParams)
		{
			FMeshDescription* MeshDescription = CommitParams.MeshDescriptionOut;

			bool bVerticesOnly = false;
			bool bAttributesOnly = true;
			if (FDynamicMeshToMeshDescription::HaveMatchingElementCounts(DynamicMeshResult, MeshDescription, bVerticesOnly, bAttributesOnly))
			{
				FDynamicMeshToMeshDescription Converter;
				Converter.UpdateAttributes(DynamicMeshResult, *MeshDescription, false, false, true/*update uvs*/);
			}
			else
			{
				// must have been duplicate tris in the mesh description; we can't count on 1-to-1 mapping of TriangleIDs.  Just convert 
				FDynamicMeshToMeshDescription Converter;
				Converter.Convert(DynamicMeshResult, *MeshDescription);
			}
		});
		GetToolManager()->EndUndoTransaction();
	}

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
	ParameterizeMeshOp->UVLayer = UVChannelProperties->GetSelectedChannelIndex(true);
	
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

	ParameterizeMeshOp->Method = (EParamOpBackend)(int)Settings->Method;

	UE::Geometry::FTransform3d XForm3d(Cast<IPrimitiveComponentBackedTarget>(Target)->GetWorldTransform());
	ParameterizeMeshOp->SetTransform(XForm3d);

	return ParameterizeMeshOp;
}



#undef LOCTEXT_NAMESPACE
