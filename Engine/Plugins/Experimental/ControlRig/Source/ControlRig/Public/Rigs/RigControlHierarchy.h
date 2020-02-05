// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigHierarchyDefines.h"
#include "TransformNoScale.h"
#include "RigControlHierarchy.generated.h"

class UControlRig;

UENUM(BlueprintType)
enum class ERigControlType : uint8
{
	Bool,
	Float,
	Vector2D,
	Position,
	Scale,
	Rotator,
	Transform,
	TransformNoScale
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
struct CONTROLRIG_API FRigControlValue
{
	GENERATED_BODY()

public:

	FRigControlValue()
		:Storage(FTransform::Identity)
	{
	}

	template<class T>
	FORCEINLINE_DEBUGGABLE T Get() const
	{
		return GetRef<T>();
	}

	template<class T>
	FORCEINLINE_DEBUGGABLE T& GetRef()
	{
		ensure(sizeof(T) <= sizeof(FTransform));
		return *(T*)&Storage;
	}

	template<class T>
	FORCEINLINE_DEBUGGABLE const T& GetRef() const
	{
		ensure(sizeof(T) <= sizeof(FTransform));
		return *(T*)&Storage;
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

private:
	
	UPROPERTY()
	FTransform Storage;
};

template<>
FORCEINLINE_DEBUGGABLE FQuat FRigControlValue::SetFromString<FQuat>(const FString& InString)
{
	FQuat Value;
	TBaseStructure<FQuat>::Get()->ImportText(*InString, &Value, nullptr, PPF_None, nullptr, TBaseStructure<FQuat>::Get()->GetName());
	Set<FRotator>(Value.Rotator());
	return Value;
}


class UStaticMesh;

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigControl : public FRigElement
{
	GENERATED_BODY()

		FRigControl()
		: FRigElement()
		, ControlType(ERigControlType::Transform)
		, ParentName(NAME_None)
		, ParentIndex(INDEX_NONE)
		, SpaceName(NAME_None)
		, SpaceIndex(INDEX_NONE)
		, InitialValue()
		, Value()
		, PrimaryAxis(ERigControlAxis::X)
		, bIsCurve(false)
		, bAnimatable(true)
		, bLimitTranslation(false)
		, bLimitRotation(false)
		, bLimitScale(false)
		, bDrawLimits(true)
		, MinimumValue()
		, MaximumValue()
		, bGizmoEnabled(true)
		, GizmoName(TEXT("Gizmo"))
		, GizmoTransform(FTransform::Identity)
		, GizmoColor(FLinearColor::Red)
		, Dependents()
		, bIsTransientControl(false)
	{
	}
	virtual ~FRigControl() {}

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Control)
	ERigControlType ControlType;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Control)
	FName ParentName;

