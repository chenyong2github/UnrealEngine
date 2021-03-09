// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Transform.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieScenePropertySystemTypes.h"
#include "Engine/EngineTypes.h"
#include "EulerTransform.h"
#include "TransformData.h"
#include "EntitySystem/MovieScenePropertyTraits.h"
#include "EntitySystem/MovieScenePropertyMetaDataTraits.h"
#include "MovieSceneTracksPropertyTypes.h"
#include "Styling/SlateColor.h"
#include "MovieSceneTracksComponentTypes.generated.h"

class UMovieSceneLevelVisibilitySection;
struct FMovieSceneObjectBindingID;

/** Component data for the level visibility system */
USTRUCT()
struct FLevelVisibilityComponentData
{
	GENERATED_BODY()

	UPROPERTY()
	const UMovieSceneLevelVisibilitySection* Section = nullptr;
};

namespace UE
{
namespace MovieScene
{

struct FComponentAttachParamsDestination
{
	FName SocketName    = NAME_None;
	FName ComponentName = NAME_None;

	MOVIESCENETRACKS_API USceneComponent* ResolveAttachment(AActor* InParentActor) const;
};

struct FComponentAttachParams
{
	EAttachmentRule AttachmentLocationRule  = EAttachmentRule::KeepRelative;
	EAttachmentRule AttachmentRotationRule  = EAttachmentRule::KeepRelative;
	EAttachmentRule AttachmentScaleRule     = EAttachmentRule::KeepRelative;

	MOVIESCENETRACKS_API void ApplyAttach(USceneComponent* NewAttachParent, USceneComponent* ChildComponentToAttach, const FName& SocketName) const;
};

struct FComponentDetachParams
{
	EDetachmentRule DetachmentLocationRule  = EDetachmentRule::KeepRelative;
	EDetachmentRule DetachmentRotationRule  = EDetachmentRule::KeepRelative;
	EDetachmentRule DetachmentScaleRule     = EDetachmentRule::KeepRelative;

	MOVIESCENETRACKS_API void ApplyDetach(USceneComponent* NewAttachParent, USceneComponent* ChildComponentToAttach, const FName& SocketName) const;
};

struct FAttachmentComponent
{
	FComponentAttachParamsDestination Destination;

	FComponentAttachParams AttachParams;
	FComponentDetachParams DetachParams;
};

struct FColorPropertyTraits
{
	using StorageType  = FIntermediateColor;
	using MetaDataType = TPropertyMetaData<EColorPropertyType>;

	static void GetObjectPropertyValue(const UObject* InObject, EColorPropertyType ColorType, const FCustomPropertyAccessor& BaseCustomAccessor, FIntermediateColor& OutValue)
	{
		const TCustomPropertyAccessor<FColorPropertyTraits>& CustomAccessor = static_cast<const TCustomPropertyAccessor<FColorPropertyTraits>&>(BaseCustomAccessor);
		OutValue = (*CustomAccessor.Functions.Getter)(InObject, ColorType);
	}
	static void GetObjectPropertyValue(const UObject* InObject, EColorPropertyType ColorType, uint16 PropertyOffset, FIntermediateColor& OutValue)
	{
		switch (ColorType)
		{
			case EColorPropertyType::Slate:  TIndirectPropertyTraits<FSlateColor, FIntermediateColor>::GetObjectPropertyValue(InObject, PropertyOffset, OutValue);  return;
			case EColorPropertyType::Linear: TIndirectPropertyTraits<FLinearColor, FIntermediateColor>::GetObjectPropertyValue(InObject, PropertyOffset, OutValue); return;
			case EColorPropertyType::Color:  TIndirectPropertyTraits<FColor, FIntermediateColor>::GetObjectPropertyValue(InObject, PropertyOffset, OutValue);       return;
		}
	}
	static void GetObjectPropertyValue(const UObject* InObject, EColorPropertyType ColorType, FTrackInstancePropertyBindings* PropertyBindings, FIntermediateColor& OutValue)
	{
		switch (ColorType)
		{
			case EColorPropertyType::Slate:  TIndirectPropertyTraits<FSlateColor, FIntermediateColor>::GetObjectPropertyValue(InObject, PropertyBindings, OutValue);  return;
			case EColorPropertyType::Linear: TIndirectPropertyTraits<FLinearColor, FIntermediateColor>::GetObjectPropertyValue(InObject, PropertyBindings, OutValue); return;
			case EColorPropertyType::Color:  TIndirectPropertyTraits<FColor, FIntermediateColor>::GetObjectPropertyValue(InObject, PropertyBindings, OutValue);       return;
		}
	}
	static void GetObjectPropertyValue(const UObject* InObject, EColorPropertyType ColorType, const FName& PropertyPath, StorageType& OutValue)
	{
		switch (ColorType)
		{
			case EColorPropertyType::Slate:  TIndirectPropertyTraits<FSlateColor, FIntermediateColor>::GetObjectPropertyValue(InObject, PropertyPath, OutValue);  return;
			case EColorPropertyType::Linear: TIndirectPropertyTraits<FLinearColor, FIntermediateColor>::GetObjectPropertyValue(InObject, PropertyPath, OutValue); return;
			case EColorPropertyType::Color:  TIndirectPropertyTraits<FColor, FIntermediateColor>::GetObjectPropertyValue(InObject, PropertyPath, OutValue);       return;
		}
	}

