// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/CameraModifier_CameraShake.h"
#include "Camera/CameraShakeBase.h"
#include "Camera/CameraShakeSourceComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "DisplayDebugHelpers.h"
#include "EngineGlobals.h"

//////////////////////////////////////////////////////////////////////////
// UCameraModifier_CameraShake

DECLARE_CYCLE_STAT(TEXT("AddCameraShake"), STAT_AddCameraShake, STATGROUP_Game);

UCameraModifier_CameraShake::UCameraModifier_CameraShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SplitScreenShakeScale = 0.5f;
}


bool UCameraModifier_CameraShake::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	// Call super where modifier may be disabled
	Super::ModifyCamera(DeltaTime, InOutPOV);

	// If no alpha, exit early
	if( Alpha <= 0.f )
	{
		return false;
	}

	// Update and apply active shakes
	if( ActiveShakes.Num() > 0 )
	{
		for (FActiveCameraShakeInfo& ShakeInfo : ActiveShakes)
		{
			if (ShakeInfo.ShakeInstance != nullptr)
			{
				// Compute the scale of this shake for this frame according to the location
				// of its source.
				float CurShakeAlpha = Alpha;
				if (ShakeInfo.ShakeSource.IsValid())
				{
					const UCameraShakeSourceComponent* SourceComponent = ShakeInfo.ShakeSource.Get();
					const float AttenuationFactor = SourceComponent->GetAttenuationFactor(InOutPOV.Location);
					CurShakeAlpha *= AttenuationFactor;
				}

				ShakeInfo.ShakeInstance->UpdateAndApplyCameraShake(DeltaTime, CurShakeAlpha, InOutPOV);
			}
		}

		// Delete any obsolete shakes
		for (int32 i = ActiveShakes.Num() - 1; i >= 0; i--)
		{
			FActiveCameraShakeInfo ShakeInfo = ActiveShakes[i]; // Copy struct, we're going to maybe delete it.
			if (ShakeInfo.ShakeInstance == nullptr || ShakeInfo.ShakeInstance->IsFinished() || ShakeInfo.ShakeSource.IsStale())
			{
				if (ShakeInfo.ShakeInstance != nullptr)
				{
					ShakeInfo.ShakeInstance->TeardownShake();
				}

				ActiveShakes.RemoveAt(i, 1);

				SaveShakeInExpiredPoolIfPossible(ShakeInfo);
			}
		}
	}

	// If ModifyCamera returns true, exit loop
	// Allows high priority things to dictate if they are
	// the last modifier to be applied
	// Returning true causes to stop adding another modifier! 
	// Returning false is the right behavior since this is not high priority modifier.
	return false;
}

