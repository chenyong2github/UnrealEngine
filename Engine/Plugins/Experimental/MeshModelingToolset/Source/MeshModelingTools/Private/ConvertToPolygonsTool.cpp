// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConvertToPolygonsTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh3.h"
#include "MeshNormals.h"
#include "DynamicMeshEditor.h"
#include "MeshRegionBoundaryLoops.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"
#include "Util/ColorConstants.h"
#include "ToolSetupUtil.h"
#include "Polygroups/PolygroupUtil.h"
#include "PreviewMesh.h"

#include "SceneManagement.h" // for FPrimitiveDrawInterface
#include "ModelingOperators.h"
#include "MeshOpPreviewHelpers.h"
#include "ModelingOperators.h"

#define LOCTEXT_NAMESPACE "UConvertToPolygonsTool"


/*
 * ToolBuilder
 */


bool UConvertToPolygonsToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}

UInteractiveTool* UConvertToPolygonsToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UConvertToPolygonsTool* NewTool = NewObject<UConvertToPolygonsTool>(SceneState.ToolManager);

	UActorComponent* ActorComponent = ToolBuilderUtil::FindFirstComponent(SceneState, CanMakeComponentTarget);
	UPrimitiveComponent* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
	check(MeshComponent != nullptr);
	NewTool->SetSelection(MakeComponentTarget(MeshComponent));
	NewTool->SetWorld(SceneState.World);

	return NewTool;
}

class FConvertToPolygonsOp : public  FDynamicMeshOperator
{
public:
	FConvertToPolygonsOp() : FDynamicMeshOperator() {}

	virtual void CalculateResult(FProgressCancel* Progress) override
	{
		if ((Progress && Progress->Cancelled()) || !OriginalMesh)
		{
			return;
		}

		ResultMesh->Copy(*OriginalMesh, true, true, true, true);
	

		if (Progress && Progress->Cancelled())
		{
			return;
		}

		Polygons = FFindPolygonsAlgorithm(ResultMesh.Get());

		switch (ConversionMode)
		{
		case EConvertToPolygonsMode::FromUVIslands:
		{
			Polygons.FindPolygonsFromUVIslands();
			break;
		}
		case EConvertToPolygonsMode::FromConnectedTris:
		{
			Polygons.FindPolygonsFromConnectedTris();
			break;
		}
		case EConvertToPolygonsMode::FaceNormalDeviation:
		{
			double DotTolerance = 1.0 - FMathd::Cos(AngleTolerance * FMathd::DegToRad);
			Polygons.FindPolygonsFromFaceNormals(DotTolerance);
			break;
		}
		default:
			check(0);
		}

		Polygons.FindPolygonEdges();

		if (bCalculateNormals && ConversionMode == EConvertToPolygonsMode::FaceNormalDeviation)
		{
			if (ResultMesh->HasAttributes() == false)
			{
				ResultMesh->EnableAttributes();
			}

			FDynamicMeshNormalOverlay* NormalOverlay = ResultMesh->Attributes()->PrimaryNormals();
			NormalOverlay->ClearElements();

			FDynamicMeshEditor Editor(ResultMesh.Get());
			for (const TArray<int>& Polygon : Polygons.FoundPolygons)
			{
				FVector3f Normal = (FVector3f)ResultMesh->GetTriNormal(Polygon[0]);
				Editor.SetTriangleNormals(Polygon, Normal);
			}

			FMeshNormals Normals(ResultMesh.Get());
			Normals.RecomputeOverlayNormals(ResultMesh->Attributes()->PrimaryNormals());
			Normals.CopyToOverlay(NormalOverlay, false);
		}

	}

	void SetTransform(const FTransform& Transform)
	{
		ResultTransform = (FTransform3d)Transform;
	}

	
	FFindPolygonsAlgorithm Polygons;
	TSharedPtr<FDynamicMesh3, ESPMode::ThreadSafe> OriginalMesh;

	// parameters set by the tool
	EConvertToPolygonsMode ConversionMode;
	double AngleTolerance;
	bool bCalculateNormals;
};

