// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundEngineArchetypes.h"

#include "MetasoundAudioFormats.h"
#include "MetasoundDataReference.h"
#include "MetasoundFrontendArchetypeRegistry.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundInterface.h"
#include "MetasoundRouter.h"
#include "MetasoundSource.h"
#include "MetasoundUObjectRegistry.h"
#include "UObject/Class.h"
#include "UObject/NoExportTypes.h"

#define LOCTEXT_NAMESPACE "MetasoundEngineArchetypes"

namespace Metasound
{
	namespace Engine
	{
		// MetasoundV1_0 is a metasound without any required inputs or outputs.
		namespace MetasoundV1_0
		{
			FMetasoundFrontendVersion GetVersion()
			{
				static const FName VersionName = FName("MetaSound");
				return FMetasoundFrontendVersion{ VersionName, FMetasoundFrontendVersionNumber{1, 0} };
			}

			FMetasoundFrontendInterface GetInterface()
			{
				FMetasoundFrontendInterface Interface;
				Interface.Version = GetVersion();

				return Interface;
			}
		}

		namespace MetasoundSourceMonoV1_0
		{
			FMetasoundFrontendVersion GetVersion()
			{
				static const FName VersionName = FName("MonoSource");
				return FMetasoundFrontendVersion{ VersionName, FMetasoundFrontendVersionNumber{1, 0} };

			}

			const FVertexName& GetOnPlayInputName()
			{
				static const FVertexName TriggerInputName = TEXT("On Play");
				return TriggerInputName;
			}

			const FVertexName& GetAudioOutputName()
			{
				static const FVertexName AudioOutputName = TEXT("Generated Audio");
				return AudioOutputName;
			}

			const FVertexName& GetIsFinishedOutputName()
			{
				static const FVertexName OnFinishedOutputName = TEXT("On Finished");
				return OnFinishedOutputName;
			}

			const FVertexName& GetAudioDeviceIDVariableName()
			{
				static const FVertexName AudioDeviceIDVarName = TEXT("AudioDeviceID");
				return AudioDeviceIDVarName;
			}

			const FVertexName& GetSoundUniqueIdName()
			{
				static const FVertexName SoundUniqueIdVarName = TEXT("SoundUniqueId");
				return SoundUniqueIdVarName;
			}

			const FVertexName& GetIsPreviewSoundName()
			{
				static const FVertexName SoundIsPreviewSoundName = TEXT("IsPreviewSound");
				return SoundIsPreviewSoundName;
			}

			FMetasoundFrontendInterface GetInterface()
			{
				FMetasoundFrontendInterface Interface;
				Interface.Version.Name = "MonoSource";
				Interface.Version.Number = FMetasoundFrontendVersionNumber{1, 0};
				
				// Inputs
				FMetasoundFrontendClassVertex OnPlayTrigger;
				OnPlayTrigger.Name = GetOnPlayInputName();

				OnPlayTrigger.TypeName = GetMetasoundDataTypeName<FTrigger>();
				OnPlayTrigger.Metadata.DisplayName = LOCTEXT("OnPlay", "On Play");
				OnPlayTrigger.Metadata.Description = LOCTEXT("OnPlayTriggerToolTip", "Trigger executed when this source is played.");
				OnPlayTrigger.VertexID = FGuid::NewGuid();

				Interface.Inputs.Add(OnPlayTrigger);

				// Outputs 
				FMetasoundFrontendClassVertex OnFinished;
				OnFinished.Name = GetIsFinishedOutputName();

				OnFinished.TypeName = GetMetasoundDataTypeName<FTrigger>();
				OnFinished.Metadata.DisplayName = LOCTEXT("OnFinished", "On Finished");
				OnFinished.Metadata.Description = LOCTEXT("OnFinishedToolTip", "Trigger executed to initiate stopping the source.");
				OnFinished.VertexID = FGuid::NewGuid();

				Interface.Outputs.Add(OnFinished);

				FMetasoundFrontendClassVertex GeneratedAudio;
				GeneratedAudio.Name = GetAudioOutputName();
				GeneratedAudio.TypeName = GetMetasoundDataTypeName<FMonoAudioFormat>();
				GeneratedAudio.Metadata.DisplayName = LOCTEXT("GeneratedMono", "Audio");
				GeneratedAudio.Metadata.Description = LOCTEXT("GeneratedAudioToolTip", "The resulting output audio from this source.");
				GeneratedAudio.VertexID = FGuid::NewGuid();

				Interface.Outputs.Add(GeneratedAudio);

				// Environment 
				FMetasoundFrontendClassEnvironmentVariable AudioDeviceID;
				AudioDeviceID.Name = GetAudioDeviceIDVariableName();
				AudioDeviceID.Metadata.Description = LOCTEXT("AudioDeviceIDToolTip", "Audio Device ID");

				Interface.Environment.Add(AudioDeviceID);

				return Interface;
			}
		}

