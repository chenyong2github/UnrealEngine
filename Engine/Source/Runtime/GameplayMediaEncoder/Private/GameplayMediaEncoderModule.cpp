// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "GameplayMediaEncoderCommon.h"

GAMEPLAYMEDIAENCODER_START

class FGameplayMediaEncoderModule : public IModuleInterface
{
public:
	FGameplayMediaEncoderModule()
	{
	}

};

IMPLEMENT_MODULE(FGameplayMediaEncoderModule, GameplayMediaEncoder);

GAMEPLAYMEDIAENCODER_END
