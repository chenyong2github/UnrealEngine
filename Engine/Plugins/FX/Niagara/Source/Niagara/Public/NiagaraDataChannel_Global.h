// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraDataChannel.h"
#include "NiagaraDataChannelHandler.h"
#include "NiagaraDataChannel_Global.generated.h"


UCLASS(Experimental)
class NIAGARA_API UNiagaraDataChannel_Global : public UNiagaraDataChannel
{
	GENERATED_BODY()

	virtual UNiagaraDataChannelHandler* CreateHandler(UWorld* OwningWorld)const override;
};

/**
Basic DataChannel handler that makes all data visible globally.
*/
UCLASS(Experimental, BlueprintType)
class NIAGARA_API UNiagaraDataChannelHandler_Global : public UNiagaraDataChannelHandler
{
	GENERATED_UCLASS_BODY()

	FNiagaraDataChannelDataPtr Data;

	//UObject Interface
	virtual void BeginDestroy()override;
	//UObject Interface End

	virtual void Init(const UNiagaraDataChannel* InChannel) override;
	virtual void BeginFrame(float DeltaTime, FNiagaraWorldManager* OwningWorld)override;
	virtual void EndFrame(float DeltaTime, FNiagaraWorldManager* OwningWorld)override;
	virtual void Tick(float DeltaTime, ETickingGroup TickGroup, FNiagaraWorldManager* OwningWorld) override;
	virtual FNiagaraDataChannelDataPtr FindData(FNiagaraDataChannelSearchParameters SearchParams, ENiagaraResourceAccess AccessType) override;
};

/**
DataChannel handler that separates DataChannel out by the hash of their position into spatially localized grid cells.
Useful for DataChannel that should only be visible to nearby consumers.
TODO: Likely unsuitable for LWC games as we need a defined min/max for the hash and storage. A sparse octree etc would be needed
*/
// UCLASS(EditInlineNew)
// class UNiagaraDataChannelChannelHandler_WorldGrid : public UObject
// {
// };

// UCLASS(EditInlineNew, abstract)
// class UNiagaraDataChannelChannelHandler_Octree : UObject
// {
// 
// };
