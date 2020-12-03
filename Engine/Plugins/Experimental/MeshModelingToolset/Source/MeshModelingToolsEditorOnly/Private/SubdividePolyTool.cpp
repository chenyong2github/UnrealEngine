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
						ESubdivisionScheme InSubdivisionScheme,
						ESubdivisionOutputNormals InNormalComputationMethod,
						ESubdivisionOutputUVs InUVComputationMethod) :
		SubdivisionLevel(InSubdivisionLevel),
		SubdivisionScheme(InSubdivisionScheme),
		NormalComputationMethod(InNormalComputationMethod),
		UVComputationMethod(InUVComputationMethod)
	{}

	int SubdivisionLevel = 3;
	ESubdivisionScheme SubdivisionScheme = ESubdivisionScheme::CatmullClark;
	ESubdivisionOutputNormals NormalComputationMethod = ESubdivisionOutputNormals::Generated;
	ESubdivisionOutputUVs UVComputationMethod = ESubdivisionOutputUVs::Interpolated;

	void ProcessMesh(const FDynamicMesh3& Mesh, FDynamicMesh3& OutRenderMesh) final
	{
		constexpr bool bAutoCompute = true;
		FGroupTopology Topo(&Mesh, bAutoCompute);
		FSubdividePoly Subd(Topo, Mesh, SubdivisionLevel);
		Subd.SubdivisionScheme = SubdivisionScheme;
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


bool USubdividePolyTool::CheckGroupTopology(FText& Message)
{
	FGroupTopology Topo(OriginalMesh.Get(), true);

	if (Topo.Groups.Num() == 0)
	{
		Message = LOCTEXT("NoGroupsWarning",
						  "This object has no PolyGroups.\nUse the PolyGroups or Select Tool to assign PolyGroups.\nTool will be limited to Loop subdivision scheme.");
		return false;
	}

	if (Topo.Groups.Num() < 2)
	{
		// TODO: for an open surface, use the surface boundary as a group boundary?
		Message = LOCTEXT("SingleGroupsWarning",
						  "This object has only one PolyGroup.\nUse the PolyGroups or Select Tool to assign PolyGroups.\nTool will be limited to Loop subdivision scheme.");
		return false;
	}

	for (const FGroupTopology::FGroup& Group : Topo.Groups)
	{
		if (Group.Boundaries.Num() == 0)
		{
			// Error: Group has no boundaries, e.g. closed surface component with only one group
			Message = LOCTEXT("NoGroupBoundaryWarning",
							  "Found a PolyGroup with no boundaries.\nUse the PolyGroups or Select Tool to assign PolyGroups.\nTool will be limited to Loop subdivision scheme.");
			return false;
		}

		if (Group.Boundaries.Num() > 1)
		{
			// Error: Group has multiple boundaries, e.g. nested polygon
			Message = LOCTEXT("MultipleGroupBoundaryWarning",
							  "Found a PolyGroup with multiple boundaries, which is not supported.\nUse the PolyGroups or Select Tool to assign PolyGroups.\nTool will be limited to Loop subdivision scheme.");
			return false;
		}

		for (const FGroupTopology::FGroupBoundary& Boundary : Group.Boundaries)
		{
			if (Boundary.GroupEdges.Num() < 3)
			{
				Message = LOCTEXT("DegenerateGroupPolygon",
								  "One PolyGroup has fewer than three boundary edges.\nUse the PolyGroups or Select Tool to assign/fix PolyGroups.\nTool will be limited to Loop subdivision scheme.");
				return false;
			}
		}
	}

	return true;
}

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

	FText ErrorMessage;
	bool bCatmullClarkOK = CheckGroupTopology(ErrorMessage);

	if (bCatmullClarkOK)
	{
		GetToolManager()->DisplayMessage(LOCTEXT("SubdividePolyToolMessage",
												 "Set the subdivision level and hit Accept to create a new subdivided mesh"),
										 EToolMessageLevel::UserNotification);
	}
	else
	{
		GetToolManager()->DisplayMessage(ErrorMessage, EToolMessageLevel::UserWarning);
	}

	Properties = NewObject<USubdividePolyToolProperties>(this, TEXT("Subdivide Mesh Tool Settings"));
	Properties->RestoreProperties(this);

	Properties->bCatmullClarkOK = bCatmullClarkOK;
	if (!bCatmullClarkOK)
	{
		Properties->SubdivisionScheme = ESubdivisionScheme::Loop;
	}

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

	check(Properties->SubdivisionLevel >= 1);	// Should be enforced by UPROPERTY meta tags

	PreviewDynamicMeshComponent->SetRenderMeshPostProcessor(MakeUnique<SubdivPostProcessor>(Properties->SubdivisionLevel,
																							Properties->SubdivisionScheme,
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
																								Properties->SubdivisionScheme,
																								Properties->NormalComputationMethod,
																								Properties->UVComputationMethod));
		PreviewDynamicMeshComponent->NotifyMeshUpdated();
	};

	// Watch for property changes
	Properties->WatchProperty(Properties->SubdivisionLevel, [this, RebuildMeshPostProcessor](int)
	{
		RebuildMeshPostProcessor();
	});
	Properties->WatchProperty(Properties->SubdivisionScheme, [this, RebuildMeshPostProcessor](ESubdivisionScheme)
	{
		RebuildMeshPostProcessor();
		bPreviewGeometryNeedsUpdate = true;		// Switch from rendering poly cage to all triangle edges
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
			}, UPreviewMesh::ERenderUpdateMode::FullUpdate);
		}
		else
		{
			PreviewMesh->SetOverrideRenderMaterial(nullptr);
			PreviewMesh->SetTriangleColorFunction(nullptr, UPreviewMesh::ERenderUpdateMode::FullUpdate);
		}
	};

	Properties->WatchProperty(Properties->bRenderGroups, RenderGroupsChanged);

	// Render with polygroup colors
	RenderGroupsChanged(Properties->bRenderGroups);

	Properties->WatchProperty(Properties->bRenderCage, [this](bool bNewRenderCage)
	{
		bPreviewGeometryNeedsUpdate = true;
	});

	PreviewGeometry = NewObject<UPreviewGeometry>(this);
	PreviewGeometry->CreateInWorld(ComponentTarget->GetOwnerActor()->GetWorld(), ComponentTarget->GetWorldTransform());
	CreateOrUpdatePreviewGeometry();

	// regenerate preview geo if mesh changes due to undo/redo/etc
	PreviewDynamicMeshComponent->OnMeshChanged.AddLambda([this]() { bPreviewGeometryNeedsUpdate = true; });

	ComponentTarget->SetOwnerVisibility(false);
	PreviewMesh->SetVisible(true);
}

