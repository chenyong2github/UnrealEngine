// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineBaseTypes.h"
#include "NiagaraCommon.h"
#include "NiagaraDataChannelPublic.generated.h"

class FNiagaraWorldManager;
class UNiagaraDataChannelAsset;
class UNiagaraDataChannel;
class UNiagaraDataChannelHandler;
struct FNiagaraDataChannelGameData;
struct FNiagaraDataChannelData;
using FNiagaraDataChannelGameDataPtr = TSharedPtr<FNiagaraDataChannelGameData>;
using FNiagaraDataChannelDataPtr = TSharedPtr<FNiagaraDataChannelData>;

/** Wrapper asset class for UNiagaraDataChannel which is instanced. */
UCLASS(Experimental, DisplayName = "Niagara Data Channel")
class NIAGARA_API UNiagaraDataChannelAsset : public UObject
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = DataChannel, Instanced)
	TObjectPtr<UNiagaraDataChannel> DataChannel;

public:

	UNiagaraDataChannel* Get() const { return DataChannel; }
};

/**
Minimal set of types and declares required for external users of Niagara Data Channels.
*/

/**
Parameters allowing users to search for the correct data channel data to read/write.
Some data channels will sub divide their data internally in various ways, e.g., spacial partition.
These parameters allow users to search for the correct internal data when reading and writing.
*/
USTRUCT(BlueprintType)
struct NIAGARA_API FNiagaraDataChannelSearchParameters
{
	GENERATED_BODY()

	/** In cases where there is an owning component such as an object spawning from itself etc, then we pass that component in. Some handlers may only use it's location but others may make use of more data. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters")
	TObjectPtr<USceneComponent> OwningComponent = nullptr;

	/** In cases where there is no owning component for data being read or written to a data channel, we simply pass in a location. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parameters")
	FVector Location = FVector::ZeroVector;

	FVector GetLocation()const;
};

USTRUCT()
struct FNiagaraDataChannelGameDataLayout
{
	GENERATED_BODY();

	/** Map of all variables contained in this DataChannel data and the indices into data arrays for game data. */
	UPROPERTY()
	TMap<FNiagaraVariableBase, int32> VariableIndices;

	/** Helpers for converting LWC types into Niagara simulation SWC types. */
	UPROPERTY()
	TArray<FNiagaraLwcStructConverter> LwcConverters;

	void Init(const TArray<FNiagaraVariable>& Variables);
};


#if !UE_BUILD_SHIPPING

/** Hooks into internal NiagaraDataChannels code for debugging and testing purposes. */
class NIAGARA_API FNiagaraDataChannelDebugUtilities
{
public: 

	static void BeginFrame(FNiagaraWorldManager* WorldMan, float DeltaSeconds);
	static void EndFrame(FNiagaraWorldManager* WorldMan, float DeltaSeconds);
	static void Tick(FNiagaraWorldManager* WorldMan, float DeltaSeconds, ETickingGroup TickGroup);
	
	static UNiagaraDataChannelHandler* FindDataChannelHandler(FNiagaraWorldManager* WorldMan, UNiagaraDataChannel* DataChannel);
};


#endif