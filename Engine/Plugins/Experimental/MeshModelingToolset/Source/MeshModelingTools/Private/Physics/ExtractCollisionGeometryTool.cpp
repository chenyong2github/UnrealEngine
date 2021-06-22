// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/ExtractCollisionGeometryTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "ToolSetupUtil.h"
#include "ModelingObjectsCreationAPI.h"
#include "Selection/ToolSelectionUtil.h"
#include "Drawing/PreviewGeometryActor.h"
#include "Util/ColorConstants.h"
#include "PreviewMesh.h"

#include "DynamicMeshEditor.h"
#include "DynamicMesh/MeshNormals.h"
#include "Generators/SphereGenerator.h"
#include "Generators/MinimalBoxMeshGenerator.h"
#include "Generators/CapsuleGenerator.h"
#include "DynamicMesh/MeshTransforms.h"
#include "Parameterization/DynamicMeshUVEditor.h"
#include "DynamicMesh/Operations/MergeCoincidentMeshEdges.h"

#include "Physics/PhysicsDataCollection.h"
#include "Physics/CollisionGeometryVisualization.h"

// physics data
#include "Engine/Classes/Engine/StaticMesh.h"
#include "Engine/Classes/Components/StaticMeshComponent.h"
#include "Engine/Classes/PhysicsEngine/BodySetup.h"
#include "Engine/Classes/PhysicsEngine/AggregateGeom.h"

#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "TargetInterfaces/StaticMeshBackedTarget.h"
#include "ModelingToolTargetUtil.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UExtractCollisionGeometryTool"

const FToolTargetTypeRequirements& UExtractCollisionGeometryToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements({
		UMeshDescriptionProvider::StaticClass(),
		UPrimitiveComponentBackedTarget::StaticClass(),
		UStaticMeshBackedTarget::StaticClass()
		});
	return TypeRequirements;
}


USingleSelectionMeshEditingTool* UExtractCollisionGeometryToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	return NewObject<UExtractCollisionGeometryTool>(SceneState.ToolManager);
}


void UExtractCollisionGeometryTool::Setup()
{
	UInteractiveTool::Setup();

	// create preview mesh
	PreviewMesh = NewObject<UPreviewMesh>(this);
	PreviewMesh->bBuildSpatialDataStructure = false;
	PreviewMesh->CreateInWorld(TargetWorld, FTransform::Identity);
	PreviewMesh->SetTransform((FTransform)UE::ToolTarget::GetLocalToWorldTransform(Target));
	PreviewMesh->SetMaterial(ToolSetupUtil::GetDefaultSculptMaterial(GetToolManager()));
	PreviewMesh->SetOverrideRenderMaterial(ToolSetupUtil::GetSelectionMaterial(GetToolManager()));
	PreviewMesh->SetTriangleColorFunction([this](const FDynamicMesh3* Mesh, int TriangleID)
	{
		return LinearColors::SelectFColor(Mesh->GetTriangleGroup(TriangleID));
	});

	OutputTypeProperties = NewObject<UCreateMeshObjectTypeProperties>(this);
	OutputTypeProperties->OutputType = UCreateMeshObjectTypeProperties::VolumeIdentifier;		// prefer volumes for extracting simple collision
	OutputTypeProperties->RestoreProperties(this, TEXT("ExtractCollisionTool"));
	OutputTypeProperties->InitializeDefault();
	OutputTypeProperties->WatchProperty(OutputTypeProperties->OutputType, [this](FString) { OutputTypeProperties->UpdatePropertyVisibility(); });
	AddToolPropertySource(OutputTypeProperties);

	Settings = NewObject<UExtractCollisionToolProperties>(this);
	Settings->RestoreProperties(this);
	AddToolPropertySource(Settings);
	Settings->WatchProperty(Settings->CollisionType, [this](EExtractCollisionOutputType NewValue) { bResultValid = false; });
	Settings->WatchProperty(Settings->bWeldEdges, [this](bool bNewValue) { bResultValid = false; });
	Settings->WatchProperty(Settings->bShowPreview, [this](bool bNewValue) { PreviewMesh->SetVisible(bNewValue); });
	PreviewMesh->SetVisible(Settings->bShowPreview);
	Settings->WatchProperty(Settings->bShowInputMesh, [this](bool bNewValue) { UE::ToolTarget::SetSourceObjectVisible(Target, bNewValue); });
	UE::ToolTarget::SetSourceObjectVisible(Target, Settings->bShowInputMesh);

	VizSettings = NewObject<UCollisionGeometryVisualizationProperties>(this);
	VizSettings->RestoreProperties(this);
	AddToolPropertySource(VizSettings);
	VizSettings->WatchProperty(VizSettings->LineThickness, [this](float NewValue) { bVisualizationDirty = true; });
	VizSettings->WatchProperty(VizSettings->Color, [this](FColor NewValue) { bVisualizationDirty = true; });


	UPrimitiveComponent* TargetComponent = UE::ToolTarget::GetTargetComponent(Target);
	UStaticMeshComponent* StaticMeshComponent = (TargetComponent) ? Cast<UStaticMeshComponent>(TargetComponent) : nullptr;
	TObjectPtr<UStaticMesh> StaticMesh = (StaticMeshComponent) ? StaticMeshComponent->GetStaticMesh() : nullptr;
	UBodySetup* BodySetup = (StaticMesh) ? StaticMesh->GetBodySetup() : nullptr;
	if (ensure(BodySetup))
	{
		PhysicsInfo = MakeShared<FPhysicsDataCollection>();
		PhysicsInfo->InitializeFromComponent(StaticMeshComponent, true);

		PreviewElements = NewObject<UPreviewGeometry>(this);
		FTransform TargetTransform = (FTransform)UE::ToolTarget::GetLocalToWorldTransform(Target);
		//PhysicsInfo->ExternalScale3D = TargetTransform.GetScale3D();
		//TargetTransform.SetScale3D(FVector::OneVector);
		PreviewElements->CreateInWorld(UE::ToolTarget::GetTargetActor(Target)->GetWorld(), TargetTransform);
		
		UE::PhysicsTools::InitializePreviewGeometryLines(*PhysicsInfo, PreviewElements,
			VizSettings->Color, VizSettings->LineThickness, 0.0f, 16);

		ObjectProps = NewObject<UPhysicsObjectToolPropertySet>(this);
		UE::PhysicsTools::InitializePhysicsToolObjectPropertySet(PhysicsInfo.Get(), ObjectProps);
		AddToolPropertySource(ObjectProps);
	}

	SetToolDisplayName(LOCTEXT("ToolName", "Collision To Mesh"));
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool", "Convert Collision Geometry to Mesh Objects"),
		EToolMessageLevel::UserNotification);
}


