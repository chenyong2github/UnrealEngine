// Copyright Epic Games, Inc. All Rights Reserved.

#include "Selection/UVEditorMeshSelectionMechanic.h"

#include "BaseBehaviors/BehaviorTargetInterfaces.h"
#include "BaseBehaviors/SingleClickOrDragBehavior.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "ContextObjectStore.h"
#include "ContextObjects/UVToolContextObjects.h"
#include "ContextObjects/UVToolViewportButtonsAPI.h"
#include "Drawing/TriangleSetComponent.h"
#include "Drawing/LineSetComponent.h"
#include "Drawing/PointSetComponent.h"
#include "Drawing/PreviewGeometryActor.h"
#include "Intersection/IntrTriangle2AxisAlignedBox2.h"
#include "Intersection/IntersectionQueries2.h"
#include "InteractiveToolManager.h"
#include "MeshOpPreviewHelpers.h" // UMeshOpPreviewWithBackgroundCompute
#include "Polyline3.h"
#include "Selections/MeshConnectedComponents.h"
#include "Spatial/GeometrySet3.h"
#include "ToolSceneQueriesUtil.h"
#include "ToolSetupUtil.h"
#include "UVEditorUXSettings.h"

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UUVEditorMeshSelectionMechanic"

namespace UVEditorMeshSelectionMechanicLocals
{
	template <typename InElementType>
	void ToggleItem(TSet<InElementType>& Set, InElementType Item)
	{
		if (Set.Remove(Item) == 0)
		{
			Set.Add(Item);
		}
	}
	
	using ESelectionMode = UUVToolSelectionAPI::EUVEditorSelectionMode;
	using FModeChangeOptions = UUVToolSelectionAPI::FSelectionMechanicModeChangeOptions;

	FUVToolSelection::EType ToCompatibleDynamicMeshSelectionType(ESelectionMode Mode)
	{
		switch (Mode)
		{
			case ESelectionMode::Mesh:
			case ESelectionMode::Island:
			case ESelectionMode::Triangle:
				return FUVToolSelection::EType::Triangle;
			case ESelectionMode::Edge:
				return FUVToolSelection::EType::Edge;
			case ESelectionMode::Vertex:
				return FUVToolSelection::EType::Vertex;
			case ESelectionMode::None: //doesn't actually matter what we return
				return FUVToolSelection::EType::Vertex;
		}
		ensure(false);
		return FUVToolSelection::EType::Vertex;
	}

	// Returns the marquee selection rectangle, obtained from the given CameraRectangle, projected to the XY plane
	FAxisAlignedBox2d GetRectangleXY(const FCameraRectangle& CameraRectangle)
	{
		ensure(CameraRectangle.bIsInitialized);
		FAxisAlignedBox2d Result;
		
		double Offset = CameraRectangle.SelectionDomain.Plane.DistanceTo(FVector::ZeroVector);
		FCameraRectangle::FRectangleInPlane Domain = CameraRectangle.ProjectSelectionDomain(Offset);
		
		// This works because we know the UV axes are aligned with the XY axes, see the comment in UUVEditorMode::InitializeTargets
		const FVector MinPoint3D = CameraRectangle.PointUVToPoint3D(Domain.Plane, Domain.Rectangle.Min);
		const FVector MaxPoint3D = CameraRectangle.PointUVToPoint3D(Domain.Plane, Domain.Rectangle.Max);
		Result.Contain(FVector2d{MinPoint3D.X, MinPoint3D.Y}); // Convert to 2D and convert to double
		Result.Contain(FVector2d{MaxPoint3D.X, MaxPoint3D.Y});
	
		return Result;
	}
	
	FVector2d XY(const FVector3d& Point)
	{
		return {Point.X, Point.Y};
	}

	void AppendVertexIDs(const FDynamicMesh3& MeshXY0, int TriangleID, TArray<int>& VertexIDs)
	{
		const FIndex3i& Triangle = MeshXY0.GetTriangleRef(TriangleID);
		VertexIDs.Add(Triangle.A);
		VertexIDs.Add(Triangle.B);
		VertexIDs.Add(Triangle.C);
	}

	void AppendVertexIDsIfIntersected(const FDynamicMesh3& MeshXY0, const FAxisAlignedBox2d& RectangleXY, int TriangleID, TArray<int>& VertexIDs)
	{
		const FIndex3i& Triangle = MeshXY0.GetTriangleRef(TriangleID);
		if (RectangleXY.Contains(XY(MeshXY0.GetVertex(Triangle.A))))
		{
			VertexIDs.Add(Triangle.A);
		}
		
		if (RectangleXY.Contains(XY(MeshXY0.GetVertex(Triangle.B))))
		{
			VertexIDs.Add(Triangle.B);
		}
		
		if (RectangleXY.Contains(XY(MeshXY0.GetVertex(Triangle.C))))
		{
			VertexIDs.Add(Triangle.C);
		}
	}
	
	void AppendEdgeIDs(const FDynamicMesh3& MeshXY0, int TriangleID, TArray<int>& EdgeIDs)
	{
		const FIndex3i& Edges = MeshXY0.GetTriEdgesRef(TriangleID);
		EdgeIDs.Add(Edges.A);
		EdgeIDs.Add(Edges.B);
		EdgeIDs.Add(Edges.C);
	}
	
