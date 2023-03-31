// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundOutputSubsystem.h"

#include "MetasoundGenerator.h"
#include "MetasoundSource.h"
#include "MetasoundTrace.h"
#include "Analysis/MetasoundFrontendAnalyzerAddress.h"
#include "Components/AudioComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

TMap<FName, FName> UMetaSoundOutputSubsystem::PassthroughAnalyzers{};

bool UMetaSoundOutputSubsystem::WatchOutput(
	UAudioComponent* AudioComponent,
	const FName OutputName,
	const FOnMetasoundOutputValueChanged& OnOutputValueChanged,
	FName AnalyzerName,
	FName AnalyzerOutputName)
{
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(UMetasoundOutputSubsystem::WatchOutput);
	
	FGeneratorInfo* GeneratorInfo = FindOrAddGeneratorInfo(AudioComponent);

	if (nullptr == GeneratorInfo)
	{
		return false;
	}
	
	check(GeneratorInfo->Source.IsValid());

	// Find the node id and type name
	const Metasound::Frontend::FNodeHandle Node = GeneratorInfo->Source->GetRootGraphHandle()->GetOutputNodeWithName(OutputName);

	if (!Node->IsValid())
	{
		return false;
	}

	const FGuid NodeId = Node->GetID();

	// We expect an output node to only have one output
	if (Node->GetNumOutputs() != 1)
	{
		return false;
	}

	const FName TypeName = Node->GetOutputs()[0]->GetDataType();

	// Make the analyzer address
	Metasound::Frontend::FAnalyzerAddress AnalyzerAddress;
	AnalyzerAddress.DataType = TypeName;
	AnalyzerAddress.InstanceID = AudioComponent->GetAudioComponentID();
	AnalyzerAddress.OutputName = OutputName;

	if (AnalyzerName.IsNone())
	{
		if (!PassthroughAnalyzers.Contains(TypeName))
		{
			return false;
		}

		AnalyzerName = PassthroughAnalyzers[TypeName];
		AnalyzerOutputName = "Value";
	}
	AnalyzerAddress.AnalyzerName = AnalyzerName;
	
	AnalyzerAddress.AnalyzerInstanceID = FGuid::NewGuid();
	AnalyzerAddress.NodeID = NodeId;

	// if we already have a generator, go ahead and make the analyzer and watcher
	if (const TSharedPtr<Metasound::FMetasoundGenerator, ESPMode::ThreadSafe> PinnedGenerator = GeneratorInfo->Generator.Pin())
	{
		CreateAnalyzerAndWatcher(PinnedGenerator, MoveTemp(AnalyzerAddress));
	}
	// otherwise enqueue it for later
	else
	{
		GeneratorInfo->AnalyzersToCreate.Enqueue(MoveTemp(AnalyzerAddress));
	}

	// either way, add the delegate to the map
	GeneratorInfo->OutputChangedDelegates.FindOrAdd(OutputName).FindOrAdd(AnalyzerOutputName).AddUnique(OnOutputValueChanged);
	
	return true;
}

bool UMetaSoundOutputSubsystem::IsTickable() const
{
	return TrackedGenerators.Num() > 0;
}

void UMetaSoundOutputSubsystem::Tick(float DeltaTime)
{
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(UMetasoundOutputSubsystem::Tick);
	 
	for (auto GeneratorIt = TrackedGenerators.CreateIterator(); GeneratorIt; ++GeneratorIt)
	{
		if (!GeneratorIt->IsValid())
		{
			GeneratorIt.RemoveCurrent();
		}
		else
		{
			for (Metasound::Private::FMetasoundOutputWatcher& Watcher : GeneratorIt->OutputWatchers)
			{
				Watcher.Update([&GeneratorIt](FName OutputName, const FMetaSoundOutput& Output)
				{
					GeneratorIt->HandleOutputChanged(OutputName, Output);
				});
			}
		}
	}
}

TStatId UMetaSoundOutputSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMetasoundGeneratorAccessSubsystem, STATGROUP_Tickables);
}

void UMetaSoundOutputSubsystem::RegisterPassthroughAnalyzerForType(const FName TypeName, const FName AnalyzerName)
{
	check(!PassthroughAnalyzers.Contains(TypeName));
	PassthroughAnalyzers.Add(TypeName, AnalyzerName);
}

void UMetaSoundOutputSubsystem::FGeneratorInfo::HandleOutputChanged(FName OutputName, const FMetaSoundOutput& Output)
{
	if (TMap<FName, FOnOutputValueChangedMulticast>* Map = OutputChangedDelegates.Find(OutputName))
	{
		if (const FOnOutputValueChangedMulticast* Delegate = Map->Find(Output.Name))
		{
			Delegate->Broadcast(Output);
		}
	}
}

bool UMetaSoundOutputSubsystem::FGeneratorInfo::IsValid() const
{
	return AudioComponent.IsValid() && Source.IsValid();
}

