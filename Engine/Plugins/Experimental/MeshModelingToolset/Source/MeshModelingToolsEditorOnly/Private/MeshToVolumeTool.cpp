// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshToVolumeTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh3.h"
#include "MeshNormals.h"
#include "DynamicMeshEditor.h"
#include "MeshRegionBoundaryLoops.h"
#include "Util/ColorConstants.h"
#include "ToolSetupUtil.h"
#include "Selections/MeshConnectedComponents.h"


#include "Engine/Classes/Engine/BlockingVolume.h"
#include "Engine/Classes/Components/BrushComponent.h"
#include "Engine/Classes/Engine/Polys.h"
#include "Model.h"
#include "BSPOps.h"		// in UnrealEd

#define LOCTEXT_NAMESPACE "UMeshToVolumeTool"


/*
 * ToolBuilder
 */


bool UMeshToVolumeToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}

UInteractiveTool* UMeshToVolumeToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UMeshToVolumeTool* NewTool = NewObject<UMeshToVolumeTool>(SceneState.ToolManager);

	UActorComponent* ActorComponent = ToolBuilderUtil::FindFirstComponent(SceneState, CanMakeComponentTarget);
	UPrimitiveComponent* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
	check(MeshComponent != nullptr);
	NewTool->SetSelection(MakeComponentTarget(MeshComponent));

	return NewTool;
}

/*
 * Tool
 */
UMeshToVolumeTool::UMeshToVolumeTool()
{
	SetToolDisplayName(LOCTEXT("MeshToVolumeToolName", "Mesh To Volume"));
}


void UMeshToVolumeTool::Setup()
{
	UInteractiveTool::Setup();

	PreviewMesh = NewObject<UPreviewMesh>(this);
	PreviewMesh->bBuildSpatialDataStructure = false;
	PreviewMesh->CreateInWorld(ComponentTarget->GetOwnerActor()->GetWorld(), FTransform::Identity);
	PreviewMesh->SetTransform(ComponentTarget->GetWorldTransform());

	FComponentMaterialSet MaterialSet;
	ComponentTarget->GetMaterialSet(MaterialSet);
	PreviewMesh->SetMaterials(MaterialSet.Materials);

	PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::ExternallyCalculated);
	PreviewMesh->InitializeMesh(ComponentTarget->GetMesh());

	InputMesh.Copy(*PreviewMesh->GetMesh());

	VolumeEdgesSet = NewObject<ULineSetComponent>(PreviewMesh->GetRootComponent());
	VolumeEdgesSet->SetupAttachment(PreviewMesh->GetRootComponent());
	VolumeEdgesSet->SetLineMaterial(ToolSetupUtil::GetDefaultLineComponentMaterial(GetToolManager()));
	VolumeEdgesSet->RegisterComponent();

	// hide input StaticMeshComponent
	ComponentTarget->SetOwnerVisibility(false);

	Settings = NewObject<UMeshToVolumeToolProperties>(this);
	Settings->RestoreProperties(this);
	AddToolPropertySource(Settings);

	Settings->WatchProperty(Settings->ConversionMode,
							[this](EMeshToVolumeMode NewMode)
							{ bVolumeValid = false; });


	HandleSourcesProperties = NewObject<UOnAcceptHandleSourcesProperties>(this);
	HandleSourcesProperties->RestoreProperties(this);
	AddToolPropertySource(HandleSourcesProperties);

	bVolumeValid = false;
	

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool", "Convert a Static Mesh to a Volume, or update an existing Volume"),
		EToolMessageLevel::UserNotification);
}





