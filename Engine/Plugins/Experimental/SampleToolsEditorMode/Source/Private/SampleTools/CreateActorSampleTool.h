// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "InteractiveToolBuilder.h"
#include "BaseTools/SingleClickTool.h"
#include "CreateActorSampleTool.generated.h"


/**
 * Builder for UCreateActorSampleTool
 */
UCLASS()
class SAMPLETOOLSEDITORMODE_API UCreateActorSampleToolBuilder : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	IToolsContextAssetAPI* AssetAPI;

	UCreateActorSampleToolBuilder() 
	{
		AssetAPI = nullptr;
	}

	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;
};



/**
 * Settings UObject for UCreateActorSampleTool. This UClass inherits from UInteractiveToolPropertySet,
 * which provides an OnModified delegate that the Tool will listen to for changes in property values.
 */
UCLASS(Transient)
class SAMPLETOOLSEDITORMODE_API UCreateActorSampleToolProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:
	UCreateActorSampleToolProperties();

	/** Place actors on existing objects */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DisplayName = "Place On Objects"))
	bool PlaceOnObjects;

	/** Height of ground plane */
	UPROPERTY(EditAnywhere, Category = Options, meta = (DisplayName = "Ground Height", UIMin = "-1000.0", UIMax = "1000.0", ClampMin = "-1000000", ClampMax = "1000000.0"))
	float GroundHeight;
};




/**
 * UCreateActorSampleTool is an example Tool that drops an empty Actor at each position the user 
 * clicks left mouse button. The Actors are placed at the first ray intersection in the scene,
 * or on a ground plane if no scene objects are hit. All the action is in the ::OnClicked handler.
 */
UCLASS()
class SAMPLETOOLSEDITORMODE_API UCreateActorSampleTool : public USingleClickTool
{
	GENERATED_BODY()

public:
	UCreateActorSampleTool();

	virtual void SetWorld(UWorld* World);
	virtual void SetAssetAPI(IToolsContextAssetAPI* AssetAPI);

	virtual void Setup() override;

	virtual void OnClicked(const FInputDeviceRay& ClickPos);


protected:
	UPROPERTY()
	UCreateActorSampleToolProperties* Properties;


protected:
	/** target World we will raycast into and create Actor in */
	UWorld* TargetWorld;

	/** Access to the ToolContext's Asset Creation API. This is not currently used, but can be used to (eg) add Components, etc*/
	IToolsContextAssetAPI* AssetAPI;
};