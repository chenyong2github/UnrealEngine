// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Rendering/SkeletalMeshLODImporterData.h"

namespace Interchange
{
	struct FSkeletalMeshPayloadData
	{
#if WITH_EDITOR
		TArray<FSkeletalMeshImportData> SkeletalMeshImportData;
#endif
	};
}