void UExtractCollisionGeometryTool::Shutdown(EToolShutdownType ShutdownType)
{
	OutputTypeProperties->SaveProperties(this, TEXT("ExtractCollisionTool"));
	Settings->SaveProperties(this);
	VizSettings->SaveProperties(this);

	UE::Geometry::FTransform3d ActorTransform(PreviewMesh->GetTransform());

	PreviewElements->Disconnect();
	PreviewMesh->SetVisible(false);
	PreviewMesh->Disconnect();
	PreviewMesh = nullptr;

	UE::ToolTarget::ShowSourceObject(Target);

	if (ShutdownType == EToolShutdownType::Accept)
	{
		UMaterialInterface* UseMaterial = UMaterial::GetDefaultMaterial(MD_Surface);

		FString TargetName = UE::ToolTarget::GetTargetComponent(Target)->GetName();

		GetToolManager()->BeginUndoTransaction(LOCTEXT("CreateCollisionMesh", "Collision To Mesh"));

		TArray<AActor*> OutputSelection;

		auto EmitNewMesh = [&](FDynamicMesh3&& Mesh, FTransform3d UseTransform, FString UseName)
		{
			FCreateMeshObjectParams NewMeshObjectParams;
			NewMeshObjectParams.TargetWorld = TargetWorld;
			NewMeshObjectParams.Transform = (FTransform)UseTransform;
			NewMeshObjectParams.BaseName = UseName;
			NewMeshObjectParams.Materials.Add(UseMaterial);
			NewMeshObjectParams.SetMesh(MoveTemp(Mesh));
			OutputTypeProperties->ConfigureCreateMeshObjectParams(NewMeshObjectParams);
			FCreateMeshObjectResult Result = UE::Modeling::CreateMeshObject(GetToolManager(), MoveTemp(NewMeshObjectParams));
			if (Result.IsOK() && Result.NewActor != nullptr)
			{
				OutputSelection.Add(Result.NewActor);
			}
		};

		int32 NumParts = CurrentMeshParts.Num();
		if (Settings->bOutputSeparateMeshes && NumParts > 1)
		{
			for ( int32 k = 0; k < NumParts; ++k)
			{
				FDynamicMesh3& MeshPart = CurrentMeshParts[k];
				FAxisAlignedBox3d Bounds = MeshPart.GetBounds();
				MeshTransforms::Translate(MeshPart, -Bounds.Center());
				FTransform3d CenterTransform = ActorTransform;
				CenterTransform.SetTranslation(CenterTransform.GetTranslation() + Bounds.Center());
				FString NewName = FString::Printf(TEXT("%s_Collision%d"), *TargetName, k);
				EmitNewMesh(MoveTemp(MeshPart), CenterTransform, NewName);
			}
		}
		else
		{
			FString NewName = FString::Printf(TEXT("%s_Collision"), *TargetName);
			EmitNewMesh(MoveTemp(CurrentMesh), ActorTransform, NewName);
		}

		ToolSelectionUtil::SetNewActorSelection(GetToolManager(), OutputSelection);

		GetToolManager()->EndUndoTransaction();
	}
}


