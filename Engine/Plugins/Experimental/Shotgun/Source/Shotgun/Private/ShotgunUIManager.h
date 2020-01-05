// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FShotgunUIManagerImpl;

class FShotgunUIManager
{
public:
	static void Initialize();
	static void Shutdown();

private:
	static TUniquePtr<FShotgunUIManagerImpl> Instance;
};