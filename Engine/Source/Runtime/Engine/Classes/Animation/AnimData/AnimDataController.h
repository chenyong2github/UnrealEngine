// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "CoreTypes.h"
#include "UObject/ObjectMacros.h"
#include "Animation/AnimData/AnimDataModel.h"
#include "Animation/AnimCurveTypes.h"
#include "Algo/Transform.h"

#if WITH_EDITOR
#include "ChangeTransactor.h"
#endif // WITH_EDITOR

#include "AnimDataController.generated.h"

#if WITH_EDITOR

#endif // WITH_EDITOR

struct FAnimationCurveIdentifier;
struct FAnimationAttributeIdentifier;
static const int32 DefaultCurveFlags = EAnimAssetCurveFlags::AACF_Editable;

namespace UE {
namespace Anim {
	class FOpenBracketAction;
	class FCloseBracketAction;
}}

/**
 * The Controller is the sole authority to perform changes on the Animation Data Model. Any mutation to the model made will
 * cause a subsequent notify (EAnimDataModelNotifyType) to be broadcasted from the Model's ModifiedEvent. Alongside of it is a 
 * payload containing information relevant to the mutation. These notifies should be relied upon to update any dependent views 
 * or generated (derived) data.
 */
UCLASS(BlueprintType)
class ENGINE_API UAnimDataController : public UObject
{
	GENERATED_BODY()

public:
	UAnimDataController() 
#if WITH_EDITOR
	: BracketDepth(0) 
#endif // WITH_EDITOR
	{}

#if WITH_EDITOR
	/** RAII helper to define a scoped-based bracket, opens and closes a controller bracket automatically */
	struct FScopedBracket
	{
		FScopedBracket(UAnimDataController* InController, const FText& InDescription)
			: Controller(InController)
		{
			Controller->OpenBracket(InDescription);
		}

		~FScopedBracket()
		{
			Controller->CloseBracket();
		}

		UAnimDataController* Controller;
	};

	/**
	* Sets the AnimDataModel instance this controller is supposed to be targeting
	*
	* @param	InModel		UAnimDataModel instance to target
	*/
	UFUNCTION(BlueprintCallable, Category=AnimationData)
	void SetModel(UAnimDataModel* InModel);

	/**
	* @return		The AnimDataModel instance this controller is currently targeting
	*/
	UFUNCTION(BlueprintPure, Category = AnimationData)
	UAnimDataModel* GetModel() { return Model; }

	/**
	* @return		The AnimDataModel instance this controller is currently targeting
	*/
	const UAnimDataModel* const GetModel() const;

	/**
	* Opens an interaction bracket, used for combining a set of controller actions. Broadcasts a EAnimDataModelNotifyType::BracketOpened notify,
	* this can be used by any Views or dependendent systems to halt any unnecessary or invalid operations until the (last) bracket is closed.
	*
	* @param	InTitle				Description of the bracket, e.g. "Generating Curve Data"
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*/
	UFUNCTION(BlueprintCallable, Category = AnimationData)
	void OpenBracket(const FText& InTitle, bool bShouldTransact = true);

	/**
	* Closes a previously opened interaction bracket, used for combining a set of controller actions. Broadcasts a EAnimDataModelNotifyType::BracketClosed notify.
	*
	* @param	InTitle				Description of the bracket, e.g. "Generating Curve Data"
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*/
	UFUNCTION(BlueprintCallable, Category = AnimationData)
	void CloseBracket(bool bShouldTransact = true);

	/**
	* Sets the total play-able length in seconds. Broadcasts a EAnimDataModelNotifyType::SequenceLengthChanged notify if successful.
	* The number of frames and keys for the provided length is recalculated according to the current value of UAnimDataModel::FrameRate.
	*
	* @param	Length				New play-able length value, has to be positive and non-zero
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*/
	UFUNCTION(BlueprintCallable, Category = AnimationData)
	void SetPlayLength(float Length, bool bShouldTransact = true);

