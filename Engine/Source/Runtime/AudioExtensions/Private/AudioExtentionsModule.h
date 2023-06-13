// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioExtentionsModule.h"

class FAudioExtensionsModule final : public IModuleInterface
{
public:
	static FAudioExtensionsModule* Get(); 
	
	virtual void StartupModule() override;
};