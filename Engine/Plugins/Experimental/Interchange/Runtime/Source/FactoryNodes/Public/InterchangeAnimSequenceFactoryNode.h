// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeFactoryBaseNode.h"
#include "Animation/AnimSequence.h"
#include "Misc/FrameRate.h"
#include "InterchangeAnimSequenceFactoryNode.generated.h"

namespace UE::Interchange::Animation
{
	INTERCHANGEFACTORYNODES_API FFrameRate ConvertSampleRatetoFrameRate(double SampleRate);
}

UCLASS(BlueprintType, Experimental)
class INTERCHANGEFACTORYNODES_API UInterchangeAnimSequenceFactoryNode : public UInterchangeFactoryBaseNode
{
	GENERATED_BODY()

public:
	UInterchangeAnimSequenceFactoryNode();

	/**
	 * Initialize node data
	 * @param UniqueID - The uniqueId for this node
	 * @param DisplayLabel - The name of the node
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | AnimSequence")
	void InitializeAnimSequenceNode(const FString& UniqueID, const FString& DisplayLabel);

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const override;

	virtual FString GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const override;

	/** Get the class this node want to create */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | AnimSequence")
	virtual class UClass* GetObjectClass() const override;

public:
	/** Return false if the Attribute was not set previously. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | AnimSequence")
	bool GetCustomSkeletonFactoryNodeUid(FString& AttributeValue) const;

	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | AnimSequence")
	bool SetCustomSkeletonFactoryNodeUid(const FString& AttributeValue);

	/** Query whether we must import the bone tracks. Return false if the Attribute was not set previously. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | AnimSequence")
	bool GetCustomImportBoneTracks(bool& AttributeValue) const;

	/** Set true if we must import the bone tracks, otherwise set it to false. Return false if the Attribute was not set previously. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | AnimSequence")
	bool SetCustomImportBoneTracks(const bool& AttributeValue);

	/** Query weather we must import the bone tracks. Return false if the Attribute was not set previously. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | AnimSequence")
	bool GetCustomImportBoneTracksSampleRate(double& AttributeValue) const;

	/** Set true if we must import the bone tracks, otherwise set it to false. Return false if the Attribute was not set previously. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | AnimSequence")
	bool SetCustomImportBoneTracksSampleRate(const double& AttributeValue);

	/** Query weather we must import the bone tracks. Return false if the Attribute was not set previously. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | AnimSequence")
	bool GetCustomImportBoneTracksRangeStart(double& AttributeValue) const;

	/** Set true if we must import the bone tracks, otherwise set it to false. Return false if the Attribute was not set previously. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | AnimSequence")
	bool SetCustomImportBoneTracksRangeStart(const double& AttributeValue);

	/** Query weather we must import the bone tracks. Return false if the Attribute was not set previously. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | AnimSequence")
	bool GetCustomImportBoneTracksRangeStop(double& AttributeValue) const;

	/** Set true if we must import the bone tracks, otherwise set it to false. Return false if the Attribute was not set previously. */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | AnimSequence")
	bool SetCustomImportBoneTracksRangeStop(const double& AttributeValue);

	/**
	 * Query the optional existing USkeleton this anim must use. The animsequence factory will use this skeleton instead of the imported one
	 * (from GetCustomSkeletonFactoryNodeUid) if this attribute is set and the skeleton pointer is valid.
	 * Pipeline set this attribute in case the user want to specify an existing skeleton.
	 * Return false if the attribute was not set.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | AnimSequence")
	bool GetCustomSkeletonSoftObjectPath(FSoftObjectPath& AttributeValue) const;

	/**
	 * Set the optional existing USkeleton this anim must use. The AnimSequence factory will use this skeleton instead of the imported one
	 * (from GetCustomSkeletonFactoryNodeUid) if this attribute is set and the skeleton pointer is valid.
	 * Pipeline set this attribute in case the user want to specify an existing skeleton.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node | AnimSequence")
	bool SetCustomSkeletonSoftObjectPath(const FSoftObjectPath& AttributeValue);

private:
	const UE::Interchange::FAttributeKey Macro_CustomSkeletonFactoryNodeUidKey = UE::Interchange::FAttributeKey(TEXT("SkeletonFactoryNodeUid"));
	
	const UE::Interchange::FAttributeKey Macro_CustomImportBoneTracksKey = UE::Interchange::FAttributeKey(TEXT("ImportBoneTracks"));
	const UE::Interchange::FAttributeKey Macro_CustomImportBoneTracksSampleRateKey = UE::Interchange::FAttributeKey(TEXT("ImportBoneTracksSampleRate"));
	const UE::Interchange::FAttributeKey Macro_CustomImportBoneTracksRangeStartKey = UE::Interchange::FAttributeKey(TEXT("ImportBoneTracksRangeStart"));
	const UE::Interchange::FAttributeKey Macro_CustomImportBoneTracksRangeStopKey = UE::Interchange::FAttributeKey(TEXT("ImportBoneTracksRangeStop"));

	const UE::Interchange::FAttributeKey Macro_CustomSkeletonSoftObjectPathKey = UE::Interchange::FAttributeKey(TEXT("SkeletonSoftObjectPath"));
};
