// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "SimplifiedParsingClassInfo.h"
#include "UnrealSourceFile.h"

// UHTLite NOTE: Provided to ease debugging during refactoring.
#define UHT_USE_PARALLEL_FOR 0

struct FHeaderParserNames
{
	static const FName NAME_IsConversionRoot;
	static const FName NAME_HideCategories;
	static const FName NAME_ShowCategories;
	static const FName NAME_SparseClassDataTypes;
	static const FName NAME_AdvancedClassDisplay;
};

struct FPerHeaderData
{
	TSharedPtr<FUnrealSourceFile> UnrealSourceFile;
	TArray<FHeaderProvider> DependsOn;
	TArray<FSimplifiedParsingClassInfo> ParsedClassArray;
};
