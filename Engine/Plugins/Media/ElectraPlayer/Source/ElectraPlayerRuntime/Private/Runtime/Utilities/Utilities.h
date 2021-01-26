// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"



namespace Electra
{

	namespace Utils
	{


		template <typename T>
		inline T AbsoluteValue(T Value)
		{
			return Value >= T(0) ? Value : -Value;
		}

		template <typename T>
		inline T Min(T a, T b)
		{
			return a < b ? a : b;
		}

		template <typename T>
		inline T Max(T a, T b)
		{
			return a > b ? a : b;
		}


	} // namespace Utils

} // namespace Electra

