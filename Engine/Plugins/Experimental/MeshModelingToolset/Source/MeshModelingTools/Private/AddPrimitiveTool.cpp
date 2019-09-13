// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AddPrimitiveTool.h"
#include "ToolBuilderUtil.h"
#include "InteractiveToolManager.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "Selection/ToolSelectionUtil.h"
#include "AssetGenerationUtil.h"

#include "MeshDescriptionBuilder.h"
#include "Generators/SweepGenerator.h"
#include "Generators/GridBoxMeshGenerator.h"
#include "Generators/RectangleMeshGenerator.h"
#include "Generators/SphereGenerator.h"
#include "DynamicMeshAttributeSet.h"

#include "StaticMeshComponentBuilder.h"
#include "Drawing/MeshDebugDrawing.h"

#define LOCTEXT_NAMESPACE "UAddPrimitiveTool"


/*
 * ToolBuilder
 */


bool UAddPrimitiveToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return (this->AssetAPI != nullptr);
}

UInteractiveTool* UAddPrimitiveToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UAddPrimitiveTool* NewTool = NewObject<UAddPrimitiveTool>(SceneState.ToolManager);
	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);
	return NewTool;
}



/*
 * Tool
 */

UProceduralShapeToolProperties::UProceduralShapeToolProperties()
{
	Shape = EMakeMeshShapeType::Box;
	Width = 100;
	Height = 200;
	//StartAngle = 0;
	//EndAngle = 360;
	Slices = 16;
	Subdivisions = 0;
	bCentered = false;
	PlaceMode = EMakeMeshPlacementType::OnScene;
    Material = CreateDefaultSubobject<UMaterialInterface>(TEXT("MATERIAL"));
}


void UAddPrimitiveTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UAddPrimitiveTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}


void UAddPrimitiveTool::Setup()
{
	USingleClickTool::Setup();

	UMouseHoverBehavior* HoverBehavior = NewObject<UMouseHoverBehavior>(this);
	HoverBehavior->Initialize(this);
	AddInputBehavior(HoverBehavior);

	ShapeSettings = NewObject<UProceduralShapeToolProperties>(this);
	AddToolPropertySource(ShapeSettings);

	// create preview mesh object
	PreviewMesh = NewObject<UPreviewMesh>(this, TEXT("PreviewMesh"));
	PreviewMesh->CreateInWorld(TargetWorld, FTransform::Identity);
	PreviewMesh->SetVisible(false);
	PreviewMesh->SetMaterial(ShapeSettings->Material);

	UpdatePreviewMesh();
}


void UAddPrimitiveTool::Shutdown(EToolShutdownType ShutdownType)
{
	PreviewMesh->SetVisible(false);
	PreviewMesh->Disconnect();
	PreviewMesh = nullptr;
}


void UAddPrimitiveTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	//FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	//MeshDebugDraw::DrawSimpleGrid(ShapeFrame, 13, 5.0f, 1.0f, FColor::Orange, false, PDI, FTransform::Identity);
}



void UAddPrimitiveTool::OnPropertyModified(UObject* PropertySet, UProperty* Property)
{
	PreviewMesh->SetMaterial(ShapeSettings->Material);
	UpdatePreviewMesh();
}




void UAddPrimitiveTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	UpdatePreviewPosition(DevicePos);
}


void UAddPrimitiveTool::UpdatePreviewPosition(const FInputDeviceRay& DeviceClickPos)
{
	FRay ClickPosWorldRay = DeviceClickPos.WorldRay;

	// hit position (temp)
	bool bHit = false;

	if (ShapeSettings->PlaceMode == EMakeMeshPlacementType::OnPlane)
	{
		FPlane DrawPlane(FVector::ZeroVector, FVector(0, 0, 1));
		FVector DrawPlanePos = FMath::RayPlaneIntersection(ClickPosWorldRay.Origin, ClickPosWorldRay.Direction, DrawPlane);
		bHit = true;
		ShapeFrame = FFrame3f(DrawPlanePos);
	}
	else
	{
		// cast ray into scene
		FVector RayStart = ClickPosWorldRay.Origin;
		FVector RayEnd = ClickPosWorldRay.PointAt(999999);
		FCollisionObjectQueryParams QueryParams(FCollisionObjectQueryParams::AllObjects);
		FHitResult Result;
		bHit = TargetWorld->LineTraceSingleByObjectType(Result, RayStart, RayEnd, QueryParams);
		if (bHit)
		{
			ShapeFrame = FFrame3f(Result.ImpactPoint, Result.ImpactNormal);
			ShapeFrame.ConstrainedAlignPerpAxes();
		}
	}

	if (ShapeSettings->Rotation != 0)
	{
		ShapeFrame.Rotate(FQuaternionf(ShapeFrame.Z(), ShapeSettings->Rotation, true));
	}

	if (bHit)
	{
		PreviewMesh->SetVisible(true);
		PreviewMesh->SetTransform(ShapeFrame.ToFTransform());
	}
	else
	{
		PreviewMesh->SetVisible(false);
	}
}


