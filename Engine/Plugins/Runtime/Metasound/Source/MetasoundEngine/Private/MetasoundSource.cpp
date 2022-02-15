// Copyright Epic Games, Inc. All Rights Reserved.
#include "MetasoundSource.h"

#include "Algo/Transform.h"
#include "AssetRegistryModule.h"
#include "AudioDeviceManager.h"
#include "IAudioParameterInterfaceRegistry.h"
#include "Internationalization/Text.h"
#include "MetasoundAssetBase.h"
#include "MetasoundAudioFormats.h"
#include "MetasoundEngineArchetypes.h"
#include "MetasoundEngineEnvironment.h"
#include "MetasoundEnvironment.h"
#include "MetasoundFrontendController.h"
#include "MetasoundFrontendDataTypeRegistry.h"
#include "MetasoundFrontendInjectReceiveNodes.h"
#include "MetasoundFrontendQuery.h"
#include "MetasoundFrontendQuerySteps.h"
#include "MetasoundFrontendTransform.h"
#include "MetasoundGenerator.h"
#include "MetasoundLog.h"
#include "MetasoundOperatorSettings.h"
#include "MetasoundOutputFormatInterfaces.h"
#include "MetasoundParameterTransmitter.h"
#include "MetasoundPrimitives.h"
#include "MetasoundReceiveNode.h"
#include "MetasoundSourceInterface.h"
#include "MetasoundTrace.h"
#include "MetasoundTrigger.h"
#include "MetasoundUObjectRegistry.h"
#include "UObject/ObjectSaveContext.h"

#if WITH_EDITORONLY_DATA
#include "EdGraph/EdGraph.h"
#endif // WITH_EDITORONLY_DATA

#define LOCTEXT_NAMESPACE "MetaSound"

namespace Metasound
{
	namespace SourcePrivate
	{
		using FFormatOutputVertexKeyMap = TMap<EMetasoundSourceAudioFormat, TArray<Metasound::FVertexName>>;

		const FFormatOutputVertexKeyMap& GetFormatOutputVertexKeys()
		{
			auto CreateVertexKeyMap = []()
			{
				using namespace Metasound::Frontend;

				return FFormatOutputVertexKeyMap
				{
					{
						EMetasoundSourceAudioFormat::Mono,
						{
							OutputFormatMonoInterface::Outputs::MonoOut
						}
					},
					{
						EMetasoundSourceAudioFormat::Stereo,
						{
							OutputFormatStereoInterface::Outputs::LeftOut,
							OutputFormatStereoInterface::Outputs::RightOut,
						}
					}
				};
			};
			static const FFormatOutputVertexKeyMap Map = CreateVertexKeyMap();
			return Map;
		}
	} // namespace SourcePrivate
} // namespace Metasound

FAutoConsoleVariableRef CVarMetaSoundBlockRate(
	TEXT("au.MetaSound.BlockRate"),
	Metasound::ConsoleVariables::BlockRate,
	TEXT("Sets block rate (blocks per second) of MetaSounds.\n")
	TEXT("Default: 100.0f"),
	ECVF_Default);