		namespace MetasoundSourceStereoV1_0
		{
			FMetasoundFrontendVersion GetVersion()
			{
				static const FName VersionName = FName("StereoSource");
				return FMetasoundFrontendVersion{ VersionName, FMetasoundFrontendVersionNumber{1, 0} };
			}

			const FVertexName& GetOnPlayInputName()
			{
				static const FVertexName TriggerInputName = TEXT("On Play");
				return TriggerInputName;
			}

			const FVertexName& GetAudioOutputName()
			{
				static const FVertexName AudioOutputName = TEXT("Generated Audio");
				return AudioOutputName;
			}

			const FVertexName& GetIsFinishedOutputName()
			{
				static const FVertexName OnFinishedOutputName = TEXT("On Finished");
				return OnFinishedOutputName;
			}

			const FVertexName& GetAudioDeviceIDVariableName()
			{
				static const FVertexName AudioDeviceIDVarName = TEXT("AudioDeviceID");
				return AudioDeviceIDVarName;
			}

			const FVertexName& GetSoundUniqueIdName()
			{
				static const FVertexName SoundUniqueIdVarName = TEXT("SoundUniqueId");
				return SoundUniqueIdVarName;
			}

			const FVertexName& GetIsPreviewSoundName()
			{
				static const FVertexName SoundIsPreviewSoundName = TEXT("IsPreviewSound");
				return SoundIsPreviewSoundName;
			}

			const FVertexName& GetGraphName()
			{
				static const FVertexName SoundGraphName = TEXT("GraphName");
				return SoundGraphName;
			}

			FMetasoundFrontendInterface GetInterface()
			{
				FMetasoundFrontendInterface Interface;
				Interface.Version = GetVersion();
				
				// Inputs
				FMetasoundFrontendClassVertex OnPlayTrigger;
				OnPlayTrigger.Name = GetOnPlayInputName();

				OnPlayTrigger.TypeName = GetMetasoundDataTypeName<FTrigger>();
				OnPlayTrigger.Metadata.DisplayName = LOCTEXT("OnPlay", "On Play");
				OnPlayTrigger.Metadata.Description = LOCTEXT("OnPlayTriggerToolTip", "Trigger executed when this source is played.");
				OnPlayTrigger.VertexID = FGuid::NewGuid();

				Interface.Inputs.Add(OnPlayTrigger);

				// Outputs
				FMetasoundFrontendClassVertex OnFinished;
				OnFinished.Name = GetIsFinishedOutputName();

				OnFinished.TypeName = GetMetasoundDataTypeName<FTrigger>();
				OnFinished.Metadata.DisplayName = LOCTEXT("OnFinished", "On Finished");
				OnFinished.Metadata.Description = LOCTEXT("OnFinishedToolTip", "Trigger executed to initiate stopping the source.");
				OnFinished.VertexID = FGuid::NewGuid();

				Interface.Outputs.Add(OnFinished);

				FMetasoundFrontendClassVertex GeneratedAudio;
				GeneratedAudio.Name = GetAudioOutputName();
				GeneratedAudio.TypeName = GetMetasoundDataTypeName<FStereoAudioFormat>();
				GeneratedAudio.Metadata.DisplayName = LOCTEXT("GeneratedStereo", "Audio");
				GeneratedAudio.Metadata.Description = LOCTEXT("GeneratedAudioToolTip", "The resulting output audio from this source.");
				GeneratedAudio.VertexID = FGuid::NewGuid();

				Interface.Outputs.Add(GeneratedAudio);

				// Environment
				FMetasoundFrontendClassEnvironmentVariable AudioDeviceID;
				AudioDeviceID.Name = GetAudioDeviceIDVariableName();
				AudioDeviceID.Metadata.Description = LOCTEXT("AudioDeviceIDToolTip", "Audio Device ID");

				Interface.Environment.Add(AudioDeviceID);

				return Interface;
			}
		}

