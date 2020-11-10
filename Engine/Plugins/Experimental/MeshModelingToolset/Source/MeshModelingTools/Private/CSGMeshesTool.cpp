// Copyright Epic Games, Inc. All Rights Reserved.

#include "CSGMeshesTool.h"
#include "CompositionOps/BooleanMeshesOp.h"

#include "ToolSetupUtil.h"

#include "DynamicMesh3.h"

#include "MeshDescriptionToDynamicMesh.h"


#define LOCTEXT_NAMESPACE "UCSGMeshesTool"



void UCSGMeshesTool::SetupProperties()
{
	Super::SetupProperties();

	CSGProperties = NewObject<UCSGMeshesToolProperties>(this);
	CSGProperties->RestoreProperties(this);
	AddToolPropertySource(CSGProperties);

	SetToolDisplayName(LOCTEXT("CSGMeshesToolName", "Mesh Boolean Tool"));
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool", "Compute CSG Booleans on the input meshes. Use the transform gizmos to tweak the positions of the input objects (can help to resolve errors/failures)"),
		EToolMessageLevel::UserNotification);
}

void UCSGMeshesTool::SaveProperties()
{
	Super::SaveProperties();
	CSGProperties->SaveProperties(this);
}


void UCSGMeshesTool::ConvertInputsAndSetPreviewMaterials(bool bSetPreviewMesh)
{
	OriginalDynamicMeshes.SetNum(ComponentTargets.Num());
	FComponentMaterialSet AllMaterialSet;
	TMap<UMaterialInterface*, int> KnownMaterials;
	TArray<TArray<int>> MaterialRemap; MaterialRemap.SetNum(ComponentTargets.Num());

	if (!CSGProperties->bOnlyUseFirstMeshMaterials)
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
	Preview->ConfigureMaterials(AllMaterialSet.Materials, ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager()));
}


void UCSGMeshesTool::SetPreviewCallbacks()
{	
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
		FTransform UseTransform = TransformProxies[Idx]->GetTransform();
		UseTransform.MultiplyScale3D(TransformInitialScales[Idx]);
		BooleanOp->Transforms[Idx] = UseTransform;
	}

	return BooleanOp;
}



void UCSGMeshesTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UCSGMeshesToolProperties, bOnlyUseFirstMeshMaterials)))
	{
		if (!AreAllTargetsValid())
		{
			GetToolManager()->DisplayMessage(LOCTEXT("InvalidTargets", "Target meshes are no longer valid"), EToolMessageLevel::UserWarning);
			return;
		}
		ConvertInputsAndSetPreviewMaterials(false);
		Preview->InvalidateResult();
	}
	else if (Property && (Property->GetFName() == GET_MEMBER_NAME_CHECKED(UCSGMeshesToolProperties, bShowNewBoundaryEdges)))
	{
		GetToolManager()->PostInvalidation();
		UpdateVisualization();
	}
	else
	{
		Super::OnPropertyModified(PropertySet, Property);
	}
}


FString UCSGMeshesTool::GetCreatedAssetName() const
{
	return TEXT("Boolean");
}


FText UCSGMeshesTool::GetActionName() const
{
	return LOCTEXT("CSGMeshes", "Boolean Meshes");
}







#undef LOCTEXT_NAMESPACE
