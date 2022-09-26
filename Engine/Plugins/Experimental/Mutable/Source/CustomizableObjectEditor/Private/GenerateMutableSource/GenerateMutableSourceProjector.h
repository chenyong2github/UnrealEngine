// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "GenerateMutableSource/GenerateMutableSource.h"


/** Convert a CustomizableObject Source Graph into a mutable source graph. */
mu::NodeProjectorPtr GenerateMutableSourceProjector(const UEdGraphPin* Pin, FMutableGraphGenerationContext& GenerationContext);
