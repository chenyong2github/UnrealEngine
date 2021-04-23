// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "RemoteControlTestData.generated.h"

USTRUCT()
struct FRemoteControlTestInnerStruct
{
	GENERATED_BODY()

	FRemoteControlTestInnerStruct() {}

	FRemoteControlTestInnerStruct(uint8 Index)
		: Color(FColor(Index, Index, Index, Index))
	{}

	UPROPERTY()
	FColor Color = FColor(1,2,3,4 );
};

UCLASS()
class URemoteControlTestObject : public UObject
{
public:
	GENERATED_BODY()

	URemoteControlTestObject()
	{
		for (int8 i = 0; i < 3; i++)
		{
			CStyleIntArray[i] = i+1;
			IntArray.Add(i+1);
			IntSet.Add(i+1);
			IntMap.Add(i, i+1);
			IntInnerStructMap.Add((int32)i, FRemoteControlTestInnerStruct((uint8)i));
		}

		StringColorMap.Add(TEXT("mykey"), FColor{1,2,3,4});
	}

	UPROPERTY()
	int32 CStyleIntArray[3];

	UPROPERTY()
	TArray<int32> IntArray;

	UPROPERTY()
	TSet<int32> IntSet;

	UPROPERTY()
	TMap<int32, int32> IntMap;

	UPROPERTY()
	TMap<int32, FRemoteControlTestInnerStruct> IntInnerStructMap;

	UPROPERTY()
	TMap<FString, FColor> StringColorMap;
};
