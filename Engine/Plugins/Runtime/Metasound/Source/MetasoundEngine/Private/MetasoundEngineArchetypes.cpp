// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundEngineArchetypes.h"

#include "MetasoundAudioFormats.h"
#include "MetasoundFrontendArchetypeRegistry.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundSource.h"

#define LOCTEXT_NAMESPACE "MetasoundEngineArchetypes"

namespace Metasound
{
	namespace Engine
	{
		namespace MetasoundEngineArchetypesPrivate
		{
			// Entry for registered archetype.
			class FArchetypeRegistryEntry : public Frontend::IArchetypeRegistryEntry
			{
			public:

				// @param InArchetype - Archetype to register.
				// @parma InTransform - Transform to convert from prior version of archetype to this version of archetype.
				FArchetypeRegistryEntry(const FMetasoundFrontendArchetype& InArchetype, TUniquePtr<Frontend::IDocumentTransform>&& InTransform)
				: Archetype(InArchetype)
				, Transform(MoveTemp(InTransform))
				{
				}

				virtual const FMetasoundFrontendArchetype& GetArchetype() const override
				{
					return Archetype;
				}

				virtual bool UpdateRootGraphArchetype(Frontend::FDocumentHandle InDocument) const override
				{
					if (Transform.IsValid())
					{
						return Transform->Transform(InDocument);
					}
					return false;
				}

			private:
				FMetasoundFrontendArchetype Archetype;
				TUniquePtr<Frontend::IDocumentTransform> Transform;
			};
		}

		// MetasoundV1_0 is a metasound without any required inputs or outputs.
		namespace MetasoundV1_0
		{
			FMetasoundFrontendVersion GetVersion()
			{
				static const FName VersionName = FName("MetaSound");
				return FMetasoundFrontendVersion{ VersionName, FMetasoundFrontendVersionNumber{1, 0} };
			}

			FMetasoundFrontendArchetype GetArchetype()
			{
				FMetasoundFrontendArchetype Archetype;
				Archetype.Version = GetVersion();

				return Archetype;
			}
		}

		namespace MetasoundSourceMonoV1_0
		{
			FMetasoundFrontendVersion GetVersion()
			{
				static const FName VersionName = FName("MonoSource");
				return FMetasoundFrontendVersion{ VersionName, FMetasoundFrontendVersionNumber{1, 0} };

			}

			const FString& GetOnPlayInputName()
			{
				static const FString TriggerInputName = TEXT("On Play");
				return TriggerInputName;
			}

			const FString& GetAudioOutputName()
			{
				static const FString AudioOutputName = TEXT("Generated Audio");
				return AudioOutputName;
			}

			const FString& GetIsFinishedOutputName()
			{
				static const FString OnFinishedOutputName = TEXT("On Finished");
				return OnFinishedOutputName;
			}

			const FString& GetAudioDeviceHandleVariableName()
			{
				static const FString AudioDeviceHandleVarName = TEXT("AudioDeviceHandle");
				return AudioDeviceHandleVarName;
			}

			const FString& GetSoundUniqueIdName()
			{
				static const FString SoundUniqueIdVarName = TEXT("SoundUniqueId");
				return SoundUniqueIdVarName;
			}

			const FString& GetIsPreviewSoundName()
			{
				static const FString SoundIsPreviewSoundName = TEXT("IsPreviewSound");
				return SoundIsPreviewSoundName;
			}

