// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MassCommonFragments.h"
#include "MassTranslator.h"
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
struct FMassCharacterMovementCopyToMassTag : public FComponentTag
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
	virtual void Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) override;

	FLWComponentQuery EntityQuery;
};

USTRUCT()
struct FMassCharacterMovementCopyToActorTag : public FComponentTag
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
	virtual void Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) override;

	FLWComponentQuery EntityQuery;
};


USTRUCT()
struct FMassCharacterOrientationCopyToMassTag : public FComponentTag
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
	virtual void Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) override;

	FLWComponentQuery EntityQuery;
};

USTRUCT()
struct FMassCharacterOrientationCopyToActorTag : public FComponentTag
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
	virtual void Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) override;

	FLWComponentQuery EntityQuery;
};

UCLASS()
class MASSACTORS_API UMassFragmentInitializer_NavLocation : public UMassFragmentInitializer
{
	GENERATED_BODY()
public:
	UMassFragmentInitializer_NavLocation();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) override;

	FLWComponentQuery EntityQuery;
};

UCLASS()
class MASSACTORS_API UMassFragmentInitializer_Transform : public UMassFragmentInitializer
{
	GENERATED_BODY()

public:
	UMassFragmentInitializer_Transform();

protected:
	virtual void ConfigureQueries() override;
	virtual void Execute(UEntitySubsystem& EntitySubsystem, FLWComponentSystemExecutionContext& Context) override;

	FLWComponentQuery EntityQuery;
};