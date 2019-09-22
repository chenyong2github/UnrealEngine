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

#include "DynamicMeshEditor.h"

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
	PivotLocation = EMakeMeshPivotLocation::Base;
	PlaceMode = EMakeMeshPlacementType::OnScene;
}

void UProceduralShapeToolProperties::SaveProperties(UInteractiveTool* SaveFromTool)
{
	UProceduralShapeToolProperties* PropertyCache = GetPropertyCache<UProceduralShapeToolProperties>();
	PropertyCache->Shape = this->Shape;
	PropertyCache->Width = this->Width;
	PropertyCache->Height = this->Height;
	PropertyCache->Slices = this->Slices;
	PropertyCache->Subdivisions = this->Subdivisions;
	PropertyCache->PivotLocation = this->PivotLocation;
	PropertyCache->PlaceMode = this->PlaceMode;
}

void UProceduralShapeToolProperties::RestoreProperties(UInteractiveTool* RestoreToTool)
{
	UProceduralShapeToolProperties* PropertyCache = GetPropertyCache<UProceduralShapeToolProperties>();
	this->Shape = PropertyCache->Shape;
	this->Width = PropertyCache->Width;
	this->Height = PropertyCache->Height;
	this->Slices = PropertyCache->Slices;
	this->Subdivisions = PropertyCache->Subdivisions;
	this->PivotLocation = PropertyCache->PivotLocation;
	this->PlaceMode = PropertyCache->PlaceMode;
}

namespace
{
	struct {
		FName Name;
		EMakeMeshShapeType EnabledShapes;
	} EnabledShapesMap[] =
	{ 
	  { TEXT("Shape"),         EMakeMeshShapeType::All },
	  { TEXT("Width"),         EMakeMeshShapeType::All },
	  { TEXT("Height"),        EMakeMeshShapeType::Box | EMakeMeshShapeType::Cylinder | EMakeMeshShapeType::Cone | EMakeMeshShapeType::Plane },
	  { TEXT("Rotation"),      EMakeMeshShapeType::All },
	  { TEXT("PlaceMode"),     EMakeMeshShapeType::All },
	  { TEXT("PivotLocation"), EMakeMeshShapeType::All },
	  { TEXT("Slices"),        EMakeMeshShapeType::Cylinder | EMakeMeshShapeType::Cone | EMakeMeshShapeType::Sphere },
	  { TEXT("Subdivisions"),  EMakeMeshShapeType::Box | EMakeMeshShapeType::Plane | EMakeMeshShapeType::Cylinder | EMakeMeshShapeType::Cone }
	};
};

// UObject interface
#if WITH_EDITOR
bool
UProceduralShapeToolProperties::CanEditChange( const UProperty* InProperty) const
{
	auto* Elem = Algo::FindByPredicate(EnabledShapesMap, [InProperty](const auto& Elem) { return Elem.Name == InProperty->GetFName(); });
	if (Elem != nullptr)
	{
		return (Shape & Elem->EnabledShapes) != EMakeMeshShapeType::None;
	}
	checkNoEntry();
	return false;
}
#endif // WITH_EDITOR	
// End of UObject interface


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
	ShapeSettings->RestoreProperties(this);

	MaterialProperties = NewObject<UNewMeshMaterialProperties>(this);
	AddToolPropertySource(MaterialProperties);
	MaterialProperties->RestoreProperties(this);

	// create preview mesh object
	PreviewMesh = NewObject<UPreviewMesh>(this, TEXT("PreviewMesh"));
	PreviewMesh->CreateInWorld(TargetWorld, FTransform::Identity);
	PreviewMesh->SetVisible(false);
	PreviewMesh->SetMaterial(MaterialProperties->Material);

	UpdatePreviewMesh();

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartAddPrimitiveTool", "Position the Primitive by moving the mouse over the scene. Drop a new instance by Left-clicking."),
		EToolMessageLevel::UserNotification);
}


void UAddPrimitiveTool::Shutdown(EToolShutdownType ShutdownType)
{
	PreviewMesh->SetVisible(false);
	PreviewMesh->Disconnect();
	PreviewMesh = nullptr;

	ShapeSettings->SaveProperties(this);
	MaterialProperties->SaveProperties(this);	
}


void UAddPrimitiveTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	//FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	//MeshDebugDraw::DrawSimpleGrid(ShapeFrame, 13, 5.0f, 1.0f, FColor::Orange, false, PDI, FTransform::Identity);
}



void UAddPrimitiveTool::OnPropertyModified(UObject* PropertySet, UProperty* Property)
{
	PreviewMesh->EnableWireframe(MaterialProperties->bWireframe);
	PreviewMesh->SetMaterial(MaterialProperties->Material);
	UpdatePreviewMesh();
}




FInputRayHit UAddPrimitiveTool::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	return FInputRayHit(0.0f);		// always hit in hover 
}

void UAddPrimitiveTool::OnBeginHover(const FInputDeviceRay& DevicePos)
{
	UpdatePreviewPosition(DevicePos);
}

bool UAddPrimitiveTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	UpdatePreviewPosition(DevicePos);
	return true;
}

void UAddPrimitiveTool::OnEndHover()
{
	// do nothing
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

	if (MaterialProperties->UVScale != 1.0 || MaterialProperties->bWorldSpaceUVScale)
	{
		FDynamicMeshEditor Editor(&NewMesh);
		float WorldUnitsInMetersFactor = MaterialProperties->bWorldSpaceUVScale ? .01f : 1.0f;
		Editor.RescaleAttributeUVs(MaterialProperties->UVScale * WorldUnitsInMetersFactor, MaterialProperties->bWorldSpaceUVScale);
	}

	// set mesh position
	FAxisAlignedBox3d Bounds = NewMesh.GetCachedBounds();
	FVector3d TargetOrigin = Bounds.Center();
	if (ShapeSettings->PivotLocation == EMakeMeshPivotLocation::Base)
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
	GetToolManager()->BeginUndoTransaction(LOCTEXT("AddPrimitiveToolTransactionName", "Add Primitive Mesh"));

	const UEnum* const MakeMeshShapeTypeEnum = StaticEnum<EMakeMeshShapeType>();		
	FString ShapeTypeName = MakeMeshShapeTypeEnum->GetNameStringByValue((uint8)ShapeSettings->Shape);

	AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
		AssetAPI, TargetWorld,
		CurMesh, CurTransform, ShapeTypeName,
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
	CylGen.LengthSamples = ShapeSettings->Subdivisions;
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
	CylGen.LengthSamples = ShapeSettings->Subdivisions;
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
