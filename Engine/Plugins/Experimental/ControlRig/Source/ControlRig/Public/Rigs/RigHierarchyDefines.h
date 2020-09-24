// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RigHierarchyDefines.generated.h"

struct FRigHierarchyContainer;

/* 
 * This is rig element types that we support
 * This can be used as a mask so supported as a bitfield
 */
UENUM(BlueprintType)
enum class ERigElementType : uint8
{
	None,
	Bone = 0x001,
	Space = 0x002,
	Control = 0x004,
	Curve = 0x008,
	All = Bone | Space | Control | Curve,
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

	FORCEINLINE bool IsValid() const
	{
		return Name != NAME_None && Type != ERigElementType::None;
	}

	FORCEINLINE operator bool() const
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
			case ERigElementType::Space:
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
		const FRigHierarchyContainer* InContainer, 
		const FRigElementKey& InParentKey,
		bool bRecursive = true,
		bool bIncludeParent = false,
		uint8 InElementTypes = (uint8)ERigElementType::All);

	// creates a collection containing all of the elements with a given name
	static FRigElementKeyCollection MakeFromName(
		const FRigHierarchyContainer* InContainer,
		const FName& InPartialName,
		uint8 InElementTypes = (uint8)ERigElementType::All);

	// creates a collection containing an item chain
	static FRigElementKeyCollection MakeFromChain(
		const FRigHierarchyContainer* InContainer,
		const FRigElementKey& InFirstItem,
		const FRigElementKey& InLastItem,
		bool bReverse = false);

	// creates a collection containing all keys of a hierarchy
	static FRigElementKeyCollection MakeFromCompleteHierarchy(
		const FRigHierarchyContainer* InContainer,
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

DECLARE_MULTICAST_DELEGATE_TwoParams(FRigElementChanged, FRigHierarchyContainer*, const FRigElementKey&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FRigElementAdded, FRigHierarchyContainer*, const FRigElementKey&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FRigElementRemoved, FRigHierarchyContainer*, const FRigElementKey&);
DECLARE_MULTICAST_DELEGATE_FourParams(FRigElementRenamed, FRigHierarchyContainer*, ERigElementType, const FName&, const FName&);
DECLARE_MULTICAST_DELEGATE_FourParams(FRigElementReparented, FRigHierarchyContainer*, const FRigElementKey&, const FName&, const FName&);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FRigElementSelected, FRigHierarchyContainer*, const FRigElementKey&, bool);
DECLARE_MULTICAST_DELEGATE_TwoParams(FRigEventDelegate, FRigHierarchyContainer*, const FRigEventContext&);
