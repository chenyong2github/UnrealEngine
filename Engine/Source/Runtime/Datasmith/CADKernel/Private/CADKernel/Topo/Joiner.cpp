// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Topo/Joiner.h"

#include "CADKernel/Geo/Curves/RestrictionCurve.h"
#include "CADKernel/Topo/TopologicalEdge.h"
#include "CADKernel/Topo/TopologicalLink.h"
#include "CADKernel/Topo/TopologicalFace.h"
#include "CADKernel/Topo/TopologicalLoop.h"
#include "CADKernel/Topo/TopologicalVertex.h"
#include "CADKernel/UI/Display.h"
#include "CADKernel/UI/Message.h"
#include "CADKernel/Utils/Util.h"

using namespace CADKernel;

FJoiner::FJoiner(TArray<TSharedPtr<FTopologicalFace>>& InSurfaces, double InTolerance)
	: Surfaces(InSurfaces)
	, JoiningTolerance(InTolerance)
	, JoiningToleranceSqr(FMath::Square(JoiningTolerance))
{
}

void FJoiner::JoinSurfaces()
{

}

void FJoiner::JoinSurfaces(bool bProcessOnlyBorderEdges, bool bProcessOnlyNonManifoldEdges)
{
	if (!(bProcessOnlyBorderEdges || bProcessOnlyNonManifoldEdges))
	{
		JoinVertices();
		CheckSelfConnectedEdge();
	}
	else
	{
		TArray<TSharedPtr<FTopologicalVertex>> NewMergeVertices;
		JoinBorderVertices(NewMergeVertices);
		JoinEdges(NewMergeVertices);
	}

	JoinEdges(Vertices);
	TArray<TSharedPtr<FTopologicalVertex>> VerticesToProcess;
	MergeUnconnectedAdjacentEdges(VerticesToProcess, true);
	JoinEdges(VerticesToProcess);
}

void FJoiner::GetVertices()
{
	Vertices.Reserve(10 * Surfaces.Num());

	for (TSharedPtr<FTopologicalFace> Surface : Surfaces)
	{
		for (TSharedPtr<FTopologicalLoop> Loop : Surface->GetLoops())
		{
			for (const FOrientedEdge& OrientedEdge : Loop->GetEdges())
			{
				const TSharedPtr<FTopologicalEdge>& Edge = OrientedEdge.Entity;
				if (!Edge->GetStartVertex()->GetLinkActiveEntity()->HasMarker1())
				{
					Edge->GetStartVertex()->GetLinkActiveEntity()->SetMarker1();
					Vertices.Add(Edge->GetStartVertex()->GetLinkActiveEntity());
				}
				if (!Edge->GetEndVertex()->GetLinkActiveEntity()->HasMarker1())
				{
					Edge->GetEndVertex()->GetLinkActiveEntity()->SetMarker1();
					Vertices.Add(Edge->GetEndVertex()->GetLinkActiveEntity());
				}
			}
		}
	}
}

void FJoiner::JoinVertices()
{
	GetVertices();

	int32 VertexNum = (int32) Vertices.Num();

	TArray<double> VerticesWeight;
	VerticesWeight.Reserve(VertexNum);
	TArray<int32> SortedVertexIndexes;
	SortedVertexIndexes.Reserve(VertexNum);
	TArray<TSharedPtr<FTopologicalVertex>> ActiveVertices;
	ActiveVertices.Reserve(VertexNum);

	for (TSharedPtr<FTopologicalVertex> Vertex: Vertices)
	{
		Vertex->ResetMarker1();
		VerticesWeight.Add(Vertex->GetCoordinates().X + Vertex->GetCoordinates().Y + Vertex->GetCoordinates().Z);
	}
	for (int32 Index = 0; Index < Vertices.Num(); ++Index)
	{
		SortedVertexIndexes.Add(Index);
	}
	SortedVertexIndexes.Sort([&VerticesWeight](const int32& Index1, const int32& Index2) { return VerticesWeight[Index1] < VerticesWeight[Index2]; });

	for (int32 IndexI = 0; IndexI < VertexNum; ++IndexI)
	{
		TSharedPtr<FTopologicalVertex> Vertex = Vertices[SortedVertexIndexes[IndexI]];
		if (Vertex->HasMarker1())
		{
			continue;
		}
		ActiveVertices.Add(Vertex);
		Vertex->SetMarker1();

		double VertexWeigth = VerticesWeight[SortedVertexIndexes[IndexI]];
		FPoint Barycenter = Vertex->GetBarycenter();
		for (int32 IndexJ = IndexI+1; IndexJ < VertexNum; ++IndexJ)
		{
			TSharedPtr<FTopologicalVertex> OtherVertex = Vertices[SortedVertexIndexes[IndexJ]];
			if (OtherVertex->HasMarker1())
			{
				continue;
			}

			double OtherVertexWeigth = VerticesWeight[SortedVertexIndexes[IndexJ]];
			if ((OtherVertexWeigth - VertexWeigth) > JoiningTolerance)
			{
				break;
			}

			double DistanceSqr = OtherVertex->GetLinkActiveEntity()->SquareDistance(Barycenter);
			if (DistanceSqr < JoiningToleranceSqr)
			{
				OtherVertex->SetMarker1();
				Vertex->Link(OtherVertex.ToSharedRef());
				Barycenter = Vertex->GetBarycenter();
			}
		}
	}

	for (TSharedPtr<FTopologicalVertex> Vertex : Vertices)
	{
		Vertex->ResetMarker1();
	}
	Swap(ActiveVertices, Vertices);
}

