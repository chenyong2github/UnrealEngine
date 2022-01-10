// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassCommonFragments.h"
#include "MassTranslator.h"
#include "MassObserverProcessor.h"
#include "MassCharacterMovementTranslators.generated.h"

class UCharacterMovementComponent;
struct FDataFragment_CharacterMovementComponentWrapper;
struct FNavLocationComponent;
struct FDataFragment_Transform;
struct FMassVelocityFragment;

USTRUCT()
struct FDataFragment_CharacterMovementComponentWrapper : public FDataFragment_ObjectWrapper
{
	GENERATED_BODY()
	TWeakObjectPtr<UCharacterMovementComponent> Component;
};

USTRUCT()
struct FMassCharacterMovementCopyToMassTag : public FMassTag
{
	GENERATED_BODY()
};

UCLASS()
class MASSACTORS_API UMassCharacterMovementToMassTranslator : public UMassTranslator
{
	GENERATED_BODY()

public:
	UMassCharacterMovementToMassTranslator();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};

USTRUCT()
struct FMassCharacterMovementCopyToActorTag : public FMassTag
{
	GENERATED_BODY()
};


UCLASS()
class MASSACTORS_API UMassCharacterMovementToActorTranslator : public UMassTranslator
{
	GENERATED_BODY()

public:
	UMassCharacterMovementToActorTranslator();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};


USTRUCT()
struct FMassCharacterOrientationCopyToMassTag : public FMassTag
{
	GENERATED_BODY()
};

UCLASS()
class MASSACTORS_API UMassCharacterOrientationToMassTranslator : public UMassTranslator
{
	GENERATED_BODY()

public:
	UMassCharacterOrientationToMassTranslator();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};

USTRUCT()
struct FMassCharacterOrientationCopyToActorTag : public FMassTag
{
	GENERATED_BODY()
};


UCLASS()
class MASSACTORS_API UMassCharacterOrientationToActorTranslator : public UMassTranslator
{
	GENERATED_BODY()

public:
	UMassCharacterOrientationToActorTranslator();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};

UCLASS()
class MASSACTORS_API UMassFragmentInitializer_NavLocation : public UMassFragmentInitializer
{
	GENERATED_BODY()
public:
	UMassFragmentInitializer_NavLocation();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(UMassEntitySubsystem& EntitySubsystem, FMassExecutionContext& Context) override;

	FMassEntityQuery EntityQuery;
};
