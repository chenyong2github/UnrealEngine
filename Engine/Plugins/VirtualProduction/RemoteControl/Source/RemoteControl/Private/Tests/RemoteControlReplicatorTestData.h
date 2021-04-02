// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "RemoteControlReplicatorTestData.generated.h"

USTRUCT()
struct FRemoteControlReplicatorCustomStruct
{
	GENERATED_BODY()

	static const int32 Int32ValueDefault = -1;

	UPROPERTY(EditAnywhere, Category = "RC")
	int32 Int32Value = Int32ValueDefault;
};

UCLASS()
class URemoteControlReplicatorTestObject : public UObject
{
public:
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "RC")
	FRemoteControlReplicatorCustomStruct CustomStruct;
};
