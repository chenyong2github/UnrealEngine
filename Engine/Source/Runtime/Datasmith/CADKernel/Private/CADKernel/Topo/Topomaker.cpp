// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Topo/Topomaker.h"

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
#include "CADKernel/Topo/TopologicalShapeEntity.h"
#include "CADKernel/UI/Display.h"
#include "CADKernel/UI/Message.h"
#include "CADKernel/Utils/Util.h"

namespace CADKernel
{

FTopomaker::FTopomaker(FSession& InSession, double InTolerance)
	: Session(InSession)
	, SewTolerance(InTolerance* UE_SQRT_2)
	, SewToleranceSquare(FMath::Square(SewTolerance))
{
	int32 ShellCount = 0;
	for (const TSharedPtr<FBody>& Body : Session.GetModel().GetBodies())
	{
		ShellCount += Body->GetShells().Num();
	}
	Shells.Reserve(ShellCount);

	for (const TSharedPtr<FBody>& Body : Session.GetModel().GetBodies())
	{
		for (const TSharedPtr<FShell>& Shell : Body->GetShells())
		{
			Shells.Add(Shell.Get());
		}
	}

	InitFaces();
}

FTopomaker::FTopomaker(FSession& InSession, const TArray<TSharedPtr<FTopologicalFace>>& InFaces, double InTolerance)
	: Session(InSession)
	, Faces(InFaces)
	, SewTolerance(InTolerance * UE_SQRT_2)
	, SewToleranceSquare(FMath::Square(SewTolerance))
{
}

FTopomaker::FTopomaker(FSession& InSession, const TArray<TSharedPtr<FShell>>& InShells, double InTolerance)
	: Session(InSession)
	, SewTolerance(InTolerance)
	, SewToleranceSquare(FMath::Square(SewTolerance))
{
	Shells.Reserve(InShells.Num());
	for(const TSharedPtr<FShell>& Shell : InShells)
	{
		Shells.Add(Shell.Get());
	}
	InitFaces();
}

void FTopomaker::InitFaces()
{
	int32 FaceCount = 0;
	for (FShell* Shell : Shells)
	{
		Shell->CompleteMetadata();
		FaceCount += Shell->FaceCount();
	}
	Faces.Reserve(FaceCount);

	for (FShell* Shell : Shells)
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

void FTopomaker::RemoveFacesFromShell()
{
	// remove faces from their shells
	TSet<FShell*> ShellSet;
	for (const TSharedPtr<FTopologicalFace>& Face : Faces)
	{
		if (Face->GetHost() != nullptr)
		{
			ShellSet.Add((FShell*) Face->GetHost());
			Face->ResetHost();
		}	
	}

	for (FShell* Shell : ShellSet)
	{
		bool bIsOutter = Shell->IsOutter();

		TArray<FOrientedFace> ShellFace;
		ShellFace.Reserve(Shell->FaceCount());
		for (const FOrientedFace& Face : Shell->GetFaces())
		{
			if (!Face.Entity->GetHost())
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

void FTopomaker::EmptyShells()
{
	for (FShell* Shell : Shells)
	{
		Shell->Empty();
	}
}

void FTopomaker::Sew()
{
	FTimePoint StartJoinTime = FChrono::Now();

	TArray<FTopologicalVertex*> BorderVertices;
	GetBorderVertices(BorderVertices);
	MergeCoincidentVertices(BorderVertices);

	CheckSelfConnectedEdge();

	MergeCoincidentEdges(BorderVertices);

	MergeUnconnectedAdjacentEdges();

	Report.SewDuration = FChrono::Elapse(StartJoinTime);
	FChrono::PrintClockElapse(EVerboseLevel::Log, TEXT(""), TEXT("Sew"), Report.SewDuration);
}

void FTopomaker::GetVertices(TArray<FTopologicalVertex*>& Vertices)
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
					Vertices.Add(&*Edge->GetStartVertex()->GetLinkActiveEntity());
				}
				if (!Edge->GetEndVertex()->GetLinkActiveEntity()->HasMarker1())
				{
					Edge->GetEndVertex()->GetLinkActiveEntity()->SetMarker1();
					Vertices.Add(&*Edge->GetEndVertex()->GetLinkActiveEntity());
				}
			}
		}
	}
	for (FTopologicalVertex* Vertex : Vertices)
	{
		Vertex->ResetMarker1();
	}
}

