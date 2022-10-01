// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/GenerateMutableSource/GenerateMutableSource.h"


mu::TablePtr GenerateMutableSourceTable(const FString& TableName, const UEdGraphPin* Pin, FMutableGraphGenerationContext& GenerationContext);