void FJoiner::JoinBorderVertices(TArray<TSharedPtr<FTopologicalVertex>>& MergedVertices)
{
	if (!Vertices.Num())
	{
		GetVertices();
	}

	int32 VertexNum = (int32)Vertices.Num();

	TArray<double> VerticesWeight;
	VerticesWeight.Reserve(VertexNum);
	TArray<int32> SortedVertexIndexes;
	SortedVertexIndexes.Reserve(VertexNum);

	TArray<TSharedPtr<FTopologicalVertex>> ActiveVertices;
	ActiveVertices.Reserve(VertexNum);

	for (TSharedPtr<FTopologicalVertex> Vertex : Vertices)
	{
		Vertex->ResetMarker1();
		VerticesWeight.Add(Vertex->GetCoordinates().X + Vertex->GetCoordinates().Y + Vertex->GetCoordinates().Z);
	}
	for (int32 Index = 0; Index < Vertices.Num(); ++Index)
	{
		SortedVertexIndexes.Add(Index);
	}
	SortedVertexIndexes.Sort([&VerticesWeight](const int32& Index1, const int32& Index2) { return VerticesWeight[Index1] < VerticesWeight[Index2]; });

	int32 StartIndexJ = 1;
	for (int32 IndexI = 0; IndexI <VertexNum; ++IndexI)
	{
		TSharedRef<FTopologicalVertex> Vertex = Vertices[SortedVertexIndexes[IndexI]].ToSharedRef();
		if (!Vertex->IsBorderVertex())
		{
			continue;
		}

		double VertexWeigth = VerticesWeight[SortedVertexIndexes[IndexI]];
		FPoint Barycenter = Vertex->GetBarycenter();
		for (int32 IndexJ = StartIndexJ; IndexJ < VertexNum; ++IndexJ)
		{
			TSharedRef<FTopologicalVertex> OtherVertex = Vertices[SortedVertexIndexes[IndexJ]].ToSharedRef();
			if (Vertex->GetLink() == OtherVertex->GetLink())
			{
				continue;
			}

			double OtherVertexWeigth = VerticesWeight[SortedVertexIndexes[IndexJ]];

			if (OtherVertexWeigth + JoiningTolerance - VertexWeigth <0)
			{
				StartIndexJ = IndexJ;
				continue;
			}

			if (OtherVertexWeigth - JoiningTolerance - VertexWeigth > 0)
			{
				break;
			}

			double DistanceSqr = OtherVertex->GetLinkActiveEntity()->SquareDistance(Barycenter);
			if (DistanceSqr < JoiningToleranceSqr)
			{
				TArray<TSharedPtr<FTopologicalEdge>> CommonEdges;
				Vertex->GetConnectedEdges(OtherVertex, CommonEdges);
				if (CommonEdges.Num() > 0)
				{
					continue;
				}

				OtherVertex->SetMarker1();
				Vertex->Link(OtherVertex);
				Barycenter = Vertex->GetBarycenter();
				ActiveVertices.Add(Vertex);
			}
		}
	}

	MergedVertices.Reserve(ActiveVertices.Num());
	for (TSharedPtr<FTopologicalVertex> Vertex : ActiveVertices)
	{
		if (Vertex->GetLinkActiveEntity()->HasMarker1())
		{
			continue;
		}
		MergedVertices.Add(Vertex->GetLinkActiveEntity());
		MergedVertices.Last()->SetMarker1();
	}

	for (TSharedPtr<FTopologicalVertex> Vertex : MergedVertices)
	{
		Vertex->ResetMarker1();
	}
}

