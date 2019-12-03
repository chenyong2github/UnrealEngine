// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AddPrimitiveTool.h"
#include "ToolBuilderUtil.h"
#include "InteractiveToolManager.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "Selection/ToolSelectionUtil.h"
#include "AssetGenerationUtil.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"


#include "MeshDescriptionBuilder.h"
#include "Generators/SweepGenerator.h"
#include "Generators/GridBoxMeshGenerator.h"
#include "Generators/RectangleMeshGenerator.h"
#include "Generators/SphereGenerator.h"
#include "Generators/BoxSphereGenerator.h"
#include "Generators/DiscMeshGenerator.h"
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
	FeatureRadius = 25;
	//StartAngle = 0;
	//EndAngle = 360;
	Slices = 16;
	Subdivisions = 0;
	PivotLocation = EMakeMeshPivotLocation::Base;
	PlaceMode = EMakeMeshPlacementType::OnScene;
	bAlignShapeToPlacementSurface = true;
}

void UProceduralShapeToolProperties::SaveProperties(UInteractiveTool* SaveFromTool)
{
	UProceduralShapeToolProperties* PropertyCache = GetPropertyCache<UProceduralShapeToolProperties>();
	PropertyCache->Shape = this->Shape;
	PropertyCache->Width = this->Width;
	PropertyCache->Height = this->Height;
	PropertyCache->FeatureRadius = this->FeatureRadius;
	PropertyCache->Slices = this->Slices;
	PropertyCache->Subdivisions = this->Subdivisions;
	PropertyCache->PivotLocation = this->PivotLocation;
	PropertyCache->PlaceMode = this->PlaceMode;
	PropertyCache->bAlignShapeToPlacementSurface = this->bAlignShapeToPlacementSurface;
}

