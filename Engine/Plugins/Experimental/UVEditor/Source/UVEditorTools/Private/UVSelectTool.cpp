// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVSelectTool.h"

#include "Algo/Unique.h"
#include "BaseGizmos/CombinedTransformGizmo.h"
#include "BaseGizmos/GizmoBaseComponent.h"
#include "ContextObjectStore.h"
#include "Drawing/LineSetComponent.h"
#include "Drawing/MeshElementsVisualizer.h"
#include "Drawing/PointSetComponent.h"
#include "Drawing/PreviewGeometryActor.h"
#include "DynamicMesh/DynamicMeshChangeTracker.h"
#include "DynamicMesh/MeshIndexUtil.h"
#include "ToolTargets/UVEditorToolMeshInput.h"
#include "InteractiveToolManager.h"
#include "MeshOpPreviewHelpers.h" // UMeshOpPreviewWithBackgroundCompute
#include "Parameterization/DynamicMeshUVEditor.h"
#include "PreviewMesh.h"
#include "Selection/UVEditorMeshSelectionMechanic.h"
#include "Selection/UVEditorDynamicMeshSelection.h"
#include "ToolSetupUtil.h"
#include "UVEditorUXSettings.h"
#include "EngineAnalytics.h"

#include "UVSeamSewAction.h"
#include "UVIslandConformalUnwrapAction.h"

#include "ToolTargetManager.h"

#define LOCTEXT_NAMESPACE "UUVSelectTool"

using namespace UE::Geometry;

namespace UVSelectToolLocals
{
	// These following three functions deal with the unfortunate problem that eids are unstable as identifiers
	// (e.g. removing and reinserting the same triangles can change the eids of the edges), so edges have to
	// be identified in another way. We identify them by vertex ID pairs. This should really be dealt with
	// on a mesh selection level, but for now we fix it here.
	// After selection changes, we convert our eids to vid pairs. After mesh changes, we update the selection
	// eids from our stored vid pairs.

	/** If selection is a non-empty edge selection, update its eids using stored vid pairs. */
	void UpdateSelectionEidsAfterMeshChange(FUVEditorDynamicMeshSelection& SelectionInOut, TArray<FIndex2i>* VidPairsIn)
	{
		if (!SelectionInOut.Mesh || SelectionInOut.Type != FUVEditorDynamicMeshSelection::EType::Edge)
		{
			// No update necessary
			return;
		}

		// Otherwise, updating eids.
		if (!ensure(VidPairsIn))
		{
			return;
		}
		SelectionInOut.SelectedIDs.Empty();
		for (const FIndex2i& VidPair : *VidPairsIn)
		{
			int32 Eid = SelectionInOut.Mesh->FindEdge(VidPair.A, VidPair.B);
			if (ensure(Eid != IndexConstants::InvalidID))
			{
				SelectionInOut.SelectedIDs.Add(Eid);
			}
		}
	}

	/** If selection mechanic holds a non-empty edge selection, update its eids using stored vid pairs. */
	void UpdateSelectionEidsAfterMeshChange(UUVEditorMeshSelectionMechanic& SelectionMechanic, TArray<FIndex2i>* VidPairsIn)
	{
		const FUVEditorDynamicMeshSelection& CurrentSelection = SelectionMechanic.GetCurrentSelection();
		if (CurrentSelection.Mesh && CurrentSelection.Type == FUVEditorDynamicMeshSelection::EType::Edge)
		{
			FUVEditorDynamicMeshSelection UpdatedSelection = CurrentSelection;
			UpdateSelectionEidsAfterMeshChange(UpdatedSelection, VidPairsIn);
			SelectionMechanic.SetSelection(UpdatedSelection, false, false);
		}
	}

	void GetVidPairsFromSelection(const FUVEditorDynamicMeshSelection& SelectionIn, TArray<FIndex2i>& VidPairsOut)
	{
		VidPairsOut.Reset();
		if (!SelectionIn.Mesh || SelectionIn.Type != FUVEditorDynamicMeshSelection::EType::Edge)
		{
			// No vid pairs to add
			return;
		}

		// Otherwise create the vid pairs out of eids
		for (int32 Eid : SelectionIn.SelectedIDs)
		{
			VidPairsOut.Add(SelectionIn.Mesh->GetEdgeV(Eid));
		}
	}

	/**
	 * An undo/redo object for selection changes that, instead of operating directly on a selection
	 * mechanic, instead operates on a context object that tools can use to route the request
	 * to the current selection mechanic. This is valuable because we want the selection changes
	 * to be undoable in different invocations of the tool, and the selection mechanic pointer
	 * will not stay the same. However, the context object will stay the same, and we can register
	 * to its delegate on each invocation.
	 * 
	 * The other thing that is different about this selection change object is that in cases of edge
	 * selections, it uses stored vid pairs rather then eids, to deal with mesh changes that alter eids.
	 */
	class FSelectionChange : public FToolCommandChange
	{
	public:
		/**
		 * @param GizmoBeforeIn Transform to which to revert gizmo on the way back (to avoid losing gizmo rotation, which
		 *   gets transacted post-selection-change, and therefore in the wrong place for undo).
		 * @param EdgeVidPairsBeforeIn
		 * @param EdgeVidPairsAfterIn
		 */
		FSelectionChange(const FUVEditorDynamicMeshSelection& SelectionBeforeIn,
			const FUVEditorDynamicMeshSelection& SelectionAfterIn,
			const FTransform& GizmoBeforeIn,
			TUniquePtr<TArray<FIndex2i>> EdgeVidPairsBeforeIn,
			TUniquePtr<TArray<FIndex2i>> EdgeVidPairsAfterIn
			)
			: SelectionBefore(SelectionBeforeIn)
			, SelectionAfter(SelectionAfterIn)
			, GizmoBefore(GizmoBeforeIn)
			, EdgeVidPairsBefore(MoveTemp(EdgeVidPairsBeforeIn))
			, EdgeVidPairsAfter(MoveTemp(EdgeVidPairsAfterIn))
		{
			// Make sure that for both selections, if we have a non-empty edge selection, we have vid pairs.
			ensure(!(
				(SelectionBefore.Mesh && SelectionBefore.Type == FUVEditorDynamicMeshSelection::EType::Edge && !EdgeVidPairsBefore)
				|| (SelectionAfter.Mesh && SelectionAfter.Type == FUVEditorDynamicMeshSelection::EType::Edge && !EdgeVidPairsAfter)));
		}

