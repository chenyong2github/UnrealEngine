// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"

namespace Chaos
{

	class IResimCacheBase
	{
	public:
		virtual ~IResimCacheBase() = default;
		virtual void Reset() = 0;
	};

} // namespace Chaos
