// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Topo/Joiner.h"

#include "CADKernel/Core/Chrono.h"
#include "CADKernel/Core/Session.h"
#include "CADKernel/Geo/Curves/RestrictionCurve.h"
#include "CADKernel/Topo/Body.h"
#include "CADKernel/Topo/Model.h"
#include "CADKernel/Topo/Shell.h"
#include "CADKernel/Topo/TopologicalEdge.h"
#include "CADKernel/Topo/TopologicalLink.h"
#include "CADKernel/Topo/TopologicalFace.h"
#include "CADKernel/Topo/TopologicalLoop.h"
#include "CADKernel/Topo/TopologicalVertex.h"
#include "CADKernel/UI/Display.h"
#include "CADKernel/UI/Message.h"
#include "CADKernel/Utils/Util.h"

using namespace CADKernel;

FJoiner::FJoiner(TSharedRef<FSession> InSession, const TArray<TSharedPtr<FTopologicalFace>>& InFaces, double InTolerance)
	: Session(InSession)
	, Faces(InFaces)
	, JoiningTolerance(InTolerance * UE_SQRT_2)
	, JoiningToleranceSquare(FMath::Square(JoiningTolerance))
{
}

void FJoiner::RemoveFacesFromShell()
{
	// remove faces from their shells
	TSet<TWeakPtr<FShell>> ShellSet;
	for (const TSharedPtr<FTopologicalFace>& Face : Faces)
	{
		if (Face->GetHost().IsValid())
		{
			ShellSet.Add(Face->GetHost());
			Face->ResetHost();
		}	
	}
	for (TWeakPtr<FShell> WeakShell : ShellSet)
	{
		TSharedPtr<FShell> Shell = WeakShell.Pin();
		bool bIsOutter = Shell->IsOutter();

		TArray<FOrientedFace> ShellFace;
		ShellFace.Reserve(Shell->FaceCount());
		for (const FOrientedFace& Face : Shell->GetFaces())
		{
			if (!Face.Entity->GetHost().IsValid())
			{
				if (bIsOutter != (Face.Direction == EOrientation::Front))
				{
					Face.Entity->SetBackOriented();
				}
			}
			else
			{
				ShellFace.Emplace(Face);
			}
		}
		Shell->ReplaceFaces(ShellFace);
	}
}

FJoiner::FJoiner(TSharedRef<FSession> InSession, const TArray<TSharedPtr<FShell>>& InShells, double InTolerance)
	: Session(InSession)
	, Shells(InShells)
	, JoiningTolerance(InTolerance)
	, JoiningToleranceSquare(FMath::Square(JoiningTolerance))
{
	int32 FaceCount = 0;
	for (const TSharedPtr<FShell>& Shell : InShells)
	{
		Shell->CompleteMetadata();
		FaceCount += Shell->FaceCount();
	}
	Faces.Reserve(FaceCount);

	for (const TSharedPtr<FShell>& Shell : InShells)
	{
		Shell->SpreadBodyOrientation();
		for (const FOrientedFace& Face : Shell->GetFaces())
		{
			Faces.Add(Face.Entity);
		}
		for (const FOrientedFace& Face : Shell->GetFaces())
		{
			Face.Entity->CompleteMetadata();
		}
	}

	for (const TSharedPtr<FTopologicalFace>& Face : Faces)
	{
		Face->ResetMarker2();
	}
}


void FJoiner::EmptyShells()
{
	for (const TSharedPtr<FShell>& Shell : Shells)
	{
		Shell->Empty();
	}
}

void FJoiner::JoinFaces()
{
	FTimePoint StartJoinTime = FChrono::Now();

	//RemoveIsolatedEdges();

	TArray<TSharedPtr<FTopologicalVertex>> BorderVertices;
	GetBorderVertices(BorderVertices);
	MergeCoincidentVertices(BorderVertices);
	//RemoveIsolatedEdges();

	CheckSelfConnectedEdge();

	//RemoveIsolatedEdges();

	//Wait();
	MergeCoincidentEdges(BorderVertices);

	//Wait();
	MergeUnconnectedAdjacentEdges();

	//StitchParallelEdges(BorderVertices);

	FDuration InitKernelIODuration = FChrono::Elapse(StartJoinTime);
	FChrono::PrintClockElapse(EVerboseLevel::Log, TEXT(""), TEXT("Join"), InitKernelIODuration);
}

