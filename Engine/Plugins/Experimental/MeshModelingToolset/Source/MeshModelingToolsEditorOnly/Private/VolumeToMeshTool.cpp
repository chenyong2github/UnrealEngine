// Copyright Epic Games, Inc. All Rights Reserved.

#include "VolumeToMeshTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh3.h"
#include "MeshNormals.h"
#include "DynamicMeshEditor.h"
#include "Util/ColorConstants.h"
#include "ToolSetupUtil.h"
#include "Selection/ToolSelectionUtil.h"
#include "AssetGenerationUtil.h"

#include "Operations/MergeCoincidentMeshEdges.h"
#include "CompGeom/PolygonTriangulation.h"
#include "MeshBoundaryLoops.h"
#include "Operations/MinimalHoleFiller.h"

#include "Model.h"

#define LOCTEXT_NAMESPACE "UVolumeToMeshTool"


/*
 * ToolBuilder
 */

bool UVolumeToMeshToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountSelectedActorsOfType<AVolume>(SceneState) == 1;
}

UInteractiveTool* UVolumeToMeshToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UVolumeToMeshTool* NewTool = NewObject<UVolumeToMeshTool>(SceneState.ToolManager);

	NewTool->SetWorld(SceneState.World);
	check(AssetAPI);
	NewTool->SetAssetAPI(AssetAPI);

	AVolume* Volume = ToolBuilderUtil::FindFirstActorOfType<AVolume>(SceneState);
	check(Volume != nullptr);
	NewTool->SetSelection(Volume);

	return NewTool;
}




/*
 * Tool
 */
UVolumeToMeshTool::UVolumeToMeshTool()
{
	SetToolDisplayName(LOCTEXT("VolumeToMeshToolName", "Volume to Mesh"));
}


void UVolumeToMeshTool::SetSelection(AVolume* Volume)
{
	TargetVolume = Volume;
}


void UVolumeToMeshTool::Setup()
{
	UInteractiveTool::Setup();

	PreviewMesh = NewObject<UPreviewMesh>(this);
	PreviewMesh->bBuildSpatialDataStructure = false;
	PreviewMesh->CreateInWorld(TargetVolume->GetWorld(), FTransform::Identity);
	PreviewMesh->SetTransform(TargetVolume->GetActorTransform());

	PreviewMesh->SetMaterial( ToolSetupUtil::GetDefaultSculptMaterial(GetToolManager()) );

	PreviewMesh->SetOverrideRenderMaterial(ToolSetupUtil::GetSelectionMaterial(GetToolManager()));
	PreviewMesh->SetTriangleColorFunction([this](const FDynamicMesh3* Mesh, int TriangleID)
	{
		return LinearColors::SelectFColor(Mesh->GetTriangleGroup(TriangleID));
	});

	VolumeEdgesSet = NewObject<ULineSetComponent>(PreviewMesh->GetRootComponent());
	VolumeEdgesSet->SetupAttachment(PreviewMesh->GetRootComponent());
	VolumeEdgesSet->SetLineMaterial(ToolSetupUtil::GetDefaultLineComponentMaterial(GetToolManager()));
	VolumeEdgesSet->RegisterComponent();


	Settings = NewObject<UVolumeToMeshToolProperties>(this);
	Settings->RestoreProperties(this);
	AddToolPropertySource(Settings);

	Settings->WatchProperty(Settings->bWeldEdges, [this](bool) { bResultValid = false; });
	Settings->WatchProperty(Settings->bAutoRepair, [this](bool) { bResultValid = false; });
	Settings->WatchProperty(Settings->bOptimizeMesh, [this](bool) { bResultValid = false; });
	Settings->WatchProperty(Settings->bShowWireframe, [this](bool) { bResultValid = false; });

	bResultValid = false;

	GetToolManager()->DisplayMessage( 
		LOCTEXT("OnStartTool", "Convert a Volume to a Static Mesh"),
		EToolMessageLevel::UserNotification);
}



