// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "NiagaraCommon.h"
#include "NiagaraShared.h"
#include "NiagaraDataInterface.h"
#include "Sound/SoundAttenuation.h"

#include "NiagaraDataInterfaceAudioPlayer.generated.h"

class USoundConcurrency;

struct FAudioParticleData
{
	FVector Position;
	FRotator Rotation;
	float Volume = 1;
	float Pitch = 1;
	float StartTime = 1;
};

struct FPersistentAudioParticleData
{
	int32 AudioHandle = 0;

	/** The update callback is executed in PerInstanceTickPostSimulate, which runs on the game thread */
	TFunction<void(struct FAudioPlayerInterface_InstanceData*,UAudioComponent*,FNiagaraSystemInstance*)> UpdateCallback;
};

struct FAudioPlayerInterface_InstanceData
{
	/** We use a lock-free queue here because multiple threads might try to push data to it at the same time. */
	TQueue<FAudioParticleData, EQueueMode::Mpsc> PlayAudioQueue;
	TQueue<FPersistentAudioParticleData, EQueueMode::Mpsc> PersistentAudioActionQueue;
	FThreadSafeCounter HandleCount;

	TSortedMap<int32, TWeakObjectPtr<UAudioComponent>> PersistentAudioMapping;

	TWeakObjectPtr<USoundBase> SoundToPlay;
	TWeakObjectPtr<USoundAttenuation> Attenuation;
	TWeakObjectPtr<USoundConcurrency> Concurrency;
	TArray<FName> ParameterNames;

	int32 MaxPlaysPerTick = 0;
	bool bStopWhenComponentIsDestroyed = true;

	// we track if at least one particle played a sound to prevent problems where sounds keep on playing when scalability culls an emitter (which the DI does not notice otherwise)
	bool bHadPersistentAudioUpdateThisTick = false;
};

/** This Data Interface can be used to play one-shot audio effects driven by particle data. */
UCLASS(EditInlineNew, Category = "Audio", meta = (DisplayName = "Audio Player"))
class NIAGARA_API UNiagaraDataInterfaceAudioPlayer : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:
	/** Reference to the audio asset to play */
	UPROPERTY(EditAnywhere, Category = "Audio")
    USoundBase* SoundToPlay;

	/** Optional sound attenuation setting to use */
	UPROPERTY(EditAnywhere, Category = "Audio")
	USoundAttenuation* Attenuation;

	/** Optional sound concurrency setting to use */
	UPROPERTY(EditAnywhere, Category = "Audio")
	USoundConcurrency* Concurrency;
	
	/** A set of parameter names that can be referenced via index when setting sound cue parameters on persistent audio */
	UPROPERTY(EditAnywhere, Category = "Parameters")
	TArray<FName> ParameterNames;

	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Audio", meta = (InlineEditConditionToggle))
    bool bLimitPlaysPerTick;

	/** This sets the max number of sounds played each tick.
	 *  If more particles try to play a sound in a given tick, then it will play sounds until the limit is reached and discard the rest.
	 *  The particles to discard when over the limit are *not* chosen in a deterministic way. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Audio", meta=(EditCondition="bLimitPlaysPerTick", ClampMin="0", UIMin="0"))
    int32 MaxPlaysPerTick;

	/** If false then it the audio component keeps playing after the niagara component was destroyed. Looping sounds are always stopped when the component is destroyed. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Audio")
	bool bStopWhenComponentIsDestroyed = true;

	//UObject Interface
	virtual void PostInitProperties() override;
	//UObject Interface End

	//UNiagaraDataInterface Interface
	virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc) override;
	virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	virtual int32 PerInstanceDataSize() const override { return sizeof(FAudioPlayerInterface_InstanceData); }
	virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual bool PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	virtual bool Equals(const UNiagaraDataInterface* Other) const override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return Target == ENiagaraSimTarget::CPUSim; }

	virtual bool HasPreSimulateTick() const override { return true; }
	virtual bool HasPostSimulateTick() const override { return true; }
	//UNiagaraDataInterface Interface

	virtual void PlayOneShotAudio(FVectorVMContext& Context);
	virtual void PlayPersistentAudio(FVectorVMContext& Context);
	virtual void SetParameterBool(FVectorVMContext& Context);
	virtual void SetParameterInteger(FVectorVMContext& Context);
	virtual void SetParameterFloat(FVectorVMContext& Context);
	virtual void UpdateVolume(FVectorVMContext& Context);
	virtual void UpdatePitch(FVectorVMContext& Context);
	virtual void UpdateLocation(FVectorVMContext& Context);
	virtual void UpdateRotation(FVectorVMContext& Context);
	virtual void SetPausedState(FVectorVMContext& Context);

protected:
	virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;
	
private:
	static const FName PlayAudioName;
	static const FName PlayPersistentAudioName;
	static const FName SetPersistentAudioVolumeName;
	static const FName SetPersistentAudioPitchName;
	static const FName SetPersistentAudioLocationName;
	static const FName SetPersistentAudioRotationName;
	static const FName SetPersistentAudioBoolParamName;
	static const FName SetPersistentAudioIntegerParamName;
	static const FName SetPersistentAudioFloatParamName;
	static const FName PausePersistentAudioName;
};
