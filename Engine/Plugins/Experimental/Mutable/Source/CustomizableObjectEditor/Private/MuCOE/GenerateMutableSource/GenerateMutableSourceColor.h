// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/GenerateMutableSource/GenerateMutableSource.h"

#include "MuT/NodeColour.h"

/** Convert a CustomizableObject Source Graph into a mutable source graph. */
mu::NodeColourPtr GenerateMutableSourceColor(const UEdGraphPin* Pin, FMutableGraphGenerationContext& GenerationContext);
