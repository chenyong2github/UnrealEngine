// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SnapshotArchives.h"

/* For saving object */
class FTakeSnapshotArchive : public FObjectSnapshotArchive
{
public:
	
	FTakeSnapshotArchive(FBaseObjectInfo& InObjectInfo);
	
};
