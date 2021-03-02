// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnhancedInputLibrary.h"

#include "Engine/Engine.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputModule.h"
#include "EnhancedPlayerInput.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "UObject/UObjectIterator.h"

void UEnhancedInputLibrary::ForEachSubsystem(TFunctionRef<void(IEnhancedInputSubsystemInterface*)> SubsystemPredicate)
{
	// Engine
	SubsystemPredicate(Cast<IEnhancedInputSubsystemInterface>(GEngine->GetEngineSubsystem<UEnhancedInputEngineSubsystem>()));

	// Players
	for (TObjectIterator<UEnhancedInputLocalPlayerSubsystem> It; It; ++It)
	{
		SubsystemPredicate(Cast<IEnhancedInputSubsystemInterface>(*It));
	}
}

void UEnhancedInputLibrary::RequestRebuildControlMappingsUsingContext(const UInputMappingContext* Context, bool bForceImmediately)
{
	ForEachSubsystem([Context, bForceImmediately](IEnhancedInputSubsystemInterface* Subsystem) 
		{
			check(Subsystem);
			if (Subsystem && Subsystem->HasMappingContext(Context))
			{
				Subsystem->RequestRebuildControlMappings(bForceImmediately);
			}
		});
}

FInputActionValue UEnhancedInputLibrary::GetBoundActionValue(AActor* Actor, const UInputAction* Action)
{
	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(Actor->InputComponent);
	return EIC ? EIC->GetBoundActionValue(Action) : FInputActionValue(Action->ValueType, FVector::ZeroVector);
}


void UEnhancedInputLibrary::BreakInputActionValue(FInputActionValue InActionValue, float& X, float& Y, float& Z)
{
	FVector AsAxis3D = InActionValue.Get<FInputActionValue::Axis3D>();
	X = AsAxis3D.X;
	Y = AsAxis3D.Y;
	Z = AsAxis3D.Z;
}

FInputActionValue UEnhancedInputLibrary::MakeInputActionValue(float X, float Y, float Z, const FInputActionValue& MatchValueType)
{
	return FInputActionValue(MatchValueType.GetValueType(), FVector(X, Y, Z));
}

// FInputActionValue type conversions

bool UEnhancedInputLibrary::Conv_InputActionValueToBool(FInputActionValue InValue)
{
	return InValue.Get<bool>();
}

float UEnhancedInputLibrary::Conv_InputActionValueToAxis1D(FInputActionValue InValue)
{
	return InValue.Get<FInputActionValue::Axis1D>();
}

FVector2D UEnhancedInputLibrary::Conv_InputActionValueToAxis2D(FInputActionValue InValue)
{
	return InValue.Get<FInputActionValue::Axis2D>();
}

FVector UEnhancedInputLibrary::Conv_InputActionValueToAxis3D(FInputActionValue InValue)
{
	return InValue.Get<FInputActionValue::Axis3D>();
}
