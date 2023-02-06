// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "CoreTypes.h"
#include "Modules/ModuleManager.h"

TCHAR GInternalProjectName[64] = TEXT("LowLevelTests");
const TCHAR* GForeignEngineDir = nullptr;

#if !EXPLICIT_TESTS_TARGET
bool GIsGameAgnosticExe = false;
#endif

// Typical defined by TargetRules but LowLevelTestRunner is not setup correctly
// Should revist this in the future
#ifndef IMPLEMENT_ENCRYPTION_KEY_REGISTRATION
	#define IMPLEMENT_ENCRYPTION_KEY_REGISTRATION()
#endif
#ifndef IMPLEMENT_SIGNING_KEY_REGISTRATION
	#define IMPLEMENT_SIGNING_KEY_REGISTRATION()
#endif

// Debug visualizers and new operator overloads
PER_MODULE_BOILERPLATE