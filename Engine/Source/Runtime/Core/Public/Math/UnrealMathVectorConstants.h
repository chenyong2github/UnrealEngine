// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


namespace GlobalVectorConstants
{
	static const VectorRegister4Float FloatOne = MakeVectorRegisterFloat(1.0f, 1.0f, 1.0f, 1.0f);
	static const VectorRegister4Float FloatZero = MakeVectorRegisterFloat(0.0f, 0.0f, 0.0f, 0.0f);
	static const VectorRegister4Float FloatMinusOne = MakeVectorRegisterFloat(-1.0f, -1.0f, -1.0f, -1.0f);
	static const VectorRegister4Float Float0001 = MakeVectorRegisterFloat( 0.0f, 0.0f, 0.0f, 1.0f );
	static const VectorRegister4Float Float1000 = MakeVectorRegisterFloat( 1.0f, 0.0f, 0.0f, 0.0f );
	static const VectorRegister4Float SmallLengthThreshold = MakeVectorRegisterFloat(1.e-8f, 1.e-8f, 1.e-8f, 1.e-8f);
	static const VectorRegister4Float FloatOneHundredth = MakeVectorRegisterFloat(0.01f, 0.01f, 0.01f, 0.01f);
	static const VectorRegister4Float Float111_Minus1 = MakeVectorRegisterFloat( 1.f, 1.f, 1.f, -1.f );
	static const VectorRegister4Float FloatMinus1_111= MakeVectorRegisterFloat( -1.f, 1.f, 1.f, 1.f );
	static const VectorRegister4Float FloatOneHalf = MakeVectorRegisterFloat( 0.5f, 0.5f, 0.5f, 0.5f );
	static const VectorRegister4Float FloatMinusOneHalf = MakeVectorRegisterFloat( -0.5f, -0.5f, -0.5f, -0.5f );
	static const VectorRegister4Float KindaSmallNumber = MakeVectorRegisterFloat( KINDA_SMALL_NUMBER, KINDA_SMALL_NUMBER, KINDA_SMALL_NUMBER, KINDA_SMALL_NUMBER );
	static const VectorRegister4Float SmallNumber = MakeVectorRegisterFloat( SMALL_NUMBER, SMALL_NUMBER, SMALL_NUMBER, SMALL_NUMBER );
	static const VectorRegister4Float ThreshQuatNormalized = MakeVectorRegisterFloat( THRESH_QUAT_NORMALIZED, THRESH_QUAT_NORMALIZED, THRESH_QUAT_NORMALIZED, THRESH_QUAT_NORMALIZED );
	static const VectorRegister4Float BigNumber = MakeVectorRegisterFloat(BIG_NUMBER, BIG_NUMBER, BIG_NUMBER, BIG_NUMBER);

	static const VectorRegister2Double DoubleOne2d = MakeVectorRegister2Double(1.0, 1.0);
	static const VectorRegister4Double DoubleOne = MakeVectorRegisterDouble(1.0, 1.0, 1.0, 1.0);
	static const VectorRegister4Double DoubleZero = MakeVectorRegisterDouble(0.0, 0.0, 0.0, 0.0);
	static const VectorRegister4Double DoubleMinusOne = MakeVectorRegisterDouble(-1.0, -1.0, -1.0, -1.0);
	static const VectorRegister4Double Double0001 = MakeVectorRegisterDouble(0.0f, 0.0, 0.0, 1.0);
	static const VectorRegister4Double Double1000 = MakeVectorRegisterDouble(1.0, 0.0, 0.0, 0.0);
	static const VectorRegister4Double DoubleSmallLengthThreshold = MakeVectorRegisterDouble(1.e-8, 1.e-8, 1.e-8, 1.e-8);
	static const VectorRegister4Double DoubleOneHundredth = MakeVectorRegisterDouble(0.01, 0.01, 0.01, 0.01);
	static const VectorRegister4Double Double111_Minus1 = MakeVectorRegisterDouble(1., 1., 1., -1.);
	static const VectorRegister4Double DoubleMinus1_111 = MakeVectorRegisterDouble(-1., 1., 1., 1.);
	static const VectorRegister4Double DoubleOneHalf = MakeVectorRegisterDouble(0.5, 0.5, 0.5, 0.5);
	static const VectorRegister4Double DoubleMinusOneHalf = MakeVectorRegisterDouble(-0.5, -0.5, -0.5, -0.5);
	static const VectorRegister4Double DoubleKindaSmallNumber = MakeVectorRegisterDouble(DOUBLE_KINDA_SMALL_NUMBER, DOUBLE_KINDA_SMALL_NUMBER, DOUBLE_KINDA_SMALL_NUMBER, DOUBLE_KINDA_SMALL_NUMBER);
	static const VectorRegister4Double DoubleSmallNumber = MakeVectorRegisterDouble(DOUBLE_SMALL_NUMBER, DOUBLE_SMALL_NUMBER, DOUBLE_SMALL_NUMBER, DOUBLE_SMALL_NUMBER);
	static const VectorRegister4Double DoubleThreshQuatNormalized = MakeVectorRegisterDouble(DOUBLE_THRESH_QUAT_NORMALIZED, DOUBLE_THRESH_QUAT_NORMALIZED, DOUBLE_THRESH_QUAT_NORMALIZED, DOUBLE_THRESH_QUAT_NORMALIZED);
	static const VectorRegister4Double DoubleBigNumber = MakeVectorRegisterDouble(DOUBLE_BIG_NUMBER, DOUBLE_BIG_NUMBER, DOUBLE_BIG_NUMBER, DOUBLE_BIG_NUMBER);