void UVolumeToMeshTool::Shutdown(EToolShutdownType ShutdownType)
{
	Settings->SaveProperties(this);

	FTransform3d Transform(PreviewMesh->GetTransform());
	PreviewMesh->SetVisible(false);
	PreviewMesh->Disconnect();
	PreviewMesh = nullptr;

	if (ShutdownType == EToolShutdownType::Accept )
	{
		UMaterialInterface* UseMaterial = UMaterial::GetDefaultMaterial(MD_Surface);

		FString NewName = TargetVolume.IsValid() ?
			FString::Printf(TEXT("%sMesh"), *TargetVolume->GetName()) : TEXT("Volume Mesh");

		GetToolManager()->BeginUndoTransaction(LOCTEXT("CreateMeshVolume", "Volume To Mesh"));

		AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
			AssetAPI, TargetWorld,
			&CurrentMesh, Transform, NewName, UseMaterial);
		if (NewActor != nullptr)
		{
			ToolSelectionUtil::SetNewActorSelection(GetToolManager(), NewActor);
		}

		GetToolManager()->EndUndoTransaction();
	}


}


void UVolumeToMeshTool::OnTick(float DeltaTime)
{
	if (bResultValid == false)
	{
		RecalculateMesh();
	}
}

void UVolumeToMeshTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}

bool UVolumeToMeshTool::CanAccept() const
{
	return bResultValid && CurrentMesh.TriangleCount() > 0;
}


void UVolumeToMeshTool::UpdateLineSet()
{
	VolumeEdgesSet->Clear();

	FColor BoundaryEdgeColor = LinearColors::VideoRed3b();
	float BoundaryEdgeThickness = 1.0;
	float BoundaryEdgeDepthBias = 2.0f;

	FColor WireEdgeColor = LinearColors::Gray3b();
	float WireEdgeThickness = 0.1;
	float WireEdgeDepthBias = 1.0f;

	if (Settings->bShowWireframe)
	{
		VolumeEdgesSet->ReserveLines(CurrentMesh.EdgeCount());

		for (int32 eid : CurrentMesh.EdgeIndicesItr())
		{
			FVector3d A, B;
			CurrentMesh.GetEdgeV(eid, A, B);
			if (CurrentMesh.IsBoundaryEdge(eid))
			{
				VolumeEdgesSet->AddLine((FVector)A, (FVector)B,
					BoundaryEdgeColor, BoundaryEdgeThickness, BoundaryEdgeDepthBias);
			}
			else
			{
				VolumeEdgesSet->AddLine((FVector)A, (FVector)B,
					WireEdgeColor, WireEdgeThickness, WireEdgeDepthBias);
			}
		}
	}
}





/** @return triangle aspect ratio transformed to be in [0,1] range */
double UnitAspectRatio(const FVector3d& A, const FVector3d& B, const FVector3d& C)
{
	double AspectRatio = VectorUtil::AspectRatio(A, B, C);
	return (AspectRatio > 1.0) ? FMathd::Clamp(1.0 / AspectRatio, 0.0, 1.0) : AspectRatio;
}
/** @return triangle aspect ratio transformed to be in [0,1] range */
double UnitAspectRatio(const FDynamicMesh3& Mesh, int32 TriangleID)
{
	FVector3d A, B, C;
	Mesh.GetTriVertices(TriangleID, A, B, C);
	return UnitAspectRatio(A, B, C);
}



/**
 * If both triangles on an edge are coplanar, we can arbitrarily flip the interior edge to
 * improve triangle quality. Similarly if one triangle on an edge is degenerate, we can flip
 * the edge without affecting the shape to try to remove it. This code does a single pass of
 * such an optimization.
 * Note: could be more efficient to do multiple passes internally, would save on the initial computation
 */
