// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADKernel/Topo/Body.h"

#include "CADKernel/Topo/Shell.h"

void CADKernel::FBody::AddShell(TSharedRef<FShell> Shell)
{
	Shells.Add(Shell);
	Shell->HostedBy = StaticCastSharedRef<FBody>(AsShared());
}

void CADKernel::FBody::RemoveEmptyShell()
{
	TArray<TSharedPtr<FShell>> NewShells;
	NewShells.Reserve(Shells.Num());
	for (TSharedPtr<FShell> Shell : Shells)
	{
		if (Shell->FaceCount() > 0)
		{
			NewShells.Emplace(Shell);
		}
	}
	Swap(NewShells, Shells);
}

TSharedPtr<CADKernel::FEntityGeom> CADKernel::FBody::ApplyMatrix(const CADKernel::FMatrixH& InMatrix) const 
{
	TArray<TSharedPtr<FShell>> NewShells;
	for (TSharedPtr<FShell> Shell : Shells)
	{
		NewShells.Add(StaticCastSharedPtr<FShell>(Shell->ApplyMatrix(InMatrix)));
	}
	return FEntity::MakeShared<FBody>(NewShells);
}

#ifdef CADKERNEL_DEV
CADKernel::FInfoEntity& CADKernel::FBody::GetInfo(FInfoEntity& Info) const
{
	return FTopologicalEntity::GetInfo(Info).Add(TEXT("shells"), Shells).Add(*this);
}
#endif

