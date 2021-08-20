// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVSelectTool.h"

#include "BaseGizmos/TransformGizmo.h"
#include "ContextObjectStore.h"
#include "DynamicMesh/DynamicMeshChangeTracker.h"
#include "Drawing/LineSetComponent.h"
#include "Drawing/MeshElementsVisualizer.h"
#include "Drawing/PreviewGeometryActor.h"
#include "DynamicMesh/DynamicMeshChangeTracker.h"
#include "ToolTargets/UVEditorToolMeshInput.h"
#include "InteractiveToolManager.h"
#include "MeshOpPreviewHelpers.h" // UMeshOpPreviewWithBackgroundCompute
#include "PreviewMesh.h"
#include "Selection/MeshSelectionMechanic.h"
#include "Selection/DynamicMeshSelection.h"
#include "ToolSetupUtil.h"

#include "ToolTargetManager.h"

#define LOCTEXT_NAMESPACE "UUVSelectTool"

using namespace UE::Geometry;

namespace UVSelectToolLocals
{
	/**
	 * An undo/redo object for selection changes that, instead of operating directly on a selection
	 * mechanic, instead operates on a context object that tools can use to route the request
	 * to the current selection mechanic. This is valuable because we want the selection changes
	 * to be undoable in different invocations of the tool, and the selection mechanic pointer
	 * will not stay the same. However, the context object will stay the same, and we can register
	 * to its delegate on each invocation.
	 */
	class FSelectionChange : public FToolCommandChange
	{
	public:
		/**
		 * @param bBroadcastOnSelectionChangedIn Whether the change in selection should broadcast
		 *   OnSelectionChanged, which updates gizmo, etc.
		 * @param GizmoBeforeIn Only relevant if bBroadcastOnSelectionChangedIn is true. In that case,
		 *   the gizmo gets reset on the way forward to the current selection, which means we have to
		 *   reset it to the old orientation on the way back (otherwise a rotated gizmo would end up
		 *   losing its rotation on undo).
		 */
		FSelectionChange(const FDynamicMeshSelection& SelectionBeforeIn,
			const FDynamicMeshSelection& SelectionAfterIn,
			bool bBroadcastOnSelectionChangedIn,
			const FTransform& GizmoBeforeIn)
			: SelectionBefore(SelectionBeforeIn)
			, SelectionAfter(SelectionAfterIn)
			, bBroadcastOnSelectionChanged(bBroadcastOnSelectionChangedIn)
			, GizmoBefore(GizmoBeforeIn)
		{
		}

		virtual void Apply(UObject* Object) override
		{
			UUVSelectToolChangeRouter* ChangeRouter = Cast<UUVSelectToolChangeRouter>(Object);
			if (ensure(ChangeRouter) && ChangeRouter->CurrentSelectTool.IsValid())
			{
				ChangeRouter->CurrentSelectTool->SetSelection(SelectionAfter, bBroadcastOnSelectionChanged);
			}
		}

		virtual void Revert(UObject* Object) override
		{
			UUVSelectToolChangeRouter* ChangeRouter = Cast<UUVSelectToolChangeRouter>(Object);
			if (ensure(ChangeRouter) && ChangeRouter->CurrentSelectTool.IsValid())
			{
				ChangeRouter->CurrentSelectTool->SetSelection(SelectionBefore, bBroadcastOnSelectionChanged);
				if (bBroadcastOnSelectionChanged)
				{
					ChangeRouter->CurrentSelectTool->SetGizmoTransform(GizmoBefore);
				}
			}
		}

		virtual FString ToString() const override
		{
			return TEXT("UVSelectToolLocals::FSelectionChange");
		}

	protected:
		FDynamicMeshSelection SelectionBefore;
		FDynamicMeshSelection SelectionAfter;
		bool bBroadcastOnSelectionChanged;
		FTransform GizmoBefore;
	};

	/**
	 * A change that allows another change to be injected into it even after it is placed in the undo
	 * stack.
	 * NOTE: Remove this if we merge Transform into Select tool and choose not to store/restore
	 * selecitons in the Select tool.
	 */
	class FSpeculativeSelectionChange : public FToolCommandChange
	{
	public:
		FSpeculativeSelectionChange(TSharedPtr<TUniquePtr<FToolCommandChange>> PointerToContentIn)
			: PointerToContent(PointerToContentIn)
		{
		}