// =========================================================================================================================================================================================================
// =========================================================================================================================================================================================================
// =========================================================================================================================================================================================================
//
//
//                                                                            NOT YET REVIEWED
//
//
// =========================================================================================================================================================================================================
// =========================================================================================================================================================================================================
// =========================================================================================================================================================================================================


void FJoiner::CheckSelfConnectedEdge()
{
	TArray<TSharedPtr<FTopologicalEdge>> CrunchedEdges;
	CrunchedEdges.Reserve(100);

	TArray<TSharedPtr<FTopologicalEdge>> IsolatedEdges;
	IsolatedEdges.Reserve(100);

	for (TSharedPtr<FTopologicalVertex> Vertex : Vertices)
	{
		if (Vertex->GetTwinsEntities().Num() > 1)
		{
			for (TWeakPtr<FTopologicalVertex> TwinVertex : Vertex->GetTwinsEntities())
			{
				TArray<TWeakPtr<FTopologicalEdge>> Edges = TwinVertex.Pin()->GetConnectedEdges();
				for (TWeakPtr<FTopologicalEdge> WeakEdge : Edges)
				{
					TSharedPtr<FTopologicalEdge> Edge = WeakEdge.Pin();
					if (!Edge->GetLoop().IsValid())
					{
						IsolatedEdges.Add(Edge);
						continue;
					}

					if (Edge->HasMarker1())
					{
						continue;
					}

					if (Edge->GetStartVertex()->GetLink() == Edge->GetEndVertex()->GetLink())
					{
						ensureCADKernel(!Edge->IsDegenerated());
						CrunchedEdges.Add(Edge);
						Edge->SetMarker1();
					}
				}
			}
		}
	}

	TArray<TSharedPtr<FTopologicalVertex>> NewVertices;
	NewVertices.Reserve((CrunchedEdges.Num() + IsolatedEdges.Num()) * 2);

	TFunction<void(const TSharedPtr<TTopologicalLink<FTopologicalVertex>>&, const TSharedPtr<FTopologicalVertex>&, const TSharedPtr<FTopologicalVertex>&)> DeleteVertexOfLink = [&NewVertices](const TSharedPtr<TTopologicalLink<FTopologicalVertex>>& OldLink, const TSharedPtr<FTopologicalVertex>& Vertex1, const TSharedPtr<FTopologicalVertex>& Vertex2)
	{
		TSharedPtr<FTopologicalVertex> NewVertex = TSharedPtr<FTopologicalVertex>();
		for (TWeakPtr<FTopologicalVertex> IVertex : OldLink->GetTwinsEntities())
		{
			ensureCADKernel(IVertex.IsValid());
			IVertex.Pin()->ResetTopologicalLink();
			if (IVertex == Vertex1 || IVertex == Vertex2)
			{
				continue;
			}

			if (!NewVertex)
			{
				NewVertex = IVertex.Pin();
				NewVertices.Add(NewVertex);
			}
			else
			{
				NewVertex->Link(IVertex.Pin().ToSharedRef());
			}
		}
	};

	for (TSharedPtr<FTopologicalEdge> Edge : IsolatedEdges)
	{
		TSharedPtr<FTopologicalVertex> Vertex1 = Edge->GetStartVertex();
		TSharedPtr<FTopologicalVertex> Vertex2 = Edge->GetEndVertex();
		if (!Vertex1 && !Vertex2)
		{
			continue;
		}

		ensureCADKernel(!Vertex1 == !Vertex2);

		TSharedPtr<TTopologicalLink<FTopologicalVertex>> OldLink1 = Vertex1->GetLink();
		TSharedPtr<TTopologicalLink<FTopologicalVertex>> OldLink2 = Vertex2->GetLink();

		Vertex1->RemoveConnectedEdge(Edge.ToSharedRef());
		Vertex2->RemoveConnectedEdge(Edge.ToSharedRef());
		if (!Vertex1->GetConnectedEdges().Num())
		{
			if (!Vertex2->GetConnectedEdges().Num())
			{
				DeleteVertexOfLink(OldLink1, Vertex1, Vertex2);
				if (OldLink1 != OldLink2)
				{
					DeleteVertexOfLink(OldLink2, Vertex1, Vertex2);
				}
			}
			else
			{
				DeleteVertexOfLink(OldLink1, Vertex1, nullptr);
			}
			continue;
		}

		if (!Vertex2->GetConnectedEdges().Num())
		{
			DeleteVertexOfLink(OldLink2, Vertex2, nullptr);
		}
	}

	for(TSharedPtr<FTopologicalEdge> Edge : CrunchedEdges)
	{
		Edge->ResetMarkers();

		TSharedPtr<FTopologicalVertex> Vertex1 = Edge->GetStartVertex();
		TSharedPtr<FTopologicalVertex> Vertex2 = Edge->GetEndVertex();
		TSharedPtr<TTopologicalLink<FTopologicalVertex>> OldLink = Vertex1->GetLink();

		for (TWeakPtr<FTopologicalVertex> Vertex : OldLink->GetTwinsEntities())
		{
			Vertex.Pin()->ResetTopologicalLink();
		}

		for (TWeakPtr<FTopologicalVertex> Vertex : OldLink->GetTwinsEntities())
		{
			double Distance1 = Vertex1->Distance(Vertex.Pin().ToSharedRef());
			double Distance2 = Vertex2->Distance(Vertex.Pin().ToSharedRef());
			if (Distance1 <Distance2)
			{
				Vertex1->Link(Vertex.Pin().ToSharedRef());
			}
			else
			{
				Vertex2->Link(Vertex.Pin().ToSharedRef());
			}
		}

		NewVertices.Add(Vertex1->GetLinkActiveEntity());
		NewVertices.Add(Vertex2->GetLinkActiveEntity());
	}

	TArray<TSharedPtr<FTopologicalVertex>> ActiveVertices;
	ActiveVertices.Reserve(Vertices.Num() + NewVertices.Num());

	for (TSharedPtr<FTopologicalVertex> Vertex : NewVertices)
	{
		if (Vertex->GetLinkActiveEntity()->HasMarker1())
		{
			continue;
		}
		Vertex->GetLinkActiveEntity()->SetMarker1();
		ActiveVertices.Add(Vertex->GetLinkActiveEntity());
	}

	int32 NumOfVertices = (int32)ActiveVertices.Num();

	for (TSharedPtr<FTopologicalVertex> Vertex : Vertices)
	{
		if (Vertex->GetLinkActiveEntity()->HasMarker1())
		{
			continue;
		}
		ActiveVertices.Add(Vertex->GetLinkActiveEntity());
	}

	for (int32 Index = 0; Index <NumOfVertices; Index++)
	{
		ActiveVertices[Index]->ResetMarker1();
	}

	Swap(ActiveVertices, Vertices);
}

