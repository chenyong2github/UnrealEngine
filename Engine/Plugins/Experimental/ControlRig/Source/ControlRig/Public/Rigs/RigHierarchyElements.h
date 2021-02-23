// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigHierarchyDefines.h"
#include "RigHierarchyElements.generated.h"

struct FRigUnitContext;
struct FRigBaseElement;
class URigHierarchy;

DECLARE_DELEGATE_RetVal_ThreeParams(FTransform, FRigAuxiliaryElementGetWorldTransformDelegate, const FRigUnitContext*, const FRigElementKey& /* Key */, bool /* bInitial */);

#define DECLARE_RIG_ELEMENT_METHODS(ElementType) \
template<typename T> \
friend FORCEINLINE const T* Cast(const ElementType* InElement) \
{ \
   return Cast<T>((const FRigBaseElement*) InElement); \
} \
template<typename T> \
friend FORCEINLINE T* Cast(ElementType* InElement) \
{ \
   return Cast<T>((FRigBaseElement*) InElement); \
}

namespace ERigTransformType
{
	enum Type
	{
		InitialLocal,
        CurrentLocal,
        InitialGlobal,
        CurrentGlobal
	};

	FORCEINLINE ERigTransformType::Type SwapCurrentAndInitial(const Type InTransformType)
	{
		switch(InTransformType)
		{
			case CurrentLocal:
			{
				return InitialLocal;
			}
			case CurrentGlobal:
			{
				return InitialGlobal;
			}
			case InitialLocal:
			{
				return CurrentLocal;
			}
			default:
			{
				break;
			}
		}
		return CurrentGlobal;
	}

	FORCEINLINE Type SwapLocalAndGlobal(const Type InTransformType)
	{
		switch(InTransformType)
		{
			case CurrentLocal:
			{
				return CurrentGlobal;
			}
			case CurrentGlobal:
			{
				return CurrentLocal;
			}
			case InitialLocal:
			{
				return InitialGlobal;
			}
			default:
			{
				break;
			}
		}
		return InitialLocal;
	}

	FORCEINLINE Type MakeLocal(const Type InTransformType)
	{
		switch(InTransformType)
		{
			case CurrentLocal:
			case CurrentGlobal:
			{
				return CurrentLocal;
			}
			default:
			{
				break;
			}
		}
		return InitialLocal;
	}

	FORCEINLINE Type MakeGlobal(const Type InTransformType)
	{
		switch(InTransformType)
		{
			case CurrentLocal:
			case CurrentGlobal:
			{
				return CurrentGlobal;
			}
			default:
			{
				break;
			}
		}
		return InitialGlobal;
	}

	FORCEINLINE Type MakeInitial(const Type InTransformType)
	{
		switch(InTransformType)
		{
			case CurrentLocal:
			case InitialLocal:
			{
				return InitialLocal;
			}
			default:
			{
				break;
			}
		}
		return InitialGlobal;
	}

	FORCEINLINE Type MakeCurrent(const Type InTransformType)
	{
		switch(InTransformType)
		{
			case CurrentLocal:
        	case InitialLocal:
			{
				return CurrentLocal;
			}
			default:
			{
				break;
			}
		}
		return CurrentGlobal;
	}

	FORCEINLINE bool IsLocal(const Type InTransformType)
	{
		switch(InTransformType)
		{
			case CurrentLocal:
        	case InitialLocal:
			{
				return true;
			}
			default:
			{
				break;
			}
		}
		return false;
	}

	FORCEINLINE bool IsGlobal(const Type InTransformType)
	{
		switch(InTransformType)
		{
			case CurrentGlobal:
        	case InitialGlobal:
			{
				return true;;
			}
			default:
			{
				break;
			}
		}
		return false;
	}

	FORCEINLINE bool IsInitial(const Type InTransformType)
	{
		switch(InTransformType)
		{
			case InitialLocal:
        	case InitialGlobal:
			{
				return true;
			}
			default:
			{
				break;
			}
		}
		return false;
	}

