// Copyright Epic Games, Inc. All Rights Reserved.

#include "Physics/SetCollisionGeometryTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"
#include "ToolSetupUtil.h"
#include "Drawing/PreviewGeometryActor.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "MeshTransforms.h"
#include "DynamicMeshEditor.h"
#include "Selections/MeshConnectedComponents.h"
#include "DynamicSubmesh3.h"

#include "ShapeApproximation/ShapeDetection3.h"
#include "ShapeApproximation/MeshSimpleShapeApproximation.h"

#include "Physics/PhysicsDataCollection.h"
#include "Physics/CollisionGeometryVisualization.h"

// physics data
#include "Engine/Classes/Engine/StaticMesh.h"
#include "Engine/Classes/Components/StaticMeshComponent.h"
#include "Engine/Classes/PhysicsEngine/BodySetup.h"

#include "Async/ParallelFor.h"

#define LOCTEXT_NAMESPACE "USetCollisionGeometryTool"



bool USetCollisionGeometryToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	TArray<UActorComponent*> Components = ToolBuilderUtil::FindAllComponents(SceneState, CanMakeComponentTarget);
	return (Components.Num() > 0 && Cast<UStaticMeshComponent>(Components[Components.Num() - 1]) != nullptr);
}


UInteractiveTool* USetCollisionGeometryToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	USetCollisionGeometryTool* NewTool = NewObject<USetCollisionGeometryTool>(SceneState.ToolManager);
	TArray<UActorComponent*> Components = ToolBuilderUtil::FindAllComponents(SceneState, CanMakeComponentTarget);
	TArray<TUniquePtr<FPrimitiveComponentTarget>> ComponentTargets;
	for (UActorComponent* ActorComponent : Components)
	{
		auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
		if (MeshComponent)
		{
			ComponentTargets.Add(MakeComponentTarget(MeshComponent));
		}
	}
	NewTool->SetSelection(MoveTemp(ComponentTargets));
	return NewTool;
}


void USetCollisionGeometryTool::Setup()
{
	UInteractiveTool::Setup();

	// if we have one selection, use it as the source, otherwise use all but hte last selected mesh
	bSourcesHidden = (ComponentTargets.Num() > 1);
	if (ComponentTargets.Num() == 1)
	{
		SourceObjectIndices.Add(0);
	}
	else
	{
		for (int32 k = 0; k < ComponentTargets.Num() -1; ++k)
		{
			SourceObjectIndices.Add(k);
			ComponentTargets[k]->SetOwnerVisibility(false);
		}
	}

	bInputMeshesValid = true;

	TUniquePtr<FPrimitiveComponentTarget>& CollisionTarget = ComponentTargets[ComponentTargets.Num() - 1];
	PreviewGeom = NewObject<UPreviewGeometry>(this);
	FTransform PreviewTransform = CollisionTarget->GetWorldTransform();
	OrigTargetTransform = PreviewTransform;
	TargetScale3D = PreviewTransform.GetScale3D();
	PreviewTransform.SetScale3D(FVector::OneVector);
	PreviewGeom->CreateInWorld(CollisionTarget->GetOwnerActor()->GetWorld(), PreviewTransform);

	// initialize initial collision object
	InitialCollision = MakeShared<FPhysicsDataCollection>();
	InitialCollision->InitializeFromComponent(CollisionTarget->GetOwnerComponent(), true);
	InitialCollision->ExternalScale3D = TargetScale3D;

	// create tool options
	Settings = NewObject<USetCollisionGeometryToolProperties>(this);
	Settings->RestoreProperties(this);
	AddToolPropertySource(Settings);
	Settings->bUseWorldSpace = (SourceObjectIndices.Num() > 1);
	Settings->WatchProperty(Settings->InputMode, [this](ESetCollisionGeometryInputMode) { bResultValid = false; });
	Settings->WatchProperty(Settings->GeometryType, [this](ECollisionGeometryType) { bResultValid = false; });
	Settings->WatchProperty(Settings->bUseWorldSpace, [this](bool) { bInputMeshesValid = false; });
	Settings->WatchProperty(Settings->bAppendToExisting, [this](bool) { bResultValid = false; });
	Settings->WatchProperty(Settings->bRemoveContained, [this](bool) { bResultValid = false; });
	Settings->WatchProperty(Settings->bEnableMaxCount, [this](bool) { bResultValid = false; });
	Settings->WatchProperty(Settings->MaxCount, [this](int32) { bResultValid = false; });
	Settings->WatchProperty(Settings->MinThickness, [this](float) { bResultValid = false; });
	Settings->WatchProperty(Settings->bDetectBoxes, [this](int32) { bResultValid = false; });
	Settings->WatchProperty(Settings->bDetectSpheres, [this](int32) { bResultValid = false; });
	Settings->WatchProperty(Settings->bDetectCapsules, [this](int32) { bResultValid = false; });
	Settings->WatchProperty(Settings->bSimplifyHulls, [this](bool) { bResultValid = false; });
	Settings->WatchProperty(Settings->HullTargetFaceCount, [this](int32) { bResultValid = false; });
	Settings->WatchProperty(Settings->bSimplifyPolygons, [this](bool) { bResultValid = false; });
	Settings->WatchProperty(Settings->HullTolerance, [this](float) { bResultValid = false; });
	Settings->WatchProperty(Settings->SweepAxis, [this](EProjectedHullAxis) { bResultValid = false; });
		
	bResultValid = false;

	VizSettings = NewObject<UCollisionGeometryVisualizationProperties>(this);
	VizSettings->RestoreProperties(this);
	AddToolPropertySource(VizSettings);
	VizSettings->WatchProperty(VizSettings->LineThickness, [this](float NewValue) { bVisualizationDirty = true; });
	VizSettings->WatchProperty(VizSettings->Color, [this](FColor NewValue) { bVisualizationDirty = true; });
	VizSettings->WatchProperty(VizSettings->bShowHidden, [this](bool bNewValue) { bVisualizationDirty = true; });

	// add option for collision properties
	CollisionProps = NewObject<UPhysicsObjectToolPropertySet>(this);
	AddToolPropertySource(CollisionProps);

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool", "Initialize Simple Collision geometry for a Mesh from one or more input Meshes (including itself)."),
		EToolMessageLevel::UserNotification);
}