void FJoiner::FixCollapsedEdges()
{
	TArray<TSharedPtr<FTopologicalEdge>> CollapsedEdges;

	int32 NbDegeneratedEdges = 0;
	for (const TSharedPtr<FTopologicalFace>& Surface : Surfaces)
	{
		for (const TSharedPtr<FTopologicalLoop>& Loop : Surface->GetLoops())
		{
			for (const FOrientedEdge& OrientedEdge : Loop->GetEdges())
			{
				const TSharedPtr<FTopologicalEdge>& Edge = OrientedEdge.Entity;
				for (const TWeakPtr<FTopologicalEdge>& TwinEdge : Edge->GetTwinsEntities())
				{
					TSharedPtr<FTopologicalFace> Face = TwinEdge.Pin()->GetFace();
					if (!Face.IsValid())
					{
						continue;
					}
				}

				if (Edge->GetStartVertex()->GetLink() == Edge->GetEndVertex()->GetLink())
				{
					if (!Edge->IsDegenerated())
					{
						for (const TWeakPtr<FTopologicalEdge>& TwinEdge : Edge->GetTwinsEntities())
						{
							TwinEdge.Pin()->SetAsDegenerated();
						}
					}
				}
			}
		}
	}

	for (TSharedPtr<FTopologicalEdge>& Edge : CollapsedEdges)
	{
		TSharedPtr<FTopologicalLoop> Loop = Edge->GetLoop();
		Loop->RemoveEdge(Edge);

		TSharedPtr<FTopologicalVertex> StartVertex = Edge->GetStartVertex();
		TSharedPtr<FTopologicalVertex> EndVertex = Edge->GetEndVertex();

		Edge->Delete();

		StartVertex->DeleteIfIsolated();
		EndVertex->DeleteIfIsolated();

		if (Loop->GetEdges().Num() == 0)
		{
			TSharedPtr<FTopologicalFace> Face = Loop->GetFace();
			Face->RemoveLoop(Loop);
		}
	}
}



