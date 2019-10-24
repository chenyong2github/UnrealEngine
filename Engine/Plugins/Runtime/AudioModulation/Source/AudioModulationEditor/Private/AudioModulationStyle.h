// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Math/Color.h"


class FAudioModulationStyle
{
public:
	static const FColor GetVolumeBusColor()  { return FColor(33, 183, 0); }
	static const FColor GetPitchBusColor()   { return FColor(181, 21, 0); }
	static const FColor GetLPFBusColor()     { return FColor(0, 156, 183); }
	static const FColor GetHPFBusColor()     { return FColor(94, 237, 183); }
	static const FColor GetControlBusColor() { return FColor(215, 180, 210); }
};