	UPROPERTY(BlueprintReadOnly, transient, Category = Control)
	int32 ParentIndex;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Control)
	FName SpaceName;

	UPROPERTY(BlueprintReadOnly, transient, Category = Control)
	int32 SpaceIndex;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = Control)
	FRigControlValue InitialValue;

	UPROPERTY(BlueprintReadOnly, transient, VisibleAnywhere, Category = Control)
	FRigControlValue Value;

	/** the primary axis to use for float controls */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Control)
	ERigControlAxis PrimaryAxis;

	/** If Created from a Curve  Container*/
	UPROPERTY(transient)
	bool bIsCurve;

	/** If the control is animatable in sequencer */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Control)
	bool bAnimatable;

	/** True if the control has to obey translation limits. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Limits)
	bool bLimitTranslation;

	/** True if the control has to obey rotation limits. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Limits)
	bool bLimitRotation;

	/** True if the control has to obey scale limits. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Limits)
	bool bLimitScale;

	/** True if the limits should be drawn in debug. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Limits, meta = (EditCondition = "bLimitTranslation || bLimitRotation || bLimitScale"))
	bool bDrawLimits;

	/** The storage for all minimum values */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Limits, meta = (EditCondition = "bLimitTranslation || bLimitRotation || bLimitScale"))
	FRigControlValue MinimumValue;

	/** The storage for all maximum values */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Limits, meta = (EditCondition = "bLimitTranslation || bLimitRotation || bLimitScale"))
	FRigControlValue MaximumValue;

	/** Set to true if the gizmo is enabled in 3d */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Gizmo)
	bool bGizmoEnabled;

	/* This is optional UI setting - this doesn't mean this is always used, but it is optional for manipulation layer to use this*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Gizmo, meta = (EditCondition = "bGizmoEnabled"))
	FName GizmoName;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Gizmo, meta = (EditCondition = "bGizmoEnabled"))
	FTransform GizmoTransform;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Gizmo, meta = (EditCondition = "bGizmoEnabled"))
	FLinearColor GizmoColor;

	/** dependent list - direct dependent for child or anything that needs to update due to this */
	UPROPERTY(transient)
	TArray<int32> Dependents;

	/** If the control is transient and only visible in the control rig editor */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Control)
	bool bIsTransientControl;

	FORCEINLINE_DEBUGGABLE virtual ERigElementType GetElementType() const override
	{
		return ERigElementType::Control;
	}

	FORCEINLINE_DEBUGGABLE virtual FRigElementKey GetParentElementKey() const
	{
		return FRigElementKey(ParentName, GetElementType());
	}

	FORCEINLINE_DEBUGGABLE virtual FRigElementKey GetSpaceElementKey() const
	{
		return FRigElementKey(SpaceName, ERigElementType::Space);
	}

	FORCEINLINE_DEBUGGABLE const FRigControlValue& GetValue(ERigControlValueType InValueType = ERigControlValueType::Current) const
	{
		switch(InValueType)
		{
			case ERigControlValueType::Initial:
			{
				return InitialValue;
			}
			case ERigControlValueType::Minimum:
			{
				return MinimumValue;
			}
			case ERigControlValueType::Maximum:
			{
				return MaximumValue;
			}
			default:
			{
				break;
			}
		}
		return Value;
	}

	FORCEINLINE_DEBUGGABLE FRigControlValue& GetValue(ERigControlValueType InValueType = ERigControlValueType::Current)
	{
		switch(InValueType)
		{
			case ERigControlValueType::Initial:
			{
				return InitialValue;
			}
			case ERigControlValueType::Minimum:
			{
				return MinimumValue;
			}
			case ERigControlValueType::Maximum:
			{
				return MaximumValue;
			}
			default:
			{
				break;
			}
		}
		return Value;
	}

	void ApplyLimits(FRigControlValue& InOutValue);
	FORCEINLINE_DEBUGGABLE static float Clamp(float Value, float Minimum, float Maximum)
	{
		if (Minimum < Maximum)
		{
			return FMath::Clamp<float>(Value, Minimum, Maximum);
		}
		return FMath::Clamp<float>(Value, Maximum, Minimum);
	}

	FORCEINLINE_DEBUGGABLE static FProperty* FindPropertyForValueType(ERigControlValueType InValueType)
	{
		switch (InValueType)
		{
			case ERigControlValueType::Current:
			{
				return FRigControl::StaticStruct()->FindPropertyByName(TEXT("Value"));
			}
			case ERigControlValueType::Initial:
			{
				return FRigControl::StaticStruct()->FindPropertyByName(TEXT("InitialValue"));
			}
			case ERigControlValueType::Minimum:
			{
				return FRigControl::StaticStruct()->FindPropertyByName(TEXT("MinimumValue"));
			}
			case ERigControlValueType::Maximum:
			{
				return FRigControl::StaticStruct()->FindPropertyByName(TEXT("MaximumValue"));
			}
		}
		return nullptr;
	}
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigControlHierarchy
{
	GENERATED_BODY()

	FRigControlHierarchy();
	FRigControlHierarchy& operator= (const FRigControlHierarchy &InOther);

	FORCEINLINE_DEBUGGABLE ERigElementType RigElementType() const { return ERigElementType::Control; }

	FORCEINLINE_DEBUGGABLE int32 Num() const { return Controls.Num(); }
	FORCEINLINE_DEBUGGABLE const FRigControl& operator[](int32 InIndex) const { return Controls[InIndex]; }
	FORCEINLINE_DEBUGGABLE FRigControl& operator[](int32 InIndex) { return Controls[InIndex]; }
	FORCEINLINE_DEBUGGABLE const FRigControl& operator[](const FName& InName) const { return Controls[GetIndex(InName)]; }
	FORCEINLINE_DEBUGGABLE FRigControl& operator[](const FName& InName) { return Controls[GetIndex(InName)]; }

	FORCEINLINE_DEBUGGABLE const TArray<FRigControl>& GetControls() const { return Controls; }

	FORCEINLINE_DEBUGGABLE TArray<FRigControl>::RangedForIteratorType      begin()       { return Controls.begin(); }
	FORCEINLINE_DEBUGGABLE TArray<FRigControl>::RangedForConstIteratorType begin() const { return Controls.begin(); }
	FORCEINLINE_DEBUGGABLE TArray<FRigControl>::RangedForIteratorType      end()         { return Controls.end();   }
	FORCEINLINE_DEBUGGABLE TArray<FRigControl>::RangedForConstIteratorType end() const   { return Controls.end();   }

	FORCEINLINE_DEBUGGABLE bool IsNameAvailable(const FName& InPotentialNewName) const { return GetIndex(InPotentialNewName) == INDEX_NONE; }

	FName GetSafeNewName(const FName& InPotentialNewName) const;

	FRigControl& Add(
		const FName& InNewName,
		ERigControlType InControlType = ERigControlType::Transform,
		const FName& InParentName = NAME_None,
		const FName& InSpaceName = NAME_None,
		const FRigControlValue& InValue = FRigControlValue(),
		const FName& InGizmoName = TEXT("Gizmo"),
		const FTransform& InGizmoTransform = FTransform::Identity,
		const FLinearColor& InGizmoColor = FLinearColor::Red
	);

	FRigControl Remove(const FName& InNameToRemove);

	FName Rename(const FName& InOldName, const FName& InNewName);

	bool Reparent(const FName& InName, const FName& InNewParentName);

	void SetSpace(const FName& InName, const FName& InNewSpaceName);

	FName GetName(int32 InIndex) const;

	FORCEINLINE_DEBUGGABLE int32 GetIndex(const FName& InName) const
	{
		if(NameToIndexMapping.Num() != Controls.Num())
		{
			return GetIndexSlow(InName);
		}

		const int32* Index = NameToIndexMapping.Find(InName);
		if (Index)
		{
			return *Index;
		}

		return INDEX_NONE;
	}

	// list of names of children - this is not cheap, and is supposed to be used only for one time set up
	int32 GetChildren(const FName& InName, TArray<int32>& OutChildren, bool bRecursively) const;

	int32 GetChildren(const int32 InIndex, TArray<int32>& OutChildren, bool bRecursively) const;

	void SetGlobalTransform(const FName& InName, const FTransform& InTransform);

	void SetGlobalTransform(int32 InIndex, const FTransform& InTransform);

	FTransform GetGlobalTransform(const FName& InName) const;

	FTransform GetGlobalTransform(int32 InIndex) const;

	void SetLocalTransform(const FName& InName, const FTransform& InTransform, ERigControlValueType InValueType = ERigControlValueType::Current);

	void SetLocalTransform(int32 InIndex, const FTransform& InTransform, ERigControlValueType InValueType = ERigControlValueType::Current);

	FTransform GetLocalTransform(const FName& InName, ERigControlValueType InValueType = ERigControlValueType::Current) const;

	FTransform GetLocalTransform(int32 InIndex, ERigControlValueType InValueType = ERigControlValueType::Current) const;

	FTransform GetParentTransform(int32 InIndex) const;

	// this is only valid if you have transform as type
	// setting initial transform from global transform (control transform is based on parent)
	void SetInitialGlobalTransform(const FName& InName, const FTransform& GlobalTransform);
	
	void SetInitialGlobalTransform(int32 InIndex, const FTransform& GlobalTransform);
	
	FTransform GetInitialGlobalTransform(const FName& InName) const;
	
	FTransform GetInitialGlobalTransform(int32 InIndex) const;
	
	void SetValue(const FName& InName, const FRigControlValue& InValue, ERigControlValueType InValueType = ERigControlValueType::Current);

	void SetValue(int32 InIndex, const FRigControlValue& InValue, ERigControlValueType InValueType = ERigControlValueType::Current);

	template<class T>
	FORCEINLINE_DEBUGGABLE void SetValue(const FName& InName, T InValue, ERigControlValueType InValueType = ERigControlValueType::Current)
	{
		SetValue(InName, FRigControlValue::Make<T>(InValue), InValueType);
	}

	template<class T>
	FORCEINLINE_DEBUGGABLE void SetValue(int32 InIndex, T InValue, ERigControlValueType InValueType = ERigControlValueType::Current)
	{
		SetValue(InIndex, FRigControlValue::Make<T>(InValue), InValueType);
	}

	FRigControlValue GetValue(const FName& InName, ERigControlValueType InValueType = ERigControlValueType::Current) const;

	FRigControlValue GetValue(int32 InIndex, ERigControlValueType InValueType = ERigControlValueType::Current) const;

	template<class T>
	FORCEINLINE_DEBUGGABLE T GetValue(const FName& InName, ERigControlValueType InValueType = ERigControlValueType::Current) const
	{
		return GetValue(InName, InValueType).Get<T>();
	}

	template<class T>
	FORCEINLINE_DEBUGGABLE T GetValue(int32 InIndex, ERigControlValueType InValueType = ERigControlValueType::Current) const
	{
		return GetValue(InIndex, InValueType).Get<T>();
	}

	FORCEINLINE_DEBUGGABLE void SetInitialValue(const FName& InName, const FRigControlValue& InValue)
	{
		SetValue(InName, InValue, ERigControlValueType::Initial);
	}

	FORCEINLINE_DEBUGGABLE void SetInitialValue(int32 InIndex, const FRigControlValue& InValue)
	{
		SetValue(InIndex, InValue, ERigControlValueType::Initial);
	}

	template<class T>
	FORCEINLINE_DEBUGGABLE void SetInitialValue(const FName& InName, T InValue)
	{
		SetInitialValue(InName, FRigControlValue::Make<T>(InValue));
	}

	template<class T>
	FORCEINLINE_DEBUGGABLE void SetInitialValue(int32 InIndex, T InValue)
	{
		SetInitialValue(InIndex, FRigControlValue::Make<T>(InValue));
	}

	FORCEINLINE_DEBUGGABLE FRigControlValue GetInitialValue(const FName& InName) const
	{
		return GetValue(InName, ERigControlValueType::Initial);
	}

	FORCEINLINE_DEBUGGABLE FRigControlValue GetInitialValue(int32 InIndex) const
	{
		return GetValue(InIndex, ERigControlValueType::Initial);
	}

	template<class T>
	FORCEINLINE_DEBUGGABLE T GetInitialValue(const FName& InName) const
	{
		return GetInitialValue(InName).Get<T>();
	}

	template<class T>
	FORCEINLINE_DEBUGGABLE T GetInitialValue(int32 InIndex) const
	{
		return GetInitialValue(InIndex).Get<T>();
	}

	FORCEINLINE_DEBUGGABLE void SetMinimumValue(const FName& InName, const FRigControlValue& InValue)
	{
		SetValue(InName, InValue, ERigControlValueType::Minimum);
	}

	FORCEINLINE_DEBUGGABLE void SetMinimumValue(int32 InIndex, const FRigControlValue& InValue)
	{
		SetValue(InIndex, InValue, ERigControlValueType::Minimum);
	}

	template<class T>
	FORCEINLINE_DEBUGGABLE void SetMinimumValue(const FName& InName, T InValue)
	{
		SetMinimumValue(InName, FRigControlValue::Make<T>(InValue));
	}

	template<class T>
	FORCEINLINE_DEBUGGABLE void SetMinimumValue(int32 InIndex, T InValue)
	{
		SetMinimumValue(InIndex, FRigControlValue::Make<T>(InValue));
	}

	FORCEINLINE_DEBUGGABLE FRigControlValue GetMinimumValue(const FName& InName) const
	{
		return GetValue(InName, ERigControlValueType::Minimum);
	}

	FORCEINLINE_DEBUGGABLE FRigControlValue GetMinimumValue(int32 InIndex) const
	{
		return GetValue(InIndex, ERigControlValueType::Minimum);
	}

	template<class T>
	FORCEINLINE_DEBUGGABLE T GetMinimumValue(const FName& InName) const
	{
		return GetMinimumValue(InName).Get<T>();
	}

	template<class T>
	FORCEINLINE_DEBUGGABLE T GetMinimumValue(int32 InIndex) const
	{
		return GetMinimumValue(InIndex).Get<T>();
	}

	FORCEINLINE_DEBUGGABLE void SetMaximumValue(const FName& InName, const FRigControlValue& InValue)
	{
		return SetValue(InName, InValue, ERigControlValueType::Maximum);
	}

	FORCEINLINE_DEBUGGABLE void SetMaximumValue(int32 InIndex, const FRigControlValue& InValue)
	{
		return SetValue(InIndex, InValue, ERigControlValueType::Maximum);
	}

	template<class T>
	FORCEINLINE_DEBUGGABLE void SetMaximumValue(const FName& InName, T InValue)
	{
		SetMaximumValue(InName, FRigControlValue::Make<T>(InValue));
	}

	template<class T>
	FORCEINLINE_DEBUGGABLE void SetMaximumValue(int32 InIndex, T InValue)
	{
		SetMaximumValue(InIndex, FRigControlValue::Make<T>(InValue));
	}

	FORCEINLINE_DEBUGGABLE FRigControlValue GetMaximumValue(const FName& InName) const
	{
		return GetValue(InName, ERigControlValueType::Maximum);
	}

	FORCEINLINE_DEBUGGABLE FRigControlValue GetMaximumValue(int32 InIndex) const
	{
		return GetValue(InIndex, ERigControlValueType::Maximum);
	}

	template<class T>
	FORCEINLINE_DEBUGGABLE T GetMaximumValue(const FName& InName) const
	{
		return GetMaximumValue(InName).Get<T>();
	}

	template<class T>
	FORCEINLINE_DEBUGGABLE T GetMaximumValue(int32 InIndex) const
	{
		return GetMaximumValue(InIndex).Get<T>();
	}

	// updates all of the internal caches
	void Initialize(bool bResetTransforms = true);

	// clears the hierarchy and removes all content
	void Reset();

	// resets all of the values back to the initial values
	void ResetValues();

#if WITH_EDITOR

	bool Select(const FName& InName, bool bSelect = true);
	bool ClearSelection();
	TArray<FName> CurrentSelection() const;
	bool IsSelected(const FName& InName) const;

	FRigElementAdded OnControlAdded;
	FRigElementRemoved OnControlRemoved;
	FRigElementRenamed OnControlRenamed;
	FRigElementReparented OnControlReparented;
	FRigElementSelected OnControlSelected;
	FRigElementChanged OnControlUISettingsChanged;

	void HandleOnElementRemoved(FRigHierarchyContainer* InContainer, const FRigElementKey& InKey);
	void HandleOnElementRenamed(FRigHierarchyContainer* InContainer, ERigElementType InElementType, const FName& InOldName, const FName& InNewName);

#endif

private:

	// disable copy constructor
	FRigControlHierarchy(const FRigControlHierarchy& InOther) {}

	FRigHierarchyContainer* Container;

	UPROPERTY(EditAnywhere, Category = FRigControlHierarchy)
	TArray<FRigControl> Controls;

	// can serialize fine? 
	UPROPERTY()
	TMap<FName, int32> NameToIndexMapping;

#if WITH_EDITORONLY_DATA
	UPROPERTY(transient)
	TArray<FName> Selection;
#endif

	int32 GetSpaceIndex(const FName& InName) const;

	int32 GetIndexSlow(const FName& InName) const;

	void RefreshMapping();

	// list of names of children - this is not cheap, and is supposed to be used only for one time set up
	int32 GetChildrenRecursive(const int32 InIndex, TArray<int32>& OutChildren, bool bRecursively) const;

	friend struct FRigHierarchyContainer;
	friend class UControlRigHierarchyModifier;
};