void PlanarFlipsOptimization(FDynamicMesh3& Mesh, double PlanarDotThresh = 0.99)
{
	struct FFlatEdge
	{
		int32 eid;
		double MinAspect;
	};

	TArray<double> AspectRatios;
	TArray<FVector3d> Normals;
	AspectRatios.SetNum(Mesh.MaxTriangleID());
	Normals.SetNum(Mesh.MaxTriangleID());
	for (int32 tid : Mesh.TriangleIndicesItr())
	{
		FVector3d A, B, C;
		Mesh.GetTriVertices(tid, A, B, C);
		AspectRatios[tid] = UnitAspectRatio(A, B, C);
		Normals[tid] = VectorUtil::Normal(A, B, C);
	}

	TArray<FFlatEdge> Flips;
	for (int32 eid : Mesh.EdgeIndicesItr())
	{
		if (Mesh.IsBoundaryEdge(eid) == false)
		{
			FIndex2i EdgeT = Mesh.GetEdgeT(eid);
			if (AspectRatios[EdgeT.A] < 0.01 && AspectRatios[EdgeT.B] < 0.01)
			{
				continue;		// if both are degenerate we can't fix by flipping edge between them
			}
			double MinAspect = FMathd::Min(AspectRatios[EdgeT.A], AspectRatios[EdgeT.B]);
			double NormDot = Normals[EdgeT.A].Dot(Normals[EdgeT.B]);
			if (NormDot > PlanarDotThresh)
			{
				Flips.Add({ eid, MinAspect });
			}
		}
	}

	Flips.Sort([&](const FFlatEdge& A, const FFlatEdge& B) { return A.MinAspect < B.MinAspect; });

	for (int32 k = 0; k < Flips.Num(); ++k)
	{
		int32 eid = Flips[k].eid;
		FIndex2i EdgeV = Mesh.GetEdgeV(eid);
		int32 a = EdgeV.A, b = EdgeV.B;
		FIndex2i EdgeT = Mesh.GetEdgeT(eid);
		FIndex3i Tri0 = Mesh.GetTriangle(EdgeT.A), Tri1 = Mesh.GetTriangle(EdgeT.B);
		int32 c = IndexUtil::OrientTriEdgeAndFindOtherVtx(a, b, Tri0);
		int32 d = IndexUtil::FindTriOtherVtx(a, b, Tri1);

		double AspectA = AspectRatios[EdgeT.A], AspectB = AspectRatios[EdgeT.B];
		double Metric = FMathd::Min(AspectA, AspectB);
		FVector3d Normal = (AspectA > AspectB) ? Normals[EdgeT.A] : Normals[EdgeT.B];

		FVector3d A = Mesh.GetVertex(a), B = Mesh.GetVertex(b);
		FVector3d C = Mesh.GetVertex(c), D = Mesh.GetVertex(d);

		double FlipAspect1 = UnitAspectRatio(C, D, B);
		double FlipAspect2 = UnitAspectRatio(D, C, A);
		FVector3d FlipNormal1 = VectorUtil::Normal(C, D, B);
		FVector3d FlipNormal2 = VectorUtil::Normal(D, C, A);
		if (FlipNormal1.Dot(Normal) < PlanarDotThresh || FlipNormal2.Dot(Normal) < PlanarDotThresh)
		{
			continue;		// should not happen?
		}

		if (FMathd::Min(FlipAspect1, FlipAspect2) > Metric)
		{
			FDynamicMesh3::FEdgeFlipInfo FlipInfo;
			if (Mesh.FlipEdge(eid, FlipInfo) == EMeshResult::Ok)
			{
				AspectRatios[EdgeT.A] = UnitAspectRatio(Mesh, EdgeT.A);
				AspectRatios[EdgeT.B] = UnitAspectRatio(Mesh, EdgeT.B);

				// safety check - if somehow we flipped the normal, flip it back
				bool bInvertedNormal = (Mesh.GetTriNormal(EdgeT.A).Dot(Normal) < PlanarDotThresh) ||
					(Mesh.GetTriNormal(EdgeT.B).Dot(Normal) < PlanarDotThresh);
				if (bInvertedNormal)
				{
					UE_LOG(LogTemp, Warning, TEXT("UE::Water::PlanarFlipsOptimization - Invalid Flip!"));
					Mesh.FlipEdge(eid, FlipInfo);
					AspectRatios[EdgeT.A] = UnitAspectRatio(Mesh, EdgeT.A);
					AspectRatios[EdgeT.B] = UnitAspectRatio(Mesh, EdgeT.B);
				}
			}
		}
	}
}



struct FVolumeToMeshOptions
{
	bool bInWorldSpace = false;
	bool bSetGroups = true;
	bool bMergeVertices = true;
	bool bAutoRepairMesh = true;
	bool bOptimizeMesh = true;
};


/**
 * Extracts a FDynamicMesh3 from an AVolume
 * The output mesh is in World Space.
 */
