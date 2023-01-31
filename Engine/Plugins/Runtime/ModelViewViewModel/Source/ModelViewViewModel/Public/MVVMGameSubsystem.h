// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/MVVMViewModelCollection.h"

#include "UObject/Package.h"
#include "MVVMGameSubsystem.generated.h"


/** */
UCLASS(DisplayName="Viewmodel Game Subsytem")
class MODELVIEWVIEWMODEL_API UMVVMGameSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin UEngineSubsystem interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End UEngineSubsystem interface

	UFUNCTION(BlueprintCallable, Category = "Viewmodel")
	UMVVMViewModelCollectionObject* GetGlobalViewModelCollection() const
	{
		return GlobalViewModelCollection;
	}
private:
	UPROPERTY(Transient)
	TObjectPtr<UMVVMViewModelCollectionObject> GlobalViewModelCollection;
};
