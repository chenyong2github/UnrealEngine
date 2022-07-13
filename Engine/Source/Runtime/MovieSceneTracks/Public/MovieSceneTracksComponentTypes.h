// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Math/Transform.h"
#include "EntitySystem/BuiltInComponentTypes.h"
#include "EntitySystem/MovieSceneEntityIDs.h"
#include "EntitySystem/MovieScenePropertySystemTypes.h"
#include "EntitySystem/MovieScenePropertyMetaDataTraits.h"
#include "EntitySystem/MovieScenePropertyTraits.h"
#include "Engine/EngineTypes.h"
#include "EulerTransform.h"
#include "TransformData.h"
#include "MovieSceneTracksPropertyTypes.h"
#include "Styling/SlateColor.h"
#include "MovieSceneTracksComponentTypes.generated.h"

class UMaterialParameterCollection;
class UMovieSceneDataLayerSection;
class UMovieSceneLevelVisibilitySection;
struct FMovieSceneObjectBindingID;


/** Component data for the Float Perlin Noise Channel */
USTRUCT()
struct MOVIESCENETRACKS_API FFloatPerlinNoiseParams
{
	GENERATED_BODY()

	UPROPERTY()
	float Frequency;

	UPROPERTY()
	float Amplitude;

	FFloatPerlinNoiseParams();
	FFloatPerlinNoiseParams(float InFrequency, float InAmplitude);
};

/** Component data for the Float Perlin Noise Channel */
USTRUCT()
struct MOVIESCENETRACKS_API FDoublePerlinNoiseParams
{
	GENERATED_BODY()

	UPROPERTY()
	double Frequency;

	UPROPERTY()
	double Amplitude;

	FDoublePerlinNoiseParams();
	FDoublePerlinNoiseParams(double InFrequency, double InAmplitude);
};

/** Component data for the level visibility system */
USTRUCT()
struct FLevelVisibilityComponentData
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<const UMovieSceneLevelVisibilitySection> Section = nullptr;
};

/** Component data for the data layer system */
USTRUCT()
struct FMovieSceneDataLayerComponentData
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<const UMovieSceneDataLayerSection> Section = nullptr;
};

/** Component data for the constraint system */
USTRUCT()
struct FConstraintComponentData
{
	GENERATED_BODY()

	UPROPERTY()
	FName ConstraintName;

	FMovieSceneBoolChannel* Channel;
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

struct FFloatPropertyTraits
{
	using StorageType  = float;
	using MetaDataType = TPropertyMetaData<bool>;

	using FloatTraitsImpl = TDirectPropertyTraits<float>;
	using DoubleTraitsImpl = TDirectPropertyTraits<double>;

	static void GetObjectPropertyValue(const UObject* InObject, bool bIsDouble, const FCustomPropertyAccessor& BaseCustomAccessor, float& OutValue)
	{
		checkf(!bIsDouble, TEXT("Type mismatch between float and double. Please check for any custom accessors defined on the wrong property type."));
		const TCustomPropertyAccessor<FFloatPropertyTraits>& CustomAccessor = static_cast<const TCustomPropertyAccessor<FFloatPropertyTraits>&>(BaseCustomAccessor);
		OutValue = (*CustomAccessor.Functions.Getter)(InObject, bIsDouble);
	}
	static void GetObjectPropertyValue(const UObject* InObject, bool bIsDouble, uint16 PropertyOffset, float& OutValue)
	{
		if (bIsDouble)
		{
			double TempValue;
			DoubleTraitsImpl::GetObjectPropertyValue(InObject, PropertyOffset, TempValue);
			OutValue = TempValue;
		}
		else
		{
			FloatTraitsImpl::GetObjectPropertyValue(InObject, PropertyOffset, OutValue);
		}
	}
	static void GetObjectPropertyValue(const UObject* InObject, bool bIsDouble, FTrackInstancePropertyBindings* PropertyBindings, float& OutValue)
	{
		if (bIsDouble)
		{
			double TempValue;
			DoubleTraitsImpl::GetObjectPropertyValue(InObject, PropertyBindings, TempValue);
			OutValue = TempValue;
		}
		else
		{
			FloatTraitsImpl::GetObjectPropertyValue(InObject, PropertyBindings, OutValue);
		}
	}
	static void GetObjectPropertyValue(const UObject* InObject, bool bIsDouble, const FName& PropertyPath, float& OutValue)
	{
		if (bIsDouble)
		{
			double TempValue;
			DoubleTraitsImpl::GetObjectPropertyValue(InObject, PropertyPath, TempValue);
			OutValue = TempValue;
		}
		else
		{
			FloatTraitsImpl::GetObjectPropertyValue(InObject, PropertyPath, OutValue);
		}
	}

