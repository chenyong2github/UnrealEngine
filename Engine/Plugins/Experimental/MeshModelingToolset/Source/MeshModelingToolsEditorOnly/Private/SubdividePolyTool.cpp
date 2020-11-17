// Copyright Epic Games, Inc. All Rights Reserved.

#include "SubdividePolyTool.h"
#include "ToolBuilderUtil.h"
#include "InteractiveToolManager.h"
#include "GroupTopology.h"
#include "MeshDescriptionToDynamicMesh.h"
#include "DynamicMeshToMeshDescription.h"
#include "ToolSetupUtil.h"
#include "Util/ColorConstants.h"
#include "MeshNormals.h"
#include "SimpleDynamicMeshComponent.h"
#include "Drawing/PreviewGeometryActor.h"

#define LOCTEXT_NAMESPACE "USubdividePolyTool"

class SubdivPostProcessor : public IRenderMeshPostProcessor
{
public:

	SubdivPostProcessor(int InSubdivisionLevel,
						ESubdivisionOutputNormals InNormalComputationMethod,
						ESubdivisionOutputUVs InUVComputationMethod) :
		SubdivisionLevel(InSubdivisionLevel),
		NormalComputationMethod(InNormalComputationMethod),
		UVComputationMethod(InUVComputationMethod)
	{}

	int SubdivisionLevel = 3;

	ESubdivisionOutputNormals NormalComputationMethod = ESubdivisionOutputNormals::Generated;

	ESubdivisionOutputUVs UVComputationMethod = ESubdivisionOutputUVs::Interpolated;

	void ProcessMesh(const FDynamicMesh3& Mesh, FDynamicMesh3& OutRenderMesh) final
	{
		constexpr bool bAutoCompute = true;
		FGroupTopology Topo(&Mesh, bAutoCompute);
		FSubdividePoly Subd(Topo, Mesh, SubdivisionLevel);
		Subd.NormalComputationMethod = NormalComputationMethod;
		Subd.UVComputationMethod = UVComputationMethod;

		ensure(Subd.ComputeTopologySubdivision());

		ensure(Subd.ComputeSubdividedMesh(OutRenderMesh));
	}
};


// Tool builder

bool USubdividePolyToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return ToolBuilderUtil::CountComponents(SceneState, CanMakeComponentTarget) == 1;
}

UInteractiveTool* USubdividePolyToolBuilder::BuildTool(const FToolBuilderState& SceneState) const
{
	UActorComponent* ActorComponent = ToolBuilderUtil::FindFirstComponent(SceneState, CanMakeComponentTarget);
	auto* MeshComponent = Cast<UPrimitiveComponent>(ActorComponent);
	check(MeshComponent != nullptr);

	USubdividePolyTool* NewTool = NewObject<USubdividePolyTool>(SceneState.ToolManager);
	NewTool->SetSelection(MakeComponentTarget(MeshComponent));
	NewTool->SetWorld(SceneState.World);

	return NewTool;
}


// Tool actual