	void AppendEdgeIDsIfIntersected(const FDynamicMesh3& MeshXY0, const FAxisAlignedBox2d& RectangleXY, int TriangleID, TArray<int>& EdgeIDs)
	{
		const FIndex3i& Edges = MeshXY0.GetTriEdgesRef(TriangleID);

		const FIndex2i& EdgeA = MeshXY0.GetEdgeRef(Edges.A).Vert;
		const FSegment2d SegmentA(XY(MeshXY0.GetVertex(EdgeA.A)), XY(MeshXY0.GetVertex(EdgeA.B)));
		if (TestIntersection(SegmentA, RectangleXY))
		{
			EdgeIDs.Add(Edges.A);
		}
		
		const FIndex2i& EdgeB = MeshXY0.GetEdgeRef(Edges.B).Vert;
		const FSegment2d SegmentB(XY(MeshXY0.GetVertex(EdgeB.A)), XY(MeshXY0.GetVertex(EdgeB.B)));
		if (TestIntersection(SegmentB, RectangleXY))
		{
			EdgeIDs.Add(Edges.B);
		}
		
		const FIndex2i& EdgeC = MeshXY0.GetEdgeRef(Edges.C).Vert;
		const FSegment2d SegmentC(XY(MeshXY0.GetVertex(EdgeC.A)), XY(MeshXY0.GetVertex(EdgeC.B)));
		if (TestIntersection(SegmentC, RectangleXY))
		{
			EdgeIDs.Add(Edges.C);
		}
	}
	
	void AppendTriangleID(const FDynamicMesh3&, int TriangleID, TArray<int>& TriangleIDs)
	{
		TriangleIDs.Add(TriangleID);
	}

	void AppendTriangleIDIfIntersected(const FDynamicMesh3& MeshXY0, const FAxisAlignedBox2d& RectangleXY, int TriangleID, TArray<int>& TriangleIDs)
	{
		const FIndex3i& Triangle = MeshXY0.GetTriangleRef(TriangleID);
		const FTriangle2d TriangleXY(XY(MeshXY0.GetVertex(Triangle.A)),
									 XY(MeshXY0.GetVertex(Triangle.B)),
									 XY(MeshXY0.GetVertex(Triangle.C)));
		
		// Check with bTriangleIsOriented = false since some triangles maybe oriented away from the camera
		if (FIntrTriangle2AxisAlignedBox2d Intersects(TriangleXY, RectangleXY, false); Intersects.Test())
		{
			TriangleIDs.Add(TriangleID);
		}
	}
	
	// Returns indices, collected by the given functions, from triangles which are intersected by the given rectangle.
	// TreeXY0 must contain a mesh with vertices in the XY plane (have zero Z coordinate)
	template<typename IDsFromTriangleF, typename IDsFromTriangleIfIntersectedF>
	TArray<int32> FindAllIntersectionsAxisAlignedBox2(const FDynamicMeshAABBTree3& TreeXY0,
													  const FAxisAlignedBox2d& RectangleXY,
													  IDsFromTriangleF AppendIDs,
													  IDsFromTriangleIfIntersectedF AppendIDsIfIntersected)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FindAllIntersectionsAxisAlignedBox2);
		
		check(TreeXY0.GetMesh());
		
		TArray<int32> Result;
		FAxisAlignedBox2d TreeRectangleXY;
		TreeRectangleXY.Contain(XY(TreeXY0.GetBoundingBox().Min));
		TreeRectangleXY.Contain(XY(TreeXY0.GetBoundingBox().Max));
		if (RectangleXY.Contains(TreeRectangleXY))
		{
			// Early out selecting everything
			Result.Reserve(TreeXY0.GetMesh()->TriangleCount());
			for (int TriangleID : TreeXY0.GetMesh()->TriangleIndicesItr())
			{
				AppendIDs(*TreeXY0.GetMesh(), TriangleID, Result);
			}
			return Result;
		}
		
		int SelectAllDepth = TNumericLimits<int>::Max();
		int CurrentDepth = -1;
		
		// Traversal is depth first
		FDynamicMeshAABBTree3::FTreeTraversal Traversal;
		
		Traversal.NextBoxF =
			[&RectangleXY, &SelectAllDepth, &CurrentDepth](const FAxisAlignedBox3d& Box, int Depth)
		{
			CurrentDepth = Depth;
			if (Depth > SelectAllDepth)
			{
				// We are deeper than the depth whose AABB was first detected to be contained in the RectangleXY,
				// descend and collect all leaf triangles
				return true;
			}
			
			SelectAllDepth = TNumericLimits<int>::Max();
			
			const FAxisAlignedBox2d BoxXY(XY(Box.Min), XY(Box.Max));
			if (RectangleXY.Intersects(BoxXY))
			{
				if (RectangleXY.Contains(BoxXY))
				{
					SelectAllDepth = Depth;
				}
				
				return true;		
			}
			return false;
		};
		
		Traversal.NextTriangleF =
			[&RectangleXY, &SelectAllDepth, &CurrentDepth, &TreeXY0, &Result, &AppendIDs, &AppendIDsIfIntersected]
			(int TriangleID)
		{
			if (CurrentDepth >= SelectAllDepth)
			{
				// This TriangleID is entirely contained in the selection rectangle so we can skip intersection testing
				return AppendIDs(*TreeXY0.GetMesh(), TriangleID, Result);
			}
			return AppendIDsIfIntersected(*TreeXY0.GetMesh(), RectangleXY, TriangleID, Result);
		};
		
		TreeXY0.DoTraversal(Traversal);

		return Result;
	}

	bool ConvertToHitElementList(ESelectionMode SelectionMode, 
		const FDynamicMesh3& Mesh, int32 HitTid, const FViewCameraState& CameraState, 
		const FRay& Ray, TArray<int32>& IDsOut)
	{
		if (!ensure(HitTid != IndexConstants::InvalidID && Mesh.IsTriangle(HitTid)))
		{
			return false;
		}

		IDsOut.Reset();

		switch (SelectionMode)
		{
		case ESelectionMode::Island:
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Component);

			FMeshConnectedComponents MeshSelectedComponent(&Mesh);
			TArray<int32> SeedTriangles;
			SeedTriangles.Add(HitTid);
			MeshSelectedComponent.FindTrianglesConnectedToSeeds(SeedTriangles);
			ensure(MeshSelectedComponent.Components.Num() == 1); // Expect each triangle to only be in a single component
			IDsOut.Append(MoveTemp(MeshSelectedComponent.Components[0].Indices));
			break;
		}
		case ESelectionMode::Edge:
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Edge);
			// TODO: We'll need the ability to hit occluded triangles to see if there is a better edge to snap to.

			// Try to snap to one of the edges.
			FIndex3i Eids = Mesh.GetTriEdges(HitTid);

			FGeometrySet3 GeometrySet;
			for (int i = 0; i < 3; ++i)
			{
				FIndex2i Vids = Mesh.GetEdgeV(Eids[i]);
				FPolyline3d Polyline(Mesh.GetVertex(Vids.A), Mesh.GetVertex(Vids.B));
				GeometrySet.AddCurve(Eids[i], Polyline);
			}

			FGeometrySet3::FNearest Nearest;
			if (GeometrySet.FindNearestCurveToRay(Ray, Nearest,
				[&CameraState](const FVector3d& Position1, const FVector3d& Position2) {
					return ToolSceneQueriesUtil::PointSnapQuery(CameraState,
						Position1, Position2,
						ToolSceneQueriesUtil::GetDefaultVisualAngleSnapThreshD()); }))
			{
				IDsOut.Add(Nearest.ID);
			}
			break;
		}
		case ESelectionMode::Vertex:
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Vertex);
			// TODO: Improve this to handle super narrow, sliver triangles better, where testing near vertices can be difficult.

			// Try to snap to one of the vertices
			FIndex3i Vids = Mesh.GetTriangle(HitTid);

			FGeometrySet3 GeometrySet;
			for (int i = 0; i < 3; ++i)
			{
				GeometrySet.AddPoint(Vids[i], Mesh.GetTriVertex(HitTid, i));
			}

			FGeometrySet3::FNearest Nearest;
			if (GeometrySet.FindNearestPointToRay(Ray, Nearest,
				[&CameraState](const FVector3d& Position1, const FVector3d& Position2) {
					return ToolSceneQueriesUtil::PointSnapQuery(CameraState,
						Position1, Position2,
						ToolSceneQueriesUtil::GetDefaultVisualAngleSnapThreshD()); }))
			{
				IDsOut.Add(Nearest.ID);
			}
			break;
		}
		case ESelectionMode::Triangle:
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Triangle);

			IDsOut.Add(HitTid);
			break;
		}
		case ESelectionMode::Mesh:
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Mesh);

			for (int32 Tid : Mesh.TriangleIndicesItr())
			{
				IDsOut.Add(Tid);
			}
			break;
		}
		default:
			ensure(false);
			break;
		}

		return !IDsOut.IsEmpty();
	}

	/**
	 * Undo/redo transaction for selection mode changes
	 */
	class  FModeChange : public FToolCommandChange
	{
	public:
		FModeChange(ESelectionMode BeforeIn, 
			ESelectionMode AfterIn)
			: Before(BeforeIn)
			, After(AfterIn)
		{};

		virtual void Apply(UObject* Object) override
		{
			UUVEditorMeshSelectionMechanic* SelectionMechanic = Cast<UUVEditorMeshSelectionMechanic>(Object);
			FModeChangeOptions Options;
			Options.bConvertExisting = false;
			Options.bBroadcastIfConverted = false;
			Options.bEmitChanges = false;
			SelectionMechanic->SetSelectionMode(After, Options);
		}

		virtual void Revert(UObject* Object) override
		{
			UUVEditorMeshSelectionMechanic* SelectionMechanic = Cast<UUVEditorMeshSelectionMechanic>(Object);
			FModeChangeOptions Options;
			Options.bConvertExisting = false;
			Options.bBroadcastIfConverted = false;
			Options.bEmitChanges = false;
			SelectionMechanic->SetSelectionMode(Before, Options);
		}

		virtual bool HasExpired(UObject* Object) const override
		{
			UUVEditorMeshSelectionMechanic* SelectionMechanic = Cast<UUVEditorMeshSelectionMechanic>(Object);
			return !SelectionMechanic->IsEnabled();
		}


		virtual FString ToString() const override
		{
			return TEXT("UVEditorMeshSelectionMechanicLocals::FModeChange");
		}

	protected:
		ESelectionMode Before;
		ESelectionMode After;
	};
} // namespace UVEditorMeshSelectionMechanicLocals


