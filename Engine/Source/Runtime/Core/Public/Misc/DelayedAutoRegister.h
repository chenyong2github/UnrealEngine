// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

template <typename FuncType> class TFunction;

enum class EDelayedRegisterRunPhase : uint8
{
	StartOfEnginePreInit,
	FileSystemReady,
	TaskGraphSystemReady,
	StatSystemReady,
	IniSystemReady,
	EarliestPossiblePluginsLoaded,
	ShaderTypesReady,
	PreObjectSystemReady,
	ObjectSystemReady,
	DeviceProfileManagerReady,
	EndOfEngineInit,

	NumPhases,
};

struct CORE_API FDelayedAutoRegisterHelper
{

	FDelayedAutoRegisterHelper(EDelayedRegisterRunPhase RunPhase, TFunction<void()> RegistrationFunction);

	static void RunAndClearDelayedAutoRegisterDelegates(EDelayedRegisterRunPhase RunPhase);
};

#if UE_ENABLE_INCLUDE_ORDER_DEPRECATED_IN_5_3
#include "Templates/Function.h"
#endif
