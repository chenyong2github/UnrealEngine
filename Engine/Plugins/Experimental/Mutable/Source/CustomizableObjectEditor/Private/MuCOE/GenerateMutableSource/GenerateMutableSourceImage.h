// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MuCOE/GenerateMutableSource/GenerateMutableSource.h"


mu::ImagePtr ConvertTextureUnrealToMutable(UTexture2D* Texture, const UCustomizableObjectNode* Node, FCustomizableObjectCompiler* Compiler, bool bIsNormalComposite);


mu::NodeImagePtr ResizeToMaxTextureSize(float MaxTextureSize, const UTexture2D* BaseTexture, mu::NodeImageConstantPtr ImageNode);


/** Convert a CustomizableObject Source Graph into a mutable source graph. */
mu::NodeImagePtr GenerateMutableSourceImage(const UEdGraphPin* Pin, FMutableGraphGenerationContext& GenerationContext, float MaxTextureSize);