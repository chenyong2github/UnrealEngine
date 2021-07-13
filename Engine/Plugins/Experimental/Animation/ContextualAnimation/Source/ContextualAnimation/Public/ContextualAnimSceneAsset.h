// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Templates/SubclassOf.h"
#include "ContextualAnimTypes.h"
#include "ContextualAnimSceneAsset.generated.h"

class UContextualAnimScenePivotProvider;
class UContextualAnimSceneInstance;

USTRUCT(BlueprintType)
struct FContextualAnimAlignmentSectionData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alignment")
	FName SectionName = NAME_None;

	UPROPERTY(EditAnywhere, Instanced, Category = "Alignment")
	UContextualAnimScenePivotProvider* ScenePivotProvider = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Alignment", AdvancedDisplay)
	TArray<FTransform> ScenePivots;
};

enum class EContextualAnimForEachResult : uint8
{
	Break,
	Continue,
};

UCLASS(Blueprintable)
class CONTEXTUALANIMATION_API UContextualAnimSceneAsset : public UDataAsset
{
	GENERATED_BODY()

public:

	typedef TFunction<EContextualAnimForEachResult(const FName& Role, const FContextualAnimData& AnimData)> FForEachAnimDataFunction;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Animations")
	TMap<FName, FContextualAnimCompositeTrack> DataContainer;

	UPROPERTY(EditAnywhere, Category = "Settings")
	TSubclassOf<UContextualAnimSceneInstance> SceneInstanceClass;

	UPROPERTY(EditAnywhere, Category = "Settings")
	bool bDisableCollisionBetweenActors;

	UPROPERTY(EditAnywhere, Category = "Alignment")
	TArray<FContextualAnimAlignmentSectionData> AlignmentSections;

	UPROPERTY(EditAnywhere, Category = "Alignment", meta = (ClampMin = "1", ClampMax = "60"), AdvancedDisplay)
	int32 SampleRate;

	UPROPERTY(EditAnywhere, Category = "Transitions")
	TArray<FContextualAnimTransitionContainer> Transitions;

	//@TODO: Rename this is used for selection and move to private
	UPROPERTY(EditAnywhere, Category = "Roles")
	FName PrimaryRole;

	UContextualAnimSceneAsset(const FObjectInitializer& ObjectInitializer);

	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;

	const FContextualAnimTrackSettings* GetTrackSettings(const FName& Role) const;
	
	const FContextualAnimData* GetAnimDataForRoleAtIndex(const FName& Role, int32 Index) const;
	
	void ForEachAnimData(FForEachAnimDataFunction Function) const;

	TArray<FName> GetRoles() const;

	FORCEINLINE FName GetAlignmentSectionNameAtIndex(int32 Index) const
	{
		return AlignmentSections.IsValidIndex(Index) ? AlignmentSections[Index].SectionName : NAME_None;
	}

	FORCEINLINE const FName& GetLeaderRole() const { return (LeaderRole != NAME_None) ? LeaderRole : PrimaryRole; }

	FORCEINLINE float GetRadius() const { return Radius; }

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Asset")
	bool Query(const FName& Role, FContextualAnimQueryResult& OutResult, const FContextualAnimQueryParams& QueryParams, const FTransform& ToWorldTransform) const;

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Asset")
	UAnimMontage* GetAnimationForRoleAtIndex(FName Role, int32 Index) const;

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Asset")
	int32 FindAnimIndex(FName Role, UAnimMontage* Animation) const;

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Asset")
	FTransform ExtractAlignmentTransformAtTime(FName Role, int32 AnimDataIndex, float Time) const;

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Asset")
	FTransform ExtractIKTargetTransformAtTime(FName Role, int32 AnimDataIndex, FName TrackName, float Time) const;

	bool QueryCompositeTrack(const FContextualAnimCompositeTrack* Track, FContextualAnimQueryResult& OutResult, const FContextualAnimQueryParams& QueryParams, const FTransform& ToWorldTransform) const;

protected:

	void GenerateAlignmentTracks(const FContextualAnimTrackSettings& Settings, FContextualAnimData& AnimData);

	void GenerateIKTargetTracks(const FContextualAnimTrackSettings& Settings, FContextualAnimData& AnimData);

	void UpdateRadius();

private:

	UPROPERTY(EditAnywhere, Category = "Roles")
	FName LeaderRole;

	/** Radius that enclose all the entry points */
	UPROPERTY(VisibleAnywhere, Category = "Settings")
	float Radius = 0.f;
};