void UUVEditorMeshSelectionMechanic::Setup(UInteractiveTool* ParentToolIn)
{
	UInteractionMechanic::Setup(ParentToolIn);

	UContextObjectStore* ContextStore = GetParentTool()->GetToolManager()->GetContextObjectStore();
	EmitChangeAPI = ContextStore->FindContext<UUVToolEmitChangeAPI>();
	check(EmitChangeAPI);

	// This will be the target for the click drag behavior below
	MarqueeMechanic = NewObject<URectangleMarqueeMechanic>();
	MarqueeMechanic->bUseExternalClickDragBehavior = true;
	MarqueeMechanic->Setup(ParentToolIn);
	MarqueeMechanic->OnDragRectangleStarted.AddUObject(this, &UUVEditorMeshSelectionMechanic::OnDragRectangleStarted);
	// TODO(Performance) :DynamicMarqueeSelection It would be cool to have the marquee selection update dynamically as
	//  the rectangle gets changed, right now this isn't interactive for large meshes so we disabled it
	//MarqueeMechanic->OnDragRectangleChanged.AddUObject(this, &UUVEditorMeshSelectionMechanic::OnDragRectangleChanged);
	MarqueeMechanic->OnDragRectangleFinished.AddUObject(this, &UUVEditorMeshSelectionMechanic::OnDragRectangleFinished);

	USingleClickOrDragInputBehavior* ClickOrDragBehavior = NewObject<USingleClickOrDragInputBehavior>();
	ClickOrDragBehavior->Initialize(this, MarqueeMechanic);
	ClickOrDragBehavior->Modifiers.RegisterModifier(ShiftModifierID, FInputDeviceState::IsShiftKeyDown);
	ClickOrDragBehavior->Modifiers.RegisterModifier(CtrlModifierID, FInputDeviceState::IsCtrlKeyDown);
	ParentTool->AddInputBehavior(ClickOrDragBehavior);

	UMouseHoverBehavior* HoverBehavior = NewObject<UMouseHoverBehavior>();
	HoverBehavior->Initialize(this);
	ParentTool->AddInputBehavior(HoverBehavior);

	ViewportButtonsAPI = ContextStore->FindContext<UUVToolViewportButtonsAPI>();
	check(ViewportButtonsAPI);
	ViewportButtonsAPI->OnSelectionModeChange.AddWeakLambda(this,
		[this](UUVToolSelectionAPI::EUVEditorSelectionMode NewMode) {
			SetSelectionMode(NewMode);
		});
	// Make sure we match the activated button
	FModeChangeOptions ModeChangeOptions;
	ModeChangeOptions.bEmitChanges = false;
	SetSelectionMode(ViewportButtonsAPI->GetSelectionMode(), 
		ModeChangeOptions); // convert, broadcast, don't emit

	SetIsEnabled(bIsEnabled);
}