UCameraShakeBase* UCameraModifier_CameraShake::AddCameraShake(TSubclassOf<UCameraShakeBase> ShakeClass, const FAddCameraShakeParams& Params)
{
	SCOPE_CYCLE_COUNTER(STAT_AddCameraShake);

	if (ShakeClass != nullptr)
	{
		float Scale = Params.Scale;
		const UCameraShakeSourceComponent* SourceComponent = Params.SourceComponent;
		const bool bIsCustomInitialized = Params.Initializer.IsBound();

		// Adjust for splitscreen
		if (CameraOwner != nullptr && GEngine->IsSplitScreen(CameraOwner->GetWorld()))
		{
			Scale *= SplitScreenShakeScale;
		}

		UCameraShakeBase const* const ShakeCDO = GetDefault<UCameraShakeBase>(ShakeClass);
		const bool bIsSingleInstance = ShakeCDO && ShakeCDO->bSingleInstance;
		if (bIsSingleInstance)
		{
			// Look for existing instance of same class
			for (FActiveCameraShakeInfo& ShakeInfo : ActiveShakes)
			{
				UCameraShakeBase* ShakeInst = ShakeInfo.ShakeInstance;
				if (ShakeInst && (ShakeClass == ShakeInst->GetClass()))
				{
					if (!ShakeInfo.bIsCustomInitialized && !bIsCustomInitialized)
					{
						// Just restart the existing shake, possibly at the new location.
						// Warning: if the shake source changes, this would "teleport" the shake, which might create a visual
						// artifact, if the user didn't intend to do this.
						ShakeInfo.ShakeSource = SourceComponent;
						ShakeInst->StartShake(CameraOwner, Scale, Params.PlaySpace, Params.UserPlaySpaceRot);
						return ShakeInst;
					}
					else
					{
						// If either the old or new shake are custom initialized, we can't
						// reliably restart the existing shake and expect it to be the same as what the caller wants. 
						// So we forcibly stop the existing shake immediately and will create a brand new one.
						ShakeInst->StopShake(true);
						// Discard it right away so the spot is free in the active shakes array.
						ShakeInfo.ShakeInstance = nullptr;
					}
				}
			}
		}

		// Try to find a shake in the expired pool
		UCameraShakeBase* NewInst = ReclaimShakeFromExpiredPool(ShakeClass);

		// No old shakes, create a new one
		if (NewInst == nullptr)
		{
			NewInst = NewObject<UCameraShakeBase>(this, ShakeClass);
		}

		if (NewInst)
		{
			// Custom initialization if necessary.
			if (bIsCustomInitialized)
			{
				Params.Initializer.Execute(NewInst);
			}

			// Initialize new shake and add it to the list of active shakes
			NewInst->StartShake(CameraOwner, Scale, Params.PlaySpace, Params.UserPlaySpaceRot);

			// Look for nulls in the array to replace first -- keeps the array compact
			bool bReplacedNull = false;
			for (int32 Idx = 0; Idx < ActiveShakes.Num(); ++Idx)
			{
				FActiveCameraShakeInfo& ShakeInfo = ActiveShakes[Idx];
				if (ShakeInfo.ShakeInstance == nullptr)
				{
					ShakeInfo.ShakeInstance = NewInst;
					ShakeInfo.ShakeSource = SourceComponent;
					ShakeInfo.bIsCustomInitialized = bIsCustomInitialized;
					bReplacedNull = true;
				}
			}

			// no holes, extend the array
			if (bReplacedNull == false)
			{
				FActiveCameraShakeInfo ShakeInfo;
				ShakeInfo.ShakeInstance = NewInst;
				ShakeInfo.ShakeSource = SourceComponent;
				ShakeInfo.bIsCustomInitialized = bIsCustomInitialized;
				ActiveShakes.Emplace(ShakeInfo);
			}
		}

		return NewInst;
	}

	return nullptr;
}

void UCameraModifier_CameraShake::SaveShakeInExpiredPool(UCameraShakeBase* ShakeInst)
{
	FPooledCameraShakes& PooledCameraShakes = ExpiredPooledShakesMap.FindOrAdd(ShakeInst->GetClass());
	if (PooledCameraShakes.PooledShakes.Num() < 5)
	{
		PooledCameraShakes.PooledShakes.Emplace(ShakeInst);
	}
}

void UCameraModifier_CameraShake::SaveShakeInExpiredPoolIfPossible(const FActiveCameraShakeInfo& ShakeInfo)
{
	if (ShakeInfo.ShakeInstance && !ShakeInfo.bIsCustomInitialized)
	{
		SaveShakeInExpiredPool(ShakeInfo.ShakeInstance);
	}
}

UCameraShakeBase* UCameraModifier_CameraShake::ReclaimShakeFromExpiredPool(TSubclassOf<UCameraShakeBase> CameraShakeClass)
{
	if (FPooledCameraShakes* PooledCameraShakes = ExpiredPooledShakesMap.Find(CameraShakeClass))
	{
		if (PooledCameraShakes->PooledShakes.Num() > 0)
		{
			UCameraShakeBase* OldShake = PooledCameraShakes->PooledShakes.Pop();
			// Calling new object with the exact same name will re-initialize the uobject in place
			OldShake = NewObject<UCameraShakeBase>(this, CameraShakeClass, OldShake->GetFName());
			return OldShake;
		}
	}
	return nullptr;
}

void UCameraModifier_CameraShake::GetActiveCameraShakes(TArray<FActiveCameraShakeInfo>& ActiveCameraShakes) const
{
	ActiveCameraShakes.Append(ActiveShakes);
}

void UCameraModifier_CameraShake::RemoveCameraShake(UCameraShakeBase* ShakeInst, bool bImmediately)
{
	for (int32 i = 0; i < ActiveShakes.Num(); ++i)
	{
		FActiveCameraShakeInfo& ShakeInfo = ActiveShakes[i];
		if (ShakeInfo.ShakeInstance == ShakeInst)
		{
			ShakeInst->StopShake(bImmediately);

			if (bImmediately)
			{
				SaveShakeInExpiredPoolIfPossible(ShakeInfo);
				ActiveShakes.RemoveAt(i, 1);
			}
			break;
		}
	}
}

