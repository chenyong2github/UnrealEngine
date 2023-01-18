// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleFaderBase.h"

#include "DMXControlConsoleFaderGroup.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleFaderBase"

int32 UDMXControlConsoleFaderBase::GetIndex() const
{
	int32 Index = -1;

	const UDMXControlConsoleFaderGroup* Outer = Cast<UDMXControlConsoleFaderGroup>(GetOuter());
	if (!ensureMsgf(Outer, TEXT("Invalid outer for '%s', cannot get fader index correctly."), *GetName()))
	{
		return Index;
	}

	const TArray<UDMXControlConsoleFaderBase*> Faders = Outer->GetFaders();
	Index = Faders.IndexOfByKey(this);

	return Index;
}

UDMXControlConsoleFaderGroup& UDMXControlConsoleFaderBase::GetOwnerFaderGroupChecked() const
{
	UDMXControlConsoleFaderGroup* Outer = Cast<UDMXControlConsoleFaderGroup>(GetOuter());
	checkf(Outer, TEXT("Invalid outer for '%s', cannot get fader index correctly."), *GetName());

	return *Outer;
}

void UDMXControlConsoleFaderBase::SetFaderName(const FString& NewName)
{ 
	FaderName = NewName;
}

void UDMXControlConsoleFaderBase::SetValue(const uint32 NewValue)
{
	Value = FMath::Clamp(NewValue, MinValue, MaxValue);
}

void UDMXControlConsoleFaderBase::ToggleMute()
{
	bIsMuted = !bIsMuted;
}

void UDMXControlConsoleFaderBase::Destroy()
{
	UDMXControlConsoleFaderGroup* Outer = Cast<UDMXControlConsoleFaderGroup>(GetOuter());
	if (!ensureMsgf(Outer, TEXT("Invalid outer for '%s', cannot destroy fader correctly."), *GetName()))
	{
		return;
	}

#if WITH_EDITOR
	Outer->PreEditChange(UDMXControlConsoleFaderGroup::StaticClass()->FindPropertyByName(UDMXControlConsoleFaderGroup::GetFadersPropertyName()));
#endif // WITH_EDITOR

	Outer->DeleteFader(this);

#if WITH_EDITOR
	Outer->PostEditChange();
#endif // WITH_EDITOR
}

void UDMXControlConsoleFaderBase::PostInitProperties()
{
	Super::PostInitProperties();

	FaderName = GetName();
}

#if WITH_EDITOR
void UDMXControlConsoleFaderBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName != GetValuePropertyName())
	{
		return;
	}

	SetValue(Value);
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
