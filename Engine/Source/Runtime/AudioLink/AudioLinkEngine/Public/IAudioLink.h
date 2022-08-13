// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

/** IAudioLink
  *	Abstract interface for AudioLink instances. 
  *	Purely opaque.
  */
class AUDIOLINKENGINE_API IAudioLink
{
protected:
	IAudioLink();
public:
	virtual ~IAudioLink() = default;
};

