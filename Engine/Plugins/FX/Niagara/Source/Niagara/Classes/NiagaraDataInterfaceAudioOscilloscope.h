// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraCommon.h"
#include "NiagaraShared.h"
#include "VectorVM.h"
#include "StaticMeshResources.h"
#include "NiagaraDataInterface.h"
#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "DSP/MultithreadedPatching.h"
#include "NiagaraDataInterfaceAudioOscilloscope.generated.h"

// Class used to to capture the audio stream of an arbitrary submix.
class NIAGARA_API FNiagaraSubmixListener : public ISubmixBufferListener
{
public:
	FNiagaraSubmixListener(Audio::FPatchMixer& InMixer, int32 InNumSamplesToBuffer);
	FNiagaraSubmixListener(const FNiagaraSubmixListener& Other)
	{
		// Copy constructor technically required to compile TMap, but not used during runtime if move constructor is available.
		// If you're hitting this, consider using Emplace or Add(MoveTemp()).
		checkNoEntry();
	}

	FNiagaraSubmixListener(FNiagaraSubmixListener&& Other);

	virtual ~FNiagaraSubmixListener();

	float GetSampleRate();
	int32 GetNumChannels();

	// Begin ISubmixBufferListener overrides
	virtual void OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock) override;
	// End ISubmixBufferListener overrides

private:
	FNiagaraSubmixListener();

	int32 NumChannelsInSubmix;
	int32 SubmixSampleRate;

	Audio::FPatchInput MixerInput;
};

struct FNiagaraDataInterfaceProxyOscilloscope : public FNiagaraDataInterfaceProxy
{
	FNiagaraDataInterfaceProxyOscilloscope(int32 InResolution, float InScopeInMillseconds);

	~FNiagaraDataInterfaceProxyOscilloscope();

	void OnBeginDestroy();

	/**
	 * Sample vertical displacement from the oscilloscope buffer.
	 * @param NormalizedPositionInBuffer Horizontal position in the Oscilloscope buffer, from 0.0 to 1.0.
	 * @param Channel channel index.
	 * @return Amplitude at this position.
	 */
	float SampleAudio(float NormalizedPositionInBuffer, int32 Channel);

	/**
	 * @return the number of channels in the buffer.
	 */
	int32 GetNumChannels();

	// Called when the Submix property changes.
	void OnUpdateSubmix(USoundSubmix* Submix);

	void RegisterToAllAudioDevices();
	void UnregisterFromAllAudioDevices(USoundSubmix* Submix);

	// Called when Resolution or Zoom are changed.
	void OnUpdateResampling(int32 InResolution, float InScopeInMilliseconds);

	// This function enqueues a render thread command to decimate the pop audio off of the SubmixListener, downsample it, and post it to the GPUAudioBuffer.
	void PostAudioToGPU();
	FReadBuffer& ComputeAndPostSRV();

	// This function pops audio and downsamples it to our specified resolution.
	void DownsampleAudioToBuffer();

	virtual int32 PerInstanceDataPassedToRenderThreadSize() const override
	{
		return 0;
	}

private:
	void OnNewDeviceCreated(Audio::FDeviceId InID);
	void OnDeviceDestroyed(Audio::FDeviceId InID);

	TMap<Audio::FDeviceId, FNiagaraSubmixListener> SubmixListeners;

	// This 
	Audio::FPatchMixer PatchMixer;

	USoundSubmix* SubmixRegisteredTo;
	bool bIsSubmixListenerRegistered;

	int32 Resolution;
	float ScopeInMilliseconds;

	// The buffer we downsample PoppedBuffer to based on the Resolution property.
	Audio::AlignedFloatBuffer PopBuffer;
	Audio::AlignedFloatBuffer DownsampledBuffer;

	// Handle for the SRV used by the generated HLSL.
	FReadBuffer GPUDownsampledBuffer;
	int32 NumChannelsInDownsampledBuffer;
	
	FDelegateHandle DeviceCreatedHandle;
	FDelegateHandle DeviceDestroyedHandle;

	FCriticalSection DownsampleBufferLock;
};

/** Data Interface allowing sampling of recent audio data. */
UCLASS(EditInlineNew, Category = "Audio", meta = (DisplayName = "Audio Oscilloscope"))
class NIAGARA_API UNiagaraDataInterfaceAudioOscilloscope : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()
public:

	DECLARE_NIAGARA_DI_PARAMETER();
	
	UPROPERTY(EditAnywhere, Category = "Oscilloscope")
	USoundSubmix* Submix;

	// The number of samples of audio to pass to the GPU. Audio will be resampled to fit this resolution.
	UPROPERTY(EditAnywhere, Category = "Oscilloscope")
	int32 Resolution;

	// The number of milliseconds of audio to show.
	UPROPERTY(EditAnywhere, Category = "Oscilloscope")
	float ScopeInMilliseconds;

	//VM function overrides:
	void SampleAudio(FVectorVMContext& Context);
	void GetNumChannels(FVectorVMContext& Context);

	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;

	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { 
		if (Target == ENiagaraSimTarget::GPUComputeSim)
		{
			return true;
		}
		else
		{
			return false;
		}
	}
	virtual bool RequiresDistanceFieldData() const override { return false; }

	virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
	virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR

	virtual void PostInitProperties() override;
	virtual void BeginDestroy() override;
	virtual void PostLoad() override;

protected:
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
};