void USubdividePolyTool::Setup()
{
	UInteractiveTool::Setup();

	if (!ComponentTarget)
	{
		return;
	}

	bool bWantVertexNormals = false;
	OriginalMesh = MakeShared<FDynamicMesh3>(bWantVertexNormals, false, false, false);
	FMeshDescriptionToDynamicMesh Converter;
	Converter.Convert(ComponentTarget->GetMesh(), *OriginalMesh);

	//
	// Error checking
	//

	FGroupTopology Topo(OriginalMesh.Get(), true);
	if (Topo.Groups.Num() == 0)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("NoGroupsWarning",
												 "This object has no PolyGroups. Use the PolyGroups or Select Tool to assign PolyGroups."),
										 EToolMessageLevel::UserWarning);
		return;
	}

	if (Topo.Groups.Num() < 2)
	{
		// TODO: for an open surface, use the surface boundary as a group boundary?

		GetToolManager()->DisplayMessage(LOCTEXT("SingleGroupsWarning",
												 "This object has only one PolyGroup. Use the PolyGroups or Select Tool to assign PolyGroups."),
										 EToolMessageLevel::UserWarning);
		return;
	}

	for (const FGroupTopology::FGroup& Group : Topo.Groups)
	{
		if (Group.Boundaries.Num() == 0)
		{
			// Error: Group has no boundaries, e.g. closed surface component with only one group
			GetToolManager()->DisplayMessage(LOCTEXT("NoGroupBoundaryWarning",
													 "Found a PolyGroup with no boundaries. Use the PolyGroups or Select Tool to assign PolyGroups."),
											 EToolMessageLevel::UserWarning);
			return;
		}

		if (Group.Boundaries.Num() > 1)
		{
			// Error: Group has multiple boundaries, e.g. nested polygon
			GetToolManager()->DisplayMessage(LOCTEXT("MultipleGroupBoundaryWarning",
													 "Found a PolyGroup with multiple boundaries, which is not supported. Use the PolyGroups or Select Tool to assign PolyGroups."),
											 EToolMessageLevel::UserWarning);
			return;
		}

		for ( const FGroupTopology::FGroupBoundary& Boundary : Group.Boundaries )
		{
			if (Boundary.GroupEdges.Num() < 3)
			{
				GetToolManager()->DisplayMessage(LOCTEXT("DegenerateGroupPolygon",
														 "One PolyGroup has fewer than three boundary edges. Use the PolyGroups or Select Tool to assign/fix PolyGroups."),
												 EToolMessageLevel::UserWarning);
				return;
			}
		}
	}

	//
	// Finished error checking
	//

	GetToolManager()->DisplayMessage(LOCTEXT("SubdividePolyToolMessage",
											 "Set the subdivision level and hit Accept to create a new subdivided mesh"),
									 EToolMessageLevel::UserNotification);

	Properties = NewObject<USubdividePolyToolProperties>(this, TEXT("Subdivide Mesh Tool Settings"));
	Properties->RestoreProperties(this);
	AddToolPropertySource(Properties);
	SetToolPropertySourceEnabled(Properties, true);

	PreviewMesh = NewObject<UPreviewMesh>(this);
	if (PreviewMesh == nullptr)
	{
		return;
	}
	PreviewMesh->CreateInWorld(TargetWorld, FTransform::Identity);

	PreviewMesh->SetTransform(ComponentTarget->GetWorldTransform());
	PreviewMesh->UpdatePreview(OriginalMesh.Get());

	USimpleDynamicMeshComponent* PreviewDynamicMeshComponent = (USimpleDynamicMeshComponent*)PreviewMesh->GetRootComponent();
	if (PreviewDynamicMeshComponent == nullptr)
	{
		return;
	}

	PreviewDynamicMeshComponent->SetRenderMeshPostProcessor(MakeUnique<SubdivPostProcessor>(Properties->SubdivisionLevel,
																					 Properties->NormalComputationMethod,
																					 Properties->UVComputationMethod));

	// Use the input mesh's material on the preview
	FComponentMaterialSet MaterialSet;
	ComponentTarget->GetMaterialSet(MaterialSet);
	for (int k = 0; k < MaterialSet.Materials.Num(); ++k)
	{
		PreviewMesh->SetMaterial(k, MaterialSet.Materials[k]);
	}

	// configure secondary render material
	UMaterialInterface* SelectionMaterial = ToolSetupUtil::GetSelectionMaterial(FLinearColor(0.8f, 0.75f, 0.0f), GetToolManager());
	if (SelectionMaterial != nullptr)
	{
		PreviewMesh->SetSecondaryRenderMaterial(SelectionMaterial);
	}

	// dynamic mesh configuration settings
	auto RebuildMeshPostProcessor = [this]()
	{
		USimpleDynamicMeshComponent* PreviewDynamicMeshComponent = (USimpleDynamicMeshComponent*)PreviewMesh->GetRootComponent();
		PreviewDynamicMeshComponent->SetRenderMeshPostProcessor(MakeUnique<SubdivPostProcessor>(Properties->SubdivisionLevel,
																							Properties->NormalComputationMethod,
																							Properties->UVComputationMethod));
		PreviewDynamicMeshComponent->NotifyMeshUpdated();
	};

	// Watch for property changes
	Properties->WatchProperty(Properties->SubdivisionLevel, [this, RebuildMeshPostProcessor](int)
	{
		RebuildMeshPostProcessor();
	});
	Properties->WatchProperty(Properties->NormalComputationMethod, [this, RebuildMeshPostProcessor](ESubdivisionOutputNormals)
	{
		RebuildMeshPostProcessor();
	});
	Properties->WatchProperty(Properties->UVComputationMethod, [this, RebuildMeshPostProcessor](ESubdivisionOutputUVs)
	{
		RebuildMeshPostProcessor();
	});


	auto RenderGroupsChanged = [this](bool bNewRenderGroups)
	{
		if (bNewRenderGroups)
		{
			PreviewMesh->SetOverrideRenderMaterial(ToolSetupUtil::GetSelectionMaterial(GetToolManager()));
			PreviewMesh->SetTriangleColorFunction([](const FDynamicMesh3* Mesh, int TriangleID)
			{
				return LinearColors::SelectFColor(Mesh->GetTriangleGroup(TriangleID));
			});
		}
		else
		{
			PreviewMesh->SetOverrideRenderMaterial(nullptr);
			PreviewMesh->SetTriangleColorFunction(nullptr);
		}

		USimpleDynamicMeshComponent* PreviewDynamicMeshComponent = (USimpleDynamicMeshComponent*)PreviewMesh->GetRootComponent();
		PreviewDynamicMeshComponent->FastNotifyColorsUpdated();
	};

	Properties->WatchProperty(Properties->bRenderGroups, RenderGroupsChanged);

	ComponentTarget->SetOwnerVisibility(false);

	// Render with polygroup colors
	RenderGroupsChanged(Properties->bRenderGroups);


	PreviewGeometry = NewObject<UPreviewGeometry>(this);
	PreviewGeometry->CreateInWorld(ComponentTarget->GetOwnerActor()->GetWorld(), ComponentTarget->GetWorldTransform());
	CreateOrUpdatePreviewGeometry();

	// regenerate preview geo if mesh changes due to undo/redo/etc
	PreviewDynamicMeshComponent->OnMeshChanged.AddLambda([this]() { bPreviewGeometryNeedsUpdate = true; });

	PreviewMesh->SetVisible(true);
}

