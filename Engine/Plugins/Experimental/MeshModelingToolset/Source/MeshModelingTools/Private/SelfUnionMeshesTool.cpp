// Copyright Epic Games, Inc. All Rights Reserved.

#include "SelfUnionMeshesTool.h"
#include "CompositionOps/SelfUnionMeshesOp.h"
#include "InteractiveToolManager.h"
#include "ToolSetupUtil.h"

#include "DynamicMesh3.h"
#include "DynamicMeshEditor.h"
#include "MeshTransforms.h"

#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"

#include "AssetGenerationUtil.h"


#define LOCTEXT_NAMESPACE "USelfUnionMeshesTool"



void USelfUnionMeshesTool::SetupProperties()
{
	Super::SetupProperties();
	Properties = NewObject<USelfUnionMeshesToolProperties>(this);
	Properties->RestoreProperties(this);
	AddToolPropertySource(Properties);

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool", "Compute a Self-Union of the input meshes, to resolve self-intersections. Use the transform gizmos to tweak the positions of the input objects (can help to resolve errors/failures)"),
		EToolMessageLevel::UserNotification);
}


void USelfUnionMeshesTool::SaveProperties()
{
	Super::SaveProperties();
	Properties->SaveProperties(this);
}


void USelfUnionMeshesTool::TransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
	ConvertInputsAndSetPreviewMaterials(false); // have to redo the conversion because the transforms are all baked there
	Preview->InvalidateResult();
}


void USelfUnionMeshesTool::ConvertInputsAndSetPreviewMaterials(bool bSetPreviewMesh)
{
	FComponentMaterialSet AllMaterialSet;
	TMap<UMaterialInterface*, int> KnownMaterials;
	TArray<TArray<int>> MaterialRemap; MaterialRemap.SetNum(ComponentTargets.Num());

	if (!Properties->bOnlyUseFirstMeshMaterials)
	{
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
	}
	else
	{
		ComponentTargets[0]->GetMaterialSet(AllMaterialSet);
		for (int MatIdx = 0; MatIdx < AllMaterialSet.Materials.Num(); MatIdx++)
		{
			MaterialRemap[0].Add(MatIdx);
		}
		for (int ComponentIdx = 1; ComponentIdx < ComponentTargets.Num(); ComponentIdx++)
		{
			MaterialRemap[ComponentIdx].Init(0, ComponentTargets[ComponentIdx]->GetNumMaterials());
		}
	}

	CombinedSourceMeshes = MakeShared<FDynamicMesh3>();
	CombinedSourceMeshes->EnableAttributes();
	CombinedSourceMeshes->EnableTriangleGroups(0);
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
		FTransform UseTransform = TransformProxies[ComponentIdx]->GetTransform();
		UseTransform.MultiplyScale3D(TransformInitialScales[ComponentIdx]);
		FTransform3d WorldTransform = (FTransform3d)UseTransform;
		if (WorldTransform.GetDeterminant() < 0)
		{
			ComponentMesh.ReverseOrientation(false);
		}
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

	Preview->ConfigureMaterials(AllMaterialSet.Materials, ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager()));

	if (bSetPreviewMesh)
	{
		Preview->PreviewMesh->UpdatePreview(CombinedSourceMeshes.Get());
	}
}


void USelfUnionMeshesTool::SetPreviewCallbacks()
{
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


TUniquePtr<FDynamicMeshOperator> USelfUnionMeshesTool::MakeNewOperator()
{
	TUniquePtr<FSelfUnionMeshesOp> Op = MakeUnique<FSelfUnionMeshesOp>();
	
	Op->bAttemptFixHoles = Properties->bAttemptFixHoles;
	Op->WindingNumberThreshold = Properties->WindingNumberThreshold;
	Op->bTrimFlaps = Properties->bTrimFlaps;

	Op->SetResultTransform(FTransform3d::Identity()); // TODO Center the combined meshes (when building them) and change this transform accordingly
	Op->CombinedMesh = CombinedSourceMeshes;

	return Op;
}


void USelfUnionMeshesTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(USelfUnionMeshesToolProperties, bOnlyUseFirstMeshMaterials)))
	{
		if (!AreAllTargetsValid())
		{
			GetToolManager()->DisplayMessage(LOCTEXT("InvalidTargets", "Target meshes are no longer valid"), EToolMessageLevel::UserWarning);
			return;
		}
		ConvertInputsAndSetPreviewMaterials(false);
		Preview->InvalidateResult();
	}
	else if (PropertySet == HandleSourcesProperties)
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
		Super::OnPropertyModified(PropertySet, Property);
	}
}


FString USelfUnionMeshesTool::GetCreatedAssetName() const
{
	return TEXT("Merge");
}


FText USelfUnionMeshesTool::GetActionName() const
{
	return LOCTEXT("SelfUnionMeshes", "Merge Meshes");
}






#undef LOCTEXT_NAMESPACE