	FORCEINLINE bool IsCurrent(const Type InTransformType)
	{
		switch(InTransformType)
		{
			case CurrentLocal:
        	case CurrentGlobal:
			{
				return true;
			}
			default:
			{
				break;
			}
		}
		return false;
	}
}

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigComputedTransform
{
	GENERATED_BODY()

	FRigComputedTransform()
	: Transform(FTransform::Identity)
	, bDirty(false)
	{}

	void Save(FArchive& Ar);
	void Load(FArchive& Ar);

	FORCEINLINE_DEBUGGABLE void Set(const FTransform& InTransform)
	{
#if WITH_EDITOR
		ensure(InTransform.GetRotation().IsNormalized());
#endif
		// ensure(!FMath::IsNearlyZero(InTransform.GetScale3D().X));
		// ensure(!FMath::IsNearlyZero(InTransform.GetScale3D().Y));
		// ensure(!FMath::IsNearlyZero(InTransform.GetScale3D().Z));
		Transform = InTransform;
		bDirty = false;
	}

	FORCEINLINE static bool Equals(const FTransform& A, const FTransform& B, const float InTolerance = 0.0001f)
	{
		return (A.GetTranslation() - B.GetTranslation()).IsNearlyZero(InTolerance) &&
			A.GetRotation().Equals(B.GetRotation(), InTolerance) &&
			(A.GetScale3D() - B.GetScale3D()).IsNearlyZero(InTolerance);
	}

	FORCEINLINE bool operator == (const FRigComputedTransform& Other) const
	{
		return bDirty == Other.bDirty && Equals(Transform, Other.Transform);
    }

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Pose")
	FTransform Transform;

	bool bDirty;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigLocalAndGlobalTransform
{
	GENERATED_BODY()

	FRigLocalAndGlobalTransform()
    : Local()
    , Global()
	{}

	void Save(FArchive& Ar);
	void Load(FArchive& Ar);

	FORCEINLINE bool operator == (const FRigLocalAndGlobalTransform& Other) const
	{
		return Local == Other.Local && Global == Other.Global;
	}

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Pose")
	FRigComputedTransform Local;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Pose")
	FRigComputedTransform Global;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigCurrentAndInitialTransform
{
	GENERATED_BODY()

	FRigCurrentAndInitialTransform()
    : Current()
    , Initial()
	{}

	FORCEINLINE const FRigComputedTransform& operator[](const ERigTransformType::Type InTransformType) const
	{
		switch(InTransformType)
		{
			case ERigTransformType::CurrentLocal:
			{
				return Current.Local;
			}
			case ERigTransformType::CurrentGlobal:
			{
				return Current.Global;
			}
			case ERigTransformType::InitialLocal:
			{
				return Initial.Local;
			}
			default:
			{
				break;
			}
		}
		return Initial.Global;
	}

	FORCEINLINE FRigComputedTransform& operator[](const ERigTransformType::Type InTransformType)
	{
		switch(InTransformType)
		{
			case ERigTransformType::CurrentLocal:
			{
				return Current.Local;
			}
			case ERigTransformType::CurrentGlobal:
			{
				return Current.Global;
			}
			case ERigTransformType::InitialLocal:
			{
				return Initial.Local;
			}
			default:
			{
				break;
			}
		}
		return Initial.Global;
	}

	FORCEINLINE const FTransform& Get(const ERigTransformType::Type InTransformType) const
	{
		return operator[](InTransformType).Transform;
	}

	FORCEINLINE void Set(const ERigTransformType::Type InTransformType, const FTransform& InTransform)
	{
		operator[](InTransformType).Set(InTransform);
	}

	FORCEINLINE bool IsDirty(const ERigTransformType::Type InTransformType) const
	{
		return operator[](InTransformType).bDirty;
	}

	FORCEINLINE void MarkDirty(const ERigTransformType::Type InTransformType)
	{
		ensure(!(operator[](ERigTransformType::SwapLocalAndGlobal(InTransformType)).bDirty));
		operator[](InTransformType).bDirty = true;
	}

	void Save(FArchive& Ar);
	void Load(FArchive& Ar);

	FORCEINLINE bool operator == (const FRigCurrentAndInitialTransform& Other) const
	{
		return Current == Other.Current && Initial == Other.Initial;
	}

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Pose")
	FRigLocalAndGlobalTransform Current;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Pose")
	FRigLocalAndGlobalTransform Initial;
};

USTRUCT()
struct CONTROLRIG_API FRigHierarchyCopyPasteContentPerElement
{
	GENERATED_USTRUCT_BODY()

    UPROPERTY()
	FRigElementKey Key;

	UPROPERTY()
	FString Content;

	UPROPERTY()
	TArray<FRigElementKey> Parents;

	UPROPERTY()
	TArray<float> ParentWeights;

	UPROPERTY()
	FRigCurrentAndInitialTransform Pose;
};

USTRUCT()
struct CONTROLRIG_API FRigHierarchyCopyPasteContent
{
	GENERATED_USTRUCT_BODY()

    UPROPERTY()
	TArray<FRigHierarchyCopyPasteContentPerElement> Elements;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigBaseElement
{
	GENERATED_BODY()

public:

	FRigBaseElement()
    : Key()
    , Index(INDEX_NONE)
	, SubIndex(INDEX_NONE)
	, bSelected(false)
	, TopologyVersion(0)
	{}

	virtual ~FRigBaseElement(){}

	enum ESerializationPhase
	{
		StaticData,
		InterElementData
	};

protected:
	
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = RigElement, meta = (AllowPrivateAccess = "true"))
	FRigElementKey Key;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = RigElement, meta = (AllowPrivateAccess = "true"))
	int32 Index;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = RigElement, meta = (AllowPrivateAccess = "true"))
	int32 SubIndex;

	UPROPERTY(BlueprintReadOnly, Transient, Category = RigElement, meta = (AllowPrivateAccess = "true"))
	bool bSelected;

	FORCEINLINE static bool IsClassOf(const FRigBaseElement* InElement)
	{
		return true;
	}

public:

	void Serialize(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase);
	virtual void Save(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase);
	virtual void Load(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase);

	FORCEINLINE const FName& GetName() const { return Key.Name; }
	FORCEINLINE virtual const FName& GetDisplayName() const { return GetName(); }
	FORCEINLINE ERigElementType GetType() const { return Key.Type; }
	FORCEINLINE const FRigElementKey& GetKey() const { return Key; }
	FORCEINLINE int32 GetIndex() const { return Index; }
	FORCEINLINE int32 GetSubIndex() const { return SubIndex; }
	FORCEINLINE bool IsSelected() const { return bSelected; }

	template<typename T>
	FORCEINLINE bool IsA() const { return T::IsClassOf(this); }

	FORCEINLINE bool IsTypeOf(ERigElementType InElementType) const
	{
		return ((uint8)InElementType & (uint8)Key.Type) == (uint8)Key.Type;
	}

	template<typename T>
    friend FORCEINLINE const T* Cast(const FRigBaseElement* InElement)
	{
		if(InElement)
		{
			if(InElement->IsA<T>())
			{
				return static_cast<const T*>(InElement);
			}
		}
		return nullptr;
	}

	template<typename T>
    friend FORCEINLINE T* Cast(FRigBaseElement* InElement)
	{
		if(InElement)
		{
			if(InElement->IsA<T>())
			{
				return static_cast<T*>(InElement);
			}
		}
		return nullptr;
	}

	template<typename T>
    friend FORCEINLINE const T* CastChecked(const FRigBaseElement* InElement)
	{
		const T* Element = Cast<T>(InElement);
		check(Element);
		return Element;
	}

	template<typename T>
    friend FORCEINLINE T* CastChecked(FRigBaseElement* InElement)
	{
		T* Element = Cast<T>(InElement);
		check(Element);
		return Element;
	}

	virtual void CopyPose(FRigBaseElement* InOther, bool bCurrent, bool bInitial) {}

protected:

	mutable uint16 TopologyVersion;
	mutable TArray<FRigBaseElement*> CachedChildren;

	friend class URigHierarchy;
	friend class URigHierarchyController;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigTransformElement : public FRigBaseElement
{
public:
	
	GENERATED_BODY()
	DECLARE_RIG_ELEMENT_METHODS(FRigTransformElement)

	virtual ~FRigTransformElement(){}

	virtual void Save(FArchive& A, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase) override;
	virtual void Load(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase) override;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = RigElement, meta = (DisplayAfter = "Index", ShowOnlyInnerProperties))
	FRigCurrentAndInitialTransform Pose;

protected:

	struct FElementToDirty
	{
		FORCEINLINE FElementToDirty()
			: Element(nullptr)
			, HierarchyDistance(INDEX_NONE)
		{}

		FORCEINLINE FElementToDirty(FRigTransformElement* InElement, int32 InHierarchyDistance = INDEX_NONE)
			: Element(InElement)
			, HierarchyDistance(InHierarchyDistance)
		{}

		FORCEINLINE bool operator ==(const FElementToDirty& Other) const
		{
			return Element == Other.Element;
		}

		FORCEINLINE bool operator !=(const FElementToDirty& Other) const
		{
			return Element != Other.Element;
		}
		
		FRigTransformElement* Element;
		int32 HierarchyDistance;
	};
	
	TArray<FElementToDirty> ElementsToDirty;

	FORCEINLINE static bool IsClassOf(const FRigBaseElement* InElement)
	{
		return InElement->GetType() == ERigElementType::Bone ||
			InElement->GetType() == ERigElementType::Space ||
			InElement->GetType() == ERigElementType::Control ||
			InElement->GetType() == ERigElementType::RigidBody ||
			InElement->GetType() == ERigElementType::Auxiliary;
	}

public:
	
	virtual void CopyPose(FRigBaseElement* InOther, bool bCurrent, bool bInitial) override;

protected:
	
	friend struct FRigBaseElement;
	friend struct FRigSingleParentElement;
	friend struct FRigMultiParentElement;
	friend class URigHierarchy;
	friend class URigHierarchyController;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigSingleParentElement : public FRigTransformElement
{
public:
	
	GENERATED_BODY()
	DECLARE_RIG_ELEMENT_METHODS(FRigSingleParentElement)

	FRigSingleParentElement()
	: FRigTransformElement()
	, ParentElement(nullptr)
	{}

	virtual ~FRigSingleParentElement(){}

	virtual void Save(FArchive& A, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase) override;
	virtual void Load(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase) override;

	FRigTransformElement* ParentElement;

protected:
	
	FORCEINLINE static bool IsClassOf(const FRigBaseElement* InElement)
	{
		return InElement->GetType() == ERigElementType::Bone ||
			InElement->GetType() == ERigElementType::RigidBody ||
			InElement->GetType() == ERigElementType::Auxiliary;
	}

	friend struct FRigBaseElement;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigMultiParentElement : public FRigTransformElement
{
public:
	
	GENERATED_BODY()
	DECLARE_RIG_ELEMENT_METHODS(FRigMultiParentElement)

    FRigMultiParentElement()
    : FRigTransformElement()
	{}

	virtual ~FRigMultiParentElement(){}

	virtual void Save(FArchive& A, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase) override;
	virtual void Load(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase) override;

	UPROPERTY(BlueprintReadOnly, Category = RigElement)
	FRigCurrentAndInitialTransform Parent;

	TArray<FRigTransformElement*> ParentElements;
	TArray<float> ParentWeights;
	TArray<float> ParentWeightsInitial;
	TMap<FRigElementKey, int32> IndexLookup;

protected:

	FORCEINLINE static bool IsClassOf(const FRigBaseElement* InElement)
	{
		return InElement->GetType() == ERigElementType::Space ||
			InElement->GetType() == ERigElementType::Control;
	}

public:
	
	virtual void CopyPose(FRigBaseElement* InOther, bool bCurrent, bool bInitial) override;

protected:
	
	friend struct FRigBaseElement;
};


USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigBoneElement : public FRigSingleParentElement
{
public:
	
	GENERATED_BODY()
	DECLARE_RIG_ELEMENT_METHODS(FRigBoneElement)

	FRigBoneElement()
		: FRigSingleParentElement()
	{
		Key.Type = ERigElementType::Bone;
		BoneType = ERigBoneType::User;
	}
	
	virtual ~FRigBoneElement(){}

	virtual void Save(FArchive& A, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase) override;
	virtual void Load(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase) override;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category = RigElement)
	ERigBoneType BoneType;

protected:
	
	FORCEINLINE static bool IsClassOf(const FRigBaseElement* InElement)
	{
		return InElement->GetType() == ERigElementType::Bone;
	}

	friend struct FRigBaseElement;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigSpaceElement : public FRigMultiParentElement
{
public:
	
	GENERATED_BODY()
	DECLARE_RIG_ELEMENT_METHODS(FRigSpaceElement)

	FRigSpaceElement()
    : FRigMultiParentElement()
	{
		Key.Type = ERigElementType::Space; 
	}

	virtual ~FRigSpaceElement(){}

protected:
	
	FORCEINLINE static bool IsClassOf(const FRigBaseElement* InElement)
	{
		return InElement->GetType() == ERigElementType::Space;
	}

	friend struct FRigBaseElement;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigControlSettings
{
	GENERATED_BODY()

	FRigControlSettings();

	void Save(FArchive& Ar);
	void Load(FArchive& Ar);

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Control)
	ERigControlType ControlType;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Control)
	FName DisplayName;

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

	/** The minimum limit of the control's value */
	UPROPERTY(BlueprintReadWrite, Category = Limits, meta = (EditCondition = "bLimitTranslation || bLimitRotation || bLimitScale"))
	FRigControlValue MinimumValue;

	/** The maximum limit of the control's value */
	UPROPERTY(BlueprintReadWrite, Category = Limits, meta = (EditCondition = "bLimitTranslation || bLimitRotation || bLimitScale"))
	FRigControlValue MaximumValue;

	/** Set to true if the gizmo is enabled in 3d */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Gizmo)
	bool bGizmoEnabled;

	/** Set to true if the gizmo is currently visible in 3d */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Gizmo, meta = (EditCondition = "bGizmoEnabled"))
	bool bGizmoVisible;

	/* This is optional UI setting - this doesn't mean this is always used, but it is optional for manipulation layer to use this*/
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Gizmo, meta = (EditCondition = "bGizmoEnabled"))
	FName GizmoName;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Gizmo, meta = (EditCondition = "bGizmoEnabled"))
	FLinearColor GizmoColor;

	/** If the control is transient and only visible in the control rig editor */
	UPROPERTY(BlueprintReadWrite, Category = Control)
	bool bIsTransientControl;

	/** If the control is 4transient and only visible in the control rig editor */
	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Control)
	UEnum* ControlEnum;

	/** Applies the limits expressed by these settings to a value */
	FORCEINLINE void ApplyLimits(FRigControlValue& InOutValue) const
	{
		InOutValue.ApplyLimits(bLimitTranslation, bLimitRotation, bLimitScale, ControlType, MinimumValue, MaximumValue);
	}

	/** Applies the limits expressed by these settings to a transform */
	FORCEINLINE void ApplyLimits(FTransform& InOutValue) const
	{
		FRigControlValue Value;
		Value.SetFromTransform(InOutValue, ControlType, PrimaryAxis);
		ApplyLimits(Value);
		InOutValue = Value.GetAsTransform(ControlType, PrimaryAxis);
	}

	FORCEINLINE bool IsValueTypeEnabled(ERigControlValueType InValueType) const
	{
		if(InValueType == ERigControlValueType::Minimum || InValueType == ERigControlValueType::Maximum)
		{
			return bLimitTranslation || bLimitRotation || bLimitScale;
		}
		return true;
	}

	FORCEINLINE FRigControlValue GetIdentityValue() const
	{
		FRigControlValue Value;
		Value.SetFromTransform(FTransform::Identity, ControlType, PrimaryAxis);
		return Value;
	}
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigControlElement : public FRigMultiParentElement
{
	public:
	
	GENERATED_BODY()
	DECLARE_RIG_ELEMENT_METHODS(FRigControlElement)

	FRigControlElement()
		: FRigMultiParentElement()
	{
		Key.Type = ERigElementType::Control; 
	}

	virtual ~FRigControlElement(){}
	
	FORCEINLINE virtual const FName& GetDisplayName() const override
	{
		if(!Settings.DisplayName.IsNone())
		{
			return Settings.DisplayName;
		}
		return FRigMultiParentElement::GetDisplayName();
	}

	virtual void Save(FArchive& A, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase) override;
	virtual void Load(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase) override;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Control, meta=(ShowOnlyInnerProperties))
	FRigControlSettings Settings;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = RigElement)
	FRigCurrentAndInitialTransform Offset;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = RigElement)
	FRigCurrentAndInitialTransform Gizmo;

