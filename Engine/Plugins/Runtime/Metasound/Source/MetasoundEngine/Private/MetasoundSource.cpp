// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundSource.h"

#include "AssetRegistryModule.h"
#include "CoreMinimal.h"
#include "Internationalization/Text.h"
#include "MetasoundAssetBase.h"
#include "MetasoundAudioFormats.h"
#include "MetasoundEngineEnvironment.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendQuery.h"
#include "MetasoundFrontendQuerySteps.h"
#include "MetasoundGenerator.h"
#include "MetasoundInstanceTransmitter.h"
#include "MetasoundLog.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundPrimitives.h"
#include "MetasoundReceiveNode.h"
#include "MetasoundTrigger.h"
#include "MetasoundEnvironment.h"

#if WITH_EDITORONLY_DATA
#include "EdGraph/EdGraph.h"
#endif // WITH_EDITORONLY_DATA

static float MetaSoundBlockRateCVar = 100.f;
FAutoConsoleVariableRef CVarMetaSoundBlockRate(
	TEXT("au.MetaSound.BlockRate"),
	MetaSoundBlockRateCVar,
	TEXT("Sets block rate (blocks per second) of MetaSounds.\n")
	TEXT("Default: 100.0f"),
	ECVF_Default);



#define LOCTEXT_NAMESPACE "MetasoundSource"

static FName MetasoundSourceArchetypeName = FName(TEXT("Metasound Source"));

UMetasoundSource::UMetasoundSource(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, FMetasoundAssetBase(GetMonoSourceArchetype())
{
	bRequiresStopFade = true;
	NumChannels = 1;
	Duration = INDEFINITELY_LOOPING_DURATION;
	bLooping = true;
	
	// todo: ensure that we have a method so that the audio engine can be authoritative over the sample rate the UMetasoundSource runs at.
	SampleRate = 48000.f;

}

#if WITH_EDITOR

void UMetasoundSource::PostEditUndo()
{
	Super::PostEditUndo();

	if (Graph)
	{
		Graph->Synchronize();
	}
}

void UMetasoundSource::PostEditChangeProperty(FPropertyChangedEvent& InEvent)
{
	Super::PostEditChangeProperty(InEvent);

	if (InEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UMetasoundSource, OutputFormat)) 
	{
		switch (OutputFormat)
		{
			case EMetasoundSourceAudioFormat::Stereo:

				NumChannels = 2;
				ensure(SetMetasoundArchetype(GetStereoSourceArchetype()));

				break;

			case EMetasoundSourceAudioFormat::Mono:
			default:

				NumChannels = 1;
				ensure(SetMetasoundArchetype(GetMonoSourceArchetype()));

				break;
		}
	}
}

#endif // WITHEDITOR

bool UMetasoundSource::IsPlayable() const
{
	// todo: cache off whether this metasound is buildable to an operator.
	return true;
}

bool UMetasoundSource::SupportsSubtitles() const
{
	return Super::SupportsSubtitles();
}

float UMetasoundSource::GetDuration()
{
	// eh? this is kind of a weird field anyways.
	return Super::GetDuration();
}

