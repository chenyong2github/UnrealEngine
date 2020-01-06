// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EditMeshMaterialsTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "Drawing/MeshDebugDrawing.h"
#include "DynamicMeshEditor.h"
#include "DynamicMeshChangeTracker.h"
#include "Changes/ToolCommandChangeSequence.h"
#include "Changes/MeshChange.h"
#include "Util/ColorConstants.h"
#include "Selections/MeshConnectedComponents.h"
#include "MeshRegionBoundaryLoops.h"
#include "MeshIndexUtil.h"
#include "AssetGenerationUtil.h"
#include "ToolSetupUtil.h"

#define LOCTEXT_NAMESPACE "UEditMeshMaterialsTool"



void UEditMeshMaterialsEditActions::PostMaterialAction(EEditMeshMaterialsToolActions Action)
{
	if (ParentTool.IsValid() && Cast<UEditMeshMaterialsTool>(ParentTool))
	{
		Cast<UEditMeshMaterialsTool>(ParentTool)->RequestMaterialAction(Action);
	}
}


/*
 * ToolBuilder
 */

UMeshSurfacePointTool* UEditMeshMaterialsToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	UEditMeshMaterialsTool* SelectionTool = NewObject<UEditMeshMaterialsTool>(SceneState.ToolManager);
	SelectionTool->SetWorld(SceneState.World);
	SelectionTool->SetAssetAPI(AssetAPI);
	return SelectionTool;
}




void UEditMeshMaterialsTool::Setup()
{
	UMeshSelectionTool::Setup();

	PreviewMesh->ClearOverrideRenderMaterial();

	FComponentMaterialSet AssetMaterials;
	ComponentTarget->GetMaterialSet(AssetMaterials, true);
	MaterialProps->Materials = AssetMaterials.Materials;
	CurrentMaterials = MaterialProps->Materials;
	InitialMaterialKey = GetMaterialKey();

	MaterialSetWatcher.Initialize(
		[this]() { return GetMaterialKey(); },
		[this](FMaterialSetKey NewKey) { OnMaterialSetChanged(); }, GetMaterialKey());

	FComponentMaterialSet ComponentMaterials;
	ComponentTarget->GetMaterialSet(ComponentMaterials, false);
	if (ComponentMaterials != AssetMaterials)
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("MaterialWarning", "The selected Component has a different Material set than the underlying Asset. Asset materials are shown."),
			EToolMessageLevel::UserWarning);
	}

}



UMeshSelectionToolActionPropertySet* UEditMeshMaterialsTool::CreateEditActions()
{
	UEditMeshMaterialsEditActions* Actions = NewObject<UEditMeshMaterialsEditActions>(this);
	Actions->Initialize(this);
	return Actions;
}

void UEditMeshMaterialsTool::AddSubclassPropertySets()
{
	MaterialProps = NewObject<UEditMeshMaterialsToolProperties>(this);
	MaterialProps->RestoreProperties(this);
	AddToolPropertySource(MaterialProps);
}


void UEditMeshMaterialsTool::RequestMaterialAction(EEditMeshMaterialsToolActions ActionType)
{
	if (bHavePendingAction)
	{
		return;
	}

	PendingSubAction = ActionType;
	bHavePendingSubAction = true;
}




void UEditMeshMaterialsTool::Tick(float DeltaTime)
{
	UMeshSelectionTool::Tick(DeltaTime);

	MaterialSetWatcher.CheckAndUpdate();

	if (bHavePendingSubAction)
	{
		ApplyMaterialAction(PendingSubAction);
		bHavePendingSubAction = false;
		PendingSubAction = EEditMeshMaterialsToolActions::NoAction;
	}
}




void UEditMeshMaterialsTool::ApplyMaterialAction(EEditMeshMaterialsToolActions ActionType)
{
	switch (ActionType)
	{
	case EEditMeshMaterialsToolActions::AssignMaterial:
		AssignMaterialToSelectedTriangles();
		break;
	}
}


