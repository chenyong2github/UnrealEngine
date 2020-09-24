// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMediaInfo.h"

class IMediaInfoModule : public IMediaInfo
{
public:
	virtual ~IMediaInfoModule() { }

	virtual void Initialize(IMediaModule* MediaModule) = 0;
};
