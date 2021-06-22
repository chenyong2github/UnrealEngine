// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#ifndef WITH_STRUCTUTILS_DEBUG
#define WITH_STRUCTUTILS_DEBUG (!(UE_BUILD_SHIPPING || UE_BUILD_SHIPPING_WITH_EDITOR || UE_BUILD_TEST) && 1)
#endif // WITH_STRUCTUTILS_DEBUG