void UUVEditorMeshSelectionMechanic::Initialize(UWorld* World, UUVToolSelectionAPI* SelectionAPIIn)
{
	// It may be unreasonable to worry about Initialize being called more than once, but let's be safe anyway
	if (HoverGeometryActor)
	{
		HoverGeometryActor->Destroy();
	}

	SelectionAPI = SelectionAPIIn;
	
	HoverGeometryActor = World->SpawnActor<APreviewGeometryActor>();

	HoverTriangleSet = NewObject<UTriangleSetComponent>(HoverGeometryActor);
	HoverTriangleSetMaterial = ToolSetupUtil::GetCustomTwoSidedDepthOffsetMaterial(GetParentTool()->GetToolManager(),
		FUVEditorUXSettings::SelectionHoverTriangleFillColor,
		FUVEditorUXSettings::SelectionHoverTriangleDepthBias,
		FUVEditorUXSettings::SelectionHoverTriangleOpacity);
	HoverGeometryActor->SetRootComponent(HoverTriangleSet.Get());
	HoverTriangleSet->RegisterComponent();

	HoverPointSet = NewObject<UPointSetComponent>(HoverGeometryActor);
	HoverPointSet->SetPointMaterial(ToolSetupUtil::GetDefaultPointComponentMaterial(GetParentTool()->GetToolManager(), false));
	HoverPointSet->AttachToComponent(HoverTriangleSet.Get(), FAttachmentTransformRules::KeepWorldTransform);
	HoverPointSet->RegisterComponent();
	
	HoverLineSet = NewObject<ULineSetComponent>(HoverGeometryActor);
	HoverLineSet->SetLineMaterial(ToolSetupUtil::GetDefaultLineComponentMaterial(GetParentTool()->GetToolManager(), false));
	HoverLineSet->AttachToComponent(HoverTriangleSet.Get(), FAttachmentTransformRules::KeepWorldTransform);
	HoverLineSet->RegisterComponent();
}

void UUVEditorMeshSelectionMechanic::SetIsEnabled(bool bIsEnabledIn)
{
	bIsEnabled = bIsEnabledIn;
	if (MarqueeMechanic)
	{
		MarqueeMechanic->SetIsEnabled(bIsEnabled && SelectionMode != ESelectionMode::None);
	}
	if (ViewportButtonsAPI)
	{
		ViewportButtonsAPI->SetSelectionButtonsEnabled(bIsEnabledIn);
	}
}

void UUVEditorMeshSelectionMechanic::SetShowHoveredElements(bool bShow)
{
	bShowHoveredElements = bShow;
	if (!bShowHoveredElements)
	{
		HoverPointSet->Clear();
		HoverLineSet->Clear();
		HoverTriangleSet->Clear();
	}
}

void UUVEditorMeshSelectionMechanic::Shutdown()
{
	if (HoverGeometryActor)
	{
		HoverGeometryActor->Destroy();
		HoverGeometryActor = nullptr;
	}
	SelectionAPI = nullptr;
	ViewportButtonsAPI = nullptr;
	EmitChangeAPI = nullptr;
	MarqueeMechanic = nullptr;
	HoverTriangleSetMaterial = nullptr;
}

void UUVEditorMeshSelectionMechanic::SetTargets(const TArray<TObjectPtr<UUVEditorToolMeshInput>>& TargetsIn)
{
	Targets = TargetsIn;

	// Retrieve cached AABB tree storage, or else set it up
	UContextObjectStore* ContextStore = ParentTool->GetToolManager()->GetContextObjectStore();
	UUVToolAABBTreeStorage* TreeStore = ContextStore->FindContext<UUVToolAABBTreeStorage>();
	if (!TreeStore)
	{
		TreeStore = NewObject<UUVToolAABBTreeStorage>();
		ContextStore->AddContextObject(TreeStore);
	}

	// Get or create spatials
	// Initialize the AABB trees from cached values, or make new ones
	MeshSpatials.Reset();
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
		MeshSpatials.Add(Tree);
	}
}

void UUVEditorMeshSelectionMechanic::Render(IToolsContextRenderAPI* RenderAPI)
{
	MarqueeMechanic->Render(RenderAPI);

	// Cache the camera state
	GetParentTool()->GetToolManager()->GetContextQueriesAPI()->GetCurrentViewState(CameraState);
}

void UUVEditorMeshSelectionMechanic::DrawHUD(FCanvas* Canvas, IToolsContextRenderAPI* RenderAPI)
{
	MarqueeMechanic->DrawHUD(Canvas, RenderAPI);
}

FInputRayHit UUVEditorMeshSelectionMechanic::IsHitByClick(const FInputDeviceRay& ClickPos)
{
	FInputRayHit Hit; 
	// If enabled, return a hit so we always capture and can clear the selection
	Hit.bHit = bIsEnabled && SelectionMode != ESelectionMode::None;
	return Hit;	
}


