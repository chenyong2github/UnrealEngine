// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MeshSelectionTool.h"
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
#include "Selections/MeshConnectedComponents.h"

#define LOCTEXT_NAMESPACE "UMeshSelectionTool"

/*
 * ToolBuilder
 */

UMeshSurfacePointTool* UMeshSelectionToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	UMeshSelectionTool* SelectionTool = NewObject<UMeshSelectionTool>(SceneState.ToolManager);
	SelectionTool->SetWorld(SceneState.World);
	SelectionTool->SetAssetAPI(AssetAPI);
	return SelectionTool;
}




void UMeshSelectionToolActionPropertySet::PostAction(EMeshSelectionToolActions Action)
{
	if (ParentTool.IsValid())
	{
		ParentTool->RequestAction(Action);
	}
}


void UMeshSelectionToolProperties::SaveProperties(UInteractiveTool* SaveFromTool)
{
	UMeshSelectionToolProperties* PropertyCache = GetPropertyCache<UMeshSelectionToolProperties>();
	PropertyCache->SelectionMode = this->SelectionMode;
	PropertyCache->AngleTolerance = this->AngleTolerance;
	PropertyCache->bVolumetricBrush = this->bVolumetricBrush;
	PropertyCache->bHitBackFaces = this->bHitBackFaces;
	PropertyCache->bShowWireframe = this->bShowWireframe;
	PropertyCache->FaceColorMode = this->FaceColorMode;
}

void UMeshSelectionToolProperties::RestoreProperties(UInteractiveTool* RestoreToTool)
{
	UMeshSelectionToolProperties* PropertyCache = GetPropertyCache<UMeshSelectionToolProperties>();
	this->SelectionMode = PropertyCache->SelectionMode;
	this->AngleTolerance = PropertyCache->AngleTolerance;
	this->bVolumetricBrush = PropertyCache->bVolumetricBrush;
	this->bHitBackFaces = PropertyCache->bHitBackFaces;
	this->bShowWireframe = PropertyCache->bShowWireframe;
	this->FaceColorMode = PropertyCache->FaceColorMode;
}



/*
 * Tool
 */

UMeshSelectionTool::UMeshSelectionTool()
{
}

void UMeshSelectionTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UMeshSelectionTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}



void UMeshSelectionTool::Setup()
{
	UDynamicMeshBrushTool::Setup();

	// hide strength and falloff
	BrushProperties->bShowFullSettings = false;

	SelectionProps = NewObject<UMeshSelectionToolProperties>(this);
	SelectionProps->RestoreProperties(this);
	AddToolPropertySource(SelectionProps);

	AddSubclassPropertySets();

	SelectionActions = NewObject<UMeshSelectionEditActions>(this);
	SelectionActions->Initialize(this);
	AddToolPropertySource(SelectionActions);

	EditActions = CreateEditActions();
	AddToolPropertySource(EditActions);

	// enable wireframe on component
	PreviewMesh->EnableWireframe(true);

	// disable shadows
	PreviewMesh->GetRootComponent()->bCastDynamicShadow = false;

	// configure secondary render material
	UMaterialInterface* SelectionMaterial = ToolSetupUtil::GetSelectionMaterial(FLinearColor(0.9f, 0.1f, 0.1f), GetToolManager());
	if (SelectionMaterial != nullptr)
	{
		PreviewMesh->SetSecondaryRenderMaterial(SelectionMaterial);
	}

	// enable secondary triangle buffers
	PreviewMesh->EnableSecondaryTriangleBuffers(
		[this](const FDynamicMesh3* Mesh, int32 TriangleID)
	{
		return SelectedTriangles[TriangleID] ? true : false;
	});

	const FDynamicMesh3* Mesh = PreviewMesh->GetPreviewDynamicMesh();
	SelectedVertices = TBitArray<>(false, Mesh->MaxVertexID());
	SelectedTriangles = TBitArray<>(false, Mesh->MaxTriangleID());

	// we could probably calculate this on-demand but we need to do it before making any mesh changes? or update?
	CacheUVIslandIDs();

	this->Selection = NewObject<UMeshSelectionSet>(this);
	Selection->GetOnModified().AddLambda([this](USelectionSet* SelectionObj)
	{
		OnExternalSelectionChange();
	});

	// rebuild octree if mesh changes
	PreviewMesh->GetOnMeshChanged().AddLambda([this]() { bOctreeValid = false; bFullMeshInvalidationPending = true; });

	ShowWireframeWatcher.Initialize(
		[this]() { return SelectionProps->bShowWireframe; },
		[this](bool bNewValue) { PreviewMesh->EnableWireframe(bNewValue); }, SelectionProps->bShowWireframe);

	ColorModeWatcher.Initialize(
		[this]() { return SelectionProps->FaceColorMode; },
		[this](EMeshFacesColorMode NewValue) { bColorsUpdatePending = true; UpdateVisualization(false); }, SelectionProps->FaceColorMode );
	bColorsUpdatePending = (SelectionProps->FaceColorMode != EMeshFacesColorMode::None);
	
}



