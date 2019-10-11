// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class SWindow;
class FSceneViewport;


struct FRemoteSessionUtils
{
	static void FindSceneViewport(TWeakPtr<SWindow>& OutInputWindow, TWeakPtr<FSceneViewport>& OutSceneViewport);
};
