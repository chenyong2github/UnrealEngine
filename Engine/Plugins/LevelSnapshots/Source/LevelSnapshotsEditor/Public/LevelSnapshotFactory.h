// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "LevelSnapshotFactory.generated.h"

UCLASS()
class LEVELSNAPSHOTSEDITOR_API ULevelSnapshotFactory : public UFactory
{
	GENERATED_BODY()

public:

	ULevelSnapshotFactory();
	
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	virtual bool ShouldShowInNewMenu() const override;
};