// Copyright Epic Games, Inc. All Rights Reserved.

#include "TopologyReport.h"

#include "CADKernel/Topo/Body.h"
#include "CADKernel/Topo/Shell.h"
#include "CADKernel/Topo/TopologicalFace.h"
#include "CADKernel/Topo/TopologicalEdge.h"

#include "CADKernel/UI/Message.h"

namespace CADKernel
{

bool FTopologyReport::HasMarker(const FTopologicalEntity* Entity)
{
	if (!Entity->HasMarker1())
	{
		Entity->SetMarker1();
		Entities.Add(Entity);
		return false;
	}
	return true;
}

void FTopologyReport::Add(const FBody* Body)
{
	if (!HasMarker(Body))
	{
		BodyCount++;
	}
}

void FTopologyReport::Add(const FShell* Shell)
{
	if (!HasMarker(Shell))
	{
		ShellCount++;
	}
}

void FTopologyReport::Add(const FTopologicalFace* Face)
{
	if (!HasMarker(Face))
	{
		FaceCount++;
	}
}

void FTopologyReport::Add(const FTopologicalEdge* Edge)
{
	EdgeCount++;

	const TSharedRef<const FTopologicalEdge>& ActiveEdge = Edge->GetLinkActiveEdge();
	if (!HasMarker(&*ActiveEdge))
	{
		ActiveEdge->SetMarker1();

		CoedgeCount++;

		switch (ActiveEdge->GetTwinEntityCount())
		{
		case 1:
			BorderEdgeCount++;
			Edges.Add(&ActiveEdge.Get());
			break;
		case 2:
			SurfaceEdgeCount++;
			break;
		default:
			NonManifoldEdgeCount++;
			Edges.Add(&ActiveEdge.Get());
			break;
		}
	}
}

void FTopologyReport::CountLoops()
{
	LoopCount = 0;
	ChainCount = 0;

	for (const FTopologicalEdge* Edge : Edges)
	{
		Edge->SetMarker2();
	}

	for (const FTopologicalEdge* Edge : Edges)
	{
		if (Edge->HasMarker1())
		{
			continue;
		}
		Edge->SetMarker1();

		if (Edge->GetTwinEntityCount() == 2)
		{
			continue;
		}

		const FTopologicalVertex* FirstVertex = &Edge->GetStartVertex()->GetLinkActiveEntity().Get();
		const FTopologicalVertex* NextVertex = &Edge->GetEndVertex()->GetLinkActiveEntity().Get();

		const FTopologicalEdge* NextEdge = Edge;

		TArray<FTopologicalEdge*> ConnectedEdges;

		bool bIsCycle = true;
		while (NextVertex != FirstVertex)
		{
			ConnectedEdges.Empty();
			NextVertex->GetConnectedEdges(ConnectedEdges);

			int32 BorderCount = 0;
			const FTopologicalEdge* VertexEdges[2] = { nullptr, nullptr };
			for (const FTopologicalEdge* ConnectedEdge : ConnectedEdges)
			{
				if (ConnectedEdge->HasMarker2())
				{
					if (BorderCount < 2)
					{
						VertexEdges[BorderCount++] = ConnectedEdge;
					}
					else
					{
						BorderCount++;
						break;
					}
				}
			}

			if (BorderCount != 2)
			{
				if (!bIsCycle)
				{
					break;
				}
				else
				{
					Swap(NextVertex, FirstVertex);
					bIsCycle = false;
				}
			}
			else
			{
				NextEdge = VertexEdges[0] == NextEdge ? VertexEdges[1] : VertexEdges[0];
				NextEdge->SetMarker1();
				NextVertex = &NextEdge->GetOtherVertex(*NextVertex)->GetLinkActiveEntity().Get();
			}
		}

		if (bIsCycle)
		{
			LoopCount++;
		}
		else
		{
			ChainCount++;
		}
	}

	for (const FTopologicalEdge* Edge : Edges)
	{
		Edge->ResetMarkers();
	}

}


void FTopologyReport::Print()
{
	for (const FTopologicalEntity* Entity : Entities)
	{
		Entity->ResetMarker1();
	}

	CountLoops();

	FMessage::FillReportFile(TEXT("Body"), BodyCount);
	FMessage::FillReportFile(TEXT("Shell"), ShellCount);
	FMessage::FillReportFile(TEXT("Face"), FaceCount);
	FMessage::FillReportFile(TEXT("Edge"), EdgeCount);
	FMessage::FillReportFile(TEXT(""), TEXT(""));
	FMessage::FillReportFile(TEXT("CoEdge"), CoedgeCount);
	FMessage::FillReportFile(TEXT("Surface"), SurfaceEdgeCount);
	FMessage::FillReportFile(TEXT("Border"), BorderEdgeCount);
	FMessage::FillReportFile(TEXT("NManif"), NonManifoldEdgeCount);
	FMessage::FillReportFile(TEXT(""), TEXT(""));
	FMessage::FillReportFile(TEXT("Loop"), LoopCount);
	FMessage::FillReportFile(TEXT("Chain"), ChainCount);
}

}
