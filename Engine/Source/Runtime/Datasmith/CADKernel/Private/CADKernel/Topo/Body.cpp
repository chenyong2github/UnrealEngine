// Copyright Epic Games, Inc. All Rights Reserved.
#include "CADKernel/Topo/Body.h"

#include "CADKernel/Topo/Shell.h"

using namespace CADKernel;


void FBody::AddShell(TSharedRef<FShell> Shell)
{
	Shells.Add(Shell);
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
	return FTopologicalEntity::GetInfo(Info).Add(TEXT("shells"), Shells);
}
#endif