void UCameraModifier_CameraShake::RemoveAllCameraShakesOfClass(TSubclassOf<UCameraShakeBase> ShakeClass, bool bImmediately)
{
	for (int32 i = ActiveShakes.Num()- 1; i >= 0; --i)
	{
		FActiveCameraShakeInfo& ShakeInfo = ActiveShakes[i];
		UCameraShakeBase* ShakeInst = ShakeInfo.ShakeInstance;
		if (ShakeInst != nullptr && (ShakeInst->GetClass()->IsChildOf(ShakeClass)))
		{
			ShakeInst->StopShake(bImmediately);
			if (bImmediately)
			{
				SaveShakeInExpiredPoolIfPossible(ShakeInfo);
				ActiveShakes.RemoveAt(i, 1);
			}
		}
	}
}

void UCameraModifier_CameraShake::RemoveAllCameraShakesFromSource(const UCameraShakeSourceComponent* SourceComponent, bool bImmediately)
{
	for (int32 i = ActiveShakes.Num() - 1; i >= 0; --i)
	{
		FActiveCameraShakeInfo& ShakeInfo = ActiveShakes[i];
		if (ShakeInfo.ShakeSource.Get() == SourceComponent && ShakeInfo.ShakeInstance != nullptr)
		{
			ShakeInfo.ShakeInstance->StopShake(bImmediately);
			if (bImmediately)
			{
				SaveShakeInExpiredPoolIfPossible(ShakeInfo);
				ActiveShakes.RemoveAt(i, 1);
			}
		}
	}
}

void UCameraModifier_CameraShake::RemoveAllCameraShakesOfClassFromSource(TSubclassOf<UCameraShakeBase> ShakeClass, const UCameraShakeSourceComponent* SourceComponent, bool bImmediately)
{
	for (int32 i = ActiveShakes.Num() - 1; i >= 0; --i)
	{
		FActiveCameraShakeInfo& ShakeInfo = ActiveShakes[i];
		if (ShakeInfo.ShakeSource.Get() == SourceComponent && 
				ShakeInfo.ShakeInstance != nullptr && 
				ShakeInfo.ShakeInstance->GetClass()->IsChildOf(ShakeClass))
		{
			ShakeInfo.ShakeInstance->StopShake(bImmediately);
			if (bImmediately)
			{
				SaveShakeInExpiredPoolIfPossible(ShakeInfo);
				ActiveShakes.RemoveAt(i, 1);
			}
		}
	}
}

void UCameraModifier_CameraShake::RemoveAllCameraShakes(bool bImmediately)
{
	// Clean up any active camera shake anims
	for (FActiveCameraShakeInfo& ShakeInfo : ActiveShakes)
	{
		ShakeInfo.ShakeInstance->StopShake(bImmediately);
	}

	if (bImmediately)
	{
		for (FActiveCameraShakeInfo& ShakeInfo : ActiveShakes)
		{
			SaveShakeInExpiredPoolIfPossible(ShakeInfo);
		}

		// Clear ActiveShakes array
		ActiveShakes.Empty();
	}
}

void UCameraModifier_CameraShake::DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DebugDisplay, float& YL, float& YPos)
{
	Canvas->SetDrawColor(FColor::Yellow);
	UFont* DrawFont = GEngine->GetSmallFont();
	
	int Indentation = 1;
	int LineNumber = FMath::CeilToInt(YPos / YL);

	Canvas->DrawText(DrawFont, FString::Printf(TEXT("Modifier_CameraShake %s, Alpha:%f"), *GetNameSafe(this), Alpha), Indentation * YL, (LineNumber++) * YL);

	Indentation = 2;
	for (int i = 0; i < ActiveShakes.Num(); i++)
	{
		FActiveCameraShakeInfo& ShakeInfo = ActiveShakes[i];

		if (ShakeInfo.ShakeInstance != nullptr)
		{
			Canvas->DrawText(DrawFont, FString::Printf(TEXT("[%d] %s Source:%s"), i, *GetNameSafe(ShakeInfo.ShakeInstance), *GetNameSafe(ShakeInfo.ShakeSource.Get())), Indentation* YL, (LineNumber++)* YL);
		}
	}

	YPos = LineNumber * YL;

	Super::DisplayDebug(Canvas, DebugDisplay, YL, YPos);
}
