// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TransformNoScale.h"
#include "EulerTransform.h"
#include "RigHierarchyDefines.generated.h"

class URigHierarchy;

/* 
 * This is rig element types that we support
 * This can be used as a mask so supported as a bitfield
 */
UENUM(BlueprintType)
enum class ERigElementType : uint8
{
	None,
	Bone = 0x001,
	Null = 0x002,
	Control = 0x004,
	Curve = 0x008,
	RigidBody = 0x010,
	Socket = 0x020,
	All = Bone | Null | Control | Curve | RigidBody | Socket UMETA(Hidden),
};

UENUM(BlueprintType)
enum class ERigBoneType : uint8
{
	Imported,
	User
};

UENUM()
enum class ERigHierarchyNotification : uint8
{
	ElementAdded,
	ElementRemoved,
	ElementRenamed,
	ElementSelected,
	ElementDeselected,
	ParentChanged,
	HierarchyReset,
	ControlSettingChanged,
	ControlVisibilityChanged,

	/** MAX - invalid */
	Max UMETA(Hidden),
};

UENUM()
enum class ERigEvent : uint8
{
	/** Invalid event */
	None,

	/** Request to Auto-Key the Control in Sequencer */
	RequestAutoKey,

	/** MAX - invalid */
	Max UMETA(Hidden),
};

/** When setting control values what to do with regards to setting key.*/
UENUM()
enum class EControlRigSetKey : uint8
{
	DoNotCare = 0x0,    //Don't care if a key is set or not, may get set, say if auto key is on somewhere.
	Always,				//Always set a key here
	Never				//Never set a key here.
};

UENUM(BlueprintType)
enum class ERigControlType : uint8
{
	Bool,
    Float,
    Integer,
    Vector2D,
    Position,
    Scale,
    Rotator,
    Transform,
    TransformNoScale,
    EulerTransform,
};

UENUM(BlueprintType)
enum class ERigControlValueType : uint8
{
	Initial,
    Current,
    Minimum,
    Maximum
};