	static void SetObjectPropertyValue(UObject* InObject, EColorPropertyType ColorType, const FCustomPropertyAccessor& BaseCustomAccessor, const FIntermediateColor& InValue)
	{
		const TCustomPropertyAccessor<FColorPropertyTraits>& CustomAccessor = static_cast<const TCustomPropertyAccessor<FColorPropertyTraits>&>(BaseCustomAccessor);
		(*CustomAccessor.Functions.Setter)(InObject, ColorType, InValue);
	}
	static void SetObjectPropertyValue(UObject* InObject, EColorPropertyType ColorType, uint16 PropertyOffset, const FIntermediateColor& InValue)
	{
		switch (ColorType)
		{
			case EColorPropertyType::Slate:  TIndirectPropertyTraits<FSlateColor, FIntermediateColor>::SetObjectPropertyValue(InObject, PropertyOffset, InValue);  return;
			case EColorPropertyType::Linear: TIndirectPropertyTraits<FLinearColor, FIntermediateColor>::SetObjectPropertyValue(InObject, PropertyOffset, InValue); return;
			case EColorPropertyType::Color:  TIndirectPropertyTraits<FColor, FIntermediateColor>::SetObjectPropertyValue(InObject, PropertyOffset, InValue);       return;
		}
	}
	static void SetObjectPropertyValue(UObject* InObject, EColorPropertyType ColorType, FTrackInstancePropertyBindings* PropertyBindings, const FIntermediateColor& InValue)
	{
		switch (ColorType)
		{
			case EColorPropertyType::Slate:  TIndirectPropertyTraits<FSlateColor, FIntermediateColor>::SetObjectPropertyValue(InObject, PropertyBindings, InValue);  return;
			case EColorPropertyType::Linear: TIndirectPropertyTraits<FLinearColor, FIntermediateColor>::SetObjectPropertyValue(InObject, PropertyBindings, InValue); return;
			case EColorPropertyType::Color:  TIndirectPropertyTraits<FColor, FIntermediateColor>::SetObjectPropertyValue(InObject, PropertyBindings, InValue);       return;
		}
	}

	static FIntermediateColor CombineComposites(EColorPropertyType InType, float InR, float InG, float InB, float InA)
	{
		return FIntermediateColor(InR, InG, InB, InA);
	}
};

struct FVectorChannelMetaData
{
	uint8 NumChannels = 0;
};

struct FVectorPropertyTraits
{
	using StorageType  = FIntermediateVector;
	using MetaDataType = TPropertyMetaData<FVectorChannelMetaData>;