void UAddPrimitiveTool::UpdatePreviewMesh()
{
	FDynamicMesh3 NewMesh;
	switch (ShapeSettings->Shape)
	{
	case EMakeMeshShapeType::Plane:
		GeneratePlane(&NewMesh);
		break;

	case EMakeMeshShapeType::Cylinder:
		GenerateCylinder(&NewMesh);
		break;

	case EMakeMeshShapeType::Cone:
		GenerateCone(&NewMesh);
		break;

	case EMakeMeshShapeType::Sphere:
		GenerateSphere(&NewMesh);
		break;

	case EMakeMeshShapeType::Box:
	default:
		GenerateBox(&NewMesh);
		break;
	}

	// set mesh position
	FAxisAlignedBox3d Bounds = NewMesh.GetCachedBounds();
	FVector3d TargetOrigin = Bounds.Center();
	if (ShapeSettings->bCentered == false)
	{
		TargetOrigin.Z = Bounds.Min.Z;
	}
	for (int vid : NewMesh.VertexIndicesItr())
	{
		FVector3d Pos = NewMesh.GetVertex(vid);
		Pos -= TargetOrigin;
		NewMesh.SetVertex(vid, Pos);
	}


	PreviewMesh->UpdatePreview(&NewMesh);
}




void UAddPrimitiveTool::OnClicked(const FInputDeviceRay& DeviceClickPos)
{
#if WITH_EDITOR
	const FDynamicMesh3* CurMesh = PreviewMesh->GetPreviewDynamicMesh();
	FTransform3d CurTransform(PreviewMesh->GetTransform());
	UMaterialInterface* Material = PreviewMesh->GetMaterial();
	GetToolManager()->BeginUndoTransaction(LOCTEXT("MergeMeshes", "Merge Meshes"));

	AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
		AssetAPI, TargetWorld,
		CurMesh, CurTransform, TEXT("ShapeMesh"),
		AssetGenerationUtil::GetDefaultAutoGeneratedAssetPath(),
		Material
		);
	
	// select newly-created object
	ToolSelectionUtil::SetNewActorSelection(GetToolManager(), NewActor);

	GetToolManager()->EndUndoTransaction();
#else
	checkNoEntry();
#endif
}



void UAddPrimitiveTool::GenerateBox(FDynamicMesh3* OutMesh)
{
	FGridBoxMeshGenerator BoxGen;
	BoxGen.Box = FOrientedBox3d(FVector3d::Zero(), 0.5*FVector3d(ShapeSettings->Width, ShapeSettings->Width, ShapeSettings->Height));
	int EdgeNum = ShapeSettings->Subdivisions + 2;
	BoxGen.EdgeVertices = FIndex3i(EdgeNum, EdgeNum, EdgeNum);
	BoxGen.Generate();
	OutMesh->Copy(&BoxGen);
}


void UAddPrimitiveTool::GeneratePlane(FDynamicMesh3* OutMesh)
{
	FRectangleMeshGenerator RectGen;
	RectGen.Width = ShapeSettings->Width;
	RectGen.Height = ShapeSettings->Height;
	RectGen.WidthVertexCount = RectGen.HeightVertexCount = ShapeSettings->Subdivisions + 2;
	RectGen.Generate();
	OutMesh->Copy(&RectGen);
}


void UAddPrimitiveTool::GenerateCylinder(FDynamicMesh3* OutMesh)
{
	FCylinderGenerator CylGen;
	CylGen.Radius[0] = ShapeSettings->Width * 0.5f;
	CylGen.Radius[1] = CylGen.Radius[0];
	CylGen.Height = ShapeSettings->Height;
	CylGen.AngleSamples = ShapeSettings->Slices;
	CylGen.bCapped = true;
	CylGen.Generate();
	OutMesh->Copy(&CylGen);
}


void UAddPrimitiveTool::GenerateCone(FDynamicMesh3* OutMesh)
{
	// Unreal's standard cone is just a cylinder with a very small top
	FCylinderGenerator CylGen;
	CylGen.Radius[0] = ShapeSettings->Width * 0.5f;
	CylGen.Radius[1] = .01;
	CylGen.Height = ShapeSettings->Height;
	CylGen.AngleSamples = ShapeSettings->Slices;
	CylGen.bCapped = true;
	CylGen.Generate();
	OutMesh->Copy(&CylGen);
}


void UAddPrimitiveTool::GenerateSphere(FDynamicMesh3* OutMesh)
{
	FSphereGenerator SphereGen;
	SphereGen.Radius = ShapeSettings->Width * 0.5f;
	SphereGen.NumTheta = ShapeSettings->Slices;
	SphereGen.NumPhi = ShapeSettings->Slices;
	SphereGen.Generate();
	OutMesh->Copy(&SphereGen);
}


#undef LOCTEXT_NAMESPACE
