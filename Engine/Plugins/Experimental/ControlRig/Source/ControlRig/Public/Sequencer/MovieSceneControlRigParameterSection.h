// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Sections/MovieSceneParameterSection.h"
#include "UObject/ObjectMacros.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Sections/MovieSceneSubSection.h"
#include "ControlRig.h"
#include "MovieSceneSequencePlayer.h"
#include "Animation/AnimData/BoneMaskFilter.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieSceneControlRigParameterSection.generated.h"


class UMovieSceneControlRigSection;
/**
*  Data that's queried during an interrogtion
*/
struct FFloatInterrogationData
{
	float Val;
	FName ParameterName;
};

struct FBoolInterrogationData
{
	bool Val;
	FName ParameterName;
};

struct FVector2DInterrogationData
{
	FVector2D Val;
	FName ParameterName;
};

struct FVectorInterrogationData
{
	FVector Val;
	FName ParameterName;
};

struct FTransformInterrogationData
{
	FTransform Val;
	FName ParameterName;
};

USTRUCT()
struct CONTROLRIG_API FChannelMapInfo
{
	GENERATED_USTRUCT_BODY()

	FChannelMapInfo() = default;

	FChannelMapInfo(int32 InControlIndex, int32 InChannelIndex) : ControlIndex(InControlIndex), ChannelIndex(InChannelIndex) {};
	UPROPERTY()
	int32 ControlIndex;
	UPROPERTY()
	int32 ChannelIndex;
};

/**
 * Movie scene section that controls animation controller animation
 */
UCLASS()
class CONTROLRIG_API UMovieSceneControlRigParameterSection : public UMovieSceneParameterSection
{
	GENERATED_BODY()

public:

	/** Control Rig that controls us*/
	UPROPERTY()
	UControlRig* ControlRig;

	/** Mask for controls themselves*/
	UPROPERTY()
	TArray<bool> ControlsMask;

	/** Mask for Transform Mask*/
	UPROPERTY()
	FMovieSceneTransformMask TransformMask;

	/** Blend this track in additively (using the reference pose as a base) */
	UPROPERTY(EditAnywhere, Category = "Animation")
	bool bAdditive;

	/** Only apply bones that are in the filter */
	UPROPERTY(EditAnywhere, Category = "Animation")
	bool bApplyBoneFilter;

	/** Per-bone filter to apply to our animation */
	UPROPERTY(EditAnywhere, Category = "Animation", meta=(EditCondition=bApplyBoneFilter))
	FInputBlendPose BoneFilter;

	/** The weight curve for this animation controller section */
	UPROPERTY()
	FMovieSceneFloatChannel Weight;

	/** Map from the control name to where it starts as a channel*/
	UPROPERTY()
	TMap<FName, FChannelMapInfo> ControlChannelMap;

private:
	/** Copy of Mask for controls, checked when reconstructing*/
	TArray<bool> OldControlsMask;

public:

	UMovieSceneControlRigParameterSection();

	const TArray<bool>& GetControlsMask() const
	{
		return ControlsMask;
	}

	bool GetControlsMask(int32 Index) const
	{
		if (Index >= 0 && Index < ControlsMask.Num())
		{
			return ControlsMask[Index];
		}
		return false;
	}

	void SetControlsMask(const TArray<bool>& InMask)
	{
		ControlsMask = InMask;
		ReconstructChannelProxy(true);
	}

	void SetControlsMask(int32 Index, bool Val)
	{
		if (Index >= 0 && Index < ControlsMask.Num())
		{
			ControlsMask[Index] = Val;
		}
		ReconstructChannelProxy(true);
	}


	void FillControlsMask(bool Val)
	{
		ControlsMask.Init(Val, ControlsMask.Num());
		ReconstructChannelProxy(true);

	}

	/**
	* Access the transform mask that defines which channels this track should animate
	*/
	FMovieSceneTransformMask GetTransformMask() const
	{
		return TransformMask;
	}

	/**
	 * Set the transform mask that defines which channels this track should animate
	 */
	void SetTransformMask(FMovieSceneTransformMask NewMask)
	{
		TransformMask = NewMask;
		ReconstructChannelProxy(true);
	}

public:

	/** Whether or not to key currently, maybe evaluating so don't*/
	void  SetDoNotKey(bool bIn) const { bDoNotKey = bIn; }
	/** Get Whether to key or not*/
	bool GetDoNotKey() const { return bDoNotKey; }

	/**  Whether or not this section his scalar*/
	bool HasScalarParameter(FName InParameterName) const;

	/**  Whether or not this section his bool*/
	bool HasBoolParameter(FName InParameterName) const;

	/**  Whether or not this section his scalar*/
	bool HasVector2DParameter(FName InParameterName) const;

	/**  Whether or not this section his scalar*/
	bool HasVectorParameter(FName InParameterName) const;

	/**  Whether or not this section his scalar*/
	bool HasColorParameter(FName InParameterName) const;

	/**  Whether or not this section his scalar*/
	bool HasTransformParameter(FName InParameterName) const;

	/** Adds specified scalar parameter. */
	void AddScalarParameter(FName InParameterName,  TOptional<float> DefaultValue, bool bReconstructChannel);

	/** Adds specified bool parameter. */
	void AddBoolParameter(FName InParameterName, TOptional<bool> DefaultValue, bool bReconstructChannel);

	/** Adds a a key for a specific vector parameter. */
	void AddVectorParameter(FName InParameterName, TOptional<FVector> DefaultValue, bool bReconstructChannel);

	/** Adds a a key for a specific vector2D parameter. */
	void AddVector2DParameter(FName InParameterName, TOptional<FVector2D> DefaultValue, bool bReconstructChannel);

	/** Adds a a key for a specific color parameter. */
	void AddColorParameter(FName InParameterName, TOptional<FLinearColor> DefaultValue, bool bReconstructChannel);

	/** Adds a a key for a specific color parameter*/
	void AddTransformParameter(FName InParameterName, TOptional<FTransform> DefaultValue, bool bReconstructChannel);

public:
	/**
	* Access the interrogation key for control rig data 
	*/
	 static FMovieSceneInterrogationKey GetFloatInterrogationKey();
	 static FMovieSceneInterrogationKey GetVector2DInterrogationKey();
	 static FMovieSceneInterrogationKey GetVector4InterrogationKey();
	 static FMovieSceneInterrogationKey GetVectorInterrogationKey();
	 static FMovieSceneInterrogationKey GetTransformInterrogationKey();

	virtual void ReconstructChannelProxy(bool bForce ) override;

protected:

	//~ UMovieSceneSection interface
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostEditImport() override;
	virtual void PostLoad() override;
	virtual float GetTotalWeightValue(FFrameTime InTime) const override;


	// When true we do not set a key on the section, since it will be set because we changed the value
	// We need this because control rig notifications are set on every change even when just changing sequencer time
	// which forces a sequencer eval, not like the edito where changes are only set on UI changes(changing time doesn't send change delegate)
	mutable bool bDoNotKey;

};