UMetaSoundSource::UMetaSoundSource(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, FMetasoundAssetBase()
{
	bRequiresStopFade = true;
	NumChannels = 1;
	Duration = INDEFINITELY_LOOPING_DURATION;
	bLooping = true;
	
	// todo: ensure that we have a method so that the audio engine can be authoritative over the sample rate the UMetaSoundSource runs at.
	SampleRate = 48000.f;

}

#if WITH_EDITOR
void UMetaSoundSource::PostEditUndo()
{
	Super::PostEditUndo();

	if (Graph)
	{
		Graph->RegisterGraphWithFrontend();
	}
}

void UMetaSoundSource::PostDuplicate(EDuplicateMode::Type InDuplicateMode)
{
	Super::PostDuplicate(InDuplicateMode);

	// Guid is reset as asset may share implementation from
	// asset duplicated from but should not be registered as such.
	if (InDuplicateMode == EDuplicateMode::Normal)
	{
		Metasound::Frontend::FRegenerateAssetClassName().Transform(GetDocumentHandle());
	}
}

void UMetaSoundSource::PostEditChangeProperty(FPropertyChangedEvent& InEvent)
{
	using namespace Metasound;
	using namespace Metasound::Engine;
	using namespace Metasound::Frontend;

	Super::PostEditChangeProperty(InEvent);
	Metasound::PostEditChangeProperty(*this, InEvent);

	if (InEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UMetaSoundSource, OutputFormat))
	{
		ConvertFromPreset();

		bool bDidModifyDocument = false;
		switch (OutputFormat)
		{
			case EMetasoundSourceAudioFormat::Mono:
			{
				bDidModifyDocument = FModifyRootGraphInterfaces(
					{ OutputFormatStereoInterface::GetVersion() },
					{ OutputFormatMonoInterface::GetVersion() }
				).Transform(GetDocumentHandle());
			}
			break;

			case EMetasoundSourceAudioFormat::Stereo:
			{
				bDidModifyDocument = FModifyRootGraphInterfaces(
					{ OutputFormatMonoInterface::GetVersion() },
					{ OutputFormatStereoInterface::GetVersion() }
				).Transform(GetDocumentHandle());
			}
			break;

			default:
			{
				static_assert(static_cast<int32>(EMetasoundSourceAudioFormat::COUNT) == 2, "Possible missing format switch case coverage.");
			}
			break;
		}

		if (bDidModifyDocument)
		{
			ConformObjectDataToInterfaces();

			// Use the editor form of register to ensure other editors'
			// MetaSounds are auto-updated if they are referencing this graph.
			if (Graph)
			{
				Graph->RegisterGraphWithFrontend();
			}
			MarkMetasoundDocumentDirty();
		}
	}
}
#endif // WITH_EDITOR

bool UMetaSoundSource::ConformObjectDataToInterfaces()
{
	using namespace Metasound::Frontend;

	if (IsInterfaceDeclared(OutputFormatMonoInterface::GetVersion()))
	{
		if (OutputFormat != EMetasoundSourceAudioFormat::Mono || NumChannels != 1)
		{
			OutputFormat = EMetasoundSourceAudioFormat::Mono;
			NumChannels = 1;
			return true;
		}
	}

	if (IsInterfaceDeclared(OutputFormatStereoInterface::GetVersion()))
	{
		if (OutputFormat != EMetasoundSourceAudioFormat::Stereo || NumChannels != 2)
		{
			OutputFormat = EMetasoundSourceAudioFormat::Stereo;
			NumChannels = 2;
			return true;
		}
	}

	return false;
}


void UMetaSoundSource::BeginDestroy()
{
	UnregisterGraphWithFrontend();
	Super::BeginDestroy();
}

void UMetaSoundSource::PreSave(FObjectPreSaveContext InSaveContext)
{
	Super::PreSave(InSaveContext);
	Metasound::PreSaveAsset(*this, InSaveContext);
}

void UMetaSoundSource::Serialize(FArchive& InArchive)
{
	Super::Serialize(InArchive);
	Metasound::SerializeToArchive(*this, InArchive);
}

void UMetaSoundSource::SetReferencedAssetClassKeys(TSet<Metasound::Frontend::FNodeRegistryKey>&& InKeys)
{
	ReferencedAssetClassKeys = MoveTemp(InKeys);
}

TSet<FSoftObjectPath>& UMetaSoundSource::GetReferencedAssetClassCache()
{
	return ReferenceAssetClassCache;
}

const TSet<FSoftObjectPath>& UMetaSoundSource::GetReferencedAssetClassCache() const
{
	return ReferenceAssetClassCache;
}

#if WITH_EDITORONLY_DATA
UEdGraph* UMetaSoundSource::GetGraph()
{
	return Graph;
}

const UEdGraph* UMetaSoundSource::GetGraph() const
{
	return Graph;
}

UEdGraph& UMetaSoundSource::GetGraphChecked()
{
	check(Graph);
	return *Graph;
}

