// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DataPrepContentConsumer.h"

#include "DataprepLevelWriter.generated.h"

UCLASS(Experimental)
class DATAPREPEDITOR_API UDataprepLevelWriter : public UDataprepContentConsumer
{
	GENERATED_BODY()

public:
	// Begin UDataprepContentConsumer overrides
	virtual bool Run() override;
	virtual const FText& GetLabel() const override;
	virtual const FText& GetDescription() const override;
	// End UDataprepContentConsumer overrides

	UPROPERTY()
	FString MapFile;
};