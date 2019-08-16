// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DisplayClusterGameModeDefault.h"
#include "DisplayClusterPawnDefault.h"

#include "DisplayClusterGlobals.h"
#include "DisplayClusterLog.h"


ADisplayClusterGameModeDefault::ADisplayClusterGameModeDefault() :
	Super()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterGame);

	if (!bIsDisplayClusterActive)
	{
		return;
	}
	
	DefaultPawnClass = ADisplayClusterPawnDefault::StaticClass();
}

ADisplayClusterGameModeDefault::~ADisplayClusterGameModeDefault()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterGame);
}

