// Copyright Epic Games, Inc. All Rights Reserved.

#include "AddPrimitiveTool.h"
#include "ToolBuilderUtil.h"
#include "InteractiveToolManager.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "Selection/ToolSelectionUtil.h"
#include "AssetGenerationUtil.h"
#include "ToolSceneQueriesUtil.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"

#include "Generators/SweepGenerator.h"
#include "Generators/GridBoxMeshGenerator.h"
#include "Generators/RectangleMeshGenerator.h"
#include "Generators/SphereGenerator.h"
#include "Generators/BoxSphereGenerator.h"
#include "Generators/DiscMeshGenerator.h"
#include "DynamicMeshAttributeSet.h"
#include "FaceGroupUtil.h"

#include "Drawing/MeshDebugDrawing.h"

#include "DynamicMeshEditor.h"
#include "UObject/PropertyIterator.h"
#include "UObject/UnrealType.h"

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
	UAddPrimitiveTool* NewTool = nullptr;
	switch (ShapeType)
	{
	case EMakeMeshShapeType::Box:
		NewTool = NewObject<UAddBoxPrimitiveTool>(SceneState.ToolManager);
		break;
	case EMakeMeshShapeType::Cylinder:
		NewTool = NewObject<UAddCylinderPrimitiveTool>(SceneState.ToolManager);
		break;
	case EMakeMeshShapeType::Cone:
		NewTool = NewObject<UAddConePrimitiveTool>(SceneState.ToolManager);
		break;
	case EMakeMeshShapeType::Arrow:
		NewTool = NewObject<UAddArrowPrimitiveTool>(SceneState.ToolManager);
		break;
	case EMakeMeshShapeType::Rectangle:
		NewTool = NewObject<UAddRectanglePrimitiveTool>(SceneState.ToolManager);
		break;
	case EMakeMeshShapeType::RoundedRectangle:
		NewTool = NewObject<UAddRoundedRectanglePrimitiveTool>(SceneState.ToolManager);
		break;
	case EMakeMeshShapeType::Disc:
		NewTool = NewObject<UAddDiscPrimitiveTool>(SceneState.ToolManager);
		break;
	case EMakeMeshShapeType::PuncturedDisc:
		NewTool = NewObject<UAddPuncturedDiscPrimitiveTool>(SceneState.ToolManager);
		break;
	case EMakeMeshShapeType::Torus:
		NewTool = NewObject<UAddTorusPrimitiveTool>(SceneState.ToolManager);
		break;
	case EMakeMeshShapeType::SphericalBox:
		NewTool = NewObject<UAddSphericalBoxPrimitiveTool>(SceneState.ToolManager);
		break;
	case EMakeMeshShapeType::Sphere:
		NewTool = NewObject<UAddSpherePrimitiveTool>(SceneState.ToolManager);
		break;
	default:
		break;
	}
	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);
	return NewTool;
}

bool
UProceduralShapeToolProperties::IsEquivalent( const UProceduralShapeToolProperties* Other ) const
{
#if WITH_EDITOR
	UClass* Class = GetClass();
	if ( Other->GetClass() != Class )
	{
		return false;
	}
	for ( FProperty* Prop : TFieldRange<FProperty>(Class) )
	{
		if (Prop->HasMetaData(TEXT("ProceduralShapeSetting")) &&
			(!Prop->Identical_InContainer(this, Other)))
		{
			return false;
		}
	}
	return true;
#else
	return false;
#endif
}

void UAddPrimitiveTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UAddPrimitiveTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}

UAddPrimitiveTool::UAddPrimitiveTool(const FObjectInitializer&)
{
	ShapeSettings = CreateDefaultSubobject<UProceduralShapeToolProperties>(TEXT("ShapeSettings"));
}

void UAddPrimitiveTool::Setup()
{
	USingleClickTool::Setup();

	UMouseHoverBehavior* HoverBehavior = NewObject<UMouseHoverBehavior>(this);
	HoverBehavior->Initialize(this);
	AddInputBehavior(HoverBehavior);

	AddToolPropertySource(ShapeSettings);
	ShapeSettings->RestoreProperties(this);

	MaterialProperties = NewObject<UNewMeshMaterialProperties>(this);
	AddToolPropertySource(MaterialProperties);
	MaterialProperties->RestoreProperties(this);

	// create preview mesh object
	PreviewMesh = NewObject<UPreviewMesh>(this, TEXT("PreviewMesh"));
	PreviewMesh->CreateInWorld(TargetWorld, FTransform::Identity);
	PreviewMesh->SetVisible(false);
	PreviewMesh->SetMaterial(MaterialProperties->Material.Get());
	PreviewMesh->EnableWireframe(MaterialProperties->bWireframe);

	UpdatePreviewMesh();

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartAddPrimitiveTool", "This Tool creates new Primitive mesh assets. Position the Primitive by moving the mouse over the scene. Drop a new Asset or Instance by left-clicking (depending on Asset settings)."),
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
}



void UAddPrimitiveTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	// Because of how the ShapeSettings property set is implemented in this Tool, changes to it are transacted,
	// and if the user exits the Tool and then tries to undo/redo those transactions, this function will end up being called.
	// So we need to ensure that we handle this case.
	if (PreviewMesh)
	{
		PreviewMesh->EnableWireframe(MaterialProperties->bWireframe);
		PreviewMesh->SetMaterial(MaterialProperties->Material.Get());
		UpdatePreviewMesh();
	}
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

	FPlane DrawPlane(FVector::ZeroVector, FVector(0, 0, 1));
	if (ShapeSettings->PlaceMode == EMakeMeshPlacementType::GroundPlane)
	{
		FVector DrawPlanePos = FMath::RayPlaneIntersection(ClickPosWorldRay.Origin, ClickPosWorldRay.Direction, DrawPlane);
		bHit = true;
		ShapeFrame = FFrame3f(DrawPlanePos);
	}
	else
	{
		// cast ray into scene
		FHitResult Result;
		bHit = ToolSceneQueriesUtil::FindNearestVisibleObjectHit(TargetWorld, Result, ClickPosWorldRay);
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
		else
		{
			// fall back to ground plane if we don't have a scene hit
			FVector DrawPlanePos = FMath::RayPlaneIntersection(ClickPosWorldRay.Origin, ClickPosWorldRay.Direction, DrawPlane);
			bHit = true;
			ShapeFrame = FFrame3f(DrawPlanePos);
		}
	}

	// Snap to grid if applicable
	if (ShapeSettings->bSnapToGrid
		&& GetToolManager()->GetContextQueriesAPI()->GetCurrentCoordinateSystem() == EToolContextCoordinateSystem::World)
	{
		FSceneSnapQueryRequest Request;
		Request.RequestType = ESceneSnapQueryType::Position;
		Request.TargetTypes = ESceneSnapQueryTargetType::Grid;
		Request.Position = (FVector)ShapeFrame.Origin;
		TArray<FSceneSnapQueryResult> Results;
		if (GetToolManager()->GetContextQueriesAPI()->ExecuteSceneSnapQuery(Request, Results))
		{
			ShapeFrame.Origin = Results[0].Position;
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
	GenerateMesh( &NewMesh );

	if (ShapeSettings->PolygroupMode == EMakeMeshPolygroupMode::Single)
	{
		FaceGroupUtil::SetGroupID(NewMesh, 0);
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
	UMaterialInterface* Material = PreviewMesh->GetMaterial();

	if (ShapeSettings->bInstanceIfPossible && IsEquivalentLastGeneratedAsset())
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

	AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
		AssetAPI, TargetWorld,
		CurMesh, CurTransform, AssetName, Material);
	if (NewActor != nullptr)
	{
		LastGenerated = NewObject<ULastActorInfo>(this);
		LastGenerated->ShapeSettings = DuplicateObject(ShapeSettings, nullptr);
		LastGenerated->MaterialProperties = DuplicateObject(MaterialProperties, nullptr);
		LastGenerated->Actor = NewActor;
		LastGenerated->StaticMesh = CastChecked<AStaticMeshActor>(LastGenerated->Actor)->GetStaticMeshComponent()->GetStaticMesh();
		LastGenerated->Label = LastGenerated->Actor->GetActorLabel();

		// select newly-created object
		ToolSelectionUtil::SetNewActorSelection(GetToolManager(), LastGenerated->Actor);
	}

	GetToolManager()->EndUndoTransaction();
#else
	checkNoEntry();
#endif
}

void UAddBoxPrimitiveTool::GenerateMesh(FDynamicMesh3* OutMesh) const
{
	FGridBoxMeshGenerator BoxGen;
	auto* BoxSettings = Cast<UProceduralBoxToolProperties>(ShapeSettings);
	BoxGen.Box = FOrientedBox3d(FVector3d::Zero(), 0.5*FVector3d(BoxSettings->Depth, BoxSettings->Width, BoxSettings->Height));
	BoxGen.EdgeVertices = FIndex3i(BoxSettings->DepthSubdivisions + 1,
								   BoxSettings->WidthSubdivisions + 1,
								   BoxSettings->HeightSubdivisions + 1);
	if (ShapeSettings->PolygroupMode == EMakeMeshPolygroupMode::PerQuad)
	{
		BoxGen.bPolygroupPerQuad = true;
	}
	BoxGen.Generate();
	OutMesh->Copy(&BoxGen);
}

void UAddRectanglePrimitiveTool::GenerateMesh(FDynamicMesh3* OutMesh) const
{
	FRectangleMeshGenerator RectGen;
	auto* RectangleSettings = Cast<UProceduralRectangleToolProperties>(ShapeSettings);
	RectGen.Width = RectangleSettings->Depth;
	RectGen.Height = RectangleSettings->Width;
	RectGen.WidthVertexCount = RectangleSettings->DepthSubdivisions + 1;
	RectGen.HeightVertexCount = RectangleSettings->WidthSubdivisions + 1;
	if (ShapeSettings->PolygroupMode != EMakeMeshPolygroupMode::PerQuad)
	{
		RectGen.bSinglePolygroup = true;
	}
	RectGen.Generate();
	OutMesh->Copy(&RectGen);
}

void UAddRoundedRectanglePrimitiveTool::GenerateMesh(FDynamicMesh3* OutMesh) const
{
	FRoundedRectangleMeshGenerator RectGen;
	auto* RectangleSettings = Cast<UProceduralRoundedRectangleToolProperties>(ShapeSettings);
	RectGen.Width = RectangleSettings->Depth;
	RectGen.Height = RectangleSettings->Width;
	RectGen.WidthVertexCount = RectangleSettings->DepthSubdivisions + 1;
	RectGen.HeightVertexCount = RectangleSettings->WidthSubdivisions + 1;
	if (ShapeSettings->PolygroupMode != EMakeMeshPolygroupMode::PerQuad)
	{
		RectGen.bSinglePolygroup = true;
	}
	RectGen.Radius = RectangleSettings->CornerRadius;
	RectGen.AngleSamples = RectangleSettings->CornerSlices - 1;
	RectGen.Generate();
	OutMesh->Copy(&RectGen);
}

void UAddDiscPrimitiveTool::GenerateMesh(FDynamicMesh3* OutMesh) const
{
	FDiscMeshGenerator Gen;
	auto* DiscSettings = Cast<UProceduralDiscToolProperties>(ShapeSettings);
	Gen.Radius = DiscSettings->Radius;
	Gen.AngleSamples = DiscSettings->RadialSlices;
	Gen.RadialSamples = DiscSettings->RadialSubdivisions;
	if (ShapeSettings->PolygroupMode != EMakeMeshPolygroupMode::PerQuad)
	{
		Gen.bSinglePolygroup = true;
	}
	Gen.Generate();
	OutMesh->Copy(&Gen);
}

void UAddPuncturedDiscPrimitiveTool::GenerateMesh(FDynamicMesh3* OutMesh) const
{
	FPuncturedDiscMeshGenerator Gen;
	auto* DiscSettings = Cast<UProceduralPuncturedDiscToolProperties>(ShapeSettings);
	Gen.Radius = DiscSettings->Radius;
	Gen.HoleRadius = FMath::Min(DiscSettings->HoleRadius, Gen.Radius * .999f); // hole cannot be bigger than outer radius
	Gen.AngleSamples = DiscSettings->RadialSlices;
	Gen.RadialSamples = DiscSettings->RadialSubdivisions;
	if (ShapeSettings->PolygroupMode != EMakeMeshPolygroupMode::PerQuad)
	{
		Gen.bSinglePolygroup = true;
	}
	Gen.Generate();
	OutMesh->Copy(&Gen);
}

void UAddTorusPrimitiveTool::GenerateMesh(FDynamicMesh3* OutMesh) const
{
	FGeneralizedCylinderGenerator Gen;
	auto* TorusSettings = Cast<UProceduralTorusToolProperties>(ShapeSettings);
	Gen.CrossSection = FPolygon2d::MakeCircle(TorusSettings->MinorRadius, TorusSettings->CrossSectionSlices);
	FPolygon2d PathCircle = FPolygon2d::MakeCircle(TorusSettings->MajorRadius, TorusSettings->TubeSlices);
	for (int Idx = 0; Idx < PathCircle.VertexCount(); Idx++)
	{
		Gen.Path.Add(PathCircle[Idx]);
	}
	Gen.bLoop = true;
	Gen.bCapped = false;
	if (ShapeSettings->PolygroupMode == EMakeMeshPolygroupMode::PerQuad)
	{
		Gen.bPolygroupPerQuad = true;
	}
	Gen.InitialFrame = FFrame3d(Gen.Path[0]);
	Gen.Generate();
	OutMesh->Copy(&Gen);
}

void UAddCylinderPrimitiveTool::GenerateMesh(FDynamicMesh3* OutMesh) const
{
	FCylinderGenerator CylGen;
	auto* CylinderSettings = Cast<UProceduralCylinderToolProperties>(ShapeSettings);
	CylGen.Radius[1] = CylGen.Radius[0] = CylinderSettings->Radius;
	CylGen.Height = CylinderSettings->Height;
	CylGen.AngleSamples = CylinderSettings->RadialSlices;
	CylGen.LengthSamples = CylinderSettings->HeightSubdivisions - 1;
	CylGen.bCapped = true;
	if (ShapeSettings->PolygroupMode == EMakeMeshPolygroupMode::PerQuad)
	{
		CylGen.bPolygroupPerQuad = true;
	}
	CylGen.Generate();
	OutMesh->Copy(&CylGen);
}

void UAddConePrimitiveTool::GenerateMesh(FDynamicMesh3* OutMesh) const
{
	// Unreal's standard cone is just a cylinder with a very small top
	FCylinderGenerator CylGen;
	auto* ConeSettings = Cast<UProceduralConeToolProperties>(ShapeSettings);
	CylGen.Radius[0] = ConeSettings->Radius;
	CylGen.Radius[1] = .01;
	CylGen.Height = ConeSettings->Height;
	CylGen.AngleSamples = ConeSettings->RadialSlices;
	CylGen.LengthSamples = ConeSettings->HeightSubdivisions - 1;
	CylGen.bCapped = true;
	if (ShapeSettings->PolygroupMode == EMakeMeshPolygroupMode::PerQuad)
	{
		CylGen.bPolygroupPerQuad = true;
	}
	CylGen.Generate();
	OutMesh->Copy(&CylGen);
}

void UAddArrowPrimitiveTool::GenerateMesh(FDynamicMesh3* OutMesh) const
{
	FArrowGenerator ArrowGen;
	auto* ArrowSettings = Cast<UProceduralArrowToolProperties>(ShapeSettings);
	ArrowGen.StickRadius = ArrowSettings->ShaftRadius;
	ArrowGen.StickLength = ArrowSettings->ShaftHeight;
	ArrowGen.HeadBaseRadius = ArrowSettings->HeadRadius;
	ArrowGen.HeadTipRadius = .01f;
	ArrowGen.HeadLength = ArrowSettings->HeadHeight;
	ArrowGen.AngleSamples = ArrowSettings->RadialSlices;
	ArrowGen.bCapped = true;
	if (ShapeSettings->PolygroupMode == EMakeMeshPolygroupMode::PerQuad)
	{
		ArrowGen.bPolygroupPerQuad = true;
	}
	ArrowGen.DistributeAdditionalLengthSamples(ArrowSettings->TotalSubdivisions);
	ArrowGen.Generate();
	OutMesh->Copy(&ArrowGen);
}

void UAddSpherePrimitiveTool::GenerateMesh(FDynamicMesh3* OutMesh) const
{
	FSphereGenerator SphereGen;
	auto* SphereSettings = Cast<UProceduralSphereToolProperties>(ShapeSettings);
	SphereGen.Radius = SphereSettings->Radius;
	SphereGen.NumTheta = SphereSettings->LongitudeSlices + 1;
	SphereGen.NumPhi = SphereSettings->LatitudeSlices + 1;
	if (ShapeSettings->PolygroupMode == EMakeMeshPolygroupMode::PerQuad)
	{
		SphereGen.bPolygroupPerQuad = true;
	}
	SphereGen.Generate();
	OutMesh->Copy(&SphereGen);
}

void UAddSphericalBoxPrimitiveTool::GenerateMesh(FDynamicMesh3* OutMesh) const
{
	FBoxSphereGenerator SphereGen;
	auto* SphereSettings = Cast<UProceduralSphericalBoxToolProperties>(ShapeSettings);
	SphereGen.Radius = SphereSettings->Radius;
	SphereGen.Box = FOrientedBox3d(FVector3d::Zero(),
								   0.5*FVector3d(SphereSettings->Subdivisions + 1,
												 SphereSettings->Subdivisions + 1,
												 SphereSettings->Subdivisions + 1));
	int EdgeNum = SphereSettings->Subdivisions + 1;
	SphereGen.EdgeVertices = FIndex3i(EdgeNum, EdgeNum, EdgeNum);
	if (ShapeSettings->PolygroupMode == EMakeMeshPolygroupMode::PerQuad)
	{
		SphereGen.bPolygroupPerQuad = true;
	}
	SphereGen.Generate();
	OutMesh->Copy(&SphereGen);
}

#undef LOCTEXT_NAMESPACE