			FMetasoundFrontendArchetype GetArchetype()
			{
				FMetasoundFrontendArchetype Archetype;
				Archetype.Version.Name = "MonoSource";
				Archetype.Version.Number = FMetasoundFrontendVersionNumber{1, 0};
				
				// Inputs
				FMetasoundFrontendClassVertex OnPlayTrigger;
				OnPlayTrigger.Name = GetOnPlayInputName();

				OnPlayTrigger.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FTrigger>();
				OnPlayTrigger.Metadata.DisplayName = LOCTEXT("OnPlay", "On Play");
				OnPlayTrigger.Metadata.Description = LOCTEXT("OnPlayTriggerToolTip", "Trigger executed when this source is played.");
				OnPlayTrigger.VertexID = FGuid::NewGuid();

				Archetype.Interface.Inputs.Add(OnPlayTrigger);

				// Outputs 
				FMetasoundFrontendClassVertex OnFinished;
				OnFinished.Name = GetIsFinishedOutputName();

				OnFinished.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FTrigger>();
				OnFinished.Metadata.DisplayName = LOCTEXT("OnFinished", "On Finished");
				OnFinished.Metadata.Description = LOCTEXT("OnFinishedToolTip", "Trigger executed to initiate stopping the source.");
				OnFinished.VertexID = FGuid::NewGuid();

				Archetype.Interface.Outputs.Add(OnFinished);

				FMetasoundFrontendClassVertex GeneratedAudio;
				GeneratedAudio.Name = GetAudioOutputName();
				GeneratedAudio.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FMonoAudioFormat>();
				GeneratedAudio.Metadata.DisplayName = LOCTEXT("GeneratedMono", "Audio");
				GeneratedAudio.Metadata.Description = LOCTEXT("GeneratedAudioToolTip", "The resulting output audio from this source.");
				GeneratedAudio.VertexID = FGuid::NewGuid();

				Archetype.Interface.Outputs.Add(GeneratedAudio);

				// Environment 
				FMetasoundFrontendEnvironmentVariable AudioDeviceHandle;
				AudioDeviceHandle.Name = GetAudioDeviceHandleVariableName();
				AudioDeviceHandle.Metadata.DisplayName = FText::FromString(AudioDeviceHandle.Name);
				AudioDeviceHandle.Metadata.Description = LOCTEXT("AudioDeviceHandleToolTip", "Audio device handle");

				Archetype.Interface.Environment.Add(AudioDeviceHandle);

				return Archetype;
			}
		}

		namespace MetasoundSourceStereoV1_0
		{
			FMetasoundFrontendVersion GetVersion()
			{
				static const FName VersionName = FName("StereoSource");
				return FMetasoundFrontendVersion{ VersionName, FMetasoundFrontendVersionNumber{1, 0} };
			}

			const FString& GetOnPlayInputName()
			{
				static const FString TriggerInputName = TEXT("On Play");
				return TriggerInputName;
			}

			const FString& GetAudioOutputName()
			{
				static const FString AudioOutputName = TEXT("Generated Audio");
				return AudioOutputName;
			}

			const FString& GetIsFinishedOutputName()
			{
				static const FString OnFinishedOutputName = TEXT("On Finished");
				return OnFinishedOutputName;
			}

			const FString& GetAudioDeviceHandleVariableName()
			{
				static const FString AudioDeviceHandleVarName = TEXT("AudioDeviceHandle");
				return AudioDeviceHandleVarName;
			}

			const FString& GetSoundUniqueIdName()
			{
				static const FString SoundUniqueIdVarName = TEXT("SoundUniqueId");
				return SoundUniqueIdVarName;
			}

			const FString& GetIsPreviewSoundName()
			{
				static const FString SoundIsPreviewSoundName = TEXT("IsPreviewSound");
				return SoundIsPreviewSoundName;
			}

			FMetasoundFrontendArchetype GetArchetype()
			{
				FMetasoundFrontendArchetype Archetype;
				Archetype.Version = GetVersion();
				
				// Inputs
				FMetasoundFrontendClassVertex OnPlayTrigger;
				OnPlayTrigger.Name = GetOnPlayInputName();

				OnPlayTrigger.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FTrigger>();
				OnPlayTrigger.Metadata.DisplayName = LOCTEXT("OnPlay", "On Play");
				OnPlayTrigger.Metadata.Description = LOCTEXT("OnPlayTriggerToolTip", "Trigger executed when this source is played.");
				OnPlayTrigger.VertexID = FGuid::NewGuid();

				Archetype.Interface.Inputs.Add(OnPlayTrigger);

				// Outputs
				FMetasoundFrontendClassVertex OnFinished;
				OnFinished.Name = GetIsFinishedOutputName();

				OnFinished.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FTrigger>();
				OnFinished.Metadata.DisplayName = LOCTEXT("OnFinished", "On Finished");
				OnFinished.Metadata.Description = LOCTEXT("OnFinishedToolTip", "Trigger executed to initiate stopping the source.");
				OnFinished.VertexID = FGuid::NewGuid();

				Archetype.Interface.Outputs.Add(OnFinished);

				FMetasoundFrontendClassVertex GeneratedAudio;
				GeneratedAudio.Name = GetAudioOutputName();
				GeneratedAudio.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FStereoAudioFormat>();
				GeneratedAudio.Metadata.DisplayName = LOCTEXT("GeneratedStereo", "Audio");
				GeneratedAudio.Metadata.Description = LOCTEXT("GeneratedAudioToolTip", "The resulting output audio from this source.");
				GeneratedAudio.VertexID = FGuid::NewGuid();

				Archetype.Interface.Outputs.Add(GeneratedAudio);

				// Environment
				FMetasoundFrontendEnvironmentVariable AudioDeviceHandle;
				AudioDeviceHandle.Name = GetAudioDeviceHandleVariableName();
				AudioDeviceHandle.Metadata.DisplayName = FText::FromString(AudioDeviceHandle.Name);
				AudioDeviceHandle.Metadata.Description = LOCTEXT("AudioDeviceHandleToolTip", "Audio device handle");

				Archetype.Interface.Environment.Add(AudioDeviceHandle);

				return Archetype;
			}
		}

