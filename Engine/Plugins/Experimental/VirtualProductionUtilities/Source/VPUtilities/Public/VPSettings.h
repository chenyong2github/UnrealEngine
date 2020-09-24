// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"
#include "Engine/DeveloperSettings.h"
#include "VPSettings.generated.h"

/**
 * 
 */
UCLASS(config=Game)
class VPUTILITIES_API UVPSettings : public UObject
{
	GENERATED_BODY()

public:
	UVPSettings();

protected:
	/**
	 * The machine role(s) in a virtual production context.
	 * @note The role may be override via the command line, "-VPRole=[Role.SubRole1|Role.SubRole2]"
	 */
	UPROPERTY(config, EditAnywhere, Category="Virtual Production")
	FGameplayTagContainer Roles;

	/**
	 * The machine role(s) in a virtual production context read from the command line.
	 * ie. "-VPRole=[Role.SubRole1|Role.SubRole2]"
	 */
	bool bIsCommandLineRolesValid;
	UPROPERTY(transient)
	FGameplayTagContainer CommandLineRoles;

public:
	const FGameplayTagContainer& GetRoles() const { return bIsCommandLineRolesValid ? CommandLineRoles : Roles; }

	/** Default Kit of Focal Lengths for Virtual Camera */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, Category = "VirtualCamera|Presets")
	TArray<float> FocalLengthPresets = { 18.0,21.0,25.0,32.0,40.0,50.0,65.0,75.0,100.0,135.0 };

	/** Default Apertures for for Virtual Camera  */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, Category = "VirtualCamera|Presets")
	TArray<float> AperturePresets = { 1.0,1.4,2.0,2.8,4.0,5.6,8.0,11.0,16.0,22.0 };

	/** Default Shutter Speeds (1/s) for Virtual Camera*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, Category = "VirtualCamera|Presets")
	TArray<float> DefaultShutterSpeedPresets = { 1.0,4.0,8.0,15.0,30.0,60.0,125.0,250.0,500.0,1000.0 };

	/** Default ISOs for Virtual Camera*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, config, Category = "VirtualCamera|Presets")
	TArray<float> DefaultISOPresets = { 50,100,200,400,800,1600,3200,6400 };

	UFUNCTION(BlueprintPure, Category = "Virtual Production")
	static UVPSettings* GetVPSettings();

#if WITH_EDITORONLY_DATA
	/** When enabled, the virtual production role(s) will be displayed in the main editor UI. */
	UPROPERTY(config, EditAnywhere, Category="Virtual Production")
	bool bShowRoleInEditor;

	/** Notify when the virtual production roles have changed. */
	FSimpleMulticastDelegate OnRolesChanged;

	UPROPERTY(config, EditAnywhere, Category = "Virtual Production", DisplayName = "Director Name")
	FString DirectorName;

	UPROPERTY(config, EditAnywhere, Category = "Virtual Production", DisplayName = "Project Name")
	FString ShowName;

#endif

public:
#if WITH_EDITOR
	//~ UObject interface
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;
	//~ End UObject interface


#endif //WITH_EDITOR

};