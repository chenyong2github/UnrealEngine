// Copyright Epic Games, Inc. All Rights Reserved.

#include "ColorCorrectRegionDatabase.h"

FCriticalSection FColorCorrectRegionDatabase::FirstPrimitiveIdCriticalSection;
TMap<const AColorCorrectRegion*, FPrimitiveComponentId> FColorCorrectRegionDatabase::FirstPrimitiveIds;