		namespace MetasoundSourceMonoV1_1
		{
			FMetasoundFrontendVersion GetVersion()
			{
				static const FName VersionName = FName("MonoSource");
				return FMetasoundFrontendVersion{ VersionName, FMetasoundFrontendVersionNumber{1, 1} };
			}

			const FString& GetOnPlayInputName()
			{
				static const FString TriggerInputName = TEXT("On Play");
				return TriggerInputName;
			}

			const FString& GetAudioOutputName()
			{
				static const FString AudioOutputName = TEXT("Audio:0");
				return AudioOutputName;
			}

			const FString& GetIsFinishedOutputName()
			{
				static const FString OnFinishedOutputName = TEXT("On Finished");
				return OnFinishedOutputName;
			}

			const FString& GetAudioDeviceHandleVariableName()
			{
				static const FString AudioDeviceHandleVarName = TEXT("AudioDeviceHandle");
				return AudioDeviceHandleVarName;
			}

			const FString& GetSoundUniqueIdName()
			{
				static const FString SoundUniqueIdVarName = TEXT("SoundUniqueId");
				return SoundUniqueIdVarName;
			}

			const FString& GetIsPreviewSoundName()
			{
				static const FString SoundIsPreviewSoundName = TEXT("IsPreviewSound");
				return SoundIsPreviewSoundName;
			}

			const FString& GetInstanceIDName()
			{
				static const FString& Name = FMetasoundInstanceTransmitter::GetInstanceIDEnvironmentVariableName();
				return Name;
			}

			FMetasoundFrontendClassVertex GetClassAudioOutput()
			{
				FMetasoundFrontendClassVertex GeneratedAudio;
				GeneratedAudio.Name = GetAudioOutputName();
				GeneratedAudio.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FAudioBuffer>();
				GeneratedAudio.Metadata.DisplayName = LOCTEXT("GeneratedMono", "Audio");
				GeneratedAudio.Metadata.Description = LOCTEXT("GeneratedAudioToolTip", "The resulting output audio from this source.");
				GeneratedAudio.VertexID = FGuid::NewGuid();

				return GeneratedAudio;
			}

			FMetasoundFrontendArchetype GetArchetype()
			{
				FMetasoundFrontendArchetype Archetype;
				Archetype.Version = GetVersion();
				
				// Inputs
				FMetasoundFrontendClassVertex OnPlayTrigger;
				OnPlayTrigger.Name = GetOnPlayInputName();

				OnPlayTrigger.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FTrigger>();
				OnPlayTrigger.Metadata.DisplayName = LOCTEXT("OnPlay", "On Play");
				OnPlayTrigger.Metadata.Description = LOCTEXT("OnPlayTriggerToolTip", "Trigger executed when this source is played.");
				OnPlayTrigger.VertexID = FGuid::NewGuid();

				Archetype.Interface.Inputs.Add(OnPlayTrigger);

				// Outputs
				FMetasoundFrontendClassVertex OnFinished;
				OnFinished.Name = GetIsFinishedOutputName();

				OnFinished.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FTrigger>();
				OnFinished.Metadata.DisplayName = LOCTEXT("OnFinished", "On Finished");
				OnFinished.Metadata.Description = LOCTEXT("OnFinishedToolTip", "Trigger executed to initiate stopping the source.");
				OnFinished.VertexID = FGuid::NewGuid();

				Archetype.Interface.Outputs.Add(OnFinished);

				FMetasoundFrontendClassVertex GeneratedAudio = GetClassAudioOutput();
				Archetype.Interface.Outputs.Add(GeneratedAudio);

				// Environment
				FMetasoundFrontendEnvironmentVariable AudioDeviceHandle;
				AudioDeviceHandle.Name = GetAudioDeviceHandleVariableName();
				AudioDeviceHandle.Metadata.DisplayName = FText::FromString(AudioDeviceHandle.Name);
				AudioDeviceHandle.Metadata.Description = LOCTEXT("AudioDeviceHandleToolTip", "Audio device handle");

				Archetype.Interface.Environment.Add(AudioDeviceHandle);

				return Archetype;
			}

