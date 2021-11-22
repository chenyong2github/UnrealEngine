// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundVertex.h"

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
			const FVertexName& GetOnPlayInputName();
			const FVertexName& GetAudioOutputName();
			const FVertexName& GetIsFinishedOutputName();
			const FVertexName& GetAudioDeviceIDVariableName();
			const FVertexName& GetSoundUniqueIdName();
			const FVertexName& GetIsPreviewSoundName();
		}

		// V1.0 of a Metasound Stereo Source. Uses FStereoAudioFormat as output.
		namespace MetasoundSourceStereoV1_0
		{
			FMetasoundFrontendVersion GetVersion();
			const FVertexName& GetOnPlayInputName();
			const FVertexName& GetAudioOutputName();
			const FVertexName& GetIsFinishedOutputName();
			const FVertexName& GetAudioDeviceIDVariableName();
			const FVertexName& GetSoundUniqueIdName();
			const FVertexName& GetIsPreviewSoundName();
			const FVertexName& GetGraphName();
		}

		// V1.1 of a Metasound Mono source. Uses FAudioBuffer as output.
		namespace MetasoundSourceMonoV1_1
		{
			FMetasoundFrontendVersion GetVersion();
			const FVertexName& GetOnPlayInputName();
			const FVertexName& GetAudioOutputName();
			const FVertexName& GetIsFinishedOutputName();
			const FVertexName& GetAudioDeviceIDVariableName();
			const FVertexName& GetSoundUniqueIdName();
			const FVertexName& GetIsPreviewSoundName();
			const FVertexName& GetInstanceIDName();
			const FVertexName& GetGraphName();
		}

		// V1.1 of a Metasound Stereo source. Uses two FAudioBuffers as outputs.
		namespace MetasoundSourceStereoV1_1
		{
			FMetasoundFrontendVersion GetVersion();
			const FVertexName& GetOnPlayInputName();
			const FVertexName& GetLeftAudioOutputName();
			const FVertexName& GetRightAudioOutputName();
			const FVertexName& GetIsFinishedOutputName();
			const FVertexName& GetAudioDeviceIDVariableName();
			const FVertexName& GetSoundUniqueIdName();
			const FVertexName& GetIsPreviewSoundName();
			const FVertexName& GetInstanceIDName();
			const FVertexName& GetGraphName();
		}

		// Current version of Metasound Source.
		namespace MetasoundSource
		{
			const FVertexName& GetOnPlayInputName();
			const FVertexName& GetIsFinishedOutputName();
			const FVertexName& GetAudioDeviceIDVariableName();
			const FVertexName& GetSoundUniqueIdName();
			const FVertexName& GetIsPreviewSoundName();
			const FVertexName& GetInstanceIDName();
			const FVertexName& GetGraphName();
		}

		// Current version of MetasoundSourceMono
		namespace MetasoundSourceMono
		{
			FMetasoundFrontendVersion GetVersion();
			const FVertexName& GetAudioOutputName();
		}

		// Current version of MetasoundSourceStereo
		namespace MetasoundSourceStereo
		{
			FMetasoundFrontendVersion GetVersion();
			const FVertexName& GetLeftAudioOutputName();
			const FVertexName& GetRightAudioOutputName();
		}

		// Register all interfaces defined in MetaSoundEngine.
		void RegisterInternalInterfaces();
	}
}