void FJoiner::GetVertices(TArray<TSharedPtr<FTopologicalVertex>>& Vertices)
{
	Vertices.Empty(10 * Faces.Num());

	for (TSharedPtr<FTopologicalFace> Face : Faces)
	{
		for (TSharedPtr<FTopologicalLoop> Loop : Face->GetLoops())
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
	for (const TSharedPtr<FTopologicalVertex>& Vertex : Vertices)
	{
		Vertex->ResetMarker1();
	}
}

void FJoiner::GetBorderVertices(TArray<TSharedPtr<FTopologicalVertex>>& BorderVertices)
{
	TArray<TSharedPtr<FTopologicalVertex>> Vertices;
	GetVertices(Vertices);

	BorderVertices.Empty(Vertices.Num());

	for (const TSharedPtr<FTopologicalVertex>& Vertex : Vertices)
	{
		if (Vertex->IsBorderVertex())
		{
			BorderVertices.Add(Vertex);
		}
	}
}

void FJoiner::MergeCoincidentVertices(TArray<TSharedPtr<FTopologicalVertex>>& VerticesToMerge)
{
	FTimePoint StartTime = FChrono::Now();

	const double JoiningVerticesToleranceSquare = 2 * JoiningToleranceSquare;
	const double WeigthTolerance = 3 * JoiningTolerance;

	int32 VertexNum = (int32)VerticesToMerge.Num();

	TArray<double> VerticesWeight;
	VerticesWeight.Reserve(VertexNum);

	TArray<int32> SortedVertexIndices;
	SortedVertexIndices.Reserve(VertexNum);

	for (TSharedPtr<FTopologicalVertex> Vertex : VerticesToMerge)
	{
		VerticesWeight.Add(Vertex->GetCoordinates().X + Vertex->GetCoordinates().Y + Vertex->GetCoordinates().Z);
	}

	for (int32 Index = 0; Index < VerticesToMerge.Num(); ++Index)
	{
		SortedVertexIndices.Add(Index);
	}
	SortedVertexIndices.Sort([&VerticesWeight](const int32& Index1, const int32& Index2) { return VerticesWeight[Index1] < VerticesWeight[Index2]; });

	for (int32 IndexI = 0; IndexI < VertexNum; ++IndexI)
	{
		TSharedPtr<FTopologicalVertex> Vertex = VerticesToMerge[SortedVertexIndices[IndexI]];
		if (Vertex->HasMarker1())
		{
			continue;
		}

		ensureCADKernel(Vertex->IsActiveEntity());

		Vertex->SetMarker1();

		double VertexWeigth = VerticesWeight[SortedVertexIndices[IndexI]];
		FPoint Barycenter = Vertex->GetBarycenter();

		for (int32 IndexJ = IndexI + 1; IndexJ < VertexNum; ++IndexJ)
		{
			TSharedPtr<FTopologicalVertex> OtherVertex = VerticesToMerge[SortedVertexIndices[IndexJ]];
			if (OtherVertex->HasMarker1())
			{
				continue;
			}

			double OtherVertexWeigth = VerticesWeight[SortedVertexIndices[IndexJ]];
			if ((OtherVertexWeigth - VertexWeigth) > WeigthTolerance)
			{
				break;
			}

			double DistanceSqr = OtherVertex->GetLinkActiveEntity()->SquareDistance(Barycenter);
			if (DistanceSqr < JoiningVerticesToleranceSquare)
			{
				OtherVertex->SetMarker1();
				Vertex->Link(OtherVertex.ToSharedRef());
				Barycenter = Vertex->GetBarycenter();
			}
		}
	}

	for (const TSharedPtr<FTopologicalVertex>& Vertex : VerticesToMerge)
	{
		Vertex->ResetMarker1();
	}

	TArray<TSharedPtr<FTopologicalVertex>> ActiveVertices;
	ActiveVertices.Reserve(VertexNum);

	for (const TSharedPtr<FTopologicalVertex>& Vertex : VerticesToMerge)
	{
		TSharedPtr<FTopologicalVertex> ActiveVertex = Vertex->GetLinkActiveEntity();
		if (ActiveVertex->HasMarker1())
		{
			continue;
		}
		ActiveVertex->SetMarker1();
		ActiveVertices.Add(ActiveVertex);
	}

	for (const TSharedPtr<FTopologicalVertex>& Vertex : ActiveVertices)
	{
		Vertex->ResetMarker1();
	}

	Swap(ActiveVertices, VerticesToMerge);

	FDuration Duration = FChrono::Elapse(StartTime);
	FChrono::PrintClockElapse(EVerboseLevel::Log, TEXT("    "), TEXT("Merge Coincident vertices"), Duration);

}

void FJoiner::MergeBorderVerticesWithCoincidentOtherVertices(TArray<TSharedPtr<FTopologicalVertex>>& Vertices)
{
	int32 VertexNum = (int32)Vertices.Num();

	TArray<double> VerticesWeight;
	VerticesWeight.Reserve(VertexNum);
	TArray<int32> SortedVertexIndices;
	SortedVertexIndices.Reserve(VertexNum);

	for (const TSharedPtr<FTopologicalVertex>& Vertex : Vertices)
	{
		VerticesWeight.Add(Vertex->GetCoordinates().X + Vertex->GetCoordinates().Y + Vertex->GetCoordinates().Z);
	}

	for (int32 Index = 0; Index < Vertices.Num(); ++Index)
	{
		SortedVertexIndices.Add(Index);
	}
	SortedVertexIndices.Sort([&VerticesWeight](const int32& Index1, const int32& Index2) { return VerticesWeight[Index1] < VerticesWeight[Index2]; });

	int32 StartIndexJ = 1;
	for (int32 IndexI = 0; IndexI < VertexNum; ++IndexI)
	{
		TSharedRef<FTopologicalVertex> Vertex = Vertices[SortedVertexIndices[IndexI]].ToSharedRef();
		if (!Vertex->IsBorderVertex())
		{
			continue;
		}

		double VertexWeigth = VerticesWeight[SortedVertexIndices[IndexI]];
		FPoint Barycenter = Vertex->GetBarycenter();
		for (int32 IndexJ = StartIndexJ; IndexJ < VertexNum; ++IndexJ)
		{
			TSharedRef<FTopologicalVertex> OtherVertex = Vertices[SortedVertexIndices[IndexJ]].ToSharedRef();
			if (Vertex->GetLink() == OtherVertex->GetLink())
			{
				continue;
			}

			double OtherVertexWeigth = VerticesWeight[SortedVertexIndices[IndexJ]];

			if (OtherVertexWeigth + JoiningTolerance - VertexWeigth < 0)
			{
				StartIndexJ = IndexJ;
				continue;
			}

			if (OtherVertexWeigth - JoiningTolerance - VertexWeigth > 0)
			{
				break;
			}

			double DistanceSqr = OtherVertex->GetLinkActiveEntity()->SquareDistance(Barycenter);
			if (DistanceSqr < JoiningToleranceSquare)
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
			}
		}
	}

	TArray<TSharedPtr<FTopologicalVertex>> NewVertices;
	NewVertices.Empty(VertexNum);

	for (const TSharedPtr<FTopologicalVertex>& Vertex : Vertices)
	{
		const TSharedPtr<FTopologicalVertex>& ActiveVertex = Vertex->GetLinkActiveEntity();
		if (ActiveVertex->HasMarker1())
		{
			continue;
		}
		ActiveVertex->SetMarker1();
		NewVertices.Add(ActiveVertex);
	}

	for (TSharedPtr<FTopologicalVertex> Vertex : NewVertices)
	{
		Vertex->ResetMarker1();
	}

	Swap(NewVertices, Vertices);
}