	static const VectorRegister4Int IntOne = MakeVectorRegisterInt(1, 1, 1, 1);
	static const VectorRegister4Int IntZero = MakeVectorRegisterInt(0, 0, 0, 0);
	static const VectorRegister4Int IntMinusOne = MakeVectorRegisterInt(-1, -1, -1, -1);

	/** This is to speed up Quaternion Inverse. Static variable to keep sign of inverse **/
	static const VectorRegister4Float QINV_SIGN_MASK = MakeVectorRegisterFloat( -1.f, -1.f, -1.f, 1.f );
	static const VectorRegister4Double DOUBLE_QINV_SIGN_MASK = MakeVectorRegisterDouble(-1., -1., -1., 1.);

	static const VectorRegister4Float QMULTI_SIGN_MASK0 = MakeVectorRegisterFloat( 1.f, -1.f, 1.f, -1.f );
	static const VectorRegister4Float QMULTI_SIGN_MASK1 = MakeVectorRegisterFloat( 1.f, 1.f, -1.f, -1.f );
	static const VectorRegister4Float QMULTI_SIGN_MASK2 = MakeVectorRegisterFloat( -1.f, 1.f, 1.f, -1.f );
	static const VectorRegister4Double DOUBLE_QMULTI_SIGN_MASK0 = MakeVectorRegisterDouble(1., -1., 1., -1.);
	static const VectorRegister4Double DOUBLE_QMULTI_SIGN_MASK1 = MakeVectorRegisterDouble(1., 1., -1., -1.);
	static const VectorRegister4Double DOUBLE_QMULTI_SIGN_MASK2 = MakeVectorRegisterDouble(-1., 1., 1., -1.);

	static const VectorRegister4Float DEG_TO_RAD = MakeVectorRegister(PI/(180.f), PI/(180.f), PI/(180.f), PI/(180.f));
	static const VectorRegister4Float DEG_TO_RAD_HALF = MakeVectorRegister((PI/180.f)*0.5f, (PI/180.f)*0.5f, (PI/180.f)*0.5f, (PI/180.f)*0.5f);
	static const VectorRegister4Float RAD_TO_DEG = MakeVectorRegister((180.f)/PI, (180.f)/PI, (180.f)/PI, (180.f)/PI);
	static const VectorRegister4Double DOUBLE_DEG_TO_RAD = MakeVectorRegister(DOUBLE_PI/(180.), DOUBLE_PI/(180.), DOUBLE_PI/(180.), DOUBLE_PI/(180.));
	static const VectorRegister4Double DOUBLE_DEG_TO_RAD_HALF = MakeVectorRegister((DOUBLE_PI/180.) * 0.5, (DOUBLE_PI/180.) * 0.5, (DOUBLE_PI/180.) * 0.5, (DOUBLE_PI/180.) * 0.5);
	static const VectorRegister4Double DOUBLE_RAD_TO_DEG = MakeVectorRegister((180.)/DOUBLE_PI, (180.)/DOUBLE_PI, (180.)/DOUBLE_PI, (180.)/DOUBLE_PI);

