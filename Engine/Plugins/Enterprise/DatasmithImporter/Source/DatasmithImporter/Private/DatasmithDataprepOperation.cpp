// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithDataprepOperation.h"

#include "DatasmithBlueprintLibrary.h"
#include "Utility/DatasmithImporterUtils.h"

#define LOCTEXT_NAMESPACE "DatasmithDataprepOperation"

void UDatasmithComputeLightmapResolutionOperation::OnExecution_Implementation(const FDataprepContext& InContext)
{
	// Collect start time to log amount of time spent to import incoming file
	uint64 StartTime = FPlatformTime::Cycles64();
	int32 ObjectsCount = InContext.Objects.Num();

	UDatasmithStaticMeshBlueprintLibrary::ComputeLightmapResolution( InContext.Objects, false, IdealRatio );

	// Log time spent to import incoming file in minutes and seconds
	double ElapsedSeconds = FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);

	int ElapsedMin = int(ElapsedSeconds / 60.0);
	ElapsedSeconds -= 60.0 * (double)ElapsedMin;
	UE_LOG( LogDatasmithImport, Log, TEXT("Computation of lightmap resolution of %d object(s) took [%d min %.3f s]"), ObjectsCount, ElapsedMin, ElapsedSeconds );
}

#undef LOCTEXT_NAMESPACE
