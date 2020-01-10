// Copyright Epic Games, Inc. All Rights Reserved.

#include "Camera/CameraModifier_CameraShake.h"
#include "Camera/CameraShake.h"
#include "Camera/CameraShakeSourceComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
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
				ActiveShakes.RemoveAt(i, 1);

				if (ShakeInfo.ShakeInstance != nullptr)
				{
					SaveShakeInExpiredPool(ShakeInfo.ShakeInstance);
				}
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

UCameraShake* UCameraModifier_CameraShake::AddCameraShake(TSubclassOf<UCameraShake> ShakeClass, const FAddCameraShakeParams& Params)
{
	SCOPE_CYCLE_COUNTER(STAT_AddCameraShake);

	if (ShakeClass != nullptr)
	{
		float Scale = Params.Scale;
		const UCameraShakeSourceComponent* SourceComponent = Params.SourceComponent;

		// Adjust for splitscreen
		if (CameraOwner != nullptr && GEngine->IsSplitScreen(CameraOwner->GetWorld()))
		{
			Scale *= SplitScreenShakeScale;
		}

		UCameraShake const* const ShakeCDO = GetDefault<UCameraShake>(ShakeClass);
		const bool bIsSingleInstance = ShakeCDO && ShakeCDO->bSingleInstance;
		if (bIsSingleInstance)
		{
			// Look for existing instance of same class
			for (FActiveCameraShakeInfo& ShakeInfo : ActiveShakes)
			{
				UCameraShake* ShakeInst = ShakeInfo.ShakeInstance;
				if (ShakeInst && (ShakeClass == ShakeInst->GetClass()))
				{
					// Just restart the existing shake
					ShakeInst->PlayShake(CameraOwner, Scale, Params.PlaySpace, Params.UserPlaySpaceRot);
					return ShakeInst;
				}
			}

			// No existing instance... we'll build a new one, but let's null the
			// source in case one was specified (we don't support a source because
			// it would in some cases restart the shake by teleporting it to another
			// location, which would look bad).
			ensureMsgf(SourceComponent == nullptr, TEXT("CameraShake assets with SingleInstance enabled shouldn't be located to a source."));
			SourceComponent = nullptr;
		}

		// Try to find a shake in the expired pool
		UCameraShake* NewInst = ReclaimShakeFromExpiredPool(ShakeClass);

		// No old shakes, create a new one
		if (NewInst == nullptr)
		{
			NewInst = NewObject<UCameraShake>(this, ShakeClass);
		}

		if (NewInst)
		{
			// Initialize new shake and add it to the list of active shakes
			NewInst->PlayShake(CameraOwner, Scale, Params.PlaySpace, Params.UserPlaySpaceRot);

			// Look for nulls in the array to replace first -- keeps the array compact
			bool bReplacedNull = false;
			for (int32 Idx = 0; Idx < ActiveShakes.Num(); ++Idx)
			{
				FActiveCameraShakeInfo& ShakeInfo = ActiveShakes[Idx];
				if (ShakeInfo.ShakeInstance == nullptr)
				{
					ShakeInfo.ShakeInstance = NewInst;
					ShakeInfo.ShakeSource = SourceComponent;
					bReplacedNull = true;
				}
			}

			// no holes, extend the array
			if (bReplacedNull == false)
			{
				FActiveCameraShakeInfo ShakeInfo;
				ShakeInfo.ShakeInstance = NewInst;
				ShakeInfo.ShakeSource = SourceComponent;
				ActiveShakes.Emplace(ShakeInfo);
			}
		}

		return NewInst;
	}

	return nullptr;
}

void UCameraModifier_CameraShake::SaveShakeInExpiredPool(UCameraShake* ShakeInst)
{
	FPooledCameraShakes& PooledCameraShakes = ExpiredPooledShakesMap.FindOrAdd(ShakeInst->GetClass());
	if (PooledCameraShakes.PooledShakes.Num() < 5)
	{
		PooledCameraShakes.PooledShakes.Emplace(ShakeInst);
	}
}

UCameraShake* UCameraModifier_CameraShake::ReclaimShakeFromExpiredPool(TSubclassOf<UCameraShake> CameraShakeClass)
{
	if (FPooledCameraShakes* PooledCameraShakes = ExpiredPooledShakesMap.Find(CameraShakeClass))
	{
		if (PooledCameraShakes->PooledShakes.Num() > 0)
		{
			UCameraShake* OldShake = PooledCameraShakes->PooledShakes.Pop();
			// Calling new object with the exact same name will re-initialize the uobject in place
			OldShake = NewObject<UCameraShake>(this, CameraShakeClass, OldShake->GetFName());
			return OldShake;
		}
	}
	return nullptr;
}

void UCameraModifier_CameraShake::GetActiveCameraShakes(TArray<FActiveCameraShakeInfo>& ActiveCameraShakes) const
{
	ActiveCameraShakes.Append(ActiveShakes);
}

void UCameraModifier_CameraShake::RemoveCameraShake(UCameraShake* ShakeInst, bool bImmediately)
{
	for (int32 i = 0; i < ActiveShakes.Num(); ++i)
	{
		FActiveCameraShakeInfo& ShakeInfo = ActiveShakes[i];
		if (ShakeInfo.ShakeInstance == ShakeInst)
		{
			ShakeInst->StopShake(bImmediately);

			if (bImmediately)
			{
				ActiveShakes.RemoveAt(i, 1);
				SaveShakeInExpiredPool(ShakeInst);
			}
			break;
		}
	}
}

void UCameraModifier_CameraShake::RemoveAllCameraShakesOfClass(TSubclassOf<UCameraShake> ShakeClass, bool bImmediately)
{
	for (int32 i = ActiveShakes.Num()- 1; i >= 0; --i)
	{
		UCameraShake* ShakeInst = ActiveShakes[i].ShakeInstance;
		if (ShakeInst != nullptr && (ShakeInst->GetClass()->IsChildOf(ShakeClass)))
		{
			ShakeInst->StopShake(bImmediately);
			if (bImmediately)
			{
				ActiveShakes.RemoveAt(i, 1);
				SaveShakeInExpiredPool(ShakeInst);
			}
		}
	}
}

void UCameraModifier_CameraShake::RemoveAllCameraShakesFromSource(const UCameraShakeSourceComponent* SourceComponent, bool bImmediately)
{
	for (int32 i = ActiveShakes.Num() - 1; i >= 0; --i)
	{
		FActiveCameraShakeInfo ShakeInfo = ActiveShakes[i];  // Copy struct because we might delete it.
		if (ShakeInfo.ShakeSource.Get() == SourceComponent && ShakeInfo.ShakeInstance != nullptr)
		{
			ShakeInfo.ShakeInstance->StopShake(bImmediately);
			if (bImmediately)
			{
				ActiveShakes.RemoveAt(i, 1);
				SaveShakeInExpiredPool(ShakeInfo.ShakeInstance);
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
			SaveShakeInExpiredPool(ShakeInfo.ShakeInstance);
		}

		// Clear ActiveShakes array
		ActiveShakes.Empty();
	}
}
