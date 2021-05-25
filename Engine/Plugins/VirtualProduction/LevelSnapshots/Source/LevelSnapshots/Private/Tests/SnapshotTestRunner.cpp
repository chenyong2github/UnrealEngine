// Copyright Epic Games, Inc. All Rights Reserved.

#include "SnapshotTestRunner.h"

#include "LevelSnapshotsFunctionLibrary.h"
#include "PreviewScene.h"

FName FSnapshotTestRunner::DefaultSnapshotId = FName("DefaultSnapshotId");

FSnapshotTestRunner::FSnapshotTestRunner()
{
	TestWorld = MakeShared<FPreviewScene>(
		FPreviewScene::ConstructionValues()
			.SetEditor(true)
			);
}

FSnapshotTestRunner& FSnapshotTestRunner::ModifyWorld(TFunction<void(UWorld*)> Callback)
{
	Callback(TestWorld->GetWorld());
	return *this;
}

FSnapshotTestRunner& FSnapshotTestRunner::TakeSnapshot(FName SnapshotId)
{
	if (ULevelSnapshot** ExistingSnapshot = Snapshots.Find(SnapshotId))
	{
		(*ExistingSnapshot)->SnapshotWorld(TestWorld->GetWorld());
	}
	else
	{
		Snapshots.Add(
			SnapshotId,
			ULevelSnapshotsFunctionLibrary::TakeLevelSnapshot(TestWorld->GetWorld(), SnapshotId)
			);
	}
	
	return *this;
}

FSnapshotTestRunner& FSnapshotTestRunner::AccessSnapshot(TFunction<void (ULevelSnapshot*)> Callback, FName SnapshotId)
{
	if (ULevelSnapshot** ExistingSnapshot = Snapshots.Find(SnapshotId))
	{
		Callback(*ExistingSnapshot);
	}
	else
	{
		checkNoEntry();
	}

	return *this;
}

FSnapshotTestRunner& FSnapshotTestRunner::ApplySnapshot(TFunction<ULevelSnapshotFilter*()> Callback, FName SnapshotId)
{
	return ApplySnapshot(Callback(), SnapshotId);
}

FSnapshotTestRunner& FSnapshotTestRunner::ApplySnapshot(ULevelSnapshotFilter* Filter, FName SnapshotId)
{
	if (ULevelSnapshot** ExistingSnapshot = Snapshots.Find(SnapshotId))
	{
		ULevelSnapshotsFunctionLibrary::ApplySnapshotToWorld(TestWorld->GetWorld(), *ExistingSnapshot, Filter);
	}
	else
	{
		checkNoEntry();
	}
	
	return *this;
}

FSnapshotTestRunner& FSnapshotTestRunner::ApplySnapshot(TFunction<FPropertySelectionMap()> Callback, FName SnapshotId)
{
	return ApplySnapshot(Callback(), SnapshotId);
}

FSnapshotTestRunner& FSnapshotTestRunner::ApplySnapshot(const FPropertySelectionMap& SelectionSet, FName SnapshotId)
{
	if (ULevelSnapshot** ExistingSnapshot = Snapshots.Find(SnapshotId))
	{
		(*ExistingSnapshot)->ApplySnapshotToWorld(TestWorld->GetWorld(), SelectionSet);
	}
	else
	{
		checkNoEntry();
	}
	
	return *this;
}

FSnapshotTestRunner& FSnapshotTestRunner::RunTest(TFunction<void()> Callback)
{
	Callback();
	return *this;
}
