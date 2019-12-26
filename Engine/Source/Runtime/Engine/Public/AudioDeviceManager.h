// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Audio.h"
#include "Audio/AudioDebug.h"
#include "AudioThread.h"
#include "Containers/Queue.h"
#include "CoreMinimal.h"


#if ENABLE_AUDIO_DEBUG
class FAudioDebugger;
#endif // ENABLE_AUDIO_DEBUG

class FReferenceCollector;
class FSoundBuffer;
class IAudioDeviceModule;
class UAudioComponent;
class USoundClass;
class USoundMix;
class USoundSubmix;
class USoundWave;

struct FSourceEffectChainEntry;

enum class ESoundType : uint8
{
	Class,
	Cue,
	Wave
};

/**
* Class for managing multiple audio devices.
*/
class ENGINE_API FAudioDeviceManager
{
public:

	/**
	* Constructor
	*/
	FAudioDeviceManager();

	/**
	* Destructor
	*/
	~FAudioDeviceManager();

	/**
	* Initialize the audio device manager.
	* Return true if successfully initialized.
	**/
	bool Initialize();

	/** Returns the handle to the main audio device. */
	Audio::FDeviceId GetMainAudioDeviceHandle() const { return MainAudioDeviceHandle; }

	/** Returns true if we're currently using the audio mixer. */
	bool IsUsingAudioMixer() const;

	struct FCreateAudioDeviceResults
	{
		uint32 Handle;
		uint8  bNewDevice : 1;
		FAudioDevice* AudioDevice;

		FCreateAudioDeviceResults();
	};

	/**
	 * returns the currently used audio device module for this platform.
	 * returns nullptr if Initialize() has not been called yet.
	 */
	IAudioDeviceModule* GetAudioDeviceModule();

	/**
	* Creates and audio device instance internally and returns a
	* handle to the audio device. Returns true on success.
	*/
	bool CreateAudioDevice(bool bCreateNewDevice, FCreateAudioDeviceResults& OutResults);

	/**
	* Returns whether the audio device handle is valid (i.e. points to
	* an actual audio device instance)
	*/
	bool IsValidAudioDeviceHandle(Audio::FDeviceId Handle) const;

	/**
	* Shutsdown the audio device associated with the handle. The handle
	* will become invalid after the audio device is shut down.
	*/
	bool ShutdownAudioDevice(Audio::FDeviceId Handle);

	/**
	* Shuts down all active audio devices
	*/
	bool ShutdownAllAudioDevices();

	/**
	* Returns a ptr to the audio device associated with the handle. If the
	* handle is invalid then a NULL device ptr will be returned.
	*/
	FAudioDevice* GetAudioDevice(Audio::FDeviceId Handle);

	/**
	* Returns a ptr to the active audio device. If there is no active
	* device then it will return the main audio device.
	*/
	FAudioDevice* GetActiveAudioDevice();

	/** Returns the current number of active audio devices. */
	uint8 GetNumActiveAudioDevices() const;

	/** Returns the number of worlds (e.g. PIE viewports) using the main audio device. */
	uint8 GetNumMainAudioDeviceWorlds() const;

	/** Updates all active audio devices */
	void UpdateActiveAudioDevices(bool bGameTicking);

	/** Tracks objects in the active audio devices. */
	void AddReferencedObjects(FReferenceCollector& Collector);

	/** Stops sounds using the given resource on all audio devices. */
	void StopSoundsUsingResource(class USoundWave* InSoundWave, TArray<UAudioComponent*>* StoppedComponents = nullptr);

	/** Registers the Sound Class for all active devices. */
	void RegisterSoundClass(USoundClass* SoundClass);

	/** Unregisters the Sound Class for all active devices. */
	void UnregisterSoundClass(USoundClass* SoundClass);

	/** Initializes the sound class for all active devices. */
	void InitSoundClasses();

	/** Registers the Sound Mix for all active devices. */
	void RegisterSoundSubmix(USoundSubmix* SoundSubmix);

	/** Registers the Sound Mix for all active devices. */
	void UnregisterSoundSubmix(USoundSubmix* SoundSubmix);

	/** Initializes the sound mixes for all active devices. */
	void InitSoundSubmixes();

	/** Initialize all sound effect presets. */
	void InitSoundEffectPresets();

	/** Updates source effect chain on all sources currently using the source effect chain. */
	void UpdateSourceEffectChain(const uint32 SourceEffectChainId, const TArray<FSourceEffectChainEntry>& SourceEffectChain, const bool bPlayEffectChainTails);

	/** Updates this submix for any changes made. Broadcasts to all submix instances. */
	void UpdateSubmix(USoundSubmix* SoundSubmix);

	/** Sets which audio device is the active audio device. */
	void SetActiveDevice(uint32 InAudioDeviceHandle);

	/** Sets an audio device to be solo'd */
	void SetSoloDevice(Audio::FDeviceId InAudioDeviceHandle);

	/** Links up the resource data indices for looking up and cleaning up. */
	void TrackResource(USoundWave* SoundWave, FSoundBuffer* Buffer);

