// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MeshSelectionTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "Drawing/MeshDebugDrawing.h"
#include "DynamicMeshEditor.h"
#include "DynamicMeshChangeTracker.h"
#include "Changes/MeshChange.h"
#include "AssetGenerationUtil.h"

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

	// use ourself as tool prop source for actions
	AddToolPropertySource(this);

	// enable wireframe on component
	PreviewMesh->EnableWireframe(true);

	// set vertex color material on base component so we can see selection
	UMaterialInterface* VertexColorMat = 
		GetToolManager()->GetContextQueriesAPI()->GetStandardMaterial(EStandardToolContextMaterials::VertexColorMaterial);
	if (VertexColorMat != nullptr)
	{
		PreviewMesh->SetMaterial(VertexColorMat);
	}

	const FDynamicMesh3* Mesh = PreviewMesh->GetPreviewDynamicMesh();
	SelectedVertices = TBitArray<>(false, Mesh->MaxVertexID());
	SelectedTriangles = TBitArray<>(false, Mesh->MaxTriangleID());

	this->Selection = NewObject<UMeshSelectionSet>(this);
	Selection->GetOnModified().AddLambda([this](USelectionSet* SelectionObj)
	{
		OnExternalSelectionChange();
	});

}




void UMeshSelectionTool::OnShutdown(EToolShutdownType ShutdownType)
{
	if (bHaveModifiedMesh && ShutdownType == EToolShutdownType::Accept)
	{
		// this block bakes the modified DynamicMeshComponent back into the StaticMeshComponent inside an undo transaction
		GetToolManager()->BeginUndoTransaction(LOCTEXT("MeshSelectionToolTransactionName", "Edit Mesh"));

		ComponentTarget->CommitMesh([=](FMeshDescription* MeshDescription)
		{
			PreviewMesh->Bake(MeshDescription, true);
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
	FTransform Transform = ComponentTarget->GetWorldTransform();
	FVector StampPosLocal = Transform.InverseTransformPosition(Stamp.WorldPosition);

	// always select first triangle
	const FDynamicMesh3* Mesh = PreviewMesh->GetPreviewDynamicMesh();
	if (Mesh->IsTriangle(Stamp.HitResult.FaceIndex))
	{
		TriangleROI.Add(Stamp.HitResult.FaceIndex);
	}

	float RadiusSqr = CurrentBrushRadius * CurrentBrushRadius;
	for (int TriIdx : Mesh->TriangleIndicesItr())
	{
		FVector3d Position = Mesh->GetTriCentroid(TriIdx);
		if ((Position - StampPosLocal).SquaredLength() < RadiusSqr)
		{
			TriangleROI.Add(TriIdx);
		}
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
		for (int TriIdx : IndexBuf)
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




void UMeshSelectionTool::OnEndDrag(const FRay& Ray)
{
	UDynamicMeshBrushTool::OnEndDrag(Ray);

	bInRemoveStroke = false;
	bStampPending = false;

	// close change record
	TUniquePtr<FMeshSelectionChange> Change = EndChange();
	GetToolManager()->EmitObjectChange(Selection, MoveTemp(Change), LOCTEXT("MeshSelectionChange", "Mesh Selection"));
}


void UMeshSelectionTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
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
}






void UMeshSelectionTool::OnSelectionUpdated()
{
	UpdateVisualization();
}

void UMeshSelectionTool::UpdateVisualization()
{
	if (SelectionType == EMeshSelectionElementType::Face)
	{
		PreviewMesh->SetTriangleColorFunction([this](int TriangleID)
		{
			return SelectedTriangles[TriangleID] ? FColor::Red : FColor::White;
		}, UPreviewMesh::ERenderUpdateMode::FullUpdate);

		//bool bIsFirst = (! BaseMeshComponent->TriangleColorFunc);
		//BaseMeshComponent->TriangleColorFunc =
		//if (bIsFirst)
		//{
		//	BaseMeshComponent->NotifyMeshUpdated();
		//}
		//else
		//{
		//	BaseMeshComponent->NotifyMeshUpdated();
		//	//BaseMeshComponent->FastNotifyColorsUpdated();
		//}
	}
	else
	{
		PreviewMesh->ClearTriangleColorFunction(UPreviewMesh::ERenderUpdateMode::FullUpdate);
	}
}




void UMeshSelectionTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	UDynamicMeshBrushTool::Render(RenderAPI);

	FTransform WorldTransform = ComponentTarget->GetWorldTransform();
	const FDynamicMesh3* Mesh = PreviewMesh->GetPreviewDynamicMesh();

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
		MeshDebugDraw::DrawTriCentroids(Mesh, Selection->Faces,
			12.0f, FColor::Green, RenderAPI->GetPrimitiveDrawInterface(), WorldTransform);
		MeshDebugDraw::DrawTriCentroids(Mesh, PreviewBrushROI,
			8.0f, FColor(40, 200, 40), RenderAPI->GetPrimitiveDrawInterface(), WorldTransform);
	}
}


void UMeshSelectionTool::Tick(float DeltaTime)
{
	UDynamicMeshBrushTool::Tick(DeltaTime);
	Indicators->Tick(DeltaTime);


	if (bStampPending)
	{
		ApplyStamp(LastStamp);
		bStampPending = false;
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

/**
 * FCommandChangeSequence contains a list of FCommandChanges and associated target UObjects.
 * The sequence of changes is applied atomically.
 * @warning if the target weak UObject pointers become invalid, those changes are skipped. This likely leaves this in an undefined state.
 */
class FCommandChangeSequence : public FCommandChange
{
protected:
	struct FChangeElem
	{
		TWeakObjectPtr<UObject> TargetObject;
		TUniquePtr<FCommandChange> Change;
	};

	TArray<TSharedPtr<FChangeElem>> Sequence;

public:
	FCommandChangeSequence()
	{
	}

	/** Add a change to the sequence */
	void AppendChange(UObject* Target, TUniquePtr<FCommandChange> Change)
	{
		TSharedPtr<FChangeElem> Elem = MakeShared<FChangeElem>();
		Elem->TargetObject = Target;
		Elem->Change = MoveTemp(Change);
		Sequence.Add(Elem);
	}
	
	/** Apply sequence of changes in-order */
	virtual void Apply(UObject* Object) override
	{
		for (int k = 0; k < Sequence.Num(); ++k)
		{
			TSharedPtr<FChangeElem> Elem = Sequence[k];
			check(Elem->TargetObject.IsValid());
			if (Elem->TargetObject.IsValid())
			{
				Elem->Change->Apply(Elem->TargetObject.Get());
			}
		}
	}

	/** Reverts sequence of changes in reverse-order */
	virtual void Revert(UObject* Object) override
	{
		for (int k = Sequence.Num()-1; k >= 0; --k)
		{
			TSharedPtr<FChangeElem> Elem = Sequence[k];
			check(Elem->TargetObject.IsValid());
			if (Elem->TargetObject.IsValid())
			{
				Elem->Change->Revert(Elem->TargetObject.Get());
			}
		}
	}

	/** @return string describing this change sequenece */
	virtual FString ToString() const override
	{
		FString Result = TEXT("FCommandChangeSequence: ");
		for (int k = 0; k < Sequence.Num(); ++k)
		{
			Result = Result + Sequence[k]->Change->ToString() + TEXT(" ");
		}
		return Result;
	}
};



void UMeshSelectionTool::DeleteSelectedTriangles()
{
	check(SelectionType == EMeshSelectionElementType::Face);
	TArray<int32> SelectedFaces = Selection->GetElements(EMeshSelectionElementType::Face);
	if (SelectedFaces.Num() == 0)
	{
		return;
	}

	TUniquePtr<FCommandChangeSequence> ChangeSeq = MakeUnique<FCommandChangeSequence>();

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
	GetToolManager()->EmitObjectChange(this, MoveTemp(ChangeSeq), LOCTEXT("MeshSelectionToolDelete", "Delete Faces"));

	OnSelectionUpdated();

	bHaveModifiedMesh = true;
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




#undef LOCTEXT_NAMESPACE