bool UExtractCollisionGeometryTool::CanAccept() const
{
	return Super::CanAccept() && CurrentMesh.TriangleCount() > 0;
}




void UExtractCollisionGeometryTool::OnTick(float DeltaTime)
{
	if (bResultValid == false)
	{
		GetToolManager()->DisplayMessage(FText(), EToolMessageLevel::UserWarning);
		if (Settings->CollisionType == EExtractCollisionOutputType::Simple)
		{
			RecalculateMesh_Simple();
		}
		else
		{
			RecalculateMesh_Complex();
		}
		bResultValid = true;
	}

	if (bVisualizationDirty)
	{
		UpdateVisualization();
		bVisualizationDirty = false;
	}
}



void UExtractCollisionGeometryTool::UpdateVisualization()
{
	float UseThickness = VizSettings->LineThickness;
	FColor UseColor = VizSettings->Color;
	PreviewElements->UpdateAllLineSets([&](ULineSetComponent* LineSet)
		{
			LineSet->SetAllLinesThickness(UseThickness);
			LineSet->SetAllLinesColor(UseColor);
		});
	
	UMaterialInterface* LineMaterial = ToolSetupUtil::GetDefaultLineComponentMaterial(GetToolManager(), !VizSettings->bShowHidden);
	PreviewElements->SetAllLineSetsMaterial(LineMaterial);
}



void UExtractCollisionGeometryTool::RecalculateMesh_Simple()
{
	int32 SphereResolution = 16;

	CurrentMesh = FDynamicMesh3(EMeshComponents::FaceGroups);
	CurrentMesh.EnableAttributes();

	CurrentMeshParts.Reset();

	FDynamicMeshEditor Editor(&CurrentMesh);

	const FKAggregateGeom& AggGeom = PhysicsInfo->AggGeom;

	for (const FKSphereElem& Sphere : AggGeom.SphereElems)
	{
		FSphereGenerator SphereGen;
		SphereGen.Radius = Sphere.Radius;
		SphereGen.NumPhi = SphereGen.NumTheta = SphereResolution;
		SphereGen.bPolygroupPerQuad = false;
		SphereGen.Generate();
		FDynamicMesh3 SphereMesh(&SphereGen);

		MeshTransforms::Translate(SphereMesh, FVector3d(Sphere.Center));

		FMeshIndexMappings Mappings;
		Editor.AppendMesh(&SphereMesh, Mappings);
		CurrentMeshParts.Add(MoveTemp(SphereMesh));
	}


	for (const FKBoxElem& Box : AggGeom.BoxElems)
	{
		FMinimalBoxMeshGenerator BoxGen;
		BoxGen.Box = FOrientedBox3d(
			FFrame3d(FVector3d(Box.Center), FQuaterniond(Box.Rotation.Quaternion())),
			0.5*FVector3d(Box.X, Box.Y, Box.Z));
		BoxGen.Generate();
		FDynamicMesh3 BoxMesh(&BoxGen);

		// transform not applied because it is just the Center/Rotation

		FMeshIndexMappings Mappings;
		Editor.AppendMesh(&BoxMesh, Mappings);
		CurrentMeshParts.Add(MoveTemp(BoxMesh));
	}


	for (const FKSphylElem& Capsule: AggGeom.SphylElems)
	{
		FCapsuleGenerator CapsuleGen;
		CapsuleGen.Radius = Capsule.Radius;
		CapsuleGen.SegmentLength = Capsule.Length;
		CapsuleGen.NumHemisphereArcSteps = SphereResolution/4+1;
		CapsuleGen.NumCircleSteps = SphereResolution;
		CapsuleGen.bPolygroupPerQuad = false;
		CapsuleGen.Generate();
		FDynamicMesh3 CapsuleMesh(&CapsuleGen);

		MeshTransforms::Translate(CapsuleMesh, FVector3d(0,0,-0.5*Capsule.Length) );

		UE::Geometry::FTransform3d Transform(Capsule.GetTransform());
		MeshTransforms::ApplyTransform(CapsuleMesh, Transform);

		FMeshIndexMappings Mappings;
		Editor.AppendMesh(&CapsuleMesh, Mappings);
		CurrentMeshParts.Add(MoveTemp(CapsuleMesh));
	}


	for (const FKConvexElem& Convex : AggGeom.ConvexElems)
	{
		UE::Geometry::FTransform3d ElemTransform(Convex.GetTransform());
		FDynamicMesh3 ConvexMesh(EMeshComponents::None);
		int32 NumVertices = Convex.VertexData.Num();
		for (int32 k = 0; k < NumVertices; ++k)
		{
			ConvexMesh.AppendVertex( FVector3d(Convex.VertexData[k]) );
		}
		int32 NumTriangles = Convex.IndexData.Num() / 3;
		for (int32 k = 0; k < NumTriangles; ++k)
		{
			ConvexMesh.AppendTriangle(Convex.IndexData[3*k], Convex.IndexData[3*k+1], Convex.IndexData[3*k+2]);
		}

		ConvexMesh.ReverseOrientation();
		ConvexMesh.EnableTriangleGroups(0);
		ConvexMesh.EnableAttributes();
		FDynamicMeshUVEditor UVEditor(&ConvexMesh, 0, true);
		UVEditor.SetPerTriangleUVs();
		FMeshIndexMappings Mappings;
		Editor.AppendMesh(&ConvexMesh, Mappings);
		CurrentMeshParts.Add(MoveTemp(ConvexMesh));
	}

	for (FDynamicMesh3& MeshPart : CurrentMeshParts)
	{
		FMeshNormals::InitializeMeshToPerTriangleNormals(&MeshPart);
	}
	FMeshNormals::InitializeMeshToPerTriangleNormals(&CurrentMesh);

	PreviewMesh->UpdatePreview(&CurrentMesh);

	if (CurrentMeshParts.Num() == 0)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("NoSimpleCollisionShapes", "This Mesh has no Simple Collision Shapes"), EToolMessageLevel::UserWarning);
	}
}



