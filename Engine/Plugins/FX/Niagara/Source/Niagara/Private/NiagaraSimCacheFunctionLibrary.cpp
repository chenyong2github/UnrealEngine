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
	if ( CaptureComponent == nullptr || !CaptureComponent->IsActive() || CaptureSimCache == nullptr )
	{
		SetReadyToDestroy();
		return true;
	}

	// Should we record this frame?
	if ( (CaptureFrameCounter % CaptureFrameRate) == 0 )
	{
		// If we fail to capture the frame it might be because things became invalid
		// Or it might be because the simulation was not ticked since the last capture in which case don't advance the counter
		if ( CaptureSimCache->WriteFrame(CaptureComponent) == false )
		{
			if ( CaptureSimCache->IsCacheValid() == false )
			{
				SetReadyToDestroy();
			}

			// Make sure we don't keep this alive forever, if we didn't managed to capture anything in 10 ticks something has probably gone wrong so bail
			if (TimeOutCounter++ > 10)
			{
				UE_LOG(LogNiagara, Warning, TEXT("SimCache Write has failed too many times, abandoning capturing for (%s)"), *GetFullNameSafe(CaptureSimCache));
				SetReadyToDestroy();
			}
			return true;
		}
	}

	TimeOutCounter = 0;
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
