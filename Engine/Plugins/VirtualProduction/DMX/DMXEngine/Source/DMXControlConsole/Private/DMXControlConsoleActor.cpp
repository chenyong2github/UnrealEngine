// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleActor.h"

#include "DMXControlConsole.h"

#include "Components/SceneComponent.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleActor"

ADMXControlConsoleActor::ADMXControlConsoleActor()
{
	RootSceneComponent = CreateDefaultSubobject<USceneComponent>("SceneComponent");
	RootComponent = RootSceneComponent;
}

#if WITH_EDITOR
void ADMXControlConsoleActor::SetDMXControlConsole(UDMXControlConsole* InDMXControlConsole)
{
	if (!ensureAlwaysMsgf(!DMXControlConsole, TEXT("Tried to set the DMXControlConsole for %s, but it already has one set. Changing the control console is not supported."), *GetName()))
	{
		return;
	}

	if (!InDMXControlConsole || InDMXControlConsole == DMXControlConsole)
	{
		return;
	}

	DMXControlConsole = InDMXControlConsole;
}
#endif // WITH_EDITOR

void ADMXControlConsoleActor::StartSendingDMX()
{
	if (DMXControlConsole)
	{
		DMXControlConsole->StartSendingDMX();
	}
}

void ADMXControlConsoleActor::StopSendingDMX()
{
	if (DMXControlConsole)
	{
		DMXControlConsole->StopSendingDMX();
	}
}

void ADMXControlConsoleActor::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoActivate)
	{
		StartSendingDMX();
	}
}

void ADMXControlConsoleActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	StopSendingDMX();
}

#if WITH_EDITOR
void ADMXControlConsoleActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ADMXControlConsoleActor, bSendDMXInEditor) &&
		DMXControlConsole)
	{
		DMXControlConsole->SetSendDMXInEditorEnabled(bSendDMXInEditor);
	}
}
#endif // WITH_EDITOR

#undef LOCTEXT_NAMESPACE
