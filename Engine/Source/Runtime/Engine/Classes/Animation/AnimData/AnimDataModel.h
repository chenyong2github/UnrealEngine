// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

#include "IAnimationDataModel.h"

#include "AnimDataModel.generated.h"

/**
 * The Model represents the source data for animations. It contains both bone animation data as well as animated curves.
 * They are currently only a sub-object of a AnimSequenceBase instance. The instance derives all runtime data from the source data. 
 */
UCLASS(BlueprintType, meta=(DebugTreeLeaf))
class ENGINE_API UAnimDataModel : public UObject, public IAnimationDataModel
{
	GENERATED_BODY()
public:
	/** Begin UObject overrides */
	virtual void PostLoad() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	virtual bool IsEditorOnly() const override { return true; }
	/** End UObject overrides */
	
	/** Begin IAnimationDataModel overrides */
	virtual double GetPlayLength() const override;
	virtual int32 GetNumberOfFrames() const override;
	virtual int32 GetNumberOfKeys() const override;
	virtual FFrameRate GetFrameRate() const override;
	virtual const TArray<FBoneAnimationTrack>& GetBoneAnimationTracks() const override;
	virtual const FBoneAnimationTrack& GetBoneTrackByIndex(int32 TrackIndex) const override;
	virtual const FBoneAnimationTrack& GetBoneTrackByName(FName TrackName) const override;
	virtual const FBoneAnimationTrack* FindBoneTrackByName(FName Name) const override;
	virtual const FBoneAnimationTrack* FindBoneTrackByIndex(int32 BoneIndex) const override;
	virtual int32 GetBoneTrackIndex(const FBoneAnimationTrack& Track) const override;
	virtual int32 GetBoneTrackIndexByName(FName TrackName) const override;
	virtual bool IsValidBoneTrackIndex(int32 TrackIndex) const override;
	virtual int32 GetNumBoneTracks() const override;
	virtual void GetBoneTrackNames(TArray<FName>& OutNames) const override;

	virtual const FAnimCurveBase* FindCurve(const FAnimationCurveIdentifier& CurveIdentifier) const override;
	virtual const FFloatCurve* FindFloatCurve(const FAnimationCurveIdentifier& CurveIdentifier) const override;
	virtual const FTransformCurve* FindTransformCurve(const FAnimationCurveIdentifier& CurveIdentifier) const override;
	virtual const FRichCurve* FindRichCurve(const FAnimationCurveIdentifier& CurveIdentifier) const override;
	virtual const FAnimationCurveData& GetCurveData() const override;
	virtual int32 GetNumberOfTransformCurves() const override;
	virtual int32 GetNumberOfFloatCurves() const override;
	virtual const TArray<struct FFloatCurve>& GetFloatCurves() const override;
	virtual const TArray<struct FTransformCurve>& GetTransformCurves() const override;	
	virtual const FAnimCurveBase& GetCurve(const FAnimationCurveIdentifier& CurveIdentifier) const override;
	virtual const FFloatCurve& GetFloatCurve(const FAnimationCurveIdentifier& CurveIdentifier) const override;
	virtual const FTransformCurve& GetTransformCurve(const FAnimationCurveIdentifier& CurveIdentifier) const override;
	virtual const FRichCurve& GetRichCurve(const FAnimationCurveIdentifier& CurveIdentifier) const override;
	virtual TArrayView<const FAnimatedBoneAttribute> GetAttributes() const override;
	virtual int32 GetNumberOfAttributes() const override;
	virtual int32 GetNumberOfAttributesForBoneIndex(const int32 BoneIndex) const override;
	virtual void GetAttributesForBone(const FName& BoneName, TArray<const FAnimatedBoneAttribute*>& OutBoneAttributes) const override;
	virtual const FAnimatedBoneAttribute& GetAttribute(const FAnimationAttributeIdentifier& AttributeIdentifier) const override;
	virtual const FAnimatedBoneAttribute* FindAttribute(const FAnimationAttributeIdentifier& AttributeIdentifier) const override;
	virtual UAnimSequence* GetAnimationSequence() const override;
	virtual FAnimDataModelModifiedEvent& GetModifiedEvent() override { return ModifiedEvent; }
	virtual FGuid GenerateGuid() const override;
#if WITH_EDITOR
	virtual void Evaluate(FAnimationPoseData& InOutPoseData, const UE::Anim::DataModel::FEvaluationContext& EvaluationContext) const override;
#endif
	virtual TScriptInterface<IAnimationDataController> GetController() override;
	virtual bool HasBeenPopulated() const override { return bPopulated; }
protected:
	virtual IAnimationDataModel::FModelNotifier& GetNotifier() override;
	virtual FAnimDataModelModifiedDynamicEvent& GetModifiedDynamicEvent() override { return ModifiedEventDynamic; }
	virtual void OnNotify(const EAnimDataModelNotifyType& NotifyType, const FAnimDataModelNotifPayload& Payload) override {}
	/** End IAnimationDataModel overrides */

private:
	/** Helper functionality used by UAnimDataController to retrieve mutable data */ 
	FBoneAnimationTrack* FindMutableBoneTrackByName(FName Name);
	FBoneAnimationTrack& GetMutableBoneTrackByName(FName Name);
	FTransformCurve* FindMutableTransformCurveById(const FAnimationCurveIdentifier& CurveIdentifier);
	FFloatCurve* FindMutableFloatCurveById(const FAnimationCurveIdentifier& CurveIdentifier);
	FAnimCurveBase* FindMutableCurveById(const FAnimationCurveIdentifier& CurveIdentifier);	   
	FRichCurve* GetMutableRichCurve(const FAnimationCurveIdentifier& CurveIdentifier);

private:
	UPROPERTY(Transient)
	int32 BracketCounter = 0;