	static void SetObjectPropertyValue(UObject* InObject, bool bIsDouble, const FCustomPropertyAccessor& BaseCustomAccessor, float InValue)
	{
		checkf(!bIsDouble, TEXT("Type mismatch between float and double. Please check for any custom accessors defined on the wrong vector property type."));
		const TCustomPropertyAccessor<FFloatPropertyTraits>& CustomAccessor = static_cast<const TCustomPropertyAccessor<FFloatPropertyTraits>&>(BaseCustomAccessor);
		(*CustomAccessor.Functions.Setter)(InObject, bIsDouble, InValue);
	}
	static void SetObjectPropertyValue(UObject* InObject, bool bIsDouble, uint16 PropertyOffset, float InValue)
	{
		if (bIsDouble)
		{
			double TempValue = (float)InValue;
			DoubleTraitsImpl::SetObjectPropertyValue(InObject, PropertyOffset, TempValue);
		}
		else
		{
			FloatTraitsImpl::SetObjectPropertyValue(InObject, PropertyOffset, InValue);
		}
	}
	static void SetObjectPropertyValue(UObject* InObject, bool bIsDouble, FTrackInstancePropertyBindings* PropertyBindings, float InValue)
	{
		if (bIsDouble)
		{
			double TempValue = (float)InValue;
			DoubleTraitsImpl::SetObjectPropertyValue(InObject, PropertyBindings, TempValue);
		}
		else
		{
			FloatTraitsImpl::SetObjectPropertyValue(InObject, PropertyBindings, InValue);
		}
	}

	static float CombineComposites(bool bIsDouble, float InValue)
	{
		return InValue;
	}
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

struct FDoubleVectorPropertyTraits
{
	using StorageType  = FDoubleIntermediateVector;
	using MetaDataType = TPropertyMetaData<FVectorPropertyMetaData>;

	static void GetObjectPropertyValue(const UObject* InObject, FVectorPropertyMetaData MetaData, const FCustomPropertyAccessor& BaseCustomAccessor, FDoubleIntermediateVector& OutValue)
	{
		const TCustomPropertyAccessor<FDoubleVectorPropertyTraits>& CustomAccessor = static_cast<const TCustomPropertyAccessor<FDoubleVectorPropertyTraits>&>(BaseCustomAccessor);
		OutValue = (*CustomAccessor.Functions.Getter)(InObject, MetaData);
	}
	static void GetObjectPropertyValue(const UObject* InObject, FVectorPropertyMetaData MetaData, uint16 PropertyOffset, FDoubleIntermediateVector& OutValue)
	{
		switch (MetaData.NumChannels)
		{
			case 2: TIndirectPropertyTraits<FVector2d, FDoubleIntermediateVector>::GetObjectPropertyValue(InObject, PropertyOffset, OutValue); return;
			case 3: TIndirectPropertyTraits<FVector3d, FDoubleIntermediateVector>::GetObjectPropertyValue(InObject, PropertyOffset, OutValue); return;
			case 4: TIndirectPropertyTraits<FVector4d, FDoubleIntermediateVector>::GetObjectPropertyValue(InObject, PropertyOffset, OutValue); return;
		}
	}
	static void GetObjectPropertyValue(const UObject* InObject, FVectorPropertyMetaData MetaData, FTrackInstancePropertyBindings* PropertyBindings, FDoubleIntermediateVector& OutValue)
	{
		switch (MetaData.NumChannels)
		{
			case 2: TIndirectPropertyTraits<FVector2d, FDoubleIntermediateVector>::GetObjectPropertyValue(InObject, PropertyBindings, OutValue); return;
			case 3: TIndirectPropertyTraits<FVector3d, FDoubleIntermediateVector>::GetObjectPropertyValue(InObject, PropertyBindings, OutValue); return;
			case 4: TIndirectPropertyTraits<FVector4d, FDoubleIntermediateVector>::GetObjectPropertyValue(InObject, PropertyBindings, OutValue); return;
		}
	}
	static void GetObjectPropertyValue(const UObject* InObject, FVectorPropertyMetaData MetaData, const FName& PropertyPath, StorageType& OutValue)
	{
		switch (MetaData.NumChannels)
		{
			case 2: TIndirectPropertyTraits<FVector2d, FDoubleIntermediateVector>::GetObjectPropertyValue(InObject, PropertyPath, OutValue); return;
			case 3: TIndirectPropertyTraits<FVector3d, FDoubleIntermediateVector>::GetObjectPropertyValue(InObject, PropertyPath, OutValue); return;
			case 4: TIndirectPropertyTraits<FVector4d, FDoubleIntermediateVector>::GetObjectPropertyValue(InObject, PropertyPath, OutValue); return;
		}
	}