void FJoiner::MergeCoincidentEdges(TArray<TSharedPtr<FTopologicalVertex>>& VerticesToProcess)
{
	FTimePoint StartTime = FChrono::Now();

	for (TSharedPtr<FTopologicalVertex> VertexPtr : VerticesToProcess)
	{
		ensureCADKernel(VertexPtr.IsValid());
		TSharedRef<FTopologicalVertex> Vertex = VertexPtr.ToSharedRef();

		TArray<TWeakPtr<FTopologicalEdge>> ConnectedEdges;
		Vertex->GetConnectedEdges(ConnectedEdges);
		int32 ConnectedEdgeCount = ConnectedEdges.Num();
		if (ConnectedEdgeCount == 1)
		{
			continue;
		}

		for (int32 EdgeI = 0; EdgeI < ConnectedEdgeCount - 1; ++EdgeI)
		{
			TSharedPtr<FTopologicalEdge> Edge = ConnectedEdges[EdgeI].Pin();
			if (!Edge->IsActiveEntity())
			{
				continue;
			}
			bool bFirstEdgeBorder = Edge->IsBorder();
			TSharedRef<FTopologicalVertex> EndVertex = Edge->GetOtherVertex(Vertex)->GetLinkActiveEntity();

			for (int32 EdgeJ = EdgeI + 1; EdgeJ < ConnectedEdgeCount; ++EdgeJ)
			{
				TSharedPtr<FTopologicalEdge> SecondEdge = ConnectedEdges[EdgeJ].Pin();
				if (!SecondEdge->IsActiveEntity())
				{
					continue;
				}
				bool bSecondEdgeBorder = Edge->IsBorder();

				// Process only if at least one edge is Border
				if (!bFirstEdgeBorder && !bSecondEdgeBorder)
				{
					continue;
				}

				TSharedRef<FTopologicalVertex> OtherEdgeEndVertex = SecondEdge->GetOtherVertex(Vertex)->GetLinkActiveEntity();

				if (OtherEdgeEndVertex == EndVertex)
				{
					FPoint StartTangentEdge = Edge->GetTangentAt(Vertex);
					FPoint StartTangentOtherEdge = SecondEdge->GetTangentAt(Vertex);

					double CosAngle = StartTangentEdge.ComputeCosinus(StartTangentOtherEdge);
					if (CosAngle < 0.9)
					{
						continue;
					}

					if (Edge->GetFace() != SecondEdge->GetFace())
					{
						Edge->Link(SecondEdge.ToSharedRef(), JoiningTolerance);
					}
				}
			}
		}
	}

	FDuration Duration = FChrono::Elapse(StartTime);

	FChrono::PrintClockElapse(EVerboseLevel::Log, TEXT("    "), TEXT("Merge coincident edges"), Duration);

}

