// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenerateMutableSource/GenerateMutableSource.h"


mu::TablePtr GenerateMutableSourceTable(const FString& TableName, const UEdGraphPin* Pin, FMutableGraphGenerationContext& GenerationContext);
