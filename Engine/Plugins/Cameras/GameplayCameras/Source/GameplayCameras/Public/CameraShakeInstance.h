// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Camera/CameraShakeBase.h"
#include "CameraShakeInstance.generated.h"

UCLASS(meta=(DisplayName="CameraShake"))
class GAMEPLAYCAMERAS_API UCameraShakeInstance : public UCameraShakeBase
{
	GENERATED_BODY()

public:

	UCameraShakeInstance(const FObjectInitializer& ObjInit);
};