		virtual void Apply(UObject* Object) override
		{
			UUVSelectToolChangeRouter* ChangeRouter = Cast<UUVSelectToolChangeRouter>(Object);
			if (ensure(ChangeRouter) && ChangeRouter->CurrentSelectTool.IsValid())
			{
				UpdateSelectionEidsAfterMeshChange(SelectionAfter, EdgeVidPairsAfter.Get());
				ChangeRouter->CurrentSelectTool->SetSelection(SelectionAfter);
			}
		}

		virtual void Revert(UObject* Object) override
		{
			UUVSelectToolChangeRouter* ChangeRouter = Cast<UUVSelectToolChangeRouter>(Object);
			if (ensure(ChangeRouter) && ChangeRouter->CurrentSelectTool.IsValid())
			{
				UpdateSelectionEidsAfterMeshChange(SelectionBefore, EdgeVidPairsBefore.Get());
				ChangeRouter->CurrentSelectTool->SetSelection(SelectionBefore);
				ChangeRouter->CurrentSelectTool->SetGizmoTransform(GizmoBefore);
			}
		}

		virtual bool HasExpired(UObject* Object) const override
		{
			UUVSelectToolChangeRouter* ChangeRouter = Cast<UUVSelectToolChangeRouter>(Object);
			return !(ChangeRouter && ChangeRouter->CurrentSelectTool.IsValid());
		}

		virtual FString ToString() const override
		{
			return TEXT("UVSelectToolLocals::FSelectionChange");
		}

	protected:
		FUVEditorDynamicMeshSelection SelectionBefore;
		FUVEditorDynamicMeshSelection SelectionAfter;
		FTransform GizmoBefore;

		TUniquePtr<TArray<FIndex2i>> EdgeVidPairsBefore;
		TUniquePtr<TArray<FIndex2i>> EdgeVidPairsAfter;
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

			UUVSelectToolChangeRouter* ChangeRouter = Cast<UUVSelectToolChangeRouter>(Object);
			if (ensure(ChangeRouter) && ChangeRouter->CurrentSelectTool.IsValid())
			{
				ChangeRouter->CurrentSelectTool->SetGizmoTransform(GizmoBefore);
			}
		}

		virtual bool HasExpired(UObject* Object) const override
		{
			return !(UVToolInputObject.IsValid() && UVToolInputObject->IsValid() && UnwrapCanonicalMeshChange);
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


/*
 * ToolBuilder
 */

bool UUVSelectToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return Targets && Targets->Num() > 0;
}

UInteractiveTool* UUVSelectToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UUVSelectTool* NewTool = NewObject<UUVSelectTool>(SceneState.ToolManager);
	NewTool->SetWorld(SceneState.World);
	NewTool->SetTargets(*Targets);

	return NewTool;
}

// Tool property functions
void  USelectToolActionPropertySet::IslandConformalUnwrap()
{
	PostAction(ESelectToolAction::IslandConformalUnwrap);
}

void USelectToolActionPropertySet::PostAction(ESelectToolAction Action)
{
	if (ParentTool.IsValid())
	{
		ParentTool->RequestAction(Action);
	}
}


