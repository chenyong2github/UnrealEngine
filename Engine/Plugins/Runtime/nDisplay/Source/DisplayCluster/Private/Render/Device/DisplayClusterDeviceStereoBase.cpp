// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Render/Device/DisplayClusterDeviceStereoBase.h"
#include "DisplayClusterLog.h"


FDisplayClusterDeviceStereoBase::FDisplayClusterDeviceStereoBase()
	: FDisplayClusterDeviceBase(2)
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}

FDisplayClusterDeviceStereoBase::~FDisplayClusterDeviceStereoBase()
{
	DISPLAY_CLUSTER_FUNC_TRACE(LogDisplayClusterRender);
}
