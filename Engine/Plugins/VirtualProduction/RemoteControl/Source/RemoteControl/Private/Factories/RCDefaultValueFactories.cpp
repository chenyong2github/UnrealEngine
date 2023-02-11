// Copyright Epic Games, Inc. All Rights Reserved.

#include "Factories/RCDefaultValueFactories.h"
#include "Components/LightComponent.h"

TSharedRef<IRCDefaultValueFactory> FLightIntensityDefaultValueFactory::MakeInstance()
{
	return MakeShared<FLightIntensityDefaultValueFactory>();
}

bool FLightIntensityDefaultValueFactory::CanResetToDefaultValue(UObject* InObject) const
{
	if (const ULightComponent* LightComponent = Cast<ULightComponent>(InObject))
	{
		if (const ULightComponent* ArchetypeComponent = Cast<ULightComponent>(LightComponent->GetArchetype()))
		{
			return !FMath::IsNearlyEqual(LightComponent->ComputeLightBrightness(), ArchetypeComponent->ComputeLightBrightness());
		}
	}

	return false;
}

void FLightIntensityDefaultValueFactory::ResetToDefaultValue(UObject* InObject, FProperty* InProperty)
{
#if WITH_EDITOR
	
	InObject->PreEditChange(InProperty);
	InObject->Modify();

	if (ULightComponent* LightComponent = Cast<ULightComponent>(InObject))
	{
		if (const ULightComponent* ArchetypeComponent = Cast<ULightComponent>(LightComponent->GetArchetype()))
		{
			LightComponent->SetLightBrightness(ArchetypeComponent->ComputeLightBrightness());
		}
	}

	FPropertyChangedEvent ChangeEvent(InProperty, EPropertyChangeType::ValueSet);
	InObject->PostEditChangeProperty(ChangeEvent);

#endif // WITH_EDITOR
}

bool FLightIntensityDefaultValueFactory::SupportsClass(const UClass* InObjectClass) const
{
	if (!InObjectClass)
	{
		return false;
	}

	return InObjectClass->IsChildOf<ULightComponentBase>();
}

bool FLightIntensityDefaultValueFactory::SupportsProperty(const FProperty* InProperty) const
{
	if (!InProperty)
	{
		return false;
	}

	return InProperty->GetFName() == GET_MEMBER_NAME_CHECKED(ULightComponentBase, Intensity);
}