void FTopomaker::GetBorderVertices(TArray<FTopologicalVertex*>& BorderVertices)
{
	TArray<FTopologicalVertex*> Vertices;
	GetVertices(Vertices);

	BorderVertices.Empty(Vertices.Num());

	for (FTopologicalVertex* Vertex : Vertices)
	{
		if (Vertex->IsBorderVertex())
		{
			BorderVertices.Add(Vertex);
		}
	}
}

void FTopomaker::MergeCoincidentVertices(TArray<FTopologicalVertex*>& VerticesToMerge)
{
	FTimePoint StartTime = FChrono::Now();

	const double JoiningVerticesToleranceSquare = 2 * SewToleranceSquare;
	const double WeigthTolerance = 3 * SewTolerance;

	int32 VertexNum = (int32)VerticesToMerge.Num();

	TArray<double> VerticesWeight;
	VerticesWeight.Reserve(VertexNum);

	TArray<int32> SortedVertexIndices;
	SortedVertexIndices.Reserve(VertexNum);

	for (FTopologicalVertex* Vertex : VerticesToMerge)
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
		FTopologicalVertex* Vertex = VerticesToMerge[SortedVertexIndices[IndexI]];
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
			FTopologicalVertex* OtherVertex = VerticesToMerge[SortedVertexIndices[IndexJ]];
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
				Vertex->Link(*OtherVertex);
				Barycenter = Vertex->GetBarycenter();
			}
		}
	}

	for (FTopologicalVertex* Vertex : VerticesToMerge)
	{
		Vertex->ResetMarker1();
	}

	TArray<FTopologicalVertex*> ActiveVertices;
	ActiveVertices.Reserve(VertexNum);

	for (FTopologicalVertex* Vertex : VerticesToMerge)
	{
		FTopologicalVertex& ActiveVertex = *Vertex->GetLinkActiveEntity();
		if (ActiveVertex.HasMarker1())
		{
			continue;
		}
		ActiveVertex.SetMarker1();
		ActiveVertices.Add(&ActiveVertex);
	}

	for (FTopologicalVertex* Vertex : ActiveVertices)
	{
		Vertex->ResetMarker1();
	}

	Swap(ActiveVertices, VerticesToMerge);

	FDuration StepDuration = FChrono::Elapse(StartTime);
	FChrono::PrintClockElapse(EVerboseLevel::Log, TEXT("    "), TEXT("Merge Coincident vertices"), StepDuration);

}

void FTopomaker::MergeBorderVerticesWithCoincidentOtherVertices(TArray<FTopologicalVertex*>& Vertices)
{
	int32 VertexNum = (int32)Vertices.Num();

	TArray<double> VerticesWeight;
	VerticesWeight.Reserve(VertexNum);
	TArray<int32> SortedVertexIndices;
	SortedVertexIndices.Reserve(VertexNum);

	for (FTopologicalVertex* Vertex : Vertices)
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
		FTopologicalVertex* Vertex = Vertices[SortedVertexIndices[IndexI]];
		if (!Vertex->IsBorderVertex())
		{
			continue;
		}

		double VertexWeigth = VerticesWeight[SortedVertexIndices[IndexI]];
		FPoint Barycenter = Vertex->GetBarycenter();
		for (int32 IndexJ = StartIndexJ; IndexJ < VertexNum; ++IndexJ)
		{
			FTopologicalVertex& OtherVertex = *Vertices[SortedVertexIndices[IndexJ]];
			if (Vertex->GetLink() == OtherVertex.GetLink())
			{
				continue;
			}

			double OtherVertexWeigth = VerticesWeight[SortedVertexIndices[IndexJ]];

			if (OtherVertexWeigth + SewTolerance - VertexWeigth < 0)
			{
				StartIndexJ = IndexJ;
				continue;
			}

			if (OtherVertexWeigth - SewTolerance - VertexWeigth > 0)
			{
				break;
			}

			double DistanceSqr = OtherVertex.GetLinkActiveEntity()->SquareDistance(Barycenter);
			if (DistanceSqr < SewToleranceSquare)
			{
				TArray<FTopologicalEdge*> CommonEdges;
				Vertex->GetConnectedEdges(OtherVertex, CommonEdges);
				if (CommonEdges.Num() > 0)
				{
					continue;
				}

				OtherVertex.SetMarker1();
				Vertex->Link(OtherVertex);
				Barycenter = Vertex->GetBarycenter();
			}
		}
	}

	TArray<FTopologicalVertex*> NewVertices;
	NewVertices.Empty(VertexNum);

	for (FTopologicalVertex* Vertex : Vertices)
	{
		FTopologicalVertex& ActiveVertex = *Vertex->GetLinkActiveEntity();
		if (ActiveVertex.HasMarker1())
		{
			continue;
		}
		ActiveVertex.SetMarker1();
		NewVertices.Add(&ActiveVertex);
	}

	for (FTopologicalVertex* Vertex : NewVertices)
	{
		Vertex->ResetMarker1();
	}

	Swap(NewVertices, Vertices);
}