UMeshSelectionToolActionPropertySet* UMeshSelectionTool::CreateEditActions()
{
	UMeshSelectionMeshEditActions* Actions = NewObject<UMeshSelectionMeshEditActions>(this);
	Actions->Initialize(this);
	return Actions;
}



void UMeshSelectionTool::OnShutdown(EToolShutdownType ShutdownType)
{
	SelectionProps->SaveProperties(this);

	if (bHaveModifiedMesh && ShutdownType == EToolShutdownType::Accept)
	{
		// this block bakes the modified DynamicMeshComponent back into the StaticMeshComponent inside an undo transaction
		GetToolManager()->BeginUndoTransaction(LOCTEXT("MeshSelectionToolTransactionName", "Edit Mesh"));

		ComponentTarget->CommitMesh([=](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
		{
			PreviewMesh->Bake(CommitParams.MeshDescription, true);
		});
		GetToolManager()->EndUndoTransaction();
	}
}




void UMeshSelectionTool::RegisterActions(FInteractiveToolActionSet& ActionSet)
{
	UDynamicMeshBrushTool::RegisterActions(ActionSet);

	ActionSet.RegisterAction(this, (int32)EStandardToolActions::BaseClientDefinedActionID + 1,
		TEXT("MeshSelectionToolDelete"),
		LOCTEXT("MeshSelectionToolDelete", "Delete"),
		LOCTEXT("MeshSelectionToolDeleteTooltip", "Delete Selected Elements"),
		EModifierKey::None, EKeys::Delete,
		[this]() { DeleteSelectedTriangles(); });


	ActionSet.RegisterAction(this, (int32)EStandardToolActions::ToggleWireframe,
		TEXT("ToggleWireframe"),
		LOCTEXT("ToggleWireframe", "Toggle Wireframe"),
		LOCTEXT("ToggleWireframeTooltip", "Toggle visibility of wireframe overlay"),
		EModifierKey::Alt, EKeys::W,
		[this]() { SelectionProps->bShowWireframe = !SelectionProps->bShowWireframe; });
}



void UMeshSelectionTool::OnExternalSelectionChange()
{
	SelectedVertices.SetRange(0, SelectedVertices.Num(), false);
	SelectedTriangles.SetRange(0, SelectedTriangles.Num(), false);

	if (SelectionType == EMeshSelectionElementType::Vertex)
	{
		for (int VertIdx : Selection->Vertices)
		{
			SelectedVertices[VertIdx] = true;
		}
	}
	else if (SelectionType == EMeshSelectionElementType::Face)
	{
		for (int FaceIdx : Selection->Faces)
		{
			SelectedTriangles[FaceIdx] = true;
		}
	}

	OnSelectionUpdated();
}




bool UMeshSelectionTool::HitTest(const FRay& Ray, FHitResult& OutHit)
{
	bool bHit = UDynamicMeshBrushTool::HitTest(Ray, OutHit);
	if (bHit && SelectionProps->bHitBackFaces == false)
	{
		const FDynamicMesh3* SourceMesh = PreviewMesh->GetPreviewDynamicMesh();
		FVector3d Normal, Centroid; 
		double Area;
		SourceMesh->GetTriInfo(OutHit.FaceIndex, Normal, Area, Centroid);
		FViewCameraState StateOut;
		GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(StateOut);
		FVector3d LocalEyePosition(ComponentTarget->GetWorldTransform().InverseTransformPosition(StateOut.Position));

		if (Normal.Dot((Centroid - LocalEyePosition)) > 0)
		{
			bHit = false;
		}
	}
	return bHit;
}


void UMeshSelectionTool::OnBeginDrag(const FRay& WorldRay)
{
	UDynamicMeshBrushTool::OnBeginDrag(WorldRay);

	PreviewBrushROI.Reset();
	if (IsInBrushStroke())
	{
		bInRemoveStroke = GetShiftToggle();
		BeginChange(bInRemoveStroke == false);
		StartStamp = UBaseBrushTool::LastBrushStamp;
		LastStamp = StartStamp;
		bStampPending = true;
	}
}



void UMeshSelectionTool::OnUpdateDrag(const FRay& WorldRay)
{
	UDynamicMeshBrushTool::OnUpdateDrag(WorldRay);
	if (IsInBrushStroke())
	{
		LastStamp = UBaseBrushTool::LastBrushStamp;
		bStampPending = true;
	}
}



TUniquePtr<FDynamicMeshOctree3>& UMeshSelectionTool::GetOctree()
{
	if (bOctreeValid == false)
	{
		Octree = MakeUnique<FDynamicMeshOctree3>();
		Octree->Initialize(PreviewMesh->GetPreviewDynamicMesh());
		bOctreeValid = true;
	}
	return Octree;
}



void UMeshSelectionTool::CalculateVertexROI(const FBrushStampData& Stamp, TArray<int>& VertexROI)
{
	FTransform Transform = ComponentTarget->GetWorldTransform();
	FVector StampPosLocal = Transform.InverseTransformPosition(Stamp.WorldPosition);

	// TODO: need dynamic vertex hash table!

	float RadiusSqr = CurrentBrushRadius * CurrentBrushRadius;
	const FDynamicMesh3* Mesh = PreviewMesh->GetPreviewDynamicMesh();
	for (int VertIdx : Mesh->VertexIndicesItr())
	{
		FVector3d Position = Mesh->GetVertex(VertIdx);
		if ((Position - StampPosLocal).SquaredLength() < RadiusSqr)
		{
			VertexROI.Add(VertIdx);
		}
	}
}




void UMeshSelectionTool::CalculateTriangleROI(const FBrushStampData& Stamp, TArray<int>& TriangleROI)
{
	FTransform3d Transform(ComponentTarget->GetWorldTransform());
	FVector3d StampPosLocal = Transform.InverseTransformPosition(Stamp.WorldPosition);

	// always select first triangle
	const FDynamicMesh3* Mesh = PreviewMesh->GetPreviewDynamicMesh();

	float RadiusSqr = CurrentBrushRadius * CurrentBrushRadius;
	if (SelectionProps->bVolumetricBrush)
	{
		if (Mesh->IsTriangle(Stamp.HitResult.FaceIndex))
		{
			TriangleROI.Add(Stamp.HitResult.FaceIndex);
		}

		FAxisAlignedBox3d Bounds(StampPosLocal-CurrentBrushRadius*FVector3d::One(), StampPosLocal+CurrentBrushRadius*FVector3d::One());
		TemporaryBuffer.Reset();
		GetOctree()->RangeQuery(Bounds, TemporaryBuffer);

		for (int32 TriIdx : TemporaryBuffer)
		{
			FVector3d Position = Mesh->GetTriCentroid(TriIdx);
			if ((Position - StampPosLocal).SquaredLength() < RadiusSqr)
			{
				TriangleROI.Add(TriIdx);
			}
		}
	}
	else
	{
		TArray<int32> StartROI;
		StartROI.Add(Stamp.HitResult.FaceIndex);
		FMeshConnectedComponents::GrowToConnectedTriangles(Mesh, StartROI, TriangleROI,  &TemporaryBuffer, &TemporarySet,
			[Mesh, RadiusSqr, StampPosLocal](int t1, int t2) { return (Mesh->GetTriCentroid(t2) - StampPosLocal).SquaredLength() < RadiusSqr; });

	}


}




static void UpdateList(TArray<int>& List, int Value, bool bAdd)
{
	if (bAdd)
	{
		List.Add(Value);
	}
	else
	{
		List.RemoveSwap(Value);
	}
}


void UMeshSelectionTool::ApplyStamp(const FBrushStampData& Stamp)
{
	//const FDynamicMesh3* Mesh = PreviewMesh->GetPreviewDynamicMesh();

	IndexBuf.Reset();

	bool bDesiredValue = bInRemoveStroke ? false : true;

	if (SelectionType == EMeshSelectionElementType::Face)
	{
		CalculateTriangleROI(Stamp, IndexBuf);
		UpdateFaceSelection(Stamp, IndexBuf);
	}
	else
	{
		CalculateVertexROI(Stamp, IndexBuf);
		for (int VertIdx : IndexBuf)
		{
			if (SelectedVertices[VertIdx] != bDesiredValue)
			{
				SelectedVertices[VertIdx] = bDesiredValue;
				UpdateList(Selection->Vertices, VertIdx, bDesiredValue);
				if (ActiveSelectionChange != nullptr)
				{
					ActiveSelectionChange->Add(VertIdx);
				}
			}
		}
	}

	OnSelectionUpdated();
}






void UMeshSelectionTool::UpdateFaceSelection(const FBrushStampData& Stamp, const TArray<int>& TriangleROI)
{
	const FDynamicMesh3* Mesh = PreviewMesh->GetPreviewDynamicMesh();
	const TArray<int>* UseROI = &TriangleROI;

	TArray<int> LocalROI;
	if (SelectionProps->SelectionMode == EMeshSelectionToolPrimaryMode::AllConnected)
	{
		FMeshConnectedComponents::GrowToConnectedTriangles(Mesh, TriangleROI, LocalROI, &TemporaryBuffer, &TemporarySet);
		UseROI = &LocalROI;
	}
	else if (SelectionProps->SelectionMode == EMeshSelectionToolPrimaryMode::AllInGroup)
	{
		FMeshConnectedComponents::GrowToConnectedTriangles(Mesh, TriangleROI, LocalROI, &TemporaryBuffer, &TemporarySet,
			[Mesh](int t1, int t2) { return Mesh->GetTriangleGroup(t1) == Mesh->GetTriangleGroup(t2); } );
		UseROI = &LocalROI;
	}
	else if (SelectionProps->SelectionMode == EMeshSelectionToolPrimaryMode::ByMaterial)
	{
		const FDynamicMeshMaterialAttribute* MaterialIDs = Mesh->Attributes()->GetMaterialID();
		TArray<int32> StartROI;
		StartROI.Add(Stamp.HitResult.FaceIndex);
		FMeshConnectedComponents::GrowToConnectedTriangles(Mesh, StartROI, LocalROI, &TemporaryBuffer, &TemporarySet,
			[Mesh, MaterialIDs](int t1, int t2) { return MaterialIDs->GetValue(t1) == MaterialIDs->GetValue(t2); });
		UseROI = &LocalROI;
	}
	else if (SelectionProps->SelectionMode == EMeshSelectionToolPrimaryMode::ByUVIsland)
	{
		const FDynamicMeshMaterialAttribute* MaterialIDs = Mesh->Attributes()->GetMaterialID();
		TArray<int32> StartROI;
		StartROI.Add(Stamp.HitResult.FaceIndex);
		FMeshConnectedComponents::GrowToConnectedTriangles(Mesh, StartROI, LocalROI, &TemporaryBuffer, &TemporarySet,
			[&](int t1, int t2) { return TriangleToUVIsland[t1] == TriangleToUVIsland[t2]; });
		UseROI = &LocalROI;
	}
	else if (SelectionProps->SelectionMode == EMeshSelectionToolPrimaryMode::AllWithinAngle)
	{
		TArray<int32> StartROI; 
		StartROI.Add(Stamp.HitResult.FaceIndex);
		FVector3d StartNormal = Mesh->GetTriNormal(StartROI[0]);
		int AngleTol = SelectionProps->AngleTolerance;
		FMeshConnectedComponents::GrowToConnectedTriangles(Mesh, StartROI, LocalROI, &TemporaryBuffer, &TemporarySet,
			[Mesh, AngleTol, StartNormal](int t1, int t2) { return Mesh->GetTriNormal(t2).AngleD(StartNormal) < AngleTol; });
		UseROI = &LocalROI;
	}
	else if (SelectionProps->SelectionMode == EMeshSelectionToolPrimaryMode::AngleFiltered)
	{
		TSet<int32> BrushROI(TriangleROI);
		TArray<int32> StartROI;
		StartROI.Add(Stamp.HitResult.FaceIndex);
		FVector3d StartNormal = Mesh->GetTriNormal(StartROI[0]);
		int AngleTol = SelectionProps->AngleTolerance;
		FMeshConnectedComponents::GrowToConnectedTriangles(Mesh, StartROI, LocalROI, &TemporaryBuffer, &TemporarySet,
			[Mesh, AngleTol, StartNormal, &BrushROI](int t1, int t2) { return BrushROI.Contains(t2) && Mesh->GetTriNormal(t2).AngleD(StartNormal) < AngleTol; });
		UseROI = &LocalROI;
	}
	else if (SelectionProps->SelectionMode == EMeshSelectionToolPrimaryMode::Visible)
	{
		FViewCameraState StateOut;
		GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(StateOut);
		FVector3d LocalEyePosition(ComponentTarget->GetWorldTransform().InverseTransformPosition(StateOut.Position));

		for (int tid : TriangleROI)
		{
			FVector3d Centroid = Mesh->GetTriCentroid(tid);
			int HitTID = GetOctree()->FindNearestHitObject(FRay3d(LocalEyePosition, (Centroid - LocalEyePosition).Normalized()));
			if (HitTID == tid)
			{
				LocalROI.Add(HitTID);
			}
		}
		UseROI = &LocalROI;
	}

	bool bDesiredValue = bInRemoveStroke ? false : true;
	for (int TriIdx : *UseROI)
	{
		if (SelectedTriangles[TriIdx] != bDesiredValue)
		{
			SelectedTriangles[TriIdx] = bDesiredValue;
			UpdateList(Selection->Faces, TriIdx, bDesiredValue);
			if (ActiveSelectionChange != nullptr)
			{
				ActiveSelectionChange->Add(TriIdx);
			}
		}
	}

}




void UMeshSelectionTool::OnEndDrag(const FRay& Ray)
{
	UDynamicMeshBrushTool::OnEndDrag(Ray);

	bInRemoveStroke = false;
	bStampPending = false;

	// close change record
	TUniquePtr<FMeshSelectionChange> Change = EndChange();
	GetToolManager()->EmitObjectChange(Selection, MoveTemp(Change), LOCTEXT("MeshSelectionChange", "Mesh Selection"));
}


bool UMeshSelectionTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	UDynamicMeshBrushTool::OnUpdateHover(DevicePos);

	// todo get rid of this redundant hit test!
	FHitResult OutHit;
	if ( UDynamicMeshBrushTool::HitTest(DevicePos.WorldRay, OutHit) )
	{
		PreviewBrushROI.Reset();
		if (SelectionType == EMeshSelectionElementType::Face)
		{
			CalculateTriangleROI(LastBrushStamp, PreviewBrushROI);
		}
		else
		{
			CalculateVertexROI(LastBrushStamp, PreviewBrushROI);
		}
	}

	return true;
}