		namespace MetasoundSourceMonoV1_1
		{
			FMetasoundFrontendVersion GetVersion()
			{
				static const FName VersionName = FName("MonoSource");
				return FMetasoundFrontendVersion{ VersionName, FMetasoundFrontendVersionNumber{1, 1} };
			}

			const FVertexName& GetOnPlayInputName()
			{
				static const FVertexName TriggerInputName = TEXT("On Play");
				return TriggerInputName;
			}

			const FVertexName& GetAudioOutputName()
			{
				static const FVertexName AudioOutputName = TEXT("Audio:0");
				return AudioOutputName;
			}

			const FVertexName& GetIsFinishedOutputName()
			{
				static const FVertexName OnFinishedOutputName = TEXT("On Finished");
				return OnFinishedOutputName;
			}

			const FVertexName& GetAudioDeviceIDVariableName()
			{
				static const FVertexName AudioDeviceIDVarName = TEXT("AudioDeviceID");
				return AudioDeviceIDVarName;
			}

			const FVertexName& GetSoundUniqueIdName()
			{
				static const FVertexName SoundUniqueIdVarName = TEXT("SoundUniqueId");
				return SoundUniqueIdVarName;
			}

			const FVertexName& GetIsPreviewSoundName()
			{
				static const FVertexName SoundIsPreviewSoundName = TEXT("IsPreviewSound");
				return SoundIsPreviewSoundName;
			}

			const FVertexName& GetGraphName()
			{
				static const FVertexName SoundGraphName = TEXT("GraphName");
				return SoundGraphName;
			}

			const FVertexName& GetInstanceIDName()
			{
				static const FVertexName& Name = FMetaSoundParameterTransmitter::GetInstanceIDEnvironmentVariableName();
				return Name;
			}

			FMetasoundFrontendClassVertex GetClassAudioOutput()
			{
				FMetasoundFrontendClassVertex GeneratedAudio;
				GeneratedAudio.Name = GetAudioOutputName();
				GeneratedAudio.TypeName = GetMetasoundDataTypeName<FAudioBuffer>();
				GeneratedAudio.Metadata.DisplayName = LOCTEXT("GeneratedMono", "Audio");
				GeneratedAudio.Metadata.Description = LOCTEXT("GeneratedAudioToolTip", "The resulting output audio from this source.");
				GeneratedAudio.VertexID = FGuid::NewGuid();

				return GeneratedAudio;
			}

