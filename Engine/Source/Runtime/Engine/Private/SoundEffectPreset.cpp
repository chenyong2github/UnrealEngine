// Copyright Epic Games, Inc. All Rights Reserved.


#include "Sound/SoundEffectPreset.h"
#include "Sound/SoundEffectSource.h"
#include "Engine/Engine.h"
#include "AudioDeviceManager.h"
#include "CoreGlobals.h"

USoundEffectPreset::USoundEffectPreset(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, bInitialized(false)
{

}

void USoundEffectPreset::Update()
{
	for (int32 i = Instances.Num() - 1; i >= 0; --i)
	{
		TSoundEffectPtr EffectSharedPtr = Instances[i].Pin();
		if (!EffectSharedPtr.IsValid() || EffectSharedPtr->GetPreset() == nullptr)
		{
			Instances.RemoveAtSwap(i, 1);
		}
		else
		{
			RegisterInstance(*this, EffectSharedPtr);
		}
	}
}

void USoundEffectPreset::AddEffectInstance(TSoundEffectPtr& InEffectPtr)
{
	if (!bInitialized)
	{
		bInitialized = true;
		Init();

		// Call the optional virtual function which subclasses can implement if they need initialization
		OnInit();
	}

	Instances.AddUnique(TSoundEffectWeakPtr(InEffectPtr));
}

void USoundEffectPreset::AddReferencedEffects(FReferenceCollector& InCollector)
{
	FReferenceCollector* Collector = &InCollector;
	IterateEffects<FSoundEffectBase>([Collector](FSoundEffectBase& Instance)
	{
		if (const USoundEffectPreset* EffectPreset = Instance.GetPreset())
		{
			Collector->AddReferencedObject(EffectPreset);
		}
	});
}

void USoundEffectPreset::BeginDestroy()
{
	IterateEffects<FSoundEffectBase>([](FSoundEffectBase& Instance)
	{
		Instance.ClearPreset();
	});
	Instances.Reset();

	Super::BeginDestroy();
}

void USoundEffectPreset::RemoveEffectInstance(TSoundEffectPtr& InEffectPtr)
{
	Instances.RemoveSwap(TSoundEffectWeakPtr(InEffectPtr));
}

#if WITH_EDITORONLY_DATA
void USoundEffectPreset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Copy the settings to the thread safe version
	Init();
	OnInit();
	Update();
}

void USoundEffectSourcePresetChain::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (GEngine)
	{
		FAudioDeviceManager* AudioDeviceManager = GEngine->GetAudioDeviceManager();
		AudioDeviceManager->UpdateSourceEffectChain(GetUniqueID(), Chain, bPlayEffectChainTails);
	}
}
#endif // WITH_EDITORONLY_DATA

void USoundEffectSourcePresetChain::AddReferencedEffects(FReferenceCollector& Collector)
{
	for (FSourceEffectChainEntry& SourceEffect : Chain)
	{
		if (SourceEffect.Preset)
		{
			SourceEffect.Preset->AddReferencedEffects(Collector);
		}
	}
}