		virtual bool HasExpired(UObject* Object) const override
		{
			return !PointerToContent->IsValid();
		}

		virtual void Apply(UObject* Object) override
		{
			if (*PointerToContent)
			{
				(*PointerToContent)->Apply(Object);
			}
		}

		virtual void Revert(UObject* Object) override
		{
			if (*PointerToContent)
			{
				(*PointerToContent)->Revert(Object);
			}
		}

		virtual FString ToString() const override
		{
			return TEXT("UVSelectToolLocals::FSpeculativeSelectionChange");
		}

		TSharedPtr<TUniquePtr<FToolCommandChange>> PointerToContent;
	};

	/**
	 * A change similar to the one emitted by EmitChangeApi->EmitToolIndependentUnwrapCanonicalChange,
	 * but which updates the Select tool's gizmo in a way that preserves the rotational component
	 * (which would be lost if we just updated the gizmo from the current selection on undo/redo).
	 * 
	 * There is some built-in change tracking for the gizmo component in our transform gizmo, but 
	 * due to the order in which changes get emitted, there is not a good way to make sure that we
	 * update the selection mechanic (which needs to know the gizmo transform) at the correct time
	 * relative to those built-in changes. So, those built-in changes are actually wasted on us,
	 * but it was not easy to deactivate them because the change emitter is linked to the transform
	 * proxy...
	 *
	 * Expects UUVSelectToolChangeRouter to be the passed-in object
	 */
	class  FGizmoMeshChange : public FToolCommandChange
	{
	public:
		FGizmoMeshChange(UUVEditorToolMeshInput* UVToolInputObjectIn,
			TUniquePtr<UE::Geometry::FDynamicMeshChange> UnwrapCanonicalMeshChangeIn,
			const FTransform& GizmoBeforeIn, const FTransform& GizmoAfterIn)

			: UVToolInputObject(UVToolInputObjectIn)
			, UnwrapCanonicalMeshChange(MoveTemp(UnwrapCanonicalMeshChangeIn))
			, GizmoBefore(GizmoBeforeIn)
			, GizmoAfter(GizmoAfterIn)
		{
			ensure(UVToolInputObjectIn);
			ensure(UnwrapCanonicalMeshChange);
		};

		virtual void Apply(UObject* Object) override
		{
			UnwrapCanonicalMeshChange->Apply(UVToolInputObject->UnwrapCanonical.Get(), false);
			UVToolInputObject->UpdateFromCanonicalUnwrapUsingMeshChange(*UnwrapCanonicalMeshChange);
			
			// This is a little wasteful because we're going to reset the gizmo transform, 
			// but it updates the AABBTree for us.
			UVToolInputObject->OnUndoRedo.Broadcast(false);

			UUVSelectToolChangeRouter* ChangeRouter = Cast<UUVSelectToolChangeRouter>(Object);
			if (ensure(ChangeRouter) && ChangeRouter->CurrentSelectTool.IsValid())
			{
				ChangeRouter->CurrentSelectTool->SetGizmoTransform(GizmoAfter);
			}

		}

		virtual void Revert(UObject* Object) override
		{
			UnwrapCanonicalMeshChange->Apply(UVToolInputObject->UnwrapCanonical.Get(), true);
			UVToolInputObject->UpdateFromCanonicalUnwrapUsingMeshChange(*UnwrapCanonicalMeshChange);
			
			// This is a little wasteful because we're going to reset the gizmo transform, 
			// but it updates the AABBTree for us.
			UVToolInputObject->OnUndoRedo.Broadcast(false);

			UUVSelectToolChangeRouter* ChangeRouter = Cast<UUVSelectToolChangeRouter>(Object);
			if (ensure(ChangeRouter) && ChangeRouter->CurrentSelectTool.IsValid())
			{
				ChangeRouter->CurrentSelectTool->SetGizmoTransform(GizmoBefore);
			}
		}