void USubdividePolyTool::CreateOrUpdatePreviewGeometry()
{
	FGroupTopology Topology(OriginalMesh.Get(), true);
	int NumEdges = Topology.Edges.Num();

	PreviewGeometry->CreateOrUpdateLineSet(TEXT("TopologyEdges"),
										   NumEdges,
										   [&Topology, this](int32 Index, TArray<FRenderableLine>& LinesOut)
	{
		const FGroupTopology::FGroupEdge& Edge = Topology.Edges[Index];
		FIndex2i EdgeCorners = Edge.EndpointCorners;

		if (EdgeCorners[0] == FDynamicMesh3::InvalidID || EdgeCorners[1] == FDynamicMesh3::InvalidID)
		{
			return;
		}

		FIndex2i EdgeVertices{ Topology.Corners[EdgeCorners[0]].VertexID,
							   Topology.Corners[EdgeCorners[1]].VertexID };
		FVector A = (FVector)OriginalMesh->GetVertex(EdgeVertices[0]);
		FVector B = (FVector)OriginalMesh->GetVertex(EdgeVertices[1]);

		const float TopologyLineThickness = 4.0f;
		const FColor TopologyLineColor(255, 0, 0);
		LinesOut.Add(FRenderableLine(A, B, TopologyLineColor, TopologyLineThickness));
	});
}


void USubdividePolyTool::Shutdown(EToolShutdownType ShutdownType)
{
	if (Properties)
	{
		Properties->SaveProperties(this);
	}

	if (PreviewGeometry)
	{
		PreviewGeometry->Disconnect();
	}

	if (PreviewMesh)
	{
		if (ShutdownType == EToolShutdownType::Accept)
		{
			GetToolManager()->BeginUndoTransaction(LOCTEXT("USubdividePolyTool", "Subdivide Mesh"));

			USimpleDynamicMeshComponent* PreviewDynamicMeshComponent = (USimpleDynamicMeshComponent*)PreviewMesh->GetRootComponent();
			FDynamicMesh3* DynamicMeshResult = PreviewDynamicMeshComponent->GetRenderMesh();

			ComponentTarget->CommitMesh([DynamicMeshResult](const FPrimitiveComponentTarget::FCommitParams& CommitParams)
			{
				FDynamicMeshToMeshDescription Converter;
				Converter.Convert(DynamicMeshResult, *CommitParams.MeshDescription);
			});

			GetToolManager()->EndUndoTransaction();
		}
		PreviewMesh->Disconnect();
		PreviewMesh = nullptr;
	}

	ComponentTarget->SetOwnerVisibility(true);
}

bool USubdividePolyTool::CanAccept() const
{
	return PreviewMesh != nullptr;
}


void USubdividePolyTool::OnTick(float DeltaTime)
{
	if (bPreviewGeometryNeedsUpdate)
	{
		CreateOrUpdatePreviewGeometry();
		bPreviewGeometryNeedsUpdate = false;
	}
}


#undef LOCTEXT_NAMESPACE
