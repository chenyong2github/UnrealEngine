// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "TestCommon/ApplicationCoreUtilities.h"
#include "TestCommon/CoreUtilities.h"
#include "TestCommon/CoreUObjectUtilities.h"
#include "TestCommon/EditorUtilities.h"
#include "TestCommon/EngineUtilities.h"


void InitAllThreadPoolsEditorEx(bool MultiThreaded);

void InitOutputDevicesEx();

void InitStats();

void InitAll(bool bAllowLogging, bool bMultithreaded);

void CleanupAll();