ISoundGeneratorPtr UMetasoundSource::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	using namespace Metasound;

	Duration = INDEFINITELY_LOOPING_DURATION;
	bLooping = true;
	VirtualizationMode = EVirtualizationMode::PlayWhenSilent;

	SampleRate = InParams.SampleRate;
	FOperatorSettings InSettings = GetOperatorSettings(static_cast<FSampleRate>(SampleRate));
	FMetasoundEnvironment Environment;

	// Add audio device ID to environment.
	FAudioDeviceHandle DeviceHandle;
	if (UWorld* World = GetWorld())
	{
		DeviceHandle = World->GetAudioDevice();
	}
	if (!DeviceHandle.IsValid())
	{
		if (FAudioDeviceManager* DeviceManager = FAudioDeviceManager::Get())
		{
			DeviceHandle = DeviceManager->GetMainAudioDeviceHandle();
		}
	}
	Environment.SetValue<FAudioDeviceHandle>(GetAudioDeviceHandleVariableName(), DeviceHandle);

	// Set the unique object ID as an environment variable
	Environment.SetValue<uint32>(GetSoundUniqueIdName(), GetUniqueID());

	const FMetasoundFrontendDocument* OriginalDoc = GetDocument().Get();
	if (nullptr == OriginalDoc)
	{
		UE_LOG(LogMetasound, Error, TEXT("Cannot create sound generator. Null Metasound document in UMetasoundSource [Name:%s]"), *GetName());
		return ISoundGeneratorPtr(nullptr);
	}

	// Inject receive nodes for unused transmittable inputs. Perform edits on a copy
	// of the document to avoid altering document.
	// TODO: Use a light wrapper of the document instead of a copy.
	FMetasoundFrontendDocument DocumentWithInjectedReceives;
	ensure(CopyDocumentAndInjectReceiveNodes(InParams.InstanceID, *OriginalDoc, DocumentWithInjectedReceives));

	// Create handles for new root graph
	Frontend::FConstDocumentHandle NewDocumentHandle = Frontend::IDocumentController::CreateDocumentHandle(Frontend::MakeAccessPtr<const FMetasoundFrontendDocument>(DocumentWithInjectedReceives.AccessPoint, DocumentWithInjectedReceives));
	Frontend::FConstGraphHandle RootGraph = NewDocumentHandle->GetRootGraph();

	ensureAlways(RootGraph->IsValid());

	// Create the operator from the graph handle.
	TArray<IOperatorBuilder::FBuildErrorPtr> BuildErrors;
	TUniquePtr<IOperator> Operator = RootGraph->BuildOperator(InSettings, Environment, BuildErrors);

	// Log build errors
	for (const IOperatorBuilder::FBuildErrorPtr& Error : BuildErrors)
	{
		if (Error.IsValid())
		{
			UE_LOG(LogMetasound, Warning, TEXT("MetasoundSource [%s] build error [%s] \"%s\""), *GetName(), *(Error->GetErrorType().ToString()), *(Error->GetErrorDescription().ToString()));
		}
	}
	if (!Operator.IsValid())
	{
		UE_LOG(LogMetasound, Error, TEXT("Failed to build Metasound operator from graph in MetasoundSource [%s]"), *GetName());
	}
	else
	{
		FDataReferenceCollection Outputs = Operator->GetOutputs();
		TArrayView<FAudioBufferReadRef> OutputBuffers;

		// Get output audio buffers.
		if (EMetasoundSourceAudioFormat::Stereo == OutputFormat)
		{
			if (!Outputs.ContainsDataReadReference<FStereoAudioFormat>(GetAudioOutputName()))
			{
				UE_LOG(LogMetasound, Warning, TEXT("MetasoundSource [%s] does not contain stereo output [%s] in output"), *GetName(), *GetAudioOutputName());
			}
			OutputBuffers = Outputs.GetDataReadReferenceOrConstruct<FStereoAudioFormat>(GetAudioOutputName(), InSettings)->GetBuffers();
		}
		else if (EMetasoundSourceAudioFormat::Mono == OutputFormat)
		{
			if (!Outputs.ContainsDataReadReference<FMonoAudioFormat>(GetAudioOutputName()))
			{
				UE_LOG(LogMetasound, Warning, TEXT("MetasoundSource [%s] does not contain mono output [%s] in output"), *GetName(), *GetAudioOutputName());
			}
			OutputBuffers = Outputs.GetDataReadReferenceOrConstruct<FMonoAudioFormat>(GetAudioOutputName(), InSettings)->GetBuffers();
		}

		// References must be cached before moving the operator to the InitParams
		FDataReferenceCollection Inputs = Operator->GetInputs();
		FTriggerWriteRef PlayTrigger = Inputs.GetDataWriteReferenceOrConstruct<FTrigger>(GetOnPlayInputName(), InSettings, false);
		FTriggerReadRef FinishTrigger = Outputs.GetDataReadReferenceOrConstruct<FTrigger>(GetIsFinishedOutputName(), InSettings, false);

		// Create the FMetasoundGenerator.
		FMetasoundGeneratorInitParams InitParams =
		{
			MoveTemp(Operator),
			OutputBuffers,
			MoveTemp(PlayTrigger),
			MoveTemp(FinishTrigger)
		};

		return ISoundGeneratorPtr(new FMetasoundGenerator(MoveTemp(InitParams)));
	}

	return ISoundGeneratorPtr(nullptr);
}

