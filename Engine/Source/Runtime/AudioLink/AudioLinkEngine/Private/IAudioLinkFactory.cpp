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
	check(IsInGameThread()); 
	auto SourceBufferListenerSP = MakeShared<FBufferedSourceListener, ESPMode::ThreadSafe>(InSourceCreateParams.SizeOfBufferInFrames);
	InSourceCreateParams.AudioComponent->SetSourceBufferListener(SourceBufferListenerSP, InSourceCreateParams.bShouldZeroBuffer);
	return SourceBufferListenerSP;
}

FSharedBufferedOutputPtr IAudioLinkFactory::CreateSubmixBufferListener(const FSubmixBufferListenerCreateParams& InSubmixCreateParams)
{
	return MakeShared<FBufferedSubmixListener, ESPMode::ThreadSafe>(InSubmixCreateParams.SizeOfBufferInFrames, InSubmixCreateParams.bShouldZeroBuffer);	
}

FName IAudioLinkFactory::GetModularFeatureName()
{
	static FName FeatureName = FName(TEXT("AudioLink Factory"));
	return FeatureName;
}

TArray<IAudioLinkFactory*> IAudioLinkFactory::GetAllRegisteredFactories()
{
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