UENUM(BlueprintType)
enum class ERigControlAxis : uint8
{
	X,
    Y,
    Z
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigControlValueStorage
{
public:

	GENERATED_BODY()

	FRigControlValueStorage()
	{
		FMemory::Memzero(this, sizeof(FRigControlValueStorage));
	}

	UPROPERTY()
	float Float00;

	UPROPERTY()
	float Float01;

	UPROPERTY()
	float Float02;

	UPROPERTY()
	float Float03;

	UPROPERTY()
	float Float10;

	UPROPERTY()
	float Float11;

	UPROPERTY()
	float Float12;

	UPROPERTY()
	float Float13;

	UPROPERTY()
	float Float20;

	UPROPERTY()
	float Float21;

	UPROPERTY()
	float Float22;

	UPROPERTY()
	float Float23;

	UPROPERTY()
	float Float30;

	UPROPERTY()
	float Float31;

	UPROPERTY()
	float Float32;

	UPROPERTY()
	float Float33;

	UPROPERTY()
	bool bValid;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigControlValue
{
	GENERATED_BODY()

public:

	FRigControlValue()
		: FloatStorage()
		, Storage_DEPRECATED(FTransform::Identity)
	{
	}

	FORCEINLINE_DEBUGGABLE bool IsValid() const
	{
		return FloatStorage.bValid;
	}

	template<class T>
	FORCEINLINE_DEBUGGABLE T Get() const
	{
		return GetRef<T>();
	}

	template<class T>
	FORCEINLINE_DEBUGGABLE T& GetRef()
	{
		FloatStorage.bValid = true;
		return *(T*)&FloatStorage;
	}

	template<class T>
	FORCEINLINE_DEBUGGABLE const T& GetRef() const
	{
		return *(T*)&FloatStorage;
	}

	template<class T>
	FORCEINLINE_DEBUGGABLE void Set(T InValue)
	{
		GetRef<T>() = InValue;
	}

	template<class T>
	FORCEINLINE_DEBUGGABLE FString ToString() const
	{
		FString Result;
		TBaseStructure<T>::Get()->ExportText(Result, &GetRef<T>(), nullptr, nullptr, PPF_None, nullptr);
		return Result;
	}

	template<class T>
	FORCEINLINE_DEBUGGABLE T SetFromString(const FString& InString)
	{
		T Value;
		TBaseStructure<T>::Get()->ImportText(*InString, &Value, nullptr, PPF_None, nullptr, TBaseStructure<T>::Get()->GetName());
		Set<T>(Value);
		return Value;
	}

	template<class T>
	FORCEINLINE_DEBUGGABLE static FRigControlValue Make(T InValue)
	{
		FRigControlValue Value;
		Value.Set<T>(InValue);
		return Value;
	}

	FORCEINLINE FTransform GetAsTransform(ERigControlType InControlType, ERigControlAxis InPrimaryAxis) const
	{
		FTransform Transform = FTransform::Identity;
		switch (InControlType)
		{
			case ERigControlType::Bool:
			{
				Transform.SetLocation(FVector(Get<bool>() ? 1.f : 0.f, 0.f, 0.f));
				break;
			}
			case ERigControlType::Float:
			{
				const float ValueToGet = Get<float>();
				switch (InPrimaryAxis)
				{
					case ERigControlAxis::X:
					{
						Transform.SetLocation(FVector(ValueToGet, 0.f, 0.f));
						break;
					}
					case ERigControlAxis::Y:
					{
						Transform.SetLocation(FVector(0.f, ValueToGet, 0.f));
						break;
					}
					case ERigControlAxis::Z:
					{
						Transform.SetLocation(FVector(0.f, 0.f, ValueToGet));
						break;
					}
				}
				break;
			}
			case ERigControlType::Integer:
			{
				const int32 ValueToGet = Get<int32>();
				switch (InPrimaryAxis)
				{
					case ERigControlAxis::X:
					{
						Transform.SetLocation(FVector((float)ValueToGet, 0.f, 0.f));
						break;
					}
					case ERigControlAxis::Y:
					{
						Transform.SetLocation(FVector(0.f, (float)ValueToGet, 0.f));
						break;
					}
					case ERigControlAxis::Z:
					{
						Transform.SetLocation(FVector(0.f, 0.f, (float)ValueToGet));
						break;
					}
				}
				break;
			}
			case ERigControlType::Vector2D:
			{
				const FVector2D ValueToGet = Get<FVector2D>();
				switch (InPrimaryAxis)
				{
					case ERigControlAxis::X:
					{
						Transform.SetLocation(FVector(0.f, ValueToGet.X, ValueToGet.Y));
						break;
					}
					case ERigControlAxis::Y:
					{
						Transform.SetLocation(FVector(ValueToGet.X, 0.f, ValueToGet.Y));
						break;
					}
					case ERigControlAxis::Z:
					{
						Transform.SetLocation(FVector(ValueToGet.X, ValueToGet.Y, 0.f));
						break;
					}
				}
				break;
			}
			case ERigControlType::Position:
			{
				Transform.SetLocation(Get<FVector>());
				break;
			}
			case ERigControlType::Scale:
			{
				Transform.SetScale3D(Get<FVector>());
				break;
			}
			case ERigControlType::Rotator:
			{
				Transform.SetRotation(FQuat(Get<FRotator>()));
				break;
			}
			case ERigControlType::Transform:
			{
				return Get<FTransform>();
			}
			case ERigControlType::TransformNoScale:
			{
				const FTransformNoScale TransformNoScale = Get<FTransformNoScale>();
				Transform = TransformNoScale;
				Transform.NormalizeRotation();
				break;
			}
			case ERigControlType::EulerTransform:
			{
				const FEulerTransform EulerTransform = Get<FEulerTransform>();
				Transform = FTransform(EulerTransform.ToFTransform());
				Transform.NormalizeRotation();
				break;
			}
			default:
			{
				ensure(false);
				break;
			}
		}
		return Transform;
	}
	
	FORCEINLINE void SetFromTransform(const FTransform& InTransform, ERigControlType InControlType, ERigControlAxis InPrimaryAxis)
	{
		switch (InControlType)
		{
			case ERigControlType::Bool:
			{
				Set<bool>(InTransform.GetLocation().X > SMALL_NUMBER);
				break;
			}
			case ERigControlType::Float:
			{
				switch (InPrimaryAxis)
				{
					case ERigControlAxis::X:
					{
						Set<float>(InTransform.GetLocation().X);
						break;
					}
					case ERigControlAxis::Y:
					{
						Set<float>(InTransform.GetLocation().Y);
						break;
					}
					case ERigControlAxis::Z:
					{
						Set<float>(InTransform.GetLocation().Z);
						break;
					}
				}
				break;
			}
			case ERigControlType::Integer:
			{
				switch (InPrimaryAxis)
				{
					case ERigControlAxis::X:
					{
						Set<int32>((int32)InTransform.GetLocation().X);
						break;
					}
					case ERigControlAxis::Y:
					{
						Set<int32>((int32)InTransform.GetLocation().Y);
						break;
					}
					case ERigControlAxis::Z:
					{
						Set<int32>((int32)InTransform.GetLocation().Z);
						break;
					}
				}
				break;
			}
			case ERigControlType::Vector2D:
			{
				const FVector Location = InTransform.GetLocation();
				switch (InPrimaryAxis)
				{
					case ERigControlAxis::X:
					{
						Set<FVector2D>(FVector2D(Location.Y, Location.Z));
						break;
					}
					case ERigControlAxis::Y:
					{
						Set<FVector2D>(FVector2D(Location.X, Location.Z));
						break;
					}
					case ERigControlAxis::Z:
					{
						Set<FVector2D>(FVector2D(Location.X, Location.Y));
						break;
					}
				}
				break;
			}
			case ERigControlType::Position:
			{
				Set<FVector>(InTransform.GetLocation());
				break;
			}
			case ERigControlType::Scale:
			{
				Set<FVector>(InTransform.GetScale3D());
				break;
			}
			case ERigControlType::Rotator:
			{
				//allow for values ><180/-180 by getting diff and adding that back in.
				FRotator CurrentRotator = Get<FRotator>();
				FRotator CurrentRotWind, CurrentRotRem;
				CurrentRotator.GetWindingAndRemainder(CurrentRotWind, CurrentRotRem);

				//Get Diff
				const FRotator NewRotator = FRotator(InTransform.GetRotation());
				FRotator DeltaRot = NewRotator - CurrentRotRem;
				DeltaRot.Normalize();

				//Add Diff
				CurrentRotator = CurrentRotator + DeltaRot;
				Set<FRotator>(CurrentRotator);
				break;
			}
			case ERigControlType::Transform:
			{
				Set<FTransform>(InTransform);
				break;
			}
			case ERigControlType::TransformNoScale:
			{
				const FTransformNoScale NoScale = InTransform;
				Set<FTransformNoScale>(NoScale);
				break;
			}
			case ERigControlType::EulerTransform:
			{
				//Find Diff of the rotation from current and just add that instead of setting so we can go over/under -180
				FEulerTransform NewTransform(InTransform);

				const FEulerTransform CurrentEulerTransform = Get<FEulerTransform>();
				FRotator CurrentWinding;
				FRotator CurrentRotRemainder;
				CurrentEulerTransform.Rotation.GetWindingAndRemainder(CurrentWinding, CurrentRotRemainder);
				const FRotator NewRotator = InTransform.GetRotation().Rotator();
				FRotator DeltaRot = NewRotator - CurrentRotRemainder;
				DeltaRot.Normalize();
				const FRotator NewRotation(CurrentEulerTransform.Rotation + DeltaRot);
				NewTransform.Rotation = NewRotation;
				Set<FEulerTransform>(NewTransform);
				break;
			}
			default:
			{
				ensure(false);
				break;
			}
		}
	}

	FORCEINLINE void ApplyLimits(
		bool bLimitTranslation,
		bool bLimitRotation,
		bool bLimitScale,
		ERigControlType InControlType,
		const FRigControlValue& InMinimumValue,
		const FRigControlValue& InMaximumValue)
	{
		if (!bLimitTranslation && !bLimitRotation && !bLimitScale)
		{
			return;
		}

		struct Local
		{
			FORCEINLINE static float Clamp(const float Value, const float Minimum, const float Maximum)
			{
				if (Minimum < Maximum)
				{
					return FMath::Clamp<float>(Value, Minimum, Maximum);
				}
				return FMath::Clamp<float>(Value, Maximum, Minimum);
			}

			FORCEINLINE static int32 Clamp(const int32 Value, const int32 Minimum, const int32 Maximum)
			{
				if (Minimum < Maximum)
				{
					return FMath::Clamp<int32>(Value, Minimum, Maximum);
				}
				return FMath::Clamp<int32>(Value, Maximum, Minimum);
			}
		};

		switch(InControlType)
		{
			case ERigControlType::Float:
			{
				if (bLimitTranslation)
				{
					float& ValueRef = GetRef<float>();
					ValueRef = Local::Clamp(ValueRef, InMinimumValue.Get<float>(), InMaximumValue.Get<float>());
				}
				break;
			}
			case ERigControlType::Integer:
			{
				if (bLimitTranslation)
				{
					int32& ValueRef = GetRef<int32>();
					ValueRef = Local::Clamp(ValueRef, InMinimumValue.Get<int32>(), InMaximumValue.Get<int32>());
				}
				break;
			}
			case ERigControlType::Vector2D:
			{
				if (bLimitTranslation)
				{
					FVector2D& ValueRef = GetRef<FVector2D>();
					const FVector2D& Min = InMinimumValue.GetRef<FVector2D>();
					const FVector2D& Max = InMaximumValue.GetRef<FVector2D>();
					ValueRef.X = Local::Clamp(ValueRef.X, Min.X, Max.X);
					ValueRef.Y = Local::Clamp(ValueRef.Y, Min.Y, Max.Y);
				}
				break;
			}
			case ERigControlType::Position:
			{
				if (bLimitTranslation)
				{
					FVector& ValueRef = GetRef<FVector>();
					const FVector& Min = InMinimumValue.GetRef<FVector>();
					const FVector& Max = InMaximumValue.GetRef<FVector>();
					ValueRef.X = Local::Clamp(ValueRef.X, Min.X, Max.X);
					ValueRef.Y = Local::Clamp(ValueRef.Y, Min.Y, Max.Y);
					ValueRef.Z = Local::Clamp(ValueRef.Z, Min.Z, Max.Z);
				}
				break;
			}
			case ERigControlType::Scale:
			{
				if (bLimitScale)
				{
					FVector& ValueRef = GetRef<FVector>();
					const FVector& Min = InMinimumValue.GetRef<FVector>();
					const FVector& Max = InMaximumValue.GetRef<FVector>();
					ValueRef.X = Local::Clamp(ValueRef.X, Min.X, Max.X);
					ValueRef.Y = Local::Clamp(ValueRef.Y, Min.Y, Max.Y);
					ValueRef.Z = Local::Clamp(ValueRef.Z, Min.Z, Max.Z);
				}
				break;
			}
			case ERigControlType::Rotator:
			{
				if (bLimitRotation)
				{
					FRotator& ValueRef = GetRef<FRotator>();
					const FRotator& Min = InMinimumValue.GetRef<FRotator>();
					const FRotator& Max = InMaximumValue.GetRef<FRotator>();
					ValueRef.Pitch = Local::Clamp(ValueRef.Pitch, Min.Pitch, Max.Pitch);
					ValueRef.Yaw = Local::Clamp(ValueRef.Yaw, Min.Yaw, Max.Yaw);
					ValueRef.Roll = Local::Clamp(ValueRef.Roll, Min.Roll, Max.Roll);
				}
				break;
			}
			case ERigControlType::Transform:
			{
				FTransform& ValueRef = GetRef<FTransform>();
				const FTransform& Min = InMinimumValue.GetRef<FTransform>();
				const FTransform& Max = InMaximumValue.GetRef<FTransform>();

				if (bLimitTranslation)
				{
					ValueRef.SetLocation(FVector(
						Local::Clamp(ValueRef.GetLocation().X, Min.GetLocation().X, Max.GetLocation().X),
						Local::Clamp(ValueRef.GetLocation().Y, Min.GetLocation().Y, Max.GetLocation().Y),
						Local::Clamp(ValueRef.GetLocation().Z, Min.GetLocation().Z, Max.GetLocation().Z)
					));
				}
				if (bLimitRotation)
				{
					const FRotator Rotator = ValueRef.GetRotation().Rotator();
					const FRotator MinRotator = Min.GetRotation().Rotator();
					const FRotator MaxRotator = Max.GetRotation().Rotator();

					ValueRef.SetRotation(FQuat(FRotator(
						Local::Clamp(Rotator.Pitch, MinRotator.Pitch, MaxRotator.Pitch),
						Local::Clamp(Rotator.Yaw, MinRotator.Yaw, MaxRotator.Yaw),
						Local::Clamp(Rotator.Roll, MinRotator.Roll, MaxRotator.Roll)
					)));
				}
				if (bLimitScale)
				{
					ValueRef.SetScale3D(FVector(
						Local::Clamp(ValueRef.GetScale3D().X, Min.GetScale3D().X, Max.GetScale3D().X),
						Local::Clamp(ValueRef.GetScale3D().Y, Min.GetScale3D().Y, Max.GetScale3D().Y),
						Local::Clamp(ValueRef.GetScale3D().Z, Min.GetScale3D().Z, Max.GetScale3D().Z)
					));
				}
				break;
			}
			case ERigControlType::TransformNoScale:
			{
				FTransformNoScale& ValueRef = GetRef<FTransformNoScale>();
				const FTransformNoScale& Min = InMinimumValue.GetRef<FTransformNoScale>();
				const FTransformNoScale& Max = InMaximumValue.GetRef<FTransformNoScale>();

				if (bLimitTranslation)
				{
					ValueRef.Location = FVector(
						Local::Clamp(ValueRef.Location.X, Min.Location.X, Max.Location.X),
						Local::Clamp(ValueRef.Location.Y, Min.Location.Y, Max.Location.Y),
						Local::Clamp(ValueRef.Location.Z, Min.Location.Z, Max.Location.Z)
					);
				}
				if (bLimitRotation)
				{
					const FRotator Rotator = ValueRef.Rotation.Rotator();
					const FRotator MinRotator = Min.Rotation.Rotator();
					const FRotator MaxRotator = Max.Rotation.Rotator();

					ValueRef.Rotation = FQuat(FRotator(
						Local::Clamp(Rotator.Pitch, MinRotator.Pitch, MaxRotator.Pitch),
						Local::Clamp(Rotator.Yaw, MinRotator.Yaw, MaxRotator.Yaw),
						Local::Clamp(Rotator.Roll, MinRotator.Roll, MaxRotator.Roll)
					));
				}
				break;
			}

			case ERigControlType::EulerTransform:
			{
				FEulerTransform& ValueRef = GetRef<FEulerTransform>();
				const FEulerTransform& Min = InMinimumValue.GetRef<FEulerTransform>();
				const FEulerTransform& Max = InMaximumValue.GetRef<FEulerTransform>();

				if (bLimitTranslation)
				{
					ValueRef.Location = FVector(
						Local::Clamp(ValueRef.Location.X, Min.Location.X, Max.Location.X),
						Local::Clamp(ValueRef.Location.Y, Min.Location.Y, Max.Location.Y),
						Local::Clamp(ValueRef.Location.Z, Min.Location.Z, Max.Location.Z)
					);
				}
				if (bLimitRotation)
				{
					const FRotator Rotator = ValueRef.Rotation;
					const FRotator MinRotator = Min.Rotation;
					const FRotator MaxRotator = Max.Rotation;

					ValueRef.Rotation = FRotator(
						Local::Clamp(Rotator.Pitch, MinRotator.Pitch, MaxRotator.Pitch),
						Local::Clamp(Rotator.Yaw, MinRotator.Yaw, MaxRotator.Yaw),
						Local::Clamp(Rotator.Roll, MinRotator.Roll, MaxRotator.Roll)
					);
				}
				if (bLimitScale)
				{
					ValueRef.Location = FVector(
						Local::Clamp(ValueRef.Scale.X, Min.Scale.X, Max.Scale.X),
						Local::Clamp(ValueRef.Scale.Y, Min.Scale.Y, Max.Scale.Y),
						Local::Clamp(ValueRef.Scale.Z, Min.Scale.Z, Max.Scale.Z)
					);
				}
				break;
			}
			case ERigControlType::Bool:
			default:
			{
				break;
			}
		}
	}

private:

	UPROPERTY()
	FRigControlValueStorage FloatStorage;
	
	UPROPERTY()
	FTransform Storage_DEPRECATED;

	friend struct FRigControlHierarchy;
	friend class UControlRigBlueprint;
};

template<>
FORCEINLINE_DEBUGGABLE FQuat FRigControlValue::SetFromString<FQuat>(const FString& InString)
{
	FQuat Value;
	TBaseStructure<FQuat>::Get()->ImportText(*InString, &Value, nullptr, PPF_None, nullptr, TBaseStructure<FQuat>::Get()->GetName());
	Set<FRotator>(Value.Rotator());
	return Value;
}

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigControlModifiedContext
{
	GENERATED_BODY()

	FRigControlModifiedContext()
	: SetKey(EControlRigSetKey::DoNotCare)
	, LocalTime(FLT_MAX)
	, EventName(NAME_None)
	{}

	FRigControlModifiedContext(EControlRigSetKey InSetKey)
		: SetKey(InSetKey)
		, LocalTime(FLT_MAX)
		, EventName(NAME_None)
	{}

	FRigControlModifiedContext(EControlRigSetKey InSetKey, float InLocalTime, const FName& InEventName = NAME_None)
		: SetKey(InSetKey)
		, LocalTime(InLocalTime)
		, EventName(InEventName)
	{}

	EControlRigSetKey SetKey;
	float LocalTime;
	FName EventName;
};

/*
 * Because it's bitfield, we support some basic functionality
 */
namespace FRigElementTypeHelper
{
	static uint32 Add(uint32 InMasks, ERigElementType InType)
	{
		return InMasks & (uint32)InType;
	}

	static uint32 Remove(uint32 InMasks, ERigElementType InType)
	{
		return InMasks & ~((uint32)InType);
	}

	static uint32 ToMask(ERigElementType InType)
	{
		return (uint32)InType;
	}

	static bool DoesHave(uint32 InMasks, ERigElementType InType)
	{
		return (InMasks & (uint32)InType) != 0;
	}
}

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigElementKey
{
public:
	
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Hierarchy")
	ERigElementType Type;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Hierarchy", meta = (CustomWidget = "ElementName"))
	FName Name;

	FRigElementKey()
		: Type(ERigElementType::None)
		, Name(NAME_None)
	{}

	FRigElementKey(const FName& InName, ERigElementType InType)
		: Type(InType)
		, Name(InName)
	{}

	void Serialize(FArchive& Ar);
	void Save(FArchive& Ar);
	void Load(FArchive& Ar);
	FORCEINLINE friend FArchive& operator<<(FArchive& Ar, FRigElementKey& P)
	{
		P.Serialize(Ar);
		return Ar;
	}

	FORCEINLINE bool IsValid() const
	{
		return Name != NAME_None && Type != ERigElementType::None;
	}

	FORCEINLINE explicit operator bool() const
	{
		return IsValid();
	}

	FORCEINLINE void Reset()
	{
		Type = ERigElementType::Curve;
		Name = NAME_None;
	}

	friend FORCEINLINE uint32 GetTypeHash(const FRigElementKey& Key)
	{
		return GetTypeHash(Key.Name) * 10 + (uint32)Key.Type;
	}

	FORCEINLINE bool operator ==(const FRigElementKey& Other) const
	{
		return Name == Other.Name && Type == Other.Type;
	}

	FORCEINLINE bool operator !=(const FRigElementKey& Other) const
	{
		return Name != Other.Name || Type != Other.Type;
	}

	FORCEINLINE bool operator <(const FRigElementKey& Other) const
	{
		if (Type < Other.Type)
		{
			return true;
		}
		return Name.LexicalLess(Other.Name);
	}

	FORCEINLINE bool operator >(const FRigElementKey& Other) const
	{
		if (Type > Other.Type)
		{
			return true;
		}
		return Other.Name.LexicalLess(Name);
	}

	FORCEINLINE FString ToString() const
	{
		switch (Type)
		{
			case ERigElementType::Bone:
			{
				return FString::Printf(TEXT("Bone(%s)"), *Name.ToString());
			}
			case ERigElementType::Null:
			{
				return FString::Printf(TEXT("Space(%s)"), *Name.ToString());
			}
			case ERigElementType::Control:
			{
				return FString::Printf(TEXT("Control(%s)"), *Name.ToString());
			}
			case ERigElementType::Curve:
			{
				return FString::Printf(TEXT("Curve(%s)"), *Name.ToString());
			}
			case ERigElementType::RigidBody:
			{
				return FString::Printf(TEXT("RigidBody(%s)"), *Name.ToString());
			}
			case ERigElementType::Socket:
			{
				return FString::Printf(TEXT("Socket(%s)"), *Name.ToString());
			}
		}
		return FString();
	}
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigElementKeyCollection
{
	GENERATED_BODY()

	FORCEINLINE FRigElementKeyCollection()
	{
	}

	FORCEINLINE FRigElementKeyCollection(const TArray<FRigElementKey>& InKeys)
		: Keys(InKeys)
	{
	}

	// Resets the data structure and maintains all storage.
	FORCEINLINE void Reset()
	{
		Keys.Reset();
	}

	// Resets the data structure and removes all storage.
	FORCEINLINE void Empty()
	{
		Keys.Empty();
	}

	// Returns true if a given instruction index is valid.
	FORCEINLINE bool IsValidIndex(int32 InIndex) const
	{
		return Keys.IsValidIndex(InIndex);
	}

	// Returns the number of elements in this collection.
	FORCEINLINE int32 Num() const { return Keys.Num(); }

	// Returns true if this collection contains no elements.
	FORCEINLINE bool IsEmpty() const
	{
		return Num() == 0;
	}

	// Returns the first element of this collection
	FORCEINLINE const FRigElementKey& First() const
	{
		return Keys[0];
	}

	// Returns the first element of this collection
	FORCEINLINE FRigElementKey& First()
	{
		return Keys[0];
	}

	// Returns the last element of this collection
	FORCEINLINE const FRigElementKey& Last() const
	{
		return Keys.Last();
	}

	// Returns the last element of this collection
	FORCEINLINE FRigElementKey& Last()
	{
		return Keys.Last();
	}

	FORCEINLINE int32 Add(const FRigElementKey& InKey)
	{
		return Keys.Add(InKey);
	}

	FORCEINLINE int32 AddUnique(const FRigElementKey& InKey)
	{
		return Keys.AddUnique(InKey);
	}

	FORCEINLINE bool Contains(const FRigElementKey& InKey) const
	{
		return Keys.Contains(InKey);
	}

	// const accessor for an element given its index
	FORCEINLINE const FRigElementKey& operator[](int32 InIndex) const
	{
		return Keys[InIndex];
	}
	   
	FORCEINLINE TArray<FRigElementKey>::RangedForIteratorType      begin() { return Keys.begin(); }
	FORCEINLINE TArray<FRigElementKey>::RangedForConstIteratorType begin() const { return Keys.begin(); }
	FORCEINLINE TArray<FRigElementKey>::RangedForIteratorType      end() { return Keys.end(); }
	FORCEINLINE TArray<FRigElementKey>::RangedForConstIteratorType end() const { return Keys.end(); }

	friend FORCEINLINE uint32 GetTypeHash(const FRigElementKeyCollection& Collection)
	{
		uint32 Hash = (uint32)(Collection.Num() * 17 + 3);
		for (const FRigElementKey& Key : Collection)
		{
			Hash += GetTypeHash(Key);
		}
		return Hash;
	}

	// creates a collection containing all of the children of a given 
	static FRigElementKeyCollection MakeFromChildren(
		URigHierarchy* InHierarchy, 
		const FRigElementKey& InParentKey,
		bool bRecursive = true,
		bool bIncludeParent = false,
		uint8 InElementTypes = (uint8)ERigElementType::All);

	// creates a collection containing all of the elements with a given name
	static FRigElementKeyCollection MakeFromName(
		URigHierarchy* InHierarchy,
		const FName& InPartialName,
		uint8 InElementTypes = (uint8)ERigElementType::All);

	// creates a collection containing an item chain
	static FRigElementKeyCollection MakeFromChain(
		URigHierarchy* InHierarchy,
		const FRigElementKey& InFirstItem,
		const FRigElementKey& InLastItem,
		bool bReverse = false);

	// creates a collection containing all keys of a hierarchy
	static FRigElementKeyCollection MakeFromCompleteHierarchy(
		URigHierarchy* InHierarchy,
		uint8 InElementTypes = (uint8)ERigElementType::All);

	// returns the union between two collections
	static FRigElementKeyCollection MakeUnion(const FRigElementKeyCollection& A, const FRigElementKeyCollection& B);

	// returns the intersection between two collections
	static FRigElementKeyCollection MakeIntersection(const FRigElementKeyCollection& A, const FRigElementKeyCollection& B);

	// returns the difference between two collections
	static FRigElementKeyCollection MakeDifference(const FRigElementKeyCollection& A, const FRigElementKeyCollection& B);

	// returns the collection in the reverse order
	static FRigElementKeyCollection MakeReversed(const FRigElementKeyCollection& InCollection);

	// filters a collection by element type
	FRigElementKeyCollection FilterByType(uint8 InElementTypes) const;

	// filters a collection by name
	FRigElementKeyCollection FilterByName(const FName& InPartialName) const;

protected:

	TArray<FRigElementKey> Keys;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigElement
{
	GENERATED_BODY()

	FRigElement()
		: Name(NAME_None)
		, Index(INDEX_NONE)
	{}
	virtual ~FRigElement() {}
	
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = FRigElement)
	FName Name;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = FRigElement)
	int32 Index;

	virtual ERigElementType GetElementType() const
	{
		return ERigElementType::None; 
	}

	FRigElementKey GetElementKey() const
	{
		return FRigElementKey(Name, GetElementType());
	}
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigEventContext
{
	GENERATED_BODY()

	FRigEventContext()
		: Event(ERigEvent::None)
		, SourceEventName(NAME_None)
		, Key()
		, LocalTime(0.f)
		, Payload(nullptr)
	{}
	
	ERigEvent Event;
	FName SourceEventName;
	FRigElementKey Key;
	float LocalTime;
	void* Payload;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigMirrorSettings
{
	GENERATED_USTRUCT_BODY()

    FRigMirrorSettings()
    : MirrorAxis(EAxis::X)
    , AxisToFlip(EAxis::Z)
	{
	}

	// the axis to mirror against
	UPROPERTY(EditAnywhere, Category = Settings)
	TEnumAsByte<EAxis::Type> MirrorAxis;

	// the axis to flip for rotations
	UPROPERTY(EditAnywhere, Category = Settings)
	TEnumAsByte<EAxis::Type> AxisToFlip;

	// the string to replace all occurences of with New Name
	UPROPERTY(EditAnywhere, Category = Settings)
	FString OldName;

	// the string to replace all occurences of Old Name with
	UPROPERTY(EditAnywhere, Category = Settings)
	FString NewName;

	FTransform MirrorTransform(const FTransform& InTransform) const;
	FVector MirrorVector(const FVector& InVector) const;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FRigEventDelegate, URigHierarchy*, const FRigEventContext&);