const UEdGraph& UMetaSoundSource::GetGraphChecked() const
{
	check(Graph);
	return *Graph;
}

FText UMetaSoundSource::GetDisplayName() const
{
	FString TypeName = UMetaSoundSource::StaticClass()->GetName();
	return FMetasoundAssetBase::GetDisplayName(MoveTemp(TypeName));
}

void UMetaSoundSource::SetRegistryAssetClassInfo(const Metasound::Frontend::FNodeClassInfo& InNodeInfo)
{
	Metasound::SetMetaSoundRegistryAssetClassInfo(*this, InNodeInfo);
}
#endif // WITH_EDITORONLY_DATA

void UMetaSoundSource::InitParameters(TArray<FAudioParameter>& InParametersToInit, FName InFeatureName)
{
	auto Sanitize = [](FAudioParameter& Parameter)
	{
		switch (Parameter.ParamType)
		{
			case EAudioParameterType::Boolean:
			{
				Parameter = FAudioParameter(Parameter.ParamName, Parameter.BoolParam);
			}
			break;

			case EAudioParameterType::BooleanArray:
			{
				TArray<bool> TempArray = Parameter.ArrayBoolParam;
				Parameter = FAudioParameter(Parameter.ParamName, MoveTemp(TempArray));
			}
			break;

			case EAudioParameterType::Float:
			{
				Parameter = FAudioParameter(Parameter.ParamName, Parameter.FloatParam);
			}
			break;

			case EAudioParameterType::FloatArray:
			{
				TArray<float> TempArray = Parameter.ArrayFloatParam;
				Parameter = FAudioParameter(Parameter.ParamName, MoveTemp(TempArray));
			}
			break;

			case EAudioParameterType::Integer:
			{
				Parameter = FAudioParameter(Parameter.ParamName, Parameter.IntParam);
			}
			break;

			case EAudioParameterType::IntegerArray:
			{
				TArray<int32> TempArray = Parameter.ArrayIntParam;
				Parameter = FAudioParameter(Parameter.ParamName, MoveTemp(TempArray));
			}
			break;

			case EAudioParameterType::Object:
			{
				Parameter = FAudioParameter(Parameter.ParamName, Parameter.ObjectParam);
			}
			break;

			case EAudioParameterType::ObjectArray:
			{
				TArray<UObject*> TempArray = Parameter.ArrayObjectParam;
				Parameter = FAudioParameter(Parameter.ParamName, MoveTemp(TempArray));
			}
			break;


			case EAudioParameterType::String:
			{
				Parameter = FAudioParameter(Parameter.ParamName, Parameter.StringParam);
			}
			break;

			case EAudioParameterType::StringArray:
			{
				TArray<FString> TempArray = Parameter.ArrayStringParam;
				Parameter = FAudioParameter(Parameter.ParamName, MoveTemp(TempArray));
			}
			break;

			case EAudioParameterType::None:
			default:
			break;
		}
	};

	auto ConstructProxies = [this, FeatureName = InFeatureName](FAudioParameter& OutParamToInit)
	{
		using namespace Metasound;
		using namespace Metasound::Frontend;

		const Audio::FProxyDataInitParams ProxyInitParams { FeatureName };

		switch (OutParamToInit.ParamType)
		{
			case EAudioParameterType::Object:
			{
				FDataTypeRegistryInfo DataTypeInfo;
				if (IDataTypeRegistry::Get().GetDataTypeInfo(OutParamToInit.ObjectParam, DataTypeInfo))
				{
					Audio::IProxyDataPtr ProxyPtr = IDataTypeRegistry::Get().CreateProxyFromUObject(DataTypeInfo.DataTypeName, OutParamToInit.ObjectParam);
					OutParamToInit.ObjectProxies.Emplace(MoveTemp(ProxyPtr));

					// Null out param as it is no longer needed (nor desired to be accessed once passed to the Audio Thread)
					OutParamToInit.ObjectParam = nullptr;
				}
			}
			break;

			case EAudioParameterType::ObjectArray:
			{
				for (TObjectPtr<UObject>& Object : OutParamToInit.ArrayObjectParam)
				{
					FDataTypeRegistryInfo DataTypeInfo;
					if (IDataTypeRegistry::Get().GetDataTypeInfo(Object, DataTypeInfo))
					{
						Audio::IProxyDataPtr ProxyPtr = IDataTypeRegistry::Get().CreateProxyFromUObject(DataTypeInfo.DataTypeName, Object);
						OutParamToInit.ObjectProxies.Emplace(MoveTemp(ProxyPtr));
					}
				}
				// Reset param array as it is no longer needed (nor desired to be accessed once passed to the Audio Thread).
				// All object manipulation hereafter should be done via proxies
				OutParamToInit.ArrayObjectParam.Reset();
			}
			break;

			default:
				break;
		}
	};

	TMap<FName, FName> InputNameTypeMap;
	Algo::Transform(GetDocumentChecked().RootGraph.Interface.Inputs, InputNameTypeMap, [](const FMetasoundFrontendClassInput& Input)
	{
		return TPair<FName, FName>(Input.Name, Input.TypeName);
	});

	for (FAudioParameter& Parameter : InParametersToInit)
	{
		Sanitize(Parameter);

		if (IsParameterValid(Parameter, InputNameTypeMap))
		{
			ConstructProxies(Parameter);
		}
		else
		{
			UE_LOG(LogMetaSound, Error, TEXT("Failed to set invalid parameter '%s': Either does not exist or is unsupported type"), *Parameter.ParamName.ToString());
		}
	}
}