TSharedPtr<FTopologicalVertex> FJoiner::SplitAndLink(TSharedRef<FTopologicalVertex>& StartVertex, TSharedPtr<FTopologicalEdge>& EdgeToLink, TSharedPtr<FTopologicalEdge>& EdgeToSplit)
{
	TSharedPtr<FTopologicalVertex> VertexToLink = EdgeToLink->GetOtherVertex(StartVertex);

	FPoint ProjectedPoint;
	double UProjectedPoint = EdgeToSplit->ProjectPoint(VertexToLink->GetBarycenter(), ProjectedPoint);

	double SquareDistanceToProjectedPoint = ProjectedPoint.SquareDistance(VertexToLink->GetBarycenter());
	if (SquareDistanceToProjectedPoint > JoiningToleranceSquare)
	{
		return TSharedPtr<FTopologicalVertex>();
	}

	// Check if the ProjectedPoint is not nearly equal edge boundary
	TSharedPtr<FTopologicalVertex> EndVertex = EdgeToSplit->GetOtherVertex(StartVertex);
	if (EndVertex->SquareDistance(ProjectedPoint) < JoiningToleranceSquare)
	{
		VertexToLink->Link(EndVertex.ToSharedRef());
		EdgeToLink->Link(EdgeToSplit.ToSharedRef(), JoiningTolerance);
		// TSharedPtr<FTopologicalVertex>() is returned as EndVertex is not new
		return TSharedPtr<FTopologicalVertex>();
	}

	// JoinParallelEdges process all edges connected to startVertex (ConnectedEdges).
	// Connected edges must remain compliant i.e. all edges of ConnectedEdges must be connected to StartVertex
	// EdgeToSplit->SplitAt() must keep EdgeToSplit connected to StartVertex
	bool bKeepStartVertexConnectivity = (StartVertex->GetLink() == EdgeToSplit->GetStartVertex()->GetLink());

	TSharedPtr<FTopologicalEdge> NewEdge;
	TSharedPtr<FTopologicalVertex> NewVertex = EdgeToSplit->SplitAt(UProjectedPoint, ProjectedPoint, bKeepStartVertexConnectivity, NewEdge);
	if (!NewVertex.IsValid())
	{
		return TSharedPtr<FTopologicalVertex>();
	}

	VertexToLink->Link(NewVertex.ToSharedRef());
	EdgeToLink->Link(EdgeToSplit.ToSharedRef(), JoiningTolerance);

	return NewVertex;
}

int32 CountSplit = 0;

