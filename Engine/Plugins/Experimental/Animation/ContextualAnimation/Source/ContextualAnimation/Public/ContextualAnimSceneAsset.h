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
	TMap<FName, FContextualAnimCompositeTrack> DataContainer;

	UContextualAnimSceneAsset(const FObjectInitializer& ObjectInitializer);

	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;

	virtual const FContextualAnimTrackSettings* GetTrackSettings(const FName& Role) const override;
	
	virtual const FContextualAnimData* GetAnimDataForRoleAtIndex(const FName& Role, int32 Index) const override;

	virtual void ForEachAnimData(FForEachAnimDataFunction Function) const override;

	virtual bool Query(const FName& Role, FContextualAnimQueryResult& OutResult, const FContextualAnimQueryParams& QueryParams, const FTransform& ToWorldTransform) const override;

	//@TODO: Temp until have a roles asset
	virtual TArray<FName> GetRoles() const override;
};
