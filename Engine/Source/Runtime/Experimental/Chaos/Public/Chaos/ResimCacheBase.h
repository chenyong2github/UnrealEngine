// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"

namespace Chaos
{

class IResimCacheBase
{
public:
	IResimCacheBase()
	: bIsResimming(false)
	{
	}

	virtual ~IResimCacheBase() = default;
	virtual void Reset() = 0;
	bool IsResimming() const { return bIsResimming; }
	void SetResimming(bool bInResimming) { bIsResimming = bInResimming; }
private:
	bool bIsResimming;
};

} // namespace Chaos
