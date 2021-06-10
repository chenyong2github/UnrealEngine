// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVSelectTool.h"

#include "BaseGizmos/TransformGizmo.h"
#include "ContextObjectStore.h"
#include "Drawing/LineSetComponent.h"
#include "Drawing/MeshElementsVisualizer.h"
#include "Drawing/PreviewGeometryActor.h"
#include "DynamicMeshChangeTracker.h"
#include "ToolTargets/UVEditorToolMeshInput.h"
#include "InteractiveToolManager.h"
#include "MeshOpPreviewHelpers.h" // UMeshOpPreviewWithBackgroundCompute
#include "PreviewMesh.h"
#include "Selection/MeshSelectionMechanic.h"
#include "Selection/DynamicMeshSelection.h"
#include "ToolSetupUtil.h"
#include "UVToolContextObjects.h"

#include "ToolTargetManager.h"

#define LOCTEXT_NAMESPACE "UUVSelectTool"

using namespace UE::Geometry;

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

	SelectionMechanic = NewObject<UMeshSelectionMechanic>();
	SelectionMechanic->Setup(this);
	SelectionMechanic->SetWorld(Targets[0]->UnwrapPreview->GetWorld());
	SelectionMechanic->OnSelectionChanged.AddUObject(this, &UUVSelectTool::OnSelectionChanged);

	SelectionMechanic->SelectionMode =
		Settings->SelectionMode == EUVSelectToolSelectionMode::Island ? EMeshSelectionMechanicMode::Component
		: EMeshSelectionMechanicMode::Edge;

	// Retrieve cached AABB tree storage, or else set it up
	UContextObjectStore* ContextStore = GetToolManager()->GetContextObjectStore();
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
		UUVToolMeshSelection* SelectionStore = ContextStore->FindContext<UUVToolMeshSelection>();
		if (!SelectionStore)
		{
			SelectionStore = NewObject<UUVToolMeshSelection>();
			ContextStore->AddContextObject(SelectionStore);
		}
		*SelectionStore->Selection = SelectionMechanic->GetCurrentSelection();
	}
	else
	{
		ContextStore->RemoveContextObjectsOfType<UUVToolMeshSelection>();
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
}

void UUVSelectTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	SelectionMechanic->SelectionMode =
		Settings->SelectionMode == EUVSelectToolSelectionMode::Island ? EMeshSelectionMechanicMode::Component
		: EMeshSelectionMechanicMode::Edge;
}

void UUVSelectTool::UpdateGizmo()
{
	const FDynamicMeshSelection& Selection = SelectionMechanic->GetCurrentSelection();

	if (!Selection.IsEmpty())
	{
		FVector3d Centroid = SelectionMechanic->GetCurrentSelectionCentroid();

		// TODO: We should either make it so that transparent materials don't affect the rendering
		// of the gizmo component, or else we should pass in some depth offset into the component.
		// For now we hack it by raising the gizmo a little off the plane manually.
		Centroid += FVector3d::ZAxisVector * KINDA_SMALL_NUMBER;

		TransformGizmo->ReinitializeGizmoTransform(FTransform((FVector)Centroid));
	}

	TransformGizmo->SetVisibility(bGizmoEnabled && !SelectionMechanic->GetCurrentSelection().IsEmpty());
}

void UUVSelectTool::OnSelectionChanged()
{
	const FDynamicMeshSelection& Selection = SelectionMechanic->GetCurrentSelection();

	SelectionTargetIndex = -1;
	MovingVids.Reset();
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
	FTransform DeltaTransform = Transform.GetRelativeTransform(InitialGizmoFrame.ToFTransform());

	if (!DeltaTransform.GetTranslation().IsNearlyZero() || !DeltaTransform.GetRotation().IsIdentity() || Transform.GetScale3D() != FVector::One())
	{
		FTransform3d TransformToApply(Transform);

		// TODO: The division here is a bit of a hack. Properly-speaking, the scaling handles should act relative to
		// gizmo size, not the visible space across which we drag, otherwise it becomes dependent on the units we
		// use and our absolute distance from the object. Since our UV unwrap is scaled by 1000 to make it
		// easier to zoom in and out without running into issues, the measure of the distance across which we typically
		// drag the handles is too high to be convenient. Until we make the scaling invariant to units/distance from
		// target, we use this hack.
		TransformToApply.SetScale(FVector::One() + (Transform.GetScale3D() - FVector::One()) / 10);

		const FDynamicMeshSelection& Selection = SelectionMechanic->GetCurrentSelection();

		Targets[SelectionTargetIndex]->UnwrapPreview->PreviewMesh->DeferredEditMesh([&TransformToApply,  &Transform, this](FDynamicMesh3& MeshIn)
		{
			for (int32 i = 0; i < MovingVids.Num(); ++i)
			{
				MeshIn.SetVertex(MovingVids[i], TransformToApply.TransformPosition(MovingVertOriginalPositions[i]));
			}
		}, false);
		Targets[SelectionTargetIndex]->UpdateUnwrapPreviewOverlay(&MovingVids);

		SelectionMechanic->SetDrawnElementsTransform((FTransform)TransformToApply);

		if (Settings->bUpdatePreviewDuringDrag)
		{
			Targets[SelectionTargetIndex]->UpdateAppliedPreviewFromUnwrapPreview(&MovingVids);
		}
	}	
}

void UUVSelectTool::GizmoTransformEnded(UTransformProxy* Proxy)
{
	// TODO: Add undo support
	//FDynamicMeshChangeTracker ChangeTracker(Targets[SelectionTargetIndex]->UnwrapPreview->PreviewMesh->GetMesh());
	//ChangeTracker.BeginChange();
	//ChangeTracker.SaveTriangles(MovingVids, true);

	if (Settings->bUpdatePreviewDuringDrag)
	{
		// Both previews must already be updated, so only need to update canonical
		Targets[SelectionTargetIndex]->UpdateCanonicalFromPreviews(&MovingVids);
	}
	else
	{
		Targets[SelectionTargetIndex]->UpdateAllFromUnwrapPreview(&MovingVids);
	}

	if (!AABBTrees[SelectionTargetIndex]->IsValid())
	{
		AABBTrees[SelectionTargetIndex]->Build();
	}

	TransformGizmo->SetNewChildScale(FVector::One());
	SelectionMechanic->RebuildDrawnElements(TransformGizmo->GetGizmoTransform());
}

void UUVSelectTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	SelectionMechanic->Render(RenderAPI);
}

void UUVSelectTool::OnTick(float DeltaTime)
{
}


#undef LOCTEXT_NAMESPACE