TUniquePtr<FDynamicMeshOperator> UConvertToPolygonsOperatorFactory::MakeNewOperator()
{
	// backpointer used to populate parameters.
	check(ConvertToPolygonsTool);

	// Create the actual operator type based on the requested operation
	TUniquePtr<FConvertToPolygonsOp>  MeshOp = MakeUnique<FConvertToPolygonsOp>();

	// Operator runs on another thread - copy data over that it needs.
	ConvertToPolygonsTool->UpdateOpParameters(*MeshOp);

	// give the operator
	return MeshOp;
}

/*
 * Tool
 */
UConvertToPolygonsTool::UConvertToPolygonsTool()
{
	SetToolDisplayName(LOCTEXT("ConvertToPolygonsToolName", "Generate PolyGroups"));
}

void UConvertToPolygonsTool::SetWorld(UWorld* World)
{
	this->TargetWorld = World;
}

bool UConvertToPolygonsTool::CanAccept() const
{
	return Super::CanAccept() && (PreviewWithBackgroundCompute == nullptr || PreviewWithBackgroundCompute->HaveValidResult());
}

void UConvertToPolygonsTool::Setup()
{
	UInteractiveTool::Setup();

	FComponentMaterialSet MaterialSet;
	ComponentTarget->GetMaterialSet(MaterialSet);

	// populate the OriginalDynamicMesh with a conversion of the input mesh.
	{
		OriginalDynamicMesh = MakeShared<FDynamicMesh3, ESPMode::ThreadSafe>();
		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(ComponentTarget->GetMesh(), *OriginalDynamicMesh);
	}

	Settings = NewObject<UConvertToPolygonsToolProperties>(this);
	Settings->RestoreProperties(this);
	AddToolPropertySource(Settings);
	FTransform MeshTransform = ComponentTarget->GetWorldTransform();
	// hide existing mesh
	ComponentTarget->SetOwnerVisibility(false);
	// Set up the preview object
	{
		// create the operator factory
		UConvertToPolygonsOperatorFactory* ConvertToPolygonsOperatorFactory = NewObject<UConvertToPolygonsOperatorFactory>(this);
		ConvertToPolygonsOperatorFactory->ConvertToPolygonsTool = this; // set the back pointer


		PreviewWithBackgroundCompute = NewObject<UMeshOpPreviewWithBackgroundCompute>(ConvertToPolygonsOperatorFactory, "Preview");
		PreviewWithBackgroundCompute->Setup(this->TargetWorld, ConvertToPolygonsOperatorFactory);
		PreviewWithBackgroundCompute->SetIsMeshTopologyConstant(true, EMeshRenderAttributeFlags::Positions | EMeshRenderAttributeFlags::VertexNormals);

		// Give the preview something to display
		PreviewWithBackgroundCompute->PreviewMesh->SetTransform(ComponentTarget->GetWorldTransform());
		PreviewWithBackgroundCompute->PreviewMesh->SetTangentsMode(EDynamicMeshTangentCalcType::AutoCalculated);
		PreviewWithBackgroundCompute->PreviewMesh->UpdatePreview(OriginalDynamicMesh.Get());
		
		PreviewWithBackgroundCompute->ConfigureMaterials(MaterialSet.Materials,
			ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager())
		);

		// show the preview mesh
		PreviewWithBackgroundCompute->SetVisibility(true);

		// something to capture the polygons from the async task when it is done
		PreviewWithBackgroundCompute->OnOpCompleted.AddLambda([this](const FDynamicMeshOperator* MeshOp) 
		{ 
			const FConvertToPolygonsOp*  ConvertToPolygonsOp = static_cast<const FConvertToPolygonsOp*>(MeshOp);
				
			// edges used for tool ::Render() method 
			this->PolygonEdges = ConvertToPolygonsOp->Polygons.PolygonEdges;
				
			// we have new triangle groups to color
			UpdateVisualization();
		});

		// updates the triangle color visualization
		UpdateVisualization();
		// start the compute
		PreviewWithBackgroundCompute->InvalidateResult();
		
	}
	

	Settings->WatchProperty(Settings->ConversionMode,
							[this](EConvertToPolygonsMode NewMode)
							{ OnSettingsModified(); });
	Settings->WatchProperty(Settings->bShowGroupColors,
							[this](bool bNewValue) 
							{ UpdateVisualization(); });
	Settings->WatchProperty(Settings->AngleTolerance,
							[this](float value)
							{ OnSettingsModified(); });
	

	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool", "Cluster triangles of the Mesh into PolyGroups using various strategies"),
		EToolMessageLevel::UserNotification);
}