	/*** Sets the total play-able length in seconds. Broadcasts a EAnimDataModelNotifyType::SequenceLengthChanged notify if successful.
	* T0 and T1 are expected to represent the window of time that was either added or removed. E.g. for insertion T0 indicates the time
	* at which additional time starts and T1 were it ends. For removal T0 indicates the time at which time should be started to remove, and T1 indicates the end. Giving a total of T1 - T0 added or removed length.
	* The number of frames and keys for the provided length is recalculated according to the current value of UAnimDataModel::FrameRate.
	* @param	Length				Total new play-able length value, has to be positive and non-zero
	* @param	T0					Point between 0 and Length at which the change in time starts
	* @param	T1					Point between 0 and Length at which the change in time ends
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*/
	UFUNCTION(BlueprintCallable, Category = AnimationData)
	void ResizePlayLength(float NewLength, float T0, float T1, bool bShouldTransact = true);

	/**
	* Sets the total play-able length in seconds and resizes curves. Broadcasts EAnimDataModelNotifyType::SequenceLengthChanged
	* and EAnimDataModelNotifyType::CurveChanged notifies if successful.
	* T0 and T1 are expected to represent the window of time that was either added or removed. E.g. for insertion T0 indicates the time
	* at which additional time starts and T1 were it ends. For removal T0 indicates the time at which time should be started to remove, and T1 indicates the end. Giving a total of T1 - T0 added or removed length.
	* The number of frames and keys for the provided length is recalculated according to the current value of UAnimDataModel::FrameRate.
	*
	* @param	Length				Total new play-able length value, has to be positive and non-zero
	* @param	T0					Point between 0 and Length at which the change in time starts
	* @param	T1					Point between 0 and Length at which the change in time ends
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*/
	UFUNCTION(BlueprintCallable, Category = AnimationData)
	void Resize(float Length, float T0, float T1, bool bShouldTransact = true);

	/**
	* Sets the frame rate according to which the bone animation is expected to be sampled. Broadcasts a EAnimDataModelNotifyType::FrameRateChanged notify if successful.
	* The number of frames and keys for the provided frame rate is recalculated according to the current value of UAnimDataModel::PlayLength.
	*
	* @param	FrameRate			The new sampling frame rate, has to be positive and non-zero
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*/
	UFUNCTION(BlueprintCallable, Category = AnimationData)
	void SetFrameRate(FFrameRate FrameRate, bool bShouldTransact = true);

	/**
	* Adds a new bone animation track for the provided name. Broadcasts a EAnimDataModelNotifyType::TrackAdded notify if successful.
	*
	* @param	BoneName			Bone name for which a track should be added
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*
	* @return	The index at which the bone track was added, INDEX_NONE if adding it failed
	*/
	UFUNCTION(BlueprintCallable, Category = AnimationData)
	int32 AddBoneTrack(FName BoneName, bool bShouldTransact = true);

	/**
	* Inserts a new bone animation track for the provided name, at the provided index. Broadcasts a EAnimDataModelNotifyType::TrackAdded notify if successful.
	* The bone name is verified with the AnimModel's outer target USkeleton to ensure the bone exists.
	*
	* @param	BoneName			Bone name for which a track should be inserted
	* @param	DesiredIndex		Index at which the track should be inserted
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*
	* @return	The index at which the bone track was inserted, INDEX_NONE if the insertion failed
	*/
	UFUNCTION(BlueprintCallable, Category = AnimationData)
	int32 InsertBoneTrack(FName BoneName, int32 DesiredIndex, bool bShouldTransact = true);

	/**
	* Removes an existing bone animation track with the provided name. Broadcasts a EAnimDataModelNotifyType::TrackRemoved notify if successful.
	*
	* @param	BoneName			Bone name of the track which should be removed
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*
	* @return	Whether or not the removal was succesful
	*/
	UFUNCTION(BlueprintCallable, Category = AnimationData)
	bool RemoveBoneTrack(FName BoneName, bool bShouldTransact = true);

	/**
	* Removes all existing Bone Animation tracks. Broadcasts a EAnimDataModelNotifyType::TrackRemoved for each removed track, wrapped within BracketOpened/BracketClosed notifies.
	*
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*/
	UFUNCTION(BlueprintCallable, Category = AnimationData)
	void RemoveAllBoneTracks(bool bShouldTransact = true);