void UMeshSelectionTool::OnSelectionUpdated()
{
	UpdateVisualization(true);
}

void UMeshSelectionTool::UpdateVisualization(bool bSelectionModified)
{
	check(SelectionType == EMeshSelectionElementType::Face);  // only face selection supported so far

	bFullMeshInvalidationPending = false;

	// force an update of renderbuffers
	if (bSelectionModified)
	{
		PreviewMesh->NotifyDeferredEditCompleted(UPreviewMesh::ERenderUpdateMode::FullUpdate, true);
	}

	if (bColorsUpdatePending)
	{
		if (SelectionProps->FaceColorMode != EMeshFacesColorMode::None)
		{
			PreviewMesh->SetOverrideRenderMaterial(ToolSetupUtil::GetSelectionMaterial(GetToolManager()));
			PreviewMesh->SetTriangleColorFunction([this](const FDynamicMesh3* Mesh, int TriangleID)
			{
				return GetCurrentFaceColor(Mesh, TriangleID);
			}, 
			UPreviewMesh::ERenderUpdateMode::FastUpdate);
		}
		else
		{
			PreviewMesh->ClearOverrideRenderMaterial();
			PreviewMesh->ClearTriangleColorFunction(UPreviewMesh::ERenderUpdateMode::FastUpdate);
		}

		bColorsUpdatePending = false;
	}
}



