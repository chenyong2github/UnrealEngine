// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MathUtil.h"

#include "Math/UnrealMath.h"

#include <cfloat>

const float TMathUtilConstants<float>::Epsilon = FLT_EPSILON;
const float TMathUtilConstants<float>::ZeroTolerance = 1e-06f;
const float TMathUtilConstants<float>::MaxReal = FLT_MAX;
const float TMathUtilConstants<float>::Pi = 3.1415926535897932384626433832795f;
const float TMathUtilConstants<float>::FourPi = 4.0f * TMathUtilConstants::Pi;
const float TMathUtilConstants<float>::TwoPi = 2.0f * TMathUtilConstants::Pi;
const float TMathUtilConstants<float>::HalfPi = 0.5f * TMathUtilConstants::Pi;
const float TMathUtilConstants<float>::InvPi = 1.0f / TMathUtilConstants::Pi;
const float TMathUtilConstants<float>::InvTwoPi = 1.0f / TMathUtilConstants::TwoPi;
const float TMathUtilConstants<float>::DegToRad = TMathUtilConstants::Pi / 180.0f;
const float TMathUtilConstants<float>::RadToDeg = 180.0f / TMathUtilConstants::Pi;
const float TMathUtilConstants<float>::Sqrt2 = 1.4142135623730950488016887242097f;
const float TMathUtilConstants<float>::InvSqrt2 = 1.0f / TMathUtilConstants::Sqrt2;
const float TMathUtilConstants<float>::Sqrt3 = 1.7320508075688772935274463415059f;
const float TMathUtilConstants<float>::InvSqrt3 = 1.0f / TMathUtilConstants::Sqrt3;

const double TMathUtilConstants<double>::Epsilon = DBL_EPSILON;
const double TMathUtilConstants<double>::ZeroTolerance = 1e-08;
const double TMathUtilConstants<double>::MaxReal = DBL_MAX;
const double TMathUtilConstants<double>::Pi = 3.1415926535897932384626433832795;
const double TMathUtilConstants<double>::FourPi = 4.0 * Pi;
const double TMathUtilConstants<double>::TwoPi = 2.0 * TMathUtilConstants::Pi;
const double TMathUtilConstants<double>::HalfPi = 0.5 * TMathUtilConstants::Pi;
const double TMathUtilConstants<double>::InvPi = 1.0 / TMathUtilConstants::Pi;
const double TMathUtilConstants<double>::InvTwoPi = 1.0 / TMathUtilConstants::TwoPi;
const double TMathUtilConstants<double>::DegToRad = TMathUtilConstants::Pi / 180.0;
const double TMathUtilConstants<double>::RadToDeg = 180.0 / TMathUtilConstants::Pi;
const double TMathUtilConstants<double>::Sqrt2 = 1.4142135623730950488016887242097;
const double TMathUtilConstants<double>::InvSqrt2 = 1.0 / TMathUtilConstants::Sqrt2;
const double TMathUtilConstants<double>::Sqrt3 = 1.7320508075688772935274463415059;
const double TMathUtilConstants<double>::InvSqrt3 = 1.0 / TMathUtilConstants::Sqrt3;
