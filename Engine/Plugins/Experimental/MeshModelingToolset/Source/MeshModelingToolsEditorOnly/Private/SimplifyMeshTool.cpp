// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SimplifyMeshTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"
#include "MeshSimplification.h"
#include "MeshConstraintsUtil.h"
#include "ProjectionTargets.h"

#include "SimpleDynamicMeshComponent.h"

#include "SceneManagement.h" // for FPrimitiveDrawInterface

//#include "ProfilingDebugging/ScopedTimers.h" // enable this to use the timer.
#include "MeshDescription.h"
#include "MeshDescriptionOperations.h"
#include "OverlappingCorners.h"
#include "IMeshReductionManagerModule.h"
#include "IMeshReductionInterfaces.h"
#include "Modules/ModuleManager.h"
#include "Operations/MergeCoincidentMeshEdges.h"

#define LOCTEXT_NAMESPACE "USimplifyMeshTool"


DEFINE_LOG_CATEGORY_STATIC(LogMeshSimplification, Log, All);

/*
 * ToolBuilder
 */


bool USimplifyMeshToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}

UInteractiveTool* USimplifyMeshToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	USimplifyMeshTool* NewTool = NewObject<USimplifyMeshTool>(SceneState.ToolManager);

	UActorComponent* ActorComponent = ToolBuilderUtil::FindFirstComponent(SceneState, CanMakeComponentTarget);
	auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
	check(MeshComponent != nullptr);
	NewTool->SetSelection(MakeComponentTarget(MeshComponent));

	return NewTool;
}



/*
 * Tool
 */

USimplifyMeshToolProperties::USimplifyMeshToolProperties()
{
	SimplifierType = ESimplifyType::QEM;
	TargetMode = ESimplifyTargetType::Percentage;
	TargetPercentage = 50;
	TargetCount = 1000;
	TargetEdgeLength = 5.0;
	bReproject = false;
	bPreventNormalFlips = true;
	bDiscardAttributes = false;
}



void USimplifyMeshTool::Setup()
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

	// initialize our properties
	SimplifyProperties = NewObject<USimplifyMeshToolProperties>(this);
	AddToolPropertySource(SimplifyProperties);

	MeshStatisticsProperties = NewObject<UMeshStatisticsProperties>(this);
	AddToolPropertySource(MeshStatisticsProperties);
	MeshStatisticsProperties->Update(*DynamicMeshComponent->GetMesh());

	bResultValid = false;
}


void USimplifyMeshTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (DynamicMeshComponent != nullptr)
	{
		//DynamicMeshComponent->OnMeshChanged.Remove(OnDynamicMeshComponentChangedHandle);

		ComponentTarget->SetOwnerVisibility(true);

		if (ShutdownType == EToolShutdownType::Accept)
		{
			// this block bakes the modified DynamicMeshComponent back into the StaticMeshComponent inside an undo transaction
			GetToolManager()->BeginUndoTransaction(LOCTEXT("SimplifyMeshToolTransactionName", "Simplify Mesh"));
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


void USimplifyMeshTool::Render(IToolsContextRenderAPI* RenderAPI)
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


void USimplifyMeshTool::OnPropertyModified(UObject* PropertySet, UProperty* Property)
{
	bResultValid = false;
}


template <typename SimplificationType>
void ComputeResult(FDynamicMesh3* TargetMesh, const bool bReproject, int OriginalTriCount, FDynamicMesh3& OriginalMesh, FDynamicMeshAABBTree3& OriginalMeshSpatial, const ESimplifyTargetType TargetMode, const float TargetPercentage, const int TargetCount, const float TargetEdgeLength)
{
	SimplificationType Reducer(TargetMesh);

	Reducer.ProjectionMode = (bReproject) ? 
		SimplificationType::ETargetProjectionMode::AfterRefinement : SimplificationType::ETargetProjectionMode::NoProjection;

	Reducer.DEBUG_CHECK_LEVEL = 0;

	FMeshConstraints constraints;
	FMeshConstraintsUtil::ConstrainAllSeams(constraints, *TargetMesh, true, false);
	//FMeshConstraintsUtil::ConstrainAllSeamJunctions(constraints, *TargetMesh, true, false);
	Reducer.SetExternalConstraints(&constraints);

	FMeshProjectionTarget ProjTarget(&OriginalMesh, &OriginalMeshSpatial);
	Reducer.SetProjectionTarget(&ProjTarget);

	if (TargetMode == ESimplifyTargetType::Percentage)
	{
		double Ratio = (double)TargetPercentage / 100.0;
		int UseTarget = FMath::Max(4, (int)(Ratio * (double)OriginalTriCount));
		Reducer.SimplifyToTriangleCount(UseTarget);
	} 
	else if (TargetMode == ESimplifyTargetType::TriangleCount)
	{
		Reducer.SimplifyToTriangleCount(TargetCount);
	}
	else if (TargetMode == ESimplifyTargetType::EdgeLength)
	{
		Reducer.SimplifyToEdgeLength(TargetEdgeLength);
	}
}


void USimplifyMeshTool::UpdateResult()
{
	
	if (bResultValid) 
	{
		return;
	}

	//FScopedDurationTimeLogger Timer(TEXT("Simplification Time"));

	FDynamicMesh3* TargetMesh = DynamicMeshComponent->GetMesh();
	TargetMesh->Copy(OriginalMesh);
	int OriginalTriCount = OriginalMesh.TriangleCount();

	if (SimplifyProperties->bDiscardAttributes)
	{
		TargetMesh->DiscardAttributes();
	}
	if (SimplifyProperties->SimplifierType == ESimplifyType::QEM)
	{
		ComputeResult<FQEMSimplification>(TargetMesh, SimplifyProperties->bReproject, OriginalTriCount, OriginalMesh, OriginalMeshSpatial, 
			SimplifyProperties->TargetMode, SimplifyProperties->TargetPercentage, SimplifyProperties->TargetCount, SimplifyProperties->TargetEdgeLength);
	}
	else if (SimplifyProperties->SimplifierType == ESimplifyType::Attribute)
	{
		ComputeResult<FAttrMeshSimplification>(TargetMesh, SimplifyProperties->bReproject, OriginalTriCount, OriginalMesh, OriginalMeshSpatial,
			SimplifyProperties->TargetMode, SimplifyProperties->TargetPercentage, SimplifyProperties->TargetCount, SimplifyProperties->TargetEdgeLength);
	}
	else //if (SimplifierType == ESimplifyType::UE4Standard)
	{

		const FMeshDescription* SrcMeshDescription = ComponentTarget->GetMesh();
		FMeshDescription DstMeshDescription(*SrcMeshDescription);


		FOverlappingCorners OverlappingCorners;
		FMeshDescriptionOperations::FindOverlappingCorners(OverlappingCorners, *SrcMeshDescription, 1.e-5);

		FMeshReductionSettings ReductionSettings;
		if (SimplifyProperties->TargetMode == ESimplifyTargetType::Percentage)
		{
			ReductionSettings.PercentTriangles = FMath::Max(SimplifyProperties->TargetPercentage / 100., .001);  // Only support triangle percentage and count, but not edge length
		}
		else if (SimplifyProperties->TargetMode == ESimplifyTargetType::TriangleCount)
		{
			int32 NumTris = SrcMeshDescription->Polygons().Num();
			ReductionSettings.PercentTriangles = (float)SimplifyProperties->TargetCount / (float)NumTris;
		}

		float Error;
		{
			IMeshReductionManagerModule& MeshReductionModule = FModuleManager::Get().LoadModuleChecked<IMeshReductionManagerModule>("MeshReductionInterface");
			IMeshReduction* MeshReduction = MeshReductionModule.GetStaticMeshReductionInterface();


			if (!MeshReduction)
			{
				// no reduction possible
				Error = 0.f;
				return;
			}

			Error = ReductionSettings.MaxDeviation;
			MeshReduction->ReduceMeshDescription(DstMeshDescription, Error, *SrcMeshDescription, OverlappingCorners, ReductionSettings);
		}

		// Put the reduced mesh into the target...
		DynamicMeshComponent->InitializeMesh(&DstMeshDescription);

		// The UE4 tool will split the UV boundaries.  Need to weld this.
		{
			FDynamicMesh3* ComponentMesh = DynamicMeshComponent->GetMesh();

			FMergeCoincidentMeshEdges Merger(ComponentMesh);
			Merger.MergeSearchTolerance = 10.0f * FMathf::ZeroTolerance;
			Merger.OnlyUniquePairs = false;
			if (Merger.Apply() == false)
			{
				DynamicMeshComponent->InitializeMesh(&DstMeshDescription);
			}
			if (ComponentMesh->CheckValidity(true, EValidityCheckFailMode::ReturnOnly) == false)
			{
				DynamicMeshComponent->InitializeMesh(&DstMeshDescription);
			}
		
			DynamicMeshComponent->NotifyMeshUpdated();
		}
	}

	//UE_LOG(LogMeshSimplification, Log, TEXT("Mesh simplified to %d triangles"), TargetMesh->TriangleCount());

	DynamicMeshComponent->NotifyMeshUpdated();
	GetToolManager()->PostInvalidation();
	MeshStatisticsProperties->Update(*DynamicMeshComponent->GetMesh());

	bResultValid = true;
}

bool USimplifyMeshTool::HasAccept() const
{
	return true;
}

bool USimplifyMeshTool::CanAccept() const
{
	return true;
}




#undef LOCTEXT_NAMESPACE
