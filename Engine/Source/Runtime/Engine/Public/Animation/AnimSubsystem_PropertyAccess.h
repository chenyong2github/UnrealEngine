// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimSubsystem.h"
#include "PropertyAccess.h"
#include "AnimSubsystem_PropertyAccess.generated.h"

USTRUCT()
struct ENGINE_API FAnimSubsystem_PropertyAccess : public FAnimSubsystem
{
	GENERATED_BODY()

	friend class UAnimBlueprintExtension_PropertyAccess;

	/** FAnimSubsystem interface */
	virtual void OnUpdate(FAnimSubsystemUpdateContext& InContext) const override;
	virtual void OnParallelUpdate(FAnimSubsystemParallelUpdateContext& InContext) const override;
	virtual void OnPostLoad(FAnimSubsystemPostLoadContext& InContext) override;

	/** Access the property access library */
	const FPropertyAccessLibrary& GetLibrary() const { return Library; }

private:
	UPROPERTY()
	FPropertyAccessLibrary Library;
};