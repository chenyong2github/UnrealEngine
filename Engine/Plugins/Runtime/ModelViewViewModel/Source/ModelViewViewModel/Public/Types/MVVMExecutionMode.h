// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVMExecutionMode.generated.h"


/** */
UENUM()
enum class EMVVMExecutionMode : uint8
{
	/** Execute the binding as soon as the source value changes. */
	Immediate,
	/** Execute the binding at the end of the frame before drawing when the source value changes. */
	Delayed,
	/** Always execute the binding at the end of the frame. */
	Tick,
};