			// Update from a MetasoundSourceMonoV1_0 to MtasoundSourceMonoV1_1
			class FUpdateArchetype : public Frontend::IDocumentTransform
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

			const FString& GetOnPlayInputName()
			{
				static const FString TriggerInputName = TEXT("On Play");
				return TriggerInputName;
			}

			const FString& GetLeftAudioOutputName()
			{
				static const FString AudioOutputName = TEXT("Audio:0");
				return AudioOutputName;
			}

			const FString& GetRightAudioOutputName()
			{
				static const FString AudioOutputName = TEXT("Audio:1");
				return AudioOutputName;
			}

			const FString& GetIsFinishedOutputName()
			{
				static const FString OnFinishedOutputName = TEXT("On Finished");
				return OnFinishedOutputName;
			}

			const FString& GetAudioDeviceHandleVariableName()
			{
				static const FString AudioDeviceHandleVarName = TEXT("AudioDeviceHandle");
				return AudioDeviceHandleVarName;
			}

			const FString& GetSoundUniqueIdName()
			{
				static const FString SoundUniqueIdVarName = TEXT("SoundUniqueId");
				return SoundUniqueIdVarName;
			}

			const FString& GetIsPreviewSoundName()
			{
				static const FString SoundIsPreviewSoundName = TEXT("IsPreviewSound");
				return SoundIsPreviewSoundName;
			}

			const FString& GetInstanceIDName()
			{
				static const FString& Name = FMetasoundInstanceTransmitter::GetInstanceIDEnvironmentVariableName();
				return Name;
			}

			FMetasoundFrontendClassVertex GetClassLeftAudioOutput()
			{
				FMetasoundFrontendClassVertex GeneratedLeftAudio;
				GeneratedLeftAudio.Name = GetLeftAudioOutputName();
				GeneratedLeftAudio.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FAudioBuffer>();
				GeneratedLeftAudio.Metadata.DisplayName = LOCTEXT("GeneratedStereoLeft", "Left Audio");
				GeneratedLeftAudio.Metadata.Description = LOCTEXT("GeneratedLeftAudioToolTip", "The resulting output audio from this source.");
				GeneratedLeftAudio.VertexID = FGuid::NewGuid();

				return GeneratedLeftAudio;
			}

			FMetasoundFrontendClassVertex GetClassRightAudioOutput()
			{
				FMetasoundFrontendClassVertex GeneratedRightAudio;
				GeneratedRightAudio.Name = GetRightAudioOutputName();
				GeneratedRightAudio.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FAudioBuffer>();
				GeneratedRightAudio.Metadata.DisplayName = LOCTEXT("GeneratedStereoRight", "Right Audio");
				GeneratedRightAudio.Metadata.Description = LOCTEXT("GeneratedRightAudioToolTip", "The resulting output audio from this source.");
				GeneratedRightAudio.VertexID = FGuid::NewGuid();

				return GeneratedRightAudio;
			}

			FMetasoundFrontendArchetype GetArchetype()
			{
				FMetasoundFrontendArchetype Archetype;
				Archetype.Version = GetVersion();
				
				// Inputs
				FMetasoundFrontendClassVertex OnPlayTrigger;
				OnPlayTrigger.Name = GetOnPlayInputName();

				OnPlayTrigger.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FTrigger>();
				OnPlayTrigger.Metadata.DisplayName = LOCTEXT("OnPlay", "On Play");
				OnPlayTrigger.Metadata.Description = LOCTEXT("OnPlayTriggerToolTip", "Trigger executed when this source is played.");
				OnPlayTrigger.VertexID = FGuid::NewGuid();

				Archetype.Interface.Inputs.Add(OnPlayTrigger);

				// Outputs
				FMetasoundFrontendClassVertex OnFinished;
				OnFinished.Name = GetIsFinishedOutputName();

				OnFinished.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FTrigger>();
				OnFinished.Metadata.DisplayName = LOCTEXT("OnFinished", "On Finished");
				OnFinished.Metadata.Description = LOCTEXT("OnFinishedToolTip", "Trigger executed to initiate stopping the source.");
				OnFinished.VertexID = FGuid::NewGuid();

				Archetype.Interface.Outputs.Add(OnFinished);

				FMetasoundFrontendClassVertex GeneratedLeftAudio = GetClassLeftAudioOutput();
				Archetype.Interface.Outputs.Add(GeneratedLeftAudio);

				FMetasoundFrontendClassVertex GeneratedRightAudio = GetClassRightAudioOutput();
				Archetype.Interface.Outputs.Add(GeneratedRightAudio);

				// Environment
				FMetasoundFrontendEnvironmentVariable AudioDeviceHandle;
				AudioDeviceHandle.Name = GetAudioDeviceHandleVariableName();
				AudioDeviceHandle.Metadata.DisplayName = FText::FromString(AudioDeviceHandle.Name);
				AudioDeviceHandle.Metadata.Description = LOCTEXT("AudioDeviceHandleToolTip", "Audio device handle");
				Archetype.Interface.Environment.Add(AudioDeviceHandle);

				return Archetype;
			};

