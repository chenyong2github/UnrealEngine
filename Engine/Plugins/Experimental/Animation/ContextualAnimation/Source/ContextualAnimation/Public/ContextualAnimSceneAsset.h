// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ContextualAnimSceneAssetBase.h"
#include "ContextualAnimSceneAsset.generated.h"

UCLASS()
class CONTEXTUALANIMATION_API UContextualAnimSceneAsset : public UContextualAnimSceneAssetBase
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animations")
	TMap<FName, FContextualAnimTrack> DataContainer;

	UContextualAnimSceneAsset(const FObjectInitializer& ObjectInitializer);

	PRAGMA_DISABLE_DEPRECATION_WARNINGS // Suppress compiler warning on override of deprecated function
	UE_DEPRECATED(5.0, "Use version that takes FObjectPreSaveContext instead.")
	virtual void PreSave(const class ITargetPlatform* TargetPlatform) override;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;

	virtual UClass* GetPreviewActorClassForRole(const FName& Role) const override;
	
	virtual EContextualAnimJoinRule GetJoinRuleForRole(const FName& Role) const override;
	
	const FContextualAnimTrack* FindTrack(const FName& Role) const;
};
