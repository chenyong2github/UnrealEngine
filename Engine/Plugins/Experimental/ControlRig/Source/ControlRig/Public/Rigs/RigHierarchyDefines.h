// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigHierarchyDefines.generated.h"

struct FRigHierarchyContainer;

/* 
 * This is rig element types that we support
 * This can be used as a mask so supported as a bitfield
 */
UENUM()
enum class ERigElementType : uint8
{
	None UMETA(Hidden),
	Bone = 0x001,
	Space = 0x002,
	Control = 0x004,
	Curve = 0x008,
	All = Bone | Space | Control | Curve UMETA(Hidden),
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

struct FRigElementKey
{
	FName Name;
	ERigElementType Type;

	FRigElementKey()
		: Name(NAME_None)
		, Type(ERigElementType::None)
	{}

	FRigElementKey(const FName& InName, ERigElementType InType)
		: Name(InName)
		, Type(InType)
	{}

	FORCEINLINE bool IsValid() const
	{
		return Name != NAME_None && Type != ERigElementType::None;
	}

	FORCEINLINE operator bool() const
	{
		return IsValid();
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
	
	UPROPERTY(EditAnywhere, Category = FRigElement)
	FName Name;

	UPROPERTY(VisibleAnywhere, Category = FRigElement)
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

DECLARE_MULTICAST_DELEGATE_TwoParams(FRigElementChanged, FRigHierarchyContainer*, const FRigElementKey&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FRigElementAdded, FRigHierarchyContainer*, const FRigElementKey&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FRigElementRemoved, FRigHierarchyContainer*, const FRigElementKey&);
DECLARE_MULTICAST_DELEGATE_FourParams(FRigElementRenamed, FRigHierarchyContainer*, ERigElementType, const FName&, const FName&);
DECLARE_MULTICAST_DELEGATE_FourParams(FRigElementReparented, FRigHierarchyContainer*, const FRigElementKey&, const FName&, const FName&);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FRigElementSelected, FRigHierarchyContainer*, const FRigElementKey&, bool);