void FJoiner::StitchParallelEdges(TArray<TSharedPtr<FTopologicalVertex>>& VerticesToProcess)
{
	FTimePoint StartTime = FChrono::Now();

	for (int32 VertexI = 0; VertexI < VerticesToProcess.Num(); ++VertexI)
	{
		TSharedPtr<FTopologicalVertex> VertexPtr = VerticesToProcess[VertexI];
		ensureCADKernel(VertexPtr.IsValid());

		if (!VertexPtr->IsBorderVertex())
		{
			continue;
		}

		TSharedRef<FTopologicalVertex> Vertex = VertexPtr.ToSharedRef();

		TArray<TWeakPtr<FTopologicalEdge>> ConnectedEdges;
		Vertex->GetConnectedEdges(ConnectedEdges);
		int32 ConnectedEdgeCount = ConnectedEdges.Num();
		if (ConnectedEdgeCount == 1)
		{
			continue;
		}

		for (int32 EdgeI = 0; EdgeI < ConnectedEdgeCount - 1; ++EdgeI)
		{
			TSharedPtr<FTopologicalEdge> Edge = ConnectedEdges[EdgeI].Pin();
			ensureCADKernel(Edge->GetLoop().IsValid());

			if (Edge->IsDegenerated())
			{
				continue;
			}

			if (!Edge->IsActiveEntity())
			{
				continue;
			}
			bool bFirstEdgeBorder = Edge->IsBorder();

			for (int32 EdgeJ = EdgeI + 1; EdgeJ < ConnectedEdgeCount; ++EdgeJ)
			{
				TSharedPtr<FTopologicalEdge> SecondEdge = ConnectedEdges[EdgeJ].Pin();
				if (Edge->IsDegenerated())
				{
					continue;
				}
				if (!SecondEdge->IsActiveEntity())
				{
					continue;
				}
				bool bSecondEdgeBorder = Edge->IsBorder();

				// Process only if at least one edge is Border
				if (!bSecondEdgeBorder && !bSecondEdgeBorder)
				{
					continue;
				}

				FPoint StartTangentEdge = Edge->GetTangentAt(Vertex);
				FPoint StartTangentOtherEdge = SecondEdge->GetTangentAt(Vertex);

				double CosAngle = StartTangentEdge.ComputeCosinus(StartTangentOtherEdge);
				if (CosAngle < 0.9)
				{
					continue;
				}

				TSharedRef<FTopologicalVertex> EndVertex = Edge->GetOtherVertex(Vertex)->GetLinkActiveEntity();
				TSharedRef<FTopologicalVertex> OtherEdgeEndVertex = SecondEdge->GetOtherVertex(Vertex)->GetLinkActiveEntity();
				if (EndVertex == OtherEdgeEndVertex)
				{
					Edge->Link(SecondEdge.ToSharedRef(), JoiningTolerance);
				}
				else
				{
					double EdgeLength = Edge->Length();
					double OtherEdgeLength = SecondEdge->Length();
					if (EdgeLength < OtherEdgeLength)
					{
						CountSplit++;
						TSharedPtr<FTopologicalVertex> NewVertex = SplitAndLink(Vertex, Edge, SecondEdge);
						if (NewVertex.IsValid())
						{
							VerticesToProcess.Add(NewVertex);
						}
					}
					else
					{
						CountSplit++;
						TSharedPtr<FTopologicalVertex> NewVertex = SplitAndLink(Vertex, SecondEdge, Edge);
						if (NewVertex.IsValid())
						{
							VerticesToProcess.Add(NewVertex);
						}
					}
				}
			}
		}
	}

	FDuration Duration = FChrono::Elapse(StartTime);
	FChrono::PrintClockElapse(EVerboseLevel::Log, TEXT("    "), TEXT("Stitch Parallel Edges"), Duration);
}

