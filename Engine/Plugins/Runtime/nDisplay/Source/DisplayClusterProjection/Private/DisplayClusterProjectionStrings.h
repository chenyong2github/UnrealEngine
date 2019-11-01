// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterUtils/DisplayClusterCommonStrings.h"


namespace DisplayClusterStrings
{
	namespace cfg
	{
		namespace data
		{
			namespace projection
			{
				namespace simple
				{
					static constexpr auto Screen = TEXT("screen");
				}

				namespace easyblend
				{
					static constexpr auto File   = TEXT("file");
					static constexpr auto Origin = TEXT("origin");
					static constexpr auto Scale  = TEXT("scale");
				}

				namespace manual
				{
					static constexpr auto Rotation     = TEXT("rot");

					static constexpr auto Matrix       = TEXT("matrix");
					static constexpr auto MatrixLeft   = TEXT("matrix_left");
					static constexpr auto MatrixRight  = TEXT("matrix_right");

					static constexpr auto Frustum      = TEXT("frustum");
					static constexpr auto FrustumLeft  = TEXT("frustum_left");
					static constexpr auto FrustumRight = TEXT("frustum_right");

					static constexpr auto AngleL      = TEXT("l");
					static constexpr auto AngleLeft   = TEXT("left");
					static constexpr auto AngleR      = TEXT("r");
					static constexpr auto AngleRight  = TEXT("right");
					static constexpr auto AngleT      = TEXT("t");
					static constexpr auto AngleTop    = TEXT("top");
					static constexpr auto AngleB      = TEXT("b");
					static constexpr auto AngleBottom = TEXT("bottom");
				}
			}
		}
	}

	namespace projection
	{
		static constexpr auto Camera    = TEXT("camera");
		static constexpr auto Simple    = TEXT("simple");
		static constexpr auto MPCDI     = TEXT("mpcdi");
		static constexpr auto EasyBlend = TEXT("easyblend");
		static constexpr auto Manual    = TEXT("manual");
	}
};
