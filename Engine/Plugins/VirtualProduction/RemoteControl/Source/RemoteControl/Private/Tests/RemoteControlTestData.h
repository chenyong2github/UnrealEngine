// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "RemoteControlTestData.generated.h"

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
	TMap<FString, FColor> StringColorMap;
};
