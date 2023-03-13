// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IAnimNextInterface.h"
#include "AnimNextInterface_Wrapper.generated.h"

UCLASS()
class UAnimNextInterface_Wrapper : public UObject, public IAnimNextInterface
{
	GENERATED_BODY()

private:
	// IAnimAnimNextInterface interface
	virtual UE::AnimNext::FParamTypeHandle GetReturnTypeHandleImpl() const final override;
	virtual bool GetDataImpl(const UE::AnimNext::FContext& Context) const final override;

//	UPROPERTY(EditAnywhere, Category = "Parameters")
//	TArray<FAnimNextInterfaceParameter> Inputs;

	UPROPERTY(EditAnywhere, Category = "Parameters")
	TScriptInterface<IAnimNextInterface> Output;
};