void FJoiner::MergeUnconnectedAdjacentEdges(TArray<TSharedPtr<FTopologicalVertex>>& NewEdgeVertices, bool bOnlyIfSameCurve)
{
// TODO 
	ensureCADKernel(false);
#ifdef TODO

	NewEdgeVertices.Reserve(Vertices.Num());
	for (TSharedPtr<FTopologicalVertex> Vertex : Vertices)
	{
		TArray<TWeakPtr<FTopologicalEdge>> Edges = Vertex->GetConnectedEdges();
		if (Edges.Num() == 2)
		{
			// TODO Work in 2D
			FPoint Edge0Tangent;
			Edges[0].Pin()->GetTangentAt(Vertex, Edge0Tangent);

			FPoint Edge1Tangent;
			Edges[1].Pin()->GetTangentAt(Vertex, Edge1Tangent);

			double CosAngle = Edge0Tangent.ComputeCosinus(Edge1Tangent);
			if (CosAngle < -0.75)
			{
				TArray<EOrientation> EdgeDirections;
				EdgeDirections.Reserve(2);
				EdgeDirections.Add(Edges[0].Pin()->GetEndVertex() == Vertex ? EOrientation::Front : EOrientation::Back);
				EdgeDirections.Add(Edges[1].Pin()->GetStartVertex() == Vertex ? EOrientation::Front : EOrientation::Back);
				TSharedPtr<FTopologicalEdge> ExtendedEdge = nullptr;

				// TODO Check better if the both curve are similar (2DCurve as to be similar)
				if (Edges[0].Pin()->GetCurve() != Edges[1].Pin()->GetCurve())
				{
					//ExtendedEdge = FEdge::ReplaceEdgesByOne(Edges, EdgeDirections, GetTolerance());
				}
				else
				{
					//ExtendedEdge = FEdge::CreateEdgeToMerge2Edges(Edges, EdgeDirections);
				}

				if (ExtendedEdge)
				{
					NewEdgeVertices.Add(ExtendedEdge->GetStartVertex());
					NewEdgeVertices.Add(ExtendedEdge->GetEndVertex());
				}
				continue;
			}
		}
	}
#endif
}


void FJoiner::JoinEdges(TArray<TSharedPtr<FTopologicalVertex>>& VerticesToProcess)
{
	ensureCADKernel(false);
#ifdef TODO

	TArray<TSharedPtr<FTopologicalEdge>> ProcessedEdges;
	ProcessedEdges.Reserve(VerticesToProcess.Num());

	TArray<TWeakPtr<FTopologicalEdge>> ConnectedEdges;
	for (TSharedPtr<FTopologicalVertex> Vertex : VerticesToProcess)
	{
		ConnectedEdges.Empty();
		Vertex->GetConnectedEdges(ConnectedEdges);
		for (int32 EdgeI = 0; EdgeI < ConnectedEdges.Num(); ++EdgeI)
		{
			TSharedRef<FTopologicalEdge> Edge = ConnectedEdges[EdgeI].Pin()->GetLinkActiveEdge();
			if (Edge->HasMarker1())
			{
				continue;
			}
			TSharedRef<FTopologicalVertex> EndVertex = Edge->GetOtherVertex(Vertex.ToSharedRef())->GetLinkActiveEntity();

			for (int32 EdgeJ = EdgeI; EdgeJ < ConnectedEdges.Num(); ++EdgeJ)
			{
				TSharedRef<FTopologicalEdge> OtherEdge = ConnectedEdges[EdgeJ].Pin()->GetLinkActiveEdge();
				if (OtherEdge == Edge)
				{
					continue;
				}
				if (OtherEdge->HasMarker1())
				{
					continue;
				}
				TSharedRef<FTopologicalVertex> OtherEdgeEndVertex = OtherEdge->GetOtherVertex(Vertex.ToSharedRef())->GetLinkActiveEntity();

				if (OtherEdgeEndVertex == EndVertex)
				{
					FPoint StartTangentEdge;
					Edge->GetTangentAt(Vertex, StartTangentEdge);
					FPoint StartTangentOtherEdge;
					OtherEdge->GetTangentAt(Vertex, StartTangentOtherEdge);

					double CosAngle = StartTangentEdge.ComputeCosinus(StartTangentOtherEdge);
					if (CosAngle < 0.9)
					{
						continue;
					}

					if (Edge->GetFace() != OtherEdge->GetFace())
					{
						Edge->Link(OtherEdge);
						Edge->GetLinkActiveEdge()->SetMarker1();
						ProcessedEdges.Add(Edge->GetLinkActiveEdge());
					}
				}
			}
		}
	}

	for (TSharedPtr<FTopologicalEdge> Edge : ProcessedEdges)
	{
		Edge->ResetMarker1();
	}
#endif
}