			// Update from MetasoundSourceStereoV1_0 to MetasoundSourceStereoV1_1
			class FUpdateArchetype : public Frontend::IDocumentTransform
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

						TArray<FInputHandle> LeftInputs = StereoFormatOutput->GetInputsWithVertexName(TEXT("Left"));
						if (ensure(LeftInputs.Num() == 1))
						{
							LeftOutputToReconnect = LeftInputs[0]->GetConnectedOutput();
						}

						TArray<FInputHandle> RightInputs = StereoFormatOutput->GetInputsWithVertexName(TEXT("Right"));
						if (ensure(RightInputs.Num() == 1))
						{
							RightOutputToReconnect = RightInputs[0]->GetConnectedOutput();
						}

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

			const FString& GetOnPlayInputName()
			{
				check(CurrentMonoVersion::GetOnPlayInputName() == CurrentStereoVersion::GetOnPlayInputName());
				return CurrentMonoVersion::GetOnPlayInputName();
			}

			const FString& GetIsFinishedOutputName()
			{
				check(CurrentMonoVersion::GetIsFinishedOutputName() == CurrentStereoVersion::GetIsFinishedOutputName());
				return CurrentMonoVersion::GetIsFinishedOutputName();
			}

			const FString& GetAudioDeviceHandleVariableName()
			{
				check(CurrentMonoVersion::GetAudioDeviceHandleVariableName() == CurrentStereoVersion::GetAudioDeviceHandleVariableName());
				return CurrentMonoVersion::GetAudioDeviceHandleVariableName();
			}

			const FString& GetSoundUniqueIdName()
			{
				check(CurrentMonoVersion::GetSoundUniqueIdName() == CurrentStereoVersion::GetSoundUniqueIdName());
				return CurrentMonoVersion::GetSoundUniqueIdName();
			}

			const FString& GetIsPreviewSoundName()
			{
				check(CurrentMonoVersion::GetIsPreviewSoundName() == CurrentStereoVersion::GetIsPreviewSoundName());
				return CurrentMonoVersion::GetIsPreviewSoundName();
			}

			const FString& GetInstanceIDName()
			{
				check(CurrentMonoVersion::GetInstanceIDName() == CurrentStereoVersion::GetInstanceIDName());
				return CurrentMonoVersion::GetInstanceIDName();
			}
		}

		namespace MetasoundSourceMono
		{
			namespace Current = MetasoundSourceMonoV1_1;

			FMetasoundFrontendVersion GetVersion()
			{
				return Current::GetVersion();
			}

			const FString& GetAudioOutputName()
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

			const FString& GetLeftAudioOutputName()
			{
				return Current::GetLeftAudioOutputName();
			}

			const FString& GetRightAudioOutputName()
			{
				return Current::GetRightAudioOutputName();
			}
		}

		void RegisterArchetypes()
		{
			using namespace MetasoundEngineArchetypesPrivate;

			Frontend::IArchetypeRegistry::Get().RegisterArchetype(MakeUnique<FArchetypeRegistryEntry>(MetasoundV1_0::GetArchetype(), nullptr));

			Frontend::IArchetypeRegistry::Get().RegisterArchetype(MakeUnique<FArchetypeRegistryEntry>(MetasoundSourceMonoV1_0::GetArchetype(), nullptr));

			Frontend::IArchetypeRegistry::Get().RegisterArchetype(MakeUnique<FArchetypeRegistryEntry>(MetasoundSourceStereoV1_0::GetArchetype(), nullptr));

			Frontend::IArchetypeRegistry::Get().RegisterArchetype(MakeUnique<FArchetypeRegistryEntry>(MetasoundSourceMonoV1_1::GetArchetype(), MakeUnique<MetasoundSourceMonoV1_1::FUpdateArchetype>()));

			Frontend::IArchetypeRegistry::Get().RegisterArchetype(MakeUnique<FArchetypeRegistryEntry>(MetasoundSourceStereoV1_1::GetArchetype(), MakeUnique<MetasoundSourceStereoV1_1::FUpdateArchetype>()));

		}
	}
}

#undef LOCTEXT_NAMESPACE // MetasoundEngineArchetypes