		virtual bool HasExpired(UObject* Object) const override
		{
			return !(UVToolInputObject.IsValid() && UnwrapCanonicalMeshChange);
		}


		virtual FString ToString() const override
		{
			return TEXT("UVSelectToolLocals::FGizmoMeshChange");
		}

	protected:
		TWeakObjectPtr<UUVEditorToolMeshInput> UVToolInputObject;
		TUniquePtr<UE::Geometry::FDynamicMeshChange> UnwrapCanonicalMeshChange;
		FTransform GizmoBefore;
		FTransform GizmoAfter;
	};
}

void UUVSelectToolSpeculativeChangeAPI::EmitSpeculativeChange(UObject* TargetObject, 
	UUVToolEmitChangeAPI* EmitChangeAPI, const FText& TransactionName)
{
	ContentOfLastSpeculativeChange = MakeShared<TUniquePtr<FToolCommandChange>>();

	EmitChangeAPI->EmitToolIndependentChange(TargetObject, 
		MakeUnique<UVSelectToolLocals::FSpeculativeSelectionChange>(ContentOfLastSpeculativeChange), 
		TransactionName);
}

void UUVSelectToolSpeculativeChangeAPI::InsertIntoLastSpeculativeChange(TUniquePtr<FToolCommandChange> ChangeToInsert)
{
	if (ContentOfLastSpeculativeChange)
	{
		*ContentOfLastSpeculativeChange = MoveTemp(ChangeToInsert);
	}
}


/*
 * ToolBuilder
 */

const FToolTargetTypeRequirements& UUVSelectToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements(UUVEditorToolMeshInput::StaticClass());
	return TypeRequirements;
}

bool UUVSelectToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return Targets && Targets->Num() > 0;
}

UInteractiveTool* UUVSelectToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UUVSelectTool* NewTool = NewObject<UUVSelectTool>(SceneState.ToolManager);
	NewTool->SetWorld(SceneState.World);
	NewTool->SetGizmoEnabled(bGizmoEnabled);
	NewTool->SetTargets(*Targets);

	return NewTool;
}