FColor UMeshSelectionTool::GetCurrentFaceColor(const FDynamicMesh3* Mesh, int TriangleID)
{
	if (SelectionProps->FaceColorMode == EMeshFacesColorMode::ByGroup)
	{
		return LinearColors::SelectFColor(Mesh->GetTriangleGroup(TriangleID));
	}
	else if (SelectionProps->FaceColorMode == EMeshFacesColorMode::ByMaterialID)
	{
		return LinearColors::SelectFColor( Mesh->Attributes()->GetMaterialID()->GetValue(TriangleID) );
	}
	else if (SelectionProps->FaceColorMode == EMeshFacesColorMode::ByUVIsland)
	{
		return LinearColors::SelectFColor(TriangleToUVIsland[TriangleID]);
	}
	return FColor::Red;

}


void UMeshSelectionTool::CacheUVIslandIDs()
{
	const FDynamicMesh3* Mesh = PreviewMesh->GetMesh();
	FMeshConnectedComponents Components(Mesh);

	TriangleToUVIsland.SetNum(Mesh->MaxTriangleID());

	const FDynamicMeshUVOverlay* UV = Mesh->Attributes()->GetUVLayer(0);

	Components.FindConnectedTriangles([&](int32 TriIdx0, int32 TriIdx1)
	{
		return UV->AreTrianglesConnected(TriIdx0, TriIdx1);
	});

	int32 NumComponents = Components.Num();
	for (int32 ci = 0; ci < NumComponents; ++ci)
	{
		for (int32 TriIdx : Components.GetComponent(ci).Indices)
		{
			TriangleToUVIsland[TriIdx] = ci;
		}
	}
}


void UMeshSelectionTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	UDynamicMeshBrushTool::Render(RenderAPI);

	FTransform WorldTransform = ComponentTarget->GetWorldTransform();
	const FDynamicMesh3* Mesh = PreviewMesh->GetMesh();

	if (SelectionType == EMeshSelectionElementType::Vertex)
	{
		MeshDebugDraw::DrawVertices(Mesh, Selection->Vertices,
			12.0f, FColor::Orange, RenderAPI->GetPrimitiveDrawInterface(), WorldTransform);
		MeshDebugDraw::DrawVertices(Mesh, PreviewBrushROI,
			8.0f, FColor(40, 200, 40), RenderAPI->GetPrimitiveDrawInterface(), WorldTransform);
	}
	else
	{
		// drawn via material
		//MeshDebugDraw::DrawTriCentroids(Mesh, Selection->Faces,
		//	12.0f, FColor::Green, RenderAPI->GetPrimitiveDrawInterface(), WorldTransform);
		MeshDebugDraw::DrawTriCentroids(Mesh, PreviewBrushROI,
			4.0f, FColor(40, 200, 40), RenderAPI->GetPrimitiveDrawInterface(), WorldTransform);
	}
}


void UMeshSelectionTool::Tick(float DeltaTime)

{
	UDynamicMeshBrushTool::Tick(DeltaTime);

	ShowWireframeWatcher.CheckAndUpdate();
	ColorModeWatcher.CheckAndUpdate();

	if (bStampPending)
	{
		ApplyStamp(LastStamp);
		bStampPending = false;
	}

	if (bHavePendingAction)
	{
		ApplyAction(PendingAction);
		bHavePendingAction = false;
		PendingAction = EMeshSelectionToolActions::NoAction;
	}
}



void UMeshSelectionTool::BeginChange(bool bAdding)
{
	check(ActiveSelectionChange == nullptr);
	ActiveSelectionChange = new FMeshSelectionChangeBuilder(SelectionType, bAdding);
}

void UMeshSelectionTool::CancelChange()
{
	if (ActiveSelectionChange != nullptr)
	{
		delete ActiveSelectionChange;
		ActiveSelectionChange = nullptr;
	}
}

TUniquePtr<FMeshSelectionChange> UMeshSelectionTool::EndChange()
{
	check(ActiveSelectionChange);
	if (ActiveSelectionChange != nullptr)
	{
		TUniquePtr<FMeshSelectionChange> Result = MoveTemp(ActiveSelectionChange->Change);
		delete ActiveSelectionChange;
		ActiveSelectionChange = nullptr;

		return Result;
	}
	return TUniquePtr<FMeshSelectionChange>();
}






