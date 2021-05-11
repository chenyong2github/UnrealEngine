// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraCalibrationSubsystem.h"

#include "CameraCalibrationLog.h"
#include "CineCameraComponent.h"
#include "Engine/TimecodeProvider.h"
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
	ULensFile* ReturnedLens = nullptr;

	if (Picker.bOverrideDefaultLensFile)
	{
		ReturnedLens = Picker.LensFile;
	}
	else
	{
		ReturnedLens = GetDefaultLensFile();
	}

	return ReturnedLens;
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
	}
	else
	{
		UE_LOG(LogCameraCalibration, Verbose, TEXT("Could not create DistortionHandler for LensModel '%s'"), *LensModelClass->GetName());
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

TSubclassOf<ULensModel> UCameraCalibrationSubsystem::GetRegisteredLensModel(FName ModelName) const
{
	if (LensModelMap.Contains(ModelName))
	{
		return LensModelMap[ModelName];
	}
	return nullptr;
}

void UCameraCalibrationSubsystem::Deinitialize()
{
	LensModelMap.Empty(0);

	Super::Deinitialize();
}

#undef LOCTEXT_NAMESPACE