void UUVSelectTool::Setup()
{
	check(Targets.Num() > 0);

	UInteractiveTool::Setup();

	SetToolDisplayName(LOCTEXT("ToolName", "UV Select Tool"));

	Settings = NewObject<UUVSelectToolProperties>(this);
	Settings->RestoreProperties(this);
	AddToolPropertySource(Settings);

	UContextObjectStore* ContextStore = GetToolManager()->GetContextObjectStore();
	EmitChangeAPI = ContextStore->FindContext<UUVToolEmitChangeAPI>();

	SelectionMechanic = NewObject<UMeshSelectionMechanic>();
	SelectionMechanic->Setup(this);
	SelectionMechanic->SetWorld(Targets[0]->UnwrapPreview->GetWorld());
	SelectionMechanic->OnSelectionChanged.AddUObject(this, &UUVSelectTool::OnSelectionChanged);

	// Make it so that our selection mechanic creates undo/redo transactions that go to a selection
	// change router, which we use to route to the current selection mechanic on each tool invocation.
	ChangeRouter = ContextStore->FindContext<UUVSelectToolChangeRouter>();
	if (!ChangeRouter)
	{
		ChangeRouter = NewObject<UUVSelectToolChangeRouter>();
		ContextStore->AddContextObject(ChangeRouter);
	}
	ChangeRouter->CurrentSelectTool = this;

	SelectionMechanic->EmitSelectionChange = [this](const FDynamicMeshSelection& OldSelection,
		const FDynamicMeshSelection& NewSelection, bool bBroadcastOnSelectionChangedIn)
	{
		EmitChangeAPI->EmitToolIndependentChange(ChangeRouter, MakeUnique<UVSelectToolLocals::FSelectionChange>(
			OldSelection, NewSelection, bBroadcastOnSelectionChangedIn, TransformGizmo->GetGizmoTransform()), 
			LOCTEXT("SelectionChangeMessage", "Selection Change"));
	};

	ConfigureSelectionModeFromControls();

	// Retrieve cached AABB tree storage, or else set it up
	UUVToolAABBTreeStorage* TreeStore = ContextStore->FindContext<UUVToolAABBTreeStorage>();
	if (!TreeStore)
	{
		TreeStore = NewObject<UUVToolAABBTreeStorage>();
		ContextStore->AddContextObject(TreeStore);
	}

	// Initialize the AABB trees from cached values, or make new ones.
	for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
	{
		TSharedPtr<FDynamicMeshAABBTree3> Tree = TreeStore->Get(Target->UnwrapCanonical.Get());
		if (!Tree)
		{
			Tree = MakeShared<FDynamicMeshAABBTree3>();
			Tree->SetMesh(Target->UnwrapCanonical.Get());
			TreeStore->Set(Target->UnwrapCanonical.Get(), Tree);
		}
		if (!Tree->IsValid())
		{
			Tree->Build();
		}
		AABBTrees.Add(Tree);
	}

	// Add the spatial structures to the selection mechanic
	for (int32 i = 0; i < Targets.Num(); ++i)
	{
		SelectionMechanic->AddSpatial(AABBTrees[i],
			Targets[i]->UnwrapPreview->PreviewMesh->GetTransform());
	}

	// See if we have a stored selection
	UUVToolMeshSelection* SelectionStore = ContextStore->FindContext<UUVToolMeshSelection>();
	if (SelectionStore)
	{
		SelectionMechanic->SetSelection(*SelectionStore->Selection);
	}
	if (!SelectionMechanic->GetCurrentSelection().IsEmpty() && !SelectionMechanic->GetCurrentSelection().MatchesTimestamp())
	{
		// If we have an expired selection, then there must have been some tool that changed the
		// topology. We need to clear the selection in an undoable way, but that clear actually
		// needs to happen before the last invocation shutdown to be in the proper sequence in the 
		// undo stack.
		FDynamicMeshSelection NewSelection;
		UUVSelectToolSpeculativeChangeAPI* SpeculativeChangeAPI = ContextStore->FindContext<UUVSelectToolSpeculativeChangeAPI>();
		if (SpeculativeChangeAPI && SpeculativeChangeAPI->HasSpeculativeChange())
		{
			SpeculativeChangeAPI->InsertIntoLastSpeculativeChange(MakeUnique<UVSelectToolLocals::FSelectionChange>(
				SelectionMechanic->GetCurrentSelection(), NewSelection, false, FTransform()));
		}

		SelectionMechanic->SetSelection(NewSelection, false, false);
	}

	// Make sure that if we receive undo/redo events on the meshes, we update the tree structures
	// and the selection mechanic drawn elements. Note that we mainly have to worry about this
	// because the select tool is the default UV editor tool, and as such it can receive undo
	// transactions from other tools and from other select tool invocations. Other tools typically
	// only need to worry about their own transactions, since we undo other tool invocations before
	// we get to unrelated transactions, and we can't redo out of the default tool.
	for (int32 i = 0; i < Targets.Num(); ++i)
	{
		Targets[i]->OnUndoRedo.AddWeakLambda(this, [this, i](bool bRevert) {
			AABBTrees[i]->Build();
			UpdateGizmo();
			SelectionMechanic->RebuildDrawnElements(TransformGizmo->GetGizmoTransform());
			});
	}

	// Gizmo setup
	UInteractiveGizmoManager* GizmoManager = GetToolManager()->GetPairedGizmoManager();
	UTransformProxy* TransformProxy = NewObject<UTransformProxy>(this);
	TransformGizmo = GizmoManager->CreateCustomTransformGizmo(
		ETransformGizmoSubElements::TranslateAxisX | ETransformGizmoSubElements::TranslateAxisY | ETransformGizmoSubElements::TranslatePlaneXY
		| ETransformGizmoSubElements::ScaleAxisX | ETransformGizmoSubElements::ScaleAxisY | ETransformGizmoSubElements::ScalePlaneXY
		| ETransformGizmoSubElements::RotateAxisZ,
		this);
	TransformProxy->OnBeginTransformEdit.AddUObject(this, &UUVSelectTool::GizmoTransformStarted);
	TransformProxy->OnTransformChanged.AddUObject(this, &UUVSelectTool::GizmoTransformChanged);
	TransformProxy->OnEndTransformEdit.AddUObject(this, &UUVSelectTool::GizmoTransformEnded);

	// Always align gizmo to x and y axes
	TransformGizmo->bUseContextCoordinateSystem = false;
	TransformGizmo->SetActiveTarget(TransformProxy, GetToolManager());

	LivePreviewGeometryActor = Targets[0]->AppliedPreview->GetWorld()->SpawnActor<APreviewGeometryActor>(
		FVector::ZeroVector, FRotator(0, 0, 0), FActorSpawnParameters());
	LivePreviewLineSet = NewObject<ULineSetComponent>(LivePreviewGeometryActor);
	LivePreviewGeometryActor->SetRootComponent(LivePreviewLineSet);
	LivePreviewLineSet->RegisterComponent();
	LivePreviewLineSet->SetLineMaterial(ToolSetupUtil::GetDefaultLineComponentMaterial(
		GetToolManager(), /*bDepthTested*/ true));

	if (!SelectionMechanic->GetCurrentSelection().IsEmpty())
	{
		OnSelectionChanged();
	}
	UpdateGizmo();
}

