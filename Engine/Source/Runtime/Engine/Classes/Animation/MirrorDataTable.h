// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DataTable.h"
#include "BoneContainer.h"
#include "MirrorDataTable.generated.h"

/** Type referenced by a row in the mirror data table */
UENUM()
enum EMirrorRowType
{
	Bone,
	Notify,
	Curve
};

/** Find and Replace Method for FMirrorFindReplaceExpression. */
UENUM()
enum EMirrorFindReplaceMethod
{
	/** Only find and replace matching strings at the start of the name  */
	Prefix,
	/** Only find and replace matching strings at the end of the name  */
	Suffix,
	/** Use regular expressions for find and replace, including support for captures $1 - $10 */
	RegularExpression
};

/**  Base Mirror Table containing all data required by the animation mirroring system. */
USTRUCT()
struct FMirrorTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Mirroring)
	FName Name;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Mirroring)
	FName MirroredName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Mirroring)
	TEnumAsByte<EMirrorRowType> MirrorEntryType;

	FMirrorTableRow()
		: Name(NAME_None)
		, MirroredName(NAME_None)
		, MirrorEntryType(EMirrorRowType::Bone) {}

	ENGINE_API FMirrorTableRow(const FMirrorTableRow& Other);
	ENGINE_API FMirrorTableRow& operator=(FMirrorTableRow const& Other);
	ENGINE_API bool operator==(FMirrorTableRow const& Other) const;
	ENGINE_API bool operator!=(FMirrorTableRow const& Other) const;
	ENGINE_API bool operator<(FMirrorTableRow const& Other) const;
};

/** Find and Replace expressions used to generate mirror tables*/
USTRUCT()
struct FMirrorFindReplaceExpression
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Mirroring)
	FName FindExpression;

	UPROPERTY(EditAnywhere, Category = Mirroring)
	FName ReplaceExpression;

	UPROPERTY(EditAnywhere, Category = Mirroring)
	TEnumAsByte<EMirrorFindReplaceMethod> FindReplaceMethod;

	FMirrorFindReplaceExpression() 
		: FindExpression(NAME_None)
		, ReplaceExpression(NAME_None)
		, FindReplaceMethod(EMirrorFindReplaceMethod::Prefix) {}

	FMirrorFindReplaceExpression(FName InFindExpression, FName InReplaceExpression, EMirrorFindReplaceMethod Method)
		: FindExpression(InFindExpression)
		, ReplaceExpression(InReplaceExpression)
		, FindReplaceMethod(Method)
	{
	}
};

/**
 * Data table for mirroring bones, notifies, and curves.   The mirroring table allows self mirroring with entries where the name and mirrored name are identical
 */
UCLASS(MinimalAPI, BlueprintType, hideCategories = (ImportOptions, ImportSource) /* AutoExpandCategories = "MirrorDataTable,ImportOptions"*/)
class UMirrorDataTable : public UDataTable
{
	GENERATED_BODY()

	friend class UMirrorDataTableFactory;

public:
	UMirrorDataTable(const FObjectInitializer& ObjectInitializer);

	ENGINE_API virtual void GetPreloadDependencies(TArray<UObject*>& OutDeps) override;

	ENGINE_API virtual void Serialize(FArchive& Ar) override;

	ENGINE_API virtual void PostLoad() override;

	ENGINE_API virtual void EmptyTable() override;

#if WITH_EDITOR

	ENGINE_API virtual void CleanBeforeStructChange() override;

	ENGINE_API virtual void RestoreAfterStructChange() override;

	ENGINE_API virtual void PreEditChange(FProperty* PropertyThatWillChange) override;

	ENGINE_API virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

	ENGINE_API virtual void PostEditUndo() override;

#endif // WITH_EDITOR

	/**
	 * Apply the animation settings mirroring find and replace strings against the given name, returning
	 * the mirrored name or NAME_None if none of the find strings are found in the name. 
	 * 
	 * @param	InName		Name to map against animation settings mirroring find and replace 
	 * @return				The mirrored name or NAME_None
	 */
	ENGINE_API static FName GetSettingsMirrorName(FName InName); 