void USetCollisionGeometryTool::Shutdown(EToolShutdownType ShutdownType)
{
	VizSettings->SaveProperties(this);
	Settings->SaveProperties(this);
	PreviewGeom->Disconnect();

	// show hidden sources
	if (bSourcesHidden)
	{
		for (int32 k : SourceObjectIndices)
		{
			ComponentTargets[k]->SetOwnerVisibility(true);
		}
	}

	if (ShutdownType == EToolShutdownType::Accept)
	{
		// Make sure rendering is done so that we are not changing data being used by collision drawing.
		FlushRenderingCommands();

		GetToolManager()->BeginUndoTransaction(LOCTEXT("UpdateCollision", "Update Collision"));

		// code below derived from FStaticMeshEditor::DuplicateSelectedPrims(), FStaticMeshEditor::OnCollisionSphere(), and GeomFitUtils.cpp::GenerateSphylAsSimpleCollision()

		TUniquePtr<FPrimitiveComponentTarget>& CollisionTarget = ComponentTargets[ComponentTargets.Num() - 1];
		UStaticMeshComponent* StaticMeshComponent = CastChecked<UStaticMeshComponent>(CollisionTarget->GetOwnerComponent());
		UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
		UBodySetup* BodySetup = StaticMesh->GetBodySetup();

		// mark the BodySetup for modification. Do we need to modify the UStaticMesh??
		BodySetup->Modify();

		// clear existing simple collision. This will call BodySetup->InvalidatePhysicsData()
		BodySetup->RemoveSimpleCollision();

		// set new collision geometry
		BodySetup->AggGeom = GeneratedCollision->AggGeom;

		// update collision type
		BodySetup->CollisionTraceFlag = (ECollisionTraceFlag)(int32)Settings->SetCollisionType;

		// rebuild physics meshes
		BodySetup->CreatePhysicsMeshes();

		// rebuild nav collision (? StaticMeshEditor does this)
		StaticMesh->CreateNavCollision(/*bIsUpdate=*/true);

		// update physics state on all components using this StaticMesh
		for (FThreadSafeObjectIterator Iter(UStaticMeshComponent::StaticClass()); Iter; ++Iter)
		{
			UStaticMeshComponent* SMComponent = Cast<UStaticMeshComponent>(*Iter);
			if (SMComponent->GetStaticMesh() == StaticMesh)
			{
				if (SMComponent->IsPhysicsStateCreated())
				{
					SMComponent->RecreatePhysicsState();
				}
			}
		}

		// do we need to do a post edit change here??

		// mark static mesh as dirty so it gets resaved?
		StaticMesh->MarkPackageDirty();

#if WITH_EDITORONLY_DATA
		// mark the static mesh as having customized collision so it is not regenerated on reimport
		StaticMesh->bCustomizedCollision = true;
#endif // WITH_EDITORONLY_DATA

		// post the undo transaction
		GetToolManager()->EndUndoTransaction();
	}

}




