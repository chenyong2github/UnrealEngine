// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundGeneratorHandle.h"

#include "Components/AudioComponent.h"
#include "AudioDeviceManager.h"
#include "MetasoundGenerator.h"
#include "MetasoundSource.h"
#include "MetasoundTrace.h"
#include "MetasoundParameterPack.h"

TMap<FName, FName> UMetasoundGeneratorHandle::PassthroughAnalyzers{};

UMetasoundGeneratorHandle* UMetasoundGeneratorHandle::CreateMetaSoundGeneratorHandle(UAudioComponent* OnComponent)
{
	METASOUND_LLM_SCOPE;
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(UMetasoundGeneratorHandle::CreateMetaSoundGeneratorHandle);
	
	if (!OnComponent)
	{
		return nullptr;
	}

	UMetasoundGeneratorHandle* Result = NewObject<UMetasoundGeneratorHandle>();
	Result->SetAudioComponent(OnComponent);
	return Result;
}

void UMetasoundGeneratorHandle::BeginDestroy()
{
	Super::BeginDestroy();
	DetachGeneratorDelegates();
}

bool UMetasoundGeneratorHandle::IsValid() const
{
	return AudioComponent.IsValid();
}

uint64 UMetasoundGeneratorHandle::GetAudioComponentId() const
{
	if (AudioComponent.IsValid())
	{
		return AudioComponent->GetAudioComponentID();
	}

	return INDEX_NONE;
}

void UMetasoundGeneratorHandle::ClearCachedData()
{
	DetachGeneratorDelegates();
	AudioComponent        = nullptr;
	CachedMetasoundSource = nullptr;
	CachedGeneratorPtr    = nullptr;
	CachedParameterPack   = nullptr;
}

void UMetasoundGeneratorHandle::SetAudioComponent(UAudioComponent* InAudioComponent)
{
	if (InAudioComponent != AudioComponent)
	{
		ClearCachedData();
		AudioComponent   = InAudioComponent;
	}
}

void UMetasoundGeneratorHandle::CacheMetasoundSource()
{
	if (!AudioComponent.IsValid())
	{
		return;
	}

	UMetaSoundSource* CurrentMetasoundSource = Cast<UMetaSoundSource>(AudioComponent->GetSound());
	if (CachedMetasoundSource == CurrentMetasoundSource)
	{
		return;
	}

	DetachGeneratorDelegates();
	CachedGeneratorPtr    = nullptr;
	CachedMetasoundSource = CurrentMetasoundSource;

	if (CachedMetasoundSource.IsValid())
	{
		AttachGeneratorDelegates();
	}
}

void UMetasoundGeneratorHandle::AttachGeneratorDelegates()
{
	// These delegates can be called on a separate thread, so we need to take steps
	// to assure this UObject hasn't been garbage collected before trying to dereference. 
	// That is why we capture a TWeakObjectPtr and try to dereference that later!
	GeneratorCreatedDelegateHandle = CachedMetasoundSource->OnGeneratorInstanceCreated.AddLambda(
		[WeakGeneratorHandlePtr = TWeakObjectPtr<UMetasoundGeneratorHandle>(this),
		StatId = this->GetStatID(true)]
		(uint64 InAudioComponentId, TSharedPtr<Metasound::FMetasoundGenerator> InGenerator)
		{
			// We are in the audio render (or control) thread here, so create a "dispatch task" to be
			// executed later on the game thread...
			FFunctionGraphTask::CreateAndDispatchWhenReady(
				[WeakGeneratorHandlePtr, InAudioComponentId, InGenerator]()
				{
					METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(UMetasoundGeneratorHandle::CallingGeneratorAttachedDelegates);
					check(IsInGameThread());
					// Now, since we are in the game thread, try to dereference the pointer to
					// to the UMetasoundGeneratorHandle. This should only succeed if the UObject
					// hasn't been garbage collected.
					if (UMetasoundGeneratorHandle* TheHandle = WeakGeneratorHandlePtr.Get())
					{
						TheHandle->OnSourceCreatedAGenerator(InAudioComponentId, InGenerator);
					}
				}, 
				StatId, nullptr, ENamedThreads::GameThread);
		});
	GeneratorDestroyedDelegateHandle = CachedMetasoundSource->OnGeneratorInstanceDestroyed.AddLambda(
		[WeakGeneratorHandlePtr = TWeakObjectPtr<UMetasoundGeneratorHandle>(this),
		StatId = this->GetStatID(true)]
		(uint64 InAudioComponentId, TSharedPtr<Metasound::FMetasoundGenerator> InGenerator)
		{
			// We are in the audio render (or control) thread here, so create a "dispatch task" to be
			// executed later on the game thread...
			FFunctionGraphTask::CreateAndDispatchWhenReady(
				[WeakGeneratorHandlePtr, InAudioComponentId, InGenerator]()
				{
					METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(UMetasoundGeneratorHandle::CallingGeneratorDetachedDelegates);
					check(IsInGameThread());
					// Now, since we are in the game thread, try to dereference the pointer to
					// to the UMetasoundGeneratorHandle. This should only succeed if the UObject
					// hasn't been garbage collected.
					if (UMetasoundGeneratorHandle* TheHandle = WeakGeneratorHandlePtr.Get())
					{
						TheHandle->OnSourceDestroyedAGenerator(InAudioComponentId, InGenerator);
					}
				},
				StatId, nullptr, ENamedThreads::GameThread);
		});
}