void UUVEditorMeshSelectionMechanic::SetSelectionMode(
	ESelectionMode TargetMode, const FModeChangeOptions& Options)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_ChangeSelectionMode);

	const FText TransactionName = LOCTEXT("ChangeSelectionMode", "Change Selection Mode");
	
	using namespace UVEditorMeshSelectionMechanicLocals;

	ESelectionMode OldMode = SelectionMode;
	SelectionMode = TargetMode;
	if (OldMode == SelectionMode || !bIsEnabled || !SelectionAPI)
	{
		return;
	}

	if (ViewportButtonsAPI)
	{
		// Not clear whether we should or shouldn't broadcast this. A user could conceivably set selection
		// via mechanic and expect for a notification from the viewport buttons, but it feels wrong to
		// knowingly trigger a second call into this function if we broadcast, and that example seems like
		// questionable code organization...
		ViewportButtonsAPI->SetSelectionMode(SelectionMode, false);
	}

	if (Options.bEmitChanges)
	{
		EmitChangeAPI->BeginUndoTransaction(TransactionName);
		EmitChangeAPI->EmitToolIndependentChange(this, MakeUnique<FModeChange>(OldMode, SelectionMode), TransactionName);
	}

	MarqueeMechanic->SetIsEnabled(bIsEnabled && SelectionMode != ESelectionMode::None);

	// See whether a conversion is not necessary
	FUVToolSelection::EType ExpectedSelectionType = ToCompatibleDynamicMeshSelectionType(SelectionMode);
	FUVToolSelection::EType CurrentSelectionType = SelectionAPI->GetSelectionsType();
	if (!SelectionAPI->HaveSelections() || ExpectedSelectionType == CurrentSelectionType 
		|| !Options.bConvertExisting || SelectionMode == ESelectionMode::None)
	{
		// No conversion needed
		if (Options.bEmitChanges)
		{
			EmitChangeAPI->EndUndoTransaction();
		}
		return;
	}
	
	// We're going to convert the existing selection.
	const TArray<FUVToolSelection>& OriginalSelections = SelectionAPI->GetSelections();
	TArray<FUVToolSelection> NewSelections;

	for (const FUVToolSelection& OriginalSelection : OriginalSelections)
	{
		FDynamicMesh3* Mesh = OriginalSelection.Target->UnwrapCanonical.Get();
		
		FUVToolSelection NewSelection;
		NewSelection.Target = OriginalSelection.Target;
		NewSelection.Type = ExpectedSelectionType;

		const TSet<int32> OriginalIDs = OriginalSelection.SelectedIDs;
		TSet<int32>& NewIDs = NewSelection.SelectedIDs;

		auto VerticesToEdges = [Mesh, &OriginalIDs, &NewIDs]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_ChangeSelectionMode_VerticesToEdges);

			for (int32 Vid : OriginalIDs)
			{
				for (int32 Eid : Mesh->VtxEdgesItr(Vid))
				{
					if (!NewIDs.Contains(Eid))
					{
						FIndex2i Verts = Mesh->GetEdgeV(Eid);
						if (OriginalIDs.Contains(Verts.A) &&
							OriginalIDs.Contains(Verts.B))
						{
							NewIDs.Add(Eid);
						}
					}
				}
			}
		};

		auto VerticesToTriangles = [Mesh, &OriginalIDs, &NewIDs]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_ChangeSelectionMode_VerticesToTriangles);

			for (int32 Vid : OriginalIDs)
			{
				for (int32 Tid : Mesh->VtxTrianglesItr(Vid))
				{
					if (!NewIDs.Contains(Tid))
					{
						FIndex3i Verts = Mesh->GetTriangle(Tid);
						if (OriginalIDs.Contains(Verts.A) &&
							OriginalIDs.Contains(Verts.B) &&
							OriginalIDs.Contains(Verts.C))
						{
							NewIDs.Add(Tid);
						}
					}
				}
			}
		};

		auto EdgesToVertices = [Mesh, &OriginalIDs, &NewIDs]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_ChangeSelectionMode_EdgesToVertices);

			for (int32 Eid : OriginalIDs)
			{
				FIndex2i Verts = Mesh->GetEdgeV(Eid);
				NewIDs.Add(Verts.A);
				NewIDs.Add(Verts.B);
			}
		};

		// Triangles with two selected edges will be selected
		auto EdgesToTriangles = [Mesh, &OriginalIDs, &NewIDs]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_ChangeSelectionMode_EdgesToTriangles);

			TArray<int32> FoundTriangles;
			for (int32 Eid : OriginalIDs)
			{
				FIndex2i Tris = Mesh->GetEdgeT(Eid);
				FoundTriangles.Add(Tris.A);
				if (Tris.B != IndexConstants::InvalidID)
				{
					FoundTriangles.Add(Tris.B);
				}
			}

			if (FoundTriangles.Num() < 2)
			{
				return;
			}

			Algo::Sort(FoundTriangles);

			for (int I = 0; I < FoundTriangles.Num() - 1; I++)
			{
				if (FoundTriangles[I] == FoundTriangles[I + 1])
				{
					NewIDs.Add(FoundTriangles[I]);
					I++;
				}
			}
		};

		auto TrianglesToVertices = [Mesh, &OriginalIDs, &NewIDs]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_ChangeSelectionMode_TrianglesToVertices);

			for (int32 Tid : OriginalIDs)
			{
				FIndex3i Verts = Mesh->GetTriangle(Tid);
				NewIDs.Add(Verts.A);
				NewIDs.Add(Verts.B);
				NewIDs.Add(Verts.C);
			}
		};

		auto TrianglesToEdges = [Mesh, &OriginalIDs, &NewIDs]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_ChangeSelectionMode_TrianglesToEdges);

			for (int32 Tid : OriginalIDs)
			{
				FIndex3i Edges = Mesh->GetTriEdgesRef(Tid);
				NewIDs.Add(Edges.A);
				NewIDs.Add(Edges.B);
				NewIDs.Add(Edges.C);
			}
		};

		switch (CurrentSelectionType)
		{
		case FUVToolSelection::EType::Vertex:
			switch (ExpectedSelectionType)
			{
			case FUVToolSelection::EType::Vertex:
				ensure(false); // Should have been an early-out
				break;
			case FUVToolSelection::EType::Edge:
				VerticesToEdges();
				break;
			case FUVToolSelection::EType::Triangle:
				VerticesToTriangles();
				break;
			}
			break; // SelectionMode
		case FUVToolSelection::EType::Edge:
			switch (ExpectedSelectionType)
			{
			case FUVToolSelection::EType::Vertex:
				EdgesToVertices();
				break;
			case FUVToolSelection::EType::Edge:
				ensure(false); // Should have been an early-out
				break;
			case FUVToolSelection::EType::Triangle:
				EdgesToTriangles();
				break;
			}
			break; // SelectionMode
		case FUVToolSelection::EType::Triangle:
			switch (ExpectedSelectionType)
			{
			case FUVToolSelection::EType::Vertex:
				TrianglesToVertices();
				break;
			case FUVToolSelection::EType::Edge:
				TrianglesToEdges();
				break;
			case FUVToolSelection::EType::Triangle:
				ensure(false); // Should have been an early-out
				break;
			}
			break; // SelectionMode
		}

		if (!NewSelection.IsEmpty())
		{
			NewSelections.Add(MoveTemp(NewSelection));
		}
	}

	// Apply selection change
	SelectionAPI->SetSelections(NewSelections, Options.bBroadcastIfConverted, 
		Options.bEmitChanges);

	if (Options.bEmitChanges)
	{
		EmitChangeAPI->EndUndoTransaction();
	}
	
	return;
}

