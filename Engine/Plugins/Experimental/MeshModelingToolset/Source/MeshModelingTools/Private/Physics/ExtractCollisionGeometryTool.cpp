// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/ExtractCollisionGeometryTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "ToolSetupUtil.h"
#include "AssetGenerationUtil.h"
#include "Selection/ToolSelectionUtil.h"
#include "Drawing/PreviewGeometryActor.h"
#include "Util/ColorConstants.h"
#include "PreviewMesh.h"

#include "DynamicMeshEditor.h"
#include "MeshNormals.h"
#include "Generators/SphereGenerator.h"
#include "Generators/MinimalBoxMeshGenerator.h"
#include "Generators/CapsuleGenerator.h"
#include "MeshTransforms.h"
#include "Parameterization/DynamicMeshUVEditor.h"

#include "Physics/PhysicsDataCollection.h"
#include "Physics/CollisionGeometryVisualization.h"

// physics data
#include "Engine/Classes/Engine/StaticMesh.h"
#include "Engine/Classes/Components/StaticMeshComponent.h"
#include "Engine/Classes/PhysicsEngine/BodySetup.h"
#include "Engine/Classes/PhysicsEngine/AggregateGeom.h"


#define LOCTEXT_NAMESPACE "UExtractCollisionGeometryTool"



bool UExtractCollisionGeometryToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	int32 NumStaticMeshes = ToolBuilderUtil::CountComponents(SceneState, [&](UActorComponent* Comp) { return Cast<UStaticMeshComponent>(Comp) != nullptr; });
	int32 NumComponentTargets = ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget);
	return (NumStaticMeshes == 1 && NumStaticMeshes == NumComponentTargets);
}


UInteractiveTool* UExtractCollisionGeometryToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UExtractCollisionGeometryTool* NewTool = NewObject<UExtractCollisionGeometryTool>(SceneState.ToolManager);
	NewTool->SetWorld(SceneState.World);
	check(AssetAPI);
	NewTool->SetAssetAPI(AssetAPI);

	TArray<UActorComponent*> ValidComponents = ToolBuilderUtil::FindAllComponents(SceneState,
		[&](UActorComponent* Comp) { return Cast<UStaticMeshComponent>(Comp) != nullptr; });
	check(ValidComponents.Num() == 1);

	TUniquePtr<FPrimitiveComponentTarget> ComponentTargets;
	UStaticMeshComponent* MeshComponent = Cast<UStaticMeshComponent>(ValidComponents[0]);
	NewTool->SetSelection(MakeComponentTarget(MeshComponent));
	return NewTool;
}


void UExtractCollisionGeometryTool::Setup()
{
	UInteractiveTool::Setup();

	// create preview mesh
	PreviewMesh = NewObject<UPreviewMesh>(this);
	PreviewMesh->bBuildSpatialDataStructure = false;
	PreviewMesh->CreateInWorld(TargetWorld, FTransform::Identity);
	PreviewMesh->SetTransform(ComponentTarget->GetWorldTransform());
	PreviewMesh->SetMaterial(ToolSetupUtil::GetDefaultSculptMaterial(GetToolManager()));
	PreviewMesh->SetOverrideRenderMaterial(ToolSetupUtil::GetSelectionMaterial(GetToolManager()));
	PreviewMesh->SetTriangleColorFunction([this](const FDynamicMesh3* Mesh, int TriangleID)
	{
		return LinearColors::SelectFColor(Mesh->GetTriangleGroup(TriangleID));
	});

	VizSettings = NewObject<UCollisionGeometryVisualizationProperties>(this);
	VizSettings->RestoreProperties(this);
	AddToolPropertySource(VizSettings);
	VizSettings->WatchProperty(VizSettings->LineThickness, [this](float NewValue) { bVisualizationDirty = true; });
	VizSettings->WatchProperty(VizSettings->Color, [this](FColor NewValue) { bVisualizationDirty = true; });


	const UStaticMeshComponent* Component = CastChecked<UStaticMeshComponent>(ComponentTarget->GetOwnerComponent());
	const UStaticMesh* StaticMesh = Component->GetStaticMesh();
	if (ensure(StaticMesh && StaticMesh->GetBodySetup()))
	{
		PhysicsInfo = MakeShared<FPhysicsDataCollection>();
		PhysicsInfo->InitializeFromComponent(Component, true);

		PreviewElements = NewObject<UPreviewGeometry>(this);
		FTransform TargetTransform = ComponentTarget->GetWorldTransform();
		//PhysicsInfo->ExternalScale3D = TargetTransform.GetScale3D();
		//TargetTransform.SetScale3D(FVector::OneVector);
		PreviewElements->CreateInWorld(ComponentTarget->GetOwnerActor()->GetWorld(), TargetTransform);
		
		UE::PhysicsTools::InitializePreviewGeometryLines(*PhysicsInfo, PreviewElements,
			VizSettings->Color, VizSettings->LineThickness, 0.0f, 16);

		ObjectProps = NewObject<UPhysicsObjectToolPropertySet>(this);
		UE::PhysicsTools::InitializePhysicsToolObjectPropertySet(PhysicsInfo.Get(), ObjectProps);
		AddToolPropertySource(ObjectProps);
	}


	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool", "Convert Collision geometry to Static Mesh"),
		EToolMessageLevel::UserNotification);
}