	/**
	* Removes an existing bone animation track with the provided name. Broadcasts a EAnimDataModelNotifyType::TrackChanged notify if successful.
	* The provided number of keys provided is expected to match for each component, and be non-zero.
	*
	* @param	BoneName			Bone name of the track for which the keys should be set
	* @param	PositionalKeys		Array of keys for the translation component
	* @param	RotationalKeys		Array of keys for the rotation component
	* @param	ScalingKeys			Array of keys for the scale component
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*
	* @return	Whether or not the keys were succesfully set
	*/
	UFUNCTION(BlueprintCallable, Category = AnimationData)
	bool SetBoneTrackKeys(FName BoneName, const TArray<FVector>& PositionalKeys, const TArray<FQuat>& RotationalKeys, const TArray<FVector>& ScalingKeys, bool bShouldTransact = true);
	
	/**
	* Adds a new curve with the provided information. Broadcasts a EAnimDataModelNotifyType::CurveAdded notify if successful.
	*
	* @param	CurveId				Identifier for the to-be-added curve
	* @param	CurveFlags			Flags to be set for the curve
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*
	* @return	Whether or not the curve was succesfully added
	*/
	UFUNCTION(BlueprintCallable, Category = CurveData)
	bool AddCurve(const FAnimationCurveIdentifier& CurveId, int32 CurveFlags = 0x00000004, bool bShouldTransact = true);

	/**
	* Duplicated the curve with the identifier. Broadcasts a EAnimDataModelNotifyType::CurveAdded notify if successful.
	*
	* @param	CopyCurveId			Identifier for the to-be-duplicated curve
	* @param	NewCurveId			Identifier for the to-be-added curve
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*
	* @return	Whether or not the curve was succesfully duplicated
	*/
	UFUNCTION(BlueprintCallable, Category = CurveData)
	bool DuplicateCurve(const FAnimationCurveIdentifier& CopyCurveId, const FAnimationCurveIdentifier& NewCurveId, bool bShouldTransact = true);
	

	/**
	* Remove the curve with provided identifier. Broadcasts a EAnimDataModelNotifyType::CurveRemoved notify if successful.
	*
	* @param	CopyCurveId			Identifier for the to-be-removed curve
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*
	* @return	Whether or not the curve was succesfully removed
	*/
	UFUNCTION(BlueprintCallable, Category = CurveData)
	bool RemoveCurve(const FAnimationCurveIdentifier& CurveId, bool bShouldTransact = true);

	/**
	* Removes all the curves of the provided type. Broadcasts a EAnimDataModelNotifyType::CurveRemoved for each removed curve, wrapped within BracketOpened/BracketClosed notifies.
	*
	* @param	SupportedCurveType	Type for which all curves are to be removed
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*/
	UFUNCTION(BlueprintCallable, Category = CurveData)
	void RemoveAllCurvesOfType(ERawCurveTrackTypes SupportedCurveType, bool bShouldTransact = true);

	/**
	* Set an individual flag for the curve with provided identifier. Broadcasts a EAnimDataModelNotifyType::CurveFlagsChanged notify if successful.
	*
	* @param	CurveId			    Identifier for the curve for which the flag state is to be set
	* @param	Flag				Flag for which the state is supposed to be set
	* @param	bState				State of the flag to be, true=set/false=not set
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*
	* @return	Whether or not the flag state was succesfully set
	*/
	UFUNCTION(BlueprintCallable, Category = CurveData)
	bool SetCurveFlag(const FAnimationCurveIdentifier& CurveId, EAnimAssetCurveFlags Flag, bool bState = true, bool bShouldTransact = true);

	/**
	* Replace the flags for the curve with provided identifier. Broadcasts a EAnimDataModelNotifyType::CurveFlagsChanged notify if successful.
	*
	* @param	CurveId			    Identifier for the curve for which the flag state is to be set
	* @param	Flags				Flag mask with which the existings flags are to be replaced
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*
	* @return	Whether or not the flag mask was succesfully set
	*/
	UFUNCTION(BlueprintCallable, Category = CurveData)
	bool SetCurveFlags(const FAnimationCurveIdentifier& CurveId, int32 Flags, bool bShouldTransact = true);

