// Copyright Epic Games, Inc. All Rights Reserved.

#include "UVSelectTool.h"

#include "BaseGizmos/TransformGizmo.h"
#include "TargetInterfaces/UVUnwrapDynamicMesh.h"
#include "InteractiveToolManager.h"
#include "PreviewMesh.h"
#include "Selection/MeshSelectionMechanic.h"
#include "Selection/DynamicMeshSelection.h"
#include "UVToolStateObjects.h"

#include "ToolTargetManager.h"

#define LOCTEXT_NAMESPACE "UUVSelectTool"

using namespace UE::Geometry;

/*
 * ToolBuilder
 */


// TODO: We may need to rework how targets are passed to this tool. Right now GetTargetRequirements
// is not used, and is not even accurate, since we don't even use UUVUnwrapDynamicMesh ourselves.
// Instead, the UVMode is the one that uses UUVUnwrapDynamicMesh to create meshes that it can display,
// and we just get those preview meshes passed to us. Perhaps we should get passed some sort of
// Target-wrapped preview mesh?
// The other option is to use UUVUnwrapDynamicMesh in the tool and cache it for use by others, but
// this ends up being a lot less clean to code up, and it would be preferable to avoid that messiness.
const FToolTargetTypeRequirements& UUVSelectToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements(UUVUnwrapDynamicMesh::StaticClass());
	return TypeRequirements;
}

bool UUVSelectToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return DisplayedMeshes->Num() > 0;
}

UInteractiveTool* UUVSelectToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UUVSelectTool* NewTool = NewObject<UUVSelectTool>(SceneState.ToolManager);
	NewTool->SetGizmoEnabled(bGizmoEnabled);
	NewTool->SetStateObjectStore(StateObjectStore);
	NewTool->SetDisplayedMeshes(*DisplayedMeshes);

	return NewTool;
}

void UUVSelectTool::Setup()
{
	check(DisplayedMeshes.Num() > 0);

	UInteractiveTool::Setup();

	SetToolDisplayName(LOCTEXT("ToolName", "UV Select Tool"));

	Settings = NewObject<UUVSelectToolProperties>(this);
	Settings->RestoreProperties(this);
	AddToolPropertySource(Settings);

	SelectionMechanic = NewObject<UMeshSelectionMechanic>();
	SelectionMechanic->Setup(this);
	SelectionMechanic->OnSelectionChanged.AddUObject(this, &UUVSelectTool::OnSelectionChanged);

	SelectionMechanic->SelectionMode =
		Settings->SelectionMode == EUVSelectToolSelectionMode::Island ? EMeshSelectionMechanicMode::Component
		: EMeshSelectionMechanicMode::Edge;


	// TODO: Should StateObjectStore be mandatory? We could enforce it in builder.
	// Restore any saved state
	if (StateObjectStore)
	{
		UUVMeshAABBTrees* TreeStore = StateObjectStore->GetToolStateObject<UUVMeshAABBTrees>();
		if (TreeStore)
		{
			AABBTrees = TreeStore->AABBTrees;
		}
			
		UUVMeshSelection* SelectionStore = StateObjectStore->GetToolStateObject<UUVMeshSelection>();
		if (SelectionStore)
		{
			SelectionMechanic->SetSelection(*SelectionStore->Selection);
		}
	}

	// Initialize the spatial structures if they weren't there to load
	if (AABBTrees.IsEmpty())
	{
		for (TObjectPtr<UPreviewMesh> Mesh : DisplayedMeshes)
		{
			TSharedPtr<FDynamicMeshAABBTree3> Tree = MakeShared<FDynamicMeshAABBTree3>();
			Tree->SetMesh(Mesh->GetMesh());
			AABBTrees.Add(Tree);
		}

		// Save them if appropriate
		if (StateObjectStore)
		{
			UUVMeshAABBTrees* TreeStore = NewObject<UUVMeshAABBTrees>();
			TreeStore->AABBTrees = AABBTrees;
			StateObjectStore->SetToolStateObject(TreeStore);
		}
	}

	// Add the spatial structures to the selection mechanic
	for (TSharedPtr<FDynamicMeshAABBTree3> Tree : AABBTrees)
	{
		SelectionMechanic->AddSpatial(Tree);
	}

	// Gizmo setup
	UInteractiveGizmoManager* GizmoManager = GetToolManager()->GetPairedGizmoManager();
	UTransformProxy* TransformProxy = NewObject<UTransformProxy>(this);
	TransformGizmo = GizmoManager->CreateCustomTransformGizmo(
		ETransformGizmoSubElements::TranslateAxisX | ETransformGizmoSubElements::TranslateAxisY | ETransformGizmoSubElements::TranslatePlaneXY
		//| ETransformGizmoSubElements::ScaleAxisX | ETransformGizmoSubElements::ScaleAxisY | ETransformGizmoSubElements::ScalePlaneXY
		| ETransformGizmoSubElements::RotateAxisZ,
		this);
	TransformProxy->OnBeginTransformEdit.AddUObject(this, &UUVSelectTool::GizmoTransformStarted);
	TransformProxy->OnTransformChanged.AddUObject(this, &UUVSelectTool::GizmoTransformChanged);
	TransformProxy->OnEndTransformEdit.AddUObject(this, &UUVSelectTool::GizmoTransformEnded);

	// Always align gizmo to x and y axes
	TransformGizmo->bUseContextCoordinateSystem = false;
	TransformGizmo->SetActiveTarget(TransformProxy, GetToolManager());

	if (!SelectionMechanic->GetCurrentSelection().IsEmpty())
	{
		OnSelectionChanged();
	}
	UpdateGizmo();
}

void UUVSelectTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (StateObjectStore)
	{
		UUVMeshSelection* SelectionStore = NewObject<UUVMeshSelection>();
		SelectionStore->Selection = MakeShared<FDynamicMeshSelection>();
		*SelectionStore->Selection = SelectionMechanic->GetCurrentSelection();
		StateObjectStore->SetToolStateObject(SelectionStore);
	}

	Settings->SaveProperties(this);

	SelectionMechanic->Shutdown();

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
		FVector3d Centroid = FVector3d::Zero();
		for (int32 Vid : MovingVids)
		{
			Centroid += Selection.Mesh->GetVertex(Vid);
		}
		Centroid /= MovingVids.Num();

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

	SelectionMeshIndex = -1;
	MovingVids.Reset();
	if (!Selection.IsEmpty())
	{
		// Note which mesh we're selecting in.
		for (int32 i = 0; i < DisplayedMeshes.Num(); ++i)
		{
			if (DisplayedMeshes[i]->GetMesh() == Selection.Mesh)
			{
				SelectionMeshIndex = i;
				break;
			}
		}
		check(SelectionMeshIndex >= 0);

		// Note the selected vids
		TSet<int32> VidSet;
		if (Selection.Type == FDynamicMeshSelection::EType::Triangle)
		{
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
	
	UpdateGizmo();
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
	const FDynamicMesh3* Mesh = DisplayedMeshes[SelectionMeshIndex]->GetMesh();
	for (int32 i = 0; i < MovingVids.Num(); ++i)
	{
		MovingVertOriginalPositions[i] = InitialGizmoFrame.ToFramePoint(Mesh->GetVertex(MovingVids[i]));
	}
}

void UUVSelectTool::GizmoTransformChanged(UTransformProxy* Proxy, FTransform Transform)
{
	FFrame3d NewGizmoFrame(TransformGizmo->ActiveTarget->GetTransform());
	FTransform DeltaTransform = NewGizmoFrame.ToFTransform().GetRelativeTransform(InitialGizmoFrame.ToFTransform());
	const FDynamicMeshSelection& Selection = SelectionMechanic->GetCurrentSelection();

	if (!DeltaTransform.GetTranslation().IsNearlyZero() || !DeltaTransform.GetRotation().IsIdentity())
	{
		DisplayedMeshes[SelectionMeshIndex]->EditMesh([&Selection, &NewGizmoFrame, this](FDynamicMesh3& MeshIn)
		{
			for (int32 i = 0; i < MovingVids.Num(); ++i)
			{
				MeshIn.SetVertex(MovingVids[i], NewGizmoFrame.FromFramePoint(MovingVertOriginalPositions[i]));
			}
		});

		// TODO: This doesn't get broadcast when EditMesh is called. Should it? Or should there be some other way
		// to inform the wireframe that the mesh changed?
		DisplayedMeshes[SelectionMeshIndex]->GetOnMeshChanged().Broadcast();
	}	
}

void UUVSelectTool::GizmoTransformEnded(UTransformProxy* Proxy)
{
	if (!AABBTrees[SelectionMeshIndex]->IsValid())
	{
		AABBTrees[SelectionMeshIndex]->Build();
	}
}

void UUVSelectTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	SelectionMechanic->Render(RenderAPI);
}

void UUVSelectTool::OnTick(float DeltaTime)
{
}


#undef LOCTEXT_NAMESPACE
