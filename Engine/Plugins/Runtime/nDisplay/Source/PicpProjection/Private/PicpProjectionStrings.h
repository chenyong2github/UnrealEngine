// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

namespace PicpProjectionStrings
{
	namespace cfg
	{
		namespace data
		{
			namespace projection
			{
				namespace mpcdi
				{
					static constexpr auto File   = TEXT("file");
					static constexpr auto Buffer = TEXT("buffer");
					static constexpr auto Region = TEXT("region");
					static constexpr auto Origin = TEXT("origin");
				}

				namespace mesh
				{
					static constexpr auto FileID   = TEXT("@UESM");
					static constexpr auto BufferID = TEXT("@@Buf");
				}
			}
		}
	}

	namespace projection
	{
		static constexpr auto PicpMPCDI = TEXT("picp_mpcdi");
		static constexpr auto PicpMesh  = TEXT("picp_mesh");
	}
};
