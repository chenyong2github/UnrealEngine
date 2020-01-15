// Copyright Epic Games, Inc. All Rights Reserved.

#include "AddPatchTool.h"
#include "ToolBuilderUtil.h"
#include "InteractiveToolManager.h"
#include "BaseBehaviors/MouseHoverBehavior.h"
#include "Selection/ToolSelectionUtil.h"
#include "AssetGenerationUtil.h"

#include "MeshDescriptionBuilder.h"
#include "Generators/RectangleMeshGenerator.h"
#include "DynamicMeshAttributeSet.h"
#include "StaticMeshComponentBuilder.h"
#include "Drawing/MeshDebugDrawing.h"
#include "MeshNormals.h"

#include "Async/ParallelFor.h"

#include "DynamicMeshEditor.h"

#define LOCTEXT_NAMESPACE "UAddPatchTool"


/*
 * ToolBuilder
 */
bool UAddPatchToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return (this->AssetAPI != nullptr);
}

UInteractiveTool* UAddPatchToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UAddPatchTool* NewTool = NewObject<UAddPatchTool>(SceneState.ToolManager);
	NewTool->SetWorld(SceneState.World);
	NewTool->SetAssetAPI(AssetAPI);
	return NewTool;
}

/*
 * Tool
 */
UAddPatchToolProperties::UAddPatchToolProperties()
{
	Width = 10000;
	Subdivisions = 50;
	Rotation = 0;
	Shift = 0.0;
}

void UAddPatchToolProperties::SaveProperties(UInteractiveTool* SaveFromTool)
{
	UAddPatchToolProperties* PropertyCache = GetPropertyCache<UAddPatchToolProperties>();
	PropertyCache->Width = this->Width;
	PropertyCache->Rotation = this->Rotation;
	PropertyCache->Subdivisions = this->Subdivisions;
}

void UAddPatchToolProperties::RestoreProperties(UInteractiveTool* RestoreToTool)
{
	UAddPatchToolProperties* PropertyCache = GetPropertyCache<UAddPatchToolProperties>();
	this->Width = PropertyCache->Width;
	this->Rotation = PropertyCache->Rotation;
	this->Subdivisions = PropertyCache->Subdivisions;
}



void UAddPatchTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

void UAddPatchTool::SetAssetAPI(IToolsContextAssetAPI* AssetAPIIn)
{
	this->AssetAPI = AssetAPIIn;
}


void UAddPatchTool::Setup()
{
	USingleClickTool::Setup();

	UMouseHoverBehavior* HoverBehavior = NewObject<UMouseHoverBehavior>(this);
	HoverBehavior->Initialize(this);
	AddInputBehavior(HoverBehavior);

	ShapeSettings = NewObject<UAddPatchToolProperties>(this);
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

	GeneratePreviewBaseMesh();

	WorldBounds = FBox(ForceInit);
	for (const ULevel* Level : TargetWorld->GetLevels())
	{
		for (AActor* Actor : Level->Actors)
		{
			if (Actor)
			{
				FBox ActorBox = Actor->GetComponentsBoundingBox(true);
				if (ActorBox.IsValid)
				{
					WorldBounds += ActorBox;
				}
			}
		}
	}
	float WorldDiag = WorldBounds.GetSize().Size();
	if (ShapeSettings->Width > WorldDiag * 0.25)
	{
		ShapeSettings->Width = WorldDiag * 0.25;
	}

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartAddPatchTool", "Position the Patch by moving the mouse over the scene. Drop a new instance by Left-clicking."),
		EToolMessageLevel::UserNotification);
}


void UAddPatchTool::Shutdown(EToolShutdownType ShutdownType)
{
	PreviewMesh->SetVisible(false);
	PreviewMesh->Disconnect();
	PreviewMesh = nullptr;

	ShapeSettings->SaveProperties(this);
	MaterialProperties->SaveProperties(this);
}


void UAddPatchTool::Render(IToolsContextRenderAPI* RenderAPI)
{
}



void UAddPatchTool::OnPropertyModified(UObject* PropertySet, FProperty* Property)
{
	PreviewMesh->EnableWireframe(MaterialProperties->bWireframe);
	PreviewMesh->SetMaterial(MaterialProperties->Material);
	GeneratePreviewBaseMesh();
}



FInputRayHit UAddPatchTool::BeginHoverSequenceHitTest(const FInputDeviceRay& PressPos)
{
	return FInputRayHit(0.0f);		// always hit in hover 
}

void UAddPatchTool::OnBeginHover(const FInputDeviceRay& DevicePos)
{
	UpdatePreviewPosition(DevicePos);
}

bool UAddPatchTool::OnUpdateHover(const FInputDeviceRay& DevicePos)
{
	UpdatePreviewPosition(DevicePos);
	return true;
}

void UAddPatchTool::OnEndHover()
{
	// do nothing
}



void UAddPatchTool::Tick(float DeltaTime)
{
	if (bPreviewValid == false)
	{
		UpdatePreviewMesh();
		PreviewMesh->SetVisible(true);
		bPreviewValid = true;
	}
}