void UUVSelectTool::Shutdown(EToolShutdownType ShutdownType)
{
	UContextObjectStore* ContextStore = GetToolManager()->GetContextObjectStore();
	if (!SelectionMechanic->GetCurrentSelection().IsEmpty())
	{
		// TODO: It's not clear whether storing our selection is the right choice in the long run,
		// though we currently need it while the gizmo transformation is a separate tool... 
		UUVToolMeshSelection* SelectionStore = ContextStore->FindContext<UUVToolMeshSelection>();
		if (!SelectionStore)
		{
			SelectionStore = NewObject<UUVToolMeshSelection>();
			ContextStore->AddContextObject(SelectionStore);
		}
		*SelectionStore->Selection = SelectionMechanic->GetCurrentSelection();

		// We update the topology timestamp here because the timestamps may have divereged due
		// to our undo/redo change implementation sometimes incrementing the topology timestamp
		// even when the topology hasn't changed.
		// TODO: Check whether we still need this once we use FMeshVertexChange instead of FDynamicMeshChange
		SelectionStore->Selection->TopologyTimestamp = SelectionStore->Selection->Mesh->GetTopologyTimestamp();

		// Don't issue a transaciton if we're cancelling (currently only possible via undo out
		// of the "Transform" tool). Otherwise, issue a speculative change so that we can clear
		// the selection if the mesh topology turns out to have changed by the time we run again.
		if (ShutdownType != EToolShutdownType::Cancel)
		{
			UUVSelectToolSpeculativeChangeAPI* SpeculativeChangeAPI = ContextStore->FindContext<UUVSelectToolSpeculativeChangeAPI>();
			if (!SpeculativeChangeAPI)
			{
				SpeculativeChangeAPI = NewObject<UUVSelectToolSpeculativeChangeAPI>();
				ContextStore->AddContextObject(SpeculativeChangeAPI);
			}
			SpeculativeChangeAPI->EmitSpeculativeChange(ChangeRouter, EmitChangeAPI,
				LOCTEXT("SpeculativeSlectionChangeName", "End Select Tool"));
		}
	}
	else
	{
		ContextStore->RemoveContextObjectsOfType<UUVToolMeshSelection>();
	}

	ChangeRouter->CurrentSelectTool = nullptr;

	for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
	{
		Target->OnUndoRedo.RemoveAll(this);
	}

	Settings->SaveProperties(this);

	SelectionMechanic->Shutdown();

	if (LivePreviewGeometryActor)
	{
		LivePreviewGeometryActor->Destroy();
		LivePreviewGeometryActor = nullptr;
	}

	// Calls shutdown on gizmo and destroys it.
	GetToolManager()->GetPairedGizmoManager()->DestroyAllGizmosByOwner(this);

	EmitChangeAPI = nullptr;
	ChangeRouter = nullptr;
}

void UUVSelectTool::SetSelection(const UE::Geometry::FDynamicMeshSelection& NewSelection, bool bBroadcastOnSelectionChanged)
{
	SelectionMechanic->SetSelection(NewSelection, bBroadcastOnSelectionChanged, false);
}

