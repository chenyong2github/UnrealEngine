// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADKernel/Topo/Model.h"

#include "CADKernel/Core/EntityGeom.h"
#include "CADKernel/Topo/Body.h"
#include "CADKernel/Topo/TopologicalFace.h"
#include "CADKernel/Topo/TopologicalEdge.h"
#include "CADKernel/Topo/TopologicalVertex.h"
#include "CADKernel/Topo/TopologicalFace.h"
#include "CADKernel/Topo/Joiner.h"


void CADKernel::FModel::AddEntity(TSharedRef<FTopologicalEntity> Entity)
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

bool CADKernel::FModel::Contains(TSharedPtr<FTopologicalEntity> Entity)
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

void CADKernel::FModel::RemoveEntity(TSharedPtr<FTopologicalEntity> Entity)
{
	switch (Entity->GetEntityType())
	{
	case EEntity::Body:
		RemoveBody(StaticCastSharedPtr<FBody>(Entity));
		break;
	case EEntity::TopologicalFace:
		RemoveFace(StaticCastSharedPtr<FTopologicalFace>(Entity));
		break;
	default:
		break;
	}
}

void CADKernel::FModel::PrintBodyAndShellCount()
{
	int32 NbBody = 0;
	int32 NbShell = 0;

	TArray<TSharedPtr<FBody>> NewBodies;
	for (TSharedPtr<FBody> Body : Bodies)
	{
		NbShell += Body->GetShells().Num();
		NbBody++;
	}
	FMessage::Printf(Log, TEXT("Body count %d shell count %d \n"), NbBody, NbShell);
}

void CADKernel::FModel::RemoveEmptyBodies()
{
	int32 NbBody = 0;
	int32 NbShell = 0;

	TArray<TSharedPtr<FBody>> NewBodies;
	NewBodies.Reserve(Bodies.Num());
	for (TSharedPtr<FBody> Body : Bodies)
	{
		Body->RemoveEmptyShell();
		if (Body->GetShells().Num())
		{
			NbShell += Body->GetShells().Num();
			NewBodies.Emplace(Body);
			NbBody++;
		}
	}
	Swap(NewBodies, Bodies);
	FMessage::Printf(Log, TEXT("After RemoveEmptyBodies, Body count %d shell count %d \n"), NbBody, NbShell);
}



/*void FModel::RemoveDomain(TSharedPtr<FTopologicalFace> Domain)
{
	Faces.Remove(Domain);
}

void FModel::RemoveBody(TSharedPtr<FBody> Body)
{
	Bodies.Remove(Body);
}*/

TSharedPtr<CADKernel::FEntityGeom> CADKernel::FModel::ApplyMatrix(const FMatrixH& InMatrix) const
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

int32 CADKernel::FModel::FaceCount() const
{
	int32 FaceCount = 0;
	for (const TSharedPtr<FBody>& Body : Bodies)
	{
		FaceCount += Body->FaceCount();
	}
	FaceCount += Faces.Num();
	return FaceCount;
}

void CADKernel::FModel::GetFaces(TArray<TSharedPtr<FTopologicalFace>>& OutFaces) 
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

void CADKernel::FModel::SpreadBodyOrientation()
{
	for (TSharedPtr<FBody>& Body : Bodies)
	{
		Body->SpreadBodyOrientation();
	}
}

#ifdef CADKERNEL_DEV
CADKernel::FInfoEntity& CADKernel::FModel::GetInfo(FInfoEntity& Info) const
{
	return FEntityGeom::GetInfo(Info)
		.Add(TEXT("Bodies"), Bodies)
		.Add(TEXT("Domains"), Faces)
		.Add(*this);
}
#endif


// Topo functions
void CADKernel::FModel::MergeInto(TSharedPtr<FBody> Body, TArray<TSharedPtr<FTopologicalEntity>>& InEntities)
{

}


struct FBodyShell
{
	TSharedPtr<CADKernel::FBody> Body;
	TSharedPtr<CADKernel::FShell> Shell;

	FBodyShell(TSharedPtr<CADKernel::FBody> InBody, TSharedPtr<CADKernel::FShell> InShell)
	: Body(InBody)
	, Shell(InShell)
	{
	}

};