void UMetasoundGeneratorHandle::AttachGraphChangedDelegate()
{
	if (!CachedGeneratorPtr.IsValid() || !OnGeneratorsGraphChanged.IsBound())
		return;

	TSharedPtr<Metasound::FMetasoundGenerator> Generator = CachedGeneratorPtr.Pin();
	if (Generator)
	{
		// We're about to add a delegate to the generator. This delegate will be called on 
		// the audio render thread so we need to take steps to assure this UMetasoundGeneratorHandle
		// hasn't been garbage collected before trying to dereference it later when the delegate "fires".
		// That is why we capture a TWeakObjectPtr and try to dereference that later!
		GeneratorGraphChangedDelegateHandle = Generator->OnSetGraph.AddLambda(
			[WeakGeneratorHandlePtr = TWeakObjectPtr<UMetasoundGeneratorHandle>(this),
			StatId = this->GetStatID(true)]
			()
			{
				// We are in the audio render thread here, so create a "dispatch task" to be
				// executed later on the game thread...
				FFunctionGraphTask::CreateAndDispatchWhenReady(
					[WeakGeneratorHandlePtr]()
					{
						METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(UMetasoundGeneratorHandle::CallingGraphChangedDelegates);
						check(IsInGameThread());
						// Now, since we are in the game thread, try to dereference the pointer to
						// to the UMetasoundGeneratorHandle. This should only succeed if the UObject
						// hasn't been garbage collected.
						if (UMetasoundGeneratorHandle* TheHandle = WeakGeneratorHandlePtr.Get())
						{
							TheHandle->OnGeneratorsGraphChanged.Broadcast();
						}
					},
					StatId, nullptr, ENamedThreads::GameThread);
		});
}
}

void UMetasoundGeneratorHandle::DetachGeneratorDelegates()
{
	// First detach any callbacks that tell us when a generator is created or destroyed
	// for the UMetasoundSource of interest...
	if (CachedMetasoundSource.IsValid())
	{
	CachedMetasoundSource->OnGeneratorInstanceCreated.Remove(GeneratorCreatedDelegateHandle);
	GeneratorCreatedDelegateHandle.Reset();
	CachedMetasoundSource->OnGeneratorInstanceDestroyed.Remove(GeneratorDestroyedDelegateHandle);
	GeneratorDestroyedDelegateHandle.Reset();
}
	// Now detach any callback we may have registered to get callbacks when the generator's
	// graph has been changed...
	TSharedPtr<Metasound::FMetasoundGenerator> PinnedGenerator = CachedGeneratorPtr.Pin();
	if (PinnedGenerator)
	{
		PinnedGenerator->OnSetGraph.Remove(GeneratorGraphChangedDelegateHandle);
		GeneratorGraphChangedDelegateHandle.Reset();
	}
}