void UUVSelectTool::Setup()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UVSelectTool_Setup);
	
	ToolStartTimeAnalytics = FDateTime::UtcNow();

	using namespace UVSelectToolLocals;

	check(Targets.Num() > 0);

	UInteractiveTool::Setup();
	
	SetToolDisplayName(LOCTEXT("ToolName", "UV Select Tool"));

	UContextObjectStore* ContextStore = GetToolManager()->GetContextObjectStore();
	EmitChangeAPI = ContextStore->FindContext<UUVToolEmitChangeAPI>();
	ViewportButtonsAPI = ContextStore->FindContext<UUVToolViewportButtonsAPI>();
	ViewportButtonsAPI->SetGizmoButtonsEnabled(true);
	ViewportButtonsAPI->OnGizmoModeChange.AddWeakLambda(this, 
		[this](UUVToolViewportButtonsAPI::EGizmoMode NewGizmoMode) {
			UpdateGizmo();
		});
	ViewportButtonsAPI->SetSelectionButtonsEnabled(true);
	ViewportButtonsAPI->OnSelectionModeChange.AddWeakLambda(this,
		[this](UUVToolViewportButtonsAPI::ESelectionMode NewMode) {
			UpdateSelectionMode();
		});

	ToolActions = NewObject<USelectToolActionPropertySet>(this);
	ToolActions->Initialize(this);
	AddToolPropertySource(ToolActions);

	SelectionMechanic = NewObject<UUVEditorMeshSelectionMechanic>();
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

	SelectionMechanic->EmitSelectionChange = [this](const FUVEditorDynamicMeshSelection& OldSelection,
		const FUVEditorDynamicMeshSelection& NewSelection)
	{
		TUniquePtr<TArray<FIndex2i>> VidPairsBefore;
		TUniquePtr<TArray<FIndex2i>> VidPairsAfter;
		if (OldSelection.Type == FUVEditorDynamicMeshSelection::EType::Edge)
		{
			VidPairsBefore = MakeUnique<TArray<FIndex2i>>();
			GetVidPairsFromSelection(OldSelection, *VidPairsBefore);
		}
		if (NewSelection.Type == FUVEditorDynamicMeshSelection::EType::Edge)
		{
			VidPairsAfter = MakeUnique<TArray<FIndex2i>>();
			GetVidPairsFromSelection(NewSelection, *VidPairsAfter);
		}
		EmitChangeAPI->EmitToolIndependentChange(ChangeRouter, MakeUnique<UVSelectToolLocals::FSelectionChange>(
			OldSelection, NewSelection, TransformGizmo->GetGizmoTransform(),
			MoveTemp(VidPairsBefore), MoveTemp(VidPairsAfter)),
			LOCTEXT("SelectionChangeMessage", "Selection Change"));
	};

	UpdateSelectionMode();

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
			TRACE_CPUPROFILER_EVENT_SCOPE(BuildAABBTreeForTarget);
			Tree = MakeShared<FDynamicMeshAABBTree3>();
			Tree->SetMesh(Target->UnwrapCanonical.Get(), false);
			// For now we split round-robin on the X/Y axes TODO Experiment with better splitting heuristics
			FDynamicMeshAABBTree3::GetSplitAxisFunc GetSplitAxis = [](int Depth, const FAxisAlignedBox3d&) { return Depth % 2; };
			// Note: 16 tris/leaf was chosen with data collected by SpatialBenchmarks.cpp in GeometryProcessingUnitTests
			Tree->SetBuildOptions(16, MoveTemp(GetSplitAxis));
			Tree->Build();
			TreeStore->Set(Target->UnwrapCanonical.Get(), Tree, Target);
		}
		AABBTrees.Add(Tree);
	}

	// Add the spatial structures to the selection mechanic
	for (int32 i = 0; i < Targets.Num(); ++i)
	{
		SelectionMechanic->AddSpatial(AABBTrees[i],
			Targets[i]->UnwrapPreview->PreviewMesh->GetTransform());
	}

	// Make sure that if undo/redo events act on the meshes, we update our state.
	// The trees will be updated by the tree store, which listens to the same broadcasts.
	for (int32 i = 0; i < Targets.Num(); ++i)
	{
		Targets[i]->OnCanonicalModified.AddWeakLambda(this, [this]
		(UUVEditorToolMeshInput* InputObject, const UUVEditorToolMeshInput::FCanonicalModifiedInfo& Info) {
			if (bIgnoreOnCanonicalChange) // Used to avoid reacting to broadcasts that we ourselves caused
			{
				return;
			}
			UpdateSelectionEidsAfterMeshChange(*SelectionMechanic, &CurrentSelectionVidPairs);
			UpdateGizmo();
			SelectionMechanic->RebuildDrawnElements(TransformGizmo->GetGizmoTransform());
			SewAction->UpdateVisualizations();
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
	TransformGizmo->SetVisibility(ViewportButtonsAPI->GetGizmoMode() != UUVToolViewportButtonsAPI::EGizmoMode::Select);

	// Tell the gizmo to be drawn on top even over translucent-mode materials.
	// Note: this may someday not be necessary, if we get this to work properly by default. Normally we can't
	// use this approach in modeling mode because it adds dithering to the occluded sections, but we are able
	// to disable that in the uv editor viewports.
	for (UActorComponent* Component : TransformGizmo->GetGizmoActor()->GetComponents())
	{
		UGizmoBaseComponent* GizmoComponent = Cast<UGizmoBaseComponent>(Component);
		if (GizmoComponent)
		{
			GizmoComponent->bUseEditorCompositing = true;
		}
	}

	LivePreviewGeometryActor = Targets[0]->AppliedPreview->GetWorld()->SpawnActor<APreviewGeometryActor>(
		FVector::ZeroVector, FRotator(0, 0, 0), FActorSpawnParameters());
	LivePreviewLineSet = NewObject<ULineSetComponent>(LivePreviewGeometryActor);
	LivePreviewGeometryActor->SetRootComponent(LivePreviewLineSet);
	LivePreviewLineSet->RegisterComponent();
	LivePreviewLineSet->SetLineMaterial(ToolSetupUtil::GetDefaultLineComponentMaterial(
		GetToolManager(), /*bDepthTested*/ true));

	LivePreviewPointSet = NewObject<UPointSetComponent>(LivePreviewGeometryActor);
	LivePreviewPointSet->AttachToComponent(LivePreviewLineSet, FAttachmentTransformRules::KeepWorldTransform);
	LivePreviewPointSet->RegisterComponent();
	LivePreviewPointSet->SetPointMaterial(ToolSetupUtil::GetDefaultPointComponentMaterial(
		GetToolManager(), /*bDepthTested*/ true));

	SewAction = NewObject<UUVSeamSewAction>();
	SewAction->Setup(this);
	SewAction->SetTargets(Targets);
	SewAction->SetWorld(Targets[0]->UnwrapPreview->GetWorld());

	IslandConformalUnwrapAction = NewObject<UUVIslandConformalUnwrapAction>();
	IslandConformalUnwrapAction->Setup(this);
	IslandConformalUnwrapAction->SetTargets(Targets);
	IslandConformalUnwrapAction->SetWorld(Targets[0]->UnwrapPreview->GetWorld());

	if (!SelectionMechanic->GetCurrentSelection().IsEmpty())
	{
		OnSelectionChanged();
	}
	UpdateGizmo();

	GetToolManager()->DisplayMessage(LOCTEXT("SelectToolStatusBarMessage", 
		"Select elements in the viewport and then transform them or use one of the action buttons."), 
		EToolMessageLevel::UserNotification);

	// Analytics
	InputTargetAnalytics = UVEditorAnalytics::CollectTargetAnalytics(Targets);
}

void UUVSelectTool::Shutdown(EToolShutdownType ShutdownType)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UVSelectTool_Shutdown);
	
	// Clear selection so that it can be restored after undoing back into the select tool
	if (!SelectionMechanic->GetCurrentSelection().IsEmpty())
	{
		// (The broadcast here is so that we still broadcast on undo)
		SelectionMechanic->SetSelection(UUVEditorMeshSelectionMechanic::FUVEditorDynamicMeshSelection(), true, true);
	}

	ChangeRouter->CurrentSelectTool = nullptr;

	for (TObjectPtr<UUVEditorToolMeshInput> Target : Targets)
	{
		Target->OnCanonicalModified.RemoveAll(this);
	}

	SelectionMechanic->Shutdown();

	if (LivePreviewGeometryActor)
	{
		LivePreviewGeometryActor->Destroy();
		LivePreviewGeometryActor = nullptr;
		LivePreviewPointSet = nullptr;
		LivePreviewLineSet = nullptr;
	}

	if (SewAction)
	{
		SewAction->Shutdown();
	}

	if (IslandConformalUnwrapAction)
	{
		IslandConformalUnwrapAction->Shutdown();
	}

	// Calls shutdown on gizmo and destroys it.
	GetToolManager()->GetPairedGizmoManager()->DestroyAllGizmosByOwner(this);

	ViewportButtonsAPI->OnGizmoModeChange.RemoveAll(this);
	ViewportButtonsAPI->OnSelectionModeChange.RemoveAll(this);
	ViewportButtonsAPI->SetGizmoButtonsEnabled(false);
	ViewportButtonsAPI->SetSelectionButtonsEnabled(false);

	ViewportButtonsAPI = nullptr;
	EmitChangeAPI = nullptr;
	ChangeRouter = nullptr;

	// Analytics
	RecordAnalytics();
}