void FTopomaker::MergeCoincidentEdges(TArray<FTopologicalVertex*>& VerticesToProcess)
{
	FTimePoint StartTime = FChrono::Now();

	for (FTopologicalVertex* Vertex : VerticesToProcess)
	{
		TArray<FTopologicalEdge*> ConnectedEdges;
		Vertex->GetConnectedEdges(ConnectedEdges);
		int32 ConnectedEdgeCount = ConnectedEdges.Num();
		if (ConnectedEdgeCount == 1)
		{
			continue;
		}

		for (int32 EdgeI = 0; EdgeI < ConnectedEdgeCount - 1; ++EdgeI)
		{
			FTopologicalEdge* Edge = ConnectedEdges[EdgeI];
			if (!Edge->IsActiveEntity())
			{
				continue;
			}
			bool bFirstEdgeBorder = Edge->IsBorder();
			FTopologicalVertex& EndVertex = *Edge->GetOtherVertex(*Vertex)->GetLinkActiveEntity();

			for (int32 EdgeJ = EdgeI + 1; EdgeJ < ConnectedEdgeCount; ++EdgeJ)
			{
				FTopologicalEdge* SecondEdge = ConnectedEdges[EdgeJ];
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

				FTopologicalVertex& OtherEdgeEndVertex = *SecondEdge->GetOtherVertex(*Vertex)->GetLinkActiveEntity();

				if (&OtherEdgeEndVertex == &EndVertex)
				{
					FPoint StartTangentEdge = Edge->GetTangentAt(*Vertex);
					FPoint StartTangentOtherEdge = SecondEdge->GetTangentAt(*Vertex);

					double CosAngle = StartTangentEdge.ComputeCosinus(StartTangentOtherEdge);
					if (CosAngle < 0.9)
					{
						continue;
					}

					if (Edge->GetFace() != SecondEdge->GetFace())
					{
						Edge->Link(*SecondEdge, SewTolerance);
					}
				}
			}
		}
	}

	FDuration StepDuration = FChrono::Elapse(StartTime);

	FChrono::PrintClockElapse(EVerboseLevel::Log, TEXT("    "), TEXT("Merge coincident edges"), StepDuration);

}

