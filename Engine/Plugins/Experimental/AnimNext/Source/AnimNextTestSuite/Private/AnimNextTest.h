// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interface/InterfaceTypes.h"
#include "Interface/IAnimNextInterface.h"

#include "AnimNextTest.generated.h"

//// --- Raw type ---
USTRUCT()
struct FAnimNextTestData
{
	GENERATED_BODY()

	float A = 0.f;
	float B = 0.f;
};

// -- Defines the TestData as an interface that will be used by any variant operating on TestData --
UCLASS()
class UAnimNextInterface_TestData : public UObject, public IAnimNextInterface
{
	GENERATED_BODY()

	ANIM_NEXT_INTERFACE_RETURN_TYPE(FAnimNextTestData)

	virtual bool GetDataImpl(const UE::AnimNext::FContext& Context) const override
	{
		checkf(false, TEXT("UAnimNextInterfaceFloat::GetDataImpl must be overridden"));
		return false;
	}
};

// --- Constant user defined value ---
UCLASS()
class UAnimNextInterfaceTestDataLiteral : public UAnimNextInterface_TestData
{
	GENERATED_BODY()

	// IAnimNextInterface interface
	virtual bool GetDataImpl(const UE::AnimNext::FContext& Context) const final override;

	FAnimNextTestData Value = {1.f, 1.f};
};

// --- TestData multiply operator  ---
UCLASS()
class UAnimNextInterface_TestData_Multiply : public UAnimNextInterface_TestData
{
	GENERATED_BODY()

	// IAnimNextInterface interface
	virtual bool GetDataImpl(const UE::AnimNext::FContext& Context) const final override;

public:
	static FName NameParamA;
	static FName NameParamB;
	static FName NameParamResult;
};

// --- TestData split operator : creates two outputs from a single input (multi output test) ---
UCLASS()
class UAnimNextInterface_TestData_Split : public UAnimNextInterface_TestData
{
	GENERATED_BODY()

	// IAnimNextInterface interface
	virtual bool GetDataImpl(const UE::AnimNext::FContext& Context) const final override;

public:
	static FName InputName_AB;
	static FName OutputName_A;
	static FName OutputName_B;
};