void UAddPatchTool::UpdatePreviewPosition(const FInputDeviceRay& DeviceClickPos)
{
	FRay ClickPosWorldRay = DeviceClickPos.WorldRay;

	// cast ray into scene
	FVector RayStart = ClickPosWorldRay.Origin;
	FVector RayEnd = ClickPosWorldRay.PointAt(999999);
	FCollisionObjectQueryParams QueryParams(FCollisionObjectQueryParams::AllObjects);
	FHitResult Result;
	bool bHit = TargetWorld->LineTraceSingleByObjectType(Result, RayStart, RayEnd, QueryParams);
	if (bHit)
	{
		ShapeFrame = FFrame3f(Result.ImpactPoint, Result.ImpactNormal);
		//ShapeFrame.ConstrainedAlignPerpAxes();
	}

	// clear rotation
	ShapeFrame.Rotation = FQuaternionf::Identity();

	// rotate around up axis
	if (ShapeSettings->Rotation != 0)
	{
		ShapeFrame.Rotate(FQuaternionf(ShapeFrame.Z(), ShapeSettings->Rotation, true));
	}

	if (bHit == false)
	{
		PreviewMesh->SetVisible(false);
	}
	else 
	{
		bPreviewValid = false;
	}
}


void UAddPatchTool::UpdatePreviewMesh()
{
	FVector Direction = FVector(0, 0, 1);
	float WorldMaxHeight = WorldBounds.Max.Z;
	float WorldMinHeight = WorldBounds.Min.Z;
	float WorldHeight = WorldMaxHeight - WorldMinHeight;

	FDynamicMesh3 Projected(*BaseMesh);

	FTransform MoveTransform = ShapeFrame.ToFTransform();

	FFrame3d RayFrame(MoveTransform);
	RayFrame.Origin.Z = WorldMaxHeight + 100.0f;

	TSet<int> Misses;
	FCriticalSection MissesLock;


	int NumVerts = Projected.MaxVertexID();
	ParallelFor(NumVerts, [this, &Misses, &MissesLock, &Projected, RayFrame, WorldMinHeight, Direction, MoveTransform](int vid)
	{
		FVector3d Pos = Projected.GetVertex(vid);
		Pos = RayFrame.FromFramePoint(Pos);
		FVector RayStart = (FVector)Pos;
		FVector RayEnd = RayStart; RayEnd.Z = WorldMinHeight;

		FCollisionObjectQueryParams QueryParams(FCollisionObjectQueryParams::AllObjects);
		FHitResult Result;
		bool bHit = TargetWorld->LineTraceSingleByObjectType(Result, RayStart, RayEnd, QueryParams);
		if (bHit)
		{
			FVector3d HitPoint = Result.ImpactPoint + ShapeSettings->Shift * Direction;
			FVector HitPosWorld = (FVector)HitPoint;
			FVector HitPosLocal = MoveTransform.InverseTransformPosition(HitPosWorld);
			Projected.SetVertex(vid, HitPosLocal);
		}
		else
		{
			MissesLock.Lock();
			Misses.Add(vid);
			MissesLock.Unlock();
		}
	});

	TArray<int> RemoveTris;
	for (int tid : Projected.TriangleIndicesItr())
	{
		FIndex3i Tri = Projected.GetTriangle(tid);
		if (Misses.Contains(Tri.A) || Misses.Contains(Tri.B) || Misses.Contains(Tri.C))
		{
			RemoveTris.Add(tid);
		}
	}

	FMeshNormals::QuickComputeVertexNormals(Projected);

	FDynamicMeshEditor Editor(&Projected);
	Editor.RemoveTriangles(RemoveTris, false);
	PreviewMesh->UpdatePreview(&Projected);
	PreviewMesh->SetTransform(MoveTransform);

	PreviewMesh->SetVisible(true);
}



void UAddPatchTool::GeneratePreviewBaseMesh()
{
	BaseMesh = MakeUnique<FDynamicMesh3>();
	GeneratePlane(BaseMesh.Get());

	if (MaterialProperties->UVScale != 1.0 || MaterialProperties->bWorldSpaceUVScale)
	{
		FDynamicMeshEditor Editor(BaseMesh.Get());
		float WorldUnitsInMetersFactor = MaterialProperties->bWorldSpaceUVScale ? .01f : 1.0f;
		Editor.RescaleAttributeUVs(MaterialProperties->UVScale * WorldUnitsInMetersFactor, MaterialProperties->bWorldSpaceUVScale);
	}

	// recenter mesh
	FAxisAlignedBox3d Bounds = BaseMesh->GetCachedBounds();
	FVector3d TargetOrigin = Bounds.Center();
	for (int vid : BaseMesh->VertexIndicesItr())
	{
		FVector3d Pos = BaseMesh->GetVertex(vid);
		Pos -= TargetOrigin;
		BaseMesh->SetVertex(vid, Pos);
	}

	PreviewMesh->UpdatePreview(BaseMesh.Get());
}


void UAddPatchTool::OnClicked(const FInputDeviceRay& DeviceClickPos)
{
#if WITH_EDITOR
	const FDynamicMesh3* CurMesh = PreviewMesh->GetPreviewDynamicMesh();
	FTransform3d CurTransform(PreviewMesh->GetTransform());
	UMaterialInterface* Material = PreviewMesh->GetMaterial();
	GetToolManager()->BeginUndoTransaction(LOCTEXT("AddPatchToolTransactionName", "Add Patch Mesh"));

	AActor* NewActor = AssetGenerationUtil::GenerateStaticMeshActor(
		AssetAPI, TargetWorld,
		CurMesh, CurTransform, TEXT("Patch"),
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





void UAddPatchTool::GeneratePlane(FDynamicMesh3* OutMesh)
{
	FRectangleMeshGenerator RectGen;
	RectGen.Width = RectGen.Height = ShapeSettings->Width;
	RectGen.WidthVertexCount = RectGen.HeightVertexCount = ShapeSettings->Subdivisions + 2;
	RectGen.Generate();
	OutMesh->Copy(&RectGen);
}



#undef LOCTEXT_NAMESPACE
