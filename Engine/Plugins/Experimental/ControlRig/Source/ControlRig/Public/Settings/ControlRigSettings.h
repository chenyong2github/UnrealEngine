// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ControlRigSettings.h: Declares the ControlRigSettings class.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ControlRigGizmoLibrary.h"

#if WITH_EDITOR
#include "RigVMModel/RigVMGraph.h"
#endif

#include "ControlRigSettings.generated.h"

class UStaticMesh;

USTRUCT()
struct FControlRigSettingsPerPinBool
{
	GENERATED_BODY();

	UPROPERTY(EditAnywhere, Category = Settings)
	TMap<FString, bool> Values;
};

/**
 * Default ControlRig settings.
 */
UCLASS(config=Engine, defaultconfig, meta=(DisplayName="Control Rig"))
class CONTROLRIG_API UControlRigSettings : public UDeveloperSettings
{
	GENERATED_UCLASS_BODY()

public:

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, config, Category = DefaultGizmo)
	TSoftObjectPtr<UControlRigGizmoLibrary> DefaultGizmoLibrary;

	// When this is checked all controls will return to their initial
	// value as the user hits the Compile button.
	UPROPERTY(EditAnywhere, config, Category = Interaction)
	bool bResetControlsOnCompile;

	// When this is checked all controls will return to their initial
	// value as the user interacts with a pin value
	UPROPERTY(EditAnywhere, config, Category = Interaction)
	bool bResetControlsOnPinValueInteraction;

	/**
	 * When checked controls will be reset during a manual compilation
	 * (when pressing the Compile button)
	 */
	UPROPERTY(EditAnywhere, config, Category = Compilation)
	bool bResetControlTransformsOnCompile;

	/**
	 * A map which remembers the expansion setting for each rig unit pin.
	 */
	UPROPERTY(EditAnywhere, config, Category = NodeGraph)
	TMap<FString, FControlRigSettingsPerPinBool> RigUnitPinExpansion;
	
	/**
	 * The border color of the viewport when entering "Setup Event" mode
	 */
	UPROPERTY(EditAnywhere, config, Category = Viewport)
	FLinearColor SetupEventBorderColor;
	
	/**
	 * The border color of the viewport when entering "Backwards Solve" mode
	 */
	UPROPERTY(EditAnywhere, config, Category = Viewport)
	FLinearColor BackwardsSolveBorderColor;
	
	/**
	 * The border color of the viewport when entering "Backwards And Forwards" mode
	 */
	UPROPERTY(EditAnywhere, config, Category = Viewport)
	FLinearColor BackwardsAndForwardsBorderColor;

	/**
	* The default node snippet to create when pressing 1 + Left Mouse Button
	*/
	UPROPERTY(EditAnywhere, config, Category = Snippets, meta = (DisplayName = "1"))
	FString NodeSnippet_1;

	/**
	* The default node snippet to create when pressing 2 + Left Mouse Button
	*/
	UPROPERTY(EditAnywhere, config, Category = Snippets, meta = (DisplayName = "2"))
	FString NodeSnippet_2;

	/**
	* The default node snippet to create when pressing 3 + Left Mouse Button
	*/
	UPROPERTY(EditAnywhere, config, Category = Snippets, meta = (DisplayName = "3"))
	FString NodeSnippet_3;

	/**
	* The default node snippet to create when pressing 4 + Left Mouse Button
	*/
	UPROPERTY(EditAnywhere, config, Category = Snippets, meta = (DisplayName = "4"))
	FString NodeSnippet_4;

	/**
	* The default node snippet to create when pressing 5 + Left Mouse Button
	*/
	UPROPERTY(EditAnywhere, config, Category = Snippets, meta = (DisplayName = "5"))
	FString NodeSnippet_5;

	/**
	* The default node snippet to create when pressing 6 + Left Mouse Button
	*/
	UPROPERTY(EditAnywhere, config, Category = Snippets, meta = (DisplayName = "6"))
	FString NodeSnippet_6;

	/**
	* The default node snippet to create when pressing 7 + Left Mouse Button
	*/
	UPROPERTY(EditAnywhere, config, Category = Snippets, meta = (DisplayName = "7"))
	FString NodeSnippet_7;

	/**
	* The default node snippet to create when pressing 8 + Left Mouse Button
	*/
	UPROPERTY(EditAnywhere, config, Category = Snippets, meta = (DisplayName = "8"))
	FString NodeSnippet_8;

	/**
	* The default node snippet to create when pressing 9 + Left Mouse Button
	*/
	UPROPERTY(EditAnywhere, config, Category = Snippets, meta = (DisplayName = "9"))
	FString NodeSnippet_9;

	/**
	* The default node snippet to create when pressing 0 + Left Mouse Button
	*/
	UPROPERTY(EditAnywhere, config, Category = Snippets, meta = (DisplayName = "0"))
	FString NodeSnippet_0;

#endif

	static UControlRigSettings * Get() { return GetMutableDefault<UControlRigSettings>(); }

#if WITH_EDITOR

private:
	static FString GetSnippetContentForUnitNode(UScriptStruct* InUnitNodeStruct);

#endif
};
