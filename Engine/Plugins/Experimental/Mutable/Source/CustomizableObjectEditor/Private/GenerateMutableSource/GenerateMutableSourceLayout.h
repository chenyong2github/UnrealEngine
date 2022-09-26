// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenerateMutableSource/GenerateMutableSource.h"
#include "NodeLayout.h"


mu::NodeLayoutPtr GenerateMutableSourceLayout(const UEdGraphPin* Pin, FMutableGraphGenerationContext& GenerationContext);