	/**
	 * Apply the provided find and replace strings against the given name, returning
	 * the mirrored name or NAME_None if none of the find strings are found in the name. 
	 * 
	 * @param	MirrorFindReplaceExpressions		Find and replace expressions.  The first matching expression will be returned
	 * @param	InName								Name to find and replace 
	 * @return										The mirrored name or NAME_None if none of the expressions match
	 */
	ENGINE_API static FName GetMirrorName(FName InName, const TArray<FMirrorFindReplaceExpression>& MirrorFindReplaceExpressions);

	/**
     * Create Mirror Bone Indices for the provided BoneContainer.  The CompactBonePoseMirrorBones provides an index map which can be used to mirror at runtime
	 *
	 * @param	BoneContainer					The Bone Container that the OutCompactPaseMirrorBones should match
	 * @param	MirrorBoneIndexes				Mirror bone indexes created for the ReferenceSkeleton used by the BoneContainer 
	 * @param	OutCompactPoseMirrorBones		An efficient representation of the bones to mirror which can be used at runtime
	 */
	ENGINE_API static void FillCompactPoseMirrorBones(const FBoneContainer& BoneContainer, const TArray<int32>& MirrorBoneIndexes, TArray<FCompactPoseBoneIndex>& OutCompactPoseMirrorBones);


	/**
	 * Converts the mirror data table Name -> MirrorName map into an index map for the given ReferenceSkeleton
	 *
	 * @param	ReferenceSkeleton		The ReferenceSkeleton to compute the mirror index against
	 * @param	OutMirrorBoneIndexes	An array that provides the bone index of the mirror bone, or INDEX_NONE if the bone is not mirrored
	 */
	ENGINE_API void FillMirrorBoneIndexes(const FReferenceSkeleton& ReferenceSkeleton, TArray<int32>& OutMirrorBoneIndexes) const;

#if WITH_EDITOR  
	/**
	 * Populates the table by running the MirrorFindReplaceExpressions on bone names in the Skeleton.  If the mirrored name is also found 
	 * on the Skeleton it is added to the table.
	 */
	ENGINE_API void FindReplaceMirroredNames();
#endif // WITH_EDITOR

	/**
	 * Evaluate the MirrorFindReplaceExpressions on InName and return the replaced value of the first entry that matches
	 *
	 * @param	InName		The input string to find & replace
	 * @return				The replaced result of the first MirrorFindReplaceExpression where the find pattern matched
	 */
	ENGINE_API FName FindReplace(FName InName) const;

public:

	UPROPERTY(EditAnywhere, Category = CreateTable)
	TArray<FMirrorFindReplaceExpression> MirrorFindReplaceExpressions;

	UPROPERTY(EditAnywhere, Category = Mirroring)
	TEnumAsByte<EAxis::Type> MirrorAxis;

	UPROPERTY(EditAnywhere, AssetRegistrySearchable, Category = Skeleton)
	TObjectPtr<USkeleton> Skeleton; 

	// Index of the mirror bone for a given bone index in the reference skeleton, or INDEX_NONE if the bone is not mirrored
	TArray<int32> BoneToMirrorBoneIndex;

	// Array with entries the source UIDs of curves that should be mirrored. 
	TArray<SmartName::UID_Type> CurveMirrorSourceUIDArray;

	// Array with the target UIDs of curves that should be mirrored. 
	TArray<SmartName::UID_Type> CurveMirrorTargetUIDArray;

	// Map from notify to mirror notify
	TMap<FName, FName> NotifyToMirrorNotifyMap;

protected: 

	// Fill BoneToMirrorBoneIndex, CurveMirrorSourceUIDArray, CurveMirrorTargetUIDArray and NotifyToMirrorNotifyIndex based on the Skeleton and Table Contents
	ENGINE_API void FillMirrorArrays();

	void HandleDataTableChanged();
};

