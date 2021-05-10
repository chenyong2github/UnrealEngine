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
#include "Compilation/MovieSceneTemplateInterrogation.h"
#include "Channels/MovieSceneIntegerChannel.h"
#include "Channels/MovieSceneByteChannel.h"


#include "MovieSceneControlRigParameterSection.generated.h"

class UAnimSequence;

struct CONTROLRIG_API FControlRigBindingHelper
{
	static void BindToSequencerInstance(UControlRig* ControlRig);
	static void UnBindFromSequencerInstance(UControlRig* ControlRig);
};

struct FEnumParameterNameAndValue //uses uint8
{
	FEnumParameterNameAndValue(FName InParameterName, uint8 InValue)
	{
		ParameterName = InParameterName;
		Value = InValue;
	}

	FName ParameterName;

	uint8 Value;
};

struct FIntegerParameterNameAndValue
{
	FIntegerParameterNameAndValue(FName InParameterName, int32 InValue)
	{
		ParameterName = InParameterName;
		Value = InValue;
	}

	FName ParameterName;

	int32 Value;
};

USTRUCT()
struct CONTROLRIG_API FEnumParameterNameAndCurve
{
	GENERATED_USTRUCT_BODY()

	FEnumParameterNameAndCurve()
	{}

	FEnumParameterNameAndCurve(FName InParameterName);

	UPROPERTY()
	FName ParameterName;

	UPROPERTY()
	FMovieSceneByteChannel ParameterCurve;
};


USTRUCT()
struct CONTROLRIG_API FIntegerParameterNameAndCurve
{
	GENERATED_USTRUCT_BODY()

	FIntegerParameterNameAndCurve()
	{}
	FIntegerParameterNameAndCurve(FName InParameterName);

	UPROPERTY()
	FName ParameterName;

	UPROPERTY()
	FMovieSceneIntegerChannel ParameterCurve;
};

/**
*  Data that's queried during an interrogtion
*/
struct FFloatInterrogationData
{
	float Val;
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

	FChannelMapInfo(int32 InControlIndex, int32 InTotalChannelIndex,  int32 InChannelIndex, int32 InParentControlIndex = INDEX_NONE, FName InChannelTypeName = NAME_None) :
		ControlIndex(InControlIndex),TotalChannelIndex(InTotalChannelIndex), ChannelIndex(InChannelIndex), ParentControlIndex(InParentControlIndex), ChannelTypeName(InChannelTypeName) {};
	UPROPERTY()
	int32 ControlIndex = 0;
	UPROPERTY()
	int32 TotalChannelIndex = 0;
	UPROPERTY()
	int32 ChannelIndex = 0; //channel index for it's type.. (e.g  float, int, bool).
	UPROPERTY()
	int32 ParentControlIndex = 0;
	UPROPERTY()
	FName ChannelTypeName; 


};

/**
 * Movie scene section that controls animation controller animation
 */
UCLASS()
class CONTROLRIG_API UMovieSceneControlRigParameterSection : public UMovieSceneParameterSection
{
	GENERATED_BODY()

public:

	void AddEnumParameterKey(FName InParameterName, FFrameNumber InTime, uint8 InValue);
	void AddIntegerParameterKey(FName InParameterName, FFrameNumber InTime, int32 InValue);

	bool RemoveEnumParameter(FName InParameterName);
	bool RemoveIntegerParameter(FName InParameterName);

	TArray<FEnumParameterNameAndCurve>& GetEnumParameterNamesAndCurves();
	const TArray<FEnumParameterNameAndCurve>& GetEnumParameterNamesAndCurves() const;

	TArray<FIntegerParameterNameAndCurve>& GetIntegerParameterNamesAndCurves();
	const TArray<FIntegerParameterNameAndCurve>& GetIntegerParameterNamesAndCurves() const;

private:

	/** Control Rig that controls us*/
	UPROPERTY()
	UControlRig* ControlRig;

public:

	/** The class of control rig to instantiate */
	UPROPERTY(EditAnywhere, Category = "Animation")
	TSubclassOf<UControlRig> ControlRigClass;

