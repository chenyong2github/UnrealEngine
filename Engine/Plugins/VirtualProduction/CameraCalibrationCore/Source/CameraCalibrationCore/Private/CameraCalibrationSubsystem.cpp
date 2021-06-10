// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraCalibrationSubsystem.h"

#include "CameraCalibrationCoreLog.h"
#include "CineCameraComponent.h"
#include "Engine/TimecodeProvider.h"
#include "Misc/CoreDelegates.h"
#include "UObject/UObjectIterator.h"

#if WITH_EDITOR
#include "Editor.h"
#endif // WITH_EDITOR

#define LOCTEXT_NAMESPACE "CameraCalibrationSubsystem"



ULensFile* UCameraCalibrationSubsystem::GetDefaultLensFile() const
{
	return DefaultLensFile;
}

void UCameraCalibrationSubsystem::SetDefaultLensFile(ULensFile* NewDefaultLensFile)
{
	//Todo : Add callbacks when default lens file changes
	DefaultLensFile = NewDefaultLensFile;
}

ULensFile* UCameraCalibrationSubsystem::GetLensFile(const FLensFilePicker& Picker) const
{
	if (Picker.bUseDefaultLensFile)
	{
		return GetDefaultLensFile();
	}
	else
	{
		return Picker.LensFile;
	}
}

TArray<ULensDistortionModelHandlerBase*> UCameraCalibrationSubsystem::GetDistortionModelHandlers(UCineCameraComponent* Component)
{
	TArray<ULensDistortionModelHandlerBase*> Handlers;
	LensDistortionHandlerMap.MultiFind(Component, Handlers);

	return Handlers;
}

ULensDistortionModelHandlerBase* UCameraCalibrationSubsystem::FindDistortionModelHandler(FDistortionHandlerPicker& DistortionHandlerPicker, bool bUpdatePicker) const
{
	TArray<ULensDistortionModelHandlerBase*> Handlers;
	LensDistortionHandlerMap.MultiFind(DistortionHandlerPicker.TargetCameraComponent, Handlers);

	if (!DistortionHandlerPicker.DistortionProducerID.IsValid() && DistortionHandlerPicker.HandlerDisplayName.IsEmpty())
	{
		if (Handlers.Num() > 0)
		{
			if (bUpdatePicker)
			{
				DistortionHandlerPicker.DistortionProducerID = Handlers[0]->GetDistortionProducerID();
				DistortionHandlerPicker.HandlerDisplayName = Handlers[0]->GetDisplayName();
			}

			return Handlers[0];
		}
	}

	// Look through the available handlers to find the one driven by the distortion source's producer
	for (ULensDistortionModelHandlerBase* Handler : Handlers)
	{
		if (Handler->GetDistortionProducerID() == DistortionHandlerPicker.DistortionProducerID)
		{
			// Reassign the input handler picker's display name to that of the found handler
			if (bUpdatePicker)
			{
				DistortionHandlerPicker.HandlerDisplayName = Handler->GetDisplayName();
			}
			return Handler;
		}
	}

	// Look through the available handlers to find one with the same name as the input handler picker's display name
	for (ULensDistortionModelHandlerBase* Handler : Handlers)
	{
		if (Handler->GetDisplayName() == DistortionHandlerPicker.HandlerDisplayName)
		{
			// Reassign the input handler picker's producer to that of the found handler
			if (bUpdatePicker)
			{
				DistortionHandlerPicker.DistortionProducerID = Handler->GetDistortionProducerID();
			}
			return Handler;
		}
	}

	return nullptr;
}

ULensDistortionModelHandlerBase* UCameraCalibrationSubsystem::FindOrCreateDistortionModelHandler(FDistortionHandlerPicker& DistortionHandlerPicker, const TSubclassOf<ULensModel> LensModelClass)
{
	if (LensModelClass == nullptr)
	{
		return nullptr;
	}

	// Attempt to find a handler associated with the input distortion source
	if (ULensDistortionModelHandlerBase* Handler = FindDistortionModelHandler(DistortionHandlerPicker, false))
	{
		// If the existing handler supports the input model, simply return it
		if (Handler->IsModelSupported(LensModelClass))
		{
			// The display name may have changed, even if the handler already exists, so update the display name of the existing handler to match the distortion source
			Handler->SetDisplayName(DistortionHandlerPicker.HandlerDisplayName);
			return Handler;
		}
		else
		{
			// If the input distortion source has an existing handler, but model does not match the input model, remove the old handler from the map
			LensDistortionHandlerMap.Remove(DistortionHandlerPicker.TargetCameraComponent, Handler);
		}
	}

	// If no handler exists for the input distortion source, create a new one
	const TSubclassOf<ULensDistortionModelHandlerBase> HandlerClass = ULensModel::GetHandlerClass(LensModelClass);
	ULensDistortionModelHandlerBase* NewHandler = nullptr;
	if(HandlerClass)
	{
		NewHandler = NewObject<ULensDistortionModelHandlerBase>(DistortionHandlerPicker.TargetCameraComponent, HandlerClass);
		NewHandler->SetDistortionProducerID(DistortionHandlerPicker.DistortionProducerID);
		NewHandler->SetDisplayName(DistortionHandlerPicker.HandlerDisplayName);
		LensDistortionHandlerMap.Add(FObjectKey(DistortionHandlerPicker.TargetCameraComponent), NewHandler);

		if (!CachedFocalLengthMap.Contains(DistortionHandlerPicker.TargetCameraComponent))
		{
			FCachedFocalLength CachedFocalLength = { DistortionHandlerPicker.TargetCameraComponent->CurrentFocalLength, 0.0f };
			CachedFocalLengthMap.Add(DistortionHandlerPicker.TargetCameraComponent, CachedFocalLength);
		}
	}
	else
	{
		UE_LOG(LogCameraCalibrationCore, Verbose, TEXT("Could not create DistortionHandler for LensModel '%s'"), *LensModelClass->GetName());
	}

	return NewHandler;
}