void UMetaSoundSource::InitResources()
{
	using namespace Metasound::Frontend;
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(UMetaSoundSource::InitResources);

	FMetaSoundAssetRegistrationOptions RegOptions;
	RegOptions.bForceReregister = false;
	RegisterGraphWithFrontend(RegOptions);
}

Metasound::Frontend::FNodeClassInfo UMetaSoundSource::GetAssetClassInfo() const
{
	return { GetDocumentChecked().RootGraph, *GetPathName() };
}

bool UMetaSoundSource::IsPlayable() const
{
	// todo: cache off whether this metasound is buildable to an operator.
	return true;
}

bool UMetaSoundSource::SupportsSubtitles() const
{
	return Super::SupportsSubtitles();
}

float UMetaSoundSource::GetDuration() const
{
	// eh? this is kind of a weird field anyways.
	return Super::GetDuration();
}

bool UMetaSoundSource::ImplementsParameterInterface(Audio::FParameterInterfacePtr InInterface) const
{
	const FMetasoundFrontendVersion Version { InInterface->GetName(), { InInterface->GetVersion().Major, InInterface->GetVersion().Minor } };
	return GetDocumentChecked().Interfaces.Contains(Version);
}

ISoundGeneratorPtr UMetaSoundSource::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	using namespace Metasound;
	using namespace Metasound::Frontend;
	using namespace Metasound::Engine;

	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(UMetaSoundSource::CreateSoundGenerator);

	Duration = INDEFINITELY_LOOPING_DURATION;
	bLooping = true;

	SampleRate = InParams.SampleRate;
	FOperatorSettings InSettings = GetOperatorSettings(static_cast<FSampleRate>(SampleRate));
	FMetasoundEnvironment Environment = CreateEnvironment(InParams);

	TSharedPtr<const IGraph, ESPMode::ThreadSafe> MetasoundGraph = GetMetasoundCoreGraph();
	if (!MetasoundGraph.IsValid())
	{
		UE_LOG(LogMetaSound, Error, TEXT("Cannot create UMetaSoundSource SoundGenerator [Name:%s]. Failed to create MetaSound Graph"), *GetName());
		return ISoundGeneratorPtr(nullptr);
	}

	FMetasoundGeneratorInitParams InitParams =
	{
		InSettings,
		MetasoundGraph,
		Environment,
		GetName(),
		GetAudioOutputVertexKeys()
	};

	return ISoundGeneratorPtr(new FMetasoundGenerator(MoveTemp(InitParams)));
}