void USubdividePolyTool::CreateOrUpdatePreviewGeometry()
{
	if (!Properties->bRenderCage)
	{
		PreviewGeometry->RemoveLineSet(TEXT("TopologyEdges"));
		PreviewGeometry->RemoveLineSet(TEXT("AllEdges"));
		return;
	}

	if (Properties->SubdivisionScheme == ESubdivisionScheme::Loop)
	{
		int NumEdges = OriginalMesh->EdgeCount();

		PreviewGeometry->RemoveLineSet(TEXT("TopologyEdges"));

		PreviewGeometry->CreateOrUpdateLineSet(TEXT("AllEdges"),
											   NumEdges,
											   [this](int32 Index, TArray<FRenderableLine>& LinesOut)
		{
			FIndex2i EdgeVertices = OriginalMesh->GetEdgeV(Index);

			if (EdgeVertices[0] == FDynamicMesh3::InvalidID || EdgeVertices[1] == FDynamicMesh3::InvalidID)
			{
				return;
			}
			FVector A = (FVector)OriginalMesh->GetVertex(EdgeVertices[0]);
			FVector B = (FVector)OriginalMesh->GetVertex(EdgeVertices[1]);
			const float TopologyLineThickness = 4.0f;
			const FColor TopologyLineColor(255, 0, 0);
			LinesOut.Add(FRenderableLine(A, B, TopologyLineColor, TopologyLineThickness));
		});
	}
	else
	{
		FGroupTopology Topology(OriginalMesh.Get(), true);
		int NumEdges = Topology.Edges.Num();

		PreviewGeometry->RemoveLineSet(TEXT("AllEdges"));

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
