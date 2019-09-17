// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigHierarchyDefines.h"
#include "RigControlHierarchy.generated.h"

class UControlRig;

UENUM()
enum class ERigControlType : uint8
{
	Bool,
	Float,
	Vector2D,
	Position,
	Scale,
	Quat,
	Rotator,
	Transform
};

USTRUCT()
struct CONTROLRIG_API FRigControlValue
{
	GENERATED_BODY()

public:

	FRigControlValue()
		:Storage(FTransform::Identity)
	{
	}

	template<class T>
	FORCEINLINE T Get() const
	{
		return GetRef<T>();
	}

	template<class T>
	FORCEINLINE T& GetRef()
	{
		ensure(sizeof(T) <= sizeof(FTransform));
		return *(T*)&Storage;
	}

	template<class T>
	FORCEINLINE const T& GetRef() const
	{
		ensure(sizeof(T) <= sizeof(FTransform));
		return *(T*)&Storage;
	}

	template<class T>
	FORCEINLINE void Set(T InValue)
	{
		GetRef<T>() = InValue;
	}

	template<class T>
	FORCEINLINE static FRigControlValue Make(T InValue)
	{
		FRigControlValue Value;
		Value.Set<T>(InValue);
		return Value;
	}

private:
	
	UPROPERTY()
	FTransform Storage;
};


class UStaticMesh;

USTRUCT()
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
#if WITH_EDITORONLY_DATA
		, GizmoName(TEXT("Gizmo"))
		, GizmoTransform(FTransform::Identity)
		, GizmoColor(FLinearColor::Red)
#endif
		, Dependents()
	{
	}
	virtual ~FRigControl() {}

	UPROPERTY(VisibleAnywhere, Category = FRigElement)
	ERigControlType ControlType;

	UPROPERTY(VisibleAnywhere, Category = FRigElement)
	FName ParentName;

	UPROPERTY(transient)
	int32 ParentIndex;

	UPROPERTY(VisibleAnywhere, Category = FRigElement)
	FName SpaceName;

	UPROPERTY(transient)
	int32 SpaceIndex;

	UPROPERTY(VisibleAnywhere, Category = FRigElement)
	FRigControlValue InitialValue;

	UPROPERTY(transient, VisibleAnywhere, Category = FRigElement)
	FRigControlValue Value;

#if WITH_EDITORONLY_DATA
	/* This is optional UI setting - this doesn't mean this is always used, but it is optional for manipulation layer to use this*/
	UPROPERTY(EditAnywhere, Category = FRigElement)
	FName GizmoName;

	UPROPERTY(EditAnywhere, Category = FRigElement)
	FTransform GizmoTransform;

	UPROPERTY(EditAnywhere, Category = FRigElement)
	FLinearColor GizmoColor;
#endif // WITH_EDITORONLY_DATA

	/** dependent list - direct dependent for child or anything that needs to update due to this */
	UPROPERTY(transient)
	TArray<int32> Dependents;

	FORCEINLINE virtual ERigElementType GetElementType() const override
	{
		return ERigElementType::Control;
	}

	FORCEINLINE virtual FRigElementKey GetParentElementKey() const
	{
		return FRigElementKey(ParentName, GetElementType());
	}

	FORCEINLINE virtual FRigElementKey GetSpaceElementKey() const
	{
		return FRigElementKey(SpaceName, ERigElementType::Space);
	}
};

USTRUCT()
struct CONTROLRIG_API FRigControlHierarchy
{
	GENERATED_BODY()

	FRigControlHierarchy();
	FRigControlHierarchy& operator= (const FRigControlHierarchy &InOther);

	FORCEINLINE ERigElementType RigElementType() const { return ERigElementType::Control; }

	FORCEINLINE int32 Num() const { return Controls.Num(); }
	FORCEINLINE const FRigControl& operator[](int32 InIndex) const { return Controls[InIndex]; }
	FORCEINLINE FRigControl& operator[](int32 InIndex) { return Controls[InIndex]; }
	FORCEINLINE const FRigControl& operator[](const FName& InName) const { return Controls[GetIndex(InName)]; }
	FORCEINLINE FRigControl& operator[](const FName& InName) { return Controls[GetIndex(InName)]; }

	FORCEINLINE const TArray<FRigControl>& GetControls() const { return Controls; }