	/** Bitmask to AND out the XYZ components in a vector */
	static const VectorRegister4Float XYZMask = MakeVectorRegister((uint32)0xffffffff, (uint32)0xffffffff, (uint32)0xffffffff, (uint32)0x00000000);
	static const VectorRegister4Double DoubleXYZMask = MakeVectorRegisterDouble((uint64)0xFFFFFFFFFFFFFFFF, (uint64)0xFFFFFFFFFFFFFFFF, (uint64)0xFFFFFFFFFFFFFFFF, (uint64)0);
	
	/** Bitmask to AND out the 2nd component in a double pair vector */
	static const VectorRegister2Double DoubleZMask = MakeVectorRegister2Double((uint64)0xFFFFFFFFFFFFFFFF, (uint64)0);


	/** Bitmask to AND out the sign bit of each components in a vector */
#define SIGN_BIT ((1 << 31))
	static const VectorRegister4Float SignBit = MakeVectorRegister((uint32)SIGN_BIT, (uint32)SIGN_BIT, (uint32)SIGN_BIT, (uint32)SIGN_BIT);
	static const VectorRegister4Float SignMask = MakeVectorRegister((uint32)(~SIGN_BIT), (uint32)(~SIGN_BIT), (uint32)(~SIGN_BIT), (uint32)(~SIGN_BIT));
	static const VectorRegister4Int IntSignBit = MakeVectorRegisterInt(SIGN_BIT, SIGN_BIT, SIGN_BIT, SIGN_BIT);
	static const VectorRegister4Int IntSignMask = MakeVectorRegisterInt((~SIGN_BIT), (~SIGN_BIT), (~SIGN_BIT), (~SIGN_BIT));
#undef SIGN_BIT
	static const VectorRegister4Float AllMask = MakeVectorRegister(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF);
	static const VectorRegister4Double DoubleAllMask = MakeVectorRegisterDouble((uint64)(0xFFFFFFFFFFFFFFFF), (uint64)(0xFFFFFFFFFFFFFFFF), (uint64)(0xFFFFFFFFFFFFFFFF), (uint64)(0xFFFFFFFFFFFFFFFF));
	static const VectorRegister4Int IntAllMask = MakeVectorRegisterInt(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF);

#define DOUBLE_SIGN_BIT (uint64(1) << uint64(63))
	static const VectorRegister2Double DoubleSignBit2d = MakeVectorRegister2Double((uint64)DOUBLE_SIGN_BIT, (uint64)DOUBLE_SIGN_BIT);
	static const VectorRegister2Double DoubleSignMask2d = MakeVectorRegister2Double((uint64)(~DOUBLE_SIGN_BIT), (uint64)(~DOUBLE_SIGN_BIT));
	static const VectorRegister4Double DoubleSignBit = MakeVectorRegisterDouble((uint64)DOUBLE_SIGN_BIT, (uint64)DOUBLE_SIGN_BIT, (uint64)DOUBLE_SIGN_BIT, (uint64)DOUBLE_SIGN_BIT);
	static const VectorRegister4Double DoubleSignMask = MakeVectorRegisterDouble((uint64)(~DOUBLE_SIGN_BIT), (uint64)(~DOUBLE_SIGN_BIT), (uint64)(~DOUBLE_SIGN_BIT), (uint64)(~DOUBLE_SIGN_BIT));
#undef DOUBLE_SIGN_BIT

	/** Vector full of positive infinity */
	static const VectorRegister4Float FloatInfinity = MakeVectorRegisterFloat((uint32)0x7F800000, (uint32)0x7F800000, (uint32)0x7F800000, (uint32)0x7F800000);
	static const VectorRegister4Double DoubleInfinity = MakeVectorRegisterDouble((uint64)0x7FF0000000000000, (uint64)0x7FF0000000000000, (uint64)0x7FF0000000000000, (uint64)0x7FF0000000000000);

	static const VectorRegister4Float Pi = MakeVectorRegister(PI, PI, PI, PI);
	static const VectorRegister4Float TwoPi = MakeVectorRegister(2.0f*PI, 2.0f*PI, 2.0f*PI, 2.0f*PI);
	static const VectorRegister4Float PiByTwo = MakeVectorRegister(0.5f*PI, 0.5f*PI, 0.5f*PI, 0.5f*PI);
	static const VectorRegister4Float PiByFour = MakeVectorRegister(0.25f*PI, 0.25f*PI, 0.25f*PI, 0.25f*PI);
	static const VectorRegister4Float OneOverPi = MakeVectorRegister(1.0f / PI, 1.0f / PI, 1.0f / PI, 1.0f / PI);
	static const VectorRegister4Float OneOverTwoPi = MakeVectorRegister(1.0f / (2.0f*PI), 1.0f / (2.0f*PI), 1.0f / (2.0f*PI), 1.0f / (2.0f*PI));

