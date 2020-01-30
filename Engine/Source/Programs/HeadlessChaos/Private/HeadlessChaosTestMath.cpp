// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaos.h"
#include "HeadlessChaosTestUtility.h"
#include "Chaos/Matrix.h"
#include "Chaos/Utilities.h"

namespace ChaosTest
{
	using namespace Chaos;

	TEST(MathTests, TestMatrixInverse)
	{
		FMath::RandInit(10695676);
		const FReal Tolerance = (FReal)0.001;

		for (int RandIndex = 0; RandIndex < 20; ++RandIndex)
		{
			FMatrix33 M = RandomMatrix(-10, 10);
			FMatrix33 MI = M.Inverse();

			FMatrix33 R = Utilities::Multiply(MI, M);

			EXPECT_TRUE(R.Equals(FMatrix33::Identity, Tolerance));
		}
	}


}
