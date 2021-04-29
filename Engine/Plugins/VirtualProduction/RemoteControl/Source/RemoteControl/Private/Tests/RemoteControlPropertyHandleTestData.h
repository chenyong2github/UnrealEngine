// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "RemoteControlPropertyHandleTestData.generated.h"

UENUM()
enum class ERemoteControlEnumClass : uint8
{
	E_One,
	E_Two,
	E_Three
};

UENUM()
namespace ERemoteControlEnum
{
	enum Type
	{
		E_One,
		E_Two,
		E_Three
	};
}

USTRUCT()
struct FRemoteControlTestStructInnerSimle
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "RC")
	int32 Int32Value;
};

USTRUCT()
struct FRemoteControlTestStructInner
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "RC")
	int8 Int8Value;

	UPROPERTY(EditAnywhere, Category = "RC")
	FRemoteControlTestStructInnerSimle InnerSimle;

	UPROPERTY(EditAnywhere, Category = "RC")
	int32 Int32Value;

	FGuid Id = FGuid::NewGuid();

	friend bool operator==(const FRemoteControlTestStructInner& A, const FRemoteControlTestStructInner& B)
	{
		return A.Id == B.Id;
	}

	friend uint32 GetTypeHash(const FRemoteControlTestStructInner& Inner)
	{
		return GetTypeHash(Inner.Id);
	}
};

USTRUCT()
struct FRemoteControlTestStructOuter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "RC")
	int8 Int8Value;

	UPROPERTY(EditAnywhere, Category = "RC")
	TSet<FRemoteControlTestStructInner> StructInnerSet;

	UPROPERTY(EditAnywhere, Category = "RC")
	int32 Int32Value;

	UPROPERTY(EditAnywhere, Category = "RC")
	FRemoteControlTestStructInner RemoteControlTestStructInner;
};

UCLASS()
class URemoteControlAPITestObject : public UObject
{
public:
	GENERATED_BODY()

	URemoteControlAPITestObject()
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

	UPROPERTY(EditAnywhere, Category = "RC")
	int32 CStyleIntArray[3];

	UPROPERTY(EditAnywhere, Category = "RC")
	TArray<int32> IntArray;

	UPROPERTY(EditAnywhere, Category = "RC")
	TArray<FRemoteControlTestStructOuter> StructOuterArray;

	UPROPERTY(EditAnywhere, Category = "RC")
	TSet<int32> IntSet;

	UPROPERTY(EditAnywhere, Category = "RC")
	TMap<int32, int32> IntMap;

	UPROPERTY(EditAnywhere, Category = "RC")
	TMap<int32, FRemoteControlTestStructOuter> StructOuterMap;

	UPROPERTY(EditAnywhere, Category = "RC")
	TMap<FString, FColor> StringColorMap;

	UPROPERTY(EditAnywhere, Category = "RC")
	TArray<FVector> ArrayOfVectors;

	UPROPERTY(EditAnywhere, Category = "RC")
	int8 Int8Value;

	UPROPERTY(EditAnywhere, Category = "RC")
	int16 Int16Value;

	UPROPERTY(EditAnywhere, Category = "RC")
	int32 Int32Value;

	UPROPERTY(EditAnywhere, Category = "RC")
	float FloatValue;

	UPROPERTY(EditAnywhere, Category = "RC")
	double DoubleValue;

	UPROPERTY(EditAnywhere, Category = "RC")
	FRemoteControlTestStructOuter RemoteControlTestStructOuter;

	UPROPERTY(EditAnywhere, Category = "RC")
	FString StringValue;

	UPROPERTY(EditAnywhere, Category = "RC")
	FName NameValue;

	UPROPERTY(EditAnywhere, Category = "RC")
	FText TextValue;

	UPROPERTY(EditAnywhere, Category = "RC")
	bool bValue;

	UPROPERTY(EditAnywhere, Category = "RC")
	uint8 ByteValue;

	UPROPERTY(EditAnywhere, Category = "RC")
	TEnumAsByte<ERemoteControlEnum::Type> RemoteControlEnumByteValue;

	UPROPERTY(EditAnywhere, Category = "RC")
	ERemoteControlEnumClass RemoteControlEnumValue;

	UPROPERTY(EditAnywhere, Category = "RC")
	FVector VectorValue;

	UPROPERTY(EditAnywhere, Category = "RC")
	FRotator RotatorValue;
};