	static void GetObjectPropertyValue(const UObject* InObject, FVectorChannelMetaData MetaData, const FCustomPropertyAccessor& BaseCustomAccessor, FIntermediateVector& OutValue)
	{
		const TCustomPropertyAccessor<FVectorPropertyTraits>& CustomAccessor = static_cast<const TCustomPropertyAccessor<FVectorPropertyTraits>&>(BaseCustomAccessor);
		OutValue = (*CustomAccessor.Functions.Getter)(InObject, MetaData);
	}
	static void GetObjectPropertyValue(const UObject* InObject, FVectorChannelMetaData MetaData, uint16 PropertyOffset, FIntermediateVector& OutValue)
	{
		switch (MetaData.NumChannels)
		{
			case 2: TIndirectPropertyTraits<FVector2D, FIntermediateVector>::GetObjectPropertyValue(InObject, PropertyOffset, OutValue); return;
			case 3: TIndirectPropertyTraits<FVector,   FIntermediateVector>::GetObjectPropertyValue(InObject, PropertyOffset, OutValue); return;
			case 4: TIndirectPropertyTraits<FVector4,  FIntermediateVector>::GetObjectPropertyValue(InObject, PropertyOffset, OutValue); return;
		}
	}
	static void GetObjectPropertyValue(const UObject* InObject, FVectorChannelMetaData MetaData, FTrackInstancePropertyBindings* PropertyBindings, FIntermediateVector& OutValue)
	{
		switch (MetaData.NumChannels)
		{
			case 2: TIndirectPropertyTraits<FVector2D, FIntermediateVector>::GetObjectPropertyValue(InObject, PropertyBindings, OutValue); return;
			case 3: TIndirectPropertyTraits<FVector,   FIntermediateVector>::GetObjectPropertyValue(InObject, PropertyBindings, OutValue); return;
			case 4: TIndirectPropertyTraits<FVector4,  FIntermediateVector>::GetObjectPropertyValue(InObject, PropertyBindings, OutValue); return;
		}
	}
	static void GetObjectPropertyValue(const UObject* InObject, FVectorChannelMetaData MetaData, const FName& PropertyPath, StorageType& OutValue)
	{
		switch (MetaData.NumChannels)
		{
			case 2: TIndirectPropertyTraits<FVector2D, FIntermediateVector>::GetObjectPropertyValue(InObject, PropertyPath, OutValue); return;
			case 3: TIndirectPropertyTraits<FVector,   FIntermediateVector>::GetObjectPropertyValue(InObject, PropertyPath, OutValue); return;
			case 4: TIndirectPropertyTraits<FVector4,  FIntermediateVector>::GetObjectPropertyValue(InObject, PropertyPath, OutValue); return;
		}
	}

	static void SetObjectPropertyValue(UObject* InObject, FVectorChannelMetaData MetaData, const FCustomPropertyAccessor& BaseCustomAccessor, const FIntermediateVector& InValue)
	{
		const TCustomPropertyAccessor<FVectorPropertyTraits>& CustomAccessor = static_cast<const TCustomPropertyAccessor<FVectorPropertyTraits>&>(BaseCustomAccessor);
		(*CustomAccessor.Functions.Setter)(InObject, MetaData, InValue);
	}
	static void SetObjectPropertyValue(UObject* InObject, FVectorChannelMetaData MetaData, uint16 PropertyOffset, const FIntermediateVector& InValue)
	{
		switch (MetaData.NumChannels)
		{
			case 2: TIndirectPropertyTraits<FVector2D, FIntermediateVector>::SetObjectPropertyValue(InObject, PropertyOffset, InValue); return;
			case 3: TIndirectPropertyTraits<FVector,   FIntermediateVector>::SetObjectPropertyValue(InObject, PropertyOffset, InValue); return;
			case 4: TIndirectPropertyTraits<FVector4,  FIntermediateVector>::SetObjectPropertyValue(InObject, PropertyOffset, InValue); return;
		}

		checkf(false, TEXT("Invalid number of channels"));
	}
	static void SetObjectPropertyValue(UObject* InObject, FVectorChannelMetaData MetaData, FTrackInstancePropertyBindings* PropertyBindings, const FIntermediateVector& InValue)
	{
		switch (MetaData.NumChannels)
		{
			case 2: TIndirectPropertyTraits<FVector2D, FIntermediateVector>::SetObjectPropertyValue(InObject, PropertyBindings, InValue); return;
			case 3: TIndirectPropertyTraits<FVector,   FIntermediateVector>::SetObjectPropertyValue(InObject, PropertyBindings, InValue); return;
			case 4: TIndirectPropertyTraits<FVector4,  FIntermediateVector>::SetObjectPropertyValue(InObject, PropertyBindings, InValue); return;
		}

		checkf(false, TEXT("Invalid number of channels"));
	}

