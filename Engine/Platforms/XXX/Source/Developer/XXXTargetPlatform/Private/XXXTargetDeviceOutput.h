// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Interfaces/ITargetDeviceOutput.h"


class FXXXTargetDeviceOutput : public ITargetDeviceOutput
{
public:
	bool Init( const FString& HostName, FOutputDevice* Output);
	
private:
};
