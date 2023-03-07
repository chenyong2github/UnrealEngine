// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IAudioParameterInterfaceRegistry.h"
#include "MetasoundFrontendDocument.h"
#include "UObject/NameTypes.h"


namespace Metasound::Engine
{
	namespace InputFormatMonoInterface
	{
		namespace Inputs
		{
			METASOUNDENGINE_API const extern FName MonoIn;
		} // namespace Inputs

		METASOUNDENGINE_API const FMetasoundFrontendVersion& GetVersion();
		METASOUNDENGINE_API Audio::FParameterInterfacePtr CreateInterface();
		METASOUNDENGINE_API TArray<FMetasoundFrontendInterfaceBinding> CreateOutputBindings();
	} // namespace InputFormatMonoInterface

	namespace InputFormatStereoInterface
	{
		namespace Inputs
		{
			METASOUNDENGINE_API const extern FName LeftIn;
			METASOUNDENGINE_API const extern FName RightIn;
		} // namespace Inputs

		METASOUNDENGINE_API const FMetasoundFrontendVersion& GetVersion();
		METASOUNDENGINE_API Audio::FParameterInterfacePtr CreateInterface();
		METASOUNDENGINE_API TArray<FMetasoundFrontendInterfaceBinding> CreateOutputBindings();
	} // namespace InputFormatStereoInterface
} // namespace Metasound::Engine