void USetCollisionGeometryTool::OnTick(float DeltaTime)
{
	if (bInputMeshesValid == false)
	{
		PrecomputeInputMeshes();
		bInputMeshesValid = true;
		bResultValid = false;
	}

	if (bResultValid == false)
	{
		UpdateGeneratedCollision();
		bResultValid = true;
	}

	if (bVisualizationDirty)
	{
		UpdateVisualization();
		bVisualizationDirty = false;
	}
}


void USetCollisionGeometryTool::UpdateVisualization()
{
	float UseThickness = VizSettings->LineThickness;
	FColor UseColor = VizSettings->Color;
	PreviewGeom->UpdateAllLineSets([&](ULineSetComponent* LineSet)
	{
		LineSet->SetAllLinesThickness(UseThickness);
		LineSet->SetAllLinesColor(UseColor);
	});

	LineMaterial = ToolSetupUtil::GetDefaultLineComponentMaterial(GetToolManager(), !VizSettings->bShowHidden);
	PreviewGeom->SetAllLineSetsMaterial(LineMaterial);
}


void USetCollisionGeometryTool::UpdateGeneratedCollision()
{
	// calculate new collision
	ECollisionGeometryType ComputeType = Settings->GeometryType;


	TSharedPtr<FPhysicsDataCollection> NewCollision = MakeShared<FPhysicsDataCollection>();
	NewCollision->InitializeFromExisting(*InitialCollision);
	if (Settings->bAppendToExisting || ComputeType == ECollisionGeometryType::KeepExisting)
	{
		NewCollision->CopyGeometryFromExisting(*InitialCollision);
	}

	TSharedPtr<FMeshSimpleShapeApproximation> UseShapeGenerator = GetApproximator(Settings->InputMode);

	UseShapeGenerator->bDetectSpheres = Settings->bDetectSpheres;
	UseShapeGenerator->bDetectBoxes = Settings->bDetectBoxes;
	UseShapeGenerator->bDetectCapsules = Settings->bDetectCapsules;
	//UseShapeGenerator->bDetectConvexes = Settings->bDetectConvexes;

	UseShapeGenerator->MinDimension = Settings->MinThickness;

	switch (ComputeType)
	{
	case ECollisionGeometryType::KeepExisting:
	case ECollisionGeometryType::None:
		break;
	case ECollisionGeometryType::AlignedBoxes:
		UseShapeGenerator->Generate_AlignedBoxes(NewCollision->Geometry);
		break;
	case ECollisionGeometryType::OrientedBoxes:
		UseShapeGenerator->Generate_OrientedBoxes(NewCollision->Geometry);
		break;
	case ECollisionGeometryType::MinimalSpheres:
		UseShapeGenerator->Generate_MinimalSpheres(NewCollision->Geometry);
		break;
	case ECollisionGeometryType::Capsules:
		UseShapeGenerator->Generate_Capsules(NewCollision->Geometry);
		break;
	case ECollisionGeometryType::ConvexHulls:
		UseShapeGenerator->bSimplifyHulls = Settings->bSimplifyHulls;
		UseShapeGenerator->HullTargetFaceCount = Settings->HullTargetFaceCount;
		UseShapeGenerator->Generate_ConvexHulls(NewCollision->Geometry);
		break;
	case ECollisionGeometryType::SweptHulls:
		UseShapeGenerator->bSimplifyHulls = Settings->bSimplifyPolygons;
		UseShapeGenerator->HullSimplifyTolerance = Settings->HullTolerance;
		UseShapeGenerator->Generate_ProjectedHulls(NewCollision->Geometry, 
			(FMeshSimpleShapeApproximation::EProjectedHullAxisMode)(int32)Settings->SweepAxis);
		break;
	case ECollisionGeometryType::MinVolume:
		UseShapeGenerator->Generate_MinVolume(NewCollision->Geometry);
		break;
	}


	if (!NewCollision)
	{
		ensure(false);
		return;
	}
	GeneratedCollision = NewCollision;

	if (Settings->bRemoveContained)
	{
		GeneratedCollision->Geometry.RemoveContainedGeometry();
	}

	bool bUseMaxCount = (Settings->bEnableMaxCount);
	if (bUseMaxCount)
	{
		GeneratedCollision->Geometry.FilterByVolume(Settings->MaxCount);
	}

	GeneratedCollision->CopyGeometryToAggregate();

	// update visualization
	PreviewGeom->RemoveAllLineSets();
	UE::PhysicsTools::InitializePreviewGeometryLines(*GeneratedCollision, PreviewGeom,
		VizSettings->Color, VizSettings->LineThickness, 0.0f, 16);

	// update property set
	CollisionProps->Reset();
	UE::PhysicsTools::InitializePhysicsToolObjectPropertySet(GeneratedCollision.Get(), CollisionProps);
}