void UUVSelectTool::SetSelection(const FUVEditorDynamicMeshSelection& NewSelection)
{
	SelectionMechanic->SetSelection(NewSelection, true, 
		false); // Don't emit undo because this function is called from undo
	
	// Make sure the current selection mode is compatible with the new selection we received. Don't broadcast
	// this part because presumably we've already responded to selection change through selection change broadcast.
	// TODO: there are a couple things that are not ideal about the below. One is that we always change to
	// triangle mode when we don't know if the triangles came from island or mesh selection mode. Another is
	// that we change the selection mode in the mechanic directly rather than going through ChangeSelectionMode,
	// since we don't want to do the conversions/broadcasts that the setter performs. Still, it's not worth
	// improving this further because the proper solution will probably involve transacting the selection mode
	// changes, which we'll probably implement while moving selection up to mode level (along with other changes
	// that would probably stomp anything we do here)
	UUVToolViewportButtonsAPI::ESelectionMode CurrentMode = ViewportButtonsAPI->GetSelectionMode();
	switch (NewSelection.Type)
	{
	case FUVEditorDynamicMeshSelection::EType::Vertex:
		if (CurrentMode != UUVToolViewportButtonsAPI::ESelectionMode::Vertex)
		{
			ViewportButtonsAPI->SetSelectionMode(UUVToolViewportButtonsAPI::ESelectionMode::Vertex, false);
			SelectionMechanic->SelectionMode = EUVEditorMeshSelectionMode::Vertex;
		}
		break;
	case FUVEditorDynamicMeshSelection::EType::Edge:
		if (CurrentMode != UUVToolViewportButtonsAPI::ESelectionMode::Edge)
		{
			ViewportButtonsAPI->SetSelectionMode(UUVToolViewportButtonsAPI::ESelectionMode::Edge, false);
			SelectionMechanic->SelectionMode = EUVEditorMeshSelectionMode::Edge;
		}
		break;
	case FUVEditorDynamicMeshSelection::EType::Triangle:
		if (CurrentMode != UUVToolViewportButtonsAPI::ESelectionMode::Triangle
			&& CurrentMode != UUVToolViewportButtonsAPI::ESelectionMode::Island
			&& CurrentMode != UUVToolViewportButtonsAPI::ESelectionMode::Mesh)
		{
			ViewportButtonsAPI->SetSelectionMode(UUVToolViewportButtonsAPI::ESelectionMode::Triangle, false);
			SelectionMechanic->SelectionMode = EUVEditorMeshSelectionMode::Triangle;
		}
		break;
	}

}

