// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"

#include "StatusBarSubsystem.generated.h"

class SStatusBar;

UCLASS()
class UNREALED_API UStatusBarSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:

	/**
	 *	Prepares for use
	 */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override final;

	/**
	 *	Internal cleanup
	 */
	virtual void Deinitialize() override final;

	/** 
	 * Creates a new instance of a status bar widget
	 *
	 * @param StatusBarName	The name of the status bar for updating it later.
	 */
	TSharedRef<SWidget> MakeStatusBarWidget(FName StatusBarName);
private:
	TMap<FName, TWeakPtr<SStatusBar>> StatusBars;
};