void USetCollisionGeometryTool::InitializeDerivedMeshSet(
	const TArray<TSharedPtr<FDynamicMesh3>>& FromInputMeshes,
	TArray<TSharedPtr<FDynamicMesh3>>& ToMeshes,
	TFunctionRef<bool(const FDynamicMesh3* Mesh, int32 Tri0, int32 Tri1)> TrisConnectedPredicate)
{
	// find connected-components on input meshes, under given connectivity predicate
	TArray<TUniquePtr<FMeshConnectedComponents>> ComponentSets;
	ComponentSets.SetNum(FromInputMeshes.Num());
	ParallelFor(FromInputMeshes.Num(), [&](int32 k)
	{
		const FDynamicMesh3* Mesh = FromInputMeshes[k].Get();
		ComponentSets[k] = MakeUnique<FMeshConnectedComponents>(Mesh);
		ComponentSets[k]->FindConnectedTriangles(
			[Mesh, &TrisConnectedPredicate](int32 Tri0, int32 Tri1) 
			{ 
				return TrisConnectedPredicate(Mesh, Tri0, Tri1); 
			}
		);
	});

	// Assemble a list of all the submeshes we want to compute, so we can do them all in parallel
	struct FSubmeshSource
	{
		const FDynamicMesh3* SourceMesh;
		FIndex2i ComponentIdx;
	};
	TArray<FSubmeshSource> AllSubmeshes;
	for (int32 k = 0; k < FromInputMeshes.Num(); ++k)
	{
		const FDynamicMesh3* Mesh = FromInputMeshes[k].Get();
		int32 NumComponents = ComponentSets[k]->Num();
		for ( int32 j = 0; j < NumComponents; ++j )
		{
			const FMeshConnectedComponents::FComponent& Component = ComponentSets[k]->GetComponent(j);
			if (Component.Indices.Num() > 1)		// ignore single triangles
			{
				AllSubmeshes.Add(FSubmeshSource{ Mesh, FIndex2i(k,j) });
			}
		}
	}


	// compute all the submeshes
	ToMeshes.Reset();
	ToMeshes.SetNum(AllSubmeshes.Num());
	ParallelFor(AllSubmeshes.Num(), [&](int32 k)
	{
		const FSubmeshSource& Source = AllSubmeshes[k];
		const FMeshConnectedComponents::FComponent& Component = ComponentSets[Source.ComponentIdx.A]->GetComponent(Source.ComponentIdx.B);
		FDynamicSubmesh3 Submesh(Source.SourceMesh, Component.Indices, (int32)EMeshComponents::None, false);
		ToMeshes[k] = MakeShared<FDynamicMesh3>( MoveTemp(Submesh.GetSubmesh()) );
	});
}


template<typename T>
TArray<const T*> MakeRawPointerList(const TArray<TSharedPtr<T>>& InputList)
{
	TArray<const T*> Result;
	Result.Reserve(InputList.Num());
	for (const TSharedPtr<T>& Ptr : InputList)
	{
		Result.Add(Ptr.Get());
	}
	return MoveTemp(Result);
}