bool UMetaSoundSource::GetAllDefaultParameters(TArray<FAudioParameter>& OutParameters) const
{
	using namespace Metasound;
	using namespace Metasound::Frontend;
	using namespace Metasound::Engine;

	TArray<const FMetasoundFrontendClassInput*> TransmittableInputs = GetTransmittableClassInputs();
	for(const FMetasoundFrontendClassInput* Input : TransmittableInputs)
	{
		check(Input);

		FAudioParameter Params;
		Params.ParamName = Input->Name;

		switch (Input->DefaultLiteral.GetType())
		{
			case EMetasoundFrontendLiteralType::Boolean:
			{
				Params.ParamType = EAudioParameterType::Boolean;
				ensure(Input->DefaultLiteral.TryGet(Params.BoolParam));
			}
			break;

			case EMetasoundFrontendLiteralType::BooleanArray:
			{
				Params.ParamType = EAudioParameterType::BooleanArray;
				ensure(Input->DefaultLiteral.TryGet(Params.ArrayBoolParam));
			}
			break;

			case EMetasoundFrontendLiteralType::Integer:
			{
				Params.ParamType = EAudioParameterType::Integer;
				ensure(Input->DefaultLiteral.TryGet(Params.IntParam));
			}
			break;

			case EMetasoundFrontendLiteralType::IntegerArray:
			{
				Params.ParamType = EAudioParameterType::IntegerArray;
				ensure(Input->DefaultLiteral.TryGet(Params.ArrayIntParam));
			}
			break;

			case EMetasoundFrontendLiteralType::Float:
			{
				Params.ParamType = EAudioParameterType::Float;
				ensure(Input->DefaultLiteral.TryGet(Params.FloatParam));
			}
			break;

			case EMetasoundFrontendLiteralType::FloatArray:
			{
				Params.ParamType = EAudioParameterType::FloatArray;
				ensure(Input->DefaultLiteral.TryGet(Params.ArrayFloatParam));
			}
			break;

			case EMetasoundFrontendLiteralType::String:
			{
				Params.ParamType = EAudioParameterType::String;
				ensure(Input->DefaultLiteral.TryGet(Params.StringParam));
			}
			break;

			case EMetasoundFrontendLiteralType::StringArray:
			{
				Params.ParamType = EAudioParameterType::StringArray;
				ensure(Input->DefaultLiteral.TryGet(Params.ArrayStringParam));
			}
			break;

			case EMetasoundFrontendLiteralType::UObject:
			{
				Params.ParamType = EAudioParameterType::Object;
				UObject* Object = nullptr;
				ensure(Input->DefaultLiteral.TryGet(Object));
				Params.ObjectParam = Object;
			}
			break;

			case EMetasoundFrontendLiteralType::UObjectArray:
			{
				Params.ParamType = EAudioParameterType::ObjectArray;
				ensure(Input->DefaultLiteral.TryGet(Params.ArrayObjectParam));
			}
			break;

			default:
			break;
		}

		if (Params.ParamType != EAudioParameterType::None)
		{
			OutParameters.Add(Params);
		}
	}
	return true;
}

bool UMetaSoundSource::IsParameterValid(const FAudioParameter& InParameter) const
{
	TMap<FName, FName> InputNameTypeMap;
	Algo::Transform(GetDocumentChecked().RootGraph.Interface.Inputs, InputNameTypeMap, [] (const FMetasoundFrontendClassInput& Input)
	{
		return TPair<FName, FName>(Input.Name, Input.TypeName);
	});
	return IsParameterValid(InParameter, InputNameTypeMap);
}

