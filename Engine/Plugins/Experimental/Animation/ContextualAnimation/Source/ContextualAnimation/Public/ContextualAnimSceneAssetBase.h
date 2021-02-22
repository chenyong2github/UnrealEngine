// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataAsset.h"
#include "ContextualAnimTypes.h"
#include "ContextualAnimSceneAssetBase.generated.h"

class UContextualAnimScenePivotProvider;

USTRUCT(BlueprintType)
struct FContextualAnimAlignmentSectionData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alignment")
	FName SectionName = NAME_None;

	UPROPERTY(EditAnywhere, Instanced, Category = "Alignment")
	UContextualAnimScenePivotProvider* ScenePivotProvider = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Alignment", AdvancedDisplay)
	FTransform ScenePivot;
};

UCLASS(Abstract, Blueprintable)
class CONTEXTUALANIMATION_API UContextualAnimSceneAssetBase : public UDataAsset
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alignment")
	TArray<FContextualAnimAlignmentSectionData> AlignmentSections;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alignment", meta = (ClampMin = "1", ClampMax = "60"), AdvancedDisplay)
	int32 SampleRate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Alignment", AdvancedDisplay)
	FTransform MeshToComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roles")
	FName PrimaryRole;

	UContextualAnimSceneAssetBase(const FObjectInitializer& ObjectInitializer);

	virtual void PreSave(const class ITargetPlatform* TargetPlatform) override;

	//@TODO: Make pure virtual
	virtual UClass* GetPreviewActorClassForRole(const FName& Role) const { return nullptr; }
	virtual EContextualAnimJoinRule GetJoinRuleForRole(const FName& Role) const { return EContextualAnimJoinRule::Default; }

	FTransform ExtractTransformFromAnimData(const FContextualAnimData& AnimData, float Time) const;

	FORCEINLINE FName GetAlignmentSectionNameAtIndex(int32 Index) const 
	{
		return AlignmentSections.IsValidIndex(Index) ? AlignmentSections[Index].SectionName : NAME_None;
	}

protected:

	void GenerateAlignmentTracksRelativeToScenePivot(FContextualAnimData& AnimData);
};