TSharedPtr<Metasound::FMetasoundGenerator> UMetasoundGeneratorHandle::PinGenerator()
{
	TSharedPtr<Metasound::FMetasoundGenerator> PinnedGenerator = CachedGeneratorPtr.Pin();
	if (PinnedGenerator.IsValid() || !CachedMetasoundSource.IsValid())
	{
		return PinnedGenerator;
	}

	// The first attempt to pin failed, so reach out to the MetaSoundSource and see if it has a 
	// generator for our AudioComponent...
	check(AudioComponent.IsValid()); // expect the audio component to still be valid if the generator is.
	CachedGeneratorPtr = CachedMetasoundSource->GetGeneratorForAudioComponent(AudioComponent->GetAudioComponentID());
	PinnedGenerator    = CachedGeneratorPtr.Pin();
	return PinnedGenerator;
}

bool UMetasoundGeneratorHandle::ApplyParameterPack(UMetasoundParameterPack* Pack)
{
	METASOUND_LLM_SCOPE;
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(UMetasoundGeneratorHandle::ApplyParameterPack);
	
	if (!Pack)
	{
		return false;
	}

	// Create a copy of the parameter pack and cache it.
	CachedParameterPack = Pack->GetCopyOfParameterStorage();

	// No point in continuing if the parameter pack is not valid for any reason.
	if (!CachedParameterPack.IsValid())
	{
		return false;
	}

	// Assure that our MetaSoundSource is up to date. It is possible that this has been 
	// changed via script since we were first created.
	CacheMetasoundSource();

	// Now we can try to pin the generator.
	TSharedPtr<Metasound::FMetasoundGenerator> PinnedGenerator = PinGenerator();

	if (!PinnedGenerator.IsValid())
	{
		// Failed to pin the generator, but we have cached the parameter pack,
		// so if our delegate gets called when a new generator is created we can 
		// apply the cached parameters then.
		return false;
	}

	// Finally... send down the parameter pack.
	PinnedGenerator->QueueParameterPack(CachedParameterPack);
	return true;
}

TSharedPtr<Metasound::FMetasoundGenerator> UMetasoundGeneratorHandle::GetGenerator()
{
	// Attach if we aren't attached, check for changes, etc...
	CacheMetasoundSource();

	return PinGenerator();
}

FDelegateHandle UMetasoundGeneratorHandle::AddGraphSetCallback(const UMetasoundGeneratorHandle::FOnSetGraph& Delegate)
{
	
	FDelegateHandle Handle = OnGeneratorsGraphChanged.Add(Delegate);
	CacheMetasoundSource();
	AttachGraphChangedDelegate();
	return Handle;
}

bool UMetasoundGeneratorHandle::RemoveGraphSetCallback(const FDelegateHandle& Handle)
{
	return OnGeneratorsGraphChanged.Remove(Handle);
}

bool UMetasoundGeneratorHandle::WatchOutput(
	FName OutputName,
	const FOnMetasoundOutputValueChanged& OnOutputValueChanged,
	FName AnalyzerName,
	FName AnalyzerOutputName)
{
	METASOUND_LLM_SCOPE;
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(UMetasoundGeneratorHandle::WatchOutput);

	if (!IsValid())
	{
		return false;
	}

	CacheMetasoundSource();

	if (!CachedMetasoundSource.IsValid())
	{
		return false;
	}

	// Find the node id and type name
	const Metasound::Frontend::FNodeHandle Node = CachedMetasoundSource->GetRootGraphHandle()->GetOutputNodeWithName(OutputName);

	if (!Node->IsValid())
	{
		return false;
	}

	const FGuid NodeId = Node->GetID();

	// We expect an output node to have exactly one output
	if (!ensure(Node->GetNumOutputs() == 1))
	{
		return false;
	}

	const FName TypeName = Node->GetOutputs()[0]->GetDataType();

	// Make the analyzer address
	Metasound::Frontend::FAnalyzerAddress AnalyzerAddress;
	AnalyzerAddress.DataType = TypeName;
	AnalyzerAddress.InstanceID = AudioComponent->GetAudioComponentID();
	AnalyzerAddress.OutputName = OutputName;

	// If no analyzer name was provided, try to find a passthrough analyzer
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
	if (const TSharedPtr<Metasound::FMetasoundGenerator> PinnedGenerator = PinGenerator())
	{
		CreateAnalyzerAndWatcher(PinnedGenerator, MoveTemp(AnalyzerAddress));
	}
	// otherwise enqueue it for later
	else
	{
		OutputAnalyzersToAdd.Enqueue(MoveTemp(AnalyzerAddress));
	}
	
	// either way, add the delegate to the map
	OutputListenerMap.FindOrAdd(OutputName).FindOrAdd(AnalyzerOutputName).AddUnique(OnOutputValueChanged);

	return true;
}

