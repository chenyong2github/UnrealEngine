// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraSimCacheFunctionLibrary.h"
#include "NiagaraComponent.h"

void UAsyncNiagaraCaptureSimCache::Activate()
{
	Super::Activate();

	if ( CaptureComponent != nullptr )
	{
		if (UWorld* OwnerWorld = CaptureComponent->GetWorld())
		{
			TickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UAsyncNiagaraCaptureSimCache::OnFrameTick), 0);
		}
	}

	if ( CaptureSimCache == nullptr || CaptureComponent == nullptr || TickerHandle.IsValid() == false )
	{
		SetReadyToDestroy();
	}
}

void UAsyncNiagaraCaptureSimCache::SetReadyToDestroy()
{
	Super::SetReadyToDestroy();

	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
	}

	if ( CaptureSimCache != nullptr )
	{
		CaptureSimCache->EndWrite();
	}
	CaptureComplete.Broadcast();
}

bool UAsyncNiagaraCaptureSimCache::OnFrameTick(float DeltaTime)
{
	// Component invalid or not active?  If so complete the cache recording
	if ( CaptureComponent == nullptr || !CaptureComponent->IsActive() )
	{
		SetReadyToDestroy();
		return true;
	}

	// Should we record this frame?
	if ( (CaptureFrameCounter % CaptureFrameRate) == 0 )
	{
		if ( CaptureSimCache != nullptr )
		{
			CaptureSimCache->WriteFrame(CaptureComponent);
		}
	}
	++CaptureFrameCounter;

	// Have we recorded all the frames we need?
	// Note: the -1 is because T0 was the initial frame
	if ( (CaptureNumFrames > 0) && (CaptureFrameCounter > (CaptureFrameRate * (CaptureNumFrames - 1))) )
	{
		SetReadyToDestroy();
		return true;
	}

	return true;
}

UAsyncNiagaraCaptureSimCache* UAsyncNiagaraCaptureSimCache::CaptureNiagaraSimCacheMultiFrame(UNiagaraSimCache* SimCache, FNiagaraSimCacheCreateParameters CreateParameters, UNiagaraComponent* NiagaraComponent, int32 NumFrames, int32 CaptureRate)
{
	UAsyncNiagaraCaptureSimCache* CaptureAction = NewObject<UAsyncNiagaraCaptureSimCache>();
	CaptureAction->CaptureSimCache = SimCache;
	CaptureAction->CaptureComponent = NiagaraComponent;
	CaptureAction->CaptureNumFrames = FMath::Max(1, NumFrames);
	CaptureAction->CaptureFrameRate = FMath::Max(1, CaptureRate);
	CaptureAction->CaptureFrameCounter = 0;

	if (SimCache != nullptr)
	{
		SimCache->BeginWrite(CreateParameters, NiagaraComponent);
	}

	return CaptureAction;
}

UAsyncNiagaraCaptureSimCache* UAsyncNiagaraCaptureSimCache::CaptureNiagaraSimCacheUntilComplete(UNiagaraSimCache* SimCache, FNiagaraSimCacheCreateParameters CreateParameters, UNiagaraComponent* NiagaraComponent, int32 CaptureRate)
{
	UAsyncNiagaraCaptureSimCache* CaptureAction = NewObject<UAsyncNiagaraCaptureSimCache>();
	CaptureAction->CaptureSimCache = SimCache;
	CaptureAction->CaptureComponent = NiagaraComponent;
	CaptureAction->CaptureNumFrames = 0;
	CaptureAction->CaptureFrameRate = FMath::Max(1, CaptureRate);
	CaptureAction->CaptureFrameCounter = 0;

	if (SimCache != nullptr)
	{
		SimCache->BeginWrite(CreateParameters, NiagaraComponent);
	}

	return CaptureAction;
}

UNiagaraSimCacheFunctionLibrary::UNiagaraSimCacheFunctionLibrary(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

bool UNiagaraSimCacheFunctionLibrary::CaptureNiagaraSimCacheImmediate(UNiagaraSimCache* SimCache, FNiagaraSimCacheCreateParameters CreateParameters, UNiagaraComponent* NiagaraComponent)
{
	if ( SimCache == nullptr || NiagaraComponent == nullptr )
	{
		return false;
	}

	SimCache->BeginWrite(CreateParameters, NiagaraComponent);
	SimCache->WriteFrame(NiagaraComponent);
	SimCache->EndWrite();
	return SimCache->IsCacheValid();
}
