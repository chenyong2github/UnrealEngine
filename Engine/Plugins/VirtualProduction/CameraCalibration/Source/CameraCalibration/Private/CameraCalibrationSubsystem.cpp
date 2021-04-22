// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraCalibrationSubsystem.h"

#include "Components/ActorComponent.h"
#include "Engine/TimecodeProvider.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "CameraCalibrationLog.h"
#include "UObject/UObjectIterator.h"


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

ULensDistortionModelHandlerBase* UCameraCalibrationSubsystem::GetDistortionModelHandler(UActorComponent* Component)
{
	if (IInterface_AssetUserData* AssetUserData = Cast<IInterface_AssetUserData>(Component))
	{
		return Cast<ULensDistortionModelHandlerBase>(AssetUserData->GetAssetUserDataOfClass(ULensDistortionModelHandlerBase::StaticClass()));
	}

	return nullptr;
}

ULensDistortionModelHandlerBase* UCameraCalibrationSubsystem::FindOrCreateDistortionModelHandler(UActorComponent* Component, TSubclassOf<ULensModel> LensModelClass)
{
	if (LensModelClass == nullptr)
	{
		return nullptr;
	}

	// Get the current model handler on the input camera (if it exists)
	ULensDistortionModelHandlerBase* Handler = GetDistortionModelHandler(Component);

	// Check if the existing handler supports the input model
	if (Handler)
	{
		if (Handler->IsModelSupported(LensModelClass))
		{
			return Handler;
		}
		else
		{
			Component->RemoveUserDataOfClass(ULensDistortionModelHandlerBase::StaticClass());
			Handler = nullptr;
		}
	}

	const TSubclassOf<ULensDistortionModelHandlerBase> HandlerClass = ULensModel::GetHandlerClass(LensModelClass);
	if(HandlerClass)
	{
		Handler = NewObject<ULensDistortionModelHandlerBase>(Component, HandlerClass);
    	Component->AddAssetUserData(Handler);
	}
	else
	{
		UE_LOG(LogCameraCalibration, Verbose, TEXT("Could not create DistortionHandler for LensModel '%s'"), *LensModelClass->GetName());
	}

	return Handler;
}

void UCameraCalibrationSubsystem::RegisterDistortionModel(TSubclassOf<ULensModel> LensModel)
{
	LensModelMap.Add(LensModel->GetDefaultObject<ULensModel>()->GetModelName(), LensModel);
}

void UCameraCalibrationSubsystem::UnregisterDistortionModel(TSubclassOf<ULensModel> LensModel)
{
	LensModelMap.Remove(LensModel->GetDefaultObject<ULensModel>()->GetModelName());
}

TSubclassOf<ULensModel> UCameraCalibrationSubsystem::GetRegisteredLensModel(FName ModelName)
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