void FJoiner::MergeUnconnectedAdjacentEdges()
{
	FTimePoint StartTime = FChrono::Now();

	for (TSharedPtr<FTopologicalFace> Face : Faces)
	{
		TArray<TArray<FOrientedEdge>> ArrayOfCandidates;
		ArrayOfCandidates.Reserve(10);

		// For each loop, find unconnected successive edges...
		for (TSharedPtr<FTopologicalLoop> Loop : Face->GetLoops())
		{
			TArray<FOrientedEdge>& Edges = Loop->GetEdges();
			int32 EdgeCount = Edges.Num();


			// Find the starting edge i.e. the next edge of the first edge that its ending vertex is connecting to 3 or more edges 
			// The algorithm start to the last edge of the loop, if it verifies the criteria then the first edge is the edges[0]
			int32 EndIndex = EdgeCount;
			{
				TSharedPtr<FTopologicalVertex> EndVertex;
				do
				{
					EndIndex--;
					EndVertex = Edges[EndIndex].Direction == EOrientation::Front ? Edges[EndIndex].Entity->GetEndVertex() : Edges[EndIndex].Entity->GetStartVertex();
				} while (EndVertex->ConnectedEdgeCount() == 2 && EndIndex > 0);
			}
			EndIndex++;

			// First step
			// For the loop, find all arrays of successive unconnected edges
			TArray<FOrientedEdge>* Candidates = &ArrayOfCandidates.Emplace_GetRef();
			Candidates->Reserve(10);
			bool bCanStop = false;
			for (int32 Index = EndIndex; bCanStop == (Index != EndIndex); ++Index)
			{
				if (Index == EdgeCount)
				{
					Index = 0;
				}
				bCanStop = true;

				FOrientedEdge& Edge = Edges[Index];
				if (Edge.Entity->GetTwinsEntityCount() == 1)
				{
					TSharedPtr<FTopologicalVertex> EndVertex = Edge.Direction == EOrientation::Front ? Edge.Entity->GetEndVertex() : Edge.Entity->GetStartVertex();

					TArray<TWeakPtr<FTopologicalEdge>> ConnectedEdges;
					EndVertex->GetConnectedEdges(ConnectedEdges);

					bool bEdgeIsNotTheLast = false;
					if (ConnectedEdges.Num() == 2)
					{
						// check if the edges are tangents
						FPoint StartTangentEdge = ConnectedEdges[0].Pin()->GetTangentAt(EndVertex.ToSharedRef());
						FPoint StartTangentOtherEdge = ConnectedEdges[1].Pin()->GetTangentAt(EndVertex.ToSharedRef());

						double CosAngle = StartTangentEdge.ComputeCosinus(StartTangentOtherEdge);
						if (CosAngle < -0.9)
						{
							bEdgeIsNotTheLast = true;
						}
					}

					if (bEdgeIsNotTheLast || Candidates->Num() > 0)
					{
						Candidates->Add(Edges[Index]);
					}

					if (!bEdgeIsNotTheLast && Candidates->Num() > 0)
					{
						Candidates = &ArrayOfCandidates.Emplace_GetRef();
						Candidates->Reserve(10);
					}
				}
			}
		}

		// Second step, 
		// Each array of edges are merged to generated a single edge, the loop is updated
		for (TArray<FOrientedEdge>& Candidates : ArrayOfCandidates)
		{
			if (Candidates.Num())
			{
				TSharedPtr<FTopologicalVertex> StartVertexPtr = Candidates[0].Direction == EOrientation::Front ? Candidates[0].Entity->GetStartVertex() : Candidates[0].Entity->GetEndVertex();
				TSharedRef<FTopologicalVertex> StartVertex = StartVertexPtr.ToSharedRef();
				TSharedPtr<FTopologicalVertex> EndVertex = Candidates.Last().Direction == EOrientation::Front ? Candidates.Last().Entity->GetEndVertex() : Candidates.Last().Entity->GetStartVertex();

				FPoint StartTangentEdge = Candidates[0].Entity->GetTangentAt(StartVertex);

				TArray<TSharedPtr<FTopologicalEdge>> Edges;
				StartVertex->GetConnectedEdges(EndVertex, Edges);

				// Remove edge that is not parallel edge
				TSharedPtr<FTopologicalEdge> ParallelEdge;
				for (TSharedPtr<FTopologicalEdge>& Edge : Edges)
				{
					if (Edge->GetFace() == Face)
					{
						Edge.Reset();
						continue;
					}
					
					FPoint StartTangentOtherEdge = Edge->GetTangentAt(StartVertex);
					double CosAngle = StartTangentEdge.ComputeCosinus(StartTangentOtherEdge);
					if (CosAngle > 0.9)
					{
						ParallelEdge = Edge;
						break;
					}
				}

				if (ParallelEdge.IsValid())
				{
					//FMessage::Printf(Log, TEXT("Face %d UnconnectedAdjacentEdges\n"), Face->GetId());
					//for (auto Edge : Candidates)
					//{
					//	FMessage::Printf(Log, TEXT("       - %d \n"), Edge.Entity->GetId());
					//}

					TSharedPtr<FTopologicalEdge> NewEdge = FTopologicalEdge::CreateEdgeByMergingEdges(Candidates, StartVertex, EndVertex.ToSharedRef());
					if (!NewEdge.IsValid())
					{
						// the edges cannot be merge, they will be connected to the parallel edge with "SplitAndLink"
						// i.e. instead of merging edges to be one and link to the parallel one
						// the parallel one is split at the extremities of each edges, and each new edge is linked to its parallel edge 
						break;
					}

					// Link to the parallel edge
					// New edge is link to the first parallel one as the other parallel should already be linked together
					for (TSharedPtr<FTopologicalEdge>& Edge : Edges)
					{
						if (Edge.IsValid())
						{
							Edge->Link(NewEdge.ToSharedRef(), JoiningTolerance);
							break;
						}
					}
				}
			}
		}
	}

	FDuration Duration = FChrono::Elapse(StartTime);

	FChrono::PrintClockElapse(EVerboseLevel::Log, TEXT("    "), TEXT("Merge unconnected adjacent edges"), Duration);

}

void FJoiner::RemoveIsolatedEdges()
{
	FTimePoint StartTime = FChrono::Now();

	TArray<TSharedPtr<FTopologicalEdge>> IsolatedEdges;

	TArray<TSharedPtr<FTopologicalVertex>> Vertices;
	GetVertices(Vertices);

	for (const TSharedPtr<FTopologicalVertex>& Vertex : Vertices)
	{
		for (const TWeakPtr<FTopologicalVertex>& TwinVertex : Vertex->GetTwinsEntities())
		{
			TArray<TWeakPtr<FTopologicalEdge>> Edges = Vertex->GetDirectConnectedEdges();
			for (TWeakPtr<FTopologicalEdge> WeakEdge : Edges)
			{
				TSharedPtr<FTopologicalEdge> Edge = WeakEdge.Pin();
				if (!Edge->GetLoop().IsValid())
				{
					IsolatedEdges.Add(Edge);
				}
			}
		}
	}
	FDuration Duration = FChrono::Elapse(StartTime);

	FChrono::PrintClockElapse(EVerboseLevel::Log, TEXT("    "), TEXT("Remove Isolated Edges"), Duration);

	FMessage::Printf(EVerboseLevel::Log, TEXT("\n\nIsolatedEdges count %d\n\n\n"), IsolatedEdges.Num());
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
	FTimePoint StartTime = FChrono::Now();

	FMessage::Printf(Log, TEXT("    Self connected edges\n"));
	for (TSharedPtr<FTopologicalFace> Face : Faces)
	{
		for (TSharedPtr<FTopologicalLoop> Loop : Face->GetLoops())
		{
			for (const FOrientedEdge& OrientedEdge : Loop->GetEdges())
			{
				TSharedPtr<FTopologicalEdge> Edge = OrientedEdge.Entity;
				if (Edge->GetStartVertex()->IsLinkedTo(Edge->GetEndVertex()))
				{
					if (!Edge->IsDegenerated() && Edge->Length() < 2 * JoiningTolerance)
					{
						FMessage::Printf(Debug, TEXT("Face %d Edge %d was self connected, "), Face->GetId(), Edge->GetId());
						Edge->GetStartVertex()->UnlinkTo(Edge->GetEndVertex());
					}
				}
			}
		}
	}
	FDuration Duration = FChrono::Elapse(StartTime);

	FChrono::PrintClockElapse(EVerboseLevel::Log, TEXT("    "), TEXT("Unconnect Self connected edges"), Duration);
}

