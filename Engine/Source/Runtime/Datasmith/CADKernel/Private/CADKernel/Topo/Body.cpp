// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADKernel/Topo/Body.h"

#include "CADKernel/Topo/Model.h"
#include "CADKernel/Topo/Shell.h"
#include "CADKernel/Topo/TopologyReport.h"


namespace CADKernel
{

void FBody::AddShell(TSharedRef<FShell> Shell)
{
	Shells.Add(Shell);
	Shell->SetHost(this);
}

void FBody::RemoveEmptyShell(FModel& Model)
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
	if (NewShells.IsEmpty())
	{
		Delete();
		Model.RemoveBody(this);
	}
	else
	{
		Swap(NewShells, Shells);
	}
}

#ifdef CADKERNEL_DEV
FInfoEntity& FBody::GetInfo(FInfoEntity& Info) const
{
	return FTopologicalShapeEntity::GetInfo(Info).Add(TEXT("Shells"), Shells);
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