void UMeshSelectionTool::RequestAction(EMeshSelectionToolActions ActionType)
{
	if (bHavePendingAction)
	{
		return;
	}

	PendingAction = ActionType;
	bHavePendingAction = true;
}


void UMeshSelectionTool::ApplyAction(EMeshSelectionToolActions ActionType)
{
	switch (ActionType)
	{
		case EMeshSelectionToolActions::ClearSelection:
			ClearSelection();
			break;

		case EMeshSelectionToolActions::InvertSelection:
			InvertSelection();
			break;


		case EMeshSelectionToolActions::GrowSelection:
			GrowShrinkSelection(true);
			break;

		case EMeshSelectionToolActions::ShrinkSelection:
			GrowShrinkSelection(false);
			break;


		case EMeshSelectionToolActions::ExpandToConnected:
			ExpandToConnected();
			break;

		case EMeshSelectionToolActions::DeleteSelected:
			DeleteSelectedTriangles();
			break;

		case EMeshSelectionToolActions::DisconnectSelected:
			DisconnectSelectedTriangles();
			break;

		case EMeshSelectionToolActions::SeparateSelected:
			SeparateSelectedTriangles();
			break;

		case EMeshSelectionToolActions::FlipSelected:
			FlipSelectedTriangles();
			break;

		case EMeshSelectionToolActions::CreateGroup:
			AssignNewGroupToSelectedTriangles();
			break;
	}
}





void UMeshSelectionTool::ClearSelection()
{
	TArray<int32> SelectedFaces = Selection->GetElements(EMeshSelectionElementType::Face);
	if (SelectedFaces.Num() == 0)
	{
		return;
	}

	BeginChange(false);
	ActiveSelectionChange->Add(SelectedFaces);
	Selection->RemoveIndices(EMeshSelectionElementType::Face, SelectedFaces);

	TUniquePtr<FMeshSelectionChange> SelectionChange = EndChange();

	GetToolManager()->EmitObjectChange(Selection, MoveTemp(SelectionChange), LOCTEXT("ClearSelection", "Clear Selection"));

	OnExternalSelectionChange();
}




void UMeshSelectionTool::InvertSelection()
{
	check(SelectionType == EMeshSelectionElementType::Face);
	TArray<int32> SelectedFaces = Selection->GetElements(EMeshSelectionElementType::Face);
	if (SelectedFaces.Num() == 0)
	{
		return;
	}

	TArray<int32> InvertedFaces;
	const FDynamicMesh3* Mesh = PreviewMesh->GetPreviewDynamicMesh();
	for (int tid : Mesh->TriangleIndicesItr())
	{
		if (SelectedTriangles[tid] == false)
		{
			InvertedFaces.Add(tid);
		}
	}

	GetToolManager()->BeginUndoTransaction(LOCTEXT("InvertSelection", "Invert Selection"));

	// clear current selection
	BeginChange(false);
	ActiveSelectionChange->Add(SelectedFaces);
	Selection->RemoveIndices(EMeshSelectionElementType::Face, SelectedFaces);
	TUniquePtr<FMeshSelectionChange> ClearChange = EndChange();

	GetToolManager()->EmitObjectChange(Selection, MoveTemp(ClearChange), LOCTEXT("InvertSelection", "Invert Selection"));

	// add inverted selection
	BeginChange(true);
	ActiveSelectionChange->Add(InvertedFaces);
	Selection->AddIndices(EMeshSelectionElementType::Face, InvertedFaces);
	TUniquePtr<FMeshSelectionChange> AddChange = EndChange();

	GetToolManager()->EmitObjectChange(Selection, MoveTemp(AddChange), LOCTEXT("InvertSelection", "Invert Selection"));

	GetToolManager()->EndUndoTransaction();

	OnExternalSelectionChange();
}





void UMeshSelectionTool::GrowShrinkSelection(bool bGrow)
{
	check(SelectionType == EMeshSelectionElementType::Face);
	TArray<int32> SelectedFaces = Selection->GetElements(EMeshSelectionElementType::Face);
	if (SelectedFaces.Num() == 0)
	{
		return;
	}

	const FDynamicMesh3* Mesh = PreviewMesh->GetPreviewDynamicMesh();
	TArray<int32> Vertices;
	MeshIndexUtil::TriangleToVertexIDs(Mesh, SelectedFaces, Vertices);

	TSet<int32> ChangeFaces;
	for (int vid : Vertices)
	{
		int OutCount = 0;
		for (int tid : Mesh->VtxTrianglesItr(vid))
		{
			if (SelectedTriangles[tid] == false)
			{
				OutCount++;
			}
		}
		if (OutCount == 0)
		{
			continue;
		}

		for (int tid : Mesh->VtxTrianglesItr(vid))
		{
			if ( (bGrow && SelectedTriangles[tid] == false) || (bGrow == false && SelectedTriangles[tid]) )
			{
				ChangeFaces.Add(tid);
			}
		}
	}
	if (ChangeFaces.Num() == 0)
	{
		return;
	}

	BeginChange(bGrow);
	ActiveSelectionChange->Add(ChangeFaces);
	if (bGrow)
	{
		Selection->AddIndices(EMeshSelectionElementType::Face, ChangeFaces);
		TUniquePtr<FMeshSelectionChange> SelectionChange = EndChange();
		GetToolManager()->EmitObjectChange(Selection, MoveTemp(SelectionChange), LOCTEXT("GrowSelection", "Grow Selection"));
	}
	else
	{
		Selection->RemoveIndices(EMeshSelectionElementType::Face, ChangeFaces);
		TUniquePtr<FMeshSelectionChange> SelectionChange = EndChange();
		GetToolManager()->EmitObjectChange(Selection, MoveTemp(SelectionChange), LOCTEXT("ShrinkSelection", "Shrink Selection"));
	}
	OnExternalSelectionChange();
}