void USetCollisionGeometryTool::PrecomputeInputMeshes()
{
	TUniquePtr<FPrimitiveComponentTarget>& CollisionTarget = ComponentTargets[ComponentTargets.Num()-1];
	FTransform3d TargetTransform(CollisionTarget->GetWorldTransform());
	FTransform3d TargetTransformInv = TargetTransform.Inverse();

	InputMeshes.Reset();
	InputMeshes.SetNum(SourceObjectIndices.Num());
	ParallelFor(SourceObjectIndices.Num(), [&](int32 k)
	{
		FMeshDescriptionToDynamicMesh Converter;
		Converter.bCalculateMaps = false;
		Converter.bDisableAttributes = true;
		FDynamicMesh3 SourceMesh;
		Converter.Convert(ComponentTargets[k]->GetMesh(), SourceMesh);
		if (Settings->bUseWorldSpace)
		{
			FTransform3d ToWorld(ComponentTargets[k]->GetWorldTransform());
			MeshTransforms::ApplyTransform(SourceMesh, ToWorld);
			MeshTransforms::ApplyTransform(SourceMesh, TargetTransformInv);
		}
		SourceMesh.DiscardAttributes();
		InputMeshes[k] = MakeShared<FDynamicMesh3>(MoveTemp(SourceMesh));
	});
	InputMeshesApproximator = MakeShared<FMeshSimpleShapeApproximation>();
	InputMeshesApproximator->InitializeSourceMeshes(MakeRawPointerList<FDynamicMesh3>(InputMeshes));


	// build combined input
	CombinedInputMeshes.Reset();
	FDynamicMesh3 CombinedMesh;
	CombinedMesh.EnableTriangleGroups();
	FDynamicMeshEditor Appender(&CombinedMesh);
	FMeshIndexMappings TmpMappings;
	for (const TSharedPtr<FDynamicMesh3>& InputMesh : InputMeshes)
	{
		TmpMappings.Reset();
		Appender.AppendMesh(InputMesh.Get(), TmpMappings);
	}
	CombinedInputMeshes.Add( MakeShared<FDynamicMesh3>(MoveTemp(CombinedMesh)) );
	CombinedInputMeshesApproximator = MakeShared<FMeshSimpleShapeApproximation>();
	CombinedInputMeshesApproximator->InitializeSourceMeshes(MakeRawPointerList<FDynamicMesh3>(CombinedInputMeshes));

	// build separated input meshes
	SeparatedInputMeshes.Reset();
	InitializeDerivedMeshSet(InputMeshes, SeparatedInputMeshes, 
		[&](const FDynamicMesh3* Mesh, int32 Tri0, int32 Tri1) { return true; });
	SeparatedMeshesApproximator = MakeShared<FMeshSimpleShapeApproximation>();
	SeparatedMeshesApproximator->InitializeSourceMeshes(MakeRawPointerList<FDynamicMesh3>(SeparatedInputMeshes));

	// build per-group input meshes
	PerGroupInputMeshes.Reset();
	InitializeDerivedMeshSet(InputMeshes, PerGroupInputMeshes,
		[&](const FDynamicMesh3* Mesh, int32 Tri0, int32 Tri1) { return Mesh->GetTriangleGroup(Tri0) == Mesh->GetTriangleGroup(Tri1); });
	PerGroupMeshesApproximator = MakeShared<FMeshSimpleShapeApproximation>();
	PerGroupMeshesApproximator->InitializeSourceMeshes(MakeRawPointerList<FDynamicMesh3>(PerGroupInputMeshes));

}


TSharedPtr<FMeshSimpleShapeApproximation>& USetCollisionGeometryTool::GetApproximator(ESetCollisionGeometryInputMode MeshSetMode)
{
	if (MeshSetMode == ESetCollisionGeometryInputMode::CombineAll)
	{
		return CombinedInputMeshesApproximator;
	}
	else if ( MeshSetMode == ESetCollisionGeometryInputMode::PerMeshComponent)
	{
		return SeparatedMeshesApproximator;
	}
	else if (MeshSetMode == ESetCollisionGeometryInputMode::PerMeshGroup)
	{
		return PerGroupMeshesApproximator;
	}
	else
	{
		return InputMeshesApproximator;
	}
}


#undef LOCTEXT_NAMESPACE