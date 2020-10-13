// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/Views/IDisplayClusterConfiguratorBuilder.h"


class IDisplayClusterConfiguratorViewportBuilder
	: public IDisplayClusterConfiguratorBuilder
{
public:
	virtual void BuildViewport() = 0;

	virtual void ClearViewportSelection() = 0;
};
