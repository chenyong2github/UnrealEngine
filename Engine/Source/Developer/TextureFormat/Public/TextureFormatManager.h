// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Interfaces/ITextureFormatManagerModule.h"
#include "Modules/ModuleManager.h"

/** Return the Texture Format Manager interface, if it is available, otherwise return nullptr. **/
inline ITextureFormatManagerModule* GetTextureFormatManager()
{
	return FModuleManager::LoadModulePtr<ITextureFormatManagerModule>("TextureFormat");
}

/** Return the Texture Format Manager interface, fatal error if it is not available. **/
inline ITextureFormatManagerModule& GetTextureFormatManagerRef()
{
	class ITextureFormatManagerModule* TextureFormatManager = GetTextureFormatManager();
	if (!TextureFormatManager)
	{
		UE_LOG(LogInit, Fatal, TEXT("Texture format manager was requested, but not available."));
		CA_ASSUME( TextureFormatManager != NULL );	// Suppress static analysis warning in unreachable code (fatal error)
	}
	return *TextureFormatManager;
}

