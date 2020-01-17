// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum 
{
  DMX_UNIVERSE_SIZE = 512,
  DMX_MAX_CHANNEL_VALUE = 255,
  RDM_UID_WIDTH = 6
};

enum EDMXPortCapability
{
	DMX_PORT_CAPABILITY_NONE,
	DMX_PORT_CAPABILITY_STATIC,
	DMX_PORT_CAPABILITY_FULL,
};

enum EDMXPortDirection
{
	DMX_PORT_UNKNOWN,
	DMX_PORT_OUTPUT,
	DMX_PORT_INPUT
};