void UConvertToPolygonsTool::UpdateOpParameters(FConvertToPolygonsOp& ConvertToPolygonsOp) const
{
	ConvertToPolygonsOp.bCalculateNormals = Settings->bCalculateNormals;
	ConvertToPolygonsOp.ConversionMode    = Settings->ConversionMode;
	ConvertToPolygonsOp.AngleTolerance    = Settings->AngleTolerance;
	ConvertToPolygonsOp.OriginalMesh = OriginalDynamicMesh;
	
	FTransform LocalToWorld = ComponentTarget->GetWorldTransform();
	ConvertToPolygonsOp.SetTransform(LocalToWorld);
}

void UConvertToPolygonsTool::GenerateAsset(const FDynamicMeshOpResult& Result)
{
	GetToolManager()->BeginUndoTransaction(LOCTEXT("ConvertToPolygonsToolTransactionName", "Find Polygroups"));

	FDynamicMesh3* DynamicMeshResult = Result.Mesh.Get();
	check(DynamicMeshResult != nullptr);

	ComponentTarget->CommitMesh([DynamicMeshResult](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
	{
		FDynamicMeshToMeshDescription Converter;
		Converter.Convert(DynamicMeshResult, *CommitParams.MeshDescription);
	});

	GetToolManager()->EndUndoTransaction();
}

void UConvertToPolygonsTool::Shutdown(EToolShutdownType ShutdownType)
{
	Settings->SaveProperties(this);
	ComponentTarget->SetOwnerVisibility(true);

	if (PreviewWithBackgroundCompute)
	{
		FDynamicMeshOpResult Result = PreviewWithBackgroundCompute->Shutdown();
		if (ShutdownType == EToolShutdownType::Accept)
		{
			GenerateAsset(Result);
		}
	}
}

void UConvertToPolygonsTool::OnSettingsModified()
{
	PreviewWithBackgroundCompute->InvalidateResult();
}


void UConvertToPolygonsTool::OnTick(float DeltaTime)
{
	PreviewWithBackgroundCompute->Tick(DeltaTime);
}

void UConvertToPolygonsTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	FColor LineColor(255, 0, 0);

	FPrimitiveDrawInterface* PDI = RenderAPI->GetPrimitiveDrawInterface();
	float PDIScale = RenderAPI->GetCameraState().GetPDIScalingFactor();
	FTransform Transform = ComponentTarget->GetWorldTransform(); //Actor->GetTransform();

	for (int eid : PolygonEdges)
	{
		FVector3d A, B;
		OriginalDynamicMesh->GetEdgeV(eid, A, B);
		PDI->DrawLine(Transform.TransformPosition((FVector)A), Transform.TransformPosition((FVector)B),
			LineColor, 0, 2.0*PDIScale, 1.0f, true);
	}
}


void UConvertToPolygonsTool::UpdateVisualization()
{
	
	if (!PreviewWithBackgroundCompute)
	{
		return;
	}

	FComponentMaterialSet MaterialSet;
	if (Settings->bShowGroupColors)
	{
		int32 NumMaterials = ComponentTarget->GetNumMaterials();
		for (int32 i = 0; i < NumMaterials; ++i)
		{ 
			MaterialSet.Materials.Add(ToolSetupUtil::GetSelectionMaterial(GetToolManager()));
		}
		PreviewWithBackgroundCompute->PreviewMesh->SetTriangleColorFunction([this](const FDynamicMesh3* Mesh, int TriangleID)
		{
			return LinearColors::SelectFColor(Mesh->GetTriangleGroup(TriangleID));
		}, 
		UPreviewMesh::ERenderUpdateMode::FastUpdate);
	}
	else
	{
		ComponentTarget->GetMaterialSet(MaterialSet);
		PreviewWithBackgroundCompute->PreviewMesh->ClearTriangleColorFunction(UPreviewMesh::ERenderUpdateMode::FastUpdate);
	}
	PreviewWithBackgroundCompute->ConfigureMaterials(MaterialSet.Materials,
		ToolSetupUtil::GetDefaultWorkingMaterial(GetToolManager()));
}



#undef LOCTEXT_NAMESPACE
