// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundOutputFormatInterfaces.h"

#include "IAudioParameterInterfaceRegistry.h"
#include "MetasoundAudioBuffer.h"
#include "MetasoundTrigger.h"
#include "Templates/SharedPointer.h"
#include "UObject/Class.h"


#define LOCTEXT_NAMESPACE "MetasoundFrontend"
namespace Metasound
{
	namespace Frontend
	{
#define AUDIO_PARAMETER_INTERFACE_NAMESPACE "UE.OutputFormat.Mono"
		namespace OutputFormatMonoInterface
		{
			const FMetasoundFrontendVersion& GetVersion()
			{
				static const FMetasoundFrontendVersion Version = { AUDIO_PARAMETER_INTERFACE_NAMESPACE, { 1, 0 } };
				return Version;
			}

			namespace Outputs
			{
				const FName MonoOut = AUDIO_PARAMETER_INTERFACE_MEMBER_DEFINE("Audio:0");
			}

			Audio::FParameterInterfacePtr CreateInterface(const UClass& InUClass)
			{
				struct FInterface : public Audio::FParameterInterface
				{
					FInterface(const UClass& InAssetClass)
						: FParameterInterface(OutputFormatMonoInterface::GetVersion().Name, OutputFormatMonoInterface::GetVersion().Number.ToInterfaceVersion(), InAssetClass)
					{
						Outputs =
						{
							{
								LOCTEXT("GeneratedAudioDisplayName", "Out Mono"),
								LOCTEXT("GeneratedAudioDescription", "The resulting mono output from this source."),
								GetMetasoundDataTypeName<FAudioBuffer>(),
								Outputs::MonoOut,
								FText::GetEmpty(), // RequiredText
								EAudioParameterType::None,
								100
							}
						};
					}
				};

				return MakeShared<FInterface>(InUClass);
			}
		} // namespace OutputFormatMonoInterface
#undef AUDIO_PARAMETER_INTERFACE_NAMESPACE

#define AUDIO_PARAMETER_INTERFACE_NAMESPACE "UE.OutputFormat.Stereo"
		namespace OutputFormatStereoInterface
		{
			const FMetasoundFrontendVersion& GetVersion()
			{
				static const FMetasoundFrontendVersion Version = { AUDIO_PARAMETER_INTERFACE_NAMESPACE, { 1, 0 } };
				return Version;
			}

			namespace Outputs
			{
				const FName LeftOut = AUDIO_PARAMETER_INTERFACE_MEMBER_DEFINE("Audio:0");
				const FName RightOut = AUDIO_PARAMETER_INTERFACE_MEMBER_DEFINE("Audio:1");
			}

			Audio::FParameterInterfacePtr CreateInterface(const UClass& InUClass)
			{
				struct FInterface : public Audio::FParameterInterface
				{
					FInterface(const UClass& InAssetClass)
						: FParameterInterface(OutputFormatStereoInterface::GetVersion().Name, OutputFormatStereoInterface::GetVersion().Number.ToInterfaceVersion(), InAssetClass)
					{
						Outputs =
						{
							{
								LOCTEXT("OutputFormatStereoInterface_GeneratedLeftDisplayName", "Out Left"),
								LOCTEXT("OutputFormatStereoInterface_GeneratedLeftDescription", "The resulting left channel output audio from this source."),
								GetMetasoundDataTypeName<FAudioBuffer>(),
								Outputs::LeftOut,
								FText::GetEmpty(), // RequiredText
								EAudioParameterType::None,
								100
							},
							{
								LOCTEXT("OutputFormatStereoInterface_GeneratedRightDisplayName", "Out Right"),
								LOCTEXT("OutputFormatStereoInterface_GeneratedRightDescription", "The resulting right channel output audio from this source."),
								GetMetasoundDataTypeName<FAudioBuffer>(),
								Outputs::RightOut,
								FText::GetEmpty(), // RequiredText
								EAudioParameterType::None,
								101
							}
						};
					}
				};

				return MakeShared<FInterface>(InUClass);
			}
		} // namespace OutputFormatStereoInterface
#undef AUDIO_PARAMETER_INTERFACE_NAMESPACE
	}
}
#undef LOCTEXT_NAMESPACE