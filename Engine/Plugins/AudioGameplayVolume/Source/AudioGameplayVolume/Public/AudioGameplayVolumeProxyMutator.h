// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Sound/AudioVolume.h"
#include "AudioDeviceManager.h"
#include "AudioGameplayFlags.h"
#include "Templates/SharedPointer.h"

// Forward Declarations 
class FProxyVolumeMutator;
class UAudioGameplayVolumeProxy;
class FAudioGameplayVolumeListener;
struct FAudioGameplayActiveSoundInfo;

/**
 *  FAudioProxyMutatorSearchResult - Results from a audio proxy mutator search (see below).
 */
struct FAudioProxyMutatorSearchResult
{
	TSet<uint32> VolumeSet;
	TArray<TSharedPtr<FProxyVolumeMutator>> MatchingMutators;
	FReverbSettings ReverbSettings;
	FInteriorSettings InteriorSettings;

	void Reset()
	{
		VolumeSet.Reset();
		MatchingMutators.Empty();
		ReverbSettings = FReverbSettings();
		InteriorSettings = FInteriorSettings();
	}
};

/**
 *  FAudioProxyMutatorSearchObject - Used for searching through proxy volumes to find relevant proxy mutators
 */
struct FAudioProxyMutatorSearchObject
{
	using PayloadFlags = AudioGameplay::EComponentPayload;

	// Search parameters
	uint32 WorldID = INDEX_NONE;
	FVector Location = FVector::ZeroVector;
	PayloadFlags PayloadType = PayloadFlags::AGCP_None;
	FAudioDeviceHandle AudioDeviceHandle;
	bool bAffectedByLegacySystem = false;
	bool bFilterPayload = true;
	bool bCollectMutators = true;
	bool bGetDefaultAudioSettings = true;

	void SearchVolumes(const TArray<TWeakObjectPtr<UAudioGameplayVolumeProxy>>& ProxyVolumes, FAudioProxyMutatorSearchResult& OutResult);
};

/**
 *  FAudioProxyMutatorPriorities - Used for finding the highest priority mutators on a proxy
 */
struct FAudioProxyMutatorPriorities
{
	using PayloadFlags = AudioGameplay::EComponentPayload;

	TMap<FName, int32> PriorityMap;
	PayloadFlags PayloadType = PayloadFlags::AGCP_None;
	bool bFilterPayload = true;
};

/**
 *  FAudioProxyActiveSoundParams - Helper struct for collecting info about the active sound from affecting proxy mutators
 */
struct FAudioProxyActiveSoundParams
{
	FAudioProxyActiveSoundParams() = delete;
	FAudioProxyActiveSoundParams(const FAudioGameplayActiveSoundInfo& SoundInfo, const FAudioGameplayVolumeListener& InListener);

	float SourceInteriorVolume = 1.f;

	float SourceInteriorLPF = MAX_FILTER_FREQUENCY;

	bool bAllowSpatialization = false;
	bool bUsingWorldSettings = false;
	bool bListenerInVolume = false;

	bool bAffectedByAttenuation = false;
	bool bAffectedByFilter = false;

	const FAudioGameplayVolumeListener& Listener;
	const FAudioGameplayActiveSoundInfo& Sound;

	TArray<FSoundSubmixSendInfo> SoundSubmixSends;

	void UpdateInteriorValues();
};

/**
 *  FProxyVolumeMutator - An audio thread representation of the payload for an AudioGameplayVolumeComponent.
 */
class AUDIOGAMEPLAYVOLUME_API FProxyVolumeMutator : public TSharedFromThis<FProxyVolumeMutator>
{
public:

	FProxyVolumeMutator();
	virtual ~FProxyVolumeMutator() = default;

	using PayloadFlags = AudioGameplay::EComponentPayload;

	virtual void UpdatePriority(FAudioProxyMutatorPriorities& Priorities) const;
	virtual bool CheckPriority(const FAudioProxyMutatorPriorities& Priorities) const;

	virtual void Apply(FInteriorSettings& InteriorSettings) const;
	virtual void Apply(FAudioProxyActiveSoundParams& Params) const {}

	bool HasPayloadType(PayloadFlags InType) const;

	int32 Priority = INDEX_NONE;
	uint32 VolumeID = INDEX_NONE;
	uint32 WorldID = INDEX_NONE;

	FName MutatorName;

	PayloadFlags PayloadType;

protected:

	constexpr static const TCHAR MutatorBaseName[] = TEXT("MutatorBase");
};