void FJoiner::SplitIntoConnectedShell()
{
	// Processed1 : Surfaces added in CandidateSurfacesForMesh

	int32 TopologicalFaceCount = Faces.Num();
	// Is closed ?
	// Is one shell ?

	TArray<FFaceSubset> SubShells;

	int32 ProcessFaceCount = 0;

	TArray<TSharedPtr<FTopologicalFace>> Front;
	TFunction<void(const TSharedPtr<FTopologicalFace>&, FFaceSubset&)> GetNeighboringFaces = [&](const TSharedPtr<FTopologicalFace>& Face, FFaceSubset& Shell)
	{
		for (const TSharedPtr<FTopologicalLoop>& Loop : Face->GetLoops())
		{
			for (const FOrientedEdge& OrientedEdge : Loop->GetEdges())
			{
				const TSharedPtr<FTopologicalEdge>& Edge = OrientedEdge.Entity;
				if (Edge->HasMarker1())
				{
					continue;
				}
				Edge->SetMarker1();

				if (Edge->GetTwinsEntityCount() == 1)
				{
					if (!Edge->IsDegenerated())
					{
						Shell.BorderEdgeCount++;
					}
					continue;
				}

				if (Edge->GetTwinsEntityCount() > 2)
				{
					Shell.NonManifoldEdgeCount++;
				}

				for (TWeakPtr<FTopologicalEdge> WeakEdge : Edge->GetTwinsEntities())
				{
					TSharedPtr<FTopologicalEdge> NextEdge = WeakEdge.Pin();
					if (NextEdge->HasMarker1())
					{
						continue;
					}
					NextEdge->SetMarker1();

					TSharedPtr<FTopologicalFace> NextFace = NextEdge->GetFace();
					if (!NextFace.IsValid())
					{
						continue;
					}

					if (NextFace->HasMarker1())
					{
						continue;
					}
					NextFace->SetMarker1();
					Front.Add(NextFace);
				}
			}
		}
	};

	TFunction<void(FFaceSubset&)> SpreadFront = [&](FFaceSubset& Shell)
	{
		while (Front.Num())
		{
			TSharedPtr<FTopologicalFace> Face = Front.Pop();
			Shell.Faces.Add(Face);
			GetNeighboringFaces(Face, Shell);
		}
	};

	for (const TSharedPtr<FTopologicalFace>& Face : Faces)
	{
		if (Face->HasMarker1())
		{
			continue;
		}

		FFaceSubset& Shell = SubShells.Emplace_GetRef();
		Shell.Faces.Reserve(TopologicalFaceCount - ProcessFaceCount);
		Front.Empty(TopologicalFaceCount);

		Face->SetMarker1();
		Front.Add(Face);
		SpreadFront(Shell);
		ProcessFaceCount += Shell.Faces.Num();

		if (ProcessFaceCount == TopologicalFaceCount)
		{
			break;
		}
	}

	// reset Marker
	for (const TSharedPtr<FTopologicalFace>& Face : Faces)
	{
		Face->ResetMarkers();
		for (const TSharedPtr<FTopologicalLoop>& Loop : Face->GetLoops())
		{
			for (const FOrientedEdge& OrientedEdge : Loop->GetEdges())
			{
				OrientedEdge.Entity->ResetMarkers();
			}
		}
	}

	// for each FaceSubset, find the main shell
	for (FFaceSubset& FaceSubset : SubShells)
	{
		TMap<TWeakPtr<FShell>, int32> ShellToFaceCount;
		TMap<uint32, int32> ColorToFaceCount;
		TMap<FString, int32> NameToFaceCount;

		for (const TSharedPtr<FTopologicalFace>& Face : FaceSubset.Faces)
		{
			TWeakPtr<FShell> Shell = Face->GetHost();
			int32* Count = ShellToFaceCount.Find(Shell);
			if (Count)
			{
				(*Count)++;
			}
			else
			{
				ShellToFaceCount.Add(Shell, 1);
			}

			ColorToFaceCount.FindOrAdd(Face->GetColorId())++;
			NameToFaceCount.FindOrAdd(Face->GetName())++;
		}

		int32 MaxInstance = 0;
		TWeakPtr<FShell> MainShell;
		for (TPair<TWeakPtr<FShell>, int32>& Pair : ShellToFaceCount)
		{
			if (Pair.Value > MaxInstance)
			{
				MaxInstance = Pair.Value;
				MainShell = Pair.Key;
			}
		}

		if (MainShell.IsValid() && MainShell.Pin()->FaceCount() / 2 + 1 < MaxInstance)
		{
			FaceSubset.MainShell = MainShell;
		}

		MaxInstance = FaceSubset.Faces.Num()/3;
		FString MainName;
		for (TPair<FString, int32>& Pair : NameToFaceCount)
		{
			if (Pair.Value > MaxInstance)
			{
				MaxInstance = Pair.Value;
				MainName = Pair.Key;
			}
		}

		MaxInstance = 0;
		for (TPair<uint32, int32>& Pair : ColorToFaceCount)
		{
			if (Pair.Value > MaxInstance)
			{
				MaxInstance = Pair.Value;
				FaceSubset.MainColor = Pair.Key;
			}
		}

	}

	if (Shells.Num())
	{
		EmptyShells();
	}
	else
	{
		RemoveFacesFromShell();
	}

	// for each FaceSubset, process the Shell
	for (FFaceSubset FaceSubset : SubShells)
	{
		if (FaceSubset.MainShell.IsValid())
		{
			TSharedPtr<FShell> Shell = FaceSubset.MainShell.Pin();
			Shell->Empty(FaceSubset.Faces.Num());
			Shell->Add(FaceSubset.Faces);
		}
		else
		{
			TSharedRef<FBody> Body = FEntity::MakeShared<FBody>();
			Session->GetModel()->Add(Body);
			TSharedRef<FShell> Shell = FEntity::MakeShared<FShell>();
			Body->AddShell(Shell);
			Body->SetName(FaceSubset.MainName);
			Body->SetColorId(FaceSubset.MainColor);

			Shell->Add(FaceSubset.Faces);
			Shell->SetName(FaceSubset.MainName);
			Shell->SetColorId(FaceSubset.MainColor);
		}
	}

	Session->GetModel()->RemoveEmptyBodies();
}



