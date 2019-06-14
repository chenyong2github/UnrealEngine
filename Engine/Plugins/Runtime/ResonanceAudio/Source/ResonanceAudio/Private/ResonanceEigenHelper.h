// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

// This file provides a way to include Eigen/Core without producing static analysis warnings.

#if defined(_MSC_VER) && USING_CODE_ANALYSIS
#pragma warning(push)
#pragma warning(disable:6294) // Ill-defined for-loop:  initial condition does not satisfy test.  Loop body not executed.
#endif
THIRD_PARTY_INCLUDES_START
#include "Eigen/Core"
#include "Eigen/Dense"
THIRD_PARTY_INCLUDES_END
#if defined(_MSC_VER) && USING_CODE_ANALYSIS
#pragma warning(pop)
#endif