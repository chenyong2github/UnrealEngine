// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "EditorSubsystem.h"
#include "EditorUtilityWidget.h"
#include "Templates/SubclassOf.h"
#include "UI/VREditorFloatingUI.h"
#include "VPScoutingSubsystem.generated.h"


UENUM(BlueprintType)
enum class EVProdPanelIDs : uint8
{
	Main,
	Left,
	Right
};

UCLASS()
class UVPScoutingSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:

	UVPScoutingSubsystem();
	
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Subsystems can't have any Blueprint implementations, so we attach this class for any BP logic that we to provide. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Virtual Production")
	class AEditorUtilityActor* VProdHelper;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Virtual Production")
	float FlightSpeedCoeff;

	// @todo: Guard against user-created name collisions
	/** Open a widget UI in front of the user. Opens default VProd UI (defined via the 'Virtual Scouting User Interface' setting) if null. */
	UFUNCTION(BlueprintCallable, Category = "Virtual Production")
	void ToggleVRScoutingUI(UPARAM(ref) FVREditorFloatingUICreationContext& CreationContext);

	/** Check whether a widget UI is open*/
	UFUNCTION(BlueprintPure, Category = "Virtual Production")
	bool IsVRScoutingUIOpen(const FName& PanelID);

	UFUNCTION(BlueprintCallable, Category = "Virtual Production")
	static TArray<UVREditorInteractor*> GetActiveEditorVRControllers();

	UFUNCTION(BlueprintPure, Category = "Virtual Production")
	static const FName GetVProdPanelID(const EVProdPanelIDs Panel)
	{
		switch (Panel)
		{
			case EVProdPanelIDs::Main:
				return VProdPanelID;
			case EVProdPanelIDs::Right:
				return VProdPanelRightID;
			case EVProdPanelIDs::Left:
				return VProdPanelLeftID;
		}

		return VProdPanelID;
	};

	UFUNCTION(BlueprintPure, Category = "Virtual Production")
	static FString GetDirectorName();

	UFUNCTION(BlueprintPure, Category = "Virtual Production")
	static FString GetShowName();


private:
	UClass* EditorUtilityActorClass;
	
	// Static IDs when submitting open/close requests for the VProd main menu panels. VREditorUISystem uses FNames to manage its panels, so these should be used for consistency.	
	static const FName VProdPanelID;
	static const FName VProdPanelLeftID;
	static const FName VProdPanelRightID;
};