	static void SetObjectPropertyValue(UObject* InObject, FVectorPropertyMetaData MetaData, const FCustomPropertyAccessor& BaseCustomAccessor, const FDoubleIntermediateVector& InValue)
	{
		const TCustomPropertyAccessor<FDoubleVectorPropertyTraits>& CustomAccessor = static_cast<const TCustomPropertyAccessor<FDoubleVectorPropertyTraits>&>(BaseCustomAccessor);
		(*CustomAccessor.Functions.Setter)(InObject, MetaData, InValue);
	}
	static void SetObjectPropertyValue(UObject* InObject, FVectorPropertyMetaData MetaData, uint16 PropertyOffset, const FDoubleIntermediateVector& InValue)
	{
		switch (MetaData.NumChannels)
		{
			case 2: TIndirectPropertyTraits<FVector2d, FDoubleIntermediateVector>::SetObjectPropertyValue(InObject, PropertyOffset, InValue); return;
			case 3: TIndirectPropertyTraits<FVector3d, FDoubleIntermediateVector>::SetObjectPropertyValue(InObject, PropertyOffset, InValue); return;
			case 4: TIndirectPropertyTraits<FVector4d, FDoubleIntermediateVector>::SetObjectPropertyValue(InObject, PropertyOffset, InValue); return;
		}

		checkf(false, TEXT("Invalid number of channels"));
	}
	static void SetObjectPropertyValue(UObject* InObject, FVectorPropertyMetaData MetaData, FTrackInstancePropertyBindings* PropertyBindings, const FDoubleIntermediateVector& InValue)
	{
		switch (MetaData.NumChannels)
		{
			case 2: TIndirectPropertyTraits<FVector2d, FDoubleIntermediateVector>::SetObjectPropertyValue(InObject, PropertyBindings, InValue); return;
			case 3: TIndirectPropertyTraits<FVector3d, FDoubleIntermediateVector>::SetObjectPropertyValue(InObject, PropertyBindings, InValue); return;
			case 4: TIndirectPropertyTraits<FVector4d, FDoubleIntermediateVector>::SetObjectPropertyValue(InObject, PropertyBindings, InValue); return;
		}

		checkf(false, TEXT("Invalid number of channels"));
	}

	static FDoubleIntermediateVector CombineComposites(FVectorPropertyMetaData MetaData, double InX, double InY, double InZ, double InW)
	{
		return FDoubleIntermediateVector(InX, InY, InZ, InW);
	}
};

struct FFloatVectorPropertyTraits
{
	using StorageType = FFloatIntermediateVector;
	using MetaDataType = TPropertyMetaData<FVectorPropertyMetaData>;