			FMetasoundFrontendInterface GetInterface()
			{
				FMetasoundFrontendInterface Interface;
				Interface.Version = GetVersion();
				
				// Inputs
				FMetasoundFrontendClassVertex OnPlayTrigger;
				OnPlayTrigger.Name = GetOnPlayInputName();

				OnPlayTrigger.TypeName = GetMetasoundDataTypeName<FTrigger>();
				OnPlayTrigger.Metadata.DisplayName = LOCTEXT("OnPlay", "On Play");
				OnPlayTrigger.Metadata.Description = LOCTEXT("OnPlayTriggerToolTip", "Trigger executed when this source is played.");
				OnPlayTrigger.VertexID = FGuid::NewGuid();

				Interface.Inputs.Add(OnPlayTrigger);

				// Outputs
				FMetasoundFrontendClassVertex OnFinished;
				OnFinished.Name = GetIsFinishedOutputName();

				OnFinished.TypeName = GetMetasoundDataTypeName<FTrigger>();
				OnFinished.Metadata.DisplayName = LOCTEXT("OnFinished", "On Finished");
				OnFinished.Metadata.Description = LOCTEXT("OnFinishedToolTip", "Trigger executed to initiate stopping the source.");
				OnFinished.VertexID = FGuid::NewGuid();

				Interface.Outputs.Add(OnFinished);

				FMetasoundFrontendClassVertex GeneratedAudio = GetClassAudioOutput();
				Interface.Outputs.Add(GeneratedAudio);

				// Environment
				FMetasoundFrontendClassEnvironmentVariable AudioDeviceID;
				AudioDeviceID.Name = GetAudioDeviceIDVariableName();
				AudioDeviceID.Metadata.Description = LOCTEXT("AudioDeviceIDToolTip", "Audio Device ID");

				Interface.Environment.Add(AudioDeviceID);

				return Interface;
			}

			// Update from a MetasoundSourceMonoV1_0 to MetasoundSourceMonoV1_1
			class FUpdateInterface : public Frontend::IDocumentTransform
			{
			public:
				virtual bool Transform(Frontend::FDocumentHandle InDocument) const override
				{
					// Swap FMonoAudioFormat output node to an FAudioBuffer output node.
					using namespace Frontend;

					FGraphHandle Graph = InDocument->GetRootGraph();
					if (!Graph->IsValid())
					{
						return false;
					}

					InDocument->RemoveInterfaceVersion(MetasoundSourceMonoV1_0::GetInterface().Version);
					InDocument->AddInterfaceVersion(MetasoundSourceMonoV1_1::GetInterface().Version);

					FNodeHandle MonoFormatOutput = Graph->GetOutputNodeWithName(MetasoundSourceMonoV1_0::GetAudioOutputName());
					FVector2D MonoFormatLocation;

					FOutputHandle OutputToReconnect = IOutputController::GetInvalidHandle();
					if (MonoFormatOutput->IsValid())
					{
						// Get the first location.
						for (auto Location : MonoFormatOutput->GetNodeStyle().Display.Locations)
						{
							MonoFormatLocation = Location.Value;
						}

						// Get connections
						TArray<FInputHandle> Inputs = MonoFormatOutput->GetInputs();
						if (ensure(Inputs.Num() == 1))
						{
							OutputToReconnect = Inputs[0]->GetConnectedOutput();
						}

						Graph->RemoveOutputVertex(MetasoundSourceMonoV1_0::GetAudioOutputName());
					}

					// Create output
					FNodeHandle BufferOutput = Graph->AddOutputVertex(GetClassAudioOutput());
					if (ensure(BufferOutput->IsValid()))
					{
						FMetasoundFrontendNodeStyle Style = BufferOutput->GetNodeStyle();
						Style.Display.Locations.Add(FGuid(), MonoFormatLocation);
						BufferOutput->SetNodeStyle(Style);

						if (OutputToReconnect->IsValid())
						{
							// Reconnect
							TArray<FInputHandle> Inputs = BufferOutput->GetInputs();
							if (ensure(Inputs.Num() == 1))
							{
								ensure(OutputToReconnect->Connect(*Inputs[0]));
							}
						}
					}

					return true;
				}
			};
		}

		namespace MetasoundSourceStereoV1_1
		{
			FMetasoundFrontendVersion GetVersion()
			{
				static const FName VersionName = FName("StereoSource");
				return FMetasoundFrontendVersion{ VersionName, FMetasoundFrontendVersionNumber{1, 1} };
			}

			const FVertexName& GetOnPlayInputName()
			{
				static const FVertexName TriggerInputName = TEXT("On Play");
				return TriggerInputName;
			}

			const FVertexName& GetLeftAudioOutputName()
			{
				static const FVertexName AudioOutputName = TEXT("Audio:0");
				return AudioOutputName;
			}