protected:
	
	FORCEINLINE static bool IsClassOf(const FRigBaseElement* InElement)
	{
		return InElement->GetType() == ERigElementType::Control;
	}

public:
	
	virtual void CopyPose(FRigBaseElement* InOther, bool bCurrent, bool bInitial) override;

protected:

	friend struct FRigBaseElement;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigCurveElement : public FRigBaseElement
{
public:
	
	GENERATED_BODY()
	DECLARE_RIG_ELEMENT_METHODS(FRigCurveElement)

	FRigCurveElement()
		: FRigBaseElement()
		, Value(0.f)
	{
		Key.Type = ERigElementType::Curve;
	}

	virtual ~FRigCurveElement(){}

	virtual void Save(FArchive& A, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase) override;
	virtual void Load(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase) override;

	float Value;
	
	FORCEINLINE static bool IsClassOf(const FRigBaseElement* InElement)
	{
		return InElement->GetType() == ERigElementType::Curve;
	}

public:
	
	virtual void CopyPose(FRigBaseElement* InOther, bool bCurrent, bool bInitial) override;

protected:
	
	friend struct FRigBaseElement;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigRigidBodySettings
{
	GENERATED_BODY()

	FRigRigidBodySettings();

	void Save(FArchive& Ar);
	void Load(FArchive& Ar);


	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Control)
	float Mass;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigRigidBodyElement : public FRigSingleParentElement
{
public:
	
	GENERATED_BODY()
	DECLARE_RIG_ELEMENT_METHODS(FRigRigidBodyElement)

    FRigRigidBodyElement()
        : FRigSingleParentElement()
	{
		Key.Type = ERigElementType::RigidBody;
	}
	
	virtual ~FRigRigidBodyElement(){}

	virtual void Save(FArchive& A, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase) override;
	virtual void Load(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase) override;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = Control, meta=(ShowOnlyInnerProperties))
	FRigRigidBodySettings Settings;

protected:
	
	FORCEINLINE static bool IsClassOf(const FRigBaseElement* InElement)
	{
		return InElement->GetType() == ERigElementType::RigidBody;
	}

	friend struct FRigBaseElement;
};

USTRUCT(BlueprintType)
struct CONTROLRIG_API FRigAuxiliaryElement : public FRigSingleParentElement
{
public:
	
	GENERATED_BODY()
	DECLARE_RIG_ELEMENT_METHODS(FRigAuxiliaryElement)

    FRigAuxiliaryElement()
        : FRigSingleParentElement()
	{
		Key.Type = ERigElementType::Auxiliary;
	}
	
	virtual ~FRigAuxiliaryElement(){}

	virtual void Save(FArchive& A, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase) override;
	virtual void Load(FArchive& Ar, URigHierarchy* Hierarchy, ESerializationPhase SerializationPhase) override;

	FTransform GetAuxiliaryWorldTransform(const FRigUnitContext* InContext, bool bInitial) const;

	virtual void CopyPose(FRigBaseElement* InOther, bool bCurrent, bool bInitial) override;

protected:

	FRigAuxiliaryElementGetWorldTransformDelegate GetWorldTransformDelegate;

	FORCEINLINE static bool IsClassOf(const FRigBaseElement* InElement)
	{
		return InElement->GetType() == ERigElementType::Auxiliary;
	}

	friend struct FRigBaseElement;
	friend class URigHierarchyController;
};