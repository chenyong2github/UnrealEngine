// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

#ifndef UNIQUENETID_ESPMODE
#define UNIQUENETID_ESPMODE ESPMode::Fast
#endif

class FUniqueNetId;
struct FUniqueNetIdWrapper;

using FUniqueNetIdPtr = TSharedPtr<const FUniqueNetId, UNIQUENETID_ESPMODE>;
using FUniqueNetIdRef = TSharedRef<const FUniqueNetId, UNIQUENETID_ESPMODE>;
using FUniqueNetIdWeakPtr = TWeakPtr<const FUniqueNetId, UNIQUENETID_ESPMODE>;