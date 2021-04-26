// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADKernel/Topo/Shell.h"

#include "CADKernel/Geo/GeoEnum.h"
#include "CADKernel/Topo/Body.h"
#include "CADKernel/Topo/TopologicalFace.h"

using namespace CADKernel;

FShell::FShell(const TArray<TSharedPtr<FTopologicalFace>>& InTopologicalFaces, bool bIsInnerShell)
	: FTopologicalEntity()
	, TopologicalFaces()
{
	TopologicalFaces.Reserve(InTopologicalFaces.Num());

	TArray<EOrientation> Orientations;
	Orientations.Reserve(InTopologicalFaces.Num());

	for (TSharedPtr<FTopologicalFace> Face : InTopologicalFaces)
	{
		TopologicalFaces.Emplace(Face, EOrientation::Front);
	}

	if (bIsInnerShell)
	{
		SetInner();
	}
}


FShell::FShell(const TArray<TSharedPtr<FTopologicalFace>>& InTopologicalFaces, const TArray<EOrientation>& InOrientations, bool bIsInnerShell)
	: FTopologicalEntity()
{
	ensureCADKernel(InTopologicalFaces.Num() == InOrientations.Num());
	for (int32 Index = 0; Index < InTopologicalFaces.Num(); ++Index)
	{
		TSharedPtr<FTopologicalFace> Face = InTopologicalFaces[Index];
		EOrientation Orientation = InOrientations[Index];
		TopologicalFaces.Emplace(Face, Orientation);
	}

	if (bIsInnerShell)
	{
		SetInner();
	}
}

TSharedPtr<FEntityGeom> FShell::ApplyMatrix(const FMatrixH& InMatrix) const
{
	ensureCADKernel(false);
	return TSharedPtr<FEntityGeom>();
}

void FShell::Add(TSharedRef<FTopologicalFace> InTopologicalFace, EOrientation Orientation)
{
	TSharedPtr<FTopologicalFace> Face = InTopologicalFace;
	TopologicalFaces.Emplace(Face, Orientation);
}

#ifdef CADKERNEL_DEV
FInfoEntity& FShell::GetInfo(FInfoEntity& Info) const
{
	return FEntity::GetInfo(Info)
		.Add(TEXT("TopologicalFaces"), (TArray<TOrientedEntity<FEntity>>&) TopologicalFaces);
}
#endif

void FShell::GetFaces(TArray<TSharedPtr<FTopologicalFace>>& Faces)
{
	for (FOrientedFace& Face : TopologicalFaces)
	{
		if (Face.Entity->HasMarker1())
		{
			continue;
		}

		Faces.Emplace(Face.Entity);
		Face.Entity->SetMarker1();
	}
}

void FShell::SpreadBodyOrientation()
{
	bool bIsOutter = IsOutter();
	for (FOrientedFace& Face : TopologicalFaces)
	{
		if (Face.Entity->HasMarker2())
		{
			// the face is already processed, this should not append...
			continue;
		}

		if (bIsOutter != (Face.Direction == EOrientation::Front))
		{
			Face.Entity->SetBackOriented();
		}
		Face.Entity->SetMarker2();
	}
}

void FShell::CheckTopology(TArray<FFaceSubset>& SubShells)
{
	// Processed1 : Surfaces added in CandidateSurfacesForMesh

	int32 TopologicalFaceCount = FaceCount();
	// Is closed ?
	// Is one shell ?

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

	for (const FOrientedFace& OrientedFace : GetFaces())
	{
		if (OrientedFace.Entity->HasMarker1())
		{
			continue;
		}
	
		FFaceSubset& Shell = SubShells.Emplace_GetRef();
		Shell.Faces.Reserve(TopologicalFaceCount - ProcessFaceCount);
		Front.Empty(TopologicalFaceCount);

		const TSharedPtr<FTopologicalFace>& Face = OrientedFace.Entity;

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
	for (const FOrientedFace& OrientedFace : GetFaces())
	{
		const TSharedPtr<FTopologicalFace>& Face = OrientedFace.Entity;
		Face->ResetMarkers();
		for (const TSharedPtr<FTopologicalLoop>& Loop : Face->GetLoops())
		{
			for (const FOrientedEdge& OrientedEdge : Loop->GetEdges())
			{
				OrientedEdge.Entity->ResetMarker1();
			}
		}
	}
}
