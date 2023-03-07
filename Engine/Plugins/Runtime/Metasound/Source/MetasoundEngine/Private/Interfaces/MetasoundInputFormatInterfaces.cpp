// Copyright Epic Games, Inc. All Rights Reserved.
#include "Interfaces/MetasoundInputFormatInterfaces.h"

#include "IAudioParameterInterfaceRegistry.h"
#include "Interfaces/MetasoundOutputFormatInterfaces.h"
#include "MetasoundAudioBuffer.h"
#include "MetasoundTrigger.h"
#include "Templates/SharedPointer.h"
#include "UObject/Class.h"


#define LOCTEXT_NAMESPACE "MetasoundEngine"
namespace Metasound::Engine
{
#define AUDIO_PARAMETER_INTERFACE_NAMESPACE "UE.InputFormat.Mono"
	namespace InputFormatMonoInterface
	{
		const FMetasoundFrontendVersion& GetVersion()
		{
			static const FMetasoundFrontendVersion Version = { AUDIO_PARAMETER_INTERFACE_NAMESPACE, { 1, 0 } };
			return Version;
		}

		namespace Inputs
		{
			const FName MonoIn = AUDIO_PARAMETER_INTERFACE_MEMBER_DEFINE("Audio:0");
		} // namespace Inputs

		Audio::FParameterInterfacePtr CreateInterface()
		{
			struct FInterface : public Audio::FParameterInterface
			{
				FInterface()
					: FParameterInterface(InputFormatMonoInterface::GetVersion().Name, InputFormatMonoInterface::GetVersion().Number.ToInterfaceVersion())
				{
					Inputs =
					{
						{
							LOCTEXT("InputFormatMonoInterfaceInputName", "In Mono"),
							LOCTEXT("OutputFormatStereoInterface_RightDescription", "Mono input audio."),
							GetMetasoundDataTypeName<FAudioBuffer>(),
							Inputs::MonoIn
						}
					};
				}
			};
			return MakeShared<FInterface>();
		}

		Audio::FParameterInterfacePtr CreateInterface(const UClass& InClass)
		{
			return CreateInterface();
		}

		TArray<FMetasoundFrontendInterfaceBinding> CreateOutputBindings()
		{
			return TArray<FMetasoundFrontendInterfaceBinding>
			{
				{
					OutputFormatMonoInterface::GetVersion(), // Mono-to-mono takes priority
					0,
					{
						{
							OutputFormatMonoInterface::Outputs::MonoOut, InputFormatMonoInterface::Inputs::MonoIn,
						}
					},
				},
				{
					OutputFormatStereoInterface::GetVersion(),
					10,
					{
						{
							OutputFormatStereoInterface::Outputs::LeftOut, InputFormatMonoInterface::Inputs::MonoIn,
						}
					}
				},
				{
					OutputFormatQuadInterface::GetVersion(),
					20,
					{
						{
							OutputFormatQuadInterface::Outputs::FrontLeftOut, InputFormatMonoInterface::Inputs::MonoIn,
						}
					}
				},
				{
					OutputFormatFiveDotOneInterface::GetVersion(),
					30,
					{
						{
							OutputFormatFiveDotOneInterface::Outputs::FrontLeftOut, InputFormatMonoInterface::Inputs::MonoIn,
						}
					}
				},
				{
					OutputFormatSevenDotOneInterface::GetVersion(),
					40,
					{
						{
							OutputFormatSevenDotOneInterface::Outputs::FrontLeftOut, InputFormatMonoInterface::Inputs::MonoIn,
						}
					}
				}
			};
		}
	} // namespace InputFormatMonoInterface
#undef AUDIO_PARAMETER_INTERFACE_NAMESPACE

#define AUDIO_PARAMETER_INTERFACE_NAMESPACE "UE.InputFormat.Stereo"
	namespace InputFormatStereoInterface
	{
		const FMetasoundFrontendVersion& GetVersion()
		{
			static const FMetasoundFrontendVersion Version = { AUDIO_PARAMETER_INTERFACE_NAMESPACE, { 1, 0 } };
			return Version;
		}

