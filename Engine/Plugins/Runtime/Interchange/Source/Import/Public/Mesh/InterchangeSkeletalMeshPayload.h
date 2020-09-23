// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/SkeletalMeshLODImporterData.h"

namespace Interchange
{
	struct FSkeletalMeshPayloadData
	{
		//Currently the skeletalmesh payload data is editor only, we have to move to something available at runtime
#if WITH_EDITOR
		TArray<FSkeletalMeshImportData> SkeletalMeshImportData;
#endif
	};
}
