// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DatasmithImporter.h"
#include "DatasmithScene.h"

#include "DataPrepContentConsumer.h"

#include "UObject/SoftObjectPath.h"

#include "DatasmithConsumer.generated.h"

class UDatasmithScene;
class ULevel;

UCLASS(Experimental, config = EditorSettings, HideCategories = (DatasmithConsumerInternal))
class DATASMITHIMPORTER_API UDatasmithConsumer : public UDataprepContentConsumer
{
	GENERATED_BODY()

public:
	UDatasmithConsumer()
		: DatasmithScene(nullptr)
		, PreviousCurrentLevel(nullptr)
	{
	}

	UPROPERTY( BlueprintReadOnly, Category = DatasmithConsumerInternal, DuplicateTransient )
	TSoftObjectPtr<UDatasmithScene> DatasmithScene;

	/** Stores the level used on the last call to UDatasmithConsumer::Run */
	UPROPERTY( BlueprintReadOnly, Category = DatasmithConsumerInternal )
	FString LastLevelName;

	// Begin UDataprepContentConsumer overrides
	virtual const FText& GetLabel() const override;
	virtual const FText& GetDescription() const override;
	virtual bool SetLevelName(const FString& InLevelName, FText& OutReason ) override;
	virtual bool SetTargetContentFolder(const FString& InTargetContentFolder, FText& OutReason) override;

protected:
	virtual bool Initialize() override;
	virtual bool Run() override;
	virtual void Reset() override;
	// End UDataprepContentConsumer overrides

private:
	/** Temporary code to work with UDataprepContentConsumer */
	bool BuildContexts( UWorld* ImportWorld );

	/** Returns the level associated to LevelName if it exists */
	ULevel* FindLevel( const FString& InLevelName );

	/** Move assets if destination package path has changed since last call to UDatasmithConsumer::Run */
	void UpdateScene();

	/** Move level if destination level's name has changed since last call to UDatasmithConsumer::Run */
	void MoveLevel();

	/** Set current level to what the user specified */
	void UpdateLevel();

private:
	TUniquePtr< FDatasmithImportContext > ImportContextPtr;
	TUniquePtr< FDataprepWorkReporter > ProgressTaskPtr;

	ULevel* PreviousCurrentLevel;
};