// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ContextualAnimSceneInstance.h"
#include "ContextualAnimScenePivotProvider.generated.h"

class UContextualAnimSceneAsset;

UCLASS(Abstract, BlueprintType, EditInlineNew)
class CONTEXTUALANIMATION_API UContextualAnimScenePivotProvider : public UObject
{
	GENERATED_BODY()

public:

	UContextualAnimScenePivotProvider(const FObjectInitializer& ObjectInitializer);

	virtual FTransform CalculateScenePivot_Source(int32 AnimDataIndex) const { return FTransform::Identity; }
	virtual FTransform CalculateScenePivot_Runtime(const TMap<FName, FContextualAnimSceneActorData>& SceneActorMap) const { return FTransform::Identity; }

	const UContextualAnimSceneAsset* GetSceneAsset() const;
};

UCLASS()
class CONTEXTUALANIMATION_API UContextualAnimScenePivotProvider_Default : public UContextualAnimScenePivotProvider
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FName PrimaryRole;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FName SecondaryRole;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	float Weight = 0.5f;

	UContextualAnimScenePivotProvider_Default(const FObjectInitializer& ObjectInitializer);

	virtual FTransform CalculateScenePivot_Source(int32 AnimDataIndex) const override;
	virtual FTransform CalculateScenePivot_Runtime(const TMap<FName, FContextualAnimSceneActorData>& SceneActorMap) const override;
};

UCLASS()
class CONTEXTUALANIMATION_API UContextualAnimScenePivotProvider_RelativeTo : public UContextualAnimScenePivotProvider
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings")
	FName RelativeToRole;

	UContextualAnimScenePivotProvider_RelativeTo(const FObjectInitializer& ObjectInitializer);

	virtual FTransform CalculateScenePivot_Source(int32 AnimDataIndex) const override;
	virtual FTransform CalculateScenePivot_Runtime(const TMap<FName, FContextualAnimSceneActorData>& SceneActorMap) const override;
};