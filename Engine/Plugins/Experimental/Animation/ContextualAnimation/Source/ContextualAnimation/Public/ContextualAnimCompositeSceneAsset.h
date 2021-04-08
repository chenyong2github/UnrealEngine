// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ContextualAnimSceneAssetBase.h"
#include "ContextualAnimCompositeSceneAsset.generated.h"

// DEPRECATED will be removed shortly
UCLASS()
class CONTEXTUALANIMATION_API UContextualAnimCompositeSceneAsset : public UContextualAnimSceneAssetBase
{
	GENERATED_BODY()

public:

	static const FName InteractorRoleName;
	static const FName InteractableRoleName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animations")
	FContextualAnimCompositeTrack InteractorTrack;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animations")
	FContextualAnimTrack InteractableTrack;

	UContextualAnimCompositeSceneAsset(const FObjectInitializer& ObjectInitializer);

	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;

	virtual const FContextualAnimTrackSettings* GetTrackSettings(const FName& Role) const override;

	virtual const FContextualAnimData* GetAnimDataForRoleAtIndex(const FName& Role, int32 Index) const override;

	virtual void ForEachAnimData(FForEachAnimDataFunction Function) const override;

	//@TODO: Temp until have a roles asset
	virtual TArray<FName> GetRoles() const override;

	virtual bool Query(const FName& Role, FContextualAnimQueryResult& OutResult, const FContextualAnimQueryParams& QueryParams, const FTransform& ToWorldTransform) const override;

	bool QueryData(FContextualAnimQueryResult& OutResult, const FContextualAnimQueryParams& QueryParams, const FTransform& ToWorldTransform) const;
};