//void FModel::CheckTopology()
void CADKernel::FModel::CheckTopology() 
{
	TArray<FBodyShell> IsolatedBodies;
	IsolatedBodies.Reserve(Bodies.Num()*2);

	int32 ShellCount = 0;

	for (TSharedPtr<FBody> Body : Bodies)
	{
		for (TSharedPtr<FShell> Shell : Body->GetShells())
		{
			ShellCount++;
			TArray<FFaceSubset> SubShells;
			Shell->CheckTopology(SubShells);

			if (SubShells.Num() == 1 )
			{
				if (Shell->FaceCount() < 3 )
				{
					IsolatedBodies.Emplace(Body, Shell);
				}
				else
				{
					if (SubShells[0].BorderEdgeCount > 0 || SubShells[0].NonManifoldEdgeCount > 0)
					{
#ifdef CORETECHBRIDGE_DEBUG
						FMessage::Printf(Log, TEXT("Body %d shell %d CADId %d is opened and has %d faces "), Body->GetKioId(), Shell->GetKioId(), Shell->GetId(), Shell->FaceCount());
#else
						FMessage::Printf(Log, TEXT("Body %d shell %d is opened and has %d faces "), Body->GetId(), Shell->GetId(), Shell->FaceCount());
#endif
						FMessage::Printf(Log, TEXT("and has %d border edges and %d nonManifold edges\n"), SubShells[0].BorderEdgeCount, SubShells[0].NonManifoldEdgeCount);
					}
					else
					{
#ifdef CORETECHBRIDGE_DEBUG
						FMessage::Printf(Log, TEXT("Body %d shell %d CADId %d is closed and has %d faces\n"), Body->GetKioId(), Shell->GetKioId(), Shell->GetId(), Shell->FaceCount());
#else
						FMessage::Printf(Log, TEXT("Body %d shell %d is closed and has %d faces\n"), Body->GetId(), Shell->GetId(), Shell->FaceCount());
#endif
					}
				}
			}
			else
			{
#ifdef CORETECHBRIDGE_DEBUG
				FMessage::Printf(Log, TEXT("Body %d shell %d CADId %d has %d subshells\n"), Body->GetKioId(), Shell->GetKioId(), Shell->GetId(), SubShells.Num());
#else
				FMessage::Printf(Log, TEXT("Body %d shell %d has %d subshells\n"), Body->GetId(), Shell->GetId(), SubShells.Num());
#endif
for (const FFaceSubset& Subset : SubShells)
				{
					FMessage::Printf(Log, TEXT("     - Subshell of %d faces %d border edges and %d nonManifold edges\n"), Subset. Faces.Num(), Subset.BorderEdgeCount, Subset.NonManifoldEdgeCount);
				}
			}
		}
	}
}


void CADKernel::FModel::Split(TSharedPtr<FBody> Body, TArray<TSharedPtr<FBody>>& OutNewBody)
{

}

/**
 * Fore each shell of each body, try to stitch topological gap
 */
void CADKernel::FModel::HealModelTopology(double JoiningTolerance)
{

}



//void FModel::FixModelTopology()
void CADKernel::FModel::FixModelTopology(double JoiningTolerance)
{
	//HealModelTopology(JoiningTolerance);

	//// Find Isolated Shell
	//TArray<TSharedPtr<FShell>> IsolatedShell;

	//for (TSharedPtr<FBody> Body : Bodies)
	//{
	//	for (TSharedPtr<FShell> Shell : Body->GetShells())
	//	{
	//		if (Shell->IsOpenShell())
	//		{
	//			IsolatedShell.Add(Shell);
	//		}
	//	}
	//}

	//FJoiner Joiner(IsolatedShell, JoiningTolerance);
	//Joiner.JoinFaces();

	/*
	Body->GetFaces(Surfaces);
	TSharedRef<FBody> MergedBody = FEntity::MakeShared<FBody>();
	TSharedRef<FShell> MergedShell = FEntity::MakeShared<FShell>();

	MergedBody->AddShell(MergedShell);
	TArray<TSharedPtr<FBody>> NewBodies;
	NewBodies.Reserve(Bodies.Num());

	for (TSharedPtr<FBody>& Body : Bodies)
	{
		if (Body->GetShells().Num() == 1)
		{
			TSharedPtr<FShell> Shell = Body->GetShells()[0];
			if (Shell->FaceCount() < 3)
			{
				MergedShell->Merge(Shell);
				Body->Empty();
			}
		}
		else
		{
			NewBodies.Emplace(Body);
			Body.Reset();
		}
	}

	NewBodies.Emplace(MergedBody);

	Swap(NewBodies, Bodies);
	*/
}