	static FIntermediateVector CombineComposites(FVectorChannelMetaData MetaData, float InX, float InY, float InZ, float InW)
	{
		return FIntermediateVector(InX, InY, InZ, InW);
	}
};

using FBoolPropertyTraits               = TDirectPropertyTraits<bool>;
using FBytePropertyTraits               = TDirectPropertyTraits<uint8>;
using FEnumPropertyTraits               = TDirectPropertyTraits<uint8>;
using FIntPropertyTraits                = TDirectPropertyTraits<int32>;
using FFloatPropertyTraits              = TDirectPropertyTraits<float>;
using FTransformPropertyTraits          = TIndirectPropertyTraits<FTransform, FIntermediate3DTransform>;
using FEulerTransformPropertyTraits     = TIndirectPropertyTraits<FEulerTransform, FIntermediate3DTransform>;
using FComponentTransformPropertyTraits = TDirectPropertyTraits<FIntermediate3DTransform>;


struct MOVIESCENETRACKS_API FMovieSceneTracksComponentTypes
{
	~FMovieSceneTracksComponentTypes();

	TPropertyComponents<FBoolPropertyTraits> Bool;
	TPropertyComponents<FBytePropertyTraits> Byte;
	TPropertyComponents<FEnumPropertyTraits> Enum;
	TPropertyComponents<FIntPropertyTraits> Integer;
	TPropertyComponents<FFloatPropertyTraits> Float;
	TPropertyComponents<FColorPropertyTraits> Color;
	TPropertyComponents<FVectorPropertyTraits> Vector;
	TPropertyComponents<FTransformPropertyTraits> Transform;
	TPropertyComponents<FEulerTransformPropertyTraits> EulerTransform;
	TPropertyComponents<FComponentTransformPropertyTraits> ComponentTransform;
	TComponentTypeID<FSourceFloatChannel> QuaternionRotationChannel[3];

	TComponentTypeID<USceneComponent*> AttachParent;
	TComponentTypeID<FAttachmentComponent> AttachComponent;
	TComponentTypeID<FMovieSceneObjectBindingID> AttachParentBinding;

	struct
	{
		TCustomPropertyRegistration<FBoolPropertyTraits> Bool;
		TCustomPropertyRegistration<FBytePropertyTraits> Byte;
		TCustomPropertyRegistration<FEnumPropertyTraits> Enum;
		TCustomPropertyRegistration<FIntPropertyTraits> Integer;
		TCustomPropertyRegistration<FFloatPropertyTraits> Float;
		TCustomPropertyRegistration<FColorPropertyTraits> Color;
		TCustomPropertyRegistration<FVectorPropertyTraits> Vector;
		TCustomPropertyRegistration<FComponentTransformPropertyTraits, 1> ComponentTransform;
	} Accessors;

	TComponentTypeID<FLevelVisibilityComponentData> LevelVisibility;

	static void Destroy();

	static FMovieSceneTracksComponentTypes* Get();

private:
	FMovieSceneTracksComponentTypes();
};


} // namespace MovieScene
} // namespace UE
