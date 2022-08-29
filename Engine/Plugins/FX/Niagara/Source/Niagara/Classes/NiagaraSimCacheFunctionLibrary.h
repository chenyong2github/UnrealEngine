// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraSimCache.h"

#include "Containers/Ticker.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet/BlueprintAsyncActionBase.h"

#include "NiagaraSimCacheFunctionLibrary.generated.h"

UCLASS()
class UAsyncNiagaraCaptureSimCache : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnCaptureComplete);

	UPROPERTY()
	TObjectPtr<UNiagaraSimCache> CaptureSimCache;

	UPROPERTY()
	TObjectPtr<UNiagaraComponent> CaptureComponent;

	UPROPERTY()
	int32 CaptureNumFrames = 0;

	UPROPERTY()
	int32 CaptureFrameRate = 0;

	UPROPERTY()
	int32 CaptureFrameCounter = 0;

	UPROPERTY()
	int32 TimeOutCounter = 0;

	UPROPERTY(BlueprintAssignable)
	FOnCaptureComplete CaptureComplete;

	FTSTicker::FDelegateHandle TickerHandle;

	virtual void Activate() override;
	virtual void SetReadyToDestroy() override;
	bool OnFrameTick(float DeltaTime);

	/**
	Capture multiple frames from the provided simulation into a SimCache until the simulation becomes inactive, completes or we hit the NumFrames limit.
	Capture occurs at the end of each frame with the first frame being this frame.
	CaptureRate allows you to reduce the rate of capture, i.e. a rate of 2 would capture frames 0, 2, 4, etc.
	*/
	UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true", Category=NiagaraSimCache))
	static UAsyncNiagaraCaptureSimCache* CaptureNiagaraSimCacheMultiFrame(UNiagaraSimCache* SimCache, FNiagaraSimCacheCreateParameters CreateParameters, UNiagaraComponent* NiagaraComponent, int32 NumFrames = 16, int32 CaptureRate = 1);

	/**
	Capture frames from the provided simulation into a SimCache until the simulation becomes inactive or completes.
	Capture occurs at the end of each frame with the first frame being this frame.
	CaptureRate allows you to reduce the rate of capture, i.e. a rate of 2 would capture frames 0, 2, 4, etc.
	*/
	UFUNCTION(BlueprintCallable, meta=(BlueprintInternalUseOnly="true", Category=NiagaraSimCache))
	static UAsyncNiagaraCaptureSimCache* CaptureNiagaraSimCacheUntilComplete(UNiagaraSimCache* SimCache, FNiagaraSimCacheCreateParameters CreateParameters, UNiagaraComponent* NiagaraComponent, int32 CaptureRate = 1);
};

UCLASS()
class NIAGARA_API UNiagaraSimCacheFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_UCLASS_BODY()

public:
	/**
	Captures the simulations current frame data into the SimCache.
	This happens immediately so you may need to be careful with tick order of the component you are capturing from.
	The return can be invalid if the component can not be captured for some reason (i.e. not active).
	*/
	UFUNCTION(BlueprintCallable, Category=NiagaraSimCache)
	static bool CaptureNiagaraSimCacheImmediate(UNiagaraSimCache* SimCache, FNiagaraSimCacheCreateParameters CreateParameters, UNiagaraComponent* NiagaraComponent);
};