TUniquePtr<IAudioInstanceTransmitter> UMetasoundSource::CreateInstanceTransmitter(const FAudioInstanceTransmitterInitParams& InParams) const
{
	Metasound::FMetasoundInstanceTransmitter::FInitParams InitParams(GetOperatorSettings(InParams.SampleRate), InParams.InstanceID);

	for (const FSendInfoAndVertexName& InfoAndName : GetSendInfos(InParams.InstanceID))
	{
		InitParams.Infos.Add(InfoAndName.SendInfo);
	}

	return MakeUnique<Metasound::FMetasoundInstanceTransmitter>(InitParams);
}

bool UMetasoundSource::GetReceiveNodeMetadataForDataType(const FName& InTypeName, FMetasoundFrontendClassMetadata& OutMetadata) const
{
	using namespace Metasound;

	FFrontendQuery Query;

	Query.AddStep<FGenerateAllAvailableNodeClasses>()
		.AddStep<FFilterClassesByClassName>(FReceiveNodeNames::GetClassNameForDataType(InTypeName));
	
	FFrontendQuerySelectionView Result = Query.ExecuteQuery();

	TArrayView<const FFrontendQueryEntry* const> Selection = Result.GetSelection();

	if (Selection.Num() > 0)
	{
		check(Selection[0]->Value.IsType<FMetasoundFrontendClass>());
		OutMetadata = Selection[0]->Value.Get<FMetasoundFrontendClass>().Metadata;
		return true;
	}

	return false;
}

Metasound::Frontend::FNodeHandle UMetasoundSource::AddInputPinForSendAddress(const Metasound::FMetasoundInstanceTransmitter::FSendInfo& InSendInfo, Metasound::Frontend::FGraphHandle InGraph) const
{
	FMetasoundFrontendClassInput Description;
	FGuid VertexID = InGraph->GetNewVertexID();

	Description.Name = InSendInfo.Address.ChannelName.ToString();
	Description.TypeName = Metasound::GetMetasoundDataTypeName<Metasound::FSendAddress>();
	Description.Metadata.Description = FText::GetEmpty();
	Description.VertexID = VertexID;
	Description.DefaultLiteral.Set(InSendInfo.Address.ChannelName.ToString());
	
	return InGraph->AddInputVertex(Description);
}

