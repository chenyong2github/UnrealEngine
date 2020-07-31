// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/Subsystem.h"
#include "Subsystems/SubsystemCollection.h"
#include "AnimBlueprintClassSubsystem.generated.h"

class UAnimInstance;
class UAnimBlueprintGeneratedClass;
struct FAnimInstanceSubsystemData;
struct FAnimInstanceProxy;

UCLASS()
class ENGINE_API UAnimBlueprintClassSubsystem : public UObject
{
	GENERATED_BODY()

public:
	/** Override point to process game-thread data per-frame */
	virtual void OnUpdateAnimation(UAnimInstance* InAnimInstance, FAnimInstanceSubsystemData& InSubsystemData, float InDeltaTime) {}

	/** Override point to process worker-thread data per-frame */
	virtual void OnParallelUpdateAnimation(FAnimInstanceProxy& InProxy, FAnimInstanceSubsystemData& InSubsystemData, float InDeltaTime) {}

	/** Override point for nativized and BP anim BPs to call to perform subsystem initialization post-load/post-initialization */
	virtual void PostLoadSubsystem() {}

	/** 
	 * Get the structure that will be added to any BP-derived UAnimInstance (must be derived from FAnimInstanceSubsystemData and non-null) 
	 * Value must be non-null because we add an 'empty' struct for subsystems that dont require per-instance data for simplicity and 
	 * better performance accessing subsystem data.
	 */
	virtual const UScriptStruct* GetInstanceDataType() const;

private:
	// UObject interface
	virtual void PostLoad() override;
};