	/**
	* Replace the keys for the transform curve with provided identifier. Broadcasts a EAnimDataModelNotifyType::CurveChanged notify if successful.
	*
	* @param	CurveId			    Identifier for the transform curve for which the keys are to be set
	* @param	TransformValues		Transform Values with which the existing values are to be replaced
	* @param	TimeKeys			Time Keys with which the existing keys are to be replaced
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*
	* @return	Whether or not the transform curve keys were succesfully set
	*/
	UFUNCTION(BlueprintCallable, Category = CurveData)
	bool SetTransformCurveKeys(const FAnimationCurveIdentifier& CurveId, const TArray<FTransform>& TransformValues, const TArray<float>& TimeKeys, bool bShouldTransact = true);
		
	/**
	* Sets a single key for the transform curve with provided identifier. Broadcasts a EAnimDataModelNotifyType::CurveChanged notify if successful.
	* In case a key for any of the individual transform channel curves already exists the value is replaced.
	*
	* @param	CurveId			    Identifier for the transform curve for which the key is to be set
	* @param	Time				Time of the key to be set
	* @param	Value				Value of the key to be set
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*
	* @return	Whether or not the transform curve key was succesfully set
	*/
	UFUNCTION(BlueprintCallable, Category = CurveData)
	bool SetTransformCurveKey(const FAnimationCurveIdentifier& CurveId, float Time, const FTransform& Value, bool bShouldTransact = true);

	/**
	* Removes a single key for the transform curve with provided identifier. Broadcasts a EAnimDataModelNotifyType::CurveChanged notify if successful.
	*
	* @param	CurveId			    Identifier for the transform curve for which the key is to be removed
	* @param	Time				Time of the key to be removed
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*
	* @return	Whether or not the transform curve key was succesfully removed
	*/
	UFUNCTION(BlueprintCallable, Category = CurveData)
	bool RemoveTransformCurveKey(const FAnimationCurveIdentifier& CurveId, float Time, bool bShouldTransact = true);

	/**
	* Renames the curve with provided identifier. Broadcasts a EAnimDataModelNotifyType::CurveRenamed notify if successful.
	*
	* @param	CurveToRenameId		Identifier for the curve to be renamed
	* @param	NewCurveId			Time of the key to be removed
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*
	* @return	Whether or not the curve was succesfully renamed
	*/
	UFUNCTION(BlueprintCallable, Category = CurveData)
	bool RenameCurve(const FAnimationCurveIdentifier& CurveToRenameId, const FAnimationCurveIdentifier& NewCurveId, bool bShouldTransact = true);

	/**
	* Changes the color of the curve with provided identifier. Broadcasts a EAnimDataModelNotifyType::CurveRenamed notify if successful.
	* Currently changing curve colors is only supported for float curves.
	*
	* @param	CurveId				Identifier of the curve to change the color for
	* @param	Color				Color to which the curve is to be set
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*
	* @return	Whether or not the curve color was succesfully changed
	*/
	UFUNCTION(BlueprintCallable, Category = CurveData)
	bool SetCurveColor(const FAnimationCurveIdentifier& CurveId, FLinearColor Color, bool bShouldTransact = true);
	
	/**
	* Scales the curve with provided identifier. Broadcasts a EAnimDataModelNotifyType::CurveScaled notify if successful.
	*
	* @param	CurveId				Identifier of the curve to scale
	* @param	Origin				Time to use as the origin when scaling the curve
	* @param	Factor				Factor with which the curve is supposed to be scaled
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*
	* @return	Whether or not scaling the curve was succesful
	*/
	UFUNCTION(BlueprintCallable, Category = CurveData)
	bool ScaleCurve(const FAnimationCurveIdentifier& CurveId, float Origin, float Factor, bool bShouldTransact = true);

	/**
	* Sets a single key for the curve with provided identifier and name. Broadcasts a EAnimDataModelNotifyType::CurveChanged notify if successful.
	* In case a key for the provided key time already exists the key is replaced.
	*
	* @param	CurveId			    Identifier for the curve for which the key is to be set
	* @param	Key					Key to be set
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*
	* @return	Whether or not the curve key was succesfully set
	*/
	UFUNCTION(BlueprintCallable, Category = CurveData)
	bool SetCurveKey(const FAnimationCurveIdentifier& CurveId, const FRichCurveKey& Key, bool bShouldTransact = true);
	
