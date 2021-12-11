// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundSourceInterface.h"

#include "MetasoundDataReference.h"
#include "MetasoundPrimitives.h"
#include "MetasoundTrigger.h"
#include "UObject/Class.h"
#include "Templates/SharedPointer.h"


#define LOCTEXT_NAMESPACE "Metasound"
#define AUDIO_PARAMETER_INTERFACE_NAMESPACE "UE.Source"
namespace Metasound
{
	namespace Frontend
	{
		namespace SourceInterface
		{
			const FMetasoundFrontendVersion& GetVersion()
			{
				static const FMetasoundFrontendVersion Version = { AUDIO_PARAMETER_INTERFACE_NAMESPACE, { 1, 0 }};
				return Version;
			}

			namespace Inputs
			{
				const FName OnPlay = AUDIO_PARAMETER_INTERFACE_MEMBER_DEFINE("OnPlay");
			}

			namespace Outputs
			{
				const FName OnFinished = AUDIO_PARAMETER_INTERFACE_MEMBER_DEFINE("OnFinished");
			}

			namespace Environment
			{
				const FName DeviceID = AUDIO_PARAMETER_INTERFACE_MEMBER_DEFINE("AudioDeviceID");
				const FName GraphName = AUDIO_PARAMETER_INTERFACE_MEMBER_DEFINE("GraphName");
				const FName IsPreview = AUDIO_PARAMETER_INTERFACE_MEMBER_DEFINE("IsPreviewSound");
				const FName SoundUniqueID = AUDIO_PARAMETER_INTERFACE_MEMBER_DEFINE("SoundUniqueID");
				const FName TransmitterID = AUDIO_PARAMETER_INTERFACE_MEMBER_DEFINE("TransmitterID");
			}

			Audio::FParameterInterfacePtr CreateInterface(const UClass& InUClass)
			{
				struct FInterface : public Audio::FParameterInterface
				{
					FInterface(const UClass& InAssetClass)
						: FParameterInterface(SourceInterface::GetVersion().Name, SourceInterface::GetVersion().Number.ToInterfaceVersion(), InAssetClass)
					{
						Inputs =
						{
							{
								LOCTEXT("OnPlay", "On Play"),
								LOCTEXT("OnPlayDescription", "Trigger executed when the source is played."),
								GetMetasoundDataTypeName<FTrigger>(),
								{ Inputs::OnPlay, false }
							}
						};

						Outputs =
						{
							{
								LOCTEXT("OnFinished", "On Finished"),
								LOCTEXT("OnFinishedDescription", "Trigger executed to initiate stopping the source."),
								GetMetasoundDataTypeName<FTrigger>(),
								Outputs::OnFinished
							}
						};

						Environment =
						{
							{
								LOCTEXT("AudioDeviceIDDisplayName", "Audio Device ID"),
								LOCTEXT("AudioDeviceIDDescription", "ID of AudioDevice source is played from."),
								FName(), // TODO: Align environment data types with environment (ex. this is actually set/get as a uint32)
								Environment::DeviceID
							},
							{
								LOCTEXT("GraphNameDisplayName", "Graph Name"),
								LOCTEXT("AudioDeviceIDDescription", "Name of source graph (for debugging/logging)."),
								GetMetasoundDataTypeName<FString>(),
								Environment::GraphName
							},
							{
								LOCTEXT("IsPreviewSoundDisplayName", "Is Preview Sound"),
								LOCTEXT("IsPreviewSoundDescription", "Whether source is being played as a previewed sound."),
								GetMetasoundDataTypeName<bool>(),
								Environment::IsPreview
							},
							{
								LOCTEXT("TransmitterIDDisplayName", "Transmitter ID"),
								LOCTEXT("TransmitterIDDescription", "ID used by Transmission System to generate a unique send address for each source instance."),
								FName(), // TODO: Align environment data types with environment (ex. this is actually set/get as a uint64)
								Environment::TransmitterID
							},
							{
								LOCTEXT("AudioDeviceIDDisplayName", "Sound Unique ID"),
								LOCTEXT("AudioDeviceIDDescription", "ID of unique source instance."),
								FName(), // TODO: Align environment data types with environment (ex. this is actually set/get as a uint32)
								Environment::SoundUniqueID
							}
						};
					}
				};

				return MakeShared<FInterface>(InUClass);
			}
		} // namespace SourceInterface
	} // namespace Frontend
} // namespace Metasound
#undef AUDIO_PARAMETER_INTERFACE_NAMESPACE
#define LOCTEXT_NAMESPACE "Metasound"
