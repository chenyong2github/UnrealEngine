// Copyright Epic Games, Inc. All Rights Reserved.

#include "IAudioLinkFactory.h"
#include "Algo/Transform.h"

// Concrete Buffer Listeners.
#include "BufferedSubmixListener.h" 
#include "BufferedSourceListener.h" 

IAudioLinkFactory::IAudioLinkFactory()
{
	IModularFeatures::Get().RegisterModularFeature(GetModularFeatureName(), this);
}

IAudioLinkFactory::~IAudioLinkFactory()
{
	IModularFeatures::Get().UnregisterModularFeature(GetModularFeatureName(), this);
}

FSharedBufferedOutputPtr IAudioLinkFactory::CreateSourceBufferListener(const FSourceBufferListenerCreateParams& InSourceCreateParams)
{	
	auto SourceBufferListenerSP = MakeShared<FBufferedSourceListener, ESPMode::ThreadSafe>(InSourceCreateParams.SizeOfBufferInFrames);
	if (!InSourceCreateParams.AudioComponent.IsExplicitlyNull())
	{
		check(IsInGameThread());
		InSourceCreateParams.AudioComponent->SetSourceBufferListener(SourceBufferListenerSP, InSourceCreateParams.bShouldZeroBuffer);
	}
	return SourceBufferListenerSP;
}

FSharedBufferedOutputPtr IAudioLinkFactory::CreatePushableBufferListener(const FPushedBufferListenerCreateParams& InPushableCreateParams)
{
	// Add push functionality to the source buffer listener with simple wrapper
	struct FPushableSourceBufferListener : FBufferedSourceListener, IPushableAudioOutput
	{
		using FBufferedSourceListener::FBufferedSourceListener;

		IPushableAudioOutput* GetPushableInterface() override { return this; }
		const IPushableAudioOutput* GetPushableInterface() const { return this; }

		void PushNewBuffer(const IPushableAudioOutput::FOnNewBufferParams& InNewBuffer) override
		{
			ISourceBufferListener::FOnNewBufferParams Params;
			Params.AudioData = InNewBuffer.AudioData;
			Params.NumChannels = InNewBuffer.NumChannels;
			Params.NumSamples = InNewBuffer.NumSamples;
			Params.SourceId = InNewBuffer.Id;
			Params.SampleRate = InNewBuffer.SampleRate;
			static_cast<ISourceBufferListener*>(this)->OnNewBuffer(Params);
		}

		void LastBuffer(int32 InId) override
		{
			static_cast<ISourceBufferListener*>(this)->OnSourceReleased(InId);
		}
	};

	auto SourceBufferListenerSP = MakeShared<FPushableSourceBufferListener, ESPMode::ThreadSafe>(InPushableCreateParams.SizeOfBufferInFrames);
	
	return SourceBufferListenerSP;
}

FSharedBufferedOutputPtr IAudioLinkFactory::CreateSubmixBufferListener(const FSubmixBufferListenerCreateParams& InSubmixCreateParams)
{
	return MakeShared<FBufferedSubmixListener, ESPMode::ThreadSafe>(InSubmixCreateParams.SizeOfBufferInFrames, InSubmixCreateParams.bShouldZeroBuffer);	
}

TArray<IAudioLinkFactory*> IAudioLinkFactory::GetAllRegisteredFactories()
{
	IModularFeatures::FScopedLockModularFeatureList ScopedLockModularFeatureList;
	return IModularFeatures::Get().GetModularFeatureImplementations<IAudioLinkFactory>(GetModularFeatureName());
}

TArray<FName> IAudioLinkFactory::GetAllRegisteredFactoryNames()
{
	TArray<FName> Names;
	Algo::Transform(GetAllRegisteredFactories(), Names, [](IAudioLinkFactory* Factory) { return Factory->GetFactoryName(); });
	return Names;
}

IAudioLinkFactory* IAudioLinkFactory::FindFactory(const FName InFactoryImplName)
{
	TArray<IAudioLinkFactory*> Factories = GetAllRegisteredFactories();
	if (IAudioLinkFactory** Found = Factories.FindByPredicate([InFactoryImplName](IAudioLinkFactory* Factory) { return Factory->GetFactoryName() == InFactoryImplName; }))
	{
		return *Found;
	}
	return nullptr;
}