void UUVEditorMeshSelectionMechanic::ModifyExistingSelection(TSet<int32>& SelectionSetToModify, 
	const TArray<int32>& SelectedIDs)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_UpdateCurrentSelection);
	
	using namespace UVEditorMeshSelectionMechanicLocals;

	if (ShouldAddToSelection())
	{
		SelectionSetToModify.Append(SelectedIDs);
	}
	else if (ShouldToggleFromSelection())
	{
		for (int32 ID : SelectedIDs)
		{
			ToggleItem(SelectionSetToModify, ID);
		}
	}
	else if (ShouldRemoveFromSelection())
	{
		SelectionSetToModify = SelectionSetToModify.Difference(TSet<int32>(SelectedIDs));
	}
	else
	{
		// We shouldn't be trying to modify an existing selection if we're supposed to restart
		ensure(false);
	}
}

void UUVEditorMeshSelectionMechanic::OnClicked(const FInputDeviceRay& ClickPos)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_OnClicked);
	
	using namespace UVEditorMeshSelectionMechanicLocals;

	// IsHitByClick should prevent us being here with !bIsEnabled
	if (!ensure(bIsEnabled))
	{
		return;
	}

	FUVToolSelection::EType ElementType = ToCompatibleDynamicMeshSelectionType(SelectionMode);

	int32 HitAssetID = IndexConstants::InvalidID;
	int32 HitTid = IndexConstants::InvalidID;
	int32 ExistingSelectionIndex = IndexConstants::InvalidID;
	TArray<int32> NewIDs;

	// Do the raycast and get selected elements
	if (!GetHitTid(ClickPos, HitTid, HitAssetID, &ExistingSelectionIndex)
		|| !ConvertToHitElementList(SelectionMode, *Targets[HitAssetID]->UnwrapCanonical,
				HitTid, CameraState, ClickPos.WorldRay, NewIDs))
	{
		// Failed to select an element. See if selection needs clearing, and exit.
		if (ShouldRestartSelection() && SelectionAPI->HaveSelections())
		{
			SelectionAPI->ClearSelections(true, true); // broadcast and emit
		}
		return;
	}

	TArray<FUVToolSelection> NewSelections;
	if (!ShouldRestartSelection())
	{
		NewSelections = SelectionAPI->GetSelections();
	}

	if (NewIDs.IsEmpty())
	{
		// Nothing to add or modify.
	}
	else if (ShouldRestartSelection() 
		|| (ExistingSelectionIndex == IndexConstants::InvalidID && !ShouldRemoveFromSelection()))
	{
		// Make a new selection object
		NewSelections.Emplace();
		NewSelections.Last().Target = Targets[HitAssetID];
		NewSelections.Last().Type = ElementType;
		NewSelections.Last().SelectedIDs.Append(NewIDs);
	}
	else if (ExistingSelectionIndex != IndexConstants::InvalidID)
	{
		// Modify the existing selection object
		ModifyExistingSelection(NewSelections[ExistingSelectionIndex].SelectedIDs, NewIDs);

		// Object may end up empty due to subtraction or toggle, in which case it needs to be removed.
		if (NewSelections[ExistingSelectionIndex].IsEmpty())
		{
			NewSelections.RemoveAt(ExistingSelectionIndex);
		}
	}
	else
	{
		// The only way we can get here is if didn't have an existing selection and were trying
		// to remove selection, in which case we do nothing.
		ensure(ExistingSelectionIndex == IndexConstants::InvalidID && ShouldRemoveFromSelection());
	}

	SelectionAPI->SetSelections(NewSelections, true, true); // broadcast and emit
}

bool UUVEditorMeshSelectionMechanic::GetHitTid(const FInputDeviceRay& ClickPos, 
	int32& TidOut, int32& AssetIDOut, int32* ExistingSelectionObjectIndexOut)
{
	auto RayCastSpatial = [this, &ClickPos, &TidOut, &AssetIDOut](int32 AssetID) {
		double RayT = 0;
		if (MeshSpatials[AssetID]->FindNearestHitTriangle(ClickPos.WorldRay, RayT, TidOut))
		{
			AssetIDOut = AssetID;
			return true;
		}
		return false;
	};

	// Try raycasting the selected meshes first
	TArray<bool> SpatialTriedFlags;
	SpatialTriedFlags.SetNum(MeshSpatials.Num());
	const TArray<FUVToolSelection>& Selections = SelectionAPI->GetSelections();
	for (int32 SelectionIndex = 0; SelectionIndex < Selections.Num(); ++SelectionIndex)
	{
		const FUVToolSelection& Selection = Selections[SelectionIndex];
		if (ensure(Selection.Target.IsValid() && Selection.Target->AssetID < MeshSpatials.Num()))
		{
			if (RayCastSpatial(Selection.Target->AssetID))
			{
				if (ExistingSelectionObjectIndexOut)
				{
					*ExistingSelectionObjectIndexOut = SelectionIndex;
				}
				return true;
			}
			SpatialTriedFlags[Selection.Target->AssetID] = true;
		}
	}

	if (ExistingSelectionObjectIndexOut)
	{
		*ExistingSelectionObjectIndexOut = IndexConstants::InvalidID;
	}

	// Try raycasting the other meshes
	for (int32 AssetID = 0; AssetID < MeshSpatials.Num(); ++AssetID)
	{
		if (SpatialTriedFlags[AssetID])
		{
			continue;
		}
		if (RayCastSpatial(AssetID))
		{
			return true;
		}
	}

	return false;
}