void UUVSelectTool::SetGizmoTransform(const FTransform& NewTransform)
{
	TransformGizmo->ReinitializeGizmoTransform(NewTransform);
	SelectionMechanic->RebuildDrawnElements(NewTransform);
}

void UUVSelectTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
}

void UUVSelectTool::UpdateGizmo()
{
	const FUVEditorDynamicMeshSelection& Selection = SelectionMechanic->GetCurrentSelection();

	if (!Selection.IsEmpty())
	{
		FVector3d Centroid = SelectionMechanic->GetCurrentSelectionCentroid();

		TransformGizmo->ReinitializeGizmoTransform(FTransform((FVector)Centroid));
	}

	TransformGizmo->SetVisibility(
		ViewportButtonsAPI->GetGizmoMode() != UUVToolViewportButtonsAPI::EGizmoMode::Select
		&& !SelectionMechanic->GetCurrentSelection().IsEmpty());
}

void UUVSelectTool::UpdateSelectionMode()
{
	EUVEditorMeshSelectionMode TargetMode;
	switch (ViewportButtonsAPI->GetSelectionMode())
	{
	case UUVToolViewportButtonsAPI::ESelectionMode::Vertex:
		TargetMode = EUVEditorMeshSelectionMode::Vertex;
		break;	
	case UUVToolViewportButtonsAPI::ESelectionMode::Edge:
		TargetMode = EUVEditorMeshSelectionMode::Edge;
		break;
	case UUVToolViewportButtonsAPI::ESelectionMode::Triangle:
		TargetMode = EUVEditorMeshSelectionMode::Triangle;
		break;
	case UUVToolViewportButtonsAPI::ESelectionMode::Island:
		TargetMode = EUVEditorMeshSelectionMode::Component;
		break;
	case UUVToolViewportButtonsAPI::ESelectionMode::Mesh:
		TargetMode = EUVEditorMeshSelectionMode::Mesh;
		break;
	default:
		// We shouldn't ever get "none" as the selection mode...
		ensure(false);
		TargetMode = EUVEditorMeshSelectionMode::Vertex;
		break;
	}
	SelectionMechanic->ChangeSelectionMode(TargetMode); // broadcast and emit undo if needed
}

void UUVSelectTool::OnSelectionChanged()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UVSelectTool_OnSelectionChanged);
	
	using namespace UVSelectToolLocals;

	ClearWarning();

	const FUVEditorDynamicMeshSelection& Selection = SelectionMechanic->GetCurrentSelection();

	GetVidPairsFromSelection(Selection, CurrentSelectionVidPairs);

	SelectionTargetIndex = -1;
	MovingVids.Reset();
	SelectedTids.Reset();
	LivePreviewEids.Reset();
	LivePreviewVids.Reset();

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

		UUVEditorToolMeshInput* Target = Targets[SelectionTargetIndex];
		const FDynamicMesh3* LivePreviewMesh = Target->AppliedCanonical.Get();

		// Note the selected vids
		TSet<int32> VidSet;
		TSet<int32> TidSet;
		if (Selection.Type == FUVEditorDynamicMeshSelection::EType::Triangle)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Triangle);

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
							LivePreviewEids.Add(TriEids[i]);
							break;
						}
					}
				}
			}
		}
		else if (Selection.Type == FUVEditorDynamicMeshSelection::EType::Edge)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Edge);

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

				// Add the edge highlight in the live preview
				LivePreviewEids.Add(LivePreviewMesh->FindEdge(
					Target->UnwrapVidToAppliedVid(EdgeVids.A),
					Target->UnwrapVidToAppliedVid(EdgeVids.B)));
			}
		}
		else if (Selection.Type == FUVEditorDynamicMeshSelection::EType::Vertex)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Vertex);

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

				LivePreviewVids.Add(Target->UnwrapVidToAppliedVid(Vid));
			}
		}
		else
		{
			check(false);
		}
	}

	SewAction->SetSelection(SelectionTargetIndex, &Selection);
	IslandConformalUnwrapAction->SetSelection(SelectionTargetIndex, &Selection);

	UpdateLivePreviewLines();
	UpdateGizmo();
}

void UUVSelectTool::ClearWarning()
{
	GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserWarning);
}