	static void GetObjectPropertyValue(const UObject* InObject, FVectorPropertyMetaData MetaData, const FCustomPropertyAccessor& BaseCustomAccessor, FFloatIntermediateVector& OutValue)
	{
		checkf(!MetaData.bIsDouble, TEXT("Type mismatch between FVectorXf and FVectorXd. Please check for any custom accessors defined on the wrong vector property type."));
		const TCustomPropertyAccessor<FFloatVectorPropertyTraits>& CustomAccessor = static_cast<const TCustomPropertyAccessor<FFloatVectorPropertyTraits>&>(BaseCustomAccessor);
		OutValue = (*CustomAccessor.Functions.Getter)(InObject, MetaData);
	}
	static void GetObjectPropertyValue(const UObject* InObject, FVectorPropertyMetaData MetaData, uint16 PropertyOffset, FFloatIntermediateVector& OutValue)
	{
		if (MetaData.bIsDouble)
		{
			FDoubleIntermediateVector TempDoubleValue;
			FDoubleVectorPropertyTraits::GetObjectPropertyValue(InObject, MetaData, PropertyOffset, TempDoubleValue);
			OutValue = FFloatIntermediateVector(TempDoubleValue.X, TempDoubleValue.Y, TempDoubleValue.Z, TempDoubleValue.W);
			return;
		}

		switch (MetaData.NumChannels)
		{
		case 2: TIndirectPropertyTraits<FVector2f, FFloatIntermediateVector>::GetObjectPropertyValue(InObject, PropertyOffset, OutValue); return;
		case 3: TIndirectPropertyTraits<FVector3f, FFloatIntermediateVector>::GetObjectPropertyValue(InObject, PropertyOffset, OutValue); return;
		case 4: TIndirectPropertyTraits<FVector4f, FFloatIntermediateVector>::GetObjectPropertyValue(InObject, PropertyOffset, OutValue); return;
		}
	}
	static void GetObjectPropertyValue(const UObject* InObject, FVectorPropertyMetaData MetaData, FTrackInstancePropertyBindings* PropertyBindings, FFloatIntermediateVector& OutValue)
	{
		if (MetaData.bIsDouble)
		{
			FDoubleIntermediateVector TempDoubleValue;
			FDoubleVectorPropertyTraits::GetObjectPropertyValue(InObject, MetaData, PropertyBindings, TempDoubleValue);
			OutValue = FFloatIntermediateVector(TempDoubleValue.X, TempDoubleValue.Y, TempDoubleValue.Z, TempDoubleValue.W);
			return;
		}

		switch (MetaData.NumChannels)
		{
		case 2: TIndirectPropertyTraits<FVector2f, FFloatIntermediateVector>::GetObjectPropertyValue(InObject, PropertyBindings, OutValue); return;
		case 3: TIndirectPropertyTraits<FVector3f, FFloatIntermediateVector>::GetObjectPropertyValue(InObject, PropertyBindings, OutValue); return;
		case 4: TIndirectPropertyTraits<FVector4f, FFloatIntermediateVector>::GetObjectPropertyValue(InObject, PropertyBindings, OutValue); return;
		}
	}
	static void GetObjectPropertyValue(const UObject* InObject, FVectorPropertyMetaData MetaData, const FName& PropertyPath, StorageType& OutValue)
	{
		if (MetaData.bIsDouble)
		{
			FDoubleIntermediateVector TempDoubleValue;
			FDoubleVectorPropertyTraits::GetObjectPropertyValue(InObject, MetaData, PropertyPath, TempDoubleValue);
			OutValue = FFloatIntermediateVector(TempDoubleValue.X, TempDoubleValue.Y, TempDoubleValue.Z, TempDoubleValue.W);
			return;
		}

		switch (MetaData.NumChannels)
		{
		case 2: TIndirectPropertyTraits<FVector2f, FFloatIntermediateVector>::GetObjectPropertyValue(InObject, PropertyPath, OutValue); return;
		case 3: TIndirectPropertyTraits<FVector3f, FFloatIntermediateVector>::GetObjectPropertyValue(InObject, PropertyPath, OutValue); return;
		case 4: TIndirectPropertyTraits<FVector4f, FFloatIntermediateVector>::GetObjectPropertyValue(InObject, PropertyPath, OutValue); return;
		}
	}

	static void SetObjectPropertyValue(UObject* InObject, FVectorPropertyMetaData MetaData, const FCustomPropertyAccessor& BaseCustomAccessor, const FFloatIntermediateVector& InValue)
	{
		checkf(!MetaData.bIsDouble, TEXT("Type mismatch between FVectorXf and FVectorXd. Please check for any custom accessors defined on the wrong vector property type."));
		const TCustomPropertyAccessor<FFloatVectorPropertyTraits>& CustomAccessor = static_cast<const TCustomPropertyAccessor<FFloatVectorPropertyTraits>&>(BaseCustomAccessor);
		(*CustomAccessor.Functions.Setter)(InObject, MetaData, InValue);
	}
	static void SetObjectPropertyValue(UObject* InObject, FVectorPropertyMetaData MetaData, uint16 PropertyOffset, const FFloatIntermediateVector& InValue)
	{
		if (MetaData.bIsDouble)
		{
			FDoubleIntermediateVector TempDoubleValue(InValue.X, InValue.Y, InValue.Z, InValue.W);
			FDoubleVectorPropertyTraits::SetObjectPropertyValue(InObject, MetaData, PropertyOffset, TempDoubleValue);
			return;
		}

		switch (MetaData.NumChannels)
		{
		case 2: TIndirectPropertyTraits<FVector2f, FFloatIntermediateVector>::SetObjectPropertyValue(InObject, PropertyOffset, InValue); return;
		case 3: TIndirectPropertyTraits<FVector3f, FFloatIntermediateVector>::SetObjectPropertyValue(InObject, PropertyOffset, InValue); return;
		case 4: TIndirectPropertyTraits<FVector4f, FFloatIntermediateVector>::SetObjectPropertyValue(InObject, PropertyOffset, InValue); return;
		}

		checkf(false, TEXT("Invalid number of channels"));
	}
	static void SetObjectPropertyValue(UObject* InObject, FVectorPropertyMetaData MetaData, FTrackInstancePropertyBindings* PropertyBindings, const FFloatIntermediateVector& InValue)
	{
		if (MetaData.bIsDouble)
		{
			FDoubleIntermediateVector TempDoubleValue(InValue.X, InValue.Y, InValue.Z, InValue.W);
			FDoubleVectorPropertyTraits::SetObjectPropertyValue(InObject, MetaData, PropertyBindings, TempDoubleValue);
			return;
		}

		switch (MetaData.NumChannels)
		{
		case 2: TIndirectPropertyTraits<FVector2f, FFloatIntermediateVector>::SetObjectPropertyValue(InObject, PropertyBindings, InValue); return;
		case 3: TIndirectPropertyTraits<FVector3f, FFloatIntermediateVector>::SetObjectPropertyValue(InObject, PropertyBindings, InValue); return;
		case 4: TIndirectPropertyTraits<FVector4f, FFloatIntermediateVector>::SetObjectPropertyValue(InObject, PropertyBindings, InValue); return;
		}

		checkf(false, TEXT("Invalid number of channels"));
	}