void UUVSelectTool::SetGizmoTransform(const FTransform& NewTransform)
{
	TransformGizmo->ReinitializeGizmoTransform(NewTransform);
	SelectionMechanic->RebuildDrawnElements(NewTransform);
}

void UUVSelectTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	ConfigureSelectionModeFromControls();
}

void UUVSelectTool::UpdateGizmo()
{
	const FDynamicMeshSelection& Selection = SelectionMechanic->GetCurrentSelection();

	if (!Selection.IsEmpty())
	{
		FVector3d Centroid = SelectionMechanic->GetCurrentSelectionCentroid();

		TransformGizmo->ReinitializeGizmoTransform(FTransform((FVector)Centroid));
	}

	TransformGizmo->SetVisibility(bGizmoEnabled && !SelectionMechanic->GetCurrentSelection().IsEmpty());
}

void UUVSelectTool::ConfigureSelectionModeFromControls()
{
	switch (Settings->SelectionMode)
	{
	case EUVSelectToolSelectionMode::Island:
		SelectionMechanic->SelectionMode = EMeshSelectionMechanicMode::Component;
		break;
	case EUVSelectToolSelectionMode::Edge:
		SelectionMechanic->SelectionMode = EMeshSelectionMechanicMode::Edge;
		break;
	case EUVSelectToolSelectionMode::Vertex:
		SelectionMechanic->SelectionMode = EMeshSelectionMechanicMode::Vertex;
		break;
	case EUVSelectToolSelectionMode::Triangle:
		SelectionMechanic->SelectionMode = EMeshSelectionMechanicMode::Triangle;
		break;
	case EUVSelectToolSelectionMode::Mesh:
		SelectionMechanic->SelectionMode = EMeshSelectionMechanicMode::Mesh;
		break;
	default:
		ensure(false);
		break;
	}
}

void UUVSelectTool::OnSelectionChanged()
{
	const FDynamicMeshSelection& Selection = SelectionMechanic->GetCurrentSelection();

	SelectionTargetIndex = -1;
	MovingVids.Reset();
	SelectedTids.Reset();
	BoundaryEids.Reset();
	if (!Selection.IsEmpty())
	{
		// Note which mesh we're selecting in.
		for (int32 i = 0; i < Targets.Num(); ++i)
		{
			if (Targets[i]->UnwrapCanonical.Get() == Selection.Mesh)
			{
				SelectionTargetIndex = i;
				break;
			}
		}
		check(SelectionTargetIndex >= 0);

		// Note the selected vids
		TSet<int32> VidSet;
		TSet<int32> TidSet;
		if (Selection.Type == FDynamicMeshSelection::EType::Triangle)
		{
			const FDynamicMesh3* LivePreviewMesh = Targets[SelectionTargetIndex]->AppliedCanonical.Get();
			for (int32 Tid : Selection.SelectedIDs)
			{
				FIndex3i TriVids = Selection.Mesh->GetTriangle(Tid);
				for (int i = 0; i < 3; ++i)
				{
					if (!VidSet.Contains(TriVids[i]))
					{
						VidSet.Add(TriVids[i]);
						MovingVids.Add(TriVids[i]);
					}
				}
				if (!TidSet.Contains(Tid))
				{
					TidSet.Add(Tid);
					SelectedTids.Add(Tid);
				}

				// Gather the boundary edges in the live preview
				FIndex3i TriEids = LivePreviewMesh->GetTriEdges(Tid);
				for (int i = 0; i < 3; ++i)
				{
					FIndex2i EdgeTids = LivePreviewMesh->GetEdgeT(TriEids[i]);
					for (int j = 0; j < 2; ++j)
					{
						if (EdgeTids[j] != Tid && !Selection.SelectedIDs.Contains(EdgeTids[j]))
						{
							BoundaryEids.Add(TriEids[i]);
							break;
						}
					}
				}
			}
		}
		else if (Selection.Type == FDynamicMeshSelection::EType::Edge)
		{
			for (int32 Eid : Selection.SelectedIDs)
			{
				FIndex2i EdgeVids = Selection.Mesh->GetEdgeV(Eid);
				for (int i = 0; i < 2; ++i)
				{
					if (!VidSet.Contains(EdgeVids[i]))
					{
						VidSet.Add(EdgeVids[i]);
						MovingVids.Add(EdgeVids[i]);
					}

					TArray<int> TidOneRing;
					Selection.Mesh->GetVtxTriangles(EdgeVids[i], TidOneRing);
					for (int32 Tid : TidOneRing)
					{
						if (!TidSet.Contains(Tid))
						{
							TidSet.Add(Tid);
							SelectedTids.Add(Tid);
						}
					}
				}
			}
		}
		else if (Selection.Type == FDynamicMeshSelection::EType::Vertex)
		{
			for (int32 Vid : Selection.SelectedIDs)
			{
				if (!VidSet.Contains(Vid))
				{
					VidSet.Add(Vid);
					MovingVids.Add(Vid);
				}

				TArray<int> TidOneRing;
				Selection.Mesh->GetVtxTriangles(Vid, TidOneRing);
				for (int32 Tid : TidOneRing)
				{
					if (!TidSet.Contains(Tid))
					{
						TidSet.Add(Tid);
						SelectedTids.Add(Tid);
					}
				}

			}
		}
		else
		{
			check(false);
		}
	}

	UpdateLivePreviewLines();
	
	UpdateGizmo();
}

