// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "Templates/SubclassOf.h"
#include "ContextualAnimTypes.h"
#include "ContextualAnimSceneAsset.generated.h"

class UContextualAnimScenePivotProvider;
class UContextualAnimSceneInstance;

UCLASS(Blueprintable)
class CONTEXTUALANIMATION_API UContextualAnimRolesAsset : public UDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	TArray<FContextualAnimRoleDefinition> Roles;

	UContextualAnimRolesAsset(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {};

	const FContextualAnimRoleDefinition* FindRoleDefinitionByName(const FName& Name) const
	{
		return Roles.FindByPredicate([Name](const FContextualAnimRoleDefinition& RoleDef) { return RoleDef.Name == Name; });
	}

	FORCEINLINE int32 GetNumRoles() const { return Roles.Num(); }
};

USTRUCT(BlueprintType)
struct FContextualAnimTracksContainer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	TArray<FContextualAnimTrack> Tracks;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Defaults")
	TArray<FTransform> ScenePivots;
};

UCLASS(Blueprintable)
class CONTEXTUALANIMATION_API UContextualAnimSceneAsset : public UDataAsset
{
	GENERATED_BODY()

public:

	typedef TFunctionRef<UE::ContextualAnim::EForEachResult(const FContextualAnimTrack& AnimTrack)> FForEachAnimTrackFunction;

	UContextualAnimSceneAsset(const FObjectInitializer& ObjectInitializer);

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	virtual void PreSave(FObjectPreSaveContext ObjectSaveContext) override;

	void PrecomputeData();
	
	void ForEachAnimTrack(FForEachAnimTrackFunction Function) const;

	void ForEachAnimTrack(int32 VariantIdx, FForEachAnimTrackFunction Function) const;

	FORCEINLINE const FName& GetPrimaryRole() const { return PrimaryRole; }
	FORCEINLINE float GetRadius() const { return Radius; }
	FORCEINLINE bool GetDisableCollisionBetweenActors() const { return bDisableCollisionBetweenActors; }
	FORCEINLINE const TSubclassOf<UContextualAnimSceneInstance>& GetSceneInstanceClass() const { return SceneInstanceClass; }
	FORCEINLINE const TArray<FContextualAnimAlignmentSectionData>& GetAlignmentSections() const { return  AlignmentSections; }

	bool HasValidData() const { return RolesAsset && Variants.Num() > 0; }

	const UContextualAnimRolesAsset* GetRolesAsset() const { return RolesAsset; }

	UFUNCTION()
	TArray<FName> GetRoles() const;

	int32 GetNumRoles() const { return RolesAsset ? RolesAsset->GetNumRoles() : 0; }

	const FContextualAnimTrack* GetAnimTrack(const FName& Role, int32 VariantIdx) const;

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Asset")
	int32 FindVariantIdx(FName Role, UAnimSequenceBase* Animation) const;

	FName FindRoleByAnimation(const UAnimSequenceBase* Animation) const;

	const FContextualAnimTrack* FindFirstAnimTrackForRoleThatPassesSelectionCriteria(const FName& Role, const FContextualAnimSceneBindingContext& Primary, const FContextualAnimSceneBindingContext& Querier) const;

	const FContextualAnimTrack* FindAnimTrackForRoleWithClosestEntryLocation(const FName& Role, const FContextualAnimSceneBindingContext& Primary, const FVector& TestLocation) const;

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Asset")
	FTransform GetAlignmentTransformForRoleRelativeToScenePivot(FName Role, int32 VariantIdx, float Time) const;

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Asset")
	FTransform GetAlignmentTransformForRoleRelativeToOtherRole(FName FromRole, FName ToRole, int32 VariantIdx, float Time) const;

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Asset")
	FTransform GetIKTargetTransformForRoleAtTime(FName Role, int32 VariantIdx, FName TrackName, float Time) const;

	const FContextualAnimIKTargetDefContainer& GetIKTargetDefsForRole(const FName& Role) const;

	const FTransform& GetMeshToComponentForRole(const FName& Role) const;

	int32 GetTotalVariants() const;

	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Asset", meta = (DisplayName = "Get Anim Track"))
	const FContextualAnimTrack& BP_GetAnimTrack(FName Role, int32 VariantIdx) const
	{
		const FContextualAnimTrack* AnimTrack = GetAnimTrack(Role, VariantIdx);
		return AnimTrack ? *AnimTrack : FContextualAnimTrack::EmptyTrack;
	}

	//@TODO: Kept around only to do not break existing content. It will go away in the future.
	UFUNCTION(BlueprintCallable, Category = "Contextual Anim|Scene Asset")
	bool Query(FName Role, FContextualAnimQueryResult& OutResult, const FContextualAnimQueryParams& QueryParams, const FTransform& ToWorldTransform) const;

protected:

	UPROPERTY(EditAnywhere, Category = "Defaults")
	TObjectPtr<UContextualAnimRolesAsset> RolesAsset;

	UPROPERTY(EditAnywhere, Category = "Defaults", meta = (GetOptions = "GetRoles"))
	FName PrimaryRole = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Defaults")
	TArray<FContextualAnimTracksContainer> Variants;

	UPROPERTY(EditAnywhere, Category = "Defaults")
	TMap<FName, FContextualAnimIKTargetDefContainer> RoleToIKTargetDefsMap;

	UPROPERTY(EditAnywhere, Category = "Defaults")
	TArray<FContextualAnimAlignmentSectionData> AlignmentSections;

	UPROPERTY(EditAnywhere, Category = "Settings")
	TSubclassOf<UContextualAnimSceneInstance> SceneInstanceClass;

	UPROPERTY(EditAnywhere, Category = "Settings")
	bool bDisableCollisionBetweenActors = true;

	/** Sample rate (frames per second) used when sampling the animations to generate alignment and IK tracks */
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (ClampMin = "1", ClampMax = "60"), AdvancedDisplay)
	int32 SampleRate = 15;

	UPROPERTY(VisibleAnywhere, Category = "Settings")
	float Radius = 0.f;

	void GenerateAlignmentTracks();

	void GenerateIKTargetTracks();

	void UpdateRadius();

	friend class UContextualAnimUtilities;
	friend class FContextualAnimViewModel;
	friend class FContextualAnimEdMode;
	friend class FContextualAnimMovieSceneNotifyTrackEditor;
};
