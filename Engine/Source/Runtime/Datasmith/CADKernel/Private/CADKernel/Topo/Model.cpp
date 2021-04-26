// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADKernel/Topo/Model.h"

#include "CADKernel/Core/EntityGeom.h"
#include "CADKernel/Topo/Body.h"
#include "CADKernel/Topo/TopologicalFace.h"
#include "CADKernel/Topo/TopologicalEdge.h"
#include "CADKernel/Topo/TopologicalVertex.h"
#include "CADKernel/Topo/TopologicalFace.h"
#include "CADKernel/Topo/Joiner.h"


using namespace CADKernel;


void FModel::AddEntity(TSharedRef<FTopologicalEntity> Entity)
{
	switch (Entity->GetEntityType())
	{
	case EEntity::Body:
		Add(StaticCastSharedRef<FBody>(Entity));
		break;
	case EEntity::TopologicalFace:
		Add(StaticCastSharedRef<FTopologicalFace>(Entity));
		break;
	default:
		break;
	}
}

bool FModel::Contains(TSharedPtr<FTopologicalEntity> Entity)
{
	switch(Entity->GetEntityType())
	{
	case EEntity::Body:
		return Bodies.Find(StaticCastSharedPtr<FBody>(Entity)) != INDEX_NONE;
	case EEntity::TopologicalFace:
		return Faces.Find(StaticCastSharedPtr<FTopologicalFace>(Entity)) != INDEX_NONE;
	default:
		return false;
	}
	return false;
}

void FModel::RemoveEntity(TSharedPtr<FTopologicalEntity> Entity)
{
	switch (Entity->GetEntityType())
	{
	case EEntity::Body:
		RemoveBody(StaticCastSharedPtr<FBody>(Entity));
		break;
	case EEntity::TopologicalFace:
		RemoveDomain(StaticCastSharedPtr<FTopologicalFace>(Entity));
		break;
	default:
		break;
	}
}

void FModel::RemoveDomain(TSharedPtr<FTopologicalFace> Domain)
{
	Faces.Remove(Domain);
}

void FModel::RemoveBody(TSharedPtr<FBody> Body)
{
	Bodies.Remove(Body);
}

TSharedPtr<FEntityGeom> FModel::ApplyMatrix(const FMatrixH& InMatrix) const
{
	TSharedPtr<FModel> Model = FEntity::MakeShared<FModel>();

	for (TSharedPtr<FBody> Body : Bodies)
	{
		TSharedPtr<FBody> TransformedBody = StaticCastSharedPtr<FBody>(Body->ApplyMatrix(InMatrix));
		Model->Add(TransformedBody);
	}

	for (TSharedPtr<FTopologicalFace> Domain : Faces)
	{
		TSharedPtr<FTopologicalFace> TransformedSurface = StaticCastSharedPtr<FTopologicalFace>(Domain->ApplyMatrix(InMatrix));
		Model->Add(TransformedSurface);
	}

	return Model;
}

int32 FModel::FaceCount() const
{
	int32 FaceCount = 0;
	for (const TSharedPtr<FBody>& Body : Bodies)
	{
		FaceCount += Body->FaceCount();
	}
	FaceCount += Faces.Num();
	return FaceCount;
}

void FModel::GetFaces(TArray<TSharedPtr<FTopologicalFace>>& OutFaces) 
{
	for (const TSharedPtr<FBody>& Body : Bodies)
	{
		Body->GetFaces(OutFaces);
	}

	for (TSharedPtr<FTopologicalFace>& Face : Faces)
	{
		if (!Face->HasMarker1())
		{
			OutFaces.Emplace(Face);
			Face->SetMarker1();
		}
	}
}

void FModel::SpreadBodyOrientation()
{
	for (TSharedPtr<FBody>& Body : Bodies)
	{
		Body->SpreadBodyOrientation();
	}
}

#ifdef CADKERNEL_DEV
FInfoEntity& FModel::GetInfo(FInfoEntity& Info) const
{
	return FEntityGeom::GetInfo(Info)
		.Add(TEXT("Bodies"), Bodies)
		.Add(TEXT("Domains"), Faces);
}
#endif

void FModel::Add(TSharedPtr<FTopologicalFace> Face)
{
	Faces.Add(Face);
}

void FModel::Add(TSharedPtr<FBody> Body)
{
	Bodies.Add(Body);
}