void UEditMeshMaterialsTool::AssignMaterialToSelectedTriangles()
{
	check(SelectionType == EMeshSelectionElementType::Face);
	TArray<int32> SelectedFaces = Selection->GetElements(EMeshSelectionElementType::Face);
	if (SelectedFaces.Num() == 0)
	{
		return;
	}

	TUniquePtr<FToolCommandChangeSequence> ChangeSeq = MakeUnique<FToolCommandChangeSequence>();

	// clear current selection
	BeginChange(false);
	for (int tid : SelectedFaces)
	{
		ActiveSelectionChange->Add(tid);
	}
	Selection->RemoveIndices(EMeshSelectionElementType::Face, SelectedFaces);
	TUniquePtr<FMeshSelectionChange> SelectionChange = EndChange();
	ChangeSeq->AppendChange(Selection, MoveTemp(SelectionChange));

	int32 SetMaterialID = MaterialProps->SelectedMaterial;

	// assign new groups to triangles
	// note: using an FMeshChange is kind of overkill here
	TUniquePtr<FMeshChange> MeshChange = PreviewMesh->TrackedEditMesh(
		[&SelectedFaces, SetMaterialID](FDynamicMesh3& Mesh, FDynamicMeshChangeTracker& ChangeTracker)
	{
		if (Mesh.Attributes() && Mesh.Attributes()->HasMaterialID())
		{
			FDynamicMeshMaterialAttribute* MaterialIDAttrib = Mesh.Attributes()->GetMaterialID();
			for (int tid : SelectedFaces)
			{
				ChangeTracker.SaveTriangle(tid, true);
				MaterialIDAttrib->SetNewValue(tid, SetMaterialID);
			}
		}
	});
	ChangeSeq->AppendChange(PreviewMesh, MoveTemp(MeshChange));

	// emit combined change sequence
	GetToolManager()->EmitObjectChange(this, MoveTemp(ChangeSeq), LOCTEXT("MeshSelectionToolAssignMaterial", "Assign Material"));

	OnExternalSelectionChange();
	bHaveModifiedMesh = true;
}




void UEditMeshMaterialsTool::OnMaterialSetChanged()
{
	TUniquePtr<FEditMeshMaterials_MaterialSetChange> MaterialChange = MakeUnique<FEditMeshMaterials_MaterialSetChange>();
	MaterialChange->MaterialsBefore = CurrentMaterials;
	MaterialChange->MaterialsAfter = MaterialProps->Materials;

	PreviewMesh->SetMaterials(MaterialProps->Materials);

	CurrentMaterials = MaterialProps->Materials;

	GetToolManager()->EmitObjectChange(this, MoveTemp(MaterialChange), LOCTEXT("MaterialSetChange", "Material Change"));

	bHaveModifiedMaterials = true;
}



void UEditMeshMaterialsTool::ExternalUpdateMaterialSet(const TArray<UMaterialInterface*>& NewMaterialSet)
{
	MaterialProps->Materials = NewMaterialSet;
	MaterialSetWatcher.SilentUpdate();
	PreviewMesh->SetMaterials(MaterialProps->Materials);
	CurrentMaterials = MaterialProps->Materials;
}





void UEditMeshMaterialsTool::OnShutdown(EToolShutdownType ShutdownType)
{
	// this is a bit of a hack, UMeshSelectionTool::OnShutdown will also do this...
	SelectionProps->SaveProperties(this);

	if (ShutdownType == EToolShutdownType::Accept)
	{
		GetToolManager()->BeginUndoTransaction(LOCTEXT("EditMeshMaterialsTransactionName", "Edit Materials"));

		if (GetMaterialKey() != InitialMaterialKey)
		{
			FComponentMaterialSet NewMaterialSet;
			NewMaterialSet.Materials = CurrentMaterials;
			ComponentTarget->CommitMaterialSetUpdate(NewMaterialSet, true);
		}

		UMeshSelectionTool::OnShutdown(ShutdownType);

		GetToolManager()->EndUndoTransaction();
	}
}




bool UEditMeshMaterialsTool::FMaterialSetKey::operator!=(const FMaterialSetKey& Key2) const
{
	int Num = Values.Num();
	if (Key2.Values.Num() != Num)
	{
		return true;
	}
	for (int j = 0; j < Num; ++j)
	{
		if (Key2.Values[j] != Values[j])
		{
			return true;
		}
	}
	return false;
}

UEditMeshMaterialsTool::FMaterialSetKey UEditMeshMaterialsTool::GetMaterialKey()
{
	FMaterialSetKey Key;
	for (UMaterialInterface* Material : MaterialProps->Materials)
	{
		Key.Values.Add(Material);
	}
	return Key;
}




void FEditMeshMaterials_MaterialSetChange::Apply(UObject* Object)
{
	UEditMeshMaterialsTool* Tool = CastChecked<UEditMeshMaterialsTool>(Object);
	Tool->ExternalUpdateMaterialSet(MaterialsAfter);
}

void FEditMeshMaterials_MaterialSetChange::Revert(UObject* Object)
{
	UEditMeshMaterialsTool* Tool = CastChecked<UEditMeshMaterialsTool>(Object);
	Tool->ExternalUpdateMaterialSet(MaterialsBefore);
}

FString FEditMeshMaterials_MaterialSetChange::ToString() const
{
	return FString(TEXT("MaterialSet Change"));
}

#undef LOCTEXT_NAMESPACE