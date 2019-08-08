// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"

#if INCLUDE_CHAOS

namespace Chaos
{
	void CleanTrimesh(TArray<FVector>& InOutVertices, TArray<int32>& InOutIndices, TArray<int32>* OutOptFaceRemap);
}

#endif
