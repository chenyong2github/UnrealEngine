// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Interfaces/ITextureFormatManagerModule.h"

/** Return the Texture Format Manager interface, if it is available, otherwise return nullptr. **/
TEXTUREFORMAT_API class ITextureFormatManagerModule* GetTextureFormatManager();

/** Return the Texture Format Manager interface, fatal error if it is not available. **/
TEXTUREFORMAT_API class ITextureFormatManagerModule& GetTextureFormatManagerRef();