void UUVSelectTool::UpdateLivePreviewLines()
{
	LivePreviewLineSet->Clear();

	const FDynamicMeshSelection& Selection = SelectionMechanic->GetCurrentSelection();
	if (!Selection.IsEmpty())
	{
		FTransform MeshTransform = Targets[SelectionTargetIndex]->AppliedPreview->PreviewMesh->GetTransform();
		const FDynamicMesh3* LivePreviewMesh = Targets[SelectionTargetIndex]->AppliedCanonical.Get();
		
		for (int32 Eid : BoundaryEids)
		{
			FVector3d Vert1, Vert2;
			LivePreviewMesh->GetEdgeV(Eid, Vert1, Vert2);

			LivePreviewLineSet->AddLine(
				MeshTransform.TransformPosition(Vert1), 
				MeshTransform.TransformPosition(Vert2), 
				FColor::Yellow, 2, 1.5);
		}
		
	}
}

void UUVSelectTool::SetGizmoEnabled(bool bEnabledIn)
{
	bGizmoEnabled = bEnabledIn;

	// SetGizmoEnabled may be called before or after Setup, hence the check here to see if
	// the gizmo is set up.
	if (TransformGizmo)
	{
		UpdateGizmo();
	}
}

void UUVSelectTool::GizmoTransformStarted(UTransformProxy* Proxy)
{
	bInDrag = true;

	InitialGizmoFrame = FFrame3d(TransformGizmo->ActiveTarget->GetTransform());
	MovingVertOriginalPositions.SetNum(MovingVids.Num());
	const FDynamicMesh3* Mesh = Targets[SelectionTargetIndex]->UnwrapCanonical.Get();
	// Note: Our meshes currently don't have a transform. Otherwise we'd need to convert vid location to world
	// space first, then to the frame.
	for (int32 i = 0; i < MovingVids.Num(); ++i)
	{
		MovingVertOriginalPositions[i] = InitialGizmoFrame.ToFramePoint(Mesh->GetVertex(MovingVids[i]));
	}
}

void UUVSelectTool::GizmoTransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
	// This function gets called both during drag and on undo/redo. This might have been ok if
	// undo/redo also called GizmoTransformStarted/GizmoTransformEnded, but they don't, which
	// means the two types of events operate quite differently. We just ignore any non-drag calls.
	if (!bInDrag)
	{
		return;
	}

	FTransform DeltaTransform = Transform.GetRelativeTransform(InitialGizmoFrame.ToFTransform());

	if (!DeltaTransform.GetTranslation().IsNearlyZero() || !DeltaTransform.GetRotation().IsIdentity() || Transform.GetScale3D() != FVector::One())
	{
		UnappliedGizmoTransform = Transform;
		bGizmoTransformNeedsApplication = true;
	}	
}