void UUVEditorMeshSelectionMechanic::OnDragRectangleStarted()
{
	using namespace UVEditorMeshSelectionMechanicLocals;

	TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_OnDragRectangleStarted); // Mark start of drag sequence
	
	PreDragSelections = SelectionAPI->GetSelections();
	SelectionAPI->BeginChange();

	AssetIDToPreDragSelection.Reset();
	AssetIDToPreDragSelection.SetNumZeroed(Targets.Num());
	FUVToolSelection::EType ExpectedSelectionType = ToCompatibleDynamicMeshSelectionType(SelectionMode);
	if (SelectionAPI->HaveSelections() 
		&& SelectionAPI->GetSelectionsType() == ExpectedSelectionType)
	{
		for (FUVToolSelection& Selection : PreDragSelections)
		{
			if (ensure(Selection.Type == ExpectedSelectionType))
			{
				AssetIDToPreDragSelection[Selection.Target->AssetID] = &Selection;
			}
		}
	}
}

void UUVEditorMeshSelectionMechanic::OnDragRectangleChanged(const FCameraRectangle& CurrentRectangle)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_OnDragRectangleChanged);

	using namespace UVEditorMeshSelectionMechanicLocals;

	FAxisAlignedBox2d RectangleXY = GetRectangleXY(CurrentRectangle);
	TArray<FUVToolSelection> NewSelections;
	FUVToolSelection::EType SelectionType = ToCompatibleDynamicMeshSelectionType(SelectionMode);

	// Gather IDs in each target
	for (int32 AssetID = 0; AssetID < Targets.Num(); ++AssetID)
	{
		TArray<int32> RectangleSelectedIDs;
		const FDynamicMeshAABBTree3& Tree = *MeshSpatials[AssetID];

		if (SelectionMode == ESelectionMode::Vertex)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_OnDragRectangleChanged_Vertex);

			RectangleSelectedIDs = FindAllIntersectionsAxisAlignedBox2(
				Tree, RectangleXY, AppendVertexIDs, AppendVertexIDsIfIntersected);
		}
		else if (SelectionMode == ESelectionMode::Edge)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_OnDragRectangleChanged_Edge);

			RectangleSelectedIDs = FindAllIntersectionsAxisAlignedBox2(
				Tree, RectangleXY, AppendEdgeIDs, AppendEdgeIDsIfIntersected);
		}
		else if (SelectionMode == ESelectionMode::Triangle)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_OnDragRectangleChanged_Triangle);

			RectangleSelectedIDs = FindAllIntersectionsAxisAlignedBox2(
				Tree, RectangleXY, AppendTriangleID, AppendTriangleIDIfIntersected);
		}
		else if (SelectionMode == ESelectionMode::Island)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_OnDragRectangleChanged_Component);

			TArray<int32> SeedTriangles = FindAllIntersectionsAxisAlignedBox2(
				Tree, RectangleXY, AppendTriangleID, AppendTriangleIDIfIntersected);

			// TODO(Performance) For large meshes and selections following code is MUCH slower than AABB traversal,
			//  consider precomputing the connected components an only updating them when the mesh topology changes
			//  rather than every time the selection changes.
			FMeshConnectedComponents MeshSelectedComponent(Tree.GetMesh());
			MeshSelectedComponent.FindTrianglesConnectedToSeeds(SeedTriangles);
			for (int ComponentIndex = 0; ComponentIndex < MeshSelectedComponent.Components.Num(); ComponentIndex++)
			{
				RectangleSelectedIDs.Append(MoveTemp(MeshSelectedComponent.Components[ComponentIndex].Indices));
			}
		}
		else if (SelectionMode == ESelectionMode::Mesh)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_OnDragRectangleChanged_Mesh);

			// TODO: This shouldn't be a "find all". We can return early after the first success
			// since we're selecting the whole mesh
			TArray<int32> SelectedIDs = FindAllIntersectionsAxisAlignedBox2(
				Tree, RectangleXY, AppendTriangleID, AppendTriangleIDIfIntersected);
			if (!SelectedIDs.IsEmpty())
			{
				for (int32 Tid : Tree.GetMesh()->TriangleIndicesItr())
				{
					RectangleSelectedIDs.Add(Tid);
				}
			}
		}
		else
		{
			checkSlow(false);
		}

		// See if we have an object in our selection list that corresponds to this asset
		const FUVToolSelection* PreDragSelection = AssetIDToPreDragSelection[AssetID];

		if (RectangleSelectedIDs.IsEmpty())
		{
			if (!ShouldRestartSelection() && PreDragSelection)
			{
				// Keep the existing selection object with no modification.
				NewSelections.Emplace(*PreDragSelection);
			}
		}
		else if (ShouldRestartSelection() || (!PreDragSelection && !ShouldRemoveFromSelection()))
		{
			// Make a new selection object
			NewSelections.Emplace();
			NewSelections.Last().Target = Targets[AssetID];
			NewSelections.Last().Type = SelectionType;
			NewSelections.Last().SelectedIDs.Append(RectangleSelectedIDs);
		}
		else if (PreDragSelection)
		{
			// Modify the existing selection object
			FUVToolSelection NewSelection(*PreDragSelection);
			ModifyExistingSelection(NewSelection.SelectedIDs, RectangleSelectedIDs);

			// The object may become empty from a removal or toggle, in which case don't add it.
			if (!NewSelection.IsEmpty())
			{
				NewSelections.Add(MoveTemp(NewSelection));
			}
		}
		else
		{
			// The only way we can get here is if didn't have an existing selection and were trying
			// to remove selection, in which case we do nothing.
			ensure(!PreDragSelection && ShouldRemoveFromSelection());
		}
	}

	SelectionAPI->SetSelections(NewSelections, false, false);
	OnDragSelectionChanged.Broadcast();
}