void UCameraCalibrationSubsystem::UnregisterDistortionModelHandler(UCineCameraComponent* Component, ULensDistortionModelHandlerBase* Handler)
{
	LensDistortionHandlerMap.Remove(Component, Handler);
}

void UCameraCalibrationSubsystem::RegisterDistortionModel(TSubclassOf<ULensModel> LensModel)
{
	LensModelMap.Add(LensModel->GetDefaultObject<ULensModel>()->GetModelName(), LensModel);
}

void UCameraCalibrationSubsystem::UnregisterDistortionModel(TSubclassOf<ULensModel> LensModel)
{
	LensModelMap.Remove(LensModel->GetDefaultObject<ULensModel>()->GetModelName());
}

void UCameraCalibrationSubsystem::UpdateOriginalFocalLength(UCineCameraComponent* Component, float InFocalLength)
{
	FCachedFocalLength* CachedFocalLength = CachedFocalLengthMap.Find(Component);
	if (CachedFocalLength)
	{
		// If the input focal length matches the focal length of the camera after overscan was applied, do not update the original (prevents double overscan)
		if (!FMath::IsNearlyEqual(InFocalLength, CachedFocalLength->OverscanFocalLength))
		{
			CachedFocalLength->OriginalFocalLength = InFocalLength;
		}
	}
}

void UCameraCalibrationSubsystem::UpdateOverscanFocalLength(UCineCameraComponent* Component, float InFocalLength)
{
	FCachedFocalLength* CachedFocalLength = CachedFocalLengthMap.Find(Component);
	if (CachedFocalLength)
	{
		CachedFocalLength->OverscanFocalLength = InFocalLength;
	}
}

bool UCameraCalibrationSubsystem::GetOriginalFocalLength(UCineCameraComponent* Component, float& OutFocalLength)
{
	FCachedFocalLength* CachedFocalLength = CachedFocalLengthMap.Find(Component);
	if (CachedFocalLength)
	{
		OutFocalLength = CachedFocalLength->OriginalFocalLength;
		return true;
	}
	return false;
}

TSubclassOf<ULensModel> UCameraCalibrationSubsystem::GetRegisteredLensModel(FName ModelName) const
{
	if (LensModelMap.Contains(ModelName))
	{
		return LensModelMap[ModelName];
	}
	return nullptr;
}

TSubclassOf<UCameraNodalOffsetAlgo> UCameraCalibrationSubsystem::GetCameraNodalOffsetAlgo(FName Name) const
{
	if (CameraNodalOffsetAlgosMap.Contains(Name))
	{
		return CameraNodalOffsetAlgosMap[Name];
	}
	return nullptr;
}

TArray<FName> UCameraCalibrationSubsystem::GetCameraNodalOffsetAlgos() const
{
	TArray<FName> OutKeys;
	CameraNodalOffsetAlgosMap.GetKeys(OutKeys);
	return OutKeys;
}

TSubclassOf<UCameraCalibrationStep> UCameraCalibrationSubsystem::GetCameraCalibrationStep(FName Name) const
{
	if (CameraCalibrationStepsMap.Contains(Name))
	{
		return CameraCalibrationStepsMap[Name];
	}
	return nullptr;
}

TArray<FName> UCameraCalibrationSubsystem::GetCameraCalibrationSteps() const
{
	TArray<FName> OutKeys;
	CameraCalibrationStepsMap.GetKeys(OutKeys);
	return OutKeys;
}

void UCameraCalibrationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	PostEngineInitHandle = FCoreDelegates::OnPostEngineInit.AddLambda([&]()
	{
		// Find Nodal Offset Algos
		{
			TArray<TSubclassOf<UCameraNodalOffsetAlgo>> Algos;

			for (TObjectIterator<UClass> AlgoIt; AlgoIt; ++AlgoIt)
			{
				if (AlgoIt->IsChildOf(UCameraNodalOffsetAlgo::StaticClass()) && !AlgoIt->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
				{
					const UCameraNodalOffsetAlgo* Algo = CastChecked<UCameraNodalOffsetAlgo>(AlgoIt->GetDefaultObject());
					CameraNodalOffsetAlgosMap.Add(Algo->FriendlyName(), TSubclassOf<UCameraNodalOffsetAlgo>(*AlgoIt));
				}
			}
		}

		// Find Calibration Steps
		{
			TArray<TSubclassOf<UCameraCalibrationStep>> Steps;

			for (TObjectIterator<UClass> StepIt; StepIt; ++StepIt)
			{
				if (StepIt->IsChildOf(UCameraCalibrationStep::StaticClass()) && !StepIt->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
				{
					CameraCalibrationStepsMap.Add(StepIt->GetFName(), TSubclassOf<UCameraCalibrationStep>(*StepIt));
				}
			}
		}
	});
}

void UCameraCalibrationSubsystem::Deinitialize()
{
	LensModelMap.Empty(0);
	CameraNodalOffsetAlgosMap.Empty(0);
	CameraCalibrationStepsMap.Empty(0);

	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::OnPostEngineInit.Remove(PostEngineInitHandle);
	}

	Super::Deinitialize();
}

#undef LOCTEXT_NAMESPACE


