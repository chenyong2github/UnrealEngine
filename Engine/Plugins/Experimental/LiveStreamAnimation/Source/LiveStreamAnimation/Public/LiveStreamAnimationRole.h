// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

UENUM(BlueprintType)
enum class ELiveStreamAnimationRole : uint8
{
	Proxy,		//! Subsystem neither creates nor consumes animation data,
				//! but is acting as a Proxy to pass through.

	Processor,	//! Subsystem is consuming animation packets and evaluating
				//! them locally. It also acts as a Proxy.

	Tracker		//! Subsystem is evaluating animation locally and generating
				//! animation packets that can be sent to other connections.
				//! This node will ignore any received packets.
};