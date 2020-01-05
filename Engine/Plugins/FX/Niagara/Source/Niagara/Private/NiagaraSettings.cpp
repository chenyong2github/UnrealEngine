// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraSettings.h"

UNiagaraSettings::UNiagaraSettings(const FObjectInitializer& ObjectInitlaizer)
	: Super(ObjectInitlaizer)
	, DefaultEffectTypePtr(nullptr)
{

}

FName UNiagaraSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}

#if WITH_EDITOR
FText UNiagaraSettings::GetSectionText() const
{
	return NSLOCTEXT("NiagaraPlugin", "NiagaraSettingsSection", "Niagara");
}
#endif

void UNiagaraSettings::PostInitProperties()
{
	Super::PostInitProperties();

	DefaultEffectTypePtr = Cast<UNiagaraEffectType>(DefaultEffectType.TryLoad());
}

#if WITH_EDITOR
void UNiagaraSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	if (PropertyChangedEvent.Property != nullptr)
	{
		SettingsChangedDelegate.Broadcast(PropertyChangedEvent.Property->GetName(), this);
	}

	DefaultEffectTypePtr = Cast<UNiagaraEffectType>(DefaultEffectType.TryLoad());
}

UNiagaraSettings::FOnNiagaraSettingsChanged& UNiagaraSettings::OnSettingsChanged()
{
	return SettingsChangedDelegate;
}

UNiagaraSettings::FOnNiagaraSettingsChanged UNiagaraSettings::SettingsChangedDelegate;
#endif

UNiagaraEffectType* UNiagaraSettings::GetDefaultEffectType()const
{
	return DefaultEffectTypePtr;
}
