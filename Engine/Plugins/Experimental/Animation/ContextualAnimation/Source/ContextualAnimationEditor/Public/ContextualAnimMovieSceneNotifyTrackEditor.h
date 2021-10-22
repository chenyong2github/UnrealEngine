// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MovieSceneTrackEditor.h"
#include "ContextualAnimTypes.h"
#include "BoneContainer.h"
#include "Interfaces/Interface_BoneReferenceSkeletonProvider.h"
#include "ContextualAnimMovieSceneNotifyTrackEditor.generated.h"

class AActor;
class UAnimMontage;
class USkeleton;
class UContextualAnimSceneAsset;
class UContextualAnimMovieSceneNotifySection;
class UContextualAnimMovieSceneNotifyTrack;
class UContextualAnimMovieSceneSequence;

/** Struct used to construct the New Role Widget */
USTRUCT()
struct FNewRoleWidgetParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Settings")
	FName RoleName = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Settings")
	TSubclassOf<AActor> PreviewClass;

	UPROPERTY(EditAnywhere, Category = "Settings")
	UAnimMontage* Animation = nullptr;

	UPROPERTY(EditAnywhere, Category = "Settings")
	bool bRequiresFlyingMode;

	UPROPERTY(EditAnywhere, Category = "Settings")
	FTransform MeshToComponent = FTransform(FRotator(0.f, -90.f, 0.f));

	bool HasValidData() const;
};

USTRUCT()
struct FNewIKTargetParams
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category = "Settings")
	FName SourceRole = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Settings")
	FBoneReference SourceBone;

	UPROPERTY(EditAnywhere, Category = "Settings")
	EContextualAnimIKTargetProvider Provider;

	UPROPERTY(EditAnywhere, Category = "Settings", meta = (GetOptions = "GetTargetRoleOptions"))
	FName TargetRole = NAME_None;

	UPROPERTY(EditAnywhere, Category = "Settings")
	FBoneReference TargetBone;

	UPROPERTY(EditAnywhere, Category = "Settings")
	FName GoalName = NAME_None;

};

/** Object used to construct the New IK Target Widget */
UCLASS()
class UNewIKTargetWidgetParams : public UObject, public IBoneReferenceSkeletonProvider
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, Category = "Settings", meta = (ShowOnlyInnerProperties))
	FNewIKTargetParams Params;

	UNewIKTargetWidgetParams(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer) {}

	// ~IBoneReferenceSkeletonProvider Interface
	virtual USkeleton* GetSkeleton(bool& bInvalidSkeletonIsError, const IPropertyHandle* PropertyHandle = nullptr) override;

	void Reset(const FName& InSourceRole, const UContextualAnimSceneAsset& InSceneAsset);

	bool HasValidData() const;

	const UContextualAnimSceneAsset& GetSceneAsset() const;

	UFUNCTION()
	TArray<FString> GetTargetRoleOptions() const;

private:

	UPROPERTY()
	TWeakObjectPtr<const UContextualAnimSceneAsset> SceneAssetPtr;

	UPROPERTY()
	TArray<FName> CachedRoles;
};

/** Handles section drawing and manipulation of a MovieSceneNotifyTrack */
class FContextualAnimMovieSceneNotifyTrackEditor : public FMovieSceneTrackEditor, public FGCObject
{
public:

	FContextualAnimMovieSceneNotifyTrackEditor(TSharedRef<ISequencer> InSequencer);

	// ~FGCObject Interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FContextualAnimMovieSceneNotifyTrackEditor"); }

	// ~FMovieSceneTrackEditor Interface
	virtual TSharedRef<ISequencerSection> MakeSectionInterface(UMovieSceneSection& SectionObject, UMovieSceneTrack& Track, FGuid ObjectBinding) override;
	virtual bool SupportsType(TSubclassOf<UMovieSceneTrack> TrackClass) const override;
	virtual bool SupportsSequence(UMovieSceneSequence* InSequence) const override;
	virtual TSharedPtr<SWidget> BuildOutlinerEditWidget(const FGuid& ObjectBinding, UMovieSceneTrack* Track, const FBuildEditWidgetParams& Params) override;
	virtual void BuildAddTrackMenu(FMenuBuilder& MenuBuilder) override;
	virtual void BuildObjectBindingTrackMenu(FMenuBuilder& MenuBuilder, const TArray<FGuid>& ObjectBindings, const UClass* ObjectClass) override;

	static TSharedRef<ISequencerTrackEditor> CreateTrackEditor(TSharedRef<ISequencer> InSequencer);

	UContextualAnimMovieSceneSequence& GetMovieSceneSequence() const;

private:

	TObjectPtr<UNewIKTargetWidgetParams> NewIKTargetWidgetParams;

	TSharedPtr<FStructOnScope> NewRoleWidgetParams;

	void BuildAddTrackSubMenu(FMenuBuilder& MenuBuilder, TArray<FGuid>);

	UContextualAnimMovieSceneNotifySection* CreateNewSection(UContextualAnimMovieSceneNotifyTrack* Track, int32 RowIndex, UClass* NotifyClass);

	UContextualAnimMovieSceneNotifySection* CreateNewIKSection(UContextualAnimMovieSceneNotifyTrack* Track, int32 RowIndex, const FName& GoalName);

	void BuildNewIKTargetSubMenu(FMenuBuilder& MenuBuilder, UContextualAnimMovieSceneNotifyTrack* Track, int32 RowIndex);

	void BuildNewIKTargetWidget(FMenuBuilder& MenuBuilder, UContextualAnimMovieSceneNotifyTrack* Track, int32 RowIndex);

	void AddNewNotifyTrack(TArray<FGuid> ObjectBindings);

	void FillNewNotifyStateMenu(FMenuBuilder& MenuBuilder, bool bIsReplaceWithMenu, UContextualAnimMovieSceneNotifyTrack* Track, int32 RowIndex);
};

// FContextualAnimNotifySection
////////////////////////////////////////////////////////////////////////////////////////////////

/** UI portion of a NotifySection in a NotifyTrack */
class FContextualAnimNotifySection : public ISequencerSection
{
public:
	
	FContextualAnimNotifySection(UMovieSceneSection& InSection);
	virtual ~FContextualAnimNotifySection() { }

	virtual UMovieSceneSection* GetSectionObject() override;
	virtual int32 OnPaintSection(FSequencerSectionPainter& InPainter) const override;
	virtual FText GetSectionTitle() const override;

private:
	
	/** The section we are visualizing */
	UContextualAnimMovieSceneNotifySection& Section;
};