	static FFloatIntermediateVector CombineComposites(FVectorPropertyMetaData MetaData, float InX, float InY, float InZ, float InW)
	{
		return FFloatIntermediateVector(InX, InY, InZ, InW);
	}
};

using FBoolPropertyTraits               = TDirectPropertyTraits<bool>;
using FBytePropertyTraits               = TDirectPropertyTraits<uint8>;
using FEnumPropertyTraits               = TDirectPropertyTraits<uint8>;
using FIntPropertyTraits                = TDirectPropertyTraits<int32>;
using FDoublePropertyTraits             = TDirectPropertyTraits<double>;
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
	TPropertyComponents<FDoublePropertyTraits> Double;
	TPropertyComponents<FColorPropertyTraits> Color;
	TPropertyComponents<FFloatVectorPropertyTraits> FloatVector;
	TPropertyComponents<FDoubleVectorPropertyTraits> DoubleVector;
	TPropertyComponents<FTransformPropertyTraits> Transform;
	TPropertyComponents<FEulerTransformPropertyTraits> EulerTransform;
	TPropertyComponents<FComponentTransformPropertyTraits> ComponentTransform;
	TComponentTypeID<FSourceDoubleChannel> QuaternionRotationChannel[3];

	TComponentTypeID<FConstraintComponentData> ConstraintChannel;

	TComponentTypeID<USceneComponent*> AttachParent;
	TComponentTypeID<FAttachmentComponent> AttachComponent;
	TComponentTypeID<FMovieSceneObjectBindingID> AttachParentBinding;
	TComponentTypeID<FFloatPerlinNoiseParams> FloatPerlinNoiseChannel;
	TComponentTypeID<FDoublePerlinNoiseParams> DoublePerlinNoiseChannel;

	TComponentTypeID<int32> ComponentMaterialIndex;

	TComponentTypeID<FName> BoolParameterName;
	TComponentTypeID<FName> ScalarParameterName;
	TComponentTypeID<FName> Vector2DParameterName;
	TComponentTypeID<FName> VectorParameterName;
	TComponentTypeID<FName> ColorParameterName;
	TComponentTypeID<FName> TransformParameterName;

	TComponentTypeID<UObject*> BoundMaterial;
	TComponentTypeID<UMaterialParameterCollection*> MPC;

	struct
	{
		TCustomPropertyRegistration<FBoolPropertyTraits> Bool;
		TCustomPropertyRegistration<FBytePropertyTraits> Byte;
		TCustomPropertyRegistration<FEnumPropertyTraits> Enum;
		TCustomPropertyRegistration<FIntPropertyTraits> Integer;
		TCustomPropertyRegistration<FFloatPropertyTraits> Float;
		TCustomPropertyRegistration<FDoublePropertyTraits> Double;
		TCustomPropertyRegistration<FColorPropertyTraits> Color;
		TCustomPropertyRegistration<FFloatVectorPropertyTraits> FloatVector;
		TCustomPropertyRegistration<FDoubleVectorPropertyTraits> DoubleVector;
		TCustomPropertyRegistration<FComponentTransformPropertyTraits, 1> ComponentTransform;
	} Accessors;

	TComponentTypeID<FLevelVisibilityComponentData> LevelVisibility;
	TComponentTypeID<FMovieSceneDataLayerComponentData> DataLayer;

	static void Destroy();

	static FMovieSceneTracksComponentTypes* Get();

private:
	FMovieSceneTracksComponentTypes();
};


} // namespace MovieScene
} // namespace UE
