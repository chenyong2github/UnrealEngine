// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundGeneratorHandle.h"

#include "Components/AudioComponent.h"
#include "AudioDeviceManager.h"
#include "MetasoundGenerator.h"
#include "MetasoundSource.h"
#include "MetasoundParameterPack.h"

UMetasoundGeneratorHandle* UMetasoundGeneratorHandle::CreateMetaSoundGeneratorHandle(UAudioComponent* OnComponent)
{
	if (!OnComponent)
	{
		return nullptr;
	}

	UMetasoundGeneratorHandle* Result = NewObject<UMetasoundGeneratorHandle>();
	Result->SetAudioComponent(OnComponent);
	return Result;
}

void UMetasoundGeneratorHandle::ClearCachedData()
{
	DetachGeneratorDelegates();
	AudioComponent        = nullptr;
	AudioComponentId      = 0;
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
		AudioComponentId = InAudioComponent->GetAudioComponentID();
	}
}

void UMetasoundGeneratorHandle::CacheMetasoundSource()
{
	if (!AudioComponent)
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

	if (CachedMetasoundSource)
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
	GeneratorDestroyedDelegateHandle = CachedMetasoundSource->OnGeneratorInstanceDestroyed.AddWeakLambda(this,
		[this](uint64 InAudioComponentId, TSharedPtr<Metasound::FMetasoundGenerator> InGenerator)
		{
				// We are in the audio render thread here, so create a "dispatch task" to be
				// executed later on the game thread...
				FFunctionGraphTask::CreateAndDispatchWhenReady(
					[WeakGeneratorHandlePtr]()
					{
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
	if (CachedMetasoundSource)
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
	if (PinnedGenerator.IsValid() || !CachedMetasoundSource)
	{
		return PinnedGenerator;
	}

	// The first attempt to pin failed, so reach out to the MetaSoundSource and see if it has a 
	// generator for our AudioComponent...
	CachedGeneratorPtr = CachedMetasoundSource->GetGeneratorForAudioComponent(AudioComponentId);
	PinnedGenerator    = CachedGeneratorPtr.Pin();
	return PinnedGenerator;
}

bool UMetasoundGeneratorHandle::ApplyParameterPack(UMetasoundParameterPack* Pack)
{
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

void UMetasoundGeneratorHandle::BeginDestroy()
{
	Super::BeginDestroy();
	DetachGeneratorDelegates();
}

void UMetasoundGeneratorHandle::OnSourceCreatedAGenerator(uint64 InAudioComponentId, TSharedPtr<Metasound::FMetasoundGenerator> InGenerator)
{
	check(IsInGameThread());
	if (InAudioComponentId == AudioComponentId)
	{
		CachedGeneratorPtr = InGenerator;
		if (InGenerator)
		{
			if (CachedParameterPack)
			{
				InGenerator->QueueParameterPack(CachedParameterPack);
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
	if (InAudioComponentId == AudioComponentId)
	{
		if (CachedGeneratorPtr.IsValid())
		{
			OnGeneratorHandleDetached.Broadcast();
		}
		CachedGeneratorPtr = nullptr;
	}
}


