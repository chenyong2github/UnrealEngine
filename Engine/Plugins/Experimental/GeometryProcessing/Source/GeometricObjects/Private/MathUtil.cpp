// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "MathUtil.h"

#include <cfloat>
#include <cmath>

#include "Math/UnrealMath.h"

template<> GEOMETRICOBJECTS_API const float TMathUtil<float>::Epsilon = FLT_EPSILON;
template<> GEOMETRICOBJECTS_API const float TMathUtil<float>::ZeroTolerance = 1e-06f;
template<> GEOMETRICOBJECTS_API const float TMathUtil<float>::MaxReal = FLT_MAX;
template<> GEOMETRICOBJECTS_API const float TMathUtil<float>::Pi = (float)(4.0*atan(1.0));
template<> GEOMETRICOBJECTS_API const float TMathUtil<float>::FourPi = 4.0f*TMathUtil<float>::Pi;
template<> GEOMETRICOBJECTS_API const float TMathUtil<float>::TwoPi = 2.0f*TMathUtil<float>::Pi;
template<> GEOMETRICOBJECTS_API const float TMathUtil<float>::HalfPi = 0.5f*TMathUtil<float>::Pi;
template<> GEOMETRICOBJECTS_API const float TMathUtil<float>::InvPi = 1.0f / TMathUtil<float>::Pi;
template<> GEOMETRICOBJECTS_API const float TMathUtil<float>::InvTwoPi = 1.0f / TMathUtil<float>::TwoPi;
template<> GEOMETRICOBJECTS_API const float TMathUtil<float>::DegToRad = TMathUtil<float>::Pi / 180.0f;
template<> GEOMETRICOBJECTS_API const float TMathUtil<float>::RadToDeg = 180.0f / TMathUtil<float>::Pi;
//template<> const float TMathUtil<float>::LN_2 = TMathUtil<float>::Log(2.0f);
//template<> const float TMathUtil<float>::LN_10 = TMathUtil<float>::Log(10.0f);
//template<> const float TMathUtil<float>::INV_LN_2 = 1.0f / TMathUtil<float>::LN_2;
//template<> const float TMathUtil<float>::INV_LN_10 = 1.0f / TMathUtil<float>::LN_10;
template<> GEOMETRICOBJECTS_API const float TMathUtil<float>::Sqrt2 = (float)(sqrt(2.0));
template<> GEOMETRICOBJECTS_API const float TMathUtil<float>::InvSqrt2 = 1.0f / TMathUtil<float>::Sqrt2;
template<> GEOMETRICOBJECTS_API const float TMathUtil<float>::Sqrt3 = (float)(sqrt(3.0));
template<> GEOMETRICOBJECTS_API const float TMathUtil<float>::InvSqrt3 = 1.0f / TMathUtil<float>::Sqrt3;

template<> GEOMETRICOBJECTS_API const double TMathUtil<double>::Epsilon = DBL_EPSILON;
template<> GEOMETRICOBJECTS_API const double TMathUtil<double>::ZeroTolerance = 1e-08;
template<> GEOMETRICOBJECTS_API const double TMathUtil<double>::MaxReal = DBL_MAX;
template<> GEOMETRICOBJECTS_API const double TMathUtil<double>::Pi = 4.0*atan(1.0);
template<> GEOMETRICOBJECTS_API const double TMathUtil<double>::FourPi = 4.0*TMathUtil<double>::Pi;
template<> GEOMETRICOBJECTS_API const double TMathUtil<double>::TwoPi = 2.0*TMathUtil<double>::Pi;
template<> GEOMETRICOBJECTS_API const double TMathUtil<double>::HalfPi = 0.5*TMathUtil<double>::Pi;
template<> GEOMETRICOBJECTS_API const double TMathUtil<double>::InvPi = 1.0 / TMathUtil<double>::Pi;
template<> GEOMETRICOBJECTS_API const double TMathUtil<double>::InvTwoPi = 1.0 / TMathUtil<double>::TwoPi;
template<> GEOMETRICOBJECTS_API const double TMathUtil<double>::DegToRad = TMathUtil<double>::Pi / 180.0;
template<> GEOMETRICOBJECTS_API const double TMathUtil<double>::RadToDeg = 180.0 / TMathUtil<double>::Pi;
//template<> const double TMathUtil<double>::LN_2 = TMathUtil<double>::Log(2.0);
//template<> const double TMathUtil<double>::LN_10 = TMathUtil<double>::Log(10.0);
//template<> const double TMathUtil<double>::INV_LN_2 = 1.0 / TMathUtil<double>::LN_2;
//template<> const double TMathUtil<double>::INV_LN_10 = 1.0 / TMathUtil<double>::LN_10;
template<> GEOMETRICOBJECTS_API const double TMathUtil<double>::Sqrt2 = sqrt(2.0);
template<> GEOMETRICOBJECTS_API const double TMathUtil<double>::InvSqrt2 = 1.0f / TMathUtil<float>::Sqrt2;
template<> GEOMETRICOBJECTS_API const double TMathUtil<double>::Sqrt3 = sqrt(3.0);
template<> GEOMETRICOBJECTS_API const double TMathUtil<double>::InvSqrt3 = 1.0f / TMathUtil<float>::Sqrt3;