			const FVertexName& GetRightAudioOutputName()
			{
				static const FVertexName AudioOutputName = TEXT("Audio:1");
				return AudioOutputName;
			}

			const FVertexName& GetIsFinishedOutputName()
			{
				static const FVertexName OnFinishedOutputName = TEXT("On Finished");
				return OnFinishedOutputName;
			}

			const FVertexName& GetAudioDeviceIDVariableName()
			{
				static const FVertexName AudioDeviceIDVarName = TEXT("AudioDeviceID");
				return AudioDeviceIDVarName;
			}

			const FVertexName& GetSoundUniqueIdName()
			{
				static const FVertexName SoundUniqueIdVarName = TEXT("SoundUniqueId");
				return SoundUniqueIdVarName;
			}

			const FVertexName& GetIsPreviewSoundName()
			{
				static const FVertexName SoundIsPreviewSoundName = TEXT("IsPreviewSound");
				return SoundIsPreviewSoundName;
			}

			const FVertexName& GetGraphName()
			{
				static const FVertexName SoundGraphName = TEXT("GraphName");
				return SoundGraphName;
			}

			const FVertexName& GetInstanceIDName()
			{
				static const FVertexName& Name = FMetaSoundParameterTransmitter::GetInstanceIDEnvironmentVariableName();
				return Name;
			}

			FMetasoundFrontendClassVertex GetClassLeftAudioOutput()
			{
				FMetasoundFrontendClassVertex GeneratedLeftAudio;
				GeneratedLeftAudio.Name = GetLeftAudioOutputName();
				GeneratedLeftAudio.TypeName = GetMetasoundDataTypeName<FAudioBuffer>();
				GeneratedLeftAudio.Metadata.DisplayName = LOCTEXT("GeneratedStereoLeft", "Left Audio");
				GeneratedLeftAudio.Metadata.Description = LOCTEXT("GeneratedLeftAudioToolTip", "The resulting output audio from this source.");
				GeneratedLeftAudio.VertexID = FGuid::NewGuid();

				return GeneratedLeftAudio;
			}

			FMetasoundFrontendClassVertex GetClassRightAudioOutput()
			{
				FMetasoundFrontendClassVertex GeneratedRightAudio;
				GeneratedRightAudio.Name = GetRightAudioOutputName();
				GeneratedRightAudio.TypeName = GetMetasoundDataTypeName<FAudioBuffer>();
				GeneratedRightAudio.Metadata.DisplayName = LOCTEXT("GeneratedStereoRight", "Right Audio");
				GeneratedRightAudio.Metadata.Description = LOCTEXT("GeneratedRightAudioToolTip", "The resulting output audio from this source.");
				GeneratedRightAudio.VertexID = FGuid::NewGuid();

				return GeneratedRightAudio;
			}

			FMetasoundFrontendInterface GetInterface()
			{
				FMetasoundFrontendInterface Interface;
				Interface.Version = GetVersion();
				
				// Inputs
				FMetasoundFrontendClassVertex OnPlayTrigger;
				OnPlayTrigger.Name = GetOnPlayInputName();

				OnPlayTrigger.TypeName = GetMetasoundDataTypeName<FTrigger>();
				OnPlayTrigger.Metadata.DisplayName = LOCTEXT("OnPlay", "On Play");
				OnPlayTrigger.Metadata.Description = LOCTEXT("OnPlayTriggerToolTip", "Trigger executed when this source is played.");
				OnPlayTrigger.VertexID = FGuid::NewGuid();

				Interface.Inputs.Add(OnPlayTrigger);

				// Outputs
				FMetasoundFrontendClassVertex OnFinished;
				OnFinished.Name = GetIsFinishedOutputName();

				OnFinished.TypeName = GetMetasoundDataTypeName<FTrigger>();
				OnFinished.Metadata.DisplayName = LOCTEXT("OnFinished", "On Finished");
				OnFinished.Metadata.Description = LOCTEXT("OnFinishedToolTip", "Trigger executed to initiate stopping the source.");
				OnFinished.VertexID = FGuid::NewGuid();

				Interface.Outputs.Add(OnFinished);

				FMetasoundFrontendClassVertex GeneratedLeftAudio = GetClassLeftAudioOutput();
				Interface.Outputs.Add(GeneratedLeftAudio);

				FMetasoundFrontendClassVertex GeneratedRightAudio = GetClassRightAudioOutput();
				Interface.Outputs.Add(GeneratedRightAudio);

				// Environment
				FMetasoundFrontendClassEnvironmentVariable AudioDeviceID;
				AudioDeviceID.Name = GetAudioDeviceIDVariableName();
				AudioDeviceID.Metadata.Description = LOCTEXT("AudioDeviceIDToolTip", "Audio Device ID");
				Interface.Environment.Add(AudioDeviceID);

				return Interface;
			};

