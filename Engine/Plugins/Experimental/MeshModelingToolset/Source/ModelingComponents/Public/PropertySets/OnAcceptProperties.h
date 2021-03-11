// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InteractiveToolBuilder.h"

#include "OnAcceptProperties.generated.h"


class UInteractiveToolManager;
class AActor;


/** Options to handle source meshes */
UENUM()
enum class EHandleSourcesMethod : uint8
{
	DeleteSources = 0	UMETA(DisplayName = "Delete Sources"),

	HideSources = 1		UMETA(DisplayName = "Hide Sources"),

	KeepSources = 2		UMETA(DisplayName = "Keep Sources"),

	/** Delete all but the first source */
	KeepFirstSource = 3	UMETA(DisplayName = "Keep First Source"),

	/** Delete all but the last source */
	KeepLastSource = 4	UMETA(DisplayName = "Keep Last Source")
};




// Standard property settings for tools that create a new actor and need to decide what to do with the input (source) actor(s)
UCLASS()
class MODELINGCOMPONENTS_API UOnAcceptHandleSourcesProperties : public UInteractiveToolPropertySet
{
	GENERATED_BODY()
public:

	/** What to do with the source Actors/Components when accepting results of tool.*/
	UPROPERTY(EditAnywhere, Category = ToolOutputOptions)
	EHandleSourcesMethod OnToolAccept;

	void ApplyMethod(const TArray<AActor*>& Actors, UInteractiveToolManager* ToolManager);
};
