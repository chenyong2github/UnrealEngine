// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PoseSearchDebuggerReflection.generated.h"

/**
 * Reflection UObject being observed in the details view panel of the debugger
 */
UCLASS()
class POSESEARCHEDITOR_API UPoseSearchDebuggerReflection : public UObject
{
	GENERATED_BODY()

public:
	/** Time since last PoseSearch */
	UPROPERTY(VisibleAnywhere, Category="Motion Matching State")
	float ElapsedPoseSearchTime = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "Motion Matching State")
	float AssetPlayerTime = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "Motion Matching State")
	float LastDeltaTime = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "Motion Matching State")
	float SimLinearVelocity;

	UPROPERTY(VisibleAnywhere, Category = "Motion Matching State")
	float SimAngularVelocity;

	UPROPERTY(VisibleAnywhere, Category = "Motion Matching State")
	float AnimLinearVelocity;

	UPROPERTY(VisibleAnywhere, Category = "Motion Matching State")
	float AnimAngularVelocity;

    UPROPERTY(VisibleAnywhere, Category="Pose Vectors")
	TArray<float> QueryPoseVector;
    	
    UPROPERTY(VisibleAnywhere, Category="Pose Vectors")
	TArray<float> ActivePoseVector;

	UPROPERTY(VisibleAnywhere, Category="Pose Vectors")
	TArray<float> SelectedPoseVector;

	UPROPERTY(VisibleAnywhere, Category="Pose Vectors")
	TArray<float> CostVector;
};
