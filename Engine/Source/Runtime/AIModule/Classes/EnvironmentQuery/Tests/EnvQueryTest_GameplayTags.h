// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameplayTagContainer.h"
#include "EnvironmentQuery/EnvQueryTest.h"
#include "EnvQueryTest_GameplayTags.generated.h"

class IGameplayTagAssetInterface;

UCLASS(MinimalAPI)
class UEnvQueryTest_GameplayTags : public UEnvQueryTest
{
	GENERATED_UCLASS_BODY()

public:
	/**
	 * @note Calling function only makes sense before first run of given query
	 * by the EQS manager. The query gets preprocessed and cached then so the query 
	 * value will get stored and calling this function will not change it (unless 
	 * you call it on the cached test's instance, see UEnvQueryManager::CreateQueryInstance).
	 */
	void SetTagQueryToMatch(FGameplayTagQuery& GameplayTagQuery);

protected:
	virtual void RunTest(FEnvQueryInstance& QueryInstance) const override;

	virtual FText GetDescriptionDetails() const override;

	bool SatisfiesTest(IGameplayTagAssetInterface* ItemGameplayTagAssetInterface) const;

	/**
	 * Presave function. Gets called once before an object gets serialized for saving. This function is necessary
	 * for save time computation as Serialize gets called three times per object from within SavePackage.
	 *
	 * @warning: Objects created from within PreSave will NOT have PreSave called on them!!!
	 */
	virtual void PreSave(const class ITargetPlatform* TargetPlatform) override;

	virtual void PostLoad() override;

	UPROPERTY(EditAnywhere, Category=GameplayTagCheck)
	FGameplayTagQuery TagQueryToMatch;

	// Used to determine whether the file format needs to be updated to move data into TagQueryToMatch or not.
	UPROPERTY()
	bool bUpdatedToUseQuery;

	// Deprecated property.  Used only to load old data into TagQueryToMatch.
	UPROPERTY()
	EGameplayContainerMatchType TagsToMatch;

	// Deprecated property.  Used only to load old data into TagQueryToMatch.
	UPROPERTY()
	FGameplayTagContainer GameplayTags;
};