	/** Frees the given sound wave resource from the device manager */
	void FreeResource(USoundWave* SoundWave);

	/** Frees the sound buffer from the device manager. */
	void FreeBufferResource(FSoundBuffer* SoundBuffer);

	/** Stops using the given sound buffer. Called before freeing the buffer */
	void StopSourcesUsingBuffer(FSoundBuffer* Buffer);

	/** Retrieves the sound buffer for the given resource id */
	FSoundBuffer* GetSoundBufferForResourceID(uint32 ResourceID);

	/** Removes the sound buffer for the given resource id */
	void RemoveSoundBufferForResourceID(uint32 ResourceID);

	/** Removes sound mix from all audio devices */
	void RemoveSoundMix(USoundMix* SoundMix);

	/** Toggles playing audio for all active PIE sessions (and all devices). */
	void TogglePlayAllDeviceAudio();

	/** Gets whether or not all devices should play their audio. */
	bool IsPlayAllDeviceAudio() const { return bPlayAllDeviceAudio; }

	/** Is debug visualization of 3d sounds enabled */
	bool IsVisualizeDebug3dEnabled() const;

	/** Toggles 3d visualization of 3d sounds on/off */
	void ToggleVisualize3dDebug();

	/** Reset all sound cue trims */
	void ResetAllDynamicSoundVolumes();

	/** Get, reset, or set a sound cue trim */
	float GetDynamicSoundVolume(ESoundType SoundType,  const FName& SoundName) const;
	void ResetDynamicSoundVolume(ESoundType SoundType, const FName& SoundName);
	void SetDynamicSoundVolume(ESoundType SoundType, const FName& SoundName, float Volume);

#if ENABLE_AUDIO_DEBUG
	/** Get the audio debugger instance */
	FAudioDebugger& GetDebugger();
#endif // ENABLE_AUDIO_DEBUG

public:

	/** Array of all created buffers */
	TArray<FSoundBuffer*>			Buffers;

	/** Look up associating a USoundWave's resource ID with sound buffers	*/
	TMap<int32, FSoundBuffer*>	WaveBufferMap;

	/** Returns all the audio devices managed by device manager. */
	TArray<FAudioDevice*>& GetAudioDevices() { return Devices; }
	
private:

#if ENABLE_AUDIO_DEBUG
	/** Instance of audio debugger shared across audio devices */
	FAudioDebugger AudioDebugger;
#endif // ENABLE_AUDIO_DEBUG

	/** Returns index of the given handle */
	uint32 GetIndex(uint32 Handle) const;

	/** Returns the generation of the given handle */
	uint32 GetGeneration(uint32 Handle) const;

	/** Creates a handle given the index and a generation value. */
	uint32 CreateHandle(uint32 DeviceIndex, uint8 Generation);

	/** Toggles between audio mixer and non-audio mixer audio engine. */
	void ToggleAudioMixer();

	/** Load audio device module. */
	bool LoadDefaultAudioDeviceModule();

	/** Create a "main" audio device. */
	bool CreateMainAudioDevice();

	/** Array for generation counts of audio devices in indices */
	TArray<uint8> Generations;

	/** Audio device module which creates (old backend) audio devices. */
	IAudioDeviceModule* AudioDeviceModule;

	/** Audio device module name. This is the "old" audio engine module name to use. E.g. XAudio2 */
	FString AudioDeviceModuleName;

	/** The audio mixer module name. This is the audio mixer module name to use. E.g. AudioMixerXAudio2 */
	FString AudioMixerModuleName;

	/** Handle to the main audio device. */
	Audio::FDeviceId MainAudioDeviceHandle;

	/** Count of the number of free slots in the audio device array. */
	uint32 FreeIndicesSize;

	/** Number of actively created audio device instances. */
	uint8 NumActiveAudioDevices;

	/** Number of worlds using the main audio device instance. */
	uint8 NumWorldsUsingMainAudioDevice;

	/** Queue for free indices */
	TQueue<uint32> FreeIndices;

	/**
	* Array of audio device pointers. If the device has been free, then
	* the device ptr at the array index will be null.
	*/
	TArray<FAudioDevice*> Devices;

	/** Next resource ID to assign out to a wave/buffer */
	int32 NextResourceID;

	/** Which audio device is solo'd */
	Audio::FDeviceId SoloDeviceHandle;

	/** Which audio device is currently active */
	Audio::FDeviceId ActiveAudioDeviceHandle;

	/** Dynamic volume map */
	TMap<TTuple<ESoundType, FName>, float> DynamicSoundVolumes;

	/** Whether we're currently using the audio mixer or not. Used to toggle between old/new audio engines. */
	bool bUsingAudioMixer;

	/** Whether or not to play all audio in all active audio devices. */
	bool bPlayAllDeviceAudio;

	/** Whether or not we check to toggle audio mixer once. */
	bool bOnlyToggleAudioMixerOnce;

	/** Whether or not we've toggled the audio mixer. */
	bool bToggledAudioMixer;

	/** Audio Fence to ensure that we don't allow the audio thread to drift never endingly behind. */
	FAudioCommandFence SyncFence;
};