void UUVSelectTool::UpdateLivePreviewLines()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UVSelectTool_UpdateLivePreviewLines);
	
	LivePreviewLineSet->Clear();
	LivePreviewPointSet->Clear();

	const FUVEditorDynamicMeshSelection& Selection = SelectionMechanic->GetCurrentSelection();
	if (!Selection.IsEmpty())
	{
		FTransform MeshTransform = Targets[SelectionTargetIndex]->AppliedPreview->PreviewMesh->GetTransform();
		const FDynamicMesh3* LivePreviewMesh = Targets[SelectionTargetIndex]->AppliedCanonical.Get();

		for (int32 Eid : LivePreviewEids)
		{
			FVector3d Vert1, Vert2;
			LivePreviewMesh->GetEdgeV(Eid, Vert1, Vert2);

			LivePreviewLineSet->AddLine(
				MeshTransform.TransformPosition(Vert1), 
				MeshTransform.TransformPosition(Vert2), 
				FUVEditorUXSettings::SelectionTriangleWireframeColor,
				FUVEditorUXSettings::LivePreviewHighlightThickness,
				FUVEditorUXSettings::LivePreviewHighlightDepthOffset);
		}

		for (int32 Vid : LivePreviewVids)
		{
			FVector3d Position = LivePreviewMesh->GetVertex(Vid);

			LivePreviewPointSet->AddPoint(Position, 
				FUVEditorUXSettings::SelectionTriangleWireframeColor,
				FUVEditorUXSettings::LivePreviewHighlightPointSize,
				FUVEditorUXSettings::LivePreviewHighlightDepthOffset);
		}
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

	// Both previews must already be updated, so only need to update canonical. 
	{
		// We don't want to react to the ensuing broadcast so that we don't lose the gizmo rotation. We could just 
		// not broadcast (and update related structures, i.e. trees, ourselves), but conceptually it's better to 
		// broadcast the change since we did change the canonicals.
		TGuardValue<bool> IgnoreOnCanonicalBroadcast(bIgnoreOnCanonicalChange, true); // sets to true, restores at end

		Targets[SelectionTargetIndex]->UpdateCanonicalFromPreviews(&MovingVids,
			UUVEditorToolMeshInput::NONE_CHANGED_ARG);
	}

	const FText TransactionName(LOCTEXT("DragCompleteTransactionName", "Move Items"));
	EmitChangeAPI->EmitToolIndependentChange(ChangeRouter, MakeUnique<UVSelectToolLocals::FGizmoMeshChange>(
		Targets[SelectionTargetIndex], ChangeTracker.EndChange(), 
		InitialGizmoFrame.ToFTransform(), TransformGizmo->GetGizmoTransform()),
		TransactionName);

	TransformGizmo->SetNewChildScale(FVector::One());
	SelectionMechanic->RebuildDrawnElements(TransformGizmo->GetGizmoTransform());
}

void UUVSelectTool::ApplyGizmoTransform()
{
	if (bGizmoTransformNeedsApplication)
	{
		FTransformSRT3d TransformToApply(UnappliedGizmoTransform);

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
		Targets[SelectionTargetIndex]->UpdateUnwrapPreviewOverlayFromPositions(&MovingVids, 
			UUVEditorToolMeshInput::NONE_CHANGED_ARG, &SelectedTids);

		SelectionMechanic->SetDrawnElementsTransform((FTransform)TransformToApply);

		Targets[SelectionTargetIndex]->UpdateAppliedPreviewFromUnwrapPreview(&MovingVids, 
			UUVEditorToolMeshInput::NONE_CHANGED_ARG, &SelectedTids);

		bGizmoTransformNeedsApplication = false;
		SewAction->UpdateVisualizations();
		IslandConformalUnwrapAction->UpdateVisualizations();
	}
}

void UUVSelectTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	SelectionMechanic->Render(RenderAPI);
}

void UUVSelectTool::DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
	SelectionMechanic->DrawHUD(Canvas, RenderAPI);
}

void UUVSelectTool::OnTick(float DeltaTime)
{
	ApplyGizmoTransform();

	// Deal with any buttons that may have been clicked
	if (PendingAction != ESelectToolAction::NoAction)
	{
		ApplyAction(PendingAction);
		PendingAction = ESelectToolAction::NoAction;
	}
}

void UUVSelectTool::RequestAction(ESelectToolAction ActionType)
{
	ClearWarning();
	if (PendingAction == ESelectToolAction::NoAction)
	{
		PendingAction = ActionType;
	}
}

void UUVSelectTool::ApplyAction(ESelectToolAction ActionType)
{
	auto MaybeAddAnalyticsActionHistoryItem = [this, &ActionType](FUVEditorDynamicMeshSelection::EType ExpectedType)
	{
		if (!SelectionMechanic->GetCurrentSelection().IsEmpty() && (SelectionMechanic->GetCurrentSelection().Type == ExpectedType))
		{
			FActionHistoryItem ActionHistoryItem;
			ActionHistoryItem.Timestamp = FDateTime::UtcNow();
			ActionHistoryItem.NumOperands = SelectionMechanic->GetCurrentSelection().SelectedIDs.Num();
			ActionHistoryItem.ActionType = ActionType;
			AnalyticsActionHistory.Add(ActionHistoryItem);
		}
	};
	
	switch (ActionType)
	{
	case ESelectToolAction::Sew:
		if (SewAction)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ApplyAction_Sew);
	
			MaybeAddAnalyticsActionHistoryItem(FUVEditorDynamicMeshSelection::EType::Edge);

			const FText TransactionName(LOCTEXT("SewCompleteTransactionName", "Sew Edges"));
			EmitChangeAPI->BeginUndoTransaction(TransactionName);

			SelectionMechanic->SetSelection(FUVEditorDynamicMeshSelection(), false, true);
			SewAction->ExecuteAction(*EmitChangeAPI);

			EmitChangeAPI->EndUndoTransaction();
		}
		break;
	case ESelectToolAction::IslandConformalUnwrap:
		if (IslandConformalUnwrapAction)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ApplyAction_IslandConformalUnwrap);
			
			MaybeAddAnalyticsActionHistoryItem(FUVEditorDynamicMeshSelection::EType::Triangle);

			const FText TransactionName(LOCTEXT("ConformalUnwrapCompleteTransactionName", "Conformal Unwrap Islands"));
			EmitChangeAPI->BeginUndoTransaction(TransactionName);

			SelectionMechanic->SetSelection(FUVEditorDynamicMeshSelection(), false, true);
			IslandConformalUnwrapAction->ExecuteAction(*EmitChangeAPI);

			EmitChangeAPI->EndUndoTransaction();
		}
		break;
	case ESelectToolAction::Split:
		
		MaybeAddAnalyticsActionHistoryItem(FUVEditorDynamicMeshSelection::EType::Edge);
		
		ApplySplit();
	default:
		break;
	}
}