			// Update from MetasoundSourceStereoV1_0 to MetasoundSourceStereoV1_1
			class FUpdateInterface : public Frontend::IDocumentTransform
			{
			public:
				virtual bool Transform(Frontend::FDocumentHandle InDocument) const override
				{
					using namespace Frontend;

					FGraphHandle Graph = InDocument->GetRootGraph();

					if (!Graph->IsValid())
					{
						return false;
					}

					InDocument->RemoveInterfaceVersion(MetasoundSourceStereoV1_0::GetInterface().Version);
					InDocument->AddInterfaceVersion(MetasoundSourceStereoV1_1::GetInterface().Version);

					FNodeHandle StereoFormatOutput = Graph->GetOutputNodeWithName(MetasoundSourceStereoV1_0::GetAudioOutputName());
					FOutputHandle LeftOutputToReconnect = IOutputController::GetInvalidHandle();
					FOutputHandle RightOutputToReconnect = IOutputController::GetInvalidHandle();

					FVector2D StereoFormatLocation;

					if (StereoFormatOutput->IsValid())
					{
						// Get the first location.
						for (auto Location : StereoFormatOutput->GetNodeStyle().Display.Locations)
						{
							StereoFormatLocation = Location.Value;
						}

						FInputHandle LeftInput = StereoFormatOutput->GetInputWithVertexName(TEXT("Left"));
						LeftOutputToReconnect = LeftInput->GetConnectedOutput();

						FInputHandle RightInput = StereoFormatOutput->GetInputWithVertexName(TEXT("Right"));
						RightOutputToReconnect = RightInput->GetConnectedOutput();

						Graph->RemoveOutputVertex(MetasoundSourceStereoV1_0::GetAudioOutputName());
					}

					FNodeHandle LeftBufferOutput = Graph->AddOutputVertex(GetClassLeftAudioOutput());
					if (ensure(LeftBufferOutput->IsValid()))
					{
						FMetasoundFrontendNodeStyle Style = LeftBufferOutput->GetNodeStyle();
						Style.Display.Locations.Add(FGuid(), StereoFormatLocation);
						LeftBufferOutput->SetNodeStyle(Style);

						if (LeftOutputToReconnect->IsValid())
						{
							TArray<FInputHandle> Inputs = LeftBufferOutput->GetInputs();
							if (ensure(Inputs.Num() == 1))
							{
								ensure(LeftOutputToReconnect->Connect(*Inputs[0]));
							}
						}
					}

					FNodeHandle RightBufferOutput = Graph->AddOutputVertex(GetClassRightAudioOutput());
					if (ensure(RightBufferOutput->IsValid()))
					{
						if (RightOutputToReconnect->IsValid())
						{
							FMetasoundFrontendNodeStyle Style = RightBufferOutput->GetNodeStyle();
							Style.Display.Locations.Add(FGuid(), StereoFormatLocation + FVector2D{0, 100.f}); // TODO: How should this new output be placed?
							RightBufferOutput->SetNodeStyle(Style);

							TArray<FInputHandle> Inputs = RightBufferOutput->GetInputs();
							if (ensure(Inputs.Num() == 1))
							{
								ensure(RightOutputToReconnect->Connect(*Inputs[0]));
							}
						}
					}

					return true;
				}
			};
		}

