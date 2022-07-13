// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Components/SceneComponent.h"
#include "Curves/KeyHandle.h"
#include "MovieSceneSection.h"
#include "MovieSceneKeyStruct.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneSectionChannelOverrideRegistry.h"
#include "EntitySystem/IMovieSceneEntityProvider.h"
#include "TransformData.h"
#include "Misc/LargeWorldCoordinates.h"
#include "ConstraintsManager.h"
#include "ConstraintChannel.h"
#include "MovieScene3DTransformSection.generated.h"

#if WITH_EDITORONLY_DATA
/** Visibility options for 3d trajectory. */
UENUM()
enum class EShow3DTrajectory : uint8
{
	EST_OnlyWhenSelected UMETA(DisplayName="Only When Selected"),
	EST_Always UMETA(DisplayName="Always"),
	EST_Never UMETA(DisplayName="Never"),
};
#endif


/**
 * Proxy structure for translation keys in 3D transform sections.
 */
USTRUCT()
struct FMovieScene3DLocationKeyStruct
	: public FMovieSceneKeyStruct
{
	GENERATED_BODY()

	/** The key's translation value. */
	UPROPERTY(EditAnywhere, Category=Key)
	FVector Location = FVector::ZeroVector;

	/** The key's time. */
	UPROPERTY(EditAnywhere, Category=Key)
	FFrameNumber Time;

	FMovieSceneKeyStructHelper KeyStructInterop;

	virtual void PropagateChanges(const FPropertyChangedEvent& ChangeEvent) override;
};
template<> struct TStructOpsTypeTraits<FMovieScene3DLocationKeyStruct> : public TStructOpsTypeTraitsBase2<FMovieScene3DLocationKeyStruct> { enum { WithCopy = false }; };


/**
 * Proxy structure for translation keys in 3D transform sections.
 */
USTRUCT()
struct FMovieScene3DRotationKeyStruct
	: public FMovieSceneKeyStruct
{
	GENERATED_BODY()

	/** The key's rotation value. */
	UPROPERTY(EditAnywhere, Category=Key)
	FRotator Rotation = FRotator::ZeroRotator;

	/** The key's time. */
	UPROPERTY(EditAnywhere, Category=Key)
	FFrameNumber Time;

	FMovieSceneKeyStructHelper KeyStructInterop;

	virtual void PropagateChanges(const FPropertyChangedEvent& ChangeEvent) override;
};
template<> struct TStructOpsTypeTraits<FMovieScene3DRotationKeyStruct> : public TStructOpsTypeTraitsBase2<FMovieScene3DRotationKeyStruct> { enum { WithCopy = false }; };

/**
 * Proxy structure for translation keys in 3D transform sections.
 */
USTRUCT()
struct FMovieScene3DScaleKeyStruct
	: public FMovieSceneKeyStruct
{
	GENERATED_BODY()

	/** The key's scale value. */
	UPROPERTY(EditAnywhere, Category=Key)
	FVector3f Scale = FVector3f::OneVector;

	/** The key's time. */
	UPROPERTY(EditAnywhere, Category=Key)
	FFrameNumber Time;

	FMovieSceneKeyStructHelper KeyStructInterop;

	virtual void PropagateChanges(const FPropertyChangedEvent& ChangeEvent) override;
};
template<> struct TStructOpsTypeTraits<FMovieScene3DScaleKeyStruct> : public TStructOpsTypeTraitsBase2<FMovieScene3DScaleKeyStruct> { enum { WithCopy = false }; };


/**
 * Proxy structure for 3D transform section key data.
 */
USTRUCT()
struct FMovieScene3DTransformKeyStruct
	: public FMovieSceneKeyStruct
{
	GENERATED_BODY()

	/** The key's translation value. */
	UPROPERTY(EditAnywhere, Category=Key)
	FVector Location = FVector::ZeroVector;

	/** The key's rotation value. */
	UPROPERTY(EditAnywhere, Category=Key)
	FRotator Rotation = FRotator::ZeroRotator;

	/** The key's scale value. */
	UPROPERTY(EditAnywhere, Category=Key)
	FVector3f Scale = FVector3f::OneVector;

	/** The key's time. */
	UPROPERTY(EditAnywhere, Category=Key)
	FFrameNumber Time;

	FMovieSceneKeyStructHelper KeyStructInterop;

	virtual void PropagateChanges(const FPropertyChangedEvent& ChangeEvent) override;
};
template<> struct TStructOpsTypeTraits<FMovieScene3DTransformKeyStruct> : public TStructOpsTypeTraitsBase2<FMovieScene3DTransformKeyStruct> { enum { WithCopy = false }; };

