// Copyright Epic Games, Inc. All Rights Reserved.

#include "TakeSnapshotArchive.h"

FTakeSnapshotArchive::FTakeSnapshotArchive(FBaseObjectInfo& InObjectInfo)
{
	SetWritableObjectInfo(InObjectInfo);

	SetWantBinaryPropertySerialization(false);
	SetIsTransacting(false);
	SetIsPersistent(true);
	ArNoDelta = true;

	SetIsSaving(true);
}