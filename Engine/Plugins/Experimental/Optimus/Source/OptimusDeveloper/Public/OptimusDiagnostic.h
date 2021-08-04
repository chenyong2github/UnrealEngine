// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OptimusDiagnostic.generated.h"

UENUM()
enum class EOptimusDiagnosticLevel : uint8
{
	Ignore,
	Info,
	Warning,
	Error
};
