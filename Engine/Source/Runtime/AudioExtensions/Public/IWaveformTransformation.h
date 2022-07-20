// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "HAL/Platform.h"
#include "SignalProcessing/Public/DSP/BufferVectorOperations.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectPtr.h"
#include "UObject/UObjectGlobals.h"

#include "IWaveformTransformation.generated.h"

namespace Audio
{
	// information about the current state of the wave file we are transforming
	struct AUDIOEXTENSIONS_API FWaveformTransformationWaveInfo
	{
		float SampleRate = 0.f;
		int32 NumChannels = 0;
		Audio::FAlignedFloatBuffer* Audio = nullptr;
	};

	/*
	 * Base class for the object that processes waveform data
	 * Pass tweakable variables from its paired settings UObject in the constructor in UWaveformTransformationBase::CreateTransformation
	 *
	 * note: WaveTransformation vs WaveformTransformation is to prevent UHT class name conflicts without having to namespace everything - remember this in derived classes!
	 */
	class AUDIOEXTENSIONS_API IWaveTransformation
	{
	public:

		// Applies the transformation to the waveform and modifies WaveInfo with the resulting changes
		virtual void ProcessAudio(FWaveformTransformationWaveInfo& InOutWaveInfo) const {};
		
		virtual bool SupportsRealtimePreview() const { return false; }
		virtual bool CanChangeFileLength() const { return false; }
		virtual bool CanChangeChannelCount() const { return false; }

		virtual ~IWaveTransformation() {};
	};

	using FTransformationPtr = TUniquePtr<Audio::IWaveTransformation>;
}

// Base class to hold editor configurable properties for an arbitrary transformation of audio waveform data
UCLASS(Abstract, EditInlineNew)
class AUDIOEXTENSIONS_API UWaveformTransformationBase : public UObject
{
	GENERATED_BODY()

public:
	virtual Audio::FTransformationPtr CreateTransformation() const { return nullptr; }
};

// Object that holds an ordered list of transformations to perform on a sound wave
UCLASS(EditInlineNew)
class AUDIOEXTENSIONS_API UWaveformTransformationChain : public UObject
{
	GENERATED_BODY()
	
public:
	UPROPERTY(EditAnywhere, Instanced, Category = "Transformations")
	TArray<TObjectPtr<UWaveformTransformationBase>> Transformations;

	TArray<Audio::FTransformationPtr> CreateTransformations() const;
};