bool UMetasoundSource::CopyDocumentAndInjectReceiveNodes(uint64 InInstanceID, const FMetasoundFrontendDocument& InSourceDoc, FMetasoundFrontendDocument& OutDestDoc) const
{
	using namespace Metasound;

	OutDestDoc = InSourceDoc;

	Frontend::FDocumentHandle Document = Frontend::IDocumentController::CreateDocumentHandle(Frontend::MakeAccessPtr(OutDestDoc.AccessPoint, OutDestDoc));
	Frontend::FGraphHandle RootGraph = Document->GetRootGraph();

	TArray<FSendInfoAndVertexName> SendInfoAndVertexes = GetSendInfos(InInstanceID);

	// Inject receive nodes for each transmittable input
	for (const FSendInfoAndVertexName& InfoAndVertexName : SendInfoAndVertexes)
	{

		// Add receive node to graph
		FMetasoundFrontendClassMetadata ReceiveNodeMetadata;
		bool bSuccess = GetReceiveNodeMetadataForDataType(InfoAndVertexName.SendInfo.TypeName, ReceiveNodeMetadata);
		if (!bSuccess)
		{
			// TODO: log warning
			continue;
		}
		Frontend::FNodeHandle ReceiveNode = RootGraph->AddNode(ReceiveNodeMetadata);

		// Add receive node address to graph
		Frontend::FNodeHandle AddressNode = UMetasoundSource::AddInputPinForSendAddress(InfoAndVertexName.SendInfo, RootGraph);
		TArray<Frontend::FOutputHandle> AddressNodeOutputs = AddressNode->GetOutputs();
		if (AddressNodeOutputs.Num() != 1)
		{
			// TODO: log warning
			continue;
		}

		Frontend::FOutputHandle AddressOutput = AddressNodeOutputs[0];
		TArray<Frontend::FInputHandle> ReceiveAddressInput = ReceiveNode->GetInputsWithVertexName(Metasound::FReceiveNodeNames::GetAddressInputName());
		if (ReceiveAddressInput.Num() != 1)
		{
			// TODO: log error
			continue;
		}

		ensure(ReceiveAddressInput[0]->Connect(*AddressOutput));



		// Swap input node connections with receive node connections
		Frontend::FNodeHandle InputNode = RootGraph->GetInputNodeWithName(InfoAndVertexName.VertexName);
		if (!ensure(InputNode->GetOutputs().Num() == 1))
		{
			// TODO: handle input node with varying number of outputs or varying output types.
			continue;
		}

		Frontend::FOutputHandle InputNodeOutput = InputNode->GetOutputs()[0];

		if (ensure(ReceiveNode->IsValid()))
		{
			TArray<Frontend::FOutputHandle> ReceiveNodeOutputs = ReceiveNode->GetOutputs();
			if (!ensure(ReceiveNodeOutputs.Num() == 1))
			{
				// TODO: handle array outputs and receive nodes of varying formats.
				continue;
			}

			TArray<Frontend::FInputHandle> ReceiveDefaultInputs = ReceiveNode->GetInputsWithVertexName(Metasound::FReceiveNodeNames::GetDefaultDataInputName());
			if (ensure(ReceiveDefaultInputs.Num() == 1))
			{
				Frontend::FOutputHandle ReceiverNodeOutput = ReceiveNodeOutputs[0];
				for (Frontend::FInputHandle NodeInput : InputNodeOutput->GetCurrentlyConnectedInputs())
				{
					// Swap connections to receiver node
					ensure(InputNodeOutput->Disconnect(*NodeInput));
					ensure(ReceiverNodeOutput->Connect(*NodeInput));
				}

				ReceiveDefaultInputs[0]->Connect(*InputNodeOutput);
			}
		}
	}

	return true;
}

TArray<FString> UMetasoundSource::GetTransmittableInputVertexNames() const
{
	using namespace Metasound;

	// Unused inputs are all input vertices that are not in the archetype.
	TArray<FString> ArchetypeInputVertexNames;
	for (const FMetasoundFrontendClassVertex& Vertex : GetMetasoundArchetype().Interface.Inputs)
	{
		ArchetypeInputVertexNames.Add(Vertex.Name);
	}

	Frontend::FConstGraphHandle RootGraph = GetRootGraphHandle();
	TArray<FString> GraphInputVertexNames = RootGraph->GetInputVertexNames();

	// Filter graph inputs by archetype inputs.
	GraphInputVertexNames = GraphInputVertexNames.FilterByPredicate([&](const FString& InName) { return !ArchetypeInputVertexNames.Contains(InName); });

	auto IsDataTypeTransmittable = [&](const FString& InVertexName)
	{
		Frontend::FConstClassInputAccessPtr ClassInputPtr = RootGraph->FindClassInputWithName(InVertexName);
		if (const FMetasoundFrontendClassInput* ClassInput = ClassInputPtr.Get())
		{
			FDataTypeRegistryInfo TypeInfo;
			if (Frontend::GetTraitsForDataType(ClassInput->TypeName, TypeInfo))
			{
				return TypeInfo.bIsTransmittable;
			}
		}
		return false;
	};

	GraphInputVertexNames = GraphInputVertexNames.FilterByPredicate(IsDataTypeTransmittable);

	return GraphInputVertexNames;
}