TSharedPtr<FTopologicalVertex> FTopomaker::SplitAndLink(FTopologicalVertex& StartVertex, FTopologicalEdge& EdgeToLink, FTopologicalEdge& EdgeToSplit)
{
	FTopologicalVertex* VertexToLink = EdgeToLink.GetOtherVertex(StartVertex);

	FPoint ProjectedPoint;
	double UProjectedPoint = EdgeToSplit.ProjectPoint(VertexToLink->GetBarycenter(), ProjectedPoint);

	double SquareDistanceToProjectedPoint = ProjectedPoint.SquareDistance(VertexToLink->GetBarycenter());
	if (SquareDistanceToProjectedPoint > SewToleranceSquare)
	{
		return TSharedPtr<FTopologicalVertex>();
	}

	// Check if the ProjectedPoint is not nearly equal edge boundary
	FTopologicalVertex* EndVertex = EdgeToSplit.GetOtherVertex(StartVertex);
	if (EndVertex->SquareDistance(ProjectedPoint) < SewToleranceSquare)
	{
		VertexToLink->Link(*EndVertex);
		EdgeToLink.Link(EdgeToSplit, SewTolerance);
		// TSharedPtr<FTopologicalVertex>() is returned as EndVertex is not new
		return TSharedPtr<FTopologicalVertex>();
	}

	// JoinParallelEdges process all edges connected to startVertex (ConnectedEdges).
	// Connected edges must remain compliant i.e. all edges of ConnectedEdges must be connected to StartVertex
	// EdgeToSplit->SplitAt() must keep EdgeToSplit connected to StartVertex
	bool bKeepStartVertexConnectivity = (StartVertex.GetLink() == EdgeToSplit.GetStartVertex()->GetLink());

	TSharedPtr<FTopologicalEdge> NewEdge;
	TSharedPtr<FTopologicalVertex> NewVertex = EdgeToSplit.SplitAt(UProjectedPoint, ProjectedPoint, bKeepStartVertexConnectivity, NewEdge);
	if (!NewVertex.IsValid())
	{
		return TSharedPtr<FTopologicalVertex>();
	}

	VertexToLink->Link(*NewVertex);
	EdgeToLink.Link(EdgeToSplit, SewTolerance);

	return NewVertex;
}

void FTopomaker::StitchParallelEdges(TArray<FTopologicalVertex*>& VerticesToProcess)
{
	FTimePoint StartTime = FChrono::Now();

	for (int32 VertexI = 0; VertexI < VerticesToProcess.Num(); ++VertexI)
	{
		FTopologicalVertex* Vertex = VerticesToProcess[VertexI];

		if (!Vertex->IsBorderVertex())
		{
			continue;
		}

		TArray<FTopologicalEdge*> ConnectedEdges;
		Vertex->GetConnectedEdges(ConnectedEdges);
		int32 ConnectedEdgeCount = ConnectedEdges.Num();
		if (ConnectedEdgeCount == 1)
		{
			continue;
		}

		for (int32 EdgeI = 0; EdgeI < ConnectedEdgeCount - 1; ++EdgeI)
		{
			FTopologicalEdge* Edge = ConnectedEdges[EdgeI];
			ensureCADKernel(Edge->GetLoop() != nullptr);

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
				FTopologicalEdge* SecondEdge = ConnectedEdges[EdgeJ];
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
				if (!bFirstEdgeBorder && !bSecondEdgeBorder)
				{
					continue;
				}

				FPoint StartTangentEdge = Edge->GetTangentAt(*Vertex);
				FPoint StartTangentOtherEdge = SecondEdge->GetTangentAt(*Vertex);

				double CosAngle = StartTangentEdge.ComputeCosinus(StartTangentOtherEdge);
				if (CosAngle < 0.9)
				{
					continue;
				}

				FTopologicalVertex& EndVertex = *Edge->GetOtherVertex(*Vertex)->GetLinkActiveEntity();
				FTopologicalVertex& OtherEdgeEndVertex = *SecondEdge->GetOtherVertex(*Vertex)->GetLinkActiveEntity();
				if (&EndVertex == &OtherEdgeEndVertex)
				{
					Edge->Link(*SecondEdge, SewTolerance);
				}
				else
				{
					double EdgeLength = Edge->Length();
					double OtherEdgeLength = SecondEdge->Length();

					TSharedPtr<FTopologicalVertex> NewVertex;
					if (EdgeLength < OtherEdgeLength)
					{
						NewVertex = SplitAndLink(*Vertex, *Edge, *SecondEdge);
					}
					else
					{
						NewVertex = SplitAndLink(*Vertex, *SecondEdge, *Edge);
					}

					if (NewVertex.IsValid())
					{
						VerticesToProcess.Add(NewVertex.Get());
					}
				}
			}
		}
	}

	FDuration StepDuration = FChrono::Elapse(StartTime);
	FChrono::PrintClockElapse(EVerboseLevel::Log, TEXT("    "), TEXT("Stitch Parallel Edges"), StepDuration);
}

