// Copyright Epic Games, Inc. All Rights Reserved.

#include "OculusAudioContextManager.h"
#include "OculusAmbisonicSpatializer.h"
#include "OculusAudioReverb.h"
#include "Features/IModularFeatures.h"
#include "AudioMixerDevice.h"
#include "IOculusAudioPlugin.h"

ovrAudioContext FOculusAudioContextManager::SerializationContext = nullptr;
UActorComponent* FOculusAudioContextManager::SerializationParent = nullptr;

FOculusAudioContextManager::FOculusAudioContextManager()
	: Context(nullptr)
{
	// empty
}

FOculusAudioContextManager::~FOculusAudioContextManager()
{
	OVRA_CALL(ovrAudio_DestroyContext)(Context);
	Context = nullptr;
	SerializationContext = nullptr;
}

void FOculusAudioContextManager::OnListenerInitialize(FAudioDevice* AudioDevice, UWorld* ListenerWorld)
{
	// Inject the Context into the spatailizer (and reverb) if they're enabled
	FOculusAudioPlugin* Plugin = &FModuleManager::GetModuleChecked<FOculusAudioPlugin>("OculusAudio");
	if (Plugin == nullptr)
	{
		return; // PAS is this needed?
	}

	if (SerializationContext != nullptr)
	{
#if 0
		/* NOTE: this was meant to be some magic that would clean up the serialization
		context so there is no stale state in PIE. This failed when there was no geometry
		in the scene. Since the plugins are force to use one global context that seems
		like a better option for the vanilla UE4 integraiton to do as well for consistency.
		*/
		// We must have created some geometry
		check(SerializationParent != nullptr);
		AActor* Actor = SerializationParent->GetOwner();
		check(Actor != nullptr);
		UWorld* SerializedWorld = Actor->GetWorld();
		check(SerializedWorld == ListenerWorld);
		Context = SerializationContext;
		SerializationContext = nullptr;
		SerializationParent = nullptr;
#else
		Context = SerializationContext;
		SerializationContext = nullptr;
#endif
	}

	FString OculusSpatializerPluginName = Plugin->GetSpatializationPluginFactory()->GetDisplayName();
	FString CurrentSpatializerPluginName = AudioPluginUtilities::GetDesiredPluginName(EAudioPlugin::SPATIALIZATION);
	if (CurrentSpatializerPluginName.Equals(OculusSpatializerPluginName)) // we have a match!
	{
		OculusAudioSpatializationAudioMixer* Spatializer = 
			static_cast<OculusAudioSpatializationAudioMixer*>(AudioDevice->SpatializationPluginInterface.Get());
		Spatializer->SetContext(&Context);
	}

	FString OculusReverbPluginName = Plugin->GetReverbPluginFactory()->GetDisplayName();
	FString CurrentReverbPluginName = AudioPluginUtilities::GetDesiredPluginName(EAudioPlugin::REVERB);
	if (CurrentReverbPluginName.Equals(OculusReverbPluginName))
	{
		OculusAudioReverb* Reverb = static_cast<OculusAudioReverb*>(AudioDevice->ReverbPluginInterface.Get());
		Reverb->SetContext(&Context);
	}
}

void FOculusAudioContextManager::OnListenerShutdown(FAudioDevice* AudioDevice)
{
	FOculusAudioPlugin* Plugin = &FModuleManager::GetModuleChecked<FOculusAudioPlugin>("OculusAudio");
	check(Plugin != nullptr);

	FString OculusSpatializerPluginName = Plugin->GetSpatializationPluginFactory()->GetDisplayName();
	FString CurrentSpatializerPluginName = AudioPluginUtilities::GetDesiredPluginName(EAudioPlugin::SPATIALIZATION);
	if (CurrentSpatializerPluginName.Equals(OculusSpatializerPluginName))
	{
		OculusAudioSpatializationAudioMixer* Spatializer =
			static_cast<OculusAudioSpatializationAudioMixer*>(AudioDevice->SpatializationPluginInterface.Get());
		Spatializer->SetContext(nullptr);
	}

	FString OculusReverbPluginName = Plugin->GetReverbPluginFactory()->GetDisplayName();
	FString CurrentReverbPluginName = AudioPluginUtilities::GetDesiredPluginName(EAudioPlugin::REVERB);
	if (CurrentReverbPluginName.Equals(OculusReverbPluginName))
	{
		OculusAudioReverb* Reverb = static_cast<OculusAudioReverb*>(AudioDevice->ReverbPluginInterface.Get());
		Reverb->SetContext(nullptr);
	}
}

ovrAudioContext FOculusAudioContextManager::GetOrCreateSerializationContext(UActorComponent* Parent)
{
#if 0 //WITH_EDITOR
	/* NOTE: this was meant to be some magic that would clean up the serialization 
	context so there is no stale state in PIE. This failed when there was no geometry 
	in the scene. Since the plugins are force to use one global context that seems 
	like a better option for the vanilla UE4 integraiton to do as well for consistency.
	*/
	if (SerializationParent != nullptr)
	{
		AActor* Actor = SerializationParent->GetOwner();
		if (Actor != nullptr)
		{
			UWorld* World = Actor->GetWorld();
			if (World != nullptr)
			{
				if (World->WorldType == EWorldType::Editor)
				{
					// It was just an editor serialize, we don't need these geometry objects
					OVRA_CALL(ovrAudio_DestroyContext)(SerializationContext);
					SerializationContext = nullptr;
					SerializationParent = nullptr;
				}
			}
		}
	}
#endif

	ovrAudioContext PluginContext = FOculusAudioLibraryManager::Get().GetPluginContext();
	if (PluginContext != nullptr)
	{
		return PluginContext;
	}

	if (SerializationContext == nullptr)
	{
		ovrResult Result = OVRA_CALL(ovrAudio_CreateContext)(&SerializationContext, nullptr);
		if (Result != ovrSuccess)
		{
			const TCHAR* ErrString = GetOculusErrorString(Result);
			UE_LOG(LogAudio, Error, TEXT("Oculus Audio SDK Error - %s: %s"), TEXT("Failed to create Oculus Audio context for serialization"), ErrString);
			return nullptr;
		}

		SerializationParent = Parent;
	}

	return SerializationContext;
}