void UMeshSelectionTool::ExpandToConnected()
{
	check(SelectionType == EMeshSelectionElementType::Face);
	TArray<int32> SelectedFaces = Selection->GetElements(EMeshSelectionElementType::Face);
	if (SelectedFaces.Num() == 0)
	{
		return;
	}

	const FDynamicMesh3* Mesh = PreviewMesh->GetPreviewDynamicMesh();

	TArray<int32> Queue(SelectedFaces);
	TSet<int32> AddFaces;

	while (Queue.Num() > 0)
	{
		int32 CurTri = Queue.Pop(false);
		FIndex3i NbrTris = Mesh->GetTriNeighbourTris(CurTri);

		for (int j = 0; j < 3; ++j)
		{
			int32 tid = NbrTris[j];
			if (tid != FDynamicMesh3::InvalidID && SelectedTriangles[tid] == false && AddFaces.Contains(tid) == false)
			{
				AddFaces.Add(tid);
				Queue.Add(tid);
			}
		}
	}
	if (AddFaces.Num() == 0)
	{
		return;
	}

	BeginChange(true);
	ActiveSelectionChange->Add(AddFaces);
	Selection->AddIndices(EMeshSelectionElementType::Face, AddFaces);
	TUniquePtr<FMeshSelectionChange> SelectionChange = EndChange();
	GetToolManager()->EmitObjectChange(Selection, MoveTemp(SelectionChange), LOCTEXT("ExpandToConnected", "Expand Selection"));
	OnExternalSelectionChange();
}




void UMeshSelectionTool::DeleteSelectedTriangles()
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

	// delete triangles and emit delete triangles change
	TUniquePtr<FMeshChange> MeshChange = PreviewMesh->TrackedEditMesh(
		[&SelectedFaces](FDynamicMesh3& Mesh, FDynamicMeshChangeTracker& ChangeTracker)
	{
		FDynamicMeshEditor Editor(&Mesh);
		Editor.RemoveTriangles(SelectedFaces, true, [&ChangeTracker](int TriangleID) { ChangeTracker.SaveTriangle(TriangleID, true); });
	});
	ChangeSeq->AppendChange(PreviewMesh, MoveTemp(MeshChange));

	// emit combined change sequence
	GetToolManager()->EmitObjectChange(this, MoveTemp(ChangeSeq), LOCTEXT("MeshSelectionToolDeleteFaces", "Delete Faces"));

	bFullMeshInvalidationPending = true;
	OnExternalSelectionChange();
	bHaveModifiedMesh = true;
	bOctreeValid = false;
}



void AssignMaterial(AActor* ToActor, const TUniquePtr<FPrimitiveComponentTarget>& FromTarget)
{
	UMaterialInterface* Material = FromTarget->GetMaterial(0);
	if (!Material)
	{
		return;
	}

	//if (Cast<AStaticMeshActor>(ToActor) != nullptr)
	//{
	//	UStaticMeshComponent* Component = Cast<AStaticMeshActor>(ToActor)->GetStaticMeshComponent();
	//	if (Component)
	//	{
	//		Component->SetMaterial(0, Material);
	//	}
	//} 
	//else
	//{
		USceneComponent* Component = ToActor->GetRootComponent();
		if (Cast<UPrimitiveComponent>(Component) != nullptr)
		{
			Cast<UPrimitiveComponent>(Component)->SetMaterial(0, Material);
		}
	//}
}


