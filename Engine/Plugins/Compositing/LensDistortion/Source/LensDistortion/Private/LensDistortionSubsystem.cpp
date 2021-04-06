// Copyright Epic Games, Inc. All Rights Reserved.

#include "LensDistortionSubsystem.h"

#include "Components/ActorComponent.h"
#include "Engine/TimecodeProvider.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "UObject/UObjectIterator.h"


#define LOCTEXT_NAMESPACE "LensDistortionSubsystem"



ULensFile* ULensDistortionSubsystem::GetDefaultLensFile() const
{
	return DefaultLensFile;
}

void ULensDistortionSubsystem::SetDefaultLensFile(ULensFile* NewDefaultLensFile)
{
	//Todo : Add callbacks when default lens file changes
	DefaultLensFile = NewDefaultLensFile;
}

ULensFile* ULensDistortionSubsystem::GetLensFile(const FLensFilePicker& Picker) const
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

ULensDistortionModelHandlerBase* ULensDistortionSubsystem::GetDistortionModelHandler(UActorComponent* Component)
{
	if (IInterface_AssetUserData* AssetUserData = Cast<IInterface_AssetUserData>(Component))
	{
		return Cast<ULensDistortionModelHandlerBase>(AssetUserData->GetAssetUserDataOfClass(ULensDistortionModelHandlerBase::StaticClass()));
	}

	return nullptr;
}

ULensDistortionModelHandlerBase* ULensDistortionSubsystem::FindOrCreateDistortionModelHandler(UActorComponent* Component, TSubclassOf<ULensModel> LensModelClass)
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

	// Find all UClasses that derive from ULensDistortionModelHandlerBase
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(ULensDistortionModelHandlerBase::StaticClass()) && !It->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
		{
			if (It->GetDefaultObject<ULensDistortionModelHandlerBase>()->IsModelSupported(LensModelClass))
			{
				Handler = NewObject<ULensDistortionModelHandlerBase>(Component, *It);
				Component->AddAssetUserData(Handler);
				break;
			}
		}
	}

	return Handler;
}

void ULensDistortionSubsystem::RegisterDistortionModel(TSubclassOf<ULensModel> LensModel)
{
	LensModelMap.Add(LensModel->GetDefaultObject<ULensModel>()->GetModelName(), LensModel);
}

void ULensDistortionSubsystem::UnregisterDistortionModel(TSubclassOf<ULensModel> LensModel)
{
	LensModelMap.Remove(LensModel->GetDefaultObject<ULensModel>()->GetModelName());
}

TSubclassOf<ULensModel> ULensDistortionSubsystem::GetRegisteredLensModel(FName ModelName)
{
	if (LensModelMap.Contains(ModelName))
	{
		return LensModelMap[ModelName];
	}
	return nullptr;
}

void ULensDistortionSubsystem::Deinitialize()
{
	LensModelMap.Empty(0);

	Super::Deinitialize();
}

#undef LOCTEXT_NAMESPACE