void FTopomaker::MergeUnconnectedAdjacentEdges()
{
	FTimePoint StartTime = FChrono::Now();

	for (TSharedPtr<FTopologicalFace> FacePtr : Faces)
	{
		if (!FacePtr.IsValid())
		{
			continue;
		}

		FTopologicalFace* Face = FacePtr.Get();

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
				if (Edge.Entity->GetTwinEntityCount() == 1)
				{
					TSharedPtr<FTopologicalVertex> EndVertex = Edge.Direction == EOrientation::Front ? Edge.Entity->GetEndVertex() : Edge.Entity->GetStartVertex();

					TArray<FTopologicalEdge*> ConnectedEdges;
					EndVertex->GetConnectedEdges(ConnectedEdges);

					bool bEdgeIsNotTheLast = false;
					if (ConnectedEdges.Num() == 2)
					{
						// check if the edges are tangents
						FPoint StartTangentEdge = ConnectedEdges[0]->GetTangentAt(*EndVertex);
						FPoint StartTangentOtherEdge = ConnectedEdges[1]->GetTangentAt(*EndVertex);

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

				FPoint StartTangentEdge = Candidates[0].Entity->GetTangentAt(*StartVertex);

				TArray<FTopologicalEdge*> Edges;
				StartVertex->GetConnectedEdges(*EndVertex, Edges);

				// Remove edge that is not parallel edge
				FTopologicalEdge* ParallelEdge = nullptr;
				for (FTopologicalEdge*& Edge : Edges)
				{
					if (!Edge)
					{
						continue;
					}

					if (Edge->GetFace() == Face)
					{
						Edge = nullptr;
						continue;
					}
					
					FPoint StartTangentOtherEdge = Edge->GetTangentAt(*StartVertex);
					double CosAngle = StartTangentEdge.ComputeCosinus(StartTangentOtherEdge);
					if (CosAngle > 0.9)
					{
						ParallelEdge = Edge;
						break;
					}
				}

				if (ParallelEdge)
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
					for (FTopologicalEdge* Edge : Edges)
					{
						if (Edge)
						{
							Edge->Link(*NewEdge, SewTolerance);
							break;
						}
					}
				}
			}
		}
	}

	FDuration StepDuration = FChrono::Elapse(StartTime);

	FChrono::PrintClockElapse(EVerboseLevel::Log, TEXT("    "), TEXT("Merge unconnected adjacent edges"), StepDuration);
}

void FTopomaker::RemoveIsolatedEdges()
{
	FTimePoint StartTime = FChrono::Now();

	TArray<FTopologicalEdge*> IsolatedEdges;

	TArray<FTopologicalVertex*> Vertices;
	GetVertices(Vertices);

	for (const FTopologicalVertex* Vertex : Vertices)
	{
		for (const FTopologicalVertex* TwinVertex : Vertex->GetTwinEntities())
		{
			TArray<FTopologicalEdge*> Edges = Vertex->GetDirectConnectedEdges();
			for (FTopologicalEdge* Edge : Edges)
			{
				if (Edge->GetLoop() == nullptr)
				{
					IsolatedEdges.Add(Edge);
				}
			}
		}
	}
	FDuration StepDuration = FChrono::Elapse(StartTime);

	FChrono::PrintClockElapse(EVerboseLevel::Log, TEXT("    "), TEXT("Remove Isolated Edges"), StepDuration);

	FMessage::Printf(EVerboseLevel::Log, TEXT("\n\nIsolatedEdges count %d\n\n\n"), IsolatedEdges.Num());
}

