// Copyright Epic Games, Inc. All Rights Reserved.

#include "Changes/MeshReplacementChange.h"
#include "DynamicMesh3.h"

FMeshReplacementChange::FMeshReplacementChange()
{
}

FMeshReplacementChange::FMeshReplacementChange(TSharedPtr<const FDynamicMesh3> BeforeIn, TSharedPtr<const FDynamicMesh3> AfterIn)
{
	Before = BeforeIn;
	After = AfterIn;
}

void FMeshReplacementChange::Apply(UObject* Object)
{
	IMeshReplacementCommandChangeTarget* ChangeTarget = CastChecked<IMeshReplacementCommandChangeTarget>(Object);
	ChangeTarget->ApplyChange(this, false);

	if (OnChangeAppliedFunc)
	{
		OnChangeAppliedFunc(this, Object, true);
	}
}

void FMeshReplacementChange::Revert(UObject* Object)
{
	IMeshReplacementCommandChangeTarget* ChangeTarget = CastChecked<IMeshReplacementCommandChangeTarget>(Object);
	ChangeTarget->ApplyChange(this, true);

	if (OnChangeAppliedFunc)
	{
		OnChangeAppliedFunc(this, Object, false);
	}
}


FString FMeshReplacementChange::ToString() const
{
	return FString(TEXT("Mesh Change"));
}