void UExtractCollisionGeometryTool::Shutdown(EToolShutdownType ShutdownType)
{
	VizSettings->SaveProperties(this);

	FTransform3d Transform(PreviewMesh->GetTransform());

	PreviewElements->Disconnect();
	PreviewMesh->SetVisible(false);
	PreviewMesh->Disconnect();
	PreviewMesh = nullptr;

	if (ShutdownType == EToolShutdownType::Accept)
	{
		UMaterialInterface* UseMaterial = UMaterial::GetDefaultMaterial(MD_Surface);

		FString NewName = ComponentTarget->IsValid() ?
			FString::Printf(TEXT("%s_Collision"), *ComponentTarget->GetOwnerComponent()->GetName()) : TEXT("CollisionGeo");

		GetToolManager()->BeginUndoTransaction(LOCTEXT("CreateCollisionMesh", "Collision To Mesh"));

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


bool UExtractCollisionGeometryTool::CanAccept() const
{
	return Super::CanAccept() && CurrentMesh.TriangleCount() > 0;
}




void UExtractCollisionGeometryTool::OnTick(float DeltaTime)
{
	if (bResultValid == false)
	{
		RecalculateMesh();
	}

	if (bVisualizationDirty)
	{
		UpdateVisualization();
		bVisualizationDirty = false;
	}
}



void UExtractCollisionGeometryTool::UpdateVisualization()
{
	float UseThickness = VizSettings->LineThickness / 10.0f;
	FColor UseColor = VizSettings->Color;
	PreviewElements->UpdateAllLineSets([&](ULineSetComponent* LineSet)
		{
			LineSet->SetAllLinesThickness(UseThickness);
			LineSet->SetAllLinesColor(UseColor);
		});
}



void UExtractCollisionGeometryTool::RecalculateMesh()
{
	int32 SphereResolution = 16;

	CurrentMesh = FDynamicMesh3(EMeshComponents::FaceGroups);
	CurrentMesh.EnableAttributes();

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

		FTransform3d Transform(Capsule.GetTransform());
		MeshTransforms::ApplyTransform(CapsuleMesh, Transform);

		FMeshIndexMappings Mappings;
		Editor.AppendMesh(&CapsuleMesh, Mappings);
	}


	for (const FKConvexElem& Convex : AggGeom.ConvexElems)
	{
		FTransform3d ElemTransform(Convex.GetTransform());
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
	}

	FMeshNormals::InitializeMeshToPerTriangleNormals(&CurrentMesh);
	PreviewMesh->UpdatePreview(&CurrentMesh);


	//PreviewGeom->CreateOrUpdateLineSet(TEXT("Capsules"), AggGeom.SphylElems.Num(), [&](int32 Index, TArray<FRenderableLine>& LinesOut)
	//{
	//	const FKSphylElem& Capsule = AggGeom.SphylElems[Index];
	//	FTransform ElemTransform = Capsule.GetTransform();
	//	ElemTransform.ScaleTranslation(PhysicsData.ExternalScale3D);
	//	FTransform3f ElemTransformf(ElemTransform);
	//	const float HalfLength = Capsule.GetScaledCylinderLength(PhysicsData.ExternalScale3D) * .5f;
	//	const float Radius = Capsule.GetScaledRadius(PhysicsData.ExternalScale3D);
	//	FVector3f Top(0, 0, HalfLength), Bottom(0, 0, -HalfLength);

	//	// top and bottom circles
	//	UE::Geometry::GenerateCircleSegments<float>(CircleSteps, Radius, Top, FVector3f::UnitX(), FVector3f::UnitY(), ElemTransformf,
	//		[&](const FVector3f& A, const FVector3f& B) { LinesOut.Add(FRenderableLine((FVector)A, (FVector)B, CapsuleColor, LineThickness, DepthBias)); });
	//	UE::Geometry::GenerateCircleSegments<float>(CircleSteps, Radius, Bottom, FVector3f::UnitX(), FVector3f::UnitY(), ElemTransformf,
	//		[&](const FVector3f& A, const FVector3f& B) { LinesOut.Add(FRenderableLine((FVector)A, (FVector)B, CapsuleColor, LineThickness, DepthBias)); });

	//	// top dome
	//	UE::Geometry::GenerateArcSegments<float>(CircleSteps, Radius, 0.0, PI, Top, FVector3f::UnitY(), FVector3f::UnitZ(), ElemTransformf,
	//		[&](const FVector3f& A, const FVector3f& B) { LinesOut.Add(FRenderableLine((FVector)A, (FVector)B, CapsuleColor, LineThickness, DepthBias)); });
	//	UE::Geometry::GenerateArcSegments<float>(CircleSteps, Radius, 0.0, PI, Top, FVector3f::UnitX(), FVector3f::UnitZ(), ElemTransformf,
	//		[&](const FVector3f& A, const FVector3f& B) { LinesOut.Add(FRenderableLine((FVector)A, (FVector)B, CapsuleColor, LineThickness, DepthBias)); });

	//	// bottom dome
	//	UE::Geometry::GenerateArcSegments<float>(CircleSteps, Radius, 0.0, -PI, Bottom, FVector3f::UnitY(), FVector3f::UnitZ(), ElemTransformf,
	//		[&](const FVector3f& A, const FVector3f& B) { LinesOut.Add(FRenderableLine((FVector)A, (FVector)B, CapsuleColor, LineThickness, DepthBias)); });
	//	UE::Geometry::GenerateArcSegments<float>(CircleSteps, Radius, 0.0, -PI, Bottom, FVector3f::UnitX(), FVector3f::UnitZ(), ElemTransformf,
	//		[&](const FVector3f& A, const FVector3f& B) { LinesOut.Add(FRenderableLine((FVector)A, (FVector)B, CapsuleColor, LineThickness, DepthBias)); });

	//	// connecting lines
	//	for (int k = 0; k < 2; ++k)
	//	{
	//		FVector DX = (k < 1) ? FVector(-Radius, 0, 0) : FVector(Radius, 0, 0);
	//		LinesOut.Add(FRenderableLine(
	//			ElemTransform.TransformPosition((FVector)Top + DX),
	//			ElemTransform.TransformPosition((FVector)Bottom + DX), CapsuleColor, LineThickness, DepthBias));
	//		FVector DY = (k < 1) ? FVector(0, -Radius, 0) : FVector(0, Radius, 0);
	//		LinesOut.Add(FRenderableLine(
	//			ElemTransform.TransformPosition((FVector)Top + DY),
	//			ElemTransform.TransformPosition((FVector)Bottom + DY), CapsuleColor, LineThickness, DepthBias));
	//	}
	//});
}





#undef LOCTEXT_NAMESPACE