bool UMetaSoundSource::IsParameterValid(const FAudioParameter& InParameter, const TMap<FName, FName>& InInputNameTypeMap) const
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	const FName* TypeName = InInputNameTypeMap.Find(InParameter.ParamName);
	if (!TypeName)
	{
		return false;
	}

	bool bIsValid = false;
	switch (InParameter.ParamType)
	{
		case EAudioParameterType::Boolean:
		{
			FDataTypeRegistryInfo DataTypeInfo;
			bIsValid = IDataTypeRegistry::Get().GetDataTypeInfo(*TypeName, DataTypeInfo);
			bIsValid &= DataTypeInfo.bIsBoolParsable;
		}
		break;

		case EAudioParameterType::BooleanArray:
		{
			FDataTypeRegistryInfo DataTypeInfo;
			bIsValid = IDataTypeRegistry::Get().GetDataTypeInfo(*TypeName, DataTypeInfo);
			bIsValid &= DataTypeInfo.bIsBoolArrayParsable;
		}
		break;

		case EAudioParameterType::Float:
		{
			FDataTypeRegistryInfo DataTypeInfo;
			bIsValid = IDataTypeRegistry::Get().GetDataTypeInfo(*TypeName, DataTypeInfo);
			bIsValid &= DataTypeInfo.bIsFloatParsable;
		}
		break;

		case EAudioParameterType::FloatArray:
		{
			FDataTypeRegistryInfo DataTypeInfo;
			bIsValid = IDataTypeRegistry::Get().GetDataTypeInfo(*TypeName, DataTypeInfo);
			bIsValid &= DataTypeInfo.bIsFloatArrayParsable;
		}
		break;

		case EAudioParameterType::Integer:
		{
			FDataTypeRegistryInfo DataTypeInfo;
			bIsValid = IDataTypeRegistry::Get().GetDataTypeInfo(*TypeName, DataTypeInfo);
			bIsValid &= DataTypeInfo.bIsIntParsable;
		}
		break;

		case EAudioParameterType::IntegerArray:
		{
			FDataTypeRegistryInfo DataTypeInfo;
			bIsValid = IDataTypeRegistry::Get().GetDataTypeInfo(*TypeName, DataTypeInfo);
			bIsValid &= DataTypeInfo.bIsIntArrayParsable;

		}
		break;

		case EAudioParameterType::Object:
		{
			FDataTypeRegistryInfo DataTypeInfo;
			bIsValid = IDataTypeRegistry::Get().GetDataTypeInfo(InParameter.ObjectParam, DataTypeInfo);
			bIsValid &= DataTypeInfo.bIsProxyParsable;
			bIsValid &= DataTypeInfo.DataTypeName == *TypeName;
		}
		break;

		case EAudioParameterType::ObjectArray:
		{
			bIsValid = true;

			for (UObject* Object : InParameter.ArrayObjectParam)
			{
				FDataTypeRegistryInfo DataTypeInfo;
				bIsValid = IDataTypeRegistry::Get().GetDataTypeInfo(InParameter.ObjectParam, DataTypeInfo);
				bIsValid &= DataTypeInfo.bIsProxyParsable;
				bIsValid &= DataTypeInfo.DataTypeName == *TypeName;

				if (!bIsValid)
				{
					break;
				}
			}
		}
		break;

		case EAudioParameterType::String:
		{
			FDataTypeRegistryInfo DataTypeInfo;
			bIsValid = IDataTypeRegistry::Get().GetDataTypeInfo(*TypeName, DataTypeInfo);
			bIsValid &= DataTypeInfo.bIsStringParsable;
		}
		break;

		case EAudioParameterType::StringArray:
		{
			FDataTypeRegistryInfo DataTypeInfo;
			bIsValid = IDataTypeRegistry::Get().GetDataTypeInfo(*TypeName, DataTypeInfo);
			bIsValid &= DataTypeInfo.bIsStringArrayParsable;
		}
		break;

		case EAudioParameterType::NoneArray:
		{
			FDataTypeRegistryInfo DataTypeInfo;
			bIsValid = IDataTypeRegistry::Get().GetDataTypeInfo(*TypeName, DataTypeInfo);
			bIsValid &= DataTypeInfo.bIsDefaultArrayParsable;
		}
		case EAudioParameterType::None:
		default:
		{
			FDataTypeRegistryInfo DataTypeInfo;
			bIsValid = IDataTypeRegistry::Get().GetDataTypeInfo(*TypeName, DataTypeInfo);
			bIsValid &= DataTypeInfo.bIsDefaultParsable;
		}
		break;
	}

	return bIsValid;
}