	/** Dynamic delegate event allows scripting to register to any broadcasted notify. */
	UPROPERTY(BlueprintAssignable, Transient, Category = AnimationDataModel, meta = (ScriptName = "ModifiedEvent", AllowPrivateAccess = "true"))
	FAnimDataModelModifiedDynamicEvent ModifiedEventDynamic;
	
	/** Native delegate event allows for registerings to any broadcasted notify. */
	FAnimDataModelModifiedEvent ModifiedEvent;

	/** All individual bone animation tracks */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Animation Data Model", meta = (AllowPrivateAccess = "true"))
	TArray<FBoneAnimationTrack> BoneAnimationTracks;

	/** Total playable length of the contained animation data */
	UE_DEPRECATED(5.1, "PlayLength is deprecated use GetPlayLength instead, as it is now calculated with Number of Frames * FrameRate instead of stored as a value")
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation Data Model", meta = (AllowPrivateAccess = "true"))
	float PlayLength;
	
	/** Rate at which the animated data is sampled */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation Data Model", meta = (AllowPrivateAccess = "true"))
	FFrameRate FrameRate;

	/** Total number of sampled animated frames */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation Data Model", meta = (AllowPrivateAccess = "true"))
	int32 NumberOfFrames;

	/** Total number of sampled animated keys */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation Data Model", meta = (AllowPrivateAccess = "true"))
	int32 NumberOfKeys;
	
	/** Container with all animated curve data */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation Data Model", meta = (AllowPrivateAccess = "true"))
	FAnimationCurveData CurveData;
	
	/** Container with all animated (bone) attribute data */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Animation Data Model", meta = (AllowPrivateAccess = "true"))
	TArray<FAnimatedBoneAttribute> AnimatedBoneAttributes;

	UPROPERTY()
	bool bPopulated = false;

	TUniquePtr<IAnimationDataModel::FModelNotifier> Notifier;

	friend class UAnimDataController;
	friend class FAnimDataControllerTestBase;
};

