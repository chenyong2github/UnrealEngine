// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IAudioParameterInterfaceRegistry.h"
#include "MetasoundFrontendDocument.h"
#include "UObject/NameTypes.h"

// Forward Declarations
class UClass;

namespace Metasound
{
	namespace Frontend
	{
		namespace OutputFormatMonoInterface
		{
			namespace Outputs
			{
				METASOUNDFRONTEND_API const extern FName MonoOut;
			} // namespace Outputs

			METASOUNDFRONTEND_API const FMetasoundFrontendVersion& GetVersion();
			METASOUNDFRONTEND_API Audio::FParameterInterfacePtr CreateInterface(const UClass& InClass);
		} // namespace OutputFormatMonoInterface

		namespace OutputFormatStereoInterface
		{
			namespace Outputs
			{
				METASOUNDFRONTEND_API const extern FName LeftOut;
				METASOUNDFRONTEND_API const extern FName RightOut;
			} // namespace Outputs

			METASOUNDFRONTEND_API const FMetasoundFrontendVersion& GetVersion();
			METASOUNDFRONTEND_API Audio::FParameterInterfacePtr CreateInterface(const UClass& InClass);
		} // namespace OutputFormatMonoInterface
	} // namespace Frontend
} // namespace Metasound