void ExtractMesh(AVolume* Volume, FDynamicMesh3& Mesh, const FVolumeToMeshOptions& Options)
{
	Mesh.DiscardAttributes();
	if (Options.bSetGroups)
	{
		Mesh.EnableTriangleGroups();
	}

	UModel* Model = Volume->Brush;
	FTransform3d XForm = (Options.bInWorldSpace) ? FTransform3d(Volume->GetTransform()) : FTransform3d::Identity();

	// Each "BspNode" is a planar polygon, triangulate each polygon and accumulate in a mesh.
	// Note that this does not make any attempt to weld vertices/edges
	for (const FBspNode& Node : Model->Nodes)
	{
		FVector3d Normal = (FVector3d)Node.Plane;
		FFrame3d Plane(Node.Plane.W * Normal, Normal);

		int32 NumVerts = (Node.NodeFlags & PF_TwoSided) ? Node.NumVertices / 2 : Node.NumVertices;  // ??
		if (NumVerts > 0)
		{
			TArray<int32> VertIndices;
			TArray<FVector2d> VertPositions2d;
			VertIndices.SetNum(NumVerts);
			VertPositions2d.SetNum(NumVerts);
			for (int32 VertexIndex = 0; VertexIndex < NumVerts; ++VertexIndex)
			{
				const FVert& Vert = Model->Verts[Node.iVertPool + VertexIndex];
				FVector3d Point = (FVector3d)Model->Points[Vert.pVertex];
				Point = XForm.TransformPosition(Point);
				VertIndices[VertexIndex] = Mesh.AppendVertex(Point);
				VertPositions2d[VertexIndex] = Plane.ToPlaneUV(Point, 2);
			}

			TArray<FIndex3i> PolyTriangles;
			PolygonTriangulation::TriangulateSimplePolygon(VertPositions2d, PolyTriangles);

			int32 GroupID = FDynamicMesh3::InvalidID;
			if (Options.bSetGroups)
			{
				GroupID = Mesh.AllocateTriangleGroup();
			}

			for (FIndex3i Tri : PolyTriangles)
			{
				// flip orientation here...
				Mesh.AppendTriangle(VertIndices[Tri.A], VertIndices[Tri.C], VertIndices[Tri.B], GroupID);
			}
		}
	}

	if (Options.bMergeVertices)
	{
		// Merge the mesh edges to create a closed solid
		double MinLen, MaxLen, AvgLen;
		TMeshQueries<FDynamicMesh3>::EdgeLengthStats(Mesh, MinLen, MaxLen, AvgLen);
		FMergeCoincidentMeshEdges Merge(&Mesh);
		Merge.MergeVertexTolerance = FMathd::Max(Merge.MergeVertexTolerance, MinLen * 0.1);
		Merge.Apply();

		// If the mesh is not closed, the merge failed or the volume had cracks/holes. 
		// Do trivial hole fills to ensure the output is solid   (really want autorepair here)
		if (Mesh.IsClosed() == false && Options.bAutoRepairMesh)
		{
			FMeshBoundaryLoops BoundaryLoops(&Mesh, true);
			for (FEdgeLoop& Loop : BoundaryLoops.Loops)
			{
				FMinimalHoleFiller Filler(&Mesh, Loop);
				Filler.Fill();
			}
		}


		// try to flip towards better triangles in planar areas, should reduce/remove degenerate geo
		if (Options.bOptimizeMesh)
		{
			for (int32 k = 0; k < 5; ++k)
			{
				PlanarFlipsOptimization(Mesh);
			}
		}
	}
}



void UVolumeToMeshTool::RecalculateMesh()
{
	if (TargetVolume.IsValid())
	{
		FVolumeToMeshOptions Options;
		Options.bMergeVertices = Settings->bWeldEdges;
		Options.bAutoRepairMesh = Settings->bAutoRepair;
		Options.bOptimizeMesh = Settings->bOptimizeMesh;

		CurrentMesh = FDynamicMesh3(EMeshComponents::FaceGroups);
		ExtractMesh(TargetVolume.Get(), CurrentMesh, Options);
		FMeshNormals::InitializeMeshToPerTriangleNormals(&CurrentMesh);
		PreviewMesh->UpdatePreview(&CurrentMesh);
	}

	UpdateLineSet();

	bResultValid = true;
}




#undef LOCTEXT_NAMESPACE