	/**
	* Remove a single key from the curve with provided identifier and name. Broadcasts a EAnimDataModelNotifyType::CurveChanged notify if successful.
	*
	* @param	CurveId			    Identifier for the curve for which the key is to be removed
	* @param	Time				Time of the key to be removed
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*
	* @return	Whether or not the curve key was succesfully removed
	*/
	UFUNCTION(BlueprintCallable, Category = CurveData)
	bool RemoveCurveKey(const FAnimationCurveIdentifier& CurveId, float Time, bool bShouldTransact = true);

	/**
	* Replace the keys for the curve with provided identifier and name. Broadcasts a EAnimDataModelNotifyType::CurveChanged notify if successful.
	*
	* @param	CurveId			    Identifier for the curve for which the keys are to be replaced
	* @param	CurveKeys			Keys with which the existing keys are to be replaced
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*
	* @return	Whether or not replacing curve keys was succesful
	*/
	UFUNCTION(BlueprintCallable, Category = CurveData)
	bool SetCurveKeys(const FAnimationCurveIdentifier& CurveId, const TArray<FRichCurveKey>& CurveKeys, bool bShouldTransact = true);


	/**
	* Updates the display name values for any stored curve, with the names being retrieved from the provided skeleton. Broadcasts a EAnimDataModelNotifyType::CurveRenamed for each to-be-updated curve name, wrapped within BracketOpened/BracketClosed notifies.
	*
	* @param	Skeleton			Skeleton to retrieve the display name values from
	* @param	SupportedCurveType	Curve type for which the names should be updated
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*/
	UFUNCTION(BlueprintCallable, Category = CurveData)
	void UpdateCurveNamesFromSkeleton(const USkeleton* Skeleton, ERawCurveTrackTypes SupportedCurveType, bool bShouldTransact = true);

	/**
	* Updates the curve names with the provided skeleton, if a display name is not found it will be added thus modifying the skeleton. Broadcasts a EAnimDataModelNotifyType::CurveRenamed for each curve name for which the UID was different or if it was added as a new smartname, wrapped within BracketOpened/BracketClosed notifies.
	*
	* @param	Skeleton			Skeleton to retrieve the display name values from
	* @param	SupportedCurveType	Curve type for which the names should be updated
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*/
	UFUNCTION(BlueprintCallable, Category = CurveData)
	void FindOrAddCurveNamesOnSkeleton(USkeleton* Skeleton, ERawCurveTrackTypes SupportedCurveType, bool bShouldTransact = true);

	/**
	* Removes any bone track for which the name was not found in the provided skeleton. Broadcasts a EAnimDataModelNotifyType::TrackRemoved for each track which was not found in the skeleton, wrapped within BracketOpened/BracketClosed notifies.
	*
	* @param	Skeleton			Skeleton to retrieve the display name values from
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*/
	bool RemoveBoneTracksMissingFromSkeleton(const USkeleton* Skeleton, bool bShouldTransact = true);

	/**
	* Broadcast a EAnimDataModelNotifyType::Populated notify.
	*/
	void NotifyPopulated();	

	/**
	* Resets all data stored in the model, broadcasts a EAnimDataModelNotifyType::Reset and wraps all actions within BracketOpened/BracketClosed notifies.
	*	- Bone tracks, broadcasts a EAnimDataModelNotifyType::TrackRemoved for each
	*	- Curves, broadcasts a EAnimDataModelNotifyType::CurveRemoves for each
	*	- Play length to one frame at 30fps, broadcasts a EAnimDataModelNotifyType::PlayLengthChanged
	*	- Frame rate to 30fps, broadcasts a EAnimDataModelNotifyType::FrameRateChanged
	*
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*/
	void ResetModel(bool bShouldTransact = true);

	/**
	* Adds a new attribute with the provided information. Broadcasts a EAnimDataModelNotifyType::AttributeAdded notify if successful.
	*
	* @param	AttributeIdentifier		Identifier for the to-be-added attribute
	* @param	bShouldTransact			Whether or not any undo-redo changes should be generated
	*
	* @return	Whether or not the attribute was succesfully added
	*/
	UFUNCTION(BlueprintCallable, Category = CurveData)
	bool AddAttribute(const FAnimationAttributeIdentifier& AttributeIdentifier, bool bShouldTransact = true);

