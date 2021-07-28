// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundFrontendDocument.h"

namespace Metasound
{
	namespace Engine
	{
		// Base metasound without any required inputs or outputs
		namespace MetasoundV1_0
		{
			FMetasoundFrontendVersion GetVersion();
		}

		// V1.0 of a Metasound Mono Source. Uses FMonoAudioFormat as output.
		namespace MetasoundSourceMonoV1_0
		{
			FMetasoundFrontendVersion GetVersion();
			const FString& GetOnPlayInputName();
			const FString& GetAudioOutputName();
			const FString& GetIsFinishedOutputName();
			const FString& GetAudioDeviceHandleVariableName();
			const FString& GetSoundUniqueIdName();
			const FString& GetIsPreviewSoundName();
		}

		// V1.0 of a Metasound Stereo Source. Uses FStereoAudioFormat as output.
		namespace MetasoundSourceStereoV1_0
		{
			FMetasoundFrontendVersion GetVersion();
			const FString& GetOnPlayInputName();
			const FString& GetAudioOutputName();
			const FString& GetIsFinishedOutputName();
			const FString& GetAudioDeviceHandleVariableName();
			const FString& GetSoundUniqueIdName();
			const FString& GetIsPreviewSoundName();
		}

		// V1.1 of a Metasound Mono source. Uses FAudioBuffer as output.
		namespace MetasoundSourceMonoV1_1
		{
			FMetasoundFrontendVersion GetVersion();
			const FString& GetOnPlayInputName();
			const FString& GetAudioOutputName();
			const FString& GetIsFinishedOutputName();
			const FString& GetAudioDeviceHandleVariableName();
			const FString& GetSoundUniqueIdName();
			const FString& GetIsPreviewSoundName();
			const FString& GetInstanceIDName();
		}

		// V1.1 of a Metasound Stereo source. Uses two FAudioBuffers as outputs.
		namespace MetasoundSourceStereoV1_1
		{
			FMetasoundFrontendVersion GetVersion();
			const FString& GetOnPlayInputName();
			const FString& GetLeftAudioOutputName();
			const FString& GetRightAudioOutputName();
			const FString& GetIsFinishedOutputName();
			const FString& GetAudioDeviceHandleVariableName();
			const FString& GetSoundUniqueIdName();
			const FString& GetIsPreviewSoundName();
			const FString& GetInstanceIDName();
		}

		// Current version of Metasound Source.
		namespace MetasoundSource
		{
			const FString& GetOnPlayInputName();
			const FString& GetIsFinishedOutputName();
			const FString& GetAudioDeviceHandleVariableName();
			const FString& GetSoundUniqueIdName();
			const FString& GetIsPreviewSoundName();
			const FString& GetInstanceIDName();
		}

		// Current version of MetasoundSourceMono
		namespace MetasoundSourceMono
		{
			FMetasoundFrontendVersion GetVersion();
			const FString& GetAudioOutputName();
		}

		// Current version of MetasoundSourceStereo
		namespace MetasoundSourceStereo
		{
			FMetasoundFrontendVersion GetVersion();
			const FString& GetLeftAudioOutputName();
			const FString& GetRightAudioOutputName();
		}

		// Register all MetasoundEngine archetypes.
		void RegisterArchetypes();
	}
}