Metasound::FOperatorSettings UMetasoundSource::GetOperatorSettings(Metasound::FSampleRate InSampleRate) const
{
	// Metasound graph gets evaluated 100 times per second by default.
	float BlockRate = FMath::Clamp(MetaSoundBlockRateCVar, 1.0f, 1000.0f); 
	return Metasound::FOperatorSettings(InSampleRate, BlockRate);
}

Metasound::FSendAddress UMetasoundSource::CreateSendAddress(uint64 InInstanceID, const FString& InVertexName, const FName& InDataTypeName) const
{
	using namespace Metasound;

	FSendAddress Address;

	Address.Subsystem = GetSubsystemNameForSendScope(ETransmissionScope::Global);
	Address.ChannelName = FName(FString::Printf(TEXT("%d:%s:%s"), InInstanceID, *InVertexName, *InDataTypeName.ToString()));

	return Address;
}

TArray<UMetasoundSource::FSendInfoAndVertexName> UMetasoundSource::GetSendInfos(uint64 InInstanceID) const
{
	using FSendInfo = Metasound::FMetasoundInstanceTransmitter::FSendInfo;
	using namespace Metasound::Frontend;

	TArray<FSendInfoAndVertexName> SendInfos;

	FConstGraphHandle RootGraph = GetRootGraphHandle();

	TArray<FString> SendVertices = GetTransmittableInputVertexNames();
	for (const FString& VertexName : SendVertices)
	{
		FConstNodeHandle InputNode = RootGraph->GetInputNodeWithName(VertexName);
		for (FConstInputHandle InputHandle : InputNode->GetConstInputs())
		{
			FSendInfoAndVertexName Info;

			// TODO: incorporate VertexID into address. But need to ensure that VertexID
			// will be maintained after injecting Receive nodes. 
			Info.SendInfo.Address = CreateSendAddress(InInstanceID, InputHandle->GetName(), InputHandle->GetDataType());
			Info.SendInfo.ParameterName = FName(InputHandle->GetDisplayName().ToString()); // TODO: display name hack. Need to have naming consistent in editor for inputs
			//Info.SendInfo.ParameterName = FName(*InputHandle->GetName()); // TODO: this is the expected parameter name.
			Info.SendInfo.TypeName = InputHandle->GetDataType();
			Info.VertexName = VertexName;
			
			SendInfos.Add(Info);
		}
	}

	return SendInfos;
}

const TArray<FMetasoundFrontendArchetype>& UMetasoundSource::GetPreferredMetasoundArchetypes() const 
{
	static const TArray<FMetasoundFrontendArchetype> Preferred({GetMonoSourceArchetype(), GetStereoSourceArchetype()});

	return Preferred;
}

const FString& UMetasoundSource::GetOnPlayInputName()
{
	static const FString TriggerInputName = TEXT("On Play");
	return TriggerInputName;
}

const FString& UMetasoundSource::GetAudioOutputName()
{
	static const FString AudioOutputName = TEXT("Generated Audio");
	return AudioOutputName;
}

const FString& UMetasoundSource::GetIsFinishedOutputName()
{
	static const FString OnFinishedOutputName = TEXT("On Finished");
	return OnFinishedOutputName;
}

const FString& UMetasoundSource::GetAudioDeviceHandleVariableName()
{
	static const FString AudioDeviceHandleVarName = TEXT("AudioDeviceHandle");
	return AudioDeviceHandleVarName;
}

const FString& UMetasoundSource::GetSoundUniqueIdName()
{
	static const FString SoundUniqueIdVarName = TEXT("SoundUniqueId");
	return SoundUniqueIdVarName;
}