void UUVSelectTool::ApplySplit()
{
	using namespace UVSelectToolLocals;

	const FUVEditorDynamicMeshSelection& Selection = SelectionMechanic->GetCurrentSelection();

	if (Selection.IsEmpty())
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("SplitErrorSelectionEmpty", "Cannot split UVs. Selection was empty."),
			EToolMessageLevel::UserWarning);
	}
	else if (Selection.Type == FUVEditorDynamicMeshSelection::EType::Edge)
	{
		ApplySplitEdges();
	}
	else if (Selection.Type == FUVEditorDynamicMeshSelection::EType::Vertex)
	{
		ApplySplitBowtieVertices();
	}
	else
	{
		GetToolManager()->DisplayMessage(
			LOCTEXT("SplitErrorNotEdgeOrVert", "Cannot split UVs. Selection must be edges or vertices."),
			EToolMessageLevel::UserWarning);
	}
}

void UUVSelectTool::ApplySplitEdges()
{
	const FUVEditorDynamicMeshSelection& Selection = SelectionMechanic->GetCurrentSelection();
	if (!ensure(SelectionTargetIndex >= 0 && !Selection.IsEmpty() && Selection.Type == FUVEditorDynamicMeshSelection::EType::Edge))
	{
		return;
	}
	UUVEditorToolMeshInput* Target = Targets[SelectionTargetIndex];

	// Gather up the corresponding edge IDs in the applied (3d) mesh.
	TSet<int32> AppliedEidSet;
	for (int32 Eid : Selection.SelectedIDs)
	{
		// Note that we don't check whether edges are already boundary edges because we allow such edges
		// to be selected for splitting of any attached bowties.

		FIndex2i EdgeUnwrapVids = Selection.Mesh->GetEdgeV(Eid);

		int32 AppliedEid = Target->AppliedCanonical->FindEdge(
			Target->UnwrapVidToAppliedVid(EdgeUnwrapVids.A),
			Target->UnwrapVidToAppliedVid(EdgeUnwrapVids.B));

		if (ensure(AppliedEid != IndexConstants::InvalidID))
		{
			AppliedEidSet.Add(AppliedEid);
		}
	}

	// Perform the cut in the overlay, but don't propagate to unwrap yet
	FUVEditResult UVEditResult;
	FDynamicMeshUVEditor UVEditor(Target->AppliedCanonical.Get(),
		Target->UVLayerIndex, false);
	UVEditor.CreateSeamsAtEdges(AppliedEidSet, &UVEditResult);

	// Figure out the triangles that need to be saved in the unwrap for undo
	TSet<int32> TidSet;
	for (int32 UnwrapVid : UVEditResult.NewUVElements)
	{
		TArray<int32> VertTids;
		Target->AppliedCanonical->GetVtxTriangles(Target->UnwrapVidToAppliedVid(UnwrapVid), VertTids);
		TidSet.Append(VertTids);
	}

	FDynamicMeshChangeTracker ChangeTracker(Target->UnwrapCanonical.Get());
	ChangeTracker.BeginChange();
	ChangeTracker.SaveTriangles(TidSet, true);

	const FText TransactionName(LOCTEXT("ApplySplitEdgesTransactionName", "Split Edges"));
	EmitChangeAPI->BeginUndoTransaction(TransactionName);

	// Clear selection here so that it is restored on undo
	// TODO: We could apply a new selection after the change, but this should only happen
	// once we apply a new selection on split, which we currently don't do.
	SelectionMechanic->SetSelection(FUVEditorDynamicMeshSelection(), true, true);

	// Perform the update
	TArray<int32> AppliedTids = TidSet.Array();
	Target->UpdateAllFromAppliedCanonical(&UVEditResult.NewUVElements, &AppliedTids, &AppliedTids);
	
	// Not needed because it should happen automatically via broadcast of target canonical mesh change
	// AABBTrees[SelectionTargetIndex]->Build();

	// Emit update transaction
	EmitChangeAPI->EmitToolIndependentUnwrapCanonicalChange(
		Target, ChangeTracker.EndChange(), TransactionName);

	EmitChangeAPI->EndUndoTransaction();
}

