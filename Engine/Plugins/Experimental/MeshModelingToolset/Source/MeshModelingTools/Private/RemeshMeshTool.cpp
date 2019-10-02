// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "RemeshMeshTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"
#include "Remesher.h"
#include "MeshConstraintsUtil.h"
#include "ProjectionTargets.h"
#include "MeshNormals.h"

#include "SimpleDynamicMeshComponent.h"

#include "SceneManagement.h" // for FPrimitiveDrawInterface
#include "Async/ParallelFor.h"

#define LOCTEXT_NAMESPACE "URemeshMeshTool"


/*
 * ToolBuilder
 */


bool URemeshMeshToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}

UInteractiveTool* URemeshMeshToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	URemeshMeshTool* NewTool = NewObject<URemeshMeshTool>(SceneState.ToolManager, "Remesh Tool");

	UActorComponent* ActorComponent = ToolBuilderUtil::FindFirstComponent(SceneState, CanMakeComponentTarget);
	auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
	check(MeshComponent != nullptr);
	NewTool->SetSelection(MakeComponentTarget(MeshComponent));

	return NewTool;
}



/*
 * Tool
 */

URemeshMeshToolProperties::URemeshMeshToolProperties()
{
	TargetTriangleCount = 5000;
	SmoothingSpeed = 0.25;
	RemeshIterations = 20;
	bDiscardAttributes = false;
	SmoothingType = ERemeshSmoothingType::MeanValue;
	bPreserveSharpEdges = true;

	TargetEdgeLength = 5.0;
	bFlips = true;
	bSplits = true;
	bCollapses = true;
	bReproject = true;
	bPreventNormalFlips = true;
	bUseTargetEdgeLength = false;
}



URemeshMeshTool::URemeshMeshTool()
{
}


void URemeshMeshTool::Setup()
{
	UInteractiveTool::Setup();

	// create dynamic mesh component to use for live preview
	DynamicMeshComponent = NewObject<USimpleDynamicMeshComponent>(ComponentTarget->GetOwnerActor(), "DynamicMesh");
	DynamicMeshComponent->SetupAttachment(ComponentTarget->GetOwnerActor()->GetRootComponent());
	DynamicMeshComponent->RegisterComponent();
	DynamicMeshComponent->SetWorldTransform(ComponentTarget->GetWorldTransform());

	// copy material if there is one
	auto Material = ComponentTarget->GetMaterial(0);
	if (Material != nullptr)
	{
		DynamicMeshComponent->SetMaterial(0, Material);
	}
	DynamicMeshComponent->bExplicitShowWireframe = true;

	DynamicMeshComponent->InitializeMesh(ComponentTarget->GetMesh());
	OriginalMesh.Copy(*DynamicMeshComponent->GetMesh());
	OriginalMeshSpatial.SetMesh(&OriginalMesh, true);

	// hide input StaticMeshComponent
	ComponentTarget->SetOwnerVisibility(false);

	// calculate initial mesh area (no utility fn yet)
	InitialMeshArea = 0;
	for (int tid : OriginalMesh.TriangleIndicesItr())
	{
		InitialMeshArea += OriginalMesh.GetTriArea(tid);
	}

	BasicProperties = NewObject<URemeshMeshToolProperties>(this);
	// arbitrary threshold of 5000 tris seems reasonable? 
	BasicProperties->TargetTriangleCount = (OriginalMesh.TriangleCount() < 5000) ? 5000 : OriginalMesh.TriangleCount();
	BasicProperties->TargetEdgeLength = CalculateTargetEdgeLength(BasicProperties->TargetTriangleCount);

	// initialize our properties
	AddToolPropertySource(BasicProperties);

	MeshStatisticsProperties = NewObject<UMeshStatisticsProperties>(this);
	AddToolPropertySource(MeshStatisticsProperties);
	MeshStatisticsProperties->Update(*DynamicMeshComponent->GetMesh());

	bResultValid = false;
}


void URemeshMeshTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (DynamicMeshComponent != nullptr)
	{
		//DynamicMeshComponent->OnMeshChanged.Remove(OnDynamicMeshComponentChangedHandle);

		ComponentTarget->SetOwnerVisibility(true);

		if (ShutdownType == EToolShutdownType::Accept)
		{
			// this block bakes the modified DynamicMeshComponent back into the StaticMeshComponent inside an undo transaction
			GetToolManager()->BeginUndoTransaction(LOCTEXT("RemeshMeshToolTransactionName", "Remesh Mesh"));
			ComponentTarget->CommitMesh([=](FMeshDescription* MeshDescription)
			{
				DynamicMeshComponent->Bake(MeshDescription, true);
			});
			GetToolManager()->EndUndoTransaction();
		}

		DynamicMeshComponent->UnregisterComponent();
		DynamicMeshComponent->DestroyComponent();
		DynamicMeshComponent = nullptr;
	}
}


void URemeshMeshTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	UpdateResult();


	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	FTransform Transform = ComponentTarget->GetWorldTransform(); //Actor->GetTransform();

	FColor LineColor(255, 0, 0);
	FDynamicMesh3* TargetMesh = DynamicMeshComponent->GetMesh();
	if (TargetMesh->HasAttributes())
	{
		FDynamicMeshUVOverlay* UVOverlay = TargetMesh->Attributes()->PrimaryUV();
		for (int eid : TargetMesh->EdgeIndicesItr()) 
		{
			if (UVOverlay->IsSeamEdge(eid)) 
			{
				FVector3d A, B;
				TargetMesh->GetEdgeV(eid, A, B);
				PDI->DrawLine(Transform.TransformPosition(A), Transform.TransformPosition(B),
					LineColor, 0, 2.0, 1.0f, true);
			}
		}
	}
}


void URemeshMeshTool::OnPropertyModified(UObject* PropertySet, UProperty* Property)
{
	bResultValid = false;
}


double URemeshMeshTool::CalculateTargetEdgeLength(int TargetTriCount)
{
	double TargetTriArea = InitialMeshArea / (double)TargetTriCount;
	double EdgeLen = TriangleUtil::EquilateralEdgeLengthForArea(TargetTriArea);
	return (double)FMath::RoundToInt(EdgeLen*100.0) / 100.0;
}



void URemeshMeshTool::UpdateResult()
{
	if (bResultValid) 
	{
		return;
	}

	FDynamicMesh3* TargetMesh = DynamicMeshComponent->GetMesh();
	TargetMesh->Copy(OriginalMesh);

	if (BasicProperties->bDiscardAttributes && BasicProperties->bPreserveSharpEdges == false)
	{
		TargetMesh->DiscardAttributes();
	}

	FRemesher Remesher(TargetMesh);
	Remesher.bEnableSplits = BasicProperties->bSplits;
	Remesher.bEnableFlips = BasicProperties->bFlips;
	Remesher.bEnableCollapses = BasicProperties->bCollapses;

	// recalculate target edge length
	if (BasicProperties->bUseTargetEdgeLength == false)
	{
		BasicProperties->TargetEdgeLength = CalculateTargetEdgeLength(BasicProperties->TargetTriangleCount);
	}

	Remesher.SetTargetEdgeLength(BasicProperties->TargetEdgeLength);

	Remesher.ProjectionMode = (BasicProperties->bReproject) ? 
		FRemesher::ETargetProjectionMode::AfterRefinement : FRemesher::ETargetProjectionMode::NoProjection;

	Remesher.bEnableSmoothing = (BasicProperties->SmoothingSpeed > 0);
	Remesher.SmoothSpeedT = BasicProperties->SmoothingSpeed;
	//Remesher.SmoothType = FRemesher::ESmoothTypes::MeanValue;
	Remesher.SmoothType = (BasicProperties->bDiscardAttributes) ?
		FRemesher::ESmoothTypes::Uniform :
		(FRemesher::ESmoothTypes)(int)BasicProperties->SmoothingType;
	bool bIsUniformSmooth = (Remesher.SmoothType == FRemesher::ESmoothTypes::Uniform);

	Remesher.bPreventNormalFlips = BasicProperties->bPreventNormalFlips;

	Remesher.DEBUG_CHECK_LEVEL = 0;

	FMeshConstraints constraints;
	FMeshConstraintsUtil::ConstrainAllSeams(constraints, *TargetMesh, true, !BasicProperties->bPreserveSharpEdges);
	Remesher.SetExternalConstraints(&constraints);

	FMeshProjectionTarget ProjTarget(&OriginalMesh, &OriginalMeshSpatial);
	Remesher.SetProjectionTarget(&ProjTarget);

	if (BasicProperties->bDiscardAttributes && BasicProperties->bPreserveSharpEdges == true)
	{
		TargetMesh->DiscardAttributes();
	}

	// run the remesh iterations
	for (int k = 0; k < BasicProperties->RemeshIterations; ++k)
	{
		// If we are not uniform smoothing, then flips seem to often make things worse.
		// Possibly this is because without the tangential flow, we won't get to the nice tris.
		// In this case we are better off basically not flipping, and just letting collapses resolve things
		// regular-valence polygons - things stay "stuck". 
		// @todo try implementing edge-length flip criteria instead of valence-flip
		if (bIsUniformSmooth == false)
		{
			bool bUseFlipsThisPass = (k % 2 == 0 && k < BasicProperties->RemeshIterations/2);
			Remesher.bEnableFlips = bUseFlipsThisPass && BasicProperties->bFlips;
		}

		Remesher.BasicRemeshPass();
	}

	if (!TargetMesh->HasAttributes() && !TargetMesh->HasVertexNormals())
	{
		FMeshNormals::QuickComputeVertexNormals(*TargetMesh);
	}

	DynamicMeshComponent->NotifyMeshUpdated();
	GetToolManager()->PostInvalidation();
	MeshStatisticsProperties->Update(*DynamicMeshComponent->GetMesh());

	bResultValid = true;
}

bool URemeshMeshTool::HasAccept() const
{
	return true;
}

bool URemeshMeshTool::CanAccept() const
{
	return true;
}




#undef LOCTEXT_NAMESPACE