const FMetasoundFrontendArchetype& UMetasoundSource::GetBaseArchetype()
{
	auto CreateBaseArchetype = []() -> FMetasoundFrontendArchetype
	{
		FMetasoundFrontendArchetype Archetype;
		
		FMetasoundFrontendClassVertex OnPlayTrigger;
		OnPlayTrigger.Name = UMetasoundSource::GetOnPlayInputName();
		OnPlayTrigger.Metadata.DisplayName = FText::FromString(OnPlayTrigger.Name);
		OnPlayTrigger.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FTrigger>();
		OnPlayTrigger.Metadata.Description = LOCTEXT("OnPlayTriggerToolTip", "Trigger executed when this source is played.");
		OnPlayTrigger.VertexID = FGuid::NewGuid();

		Archetype.Interface.Inputs.Add(OnPlayTrigger);

		FMetasoundFrontendClassVertex OnFinished;
		OnFinished.Name = UMetasoundSource::GetIsFinishedOutputName();
		OnFinished.Metadata.DisplayName = FText::FromString(OnFinished.Name);
		OnFinished.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FTrigger>();
		OnFinished.Metadata.Description = LOCTEXT("OnFinishedToolTip", "Trigger executed to initiate stopping the source.");
		OnFinished.VertexID = FGuid::NewGuid();

		Archetype.Interface.Outputs.Add(OnFinished);

		FMetasoundFrontendEnvironmentVariable AudioDeviceHandle;
		AudioDeviceHandle.Name = UMetasoundSource::GetAudioDeviceHandleVariableName();
		AudioDeviceHandle.Metadata.DisplayName = FText::FromString(AudioDeviceHandle.Name);
		AudioDeviceHandle.Metadata.Description = LOCTEXT("AudioDeviceHandleToolTip", "Audio device handle");

		Archetype.Interface.Environment.Add(AudioDeviceHandle);

		return Archetype;
	};

	static const FMetasoundFrontendArchetype BaseArchetype = CreateBaseArchetype();

	return BaseArchetype;
}

const FMetasoundFrontendArchetype& UMetasoundSource::GetMonoSourceArchetype()
{
	auto CreateMonoArchetype = []() -> FMetasoundFrontendArchetype
	{
		FMetasoundFrontendArchetype Archetype = GetBaseArchetype();
		Archetype.Name = "MonoSource";

		FMetasoundFrontendClassVertex GeneratedAudio;
		GeneratedAudio.Name = UMetasoundSource::GetAudioOutputName();
		GeneratedAudio.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FMonoAudioFormat>();
		GeneratedAudio.Metadata.DisplayName = LOCTEXT("GeneratedMono", "Audio");
		GeneratedAudio.Metadata.Description = LOCTEXT("GeneratedAudioToolTip", "The resulting output audio from this source.");
		GeneratedAudio.VertexID = FGuid::NewGuid();

		Archetype.Interface.Outputs.Add(GeneratedAudio);

		return Archetype;
	};

	static const FMetasoundFrontendArchetype MonoArchetype = CreateMonoArchetype();

	return MonoArchetype;
}

const FMetasoundFrontendArchetype& UMetasoundSource::GetStereoSourceArchetype()
{
	auto CreateStereoArchetype = []() -> FMetasoundFrontendArchetype
	{
		FMetasoundFrontendArchetype Archetype = GetBaseArchetype();
		Archetype.Name = FName(TEXT("StereoSource"));

		FMetasoundFrontendClassVertex GeneratedAudio;
		GeneratedAudio.Name = UMetasoundSource::GetAudioOutputName();
		GeneratedAudio.TypeName = Metasound::Frontend::GetDataTypeName<Metasound::FStereoAudioFormat>();
		GeneratedAudio.Metadata.DisplayName = LOCTEXT("GeneratedStereo", "Audio");
		GeneratedAudio.Metadata.Description = LOCTEXT("GeneratedAudioToolTip", "The resulting output audio from this source.");
		GeneratedAudio.VertexID = FGuid::NewGuid();

		Archetype.Interface.Outputs.Add(GeneratedAudio);

		return Archetype;
	};

	static const FMetasoundFrontendArchetype StereoArchetype = CreateStereoArchetype();

	return StereoArchetype;
}

#undef LOCTEXT_NAMESPACE
