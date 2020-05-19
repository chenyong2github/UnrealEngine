// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraDataInterfaceAudioPlayer.h"

#include "NiagaraTypes.h"
#include "NiagaraCustomVersion.h"
#include "Internationalization/Internationalization.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraWorldManager.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

const FName UNiagaraDataInterfaceAudioPlayer::PlayAudioName(TEXT("PlayAudioAtLocation"));

/**
Async task to play the audio on the game thread and isolate from the niagara tick
*/
class FNiagaraAudioPlayerAsyncTask
{
	TWeakObjectPtr<USoundBase> WeakSound;
	TWeakObjectPtr<USoundAttenuation> WeakAttenuation;
	TWeakObjectPtr<USoundConcurrency> WeakConcurrency;
	TArray<FAudioParticleData> Data;
	TWeakObjectPtr<UWorld> WeakWorld;

public:
	FNiagaraAudioPlayerAsyncTask(TWeakObjectPtr<USoundBase> InSound, TWeakObjectPtr<USoundAttenuation> InAttenuation, TWeakObjectPtr<USoundConcurrency> InConcurrency, TArray<FAudioParticleData>& Data, TWeakObjectPtr<UWorld> InWorld)
		: WeakSound(InSound)
	    , WeakAttenuation(InAttenuation)
		, WeakConcurrency(InConcurrency)
		, Data(Data)
		, WeakWorld(InWorld)
	{
	}

	FORCEINLINE TStatId GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(FNiagaraAudioPlayerAsyncTask, STATGROUP_TaskGraphTasks); }
	static FORCEINLINE ENamedThreads::Type GetDesiredThread() { return ENamedThreads::GameThread; }
	static FORCEINLINE ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::FireAndForget; }

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		UWorld* World = WeakWorld.Get();
		if (World == nullptr)
		{
			UE_LOG(LogNiagara, Warning, TEXT("Invalid world reference in audio player DI, skipping play"));
			return;
		}

		USoundBase* Sound = WeakSound.Get();
		if (Sound == nullptr)
		{
			UE_LOG(LogNiagara, Warning, TEXT("Invalid sound reference in audio player DI, skipping play"));
			return;
		}

		for (const FAudioParticleData& ParticleData : Data)
		{
			UGameplayStatics::PlaySoundAtLocation(World, Sound, ParticleData.Position, ParticleData.Rotation, ParticleData.Volume,
				ParticleData.Pitch, ParticleData.StartTime, WeakAttenuation.Get(), WeakConcurrency.Get());
		}
	}
};

UNiagaraDataInterfaceAudioPlayer::UNiagaraDataInterfaceAudioPlayer(FObjectInitializer const& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SoundToPlay = nullptr;
	Attenuation = nullptr;
	Concurrency = nullptr;
	bLimitPlaysPerTick = true;
	MaxPlaysPerTick = 10;
}

void UNiagaraDataInterfaceAudioPlayer::PostInitProperties()
{
	Super::PostInitProperties();

	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		FNiagaraTypeRegistry::Register(FNiagaraTypeDefinition(GetClass()), true, false, false);
	}
}

bool UNiagaraDataInterfaceAudioPlayer::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FAudioPlayerInterface_InstanceData* PIData = new (PerInstanceData) FAudioPlayerInterface_InstanceData;
	if (bLimitPlaysPerTick)
	{
		PIData->MaxPlaysPerTick = MaxPlaysPerTick;
	}
	return true;
}

void UNiagaraDataInterfaceAudioPlayer::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FAudioPlayerInterface_InstanceData* InstData = (FAudioPlayerInterface_InstanceData*)PerInstanceData;
	InstData->~FAudioPlayerInterface_InstanceData();
}

bool UNiagaraDataInterfaceAudioPlayer::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	FAudioPlayerInterface_InstanceData* PIData = (FAudioPlayerInterface_InstanceData*)PerInstanceData;
	if (!PIData)
	{
		return true;
	}
	
	if (IsValid(SoundToPlay) && SystemInstance)
	{
		PIData->SoundToPlay = SoundToPlay;
		PIData->Attenuation = Attenuation;
		PIData->Concurrency = Concurrency;
	}
	else
	{
		PIData->SoundToPlay.Reset();
		PIData->Attenuation.Reset();
		PIData->Concurrency.Reset();
	}
	return false;
}

bool UNiagaraDataInterfaceAudioPlayer::PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
	FAudioPlayerInterface_InstanceData* PIData = (FAudioPlayerInterface_InstanceData*) PerInstanceData;
	UNiagaraSystem* System = SystemInstance->GetSystem();
	if (!PIData->GatheredData.IsEmpty() && System)
	{
		//Drain the queue into an array here
		TArray<FAudioParticleData> Data;
		FAudioParticleData Value;
		while (PIData->GatheredData.Dequeue(Value))
		{
			Data.Add(Value);
			if (PIData->MaxPlaysPerTick > 0 && Data.Num() >= PIData->MaxPlaysPerTick)
			{
				// discard the rest of the queue if over the tick limit
				PIData->GatheredData.Empty();
				break;
			}
		}
		TGraphTask<FNiagaraAudioPlayerAsyncTask>::CreateTask().ConstructAndDispatchWhenReady(PIData->SoundToPlay, PIData->Attenuation, PIData->Concurrency, Data, SystemInstance->GetWorldManager()->GetWorld());
	}
	return false;
}

