// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma  once

#include "CoreMinimal.h"
#include "RemoteControlMessages.generated.h"

UENUM()
enum class ERemoteControlEvent : uint8
{
	PreObjectPropertyChanged = 0,
	ObjectPropertyChanged,
	EventCount,
};

USTRUCT()
struct FRemoteControlObjectEventHookRequest
{
	GENERATED_BODY()

	FRemoteControlObjectEventHookRequest()
		: EventType(ERemoteControlEvent::EventCount)
	{}

	UPROPERTY()
	ERemoteControlEvent EventType;

	UPROPERTY()
	FString ObjectPath;

	UPROPERTY()
	FString PropertyName;
};