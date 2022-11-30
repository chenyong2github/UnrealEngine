// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FOpenVDBImportChannel
{
	FString Name;
	uint32 Index;
	bool bImport;
};

struct FOpenVDBImportOptions
{
	FOpenVDBImportChannel Density;
};