void FTopomaker::UnlinkNonManifoldVertex()
{
	TArray<FTopologicalVertex*> Vertices;
	GetVertices(Vertices);

	TMap<FShell*, int32> ShellToVertexCount;
	for (FTopologicalVertex* Vertex : Vertices)
	{
		ShellToVertexCount.Empty(Vertex->GetTwinEntityCount());
		for (const FTopologicalVertex* TwinVertex : Vertex->GetTwinEntities())
		{
			ensureCADKernel(!TwinVertex->GetDirectConnectedEdges().IsEmpty());
			FTopologicalEdge* Edge = TwinVertex->GetDirectConnectedEdges()[0];
			FShell* Shell = (FShell*) Edge->GetLoop()->GetFace()->GetHost();
			if (Shell != nullptr)
			{
				ShellToVertexCount.FindOrAdd(Shell)++;
			}
		}

		if (ShellToVertexCount.Num() > 1)
		{
			TMap<FShell*, TArray<FTopologicalVertex*>> ShellToVertices;
			ShellToVertices.Reserve(ShellToVertexCount.Num());
			for (TPair<FShell*, int32> Pair : ShellToVertexCount)
			{
				ShellToVertices.FindOrAdd(Pair.Key).Reserve(Pair.Value);
			}

			for (FTopologicalVertex* TwinVertex : Vertex->GetTwinEntities())
			{
				ensureCADKernel(!TwinVertex->GetDirectConnectedEdges().IsEmpty());
				FTopologicalEdge* Edge = TwinVertex->GetDirectConnectedEdges()[0];
				FShell* Shell = (FShell*) Edge->GetLoop()->GetFace()->GetHost();
				if (Shell != nullptr)
				{
					ShellToVertices.FindOrAdd(Shell).Add(TwinVertex);
				}
			}

			Vertex->UnlinkTwinEntities();
			for (TPair<FShell*, TArray<FTopologicalVertex*>> ShellVertices : ShellToVertices)
			{
				if (ShellVertices.Value.Num() > 1)
				{
					TArray<FTopologicalVertex*>& Twins = ShellVertices.Value;
					FTopologicalVertex* FirstVertex = Twins[0];
					for (FTopologicalVertex* TwinVertex : Twins)
					{
						FirstVertex->Link(*TwinVertex);
					}
				}
			}
		}
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

void FTopomaker::CheckSelfConnectedEdge()
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
					if (!Edge->IsDegenerated() && Edge->Length() < 2 * SewTolerance)
					{
						FMessage::Printf(Debug, TEXT("Face %d Edge %d was self connected\n"), Face->GetId(), Edge->GetId());
						Edge->GetStartVertex()->UnlinkTo(*Edge->GetEndVertex());
					}
				}
			}
		}
	}
	FDuration StepDuration = FChrono::Elapse(StartTime);

	FChrono::PrintClockElapse(EVerboseLevel::Log, TEXT("    "), TEXT("Unconnect Self connected edges"), StepDuration);
}