	/**
	* Removes an attribute, if found, with the provided information. Broadcasts a EAnimDataModelNotifyType::AttributeRemoved notify if successful.
	*
	* @param	AttributeIdentifier		Identifier for the to-be-removed attribute
	* @param	bShouldTransact			Whether or not any undo-redo changes should be generated
	*
	* @return	Whether or not the attribute was succesfully removed
	*/
	UFUNCTION(BlueprintCallable, Category = CurveData)
	bool RemoveAttribute(const FAnimationAttributeIdentifier& AttributeIdentifier, bool bShouldTransact = true);

	/**
	* Removes all attributes for the specified bone name, if any. Broadcasts a EAnimDataModelNotifyType::AttributeRemoved notify for each removed attribute.
	*
	* @param	BoneName			Name of the bone to remove attributes for
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*
	* @return	Total number of removes attributes
	*/
	UFUNCTION(BlueprintCallable, Category = CurveData)
	int32 RemoveAllAttributesForBone(const FName& BoneName, bool bShouldTransact = true);

	/**
	* Removes all stored attributes. Broadcasts a EAnimDataModelNotifyType::AttributeRemoved notify for each removed attribute.
	*
	* @param	bShouldTransact		Whether or not any undo-redo changes should be generated
	*
	* @return	Total number of removes attributes
	*/
	UFUNCTION(BlueprintCallable, Category = CurveData)
	int32 RemoveAllAttributes(bool bShouldTransact = true);	

	/**
	* Sets a single key for the attribute with provided identifier. Broadcasts a EAnimDataModelNotifyType::AttributeChanged notify if successful.
	* In case a key for the provided key time already exists the key is replaced.
	*
	* @param	AttributeIdentifier		Identifier for the attribute for which the key is to be set
	* @param	Time					Time of the to-be-set key
	* @param	KeyValue				Value (templated) of the to-be-set key
	* @param	bShouldTransact			Whether or not any undo-redo changes should be generated
	*
	* @return	Whether or not the key was succesfully set
	*/
	template<typename AttributeType>
	bool SetTypedAttributeKey(const FAnimationAttributeIdentifier& AttributeIdentifier, float Time, AttributeType& KeyValue, bool bShouldTransact = true)
	{
		return SetAttributeKey_Internal(AttributeIdentifier, Time, (const void*)&KeyValue, AttributeType::StaticStruct(), bShouldTransact);
	}

	/**
	* Sets a single key for the attribute with provided identifier. Broadcasts a EAnimDataModelNotifyType::AttributeChanged notify if successful.
	* In case a key for the provided key time already exists the key is replaced.
	*
	* @param	AttributeIdentifier		Identifier for the attribute for which the key is to be set
	* @param	Time					Time of the to-be-set key
	* @param	KeyValue				Value of the to-be-set key
	* @param	bShouldTransact			Whether or not any undo-redo changes should be generated
	*
	* @return	Whether or not the key was succesfully set
	*/
	bool SetAttributeKey(const FAnimationAttributeIdentifier& AttributeIdentifier, float Time, const void* KeyValue, bool bShouldTransact = true)
	{
		return SetAttributeKey_Internal(AttributeIdentifier, Time, KeyValue, AttributeIdentifier.GetType(), bShouldTransact);
	}
	
	/**
	* Replace the keys for the attribute with provided identifier. Broadcasts a EAnimDataModelNotifyType::AttributeChanged notify if successful.
	*
	* @param	AttributeIdentifier		Identifier for the attribute for which the keys are to be replaced
	* @param	Times					Times with which the existing key timings are to be replaced
	* @param	KeyValues				Values with which the existing key values are to be replaced
	* @param	bShouldTransact			Whether or not any undo-redo changes should be generated
	*
	* @return	Whether or not replacing the attribute keys was succesful
	*/
	bool SetAttributeKeys(const FAnimationAttributeIdentifier& AttributeIdentifier, TArrayView<const float> Times, TArrayView<const void*> KeyValues, bool bShouldTransact = true)
	{
		return SetAttributeKeys_Internal(AttributeIdentifier, Times, KeyValues, AttributeIdentifier.GetType(), bShouldTransact);
	}