void UUVEditorMeshSelectionMechanic::OnDragRectangleFinished(const FCameraRectangle& CurrentRectangle, bool bCancelled)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MeshSelectionMechanic_OnDragRectangleFinished); // Mark end of drag sequence

	// TODO(Performance) :DynamicMarqueeSelection Remove this call when marquee selection is fast enough to update
	//  dynamically for large meshes
	OnDragRectangleChanged(CurrentRectangle);

	if (!bCancelled)
	{
		SelectionAPI->EndChangeAndEmitIfModified(true);
	}
}

void UUVEditorMeshSelectionMechanic::OnUpdateModifierState(int ModifierID, bool bIsOn)
{
	switch (ModifierID)
	{
	case ShiftModifierID:
		bShiftToggle = bIsOn;
		break;
	case CtrlModifierID:
		bCtrlToggle = bIsOn;
		break;
	default:
		break;
	}
}

FInputRayHit UUVEditorMeshSelectionMechanic::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	FInputRayHit Hit;
	if (!bIsEnabled || !bShowHoveredElements || SelectionMode == ESelectionMode::None)
	{
		Hit.bHit = false;
		return Hit;
	}

	ESelectionMode Mode = SelectionMode;
	if (Mode != ESelectionMode::Vertex && Mode != ESelectionMode::Edge)
	{
		Mode = ESelectionMode::Triangle;
	}

	// We don't bother with the depth since everything is in the same plane.
	int32 Tid = IndexConstants::InvalidID;
	int32 AssetID = IndexConstants::InvalidID;
	Hit.bHit = GetHitTid(PressPos, Tid, AssetID);

	return Hit;
}

void UUVEditorMeshSelectionMechanic::OnBeginHover(const FInputDeviceRay& DevicePos)
{
}

bool UUVEditorMeshSelectionMechanic::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	using namespace UVEditorMeshSelectionMechanicLocals;

	ESelectionMode Mode = SelectionMode;
	if (SelectionMode != ESelectionMode::Vertex && SelectionMode != ESelectionMode::Edge)
	{
		Mode = ESelectionMode::Triangle;
	}

	HoverPointSet->Clear();
	HoverLineSet->Clear();
	HoverTriangleSet->Clear();

	int32 Tid = IndexConstants::InvalidID;
	int32 AssetID = IndexConstants::InvalidID;
	if (!GetHitTid(DevicePos, Tid, AssetID))
	{
		return false;
	}

	FDynamicMesh3* Mesh = Targets[AssetID]->UnwrapCanonical.Get();

	TArray<int32> ConvertedIDs;
	if (SelectionMode == ESelectionMode::Vertex || SelectionMode == ESelectionMode::Edge)
	{
		ConvertToHitElementList(SelectionMode, *Mesh,
			Tid, CameraState, DevicePos.WorldRay, ConvertedIDs);
		if (ConvertedIDs.IsEmpty())
		{
			// We were too far from a vert or edge, probably.
			return false;
		}
	}

	if (SelectionMode == ESelectionMode::Vertex)
	{
		const FVector3d& P = Mesh->GetVertexRef(ConvertedIDs[0]);
		const FRenderablePoint PointToRender(P,
			FUVEditorUXSettings::SelectionHoverTriangleWireframeColor,
			FUVEditorUXSettings::SelectionPointThickness);

		HoverPointSet->AddPoint(PointToRender);
	}
	else if (SelectionMode == ESelectionMode::Edge)
	{
		const FIndex2i EdgeVids = Mesh->GetEdgeV(ConvertedIDs[0]);
		const FVector& A = Mesh->GetVertexRef(EdgeVids.A);
		const FVector& B = Mesh->GetVertexRef(EdgeVids.B);

		HoverLineSet->AddLine(A, B,
			FUVEditorUXSettings::SelectionHoverTriangleWireframeColor,
			FUVEditorUXSettings::SelectionLineThickness,
			FUVEditorUXSettings::SelectionHoverWireframeDepthBias);
	}
	else
	{
		const FIndex3i Vids = Mesh->GetTriangle(Tid);
		const FVector& A = Mesh->GetVertex(Vids[0]);
		const FVector& B = Mesh->GetVertex(Vids[1]);
		const FVector& C = Mesh->GetVertex(Vids[2]);

		HoverLineSet->AddLine(A, B, FUVEditorUXSettings::SelectionHoverTriangleWireframeColor,
			FUVEditorUXSettings::SelectionLineThickness, FUVEditorUXSettings::SelectionHoverWireframeDepthBias);
		HoverLineSet->AddLine(B, C, FUVEditorUXSettings::SelectionHoverTriangleWireframeColor,
			FUVEditorUXSettings::SelectionLineThickness, FUVEditorUXSettings::SelectionHoverWireframeDepthBias);
		HoverLineSet->AddLine(C, A, FUVEditorUXSettings::SelectionHoverTriangleWireframeColor,
			FUVEditorUXSettings::SelectionLineThickness, FUVEditorUXSettings::SelectionHoverWireframeDepthBias);
		HoverTriangleSet->AddTriangle(A, B, C, FVector::ZAxisVector,
			FUVEditorUXSettings::SelectionHoverTriangleFillColor, HoverTriangleSetMaterial);
	}

	return true;
}

void UUVEditorMeshSelectionMechanic::OnEndHover()
{
	HoverPointSet->Clear();
	HoverLineSet->Clear();
	HoverTriangleSet->Clear();
}

#undef LOCTEXT_NAMESPACE