		namespace MetasoundSource
		{
			namespace CurrentMonoVersion = MetasoundSourceMonoV1_1;
			namespace CurrentStereoVersion = MetasoundSourceStereoV1_1;

			const FVertexName& GetOnPlayInputName()
			{
				check(CurrentMonoVersion::GetOnPlayInputName() == CurrentStereoVersion::GetOnPlayInputName());
				return CurrentMonoVersion::GetOnPlayInputName();
			}

			const FVertexName& GetIsFinishedOutputName()
			{
				check(CurrentMonoVersion::GetIsFinishedOutputName() == CurrentStereoVersion::GetIsFinishedOutputName());
				return CurrentMonoVersion::GetIsFinishedOutputName();
			}

			const FVertexName& GetAudioDeviceIDVariableName()
			{
				check(CurrentMonoVersion::GetAudioDeviceIDVariableName() == CurrentStereoVersion::GetAudioDeviceIDVariableName());
				return CurrentMonoVersion::GetAudioDeviceIDVariableName();
			}

			const FVertexName& GetSoundUniqueIdName()
			{
				check(CurrentMonoVersion::GetSoundUniqueIdName() == CurrentStereoVersion::GetSoundUniqueIdName());
				return CurrentMonoVersion::GetSoundUniqueIdName();
			}

			const FVertexName& GetIsPreviewSoundName()
			{
				check(CurrentMonoVersion::GetIsPreviewSoundName() == CurrentStereoVersion::GetIsPreviewSoundName());
				return CurrentMonoVersion::GetIsPreviewSoundName();
			}

			const FVertexName& GetInstanceIDName()
			{
				check(CurrentMonoVersion::GetInstanceIDName() == CurrentStereoVersion::GetInstanceIDName());
				return CurrentMonoVersion::GetInstanceIDName();
			}

			const FVertexName& GetGraphName()
			{
				check(CurrentMonoVersion::GetGraphName() == CurrentStereoVersion::GetGraphName());
				return CurrentMonoVersion::GetGraphName();
			}
		}

		namespace MetasoundSourceMono
		{
			namespace Current = MetasoundSourceMonoV1_1;

			FMetasoundFrontendVersion GetVersion()
			{
				return Current::GetVersion();
			}

			const FVertexName& GetAudioOutputName()
			{
				return Current::GetAudioOutputName();
			}
		}

		namespace MetasoundSourceStereo
		{
			namespace Current = MetasoundSourceStereoV1_1;

			FMetasoundFrontendVersion GetVersion()
			{
				return Current::GetVersion();
			}

			const FVertexName& GetLeftAudioOutputName()
			{
				return Current::GetLeftAudioOutputName();
			}

			const FVertexName& GetRightAudioOutputName()
			{
				return Current::GetRightAudioOutputName();
			}
		}

		void RegisterInternalInterfaces()
		{
			{
				constexpr bool bIsDefault = false;
				RegisterInterface<UMetaSoundSource>(MetasoundSourceStereoV1_1::GetInterface(), MakeUnique<MetasoundSourceStereoV1_1::FUpdateInterface>(), bIsDefault, IDataReference::RouterName);
				RegisterInterface<UMetaSoundSource>(MetasoundSourceStereoV1_0::GetInterface(), nullptr, bIsDefault, IDataReference::RouterName);
			}

			{
				constexpr bool bIsDefault = true;
				RegisterInterface<UMetaSound>(MetasoundV1_0::GetInterface(), nullptr, bIsDefault, IDataReference::RouterName);
				RegisterInterface<UMetaSoundSource>(MetasoundSourceMonoV1_0::GetInterface(), nullptr, bIsDefault, IDataReference::RouterName);
				RegisterInterface<UMetaSoundSource>(MetasoundSourceMonoV1_1::GetInterface(), MakeUnique<MetasoundSourceMonoV1_1::FUpdateInterface>(), bIsDefault, IDataReference::RouterName);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE // MetasoundEngineArchetypes
