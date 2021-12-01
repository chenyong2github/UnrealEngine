// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IAudioParameterInterfaceRegistry.h"
#include "MetasoundFrontendDocument.h"
#include "UObject/NameTypes.h"


// Forward Declarations
struct FMetasoundFrontendVersion;

namespace Metasound
{
	namespace Frontend
	{
		namespace SourceInterface
		{
			namespace Inputs
			{
				METASOUNDFRONTEND_API const extern FName OnPlay;
			}

			namespace Outputs
			{
				METASOUNDFRONTEND_API const extern FName OnFinished;
			}

			namespace Environment
			{
				METASOUNDFRONTEND_API const extern FName DeviceID;
				METASOUNDFRONTEND_API const extern FName GraphName;
				METASOUNDFRONTEND_API const extern FName IsPreview;
				METASOUNDFRONTEND_API const extern FName SoundUniqueID;
				METASOUNDFRONTEND_API const extern FName TransmitterID;
			}

			METASOUNDFRONTEND_API const FMetasoundFrontendVersion& GetVersion();
			METASOUNDFRONTEND_API Audio::FParameterInterfacePtr CreateInterface(const UClass& InClass);
		}
	} // namespace Frontend
} // namespace Metasound