void UUVSelectTool::GizmoTransformEnded(UTransformProxy* Proxy)
{
	bInDrag = false;

	// Set things up for undo.
	// TODO: We should really use FMeshVertexChange instead of FDynamicMeshChange because we don't
	// need to alter the mesh topology. However we currently don't have a way to apply a FMeshVertexChange
	// directly to a dynamic mesh pointer, only via UDynamicMesh. We should change things here once
	// that ability exists.
	FDynamicMeshChangeTracker ChangeTracker(Targets[SelectionTargetIndex]->UnwrapCanonical.Get());
	ChangeTracker.BeginChange();
	ChangeTracker.SaveTriangles(SelectedTids, true);

	// One final attempt to apply transforms if OnTick hasn't happened yet
	ApplyGizmoTransform();

	if (Settings->bUpdatePreviewDuringDrag)
	{
		// Both previews must already be updated, so only need to update canonical
		Targets[SelectionTargetIndex]->UpdateCanonicalFromPreviews(&MovingVids);
	}
	else
	{
		Targets[SelectionTargetIndex]->UpdateAllFromUnwrapPreview(&MovingVids, nullptr, &SelectedTids);
	}

	if (!AABBTrees[SelectionTargetIndex]->IsValid())
	{
		AABBTrees[SelectionTargetIndex]->Build();
	}

	const FText TransactionName(LOCTEXT("DragCompleteTransactionName", "Move Items"));
	EmitChangeAPI->BeginUndoTransaction(TransactionName);
	EmitChangeAPI->EmitToolIndependentChange(ChangeRouter, MakeUnique<UVSelectToolLocals::FGizmoMeshChange>(
		Targets[SelectionTargetIndex], ChangeTracker.EndChange(), 
		InitialGizmoFrame.ToFTransform(), TransformGizmo->GetGizmoTransform()),
		TransactionName);
	EmitChangeAPI->EndUndoTransaction();

	TransformGizmo->SetNewChildScale(FVector::One());
	SelectionMechanic->RebuildDrawnElements(TransformGizmo->GetGizmoTransform());
}

void UUVSelectTool::ApplyGizmoTransform()
{
	if (bGizmoTransformNeedsApplication)
	{
		FTransform3d TransformToApply(UnappliedGizmoTransform);

		// TODO: The division here is a bit of a hack. Properly-speaking, the scaling handles should act relative to
		// gizmo size, not the visible space across which we drag, otherwise it becomes dependent on the units we
		// use and our absolute distance from the object. Since our UV unwrap is scaled by 1000 to make it
		// easier to zoom in and out without running into issues, the measure of the distance across which we typically
		// drag the handles is too high to be convenient. Until we make the scaling invariant to units/distance from
		// target, we use this hack.
		TransformToApply.SetScale(FVector::One() + (UnappliedGizmoTransform.GetScale3D() - FVector::One()) / 10);

		Targets[SelectionTargetIndex]->UnwrapPreview->PreviewMesh->DeferredEditMesh([&TransformToApply, this](FDynamicMesh3& MeshIn)
			{
				for (int32 i = 0; i < MovingVids.Num(); ++i)
				{
					MeshIn.SetVertex(MovingVids[i], TransformToApply.TransformPosition(MovingVertOriginalPositions[i]));
				}
			}, false);
		Targets[SelectionTargetIndex]->UpdateUnwrapPreviewOverlayFromPositions(&MovingVids, nullptr, &SelectedTids);

		SelectionMechanic->SetDrawnElementsTransform((FTransform)TransformToApply);

		if (Settings->bUpdatePreviewDuringDrag)
		{
			Targets[SelectionTargetIndex]->UpdateAppliedPreviewFromUnwrapPreview(&MovingVids, nullptr, &SelectedTids);
		}

		bGizmoTransformNeedsApplication = false;
	}
}

void UUVSelectTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	SelectionMechanic->Render(RenderAPI);
}

void UUVSelectTool::OnTick(float DeltaTime)
{
	ApplyGizmoTransform();
}


#undef LOCTEXT_NAMESPACE
