// Copyright Epic Games, Inc. All Rights Reserved.

#include "HeadlessChaos.h"

#include "Chaos/PBDJointConstraintUtilities.h"
#include "ChaosLog.h"

namespace ChaosTest
{
	using namespace Chaos;

	struct SwingTwistCase
	{
	public:
		FVec3 SwingAxis;
		FReal SwingAngleDeg;
		FReal TwistAngleDeg;

		friend std::ostream& operator<<(std::ostream& s, const SwingTwistCase& Case)
		{
			return s << "Twist/Swing: " << Case.TwistAngleDeg << "/" << Case.SwingAngleDeg << " Swing Axis: (" << Case.SwingAxis.X << ", " << Case.SwingAxis.Y << ", " << Case.SwingAxis.Z << ")";
		}
	};

	void TestAnglesDeg(const SwingTwistCase& Case, FReal A0, FReal A1, FReal Tolerance)
	{
		if (!(FMath::IsNearlyEqual(FMath::Abs(A0 - A1), 0.0f, Tolerance) || FMath::IsNearlyEqual(FMath::Abs(A0 - A1), 360.0f, Tolerance)))
		{
			GTEST_FAIL() << "Angle Test Fail: " << A0 << " != " << A1 << " " << Case;
		}
	}

	void TestSwingTwistOrder(const SwingTwistCase& Case)
	{
		FRotation3 SwingRot = FRotation3::FromAxisAngle(Case.SwingAxis, FMath::DegreesToRadians(Case.SwingAngleDeg));

		FVec3 TwistAxis = FVec3(1, 0, 0);
		FRotation3 TwistRot = FRotation3::FromAxisAngle(TwistAxis, FMath::DegreesToRadians(Case.TwistAngleDeg));

		FRotation3 RST = SwingRot * TwistRot;
		FRotation3 RS = SwingRot;

		// Verify that a vector along the X Axis is unaffected by twist
		FVec3 X = FVec3(100, 0, 0);
		FVec3 XST = RST * X;	// Swing and Twist applied
		FVec3 XS = RS * X;		// Just Swing applied

		EXPECT_NEAR(XS.X, XST.X, KINDA_SMALL_NUMBER) << Case;
		EXPECT_NEAR(XS.Y, XST.Y, KINDA_SMALL_NUMBER) << Case;
		EXPECT_NEAR(XS.Z, XST.Z, KINDA_SMALL_NUMBER) << Case;
	}

	void TestSwingTwistDecomposition(const SwingTwistCase& Case)
	{
		FRotation3 SwingRot = FRotation3::FromAxisAngle(Case.SwingAxis, FMath::DegreesToRadians(Case.SwingAngleDeg));

		FVec3 TwistAxis = FVec3(1, 0, 0);
		FRotation3 TwistRot = FRotation3::FromAxisAngle(TwistAxis, FMath::DegreesToRadians(Case.TwistAngleDeg));

		FRotation3 R0 = FRotation3::Identity;
		FRotation3 R1 = R0 * SwingRot * TwistRot;

		FVec3 OutTwistAxis, OutSwingAxisLocal;
		FReal OutTwistAngle, OutSwingAngle;

		FPBDJointUtilities::GetTwistAxisAngle(R0, R1, OutTwistAxis, OutTwistAngle);
		FPBDJointUtilities::GetConeAxisAngleLocal(R0, R1,  1.e-6f, OutSwingAxisLocal, OutSwingAngle);
		FReal OutTwistAngleDeg = FMath::RadiansToDegrees(OutTwistAngle);
		FReal OutSwingAngleDeg = FMath::RadiansToDegrees(OutSwingAngle);

		// Degenerate behavior at 180 degrees
		if (Case.SwingAngleDeg == 180)
		{
			TestAnglesDeg(Case, 180.0f, OutSwingAngleDeg, 0.1f);
			TestAnglesDeg(Case, 0.0f, OutTwistAngleDeg, 0.1f);
			return;
		}

		// If we expect a non-zero swing, make sure we recovered the swing axis
		FReal ExpectedSwingAngleDeg = (FVec3::DotProduct(Case.SwingAxis, OutSwingAxisLocal) >= 0.0f) ? Case.SwingAngleDeg : 360 - Case.SwingAngleDeg;
		if (ExpectedSwingAngleDeg != 0)
		{
			EXPECT_NEAR(FMath::Abs(FVec3::DotProduct(Case.SwingAxis, OutSwingAxisLocal)), 1.0f, 1.e-2f) << Case;
		}

		TestAnglesDeg(Case, ExpectedSwingAngleDeg, OutSwingAngleDeg, 0.1f);
		TestAnglesDeg(Case, Case.TwistAngleDeg, OutTwistAngleDeg, 0.1f);
	}

	GTEST_TEST(JointUtilitiesTests, TestSwingTwistDecomposition)
	{
		FVec3 SwingAxes[] = 
		{
			FVec3(0, 1, 0),
			FVec3(0, 0, 1),
			FVec3(0, 1, 1).GetSafeNormal(),
		};

		//int32 TwistIndex = 3;
		for (int32 TwistIndex = 0; TwistIndex < 360; ++TwistIndex)
		{
			FReal TwistAngleDeg = (FReal)TwistIndex;

			//int32 SwingIndex = 1;
			for (int32 SwingIndex = 0; SwingIndex < 360; ++SwingIndex)
			{
				FReal SwingAngleDeg = (FReal)SwingIndex;

				for (int32 SwingAxisIndex = 0; SwingAxisIndex < UE_ARRAY_COUNT(SwingAxes); ++SwingAxisIndex)
				{
					FVec3 SwingAxis = SwingAxes[SwingAxisIndex];
					TestSwingTwistOrder({ SwingAxis, SwingAngleDeg, TwistAngleDeg });
					TestSwingTwistDecomposition({ SwingAxis, SwingAngleDeg, TwistAngleDeg });
				}
			}
		}
	}

}