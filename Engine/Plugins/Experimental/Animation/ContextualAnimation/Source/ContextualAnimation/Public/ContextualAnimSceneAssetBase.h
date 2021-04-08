// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Templates/SubclassOf.h"
#include "ContextualAnimTypes.h"
#include "ContextualAnimSceneAssetBase.generated.h"

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

UCLASS(Abstract, Blueprintable)
class CONTEXTUALANIMATION_API UContextualAnimSceneAssetBase : public UDataAsset
{
	GENERATED_BODY()

public:

	typedef TFunction<EContextualAnimForEachResult(const FName& Role, const FContextualAnimData& AnimData)> FForEachAnimDataFunction;

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

	UContextualAnimSceneAssetBase(const FObjectInitializer& ObjectInitializer);

	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;

	//@TODO: Make pure virtual
	virtual const FContextualAnimTrackSettings* GetTrackSettings(const FName& Role) const { return nullptr; }
	virtual const FContextualAnimData* GetAnimDataForRoleAtIndex(const FName& Role, int32 Index) const { return nullptr; };
	virtual void ForEachAnimData(FForEachAnimDataFunction Function) const {}

	//@TODO: Temp until have a roles asset
	virtual TArray<FName> GetRoles() const { return TArray<FName>(); }

	FORCEINLINE FName GetAlignmentSectionNameAtIndex(int32 Index) const 
	{
		return AlignmentSections.IsValidIndex(Index) ? AlignmentSections[Index].SectionName : NAME_None;
	}

	FORCEINLINE const FName& GetLeaderRole() const { return LeaderRole != NAME_None ? LeaderRole : PrimaryRole; }

	FORCEINLINE float GetRadius() const { return Radius; }

	virtual bool Query(const FName& Role, FContextualAnimQueryResult& OutResult, const FContextualAnimQueryParams& QueryParams, const FTransform& ToWorldTransform) const { return false; };

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
