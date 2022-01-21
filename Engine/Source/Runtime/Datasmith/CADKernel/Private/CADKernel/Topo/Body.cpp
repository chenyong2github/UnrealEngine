// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADKernel/Topo/Body.h"

#include "CADKernel/Topo/Shell.h"
#include "CADKernel/Topo/TopologyReport.h"


namespace CADKernel
{

void FBody::AddShell(TSharedRef<FShell> Shell)
{
	Shells.Add(Shell);
	Shell->HostedBy = this;
}

void FBody::RemoveEmptyShell()
{
	TArray<TSharedPtr<FShell>> NewShells;
	NewShells.Reserve(Shells.Num());
	for (TSharedPtr<FShell> Shell : Shells)
	{
		if (Shell->FaceCount() > 0)
		{
			NewShells.Emplace(Shell);
		}
		else
		{
			Shell->Delete();
		}
	}
	Swap(NewShells, Shells);
}

TSharedPtr<FEntityGeom> FBody::ApplyMatrix(const FMatrixH& InMatrix) const 
{
	TArray<TSharedPtr<FShell>> NewShells;
	for (TSharedPtr<FShell> Shell : Shells)
	{
		NewShells.Add(StaticCastSharedPtr<FShell>(Shell->ApplyMatrix(InMatrix)));
	}
	return FEntity::MakeShared<FBody>(NewShells);
}

#ifdef CADKERNEL_DEV
FInfoEntity& FBody::GetInfo(FInfoEntity& Info) const
{
	return FTopologicalEntity::GetInfo(Info).Add(TEXT("shells"), Shells).Add(*this);
}
#endif

void FBody::FillTopologyReport(FTopologyReport& Report) const
{
	Report.Add(this);
	for (TSharedPtr<FShell> Shell : Shells)
	{
		Shell->FillTopologyReport(Report);
	}
}

}
