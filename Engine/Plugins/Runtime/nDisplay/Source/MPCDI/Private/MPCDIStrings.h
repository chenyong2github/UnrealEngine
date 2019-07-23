// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace DisplayClusterStrings
{
	namespace cfg
	{
		namespace data
		{
			namespace mpcdi
			{
				static constexpr auto File = TEXT("file");
				static constexpr auto Buffer = TEXT("buffer");
				static constexpr auto Region = TEXT("region");
				static constexpr auto Origin = TEXT("origin");

				//Support external files
				static constexpr auto MPCDIType = TEXT("mpcdi");

				static constexpr auto FilePFM = TEXT("pfm");
				static constexpr auto WorldScale = TEXT("scale");
				static constexpr auto UseUnrealAxis = TEXT("ue4space");

				static constexpr auto FileAlpha = TEXT("alpha");
				static constexpr auto AlphaGamma = TEXT("alpha_gamma");

				static constexpr auto FileBeta = TEXT("beta");

				static constexpr auto PFMFileDefaultID = TEXT("@PFM");
			}
		}
	}
};
