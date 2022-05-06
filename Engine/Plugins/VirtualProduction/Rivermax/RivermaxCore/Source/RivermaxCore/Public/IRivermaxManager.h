// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace UE::RivermaxCore
{
	/**
	 * Not doing much at the moment but should be used as a central point to register every stream created
	 * and keep track of stats. Also manages initialization of the library.
	 */
	class RIVERMAXCORE_API IRivermaxManager
	{
	public:
		virtual ~IRivermaxManager() = default;

	public:
		
		/** Returns true if Rivermax has been initialized and is usable */
		virtual bool IsInitialized() const = 0;
	};
}

