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