	static const VectorRegister4Double DoublePi = MakeVectorRegisterDouble(DOUBLE_PI, DOUBLE_PI, DOUBLE_PI, DOUBLE_PI);
	static const VectorRegister4Double DoubleTwoPi = MakeVectorRegisterDouble(2.0 * DOUBLE_PI, 2.0 * DOUBLE_PI, 2.0 * DOUBLE_PI, 2.0 * DOUBLE_PI);
	static const VectorRegister4Double DoublePiByTwo = MakeVectorRegisterDouble(0.5 * DOUBLE_PI, 0.5 * DOUBLE_PI, 0.5 * DOUBLE_PI, 0.5 * DOUBLE_PI);
	static const VectorRegister4Double DoublePiByFour = MakeVectorRegisterDouble(0.25 * DOUBLE_PI, 0.25 * DOUBLE_PI, 0.25 * DOUBLE_PI, 0.25 * DOUBLE_PI);
	static const VectorRegister4Double DoubleOneOverPi = MakeVectorRegisterDouble(1.0 / DOUBLE_PI, 1.0 / DOUBLE_PI, 1.0 / DOUBLE_PI, 1.0 / DOUBLE_PI);
	static const VectorRegister4Double DoubleOneOverTwoPi = MakeVectorRegisterDouble(1.0 / (2.0 * DOUBLE_PI), 1.0 / (2.0 * DOUBLE_PI), 1.0 / (2.0 * DOUBLE_PI), 1.0 / (2.0 * DOUBLE_PI));

	static const VectorRegister4Float Float255 = MakeVectorRegister(255.0f, 255.0f, 255.0f, 255.0f);
	static const VectorRegister4Float Float127 = MakeVectorRegister(127.0f, 127.0f, 127.0f, 127.0f);
	static const VectorRegister4Float FloatNeg127 = MakeVectorRegister(-127.0f, -127.0f, -127.0f, -127.0f);
	static const VectorRegister4Float Float360 = MakeVectorRegister(360.f, 360.f, 360.f, 360.f);
	static const VectorRegister4Float Float180 = MakeVectorRegister(180.f, 180.f, 180.f, 180.f);

	static const VectorRegister4Double Double255 = MakeVectorRegisterDouble(255.0, 255.0, 255.0, 255.0);
	static const VectorRegister4Double Double127 = MakeVectorRegisterDouble(127.0, 127.0, 127.0, 127.0);
	static const VectorRegister4Double DoubleNeg127 = MakeVectorRegisterDouble(-127.0, -127.0, -127.0, -127.0);
	static const VectorRegister4Double Double360 = MakeVectorRegisterDouble(360., 360., 360., 360.);
	static const VectorRegister4Double Double180 = MakeVectorRegisterDouble(180., 180., 180., 180.);

	// All float numbers greater than or equal to this have no fractional value.
	static const VectorRegister4Float FloatNonFractional = MakeVectorRegister(FLOAT_NON_FRACTIONAL, FLOAT_NON_FRACTIONAL, FLOAT_NON_FRACTIONAL, FLOAT_NON_FRACTIONAL);
	static const VectorRegister4Double DoubleNonFractional = MakeVectorRegisterDouble(DOUBLE_NON_FRACTIONAL, DOUBLE_NON_FRACTIONAL, DOUBLE_NON_FRACTIONAL, DOUBLE_NON_FRACTIONAL);

	static const VectorRegister4Float FloatTwo = MakeVectorRegister(2.0f, 2.0f, 2.0f, 2.0f);
	static const uint32 AlmostTwoBits = 0x3fffffff;
	static const VectorRegister4Float FloatAlmostTwo = MakeVectorRegister(*(float*)&AlmostTwoBits, *(float*)&AlmostTwoBits, *(float*)&AlmostTwoBits, *(float*)&AlmostTwoBits);

	static const VectorRegister4Double DoubleTwo = MakeVectorRegisterDouble(2.0, 2.0, 2.0, 2.0);
	static const uint64 DoubleAlmostTwoBits = 0x3FFFFFFFFFFFFFFF;
	static const VectorRegister4Double DoubleAlmostTwo = MakeVectorRegisterDouble(*(double*)&DoubleAlmostTwoBits, *(double*)&DoubleAlmostTwoBits, *(double*)&DoubleAlmostTwoBits, *(double*)&DoubleAlmostTwoBits);
}
