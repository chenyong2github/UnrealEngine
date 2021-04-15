// Copyright Epic Games, Inc. All Rights Reserved.

#include "ClassMaps.h"
#include "UnrealHeaderTool.h"
#include "UnrealTypeDefinitionInfo.h"

FUnrealSourceFiles GUnrealSourceFilesMap;
FTypeDefinitionInfoMap GTypeDefinitionInfoMap;
TMap<UFunction*, uint32> GGeneratedCodeHashes;
FRWLock GGeneratedCodeHashesLock;