//void FJoiner::FixCollapsedEdges()
//{
//	TArray<TSharedPtr<FTopologicalEdge>> CollapsedEdges;
//
//	int32 NbDegeneratedEdges = 0;
//	for (const TSharedPtr<FTopologicalFace>& Face : Faces)
//	{
//		for (const TSharedPtr<FTopologicalLoop>& Loop : Face->GetLoops())
//		{
//			for (const FOrientedEdge& OrientedEdge : Loop->GetEdges())
//			{
//				const TSharedPtr<FTopologicalEdge>& Edge = OrientedEdge.Entity;
//				for (const TWeakPtr<FTopologicalEdge>& TwinEdge : Edge->GetTwinsEntities())
//				{
//					TSharedPtr<FTopologicalFace> Face = TwinEdge.Pin()->GetFace();
//					if (!Face.IsValid())
//					{
//						continue;
//					}
//				}
//
//				if (Edge->GetStartVertex()->GetLink() == Edge->GetEndVertex()->GetLink())
//				{
//					if (!Edge->IsDegenerated())
//					{
//						for (const TWeakPtr<FTopologicalEdge>& TwinEdge : Edge->GetTwinsEntities())
//						{
//							TwinEdge.Pin()->SetAsDegenerated();
//						}
//					}
//				}
//			}
//		}
//	}
//
//	for (TSharedPtr<FTopologicalEdge>& Edge : CollapsedEdges)
//	{
//		TSharedPtr<FTopologicalLoop> Loop = Edge->GetLoop();
//		Loop->RemoveEdge(Edge);
//
//		TSharedPtr<FTopologicalVertex> StartVertex = Edge->GetStartVertex();
//		TSharedPtr<FTopologicalVertex> EndVertex = Edge->GetEndVertex();
//
//		Edge->Delete();
//
//		StartVertex->DeleteIfIsolated();
//		EndVertex->DeleteIfIsolated();
//
//		if (Loop->GetEdges().Num() == 0)
//		{
//			TSharedPtr<FTopologicalFace> Face = Loop->GetFace();
//			Face->RemoveLoop(Loop);
//		}
//	}
//}






/*
NewEdgeVertices.Reserve(AllVertices.Num());
for (TSharedPtr<FTopologicalVertex> Vertex : AllVertices)
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
*/