enum class EMovieSceneTransformChannel : uint32
{
	None			= 0x000,

	TranslationX 	= 0x001,
	TranslationY 	= 0x002,
	TranslationZ 	= 0x004,
	Translation 	= TranslationX | TranslationY | TranslationZ,

	RotationX 		= 0x008,
	RotationY 		= 0x010,
	RotationZ 		= 0x020,
	Rotation 		= RotationX | RotationY | RotationZ,

	ScaleX 			= 0x040,
	ScaleY 			= 0x080,
	ScaleZ 			= 0x100,
	Scale 			= ScaleX | ScaleY | ScaleZ,

	AllTransform	= Translation | Rotation | Scale,

	Weight 			= 0x200,

	All				= Translation | Rotation | Scale | Weight,
};
ENUM_CLASS_FLAGS(EMovieSceneTransformChannel)

USTRUCT()
struct FMovieSceneTransformMask
{
	GENERATED_BODY()

	FMovieSceneTransformMask()
		: Mask(0)
	{}

	FMovieSceneTransformMask(EMovieSceneTransformChannel Channel)
		: Mask((__underlying_type(EMovieSceneTransformChannel))Channel)
	{}

	EMovieSceneTransformChannel GetChannels() const
	{
		return (EMovieSceneTransformChannel)Mask;
	}

	FVector GetTranslationFactor() const
	{
		EMovieSceneTransformChannel Channels = GetChannels();
		return FVector(
			EnumHasAllFlags(Channels, EMovieSceneTransformChannel::TranslationX) ? 1.f : 0.f,
			EnumHasAllFlags(Channels, EMovieSceneTransformChannel::TranslationY) ? 1.f : 0.f,
			EnumHasAllFlags(Channels, EMovieSceneTransformChannel::TranslationZ) ? 1.f : 0.f);
	}

	FVector GetRotationFactor() const
	{
		EMovieSceneTransformChannel Channels = GetChannels();
		return FVector(
			EnumHasAllFlags(Channels, EMovieSceneTransformChannel::RotationX) ? 1.f : 0.f,
			EnumHasAllFlags(Channels, EMovieSceneTransformChannel::RotationY) ? 1.f : 0.f,
			EnumHasAllFlags(Channels, EMovieSceneTransformChannel::RotationZ) ? 1.f : 0.f);
	}

	FVector GetScaleFactor() const
	{
		EMovieSceneTransformChannel Channels = GetChannels();
		return FVector(
			EnumHasAllFlags(Channels, EMovieSceneTransformChannel::ScaleX) ? 1.f : 0.f,
			EnumHasAllFlags(Channels, EMovieSceneTransformChannel::ScaleY) ? 1.f : 0.f,
			EnumHasAllFlags(Channels, EMovieSceneTransformChannel::ScaleZ) ? 1.f : 0.f);
	}

private:

	UPROPERTY()
	uint32 Mask;
};

/**
* This object contains information needed for constraint channels on the transform section
*/
UCLASS()
class MOVIESCENETRACKS_API UMovieScene3dTransformSectionConstraints : public UObject
{
	GENERATED_BODY()

public:

	/** Constraint Channels*/
	UPROPERTY()
	TArray<FConstraintAndActiveChannel> ConstraintsChannels;

	FDelegateHandle OnConstraintRemovedHandle;
};


/**
 * A 3D transform section
 */
