// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNextInterfaceTypes.h"
#include "IAnimNextInterface.h"
#include "AnimNextInterfaceContext.h"
#include "AnimNextInterface_Float.generated.h"

UCLASS()
class UAnimNextInterfaceFloat : public UObject, public IAnimNextInterface
{
	GENERATED_BODY()

	ANIM_NEXT_INTERFACE_RETURN_TYPE(float)

	virtual bool GetDataImpl(const UE::AnimNext::FContext& Context) const override
	{
		checkf(false, TEXT("UAnimNextInterfaceFloat::GetDataImpl must be overridden"));
		return false;
	}
};

UCLASS()
class UAnimNextInterface_Float_Literal : public UAnimNextInterfaceFloat
{
	GENERATED_BODY()

	// IAnimNextInterface interface
	virtual bool GetDataImpl(const UE::AnimNext::FContext& Context) const final override
	{
		Context.SetResult(Value);
		return true;
	}
	
	UPROPERTY(EditAnywhere, Category = "Parameters")
	float Value;
};

UCLASS()
class UAnimNextInterface_Float_Multiply : public UAnimNextInterfaceFloat
{
	GENERATED_BODY()

	// IAnimNextInterface interface
	virtual bool GetDataImpl(const UE::AnimNext::FContext& Context) const final override;

	UPROPERTY(EditAnywhere, Category = "Parameters")
	TArray<TScriptInterface<IAnimNextInterface>> Inputs;
};

UCLASS()
class UAnimNextInterface_Float_InterpTo : public UAnimNextInterfaceFloat
{
	GENERATED_BODY()

	// IAnimNextInterface interface
	virtual bool GetDataImpl(const UE::AnimNext::FContext& Context) const final override;

	UPROPERTY(EditAnywhere, Category = "Parameters")
	TScriptInterface<IAnimNextInterface> Current;

	UPROPERTY(EditAnywhere, Category = "Parameters")
	TScriptInterface<IAnimNextInterface> Target;

	UPROPERTY(EditAnywhere, Category = "Parameters")
	TScriptInterface<IAnimNextInterface> Speed;
};

UCLASS()
class UAnimNextInterface_Float_DeltaTime : public UAnimNextInterfaceFloat
{
	GENERATED_BODY()

	// IAnimNextInterface interface
	virtual bool GetDataImpl(const UE::AnimNext::FContext& Context) const final override
	{
		Context.SetResult(Context.GetDeltaTime());
		return true;
	}
};

USTRUCT()
struct FAnimNextInterface_Float_SpringInterpState
{
	GENERATED_BODY()

	UPROPERTY()
	float Value = 0.0f;

	UPROPERTY()
	float ValueRate = 0.0f;
};

UCLASS()
class UAnimNextInterface_Float_SpringInterp : public UAnimNextInterfaceFloat
{
	GENERATED_BODY()

	// IAnimNextInterface interface
	virtual bool GetDataImpl(const UE::AnimNext::FContext& Context) const final override;

	UPROPERTY(EditAnywhere, Category = "Parameters")
	TScriptInterface<IAnimNextInterface> Target;

	UPROPERTY(EditAnywhere, Category = "Parameters")
	TScriptInterface<IAnimNextInterface> TargetRate;
	
	UPROPERTY(EditAnywhere, Category = "Parameters")
	TScriptInterface<IAnimNextInterface> SmoothingTime;
	
	UPROPERTY(EditAnywhere, Category = "Parameters")
	TScriptInterface<IAnimNextInterface> DampingRatio;
};