void UMetasoundGeneratorHandle::RegisterPassthroughAnalyzerForType(FName TypeName, FName AnalyzerName)
{
	check(!PassthroughAnalyzers.Contains(TypeName));
	PassthroughAnalyzers.Add(TypeName, AnalyzerName);
}

void UMetasoundGeneratorHandle::UpdateWatchers()
{
	METASOUND_LLM_SCOPE;
	METASOUND_TRACE_CPUPROFILER_EVENT_SCOPE(UMetasoundGeneratorHandle::UpdateWatchers);

	for (auto& Watcher : OutputWatchers)
	{
		Watcher.Update([this](FName OutputName, const FMetaSoundOutput& Output)
		{
			if (TMap<FName, FOnOutputValueChangedMulticast>* Map = OutputListenerMap.Find(OutputName))
			{
				if (const FOnOutputValueChangedMulticast* Delegate = Map->Find(Output.Name))
				{
					Delegate->Broadcast(OutputName, Output);
				}
			}
		});
	}
}

void UMetasoundGeneratorHandle::OnSourceCreatedAGenerator(uint64 InAudioComponentId, TSharedPtr<Metasound::FMetasoundGenerator> InGenerator)
{
	check(IsInGameThread());

	if (!AudioComponent.IsValid())
	{
		return;
	}
	
	if (InAudioComponentId == AudioComponent->GetAudioComponentID())
	{
		CachedGeneratorPtr = InGenerator;
		if (InGenerator)
		{
			// If there is a parameter pack to apply, apply it
			if (CachedParameterPack)
			{
				InGenerator->QueueParameterPack(CachedParameterPack);
			}

			// If there are analyzers to create, create them
			{
				Metasound::Frontend::FAnalyzerAddress Address;
				while (OutputAnalyzersToAdd.Dequeue(Address))
				{
					CreateAnalyzerAndWatcher(InGenerator, MoveTemp(Address));
				}
			}
			
			OnGeneratorHandleAttached.Broadcast();
			// If anyone has told us they are interested in being notified when a generator's 
			// graph has changed go ahead and set that up now...
			AttachGraphChangedDelegate();
		}
	}
}

void UMetasoundGeneratorHandle::OnSourceDestroyedAGenerator(uint64 InAudioComponentId, TSharedPtr<Metasound::FMetasoundGenerator> InGenerator)
{
	check(IsInGameThread());

	if (!AudioComponent.IsValid())
	{
		return;
	}
	
	if (InAudioComponentId == AudioComponent->GetAudioComponentID())
	{
		if (CachedGeneratorPtr.IsValid())
		{
			OnGeneratorHandleDetached.Broadcast();
		}
		CachedGeneratorPtr = nullptr;
	}
}

void UMetasoundGeneratorHandle::CreateAnalyzerAndWatcher(
	const TSharedPtr<Metasound::FMetasoundGenerator> Generator,
	Metasound::Frontend::FAnalyzerAddress&& AnalyzerAddress)
{
	if (!IsValid())
	{
		return;
	}

	// Create the analyzer (will skip if there's already one)
	Generator->AddOutputVertexAnalyzer(AnalyzerAddress);
	
	// Create the watcher
	if (!OutputWatchers.ContainsByPredicate(
		[&AnalyzerAddress](const Metasound::Private::FMetasoundOutputWatcher& Watcher)
		{
			return AnalyzerAddress.OutputName == Watcher.Name;
		}))
	{
		OutputWatchers.Emplace(MoveTemp(AnalyzerAddress), Generator->OperatorSettings);
	}
}