	FORCEINLINE TArray<FRigControl>::RangedForIteratorType      begin()       { return Controls.begin(); }
	FORCEINLINE TArray<FRigControl>::RangedForConstIteratorType begin() const { return Controls.begin(); }
	FORCEINLINE TArray<FRigControl>::RangedForIteratorType      end()         { return Controls.end();   }
	FORCEINLINE TArray<FRigControl>::RangedForConstIteratorType end() const   { return Controls.end();   }

	FORCEINLINE bool IsNameAvailable(const FName& InPotentialNewName) const { return GetIndex(InPotentialNewName) == INDEX_NONE; }

	FName GetSafeNewName(const FName& InPotentialNewName) const;

	FRigControl& Add(
		const FName& InNewName,
		ERigControlType InControlType = ERigControlType::Transform,
		const FName& InParentName = NAME_None,
		const FName& InSpaceName = NAME_None,
		const FRigControlValue& InValue = FRigControlValue()
#if WITH_EDITORONLY_DATA
		, const FName& InGizmoName = TEXT("Gizmo"),
		const FTransform& InGizmoTransform = FTransform::Identity,
		const FLinearColor& InGizmoColor = FLinearColor::Red
#endif
	);

	FRigControl Remove(const FName& InNameToRemove);

	FName Rename(const FName& InOldName, const FName& InNewName);

	bool Reparent(const FName& InName, const FName& InNewParentName);

	void SetSpace(const FName& InName, const FName& InNewSpaceName);

	FName GetName(int32 InIndex) const;

	FORCEINLINE int32 GetIndex(const FName& InName) const
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

	void SetLocalTransform(const FName& InName, const FTransform& InTransform);

	void SetLocalTransform(int32 InIndex, const FTransform& InTransform);

	FTransform GetLocalTransform(const FName& InName) const;

	FTransform GetLocalTransform(int32 InIndex) const;

	FTransform GetParentTransform(int32 InIndex) const;

	// this is only valid if you have trasnform as type
	// setting initial transform from global transform (control transform is based on parent)
	void SetInitialGlobalTransform(const FName& InName, const FTransform& GlobalTransform);
	
	void SetInitialGlobalTransform(int32 InIndex, const FTransform& GlobalTransform);
	
	FTransform GetInitialGlobalTransform(const FName& InName) const;
	
	FTransform GetInitialGlobalTransform(int32 InIndex) const;
	
	void SetValue(const FName& InName, const FRigControlValue& InValue);

	void SetValue(int32 InIndex, const FRigControlValue& InValue);

	template<class T>
	FORCEINLINE void SetValue(const FName& InName, T InValue)
	{
		SetValue(InName, FRigControlValue::Make<T>(InValue));
	}

	template<class T>
	FORCEINLINE void SetValue(int32 InIndex, T InValue)
	{
		SetValue(InIndex, FRigControlValue::Make<T>(InValue));
	}

	FRigControlValue GetValue(const FName& InName) const;

	FRigControlValue GetValue(int32 InIndex) const;

	template<class T>
	FORCEINLINE T GetValue(const FName& InName) const
	{
		return GetValue(InName).Get<T>();
	}

	template<class T>
	FORCEINLINE T GetValue(int32 InIndex) const
	{
		return GetValue(InIndex).Get<T>();
	}

	void SetInitialValue(const FName& InName, const FRigControlValue& InValue);

	void SetInitialValue(int32 InIndex, const FRigControlValue& InValue);

	template<class T>
	FORCEINLINE void SetInitialValue(const FName& InName, T InValue)
	{
		SetInitialValue(InName, FRigControlValue::Make<T>(InValue));
	}

	template<class T>
	FORCEINLINE void SetInitialValue(int32 InIndex, T InValue)
	{
		SetInitialValue(InIndex, FRigControlValue::Make<T>(InValue));
	}

	FRigControlValue GetInitialValue(const FName& InName) const;

	FRigControlValue GetInitialValue(int32 InIndex) const;

	template<class T>
	FORCEINLINE T GetInitialValue(const FName& InName) const
	{
		return GetInitialValue(InName).Get<T>();
	}

	template<class T>
	FORCEINLINE T GetInitialValue(int32 InIndex) const
	{
		return GetInitialValue(InIndex).Get<T>();
	}

	// updates all of the internal caches
	void Initialize();

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
};