void UMeshToVolumeTool::BakeToVolume(AVolume* TargetVolume)
{
	check(TargetVolume->Brush);

	UModel* Model = TargetVolume->Brush;

	Model->Modify();

	Model->Initialize(TargetVolume);
	UPolys* Polys = Model->Polys;

	for (FModelFace& Face : Faces)
	{
		int32 NumVertices = Face.BoundaryLoop.Num();
		FVector Normal = (FVector)Face.Plane.Z();
		FVector U = (FVector)Face.Plane.X();
		FVector V = (FVector)Face.Plane.Y();
		FPlane FacePlane = Face.Plane.ToFPlane();

		// create FPoly. This is Editor-only and I'm not entirely sure we need it?
		int32 PolyIndex = Polys->Element.Num();
		FPoly NewPoly;
		NewPoly.Base = (FVector)Face.BoundaryLoop[0];
		NewPoly.Normal = Normal;
		NewPoly.TextureU = U;
		NewPoly.TextureV = V;
		NewPoly.Vertices.SetNum(NumVertices);
		for (int32 k = 0; k < NumVertices; ++k)
		{
			NewPoly.Vertices[k] = (FVector)Face.BoundaryLoop[k];
		}
		NewPoly.PolyFlags = 0;
		NewPoly.iLink = NewPoly.iLinkSurf = NewPoly.iBrushPoly = -1;
		NewPoly.SmoothingMask = 0;
		Polys->Element.Add(NewPoly);

/*

		// create points for this face in UModel::Points
		// TODO: can we share points between faces?
		TArray<int32> PointIndices;
		PointIndices.SetNum(NumVertices);
		for (int32 k = 0; k < NumVertices; ++k)
		{
			int32 NewIdx = Model->Points.Num();
			Model->Points.Add((FVector)Face.BoundaryLoop[k]);
			PointIndices[k] = NewIdx;
		}
		int32 BasePointIndex = PointIndices[0];

		// create normal for this face in UModel::Vectors along with U and V direction vectors
		int32 NormalIdx = Model->Vectors.Num();
		Model->Vectors.Add(Normal);
		int32 TextureUIdx = Model->Vectors.Num();
		Model->Vectors.Add(U);
		int32 TextureVIdx = Model->Vectors.Num();
		Model->Vectors.Add(V);

		// create FVerts for this face in UModel::Verts
		int32 iVertPoolStart = Model->Verts.Num();
		for (int32 k = 0; k < NumVertices; ++k)
		{
			FVert NewVert;
			NewVert.pVertex = PointIndices[k];		// Index of vertex point.
			NewVert.iSide = INDEX_NONE;				// If shared, index of unique side. Otherwise INDEX_NONE.
			NewVert.ShadowTexCoord = FVector2D::ZeroVector;			// The vertex's shadow map coordinate.
			NewVert.BackfaceShadowTexCoord = FVector2D::ZeroVector;	// The vertex's shadow map coordinate for the backface of the node.
			Model->Verts.Add(NewVert);
		}

		// create Surf

		int32 SurfIndex = Model->Surfs.Num();
		FBspSurf NewSurf;
		NewSurf.Material = nullptr;			// 4 Material.
		NewSurf.PolyFlags = 0;				// 4 Polygon flags.
		NewSurf.pBase = BasePointIndex;		// 4 Polygon & texture base point index (where U,V==0,0).
		NewSurf.vNormal = NormalIdx;		// 4 Index to polygon normal.
		NewSurf.vTextureU = TextureUIdx;	// 4 Texture U-vector index.
		NewSurf.vTextureV = TextureVIdx;	// 4 Texture V-vector index.
		//NewSurf.iBrushPoly = PolyIndex;		// 4 Editor brush polygon index.
		NewSurf.iBrushPoly = -1;
		//NewSurf.Actor = NewVolume;			// 4 Brush actor owning this Bsp surface.
		NewSurf.Actor = nullptr;
		NewSurf.Plane = FacePlane;			// 16 The plane this surface lies on.
		Model->Surfs.Add(NewSurf);


		// create nodes for this face in UModel::Nodes

		FBspNode NewNode;
		NewNode.Plane = FacePlane;					// 16 Plane the node falls into (X, Y, Z, W).
		NewNode.iVertPool = iVertPoolStart;			// 4  Index of first vertex in vertex pool, =iTerrain if NumVertices==0 and NF_TerrainFront.
		NewNode.iSurf = SurfIndex;					// 4  Index to surface information.
		NewNode.iVertexIndex = INDEX_NONE;			// The index of the node's first vertex in the UModel's vertex buffer.
													// This is initialized by UModel::UpdateVertices()
		NewNode.NumVertices = NumVertices;			// 1  Number of vertices in node.
		NewNode.NodeFlags = 0;						// 1  Node flags.
		Model->Nodes.Add(NewNode);
*/
	}

	// requires ed
	FBSPOps::csgPrepMovingBrush(TargetVolume);

	// do we need to do this?
	TargetVolume->MarkPackageDirty();
}







void UMeshToVolumeTool::Shutdown(EToolShutdownType ShutdownType)
{
	Settings->SaveProperties(this);
	HandleSourcesProperties->SaveProperties(this);

	PreviewMesh->SetVisible(false);
	PreviewMesh->Disconnect();
	PreviewMesh = nullptr;

	ComponentTarget->SetOwnerVisibility(true);


	UWorld* TargetWorld = ComponentTarget->GetOwnerActor()->GetWorld();


	if (ShutdownType == EToolShutdownType::Accept)
	{
		GetToolManager()->BeginUndoTransaction(LOCTEXT("MeshToVolumeToolTransactionName", "Create Volume"));

		FTransform TargetTransform = ComponentTarget->GetWorldTransform();

		AVolume* TargetVolume = nullptr;

		FTransform SetTransform = TargetTransform;
		if (Settings->TargetVolume.IsValid() == false)
		{
			FRotator Rotation(0.0f, 0.0f, 0.0f);
			FActorSpawnParameters SpawnInfo;
			FTransform NewActorTransform = FTransform::Identity;
			UClass* VolumeClass = Settings->NewVolumeType.Get();
			if (VolumeClass)
			{
				TargetVolume = (AVolume*)TargetWorld->SpawnActor(VolumeClass, &NewActorTransform, SpawnInfo);
			}
			else
			{
				TargetVolume = TargetWorld->SpawnActor<ABlockingVolume>(FVector::ZeroVector, Rotation, SpawnInfo);
			}
			TargetVolume->BrushType = EBrushType::Brush_Add;
			UModel* Model = NewObject<UModel>(TargetVolume);
			TargetVolume->Brush = Model;
			TargetVolume->GetBrushComponent()->Brush = TargetVolume->Brush;
		}
		else
		{
			TargetVolume = Settings->TargetVolume.Get();
			SetTransform = TargetVolume->GetActorTransform();
			TargetVolume->Modify();
			TargetVolume->GetBrushComponent()->Modify();
		}

		BakeToVolume(TargetVolume);
		TargetVolume->SetActorTransform(SetTransform);
		TargetVolume->PostEditChange();


		TArray<AActor*> Actors;
		Actors.Add(ComponentTarget->GetOwnerActor());
		HandleSourcesProperties->ApplyMethod(Actors, GetToolManager());


		GetToolManager()->EndUndoTransaction();

	}
}