bool UNiagaraDataInterfaceAudioPlayer::Equals(const UNiagaraDataInterface* Other) const
{
	if (!Super::Equals(Other))
	{
		return false;
	}

	const UNiagaraDataInterfaceAudioPlayer* OtherPlayer = CastChecked<UNiagaraDataInterfaceAudioPlayer>(Other);
	return OtherPlayer->SoundToPlay == SoundToPlay && OtherPlayer->Attenuation == Attenuation && OtherPlayer->Concurrency == Concurrency && OtherPlayer->bLimitPlaysPerTick == bLimitPlaysPerTick && OtherPlayer->MaxPlaysPerTick == MaxPlaysPerTick;
}

void UNiagaraDataInterfaceAudioPlayer::GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions)
{
	FNiagaraFunctionSignature Sig;
	Sig.Name = PlayAudioName;
#if WITH_EDITORONLY_DATA
	Sig.Description = NSLOCTEXT("Niagara", "PlayAudioDIFunctionDescription", "This function plays a sound at the given location after the simulation has ticked.");
	Sig.ExperimentalMessage = NSLOCTEXT("Niagara", "PlayAudioDIFunctionExperimental", "The return value of the audio function call currently needs to be wired to a particle parameter, because otherwise it will be removed by the compiler.");
#endif
	Sig.bMemberFunction = true;
	Sig.bRequiresContext = false;
	Sig.bSupportsGPU = false;
	Sig.bExperimental = true;
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition(GetClass()), TEXT("Audio interface")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Play Audio")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("PositionWS")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetVec3Def(), TEXT("RotationWS")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("VolumeFactor")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("PitchFactor")));
	Sig.Inputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("StartTime")));
	Sig.Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Success")));
	OutFunctions.Add(Sig);
}

bool UNiagaraDataInterfaceAudioPlayer::GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL)
{
	return false;
}

DEFINE_NDI_DIRECT_FUNC_BINDER(UNiagaraDataInterfaceAudioPlayer, StoreData);
void UNiagaraDataInterfaceAudioPlayer::GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction &OutFunc)
{
	if (BindingInfo.Name == PlayAudioName)
	{
		NDI_FUNC_BINDER(UNiagaraDataInterfaceAudioPlayer, StoreData)::Bind(this, OutFunc);
	}
	else
	{
		UE_LOG(LogNiagara, Error, TEXT("Could not find data interface external function. Expected Name: %s  Actual Name: %s"), *PlayAudioName.ToString(), *BindingInfo.Name.ToString());
	}
}

void UNiagaraDataInterfaceAudioPlayer::StoreData(FVectorVMContext& Context)
{
	VectorVM::FUserPtrHandler<FAudioPlayerInterface_InstanceData> InstData(Context);

	VectorVM::FExternalFuncInputHandler<FNiagaraBool> PlayDataParam(Context);

	VectorVM::FExternalFuncInputHandler<float> PositionParamX(Context);
	VectorVM::FExternalFuncInputHandler<float> PositionParamY(Context);
	VectorVM::FExternalFuncInputHandler<float> PositionParamZ(Context);
	
	VectorVM::FExternalFuncInputHandler<float> RotationParamX(Context);
	VectorVM::FExternalFuncInputHandler<float> RotationParamY(Context);
	VectorVM::FExternalFuncInputHandler<float> RotationParamZ(Context);
	
	VectorVM::FExternalFuncInputHandler<float> VolumeParam(Context);
	VectorVM::FExternalFuncInputHandler<float> PitchParam(Context);
	VectorVM::FExternalFuncInputHandler<float> StartTimeParam(Context);

	VectorVM::FExternalFuncRegisterHandler<FNiagaraBool> OutSample(Context);

	checkfSlow(InstData.Get(), TEXT("Audio player interface has invalid instance data. %s"), *GetPathName());
	bool ValidSoundData = InstData->SoundToPlay.IsValid();

	for (int32 i = 0; i < Context.NumInstances; ++i)
	{
		FNiagaraBool ShouldPlay = PlayDataParam.GetAndAdvance();
		FAudioParticleData Data;
		Data.Position = FVector(PositionParamX.GetAndAdvance(), PositionParamY.GetAndAdvance(), PositionParamZ.GetAndAdvance());
		Data.Rotation = FRotator(RotationParamX.GetAndAdvance(), RotationParamY.GetAndAdvance(), RotationParamZ.GetAndAdvance());
		Data.Volume = VolumeParam.GetAndAdvance();
		Data.Pitch = PitchParam.GetAndAdvance();
		Data.StartTime = StartTimeParam.GetAndAdvance();

		FNiagaraBool Valid;
		if (ValidSoundData && ShouldPlay)
		{
			Valid.SetValue(InstData->GatheredData.Enqueue(Data));
		}
		*OutSample.GetDestAndAdvance() = Valid;
	}
}

bool UNiagaraDataInterfaceAudioPlayer::CopyToInternal(UNiagaraDataInterface* Destination) const
{
	if (!Super::CopyToInternal(Destination))
	{
		return false;
	}

	UNiagaraDataInterfaceAudioPlayer* OtherTyped = CastChecked<UNiagaraDataInterfaceAudioPlayer>(Destination);
	OtherTyped->SoundToPlay = SoundToPlay;
	OtherTyped->Attenuation = Attenuation;
	OtherTyped->Concurrency = Concurrency;
	OtherTyped->bLimitPlaysPerTick = bLimitPlaysPerTick;
	OtherTyped->MaxPlaysPerTick = MaxPlaysPerTick;
	return true;
}
