// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "Chaos/Utilities.h"
#include "Chaos/Matrix.h"

namespace Chaos
{
	// @todo(ccaulfield): should be in ChaosCore, but that can't actually include its own headers at the moment (e.g., Matrix.h includes headers from Chaos)
	const PMatrix<float, 3, 3> PMatrix<float, 3, 3>::Zero = PMatrix<float, 3, 3>(0, 0, 0);
	const PMatrix<float, 3, 3> PMatrix<float, 3, 3>::Identity = PMatrix<float, 3, 3>(1, 1, 1);
}