	/**
	* Replace the keys for the attribute with provided identifier. Broadcasts a EAnimDataModelNotifyType::AttributeChanged notify if successful.
	*
	* @param	AttributeIdentifier		Identifier for the attribute for which the keys are to be replaced
	* @param	Times					Times with which the existing key timings are to be replaced
	* @param	KeyValues				Values (templated) with which the existing key values are to be replaced
	* @param	bShouldTransact			Whether or not any undo-redo changes should be generated
	*
	* @return	Whether or not replacing the attribute keys was succesful
	*/
	template<typename AttributeType>
	bool SetTypedAttributeKeys(const FAnimationAttributeIdentifier& AttributeIdentifier, TArrayView<const float> Times, TArrayView<AttributeType> KeyValues, bool bShouldTransact = true)
	{		
		TArray<const void*> KeyValuePtrs;
		Algo::Transform(KeyValues, KeyValuePtrs, [](const AttributeType& Value)
		{
			return (const void*)&Value;
		});

		return SetAttributeKeys_Internal(AttributeIdentifier, Times, MakeArrayView(KeyValuePtrs), AttributeType::StaticStruct(), bShouldTransact);
	}

	/**
	* Remove a single key from the attribute with provided identifier. Broadcasts a EAnimDataModelNotifyType::AttributeChanged notify if successful.
	*
	* @param	AttributeIdentifier		Identifier for the attribute from which the key is to be removed
	* @param	Time					Time of the key to be removed
	* @param	bShouldTransact			Whether or not any undo-redo changes should be generated
	*
	* @return	Whether or not the attribute key was succesfully removed
	*/
	UFUNCTION(BlueprintCallable, Category = CurveData)
	bool RemoveAttributeKey(const FAnimationAttributeIdentifier& AttributeIdentifier, float Time, bool bShouldTransact = true);
protected:
	/** Functionality used by FOpenBracketAction and FCloseBracketAction to broadcast their equivalent notifies without actually opening a bracket. */
	void NotifyBracketOpen();
	void NotifyBracketClosed();

private:
	/** Internal functionality for setting Attribute curve key(s) */
	bool SetAttributeKey_Internal(const FAnimationAttributeIdentifier& AttributeIdentifier, float Time, const void* KeyValue, const UScriptStruct* TypeStruct, bool bShouldTransact = true);
	bool SetAttributeKeys_Internal(const FAnimationAttributeIdentifier& AttributeIdentifier, TArrayView<const float> Times, TArrayView<const void*> KeyValues, const UScriptStruct* TypeStruct, bool bShouldTransact = true);

	/** Returns whether or not the supplied curve type is supported by the controller functionality */
	const bool IsSupportedCurveType(ERawCurveTrackTypes CurveType) const;
	/** Returns the string representation of the provided curve enum type value */
	FString GetCurveTypeValueName(ERawCurveTrackTypes InType) const;
	
	/** Resizes the curve/attribute data stored on the model according to the provided new length and time at which to insert or remove time */
	void ResizeCurves(float NewLength, bool bInserted, float T0, float T1, bool bShouldTransact = true);
	void ResizeAttributes(float NewLength, bool bInserted, float T0, float T1, bool bShouldTransact = true);

	/** Ensures that a valid model is currently targeted */
	void ValidateModel() const;

	/** Verifies whether or not the Model's outer object is (or is derived from) the specified UClass */
	bool CheckOuterClass(UClass* InClass) const;

	/** Helper functionality to output script-based warnings and errors */
	void ReportWarning(const FText& InMessage) const;
	void ReportError(const FText& InMessage) const;

	template <typename FmtType, typename... Types>
	void ReportWarningf(const FmtType& Fmt, Types... Args) const
	{	
		ReportWarning(FText::Format(Fmt, Args...));
	}

	template <typename FmtType, typename... Types>
	void ReportErrorf(const FmtType& Fmt, Types... Args) const
	{
		ReportError(FText::Format(Fmt, Args...));
	}
#endif // WITH_EDITOR

private: 
#if WITH_EDITOR
	/** Current depth of outstanding brackets, assumed to remain positive */
	int32 BracketDepth;

	/** Transactor used to store Change based undo/redo action in the engines transaction buffer */
	UE::FChangeTransactor ChangeTransactor;	
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
	/** Current UAnimDataModel instance targeted by this controller */
	UPROPERTY(transient)
	TObjectPtr<UAnimDataModel> Model;
#endif // WITH_EDITORONLY_DATA

	friend class FAnimDataControllerTestBase;
	friend UE::Anim::FOpenBracketAction;
	friend UE::Anim::FCloseBracketAction;
};
