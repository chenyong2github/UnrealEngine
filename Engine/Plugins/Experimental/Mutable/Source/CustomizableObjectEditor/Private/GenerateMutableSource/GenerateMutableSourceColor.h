// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenerateMutableSource/GenerateMutableSource.h"

#include "MutableTools/Public/NodeColour.h"

/** Convert a CustomizableObject Source Graph into a mutable source graph. */
mu::NodeColourPtr GenerateMutableSourceColor(const UEdGraphPin* Pin, FMutableGraphGenerationContext& GenerationContext);