void UExtractCollisionGeometryTool::RecalculateMesh_Complex()
{
	CurrentMesh = FDynamicMesh3(EMeshComponents::FaceGroups);
	CurrentMesh.EnableAttributes();
	CurrentMeshParts.Reset();

	bool bMeshErrors = false;

	UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(UE::ToolTarget::GetTargetComponent(Target));
	TObjectPtr<UStaticMesh> StaticMesh = (StaticMeshComponent) ? StaticMeshComponent->GetStaticMesh() : nullptr;
	IInterface_CollisionDataProvider* CollisionProvider = (StaticMesh) ? Cast<IInterface_CollisionDataProvider>(StaticMesh) : nullptr;
	if (CollisionProvider && CollisionProvider->ContainsPhysicsTriMeshData(true))
	{
		FTriMeshCollisionData CollisionData;
		if (CollisionProvider->GetPhysicsTriMeshData(&CollisionData, true))
		{
			TArray<int32> VertexIDMap;
			for (int32 k = 0; k < CollisionData.Vertices.Num(); ++k)
			{
				int32 vid = CurrentMesh.AppendVertex(CollisionData.Vertices[k]);
				VertexIDMap.Add(vid);
			}
			for (const FTriIndices& TriIndices : CollisionData.Indices)
			{
				FIndex3i Triangle(TriIndices.v0, TriIndices.v1, TriIndices.v2);
				int32 tid = CurrentMesh.AppendTriangle(Triangle);
				if (tid < 0)
				{
					bMeshErrors = true;
				}
			}

			if (Settings->bWeldEdges)
			{
				FMergeCoincidentMeshEdges Weld(&CurrentMesh);
				Weld.OnlyUniquePairs = true;
				Weld.Apply();
				Weld.OnlyUniquePairs = false;
				Weld.Apply();
			}

			if (!CollisionData.bFlipNormals)		// collision mesh has reversed orientation
			{
				CurrentMesh.ReverseOrientation(false);
			}

			FMeshNormals::InitializeMeshToPerTriangleNormals(&CurrentMesh);
		}
	}

	PreviewMesh->UpdatePreview(&CurrentMesh);

	if (CurrentMesh.TriangleCount() == 0)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("EmptyComplexCollision", "This Mesh has no Complex Collision geometry"), EToolMessageLevel::UserWarning);
	}
}

#undef LOCTEXT_NAMESPACE