void UUVSelectTool::ApplySplitBowtieVertices()
{
	const FUVEditorDynamicMeshSelection& Selection = SelectionMechanic->GetCurrentSelection();
	if (!ensure(SelectionTargetIndex >= 0 && !Selection.IsEmpty() && Selection.Type == FUVEditorDynamicMeshSelection::EType::Vertex))
	{
		return;
	}
	UUVEditorToolMeshInput* Target = Targets[SelectionTargetIndex];

	// Gather the corresponding vert ID's in the applied mesh
	TSet<int32> AppliedVidSet;
	for (int32 UnwrapVid : Selection.SelectedIDs)
	{
		int32 AppliedVid = Target->UnwrapVidToAppliedVid(UnwrapVid);
		AppliedVidSet.Add(AppliedVid);
	}

	// Split any bowties in the applied mesh overlay
	TArray<int32> NewUVElements;
	FDynamicMeshUVOverlay* Overlay = Target->AppliedCanonical->Attributes()->GetUVLayer(Target->UVLayerIndex);
	for (int32 Vid : AppliedVidSet)
	{
		Overlay->SplitBowtiesAtVertex(Vid, &NewUVElements);
	}

	// Prep for undo transaction
	TSet<int32> TidSet;
	for (int32 UnwrapVid : NewUVElements)
	{
		TArray<int32> VertTids;
		Target->AppliedCanonical->GetVtxTriangles(Target->UnwrapVidToAppliedVid(UnwrapVid), VertTids);
		TidSet.Append(VertTids);
	}

	FDynamicMeshChangeTracker ChangeTracker(Target->UnwrapCanonical.Get());
	ChangeTracker.BeginChange();
	ChangeTracker.SaveTriangles(TidSet, true);

	const FText TransactionName(LOCTEXT("ApplySplitBowtieVerticesTransactionName", "Split Bowties"));
	EmitChangeAPI->BeginUndoTransaction(TransactionName);

	// Emit selection clear first so that we restore it on undo
	FUVEditorDynamicMeshSelection NewSelection = SelectionMechanic->GetCurrentSelection(); // save type, etc
	// TODO: This emitted transaction doesn't actually need to broadcast on redo, but we don't yet
	// have support for that.
	SelectionMechanic->SetSelection(FUVEditorDynamicMeshSelection(), false, true); // don't broadcast, do emit undo

	// Perform the update
	TArray<int32> AppliedTids = TidSet.Array();
	Target->UpdateAllFromAppliedCanonical(&NewUVElements, &AppliedTids, &AppliedTids);

	// Emit update transaction
	EmitChangeAPI->EmitToolIndependentUnwrapCanonicalChange(
		Target, ChangeTracker.EndChange(), TransactionName);

	// Set up the new selection to include the new elements
	NewSelection.SelectedIDs.Append(NewUVElements);
	// TODO: This emitted transaction doesn't actually need to broadcast on undo, but we don't yet
	// have support for that.
	SelectionMechanic->SetSelection(NewSelection, true, true); // both broadcast and emit undo

	EmitChangeAPI->EndUndoTransaction();
}

void UUVSelectTool::RecordAnalytics()
{
	using namespace UVEditorAnalytics;
	
	if (!FEngineAnalytics::IsAvailable())
	{
		return;
	}
	
	TArray<FAnalyticsEventAttribute> Attributes;
	Attributes.Add(FAnalyticsEventAttribute(TEXT("Timestamp"), FDateTime::UtcNow().ToString()));
	
	// Tool inputs
	InputTargetAnalytics.AppendToAttributes(Attributes, "Input");

	// Tool outputs
	const FTargetAnalytics OutputTargetAnalytics = CollectTargetAnalytics(Targets);
	OutputTargetAnalytics.AppendToAttributes(Attributes, "Output");

	// Tool stats
	auto MaybeAppendActionStatsToAttributes = [this, &Attributes](ESelectToolAction ActionType, const FString& OperandName)
	{
		int32 NumActions = 0;
		float MeanNumOperands = 0;
		int32 MinNumOperands = TNumericLimits<int32>::Max();
		int32 MaxNumOperands = TNumericLimits<int32>::Min();
			
		for (const FActionHistoryItem& Item : AnalyticsActionHistory)
		{
			if (Item.ActionType == ActionType)
			{
				NumActions += 1;
				MeanNumOperands += static_cast<float>(Item.NumOperands);
				MinNumOperands = FGenericPlatformMath::Min(MinNumOperands, Item.NumOperands);
				MaxNumOperands = FGenericPlatformMath::Max(MaxNumOperands, Item.NumOperands);
			}
		}
		
		if (NumActions > 0)
		{
			MeanNumOperands /= NumActions;
			const FString ActionName = StaticEnum<ESelectToolAction>()->GetNameStringByIndex(static_cast<int>(ActionType));
			Attributes.Add(FAnalyticsEventAttribute(FString::Printf(TEXT("Stats.%sAction.NumActions"), *ActionName), NumActions));
			Attributes.Add(FAnalyticsEventAttribute(FString::Printf(TEXT("Stats.%sAction.MinNum%s"), *ActionName, *OperandName), MinNumOperands));
			Attributes.Add(FAnalyticsEventAttribute(FString::Printf(TEXT("Stats.%sAction.MaxNum%s"), *ActionName, *OperandName), MaxNumOperands));
			Attributes.Add(FAnalyticsEventAttribute(FString::Printf(TEXT("Stats.%sAction.MeanNum%s"), *ActionName, *OperandName), MeanNumOperands));
		}
	};
	MaybeAppendActionStatsToAttributes(ESelectToolAction::IslandConformalUnwrap, TEXT("Triangles"));
	MaybeAppendActionStatsToAttributes(ESelectToolAction::Split, TEXT("Edges"));
	MaybeAppendActionStatsToAttributes(ESelectToolAction::Sew, TEXT("Edges"));
	Attributes.Add(FAnalyticsEventAttribute(TEXT("Stats.ToolActiveDuration"), (FDateTime::UtcNow() - ToolStartTimeAnalytics).ToString()));
	
	FEngineAnalytics::GetProvider().RecordEvent(UVEditorAnalyticsEventName(TEXT("EditTool")), Attributes);

	if constexpr (false)
	{
		for (const FAnalyticsEventAttribute& Attr : Attributes)
		{
			UE_LOG(LogGeometry, Log, TEXT("Debug %s.EditTool.%s = %s"), *UVEditorAnalyticsPrefix, *Attr.GetName(), *Attr.GetValue());
		}
	}
}

#undef LOCTEXT_NAMESPACE