void UMeshToVolumeTool::OnTick(float DeltaTime)
{
	if (bVolumeValid == false)
	{
		RecalculateVolume();
	}
}

void UMeshToVolumeTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}


void UMeshToVolumeTool::UpdateLineSet()
{
	FColor BoundaryEdgeColor(240, 15, 15);
	float BoundaryEdgeThickness = 0.5;
	float BoundaryEdgeDepthBias = 2.0f;

	VolumeEdgesSet->Clear();
	for (const FModelFace& Face : Faces)
	{
		int32 NumV = Face.BoundaryLoop.Num();
		for (int32 k = 0; k < NumV; ++k)
		{
			VolumeEdgesSet->AddLine(
				(FVector)Face.BoundaryLoop[k], (FVector)Face.BoundaryLoop[(k+1)%NumV],
				BoundaryEdgeColor, BoundaryEdgeThickness, BoundaryEdgeDepthBias);
		}
	}

}



void UMeshToVolumeTool::RecalculateVolume()
{
	if (Settings->ConversionMode == EMeshToVolumeMode::MinimalPolygons)
	{
		RecalculateVolume_Polygons();
	}
	else
	{
		RecalculateVolume_Triangles();
	}
}


void UMeshToVolumeTool::RecalculateVolume_Polygons()
{
	Faces.SetNum(0);

	FMeshNormals Normals(&InputMesh);
	Normals.ComputeTriangleNormals();

	double PlanarTolerance = FMathf::ZeroTolerance;

	FMeshConnectedComponents Components(&InputMesh);
	Components.FindConnectedTriangles([&](int32 Triangle0, int32 Triangle1)
	{
		FVector3d Origin = InputMesh.GetTriCentroid(Triangle0);
		FVector3d Normal = Normals[Triangle0];

		FVector3d A, B, C;
		InputMesh.GetTriVertices(Triangle1, A, B, C);;
		double DistA = (A - Origin).Dot(Normal);
		double DistB = (B - Origin).Dot(Normal);
		double DistC = (C - Origin).Dot(Normal);
		double MaxDist = FMathd::Max3(FMathd::Abs(DistA), FMathd::Abs(DistB), FMathd::Abs(DistC));

		return MaxDist < PlanarTolerance;
	});

	for (const FMeshConnectedComponents::FComponent& Component : Components)
	{
		FVector3d FaceNormal = Normals[Component.Indices[0]];

		FMeshRegionBoundaryLoops Loops(&InputMesh, Component.Indices);
		for (const FEdgeLoop& Loop : Loops.Loops)
		{
			FModelFace Face;

			FVector3d AvgPos(0, 0, 0);
			for (int32 vid : Loop.Vertices)
			{
				FVector3d Position = InputMesh.GetVertex(vid);
				Face.BoundaryLoop.Add(Position);
				AvgPos += Position;
			}
			AvgPos /= (double)Loop.Vertices.Num();
			Algo::Reverse(Face.BoundaryLoop);

			Face.Plane = FFrame3d(AvgPos, FaceNormal);

			Faces.Add(Face);
		}
	}

	UpdateLineSet();

	bVolumeValid = true;
}






void UMeshToVolumeTool::RecalculateVolume_Triangles()
{
	Faces.SetNum(0);

	for (int32 tid : InputMesh.TriangleIndicesItr())
	{
		FVector3d A, B, C;
		InputMesh.GetTriVertices(tid, A, B, C);
		FVector3d Centroid, Normal; double Area;
		InputMesh.GetTriInfo(tid, Normal, Area, Centroid);

		FModelFace Face;
		Face.Plane = FFrame3d(Centroid, Normal);
		Face.BoundaryLoop.Add(A);
		Face.BoundaryLoop.Add(C);
		Face.BoundaryLoop.Add(B);

		Faces.Add(Face);
	}

	UpdateLineSet();

	bVolumeValid = true;
}





#undef LOCTEXT_NAMESPACE