void UProceduralShapeToolProperties::RestoreProperties(UInteractiveTool* RestoreToTool)
{
	UProceduralShapeToolProperties* PropertyCache = GetPropertyCache<UProceduralShapeToolProperties>();
	this->Shape = PropertyCache->Shape;
	this->Width = PropertyCache->Width;
	this->Height = PropertyCache->Height;
	this->FeatureRadius = PropertyCache->FeatureRadius;
	this->Slices = PropertyCache->Slices;
	this->Subdivisions = PropertyCache->Subdivisions;
	this->PivotLocation = PropertyCache->PivotLocation;
	this->PlaceMode = PropertyCache->PlaceMode;
	this->bAlignShapeToPlacementSurface = PropertyCache->bAlignShapeToPlacementSurface;
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
	  { TEXT("Height"),        EMakeMeshShapeType::Box | EMakeMeshShapeType::Cylinder | EMakeMeshShapeType::Cone | EMakeMeshShapeType::Arrow | EMakeMeshShapeType::Rectangle | EMakeMeshShapeType::RoundedRectangle },
	  { TEXT("FeatureRadius"),		   EMakeMeshShapeType::Arrow | EMakeMeshShapeType::RoundedRectangle | EMakeMeshShapeType::PuncturedDisc | EMakeMeshShapeType::Torus },
	  { TEXT("Rotation"),      EMakeMeshShapeType::All },
	  { TEXT("PlaceMode"),     EMakeMeshShapeType::All },
	  { TEXT("PivotLocation"), EMakeMeshShapeType::All },
	  { TEXT("bAlignShapeToPlacementSurface"), EMakeMeshShapeType::All },
	  { TEXT("bInstanceLastCreatedAssetIfPossible"), EMakeMeshShapeType::All },
	  { TEXT("Slices"),        EMakeMeshShapeType::Cylinder | EMakeMeshShapeType::Cone | EMakeMeshShapeType::Arrow | EMakeMeshShapeType::RoundedRectangle | EMakeMeshShapeType::Disc | EMakeMeshShapeType::PuncturedDisc | EMakeMeshShapeType::Sphere | EMakeMeshShapeType::Torus },
	  { TEXT("Subdivisions"),  EMakeMeshShapeType::Box | EMakeMeshShapeType::Rectangle | EMakeMeshShapeType::RoundedRectangle | EMakeMeshShapeType::Disc | EMakeMeshShapeType::PuncturedDisc | EMakeMeshShapeType::Cylinder |
							   EMakeMeshShapeType::Cone | EMakeMeshShapeType::Arrow | EMakeMeshShapeType::SphericalBox | EMakeMeshShapeType::Torus }
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

	if (ShapeSettings->PlaceMode == EMakeMeshPlacementType::GroundPlane)
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
			FVector3f Normal = Result.ImpactNormal;
			if (!ShapeSettings->bAlignShapeToPlacementSurface)
			{
				Normal = FVector3f::UnitZ();
			}
			ShapeFrame = FFrame3f(Result.ImpactPoint, Normal);
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
	case EMakeMeshShapeType::Rectangle:
		GenerateRectangle(&NewMesh);
		break;

	case EMakeMeshShapeType::RoundedRectangle:
		GenerateRoundedRectangle(&NewMesh);
		break;

	case EMakeMeshShapeType::Disc:
		GenerateDisc(&NewMesh);
		break;

	case EMakeMeshShapeType::PuncturedDisc:
		GeneratePuncturedDisc(&NewMesh);
		break;

	case EMakeMeshShapeType::Cylinder:
		GenerateCylinder(&NewMesh);
		break;

	case EMakeMeshShapeType::Cone:
		GenerateCone(&NewMesh);
		break;

	case EMakeMeshShapeType::Arrow:
		GenerateArrow(&NewMesh);
		break;

	case EMakeMeshShapeType::Torus:
		GenerateTorus(&NewMesh);
		break;

	case EMakeMeshShapeType::Sphere:
		GenerateSphere(&NewMesh);
		break;

	case EMakeMeshShapeType::SphericalBox:
		GenerateSphericalBox(&NewMesh);
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
	else if (ShapeSettings->PivotLocation == EMakeMeshPivotLocation::Top)
	{
		TargetOrigin.Z = Bounds.Max.Z;
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
	const UEnum* const MakeMeshShapeTypeEnum = StaticEnum<EMakeMeshShapeType>();		
	FString ShapeTypeName = MakeMeshShapeTypeEnum->GetNameStringByValue((int64)ShapeSettings->Shape);
	UMaterialInterface* Material = PreviewMesh->GetMaterial();

	if (ShapeSettings->bInstanceLastCreatedAssetIfPossible && IsEquivalentLastGeneratedAsset())
	{
		GetToolManager()->BeginUndoTransaction(LOCTEXT("AddPrimitiveToolTransactionName", "Add Primitive Mesh"));
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Template = LastGenerated->Actor;
		FTransform3d CurTransform(PreviewMesh->GetTransform());
		FRotator Rotation(0.0f, 0.0f, 0.0f);
		AStaticMeshActor* CloneActor = TargetWorld->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, Rotation, SpawnParameters);
		// some properties must be manually set on the component because they will not persist reliably through the spawn template (especially if the actor creation was undone)
		CloneActor->GetStaticMeshComponent()->SetWorldTransform(PreviewMesh->GetTransform());
		CloneActor->GetStaticMeshComponent()->SetStaticMesh(LastGenerated->StaticMesh);
		CloneActor->GetStaticMeshComponent()->SetMaterial(0, Material);
		CloneActor->SetActorLabel(LastGenerated->Label);
		// select newly-created object
		ToolSelectionUtil::SetNewActorSelection(GetToolManager(), CloneActor);
		GetToolManager()->EndUndoTransaction();

		return;
	}

	const FDynamicMesh3* CurMesh = PreviewMesh->GetPreviewDynamicMesh();
	FTransform3d CurTransform(PreviewMesh->GetTransform());
	
	GetToolManager()->BeginUndoTransaction(LOCTEXT("AddPrimitiveToolTransactionName", "Add Primitive Mesh"));

	LastGenerated = NewObject<ULastActorInfo>(this);
	LastGenerated->ShapeSettings = DuplicateObject(ShapeSettings, nullptr);
	LastGenerated->MaterialProperties = DuplicateObject(MaterialProperties, nullptr);
	LastGenerated->Actor = AssetGenerationUtil::GenerateStaticMeshActor(
		AssetAPI, TargetWorld,
		CurMesh, CurTransform, ShapeTypeName,
		AssetGenerationUtil::GetDefaultAutoGeneratedAssetPath(),
		Material
	);
	LastGenerated->StaticMesh = CastChecked<AStaticMeshActor>(LastGenerated->Actor)->GetStaticMeshComponent()->GetStaticMesh();
	LastGenerated->Label = LastGenerated->Actor->GetActorLabel();

	// select newly-created object
	ToolSelectionUtil::SetNewActorSelection(GetToolManager(), LastGenerated->Actor);

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


void UAddPrimitiveTool::GenerateRectangle(FDynamicMesh3* OutMesh)
{
	FRectangleMeshGenerator RectGen;
	RectGen.Width = ShapeSettings->Width;
	RectGen.Height = ShapeSettings->Height;
	RectGen.WidthVertexCount = RectGen.HeightVertexCount = ShapeSettings->Subdivisions + 2;
	RectGen.Generate();
	OutMesh->Copy(&RectGen);
}

void UAddPrimitiveTool::GenerateRoundedRectangle(FDynamicMesh3* OutMesh)
{
	FRoundedRectangleMeshGenerator RectGen;
	RectGen.Width = ShapeSettings->Width;
	RectGen.Height = ShapeSettings->Height;
	RectGen.WidthVertexCount = RectGen.HeightVertexCount = ShapeSettings->Subdivisions + 2;
	RectGen.Radius = ShapeSettings->FeatureRadius;
	RectGen.AngleSamples = ShapeSettings->Slices;
	RectGen.Generate();
	OutMesh->Copy(&RectGen);
}


void UAddPrimitiveTool::GenerateDisc(FDynamicMesh3* OutMesh)
{
	FDiscMeshGenerator Gen;
	Gen.Radius = ShapeSettings->Width * 0.5f;
	Gen.AngleSamples = ShapeSettings->Slices;
	Gen.RadialSamples = ShapeSettings->Subdivisions;
	Gen.Generate();
	OutMesh->Copy(&Gen);
}


void UAddPrimitiveTool::GeneratePuncturedDisc(FDynamicMesh3* OutMesh)
{
	FPuncturedDiscMeshGenerator Gen;
	Gen.Radius = ShapeSettings->Width * 0.5f;
	Gen.HoleRadius = FMath::Min(ShapeSettings->FeatureRadius, Gen.Radius * .999f); // hole cannot be bigger than outer radius
	Gen.AngleSamples = ShapeSettings->Slices;
	Gen.RadialSamples = ShapeSettings->Subdivisions;
	Gen.Generate();
	OutMesh->Copy(&Gen);
}


void UAddPrimitiveTool::GenerateTorus(FDynamicMesh3* OutMesh)
{
	FGeneralizedCylinderGenerator Gen;
	Gen.CrossSection = FPolygon2d::MakeCircle(ShapeSettings->FeatureRadius, ShapeSettings->Slices);
	FPolygon2d PathCircle = FPolygon2d::MakeCircle(ShapeSettings->Width*.5, ShapeSettings->Subdivisions+4);
	for (int Idx = 0; Idx < PathCircle.VertexCount(); Idx++)
	{
		Gen.Path.Add(PathCircle[Idx]);
	}
	Gen.bLoop = true;
	Gen.bCapped = false;
	Gen.InitialFrame = FFrame3d(Gen.Path[0]);
	Gen.Generate();
	OutMesh->Copy(&Gen);
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


void UAddPrimitiveTool::GenerateArrow(FDynamicMesh3* OutMesh)
{
	FArrowGenerator ArrowGen;
	ArrowGen.StickRadius = ShapeSettings->FeatureRadius;
	ArrowGen.StickLength = ShapeSettings->Height * .25f;
	ArrowGen.HeadBaseRadius = ShapeSettings->Width * .5f;
	ArrowGen.TipRadius = .01f;
	ArrowGen.HeadLength = ShapeSettings->Height * .75f;
	ArrowGen.AngleSamples = ShapeSettings->Slices;
	ArrowGen.bCapped = true;
	ArrowGen.DistributeAdditionalLengthSamples(ShapeSettings->Subdivisions);
	ArrowGen.Generate();
	OutMesh->Copy(&ArrowGen);
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


void UAddPrimitiveTool::GenerateSphericalBox(FDynamicMesh3* OutMesh)
{
	FBoxSphereGenerator SphereGen;
	SphereGen.Radius = ShapeSettings->Width * 0.5f;
	SphereGen.Box = FOrientedBox3d(FVector3d::Zero(), 0.5*FVector3d(ShapeSettings->Width, ShapeSettings->Width, ShapeSettings->Width));
	int EdgeNum = ShapeSettings->Subdivisions + 3;
	SphereGen.EdgeVertices = FIndex3i(EdgeNum, EdgeNum, EdgeNum);
	SphereGen.Generate();
	OutMesh->Copy(&SphereGen);
}


#undef LOCTEXT_NAMESPACE