UCLASS(MinimalAPI)
class UMovieScene3DTransformSection
	: public UMovieSceneSection
	, public IMovieSceneEntityProvider
{
	GENERATED_UCLASS_BODY()

public:

	/* From UMovieSection*/
	
	MOVIESCENETRACKS_API virtual bool ShowCurveForChannel(const void *Channel) const override;
	MOVIESCENETRACKS_API virtual void SetBlendType(EMovieSceneBlendType InBlendType) override;

public:

	/**
	 * Access the mask that defines which channels this track should animate
	 */
	MOVIESCENETRACKS_API FMovieSceneTransformMask GetMask() const;

	/**
	 * Set the mask that defines which channels this track should animate
	 */
	MOVIESCENETRACKS_API void SetMask(FMovieSceneTransformMask NewMask);

	/**
	 * Get the mask by name
	 */
	MOVIESCENETRACKS_API FMovieSceneTransformMask GetMaskByName(const FName& InName) const;

	/**
	* Get whether we should use quaternion interpolation for our rotations.
	*/
	MOVIESCENETRACKS_API bool GetUseQuaternionInterpolation() const;

	/**
	* Set whether we should use quaternion interpolation for our rotations.
	*/
	MOVIESCENETRACKS_API void SetUseQuaternionInterpolation(bool bInUseQuaternionInterpolation);

protected:

	MOVIESCENETRACKS_API virtual TSharedPtr<FStructOnScope> GetKeyStruct(TArrayView<const FKeyHandle> KeyHandles) override;
	MOVIESCENETRACKS_API virtual EMovieSceneChannelProxyType CacheChannelProxy() override;

private:
	
	MOVIESCENETRACKS_API virtual bool PopulateEvaluationFieldImpl(const TRange<FFrameNumber>& EffectiveRange, const FMovieSceneEvaluationFieldEntityMetaData& InMetaData, FMovieSceneEntityComponentFieldBuilder* OutFieldBuilder) override;
	MOVIESCENETRACKS_API virtual void ImportEntityImpl(UMovieSceneEntitySystemLinker* EntityLinker, const FEntityImportParams& Params, FImportedEntity* OutImportedEntity) override;
	MOVIESCENETRACKS_API virtual void InterrogateEntityImpl(UMovieSceneEntitySystemLinker* EntityLinker, const FEntityImportParams& Params, FImportedEntity* OutImportedEntity) override;

	template<typename BaseBuilderType>
	void BuildEntity(BaseBuilderType& InBaseBuilder, UMovieSceneEntitySystemLinker* Linker, const FEntityImportParams& Params, FImportedEntity* OutImportedEntity);
	void PopulateConstraintEntities(const TRange<FFrameNumber>& EffectiveRange, const FMovieSceneEvaluationFieldEntityMetaData& InMetaData, FMovieSceneEntityComponentFieldBuilder* OutFieldBuilder);
	void ImportConstraintEntity(UMovieSceneEntitySystemLinker* EntityLinker, const FEntityImportParams& Params, FImportedEntity* OutImportedEntity);

private:

	UPROPERTY()
	FMovieSceneTransformMask TransformMask;

	/** Translation curves */
	UPROPERTY()
	FMovieSceneDoubleChannel Translation[3];
	
	/** Rotation curves */
	UPROPERTY()
	FMovieSceneDoubleChannel Rotation[3];

	/** Scale curves */
	UPROPERTY()
	FMovieSceneDoubleChannel Scale[3];

	/** Manual weight curve */
	UPROPERTY()
	FMovieSceneFloatChannel ManualWeight;

	/** Optional pointer to a "channels override" container object. This object would only be allocated if any channels are overridden with a non-standard channel 	*/
	UPROPERTY()
	TObjectPtr<UMovieSceneSectionChannelOverrideRegistry> OverrideRegistry;

	/** Optional pointer to constraint channels*/
	UPROPERTY()
	TObjectPtr<UMovieScene3dTransformSectionConstraints> Constraints;

	/** Whether to use a quaternion linear interpolation between keys. This finds the 'shortest' rotation between keyed orientations. */
	UPROPERTY(EditAnywhere, DisplayName = "Use Quaternion Interpolation", Category = "Rotation")
	bool bUseQuaternionInterpolation;

public:

	/*
	* If it has that constraint with that Name
	*/
	MOVIESCENETRACKS_API bool HasConstraintChannel(const FName& InConstraintName) const;

	/*
	* Get constraint with that name
	*/
	MOVIESCENETRACKS_API FConstraintAndActiveChannel* GetConstraintChannel(const FName& InConstraintName);

	/*
	*  Add Cosntraint channel
	*/
	MOVIESCENETRACKS_API void AddConstraintChannel(UTickableConstraint* InConstraint);

private:

	void SetUpConstraintRemovedHandle();

#if WITH_EDITORONLY_DATA

public:

	/**
	 * Return the trajectory visibility
	 */
	MOVIESCENETRACKS_API EShow3DTrajectory GetShow3DTrajectory() const { return Show3DTrajectory; }

	/**
	 * Return the trajectory visibility
	 */
	MOVIESCENETRACKS_API void SetShow3DTrajectory(EShow3DTrajectory InShow3DTrajectory) { Show3DTrajectory = InShow3DTrajectory; }

private:

	/** Whether to show the 3d trajectory */
	UPROPERTY(EditAnywhere, DisplayName = "Show 3D Trajectory", Category = "Transform")
	EShow3DTrajectory Show3DTrajectory;

#endif // WITH_EDITORONLY_DATA
};
