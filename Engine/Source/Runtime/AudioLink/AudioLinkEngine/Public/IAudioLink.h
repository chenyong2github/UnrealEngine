// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** IAudioLink
  *	Abstract interface for AudioLink instances. 
  *	Purely opaque.
  */
class IAudioLink
{
protected:
	IAudioLink() = default;
public:
	virtual ~IAudioLink() = default;
};

