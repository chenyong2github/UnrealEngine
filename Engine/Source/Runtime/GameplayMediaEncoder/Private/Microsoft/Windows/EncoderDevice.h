// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayMediaEncoderCommon.h"

class FEncoderDevice
{
public:
	FEncoderDevice();

	TRefCountPtr<ID3D11Device> Device;
	TRefCountPtr<ID3D11DeviceContext> DeviceContext;
};

