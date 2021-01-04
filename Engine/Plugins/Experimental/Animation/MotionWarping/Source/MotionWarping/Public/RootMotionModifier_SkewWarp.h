// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RootMotionModifier.h"
#include "RootMotionModifier_SkewWarp.generated.h"

USTRUCT()
struct FRootMotionModifier_SkewWarp : public FRootMotionModifier_Warp
{
	GENERATED_BODY()

public:

	FRootMotionModifier_SkewWarp(){}
	virtual ~FRootMotionModifier_SkewWarp() {}

	virtual UScriptStruct* GetScriptStruct() const { return FRootMotionModifier_SkewWarp::StaticStruct(); }
	virtual FTransform ProcessRootMotion(UMotionWarpingComponent& OwnerComp, const FTransform& InRootMotion, float DeltaSeconds) override;
};

UCLASS(meta = (DisplayName = "Skew Warp"))
class URootMotionModifierConfig_SkewWarp : public URootMotionModifierConfig_Warp
{
	GENERATED_BODY()

public:

	URootMotionModifierConfig_SkewWarp(const FObjectInitializer& ObjectInitializer)
		: Super(ObjectInitializer) {}

	virtual TUniquePtr<FRootMotionModifier> CreateRootMotionModifier(const UAnimSequenceBase* Animation, float StartTime, float EndTime) const override
	{
		TUniquePtr<FRootMotionModifier_SkewWarp> Modifier = MakeUnique<FRootMotionModifier_SkewWarp>();
		Modifier->Animation = Animation;
		Modifier->StartTime = StartTime;
		Modifier->EndTime = EndTime;
		Modifier->SyncPointName = SyncPointName;
		Modifier->bWarpTranslation = bWarpTranslation;
		Modifier->bIgnoreZAxis = bIgnoreZAxis;
		Modifier->bWarpRotation = bWarpRotation;
		return Modifier;
	}
};