	/** Mask for controls themselves*/
	UPROPERTY()
	TArray<bool> ControlsMask;

	/** Mask for Transform Mask*/
	UPROPERTY()
	FMovieSceneTransformMask TransformMask;

	/** The weight curve for this animation controller section */
	UPROPERTY()
	FMovieSceneFloatChannel Weight;

	/** Map from the control name to where it starts as a channel*/
	UPROPERTY()
	TMap<FName, FChannelMapInfo> ControlChannelMap;

protected:
	/** Enum Curves*/
	UPROPERTY()
	TArray<FEnumParameterNameAndCurve> EnumParameterNamesAndCurves;

	/*Integer Curves*/
	UPROPERTY()
	TArray<FIntegerParameterNameAndCurve> IntegerParameterNamesAndCurves;
private:
	/** Copy of Mask for controls, checked when reconstructing*/
	TArray<bool> OldControlsMask;



public:

	UMovieSceneControlRigParameterSection();

	virtual void SetBlendType(EMovieSceneBlendType InBlendType) override;
#if WITH_EDITOR
	//Function to save control rig key when recording.
	void RecordControlRigKey(FFrameNumber FrameNumber, bool bSetDefault, bool bDoAutoKey);

	//Function to load an Anim Sequence into this section. It will automatically reszie to the section size.
	//Will return false if fails or is canceled
	virtual bool LoadAnimSequenceIntoThisSection(UAnimSequence* Sequence, UMovieScene* MovieScene, USkeleton* Skeleton,
		bool bKeyReduce, float Tolerance, FFrameNumber InStartFrame = 0);
#endif
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

	/** Recreate with this Control Rig*/
	void RecreateWithThisControlRig(UControlRig* InControlRig, bool bSetDefault);

	/* Set the control rig for this section */
	void SetControlRig(UControlRig* InControlRig);
	/* Get the control rig for this section */
	UControlRig* GetControlRig() const { return ControlRig; }

	/** Whether or not to key currently, maybe evaluating so don't*/
	void  SetDoNotKey(bool bIn) const { bDoNotKey = bIn; }
	/** Get Whether to key or not*/
	bool GetDoNotKey() const { return bDoNotKey; }

	/**  Whether or not this section his scalar*/
	bool HasScalarParameter(FName InParameterName) const;

	/**  Whether or not this section his bool*/
	bool HasBoolParameter(FName InParameterName) const;

	/**  Whether or not this section his enum*/
	bool HasEnumParameter(FName InParameterName) const;

	/**  Whether or not this section his int*/
	bool HasIntegerParameter(FName InParameterName) const;

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

	/** Adds specified enum parameter. */
	void AddEnumParameter(FName InParameterName, UEnum* Enum,TOptional<uint8> DefaultValue, bool bReconstructChannel);

	/** Adds specified int parameter. */
	void AddIntegerParameter(FName InParameterName, TOptional<int32> DefaultValue, bool bReconstructChannel);

	/** Adds a a key for a specific vector parameter. */
	void AddVectorParameter(FName InParameterName, TOptional<FVector> DefaultValue, bool bReconstructChannel);

	/** Adds a a key for a specific vector2D parameter. */
	void AddVector2DParameter(FName InParameterName, TOptional<FVector2D> DefaultValue, bool bReconstructChannel);

	/** Adds a a key for a specific color parameter. */
	void AddColorParameter(FName InParameterName, TOptional<FLinearColor> DefaultValue, bool bReconstructChannel);

	/** Adds a a key for a specific color parameter*/
	void AddTransformParameter(FName InParameterName, TOptional<FTransform> DefaultValue, bool bReconstructChannel);

	/** Clear Everything Out*/
	void ClearAllParameters();
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
	// which forces a sequencer eval, not like the editor where changes are only set on UI changes(changing time doesn't send change delegate)
	mutable bool bDoNotKey;

public:
	/** Special list of Names that we should only Modify. Needed to handle Interaction (FK/IK) since Control Rig expecting only changed value to be set
	not all Controls*/
	mutable TSet<FName> ControlsToSet;
};