void UMeshSelectionTool::DisconnectSelectedTriangles()
{
	check(SelectionType == EMeshSelectionElementType::Face);
	TArray<int32> SelectedFaces = Selection->GetElements(EMeshSelectionElementType::Face);
	if (SelectedFaces.Num() == 0)
	{
		return;
	}

	TUniquePtr<FToolCommandChangeSequence> ChangeSeq = MakeUnique<FToolCommandChangeSequence>();

	// split out selected triangles and emit triangle change
	TUniquePtr<FMeshChange> MeshChange = PreviewMesh->TrackedEditMesh(
		[&SelectedFaces](FDynamicMesh3& Mesh, FDynamicMeshChangeTracker& ChangeTracker)
	{
		// save vertices and triangles that are on the boundary of the selection
		FMeshRegionBoundaryLoops BoundaryLoops(&Mesh, SelectedFaces);
		for (const FEdgeLoop& Loop : BoundaryLoops.Loops)
		{
			for (int VID : Loop.Vertices)
			{
				ChangeTracker.SaveVertex(VID);
				// include the whole one-ring in case the disconnect creates bowties that need to be split
				for (int TID : Mesh.VtxTrianglesItr(VID))
				{
					ChangeTracker.SaveTriangle(TID, true);
				}
			}
		}

		FDynamicMeshEditor Editor(&Mesh);
		Editor.DisconnectTriangles(SelectedFaces);
	});
	ChangeSeq->AppendChange(PreviewMesh, MoveTemp(MeshChange));

	// emit combined change sequence
	GetToolManager()->EmitObjectChange(this, MoveTemp(ChangeSeq), LOCTEXT("MeshSelectionToolDisconnectFaces", "Disconnect Faces"));

	bFullMeshInvalidationPending = true;
	bHaveModifiedMesh = true;
}



void UMeshSelectionTool::SeparateSelectedTriangles()
{
#if WITH_EDITOR
	// currently AssetGenerationUtil::GenerateStaticMeshActor only defined in editor

	check(SelectionType == EMeshSelectionElementType::Face);
	TArray<int32> SelectedFaces = Selection->GetElements(EMeshSelectionElementType::Face);
	if (SelectedFaces.Num() == 0)
	{
		return;
	}

	const FDynamicMesh3* SourceMesh = PreviewMesh->GetPreviewDynamicMesh();
	if (SelectedFaces.Num() == SourceMesh->TriangleCount())
	{
		return;		// don't separate entire mesh
	}


	// extract copy of triangles
	FDynamicMesh3 SeparatedMesh;
	SeparatedMesh.EnableAttributes();
	SeparatedMesh.Attributes()->EnableMatchingAttributes(*SourceMesh->Attributes());
	FDynamicMeshEditor Editor(&SeparatedMesh);
	FMeshIndexMappings Mappings; FDynamicMeshEditResult EditResult;
	Editor.AppendTriangles(SourceMesh, SelectedFaces, Mappings, EditResult);

	// emit new asset
	FTransform3d Transform(PreviewMesh->GetTransform());
	GetToolManager()->BeginUndoTransaction(LOCTEXT("MeshSelectionToolSeparate", "Separate"));
	AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
		AssetAPI, TargetWorld, &SeparatedMesh, Transform, TEXT("Submesh"),
		AssetGenerationUtil::GetDefaultAutoGeneratedAssetPath());
	AssignMaterial(NewActor, ComponentTarget);
	GetToolManager()->EndUndoTransaction();

	// todo: undo won't remove this asset...

	// delete selected triangles from this mesh
	DeleteSelectedTriangles();
#endif
}



void UMeshSelectionTool::FlipSelectedTriangles()
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

	// flip normals
	TUniquePtr<FMeshChange> MeshChange = PreviewMesh->TrackedEditMesh(
		[&SelectedFaces](FDynamicMesh3& Mesh, FDynamicMeshChangeTracker& ChangeTracker)
	{
		for (int TID : SelectedFaces)
		{
			ChangeTracker.SaveTriangle(TID, true);
		}
		FDynamicMeshEditor Editor(&Mesh);
		Editor.ReverseTriangleOrientations(SelectedFaces, true);
	});
	ChangeSeq->AppendChange(PreviewMesh, MoveTemp(MeshChange));

	// emit combined change sequence
	GetToolManager()->EmitObjectChange(this, MoveTemp(ChangeSeq), LOCTEXT("MeshSelectionToolFlipFaces", "Flip Face Orientations"));

	bHaveModifiedMesh = true;
}


void UMeshSelectionTool::AssignNewGroupToSelectedTriangles()
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

	// assign new groups to triangles
	// note: using an FMeshChange is kind of overkill here
	TUniquePtr<FMeshChange> MeshChange = PreviewMesh->TrackedEditMesh(
		[&SelectedFaces](FDynamicMesh3& Mesh, FDynamicMeshChangeTracker& ChangeTracker)
	{
		// each component gets its own group id
		FMeshConnectedComponents Components(&Mesh);
		Components.FindConnectedTriangles(SelectedFaces);

		for (FMeshConnectedComponents::FComponent& Component : Components)
		{
			int NewGroupID = Mesh.AllocateTriangleGroup();
			for (int tid : Component.Indices)
			{
				ChangeTracker.SaveTriangle(tid, true);
				Mesh.SetTriangleGroup(tid, NewGroupID);
			}
		}
	});
	ChangeSeq->AppendChange(PreviewMesh, MoveTemp(MeshChange));

	// emit combined change sequence
	GetToolManager()->EmitObjectChange(this, MoveTemp(ChangeSeq), LOCTEXT("MeshSelectionToolCreateGroup", "Create Polygroup"));

	OnExternalSelectionChange();
	bHaveModifiedMesh = true;
}





#undef LOCTEXT_NAMESPACE