bool UMetaSoundSource::IsLooping() const
{
	return !IsOneShot();
}

bool UMetaSoundSource::IsOneShot() const
{
	using namespace Metasound::Frontend;

	// If the metasound source implements the one-shot interface, then it's a one-shot metasound
	return IsInterfaceDeclared(SourceOneShotInterface::GetVersion());
}

TUniquePtr<Audio::IParameterTransmitter> UMetaSoundSource::CreateParameterTransmitter(Audio::FParameterTransmitterInitParams&& InParams) const
{
	Metasound::FMetaSoundParameterTransmitter::FInitParams InitParams(GetOperatorSettings(InParams.SampleRate), InParams.InstanceID);

	for (const FSendInfoAndVertexName& InfoAndName : FMetasoundAssetBase::GetSendInfos(InParams.InstanceID))
	{
		InitParams.Infos.Add(InfoAndName.SendInfo);
	}

	TUniquePtr<Audio::IParameterTransmitter> NewTransmitter = MakeUnique<Metasound::FMetaSoundParameterTransmitter>(InitParams);
	for (FAudioParameter& AudioParam : InParams.DefaultParams)
	{
		NewTransmitter->SetParameter(MoveTemp(AudioParam));
	}
	InParams.DefaultParams.Reset();

	return NewTransmitter;
}

Metasound::FOperatorSettings UMetaSoundSource::GetOperatorSettings(Metasound::FSampleRate InSampleRate) const
{
	const float BlockRate = FMath::Clamp(Metasound::ConsoleVariables::BlockRate, 1.0f, 1000.0f);
	return Metasound::FOperatorSettings(InSampleRate, BlockRate);
}

Metasound::FMetasoundEnvironment UMetaSoundSource::CreateEnvironment() const
{
	using namespace Metasound;
	using namespace Metasound::Frontend;

	FMetasoundEnvironment Environment;
	Environment.SetValue<uint32>(SourceInterface::Environment::SoundUniqueID, GetUniqueID());

	return Environment;
}

Metasound::FMetasoundEnvironment UMetaSoundSource::CreateEnvironment(const FSoundGeneratorInitParams& InParams) const
{
	using namespace Metasound;
	using namespace Metasound::Engine;
	using namespace Metasound::Frontend;

	FMetasoundEnvironment Environment = CreateEnvironment();
	Environment.SetValue<bool>(SourceInterface::Environment::IsPreview, InParams.bIsPreviewSound);
	Environment.SetValue<uint64>(SourceInterface::Environment::TransmitterID, InParams.InstanceID);
	Environment.SetValue<Audio::FDeviceId>(SourceInterface::Environment::DeviceID, InParams.AudioDeviceID);

#if WITH_METASOUND_DEBUG_ENVIRONMENT
	Environment.SetValue<FString>(SourceInterface::Environment::GraphName, GetFullName());
#endif // WITH_METASOUND_DEBUG_ENVIRONMENT

	return Environment;
}

Metasound::FMetasoundEnvironment UMetaSoundSource::CreateEnvironment(const Audio::FParameterTransmitterInitParams& InParams) const
{
	using namespace Metasound;
	using namespace Metasound::Engine;
	using namespace Metasound::Frontend;

	FMetasoundEnvironment Environment = CreateEnvironment();
	Environment.SetValue<uint64>(SourceInterface::Environment::TransmitterID, InParams.InstanceID);

	return Environment;
}

const TArray<Metasound::FVertexName>& UMetaSoundSource::GetAudioOutputVertexKeys() const
{
	using namespace Metasound::SourcePrivate;

	if (const TArray<Metasound::FVertexName>* ArrayKeys = GetFormatOutputVertexKeys().Find(OutputFormat))
	{
		return *ArrayKeys;
	}
	else
	{
		// Unhandled audio format. Need to update audio output format vertex key map.
		checkNoEntry();
		static const TArray<Metasound::FVertexName> Empty;
		return Empty;
	}
}
#undef LOCTEXT_NAMESPACE // MetaSound
