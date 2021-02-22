// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ContextualAnimSceneAssetBase.h"
#include "ContextualAnimCompositeSceneAsset.generated.h"

/** Stores the result of a query function */
USTRUCT(BlueprintType)
struct FContextualAnimQueryResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Defaults")
	TWeakObjectPtr<UAnimMontage> Animation;

	UPROPERTY(BlueprintReadWrite, Category = "Defaults")
	FTransform EntryTransform;

	UPROPERTY(BlueprintReadWrite, Category = "Defaults")
	FTransform SyncTransform;

	UPROPERTY(BlueprintReadWrite, Category = "Defaults")
	float AnimStartTime = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "Defaults")
	int32 DataIndex = INDEX_NONE;

	void Reset()
	{
		Animation.Reset();
		EntryTransform = SyncTransform = FTransform::Identity;
		AnimStartTime = 0.f;
		DataIndex = INDEX_NONE;
	}

	FORCEINLINE bool IsValid() const { return DataIndex != INDEX_NONE; }
};

/** Stores the parameters passed into query function */
USTRUCT(BlueprintType)
struct FContextualAnimQueryParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	TWeakObjectPtr<const AActor> Querier;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	FTransform QueryTransform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	bool bComplexQuery = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	bool bFindAnimStartTime = false;

	FContextualAnimQueryParams() {}

	FContextualAnimQueryParams(const AActor* InQuerier, bool bInComplexQuery, bool bInFindAnimStartTime)
		: Querier(InQuerier), bComplexQuery(bInComplexQuery), bFindAnimStartTime(bInFindAnimStartTime) {}

	FContextualAnimQueryParams(const FTransform& InQueryTransform, bool bInComplexQuery, bool bInFindAnimStartTime)
		: QueryTransform(InQueryTransform), bComplexQuery(bInComplexQuery), bFindAnimStartTime(bInFindAnimStartTime) {}
};

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

	virtual void PreSave(const class ITargetPlatform* TargetPlatform) override;

	bool QueryData(FContextualAnimQueryResult& OutResult, const FContextualAnimQueryParams& QueryParams, const FTransform& ToWorldTransform) const;

	virtual UClass* GetPreviewActorClassForRole(const FName& Role) const override;
	virtual EContextualAnimJoinRule GetJoinRuleForRole(const FName& Role) const override;
};