		namespace Inputs
		{
			const FName LeftIn = AUDIO_PARAMETER_INTERFACE_MEMBER_DEFINE("Audio:0");
			const FName RightIn = AUDIO_PARAMETER_INTERFACE_MEMBER_DEFINE("Audio:1");
		} // namespace Inputs

		Audio::FParameterInterfacePtr CreateInterface()
		{
			struct FInterface : public Audio::FParameterInterface
			{
				FInterface()
					: FParameterInterface(InputFormatStereoInterface::GetVersion().Name, InputFormatStereoInterface::GetVersion().Number.ToInterfaceVersion())
				{
					Inputs =
					{
						{
							LOCTEXT("InputFormatStereoInterfaceInputLeftName", "In Left"),
							LOCTEXT("StereoIn_Left_AudioDescription", "Left stereo input audio."),
							GetMetasoundDataTypeName<FAudioBuffer>(),
							Inputs::LeftIn,
							FText::GetEmpty(), // Required Text
							100
						},
						{
							LOCTEXT("InputFormatStereoInterfaceInputRightName", "In Right"),
							LOCTEXT("StereoIn_Right_AudioDescription", "Right stereo input audio."),
							GetMetasoundDataTypeName<FAudioBuffer>(),
							Inputs::RightIn,
							FText::GetEmpty(), // Required Text
							101
						}
					};
				}
			};

			return MakeShared<FInterface>();
		}

		Audio::FParameterInterfacePtr CreateInterface(const UClass& InClass)
		{
			return CreateInterface();
		}

		TArray<FMetasoundFrontendInterfaceBinding> CreateOutputBindings()
		{
			return TArray<FMetasoundFrontendInterfaceBinding>
			{
				{
					OutputFormatMonoInterface::GetVersion(),
					10,
					{
						{
							OutputFormatMonoInterface::Outputs::MonoOut, InputFormatStereoInterface::Inputs::LeftIn
						}
					}
				},
				{
					OutputFormatStereoInterface::GetVersion(),
					0,	// Stereo to stereo takes priority
					{
						{
							OutputFormatStereoInterface::Outputs::LeftOut, InputFormatStereoInterface::Inputs::LeftIn,
						},
						{
							OutputFormatStereoInterface::Outputs::RightOut, InputFormatStereoInterface::Inputs::RightIn
						}
					}
				},
				{
					OutputFormatQuadInterface::GetVersion(),
					30,
					{
						{
							OutputFormatQuadInterface::Outputs::FrontLeftOut, InputFormatStereoInterface::Inputs::LeftIn,
						},
						{
							OutputFormatQuadInterface::Outputs::FrontRightOut, InputFormatStereoInterface::Inputs::RightIn
						}
					}
				},
				{
					OutputFormatFiveDotOneInterface::GetVersion(),
					40,
					{
						{
							OutputFormatFiveDotOneInterface::Outputs::FrontLeftOut, InputFormatStereoInterface::Inputs::LeftIn,
						},
						{
							OutputFormatFiveDotOneInterface::Outputs::FrontRightOut, InputFormatStereoInterface::Inputs::RightIn
						}
					}
				},
				{
					OutputFormatSevenDotOneInterface::GetVersion(),
					50,
					{
						{
							OutputFormatSevenDotOneInterface::Outputs::FrontLeftOut, InputFormatStereoInterface::Inputs::LeftIn,
						},
						{
							OutputFormatSevenDotOneInterface::Outputs::FrontRightOut, InputFormatStereoInterface::Inputs::RightIn
						}
					}
				}
			};
		}
	} // namespace InputFormatStereoInterface
#undef AUDIO_PARAMETER_INTERFACE_NAMESPACE
} // namespace Metasound::Engine
#undef LOCTEXT_NAMESPACE
