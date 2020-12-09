// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "FractureTool.h"

#include "FractureToolProperties.generated.h"


UENUM(BlueprintType)
enum class EDynamicStateOverrideEnum : uint8
{
	NoOverride = 0										  UMETA(DisplayName = "No Override"),
	Sleeping = 1 /*Chaos::EObjectStateType::Sleeping*/    UMETA(DisplayName = "Sleeping"),
	Kinematic = 2 /*Chaos::EObjectStateType::Kinematic*/  UMETA(DisplayName = "Kinematic"),
	Static = 3    /*Chaos::EObjectStateType::Static*/     UMETA(DisplayName = "Static")
};

/** Settings specifically related to the one-time destructive fracturing of a mesh **/
UCLASS(config = EditorPerProjectUserSettings)
class UFractureInitialDynamicStateSettings : public UFractureToolSettings
{
public:

	GENERATED_BODY()

	UFractureInitialDynamicStateSettings(const FObjectInitializer& ObjInit)
		: Super(ObjInit)
		, InitialDynamicState(EDynamicStateOverrideEnum::Kinematic)
	{}

	/** Random number generator seed for repeatability */
	UPROPERTY(EditAnywhere, Category = SetInitialDynamicState, meta = (DisplayName = "Initial Dynamic State"))
	EDynamicStateOverrideEnum InitialDynamicState;

};


UCLASS(DisplayName = "State", Category = "FractureTools")
class UFractureToolSetInitialDynamicState : public UFractureModalTool
{
public:
	GENERATED_BODY()

	UFractureToolSetInitialDynamicState(const FObjectInitializer& ObjInit);

	using FGeometryCollectionPtr = TSharedPtr<FGeometryCollection, ESPMode::ThreadSafe>;

	// UFractureTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FText GetApplyText() const override;
	virtual FSlateIcon GetToolIcon() const override;
	virtual TArray<UObject*> GetSettingsObjects() const override;

	virtual void RegisterUICommand(FFractureEditorCommands* BindingContext) override;

	virtual void Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit) override;

	UPROPERTY(EditAnywhere, Category = InitialDynamicState)
	UFractureInitialDynamicStateSettings* StateSettings;

};
