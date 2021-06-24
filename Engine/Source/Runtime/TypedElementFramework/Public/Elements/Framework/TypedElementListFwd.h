// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

class FTypedElementList;

using FTypedElementListPtr = TSharedPtr<FTypedElementList>;
using FTypedElementListRef = TSharedRef<FTypedElementList>;

using FTypedElementListConstPtr = TSharedPtr<const FTypedElementList>;
using FTypedElementListConstRef = TSharedRef<const FTypedElementList>;