UMetaSoundOutputSubsystem::FGeneratorInfo* UMetaSoundOutputSubsystem::FindOrAddGeneratorInfo(UAudioComponent* AudioComponent)
{
	if (!ensure(nullptr != AudioComponent))
	{
		return  nullptr;
	}
	
	const int64 AudioComponentId = AudioComponent->GetAudioComponentID();

	// already in the list...
	if (FGeneratorInfo* Info = TrackedGenerators.FindByPredicate([AudioComponentId](const FGeneratorInfo& GeneratorInfo)
	{
		return GeneratorInfo.AudioComponent.IsValid()
		&& GeneratorInfo.AudioComponent->GetAudioComponentID() == AudioComponentId;
	}))
	{
		if (Info->Source.IsValid())
		{
			return Info;
		}
		// Source is no longer valid, remove info
		OnGeneratorDestroyed(AudioComponentId, Info->Generator.Pin());
	}

	UMetaSoundSource* Source = Cast<UMetaSoundSource>(AudioComponent->GetSound());
	
	if (nullptr == Source)
	{
		return nullptr;
	}

	// set up a new entry
	TrackedGenerators.AddDefaulted();
	FGeneratorInfo& GeneratorInfo = TrackedGenerators.Last();
	GeneratorInfo.AudioComponent = AudioComponent;
	GeneratorInfo.Source = Source;

	// attempt to get the generator
	GeneratorInfo.Generator = GeneratorInfo.Source->GetGeneratorForAudioComponent(AudioComponentId);

	// if that didn't work, set up the generator creation callback
	if (nullptr == GeneratorInfo.Generator)
	{
		GeneratorInfo.OnCreatedHandle = GeneratorInfo.Source->OnGeneratorInstanceCreated.AddUObject(
			this,
			&UMetaSoundOutputSubsystem::OnGeneratorCreated);
	}
	
	// always set up the generator destruction callback
	GeneratorInfo.OnDestroyedHandle = GeneratorInfo.Source->OnGeneratorInstanceDestroyed.AddUObject(
		this,
		&UMetaSoundOutputSubsystem::OnGeneratorDestroyed);

	return &GeneratorInfo;
}

void UMetaSoundOutputSubsystem::OnGeneratorCreated(
	const uint64 InAudioComponentId,
	const TSharedPtr<Metasound::FMetasoundGenerator> Generator)
{
	for (auto GeneratorIt = TrackedGenerators.CreateIterator(); GeneratorIt; ++GeneratorIt)
	{
		if (!GeneratorIt->IsValid())
		{
			GeneratorIt.RemoveCurrent();
		}
		else if (GeneratorIt->AudioComponent->GetAudioComponentID() == InAudioComponentId)
		{
			GeneratorIt->Generator = Generator;
			GeneratorIt->Source->OnGeneratorInstanceCreated.Remove(GeneratorIt->OnCreatedHandle);

			// create analyzers
			Metasound::Frontend::FAnalyzerAddress Address;
			while (GeneratorIt->AnalyzersToCreate.Dequeue(Address))
			{
				CreateAnalyzerAndWatcher(Generator, MoveTemp(Address));
			}
		}
	}
}

void UMetaSoundOutputSubsystem::OnGeneratorDestroyed(
	const uint64 InAudioComponentId,
	TSharedPtr<Metasound::FMetasoundGenerator>)
{
	for (auto GeneratorIt = TrackedGenerators.CreateIterator(); GeneratorIt; ++GeneratorIt)
	{
		if (!GeneratorIt->IsValid())
		{
			GeneratorIt.RemoveCurrent();
		}
		else if (GeneratorIt->AudioComponent.IsValid() && GeneratorIt->AudioComponent->GetAudioComponentID() == InAudioComponentId)
		{
			GeneratorIt->Source->OnGeneratorInstanceDestroyed.Remove(GeneratorIt->OnDestroyedHandle);
			GeneratorIt.RemoveCurrent();
			return;
		}
	}
}

void UMetaSoundOutputSubsystem::CreateAnalyzerAndWatcher(
	const TSharedPtr<Metasound::FMetasoundGenerator>& Generator,
	Metasound::Frontend::FAnalyzerAddress&& AnalyzerAddress)
{
	if (!Generator.IsValid())
	{
		return;
	}
	
	// Find the generator info and create the watcher
	{
		FGeneratorInfo* GeneratorInfo = TrackedGenerators.FindByPredicate([&Generator](const FGeneratorInfo& Info)
		{
			return Info.Generator.HasSameObject(Generator.Get());
		});

		if (nullptr == GeneratorInfo)
		{
			return;
		}

		if (!GeneratorInfo->OutputWatchers.ContainsByPredicate(
			[&AnalyzerAddress](const Metasound::Private::FMetasoundOutputWatcher& Watcher)
			{
				return AnalyzerAddress.OutputName == Watcher.Name;
			}))
		{
			GeneratorInfo->OutputWatchers.Emplace(MoveTemp(AnalyzerAddress), Generator->OperatorSettings);
		}
	}

	// Create the analyzer (will skip if there's already one)
	Generator->AddOutputVertexAnalyzer(AnalyzerAddress);
}
