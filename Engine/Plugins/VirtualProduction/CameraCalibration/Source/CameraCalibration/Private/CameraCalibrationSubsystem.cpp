// Copyright Epic Games, Inc. All Rights Reserved.

#include "CameraCalibrationSubsystem.h"

#include "CameraCalibrationLog.h"
#include "Components/ActorComponent.h"
#include "Engine/TimecodeProvider.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "UObject/UObjectIterator.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

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

ULensDistortionModelHandlerBase* UCameraCalibrationSubsystem::FindOrCreateDistortionModelHandler(UActorComponent* Component, TSubclassOf<ULensModel> LensModelClass, EHandlerOverrideMode OverrideMode)
{
	if (LensModelClass == nullptr)
	{
		return nullptr;
	}

	// Get the current model handler on the input component (if it exists)
	if (ULensDistortionModelHandlerBase* Handler = GetDistortionModelHandler(Component))
	{
		// If the existing handler supports the input model, simply return it
		if (Handler->IsModelSupported(LensModelClass))
		{
			return Handler;
		}
		else if (OverrideMode == EHandlerOverrideMode::NoOverride)
		{
			return nullptr;
		}

		UActorComponent* ComponentToCheck = Component;
#if WITH_EDITOR
		// Actors are duplicated in PIE, so their components will not appear in the subsystem's set of components with authoritative models
		// Therefore, the component to check should be the editor world counterpart to the input component
		if (Component->GetWorld() && (Component->GetWorld()->WorldType == EWorldType::PIE))
		{
			if (AActor* PIEOwner = EditorUtilities::GetEditorWorldCounterpartActor(Component->GetOwner()))
			{
				ComponentToCheck = PIEOwner->GetComponentByClass(Component->GetClass());
			}
		}
#endif

		if (OverrideMode == EHandlerOverrideMode::SoftOverride)
		{
			// If the input component already has an authoritative model, do not override the existing handler
			if (ComponentsWithAuthoritativeModels.Contains(FObjectKey(ComponentToCheck))
				&& (ComponentsWithAuthoritativeModels[ComponentToCheck] != LensModelClass))
			{
				return nullptr;
			}
			// Mark the input component as having an authoritative model and remove the existing handler
			else
			{
				ComponentsWithAuthoritativeModels.Add(FObjectKey(ComponentToCheck), LensModelClass);
				Component->RemoveUserDataOfClass(ULensDistortionModelHandlerBase::StaticClass());
			}
		}
		// Mark the input component as having an authoritative model and remove the existing handler
		else if (OverrideMode == EHandlerOverrideMode::ForceOverride)
		{
			// Override the authoritative model for this component, if it already has one
			if (ComponentsWithAuthoritativeModels.Contains(FObjectKey(ComponentToCheck))
				&& (ComponentsWithAuthoritativeModels[ComponentToCheck] != LensModelClass))
			{
				ComponentsWithAuthoritativeModels[ComponentToCheck] = LensModelClass;
			}
			else
			{
				ComponentsWithAuthoritativeModels.Add(FObjectKey(ComponentToCheck), LensModelClass);
			}

			Component->RemoveUserDataOfClass(ULensDistortionModelHandlerBase::StaticClass());
		}
	}

	const TSubclassOf<ULensDistortionModelHandlerBase> HandlerClass = ULensModel::GetHandlerClass(LensModelClass);
	ULensDistortionModelHandlerBase* NewHandler = nullptr;
	if(HandlerClass)
	{
		NewHandler = NewObject<ULensDistortionModelHandlerBase>(Component, HandlerClass);
    	Component->AddAssetUserData(NewHandler);
	}
	else
	{
		UE_LOG(LogCameraCalibration, Verbose, TEXT("Could not create DistortionHandler for LensModel '%s'"), *LensModelClass->GetName());
	}

	return NewHandler;
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