void FTopomaker::SplitIntoConnectedShells()
{
	FTimePoint StartTime = FChrono::Now();

	// Processed1 : Surfaces added in CandidateSurfacesForMesh

	int32 TopologicalFaceCount = Faces.Num();
	// Is closed ?
	// Is one shell ?

	TArray<FFaceSubset> SubShells;

	int32 ProcessFaceCount = 0;

	TArray<FTopologicalFace*> Front;
	TFunction<void(const FTopologicalFace&, FFaceSubset&)> GetNeighboringFaces = [&](const FTopologicalFace& Face, FFaceSubset& Shell)
	{
		for (const TSharedPtr<FTopologicalLoop>& Loop : Face.GetLoops())
		{
			for (const FOrientedEdge& OrientedEdge : Loop->GetEdges())
			{
				const TSharedPtr<FTopologicalEdge>& Edge = OrientedEdge.Entity;
				if (Edge->HasMarker1())
				{
					continue;
				}
				Edge->SetMarker1();

				if (Edge->GetTwinEntityCount() == 1)
				{
					if (!Edge->IsDegenerated())
					{
						Shell.BorderEdgeCount++;
					}
					continue;
				}

				if (Edge->GetTwinEntityCount() > 2)
				{
					Shell.NonManifoldEdgeCount++;
				}

				for (FTopologicalEdge* NextEdge : Edge->GetTwinEntities())
				{
					if (NextEdge->HasMarker1())
					{
						continue;
					}
					NextEdge->SetMarker1();

					FTopologicalFace* NextFace = NextEdge->GetFace();
					if ((NextFace == nullptr) || NextFace->HasMarker1())
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
			FTopologicalFace* Face = Front.Pop();
			if (Face == nullptr)
			{
				continue;
			}

			Shell.Faces.Add(Face);
			GetNeighboringFaces(*Face, Shell);
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
		Front.Add(Face.Get());
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
		TMap<FTopologicalShapeEntity*, int32> BodyToFaceCount;
		TMap<FTopologicalShapeEntity*, int32> ShellToFaceCount;
		TMap<uint32, int32> ColorToFaceCount;
		TMap<FString, int32> NameToFaceCount;

		for (FTopologicalFace* Face : FaceSubset.Faces)
		{
			FTopologicalShapeEntity* Shell = Face->GetHost();
			FTopologicalShapeEntity* Body = Shell->GetHost();

			ShellToFaceCount.FindOrAdd(Shell)++;
			BodyToFaceCount.FindOrAdd(Body)++;
			ColorToFaceCount.FindOrAdd(Face->GetColorId())++;
			NameToFaceCount.FindOrAdd(Face->GetName())++;
		}

		FaceSubset.SetMainShell(ShellToFaceCount);
		FaceSubset.SetMainBody(BodyToFaceCount);
		FaceSubset.SetMainName(NameToFaceCount);
		FaceSubset.SetMainColor(ColorToFaceCount);
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
		if (FaceSubset.MainShell != nullptr)
		{
			FShell* Shell = (FShell*) FaceSubset.MainShell;
			Shell->Empty(FaceSubset.Faces.Num());
			Shell->Add(FaceSubset.Faces);
		}
		else
		{
			FBody* Body = (FBody*) FaceSubset.MainBody;
			if (Body == nullptr)
			{
				TSharedRef<FBody> SharedBody = FEntity::MakeShared<FBody>();
				Session.GetModel().Add(SharedBody);
				Body = &SharedBody.Get();

				Session.SpawnEntityIdent(*Body);
				Body->SetName(FaceSubset.MainName);
				Body->SetColorId(FaceSubset.MainColor);
				Body->SetHostId(Body->GetId());
			}

			TSharedRef<FShell> Shell = FEntity::MakeShared<FShell>();

			Shells.Add(&*Shell);
			Body->AddShell(Shell);
			Session.SpawnEntityIdent(*Shell);

			Shell->Add(FaceSubset.Faces);
			Shell->SetName(FaceSubset.MainName);
			Shell->SetColorId(FaceSubset.MainColor);
			Shell->SetHostId(Shell->GetId());
		}
	}

	UnlinkNonManifoldVertex();

	RemoveEmptyShells();

	FDuration StepDuration = FChrono::Elapse(StartTime);
	FChrono::PrintClockElapse(EVerboseLevel::Log, TEXT(""), TEXT("Split"), StepDuration);
}

void FTopomaker::RemoveEmptyShells()
{
	TArray<FBody*> Bodies;
	Bodies.Reserve(Shells.Num());

	TArray<FShell*> NewShells;

	for (FShell* Shell : Shells)
	{
		if (Shell->FaceCount() == 0)
		{
			FBody* Body = (FBody*) Shell->GetHost();
			if (Body != nullptr && !Body->HasMarker1())
			{
				Body->SetMarker1();
				Bodies.Add(Body);
			}
			Shell->Delete();
		}
		else
		{
			NewShells.Add(Shell);
		}
	}
	Swap(Shells, NewShells);

	FModel& Model = Session.GetModel();
	for (FBody* Body : Bodies)
	{
		Body->RemoveEmptyShell(Model);
		Body->ResetMarkers();
	}
}

void FTopomaker::OrientShells()
{
	FTimePoint StartTime = FChrono::Now();

	for (FShell* Shell : Shells)
	{
		int32 FaceSwapCount = Shell->Orient();
		Report.AddSwappedFaceCount(FaceSwapCount);
	}

	Report.OrientationDuration = FChrono::Elapse(StartTime);
	FChrono::PrintClockElapse(EVerboseLevel::Log, TEXT(""), TEXT("Orient"), Report.OrientationDuration);
}

}
