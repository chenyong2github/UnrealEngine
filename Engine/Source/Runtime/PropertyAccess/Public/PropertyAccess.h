// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "UObject/Object.h"
#include "PropertyEventInterfaces.h"
#include "IPropertyAccess.h"

#include "PropertyAccess.generated.h"

struct FPropertyAccessSystem;
enum class EPropertyAccessType : uint8;

namespace PropertyAccess
{
	/** 
	 * Called to patch up library after it is loaded.
	 * This converts all FName-based paths into node-based paths that provide an optimized way of accessing properties.
	 */
	PROPERTYACCESS_API extern void PostLoadLibrary(FPropertyAccessLibrary& InLibrary);

	/** 
	 * Process a 'tick' of a property access instance. 
	 * Note internally allocates via FMemStack and pushes its own FMemMark
	 */
	PROPERTYACCESS_API extern void ProcessCopies(UObject* InObject, const FPropertyAccessLibrary& InLibrary, EPropertyAccessCopyBatch InBatchType);

	/** 
	 * Process a single copy 
	 * Note that this can potentially allocate via FMemStack, so inserting FMemMark before a number of these calls is recommended
	 */
	PROPERTYACCESS_API extern void ProcessCopy(UObject* InObject, const FPropertyAccessLibrary& InLibrary, EPropertyAccessCopyBatch InBatchType, int32 InCopyIndex, TFunctionRef<void(const FProperty*, void*)> InPostCopyOperation);

	/** Bind all event-type accesses to their respective objects */
	PROPERTYACCESS_API extern void BindEvents(UObject* InObject, const FPropertyAccessLibrary& InLibrary);

	/** Resolve a path to an event Id for the specified class */
	PROPERTYACCESS_API extern int32 GetEventId(const UClass* InClass, TArrayView<const FName> InPath);
}

// The type of an indirection
UENUM()
enum class EPropertyAccessIndirectionType : uint8
{
	// Access node is a simple basePtr + offset
	Offset,

	// Access node needs to dereference an object at its current address
	Object,

	// Access node indexes a dynamic array
	Array,

	// Access node calls a script function to get a value
	ScriptFunction,

	// Access node calls a native function to get a value
	NativeFunction,
};

// For object nodes, we need to know what type of object we are looking at so we can cast appropriately
UENUM()
enum class EPropertyAccessObjectType : uint8
{
	// Access is not an object
	None,

	// Access is an object
	Object,

	// Access is a weak object
	WeakObject,

	// Access is a soft object
	SoftObject,
};

// Runtime-generated access node.
// Represents:
// - An offset within an object 
// - An indirection to follow (object, array, function)
USTRUCT()
struct FPropertyAccessIndirection
{
	GENERATED_BODY()

	FPropertyAccessIndirection() = default;

private:
	friend struct ::FPropertyAccessSystem;

	// Array property if this is an array indirection
	UPROPERTY()
	TFieldPath<FArrayProperty> ArrayProperty;

	// Function if this is a script of native function indirection
	UPROPERTY()
	TObjectPtr<UFunction> Function = nullptr;

	// Return buffer size if this is a script of native function indirection
	UPROPERTY()
	int32 ReturnBufferSize = 0;

	// Return buffer alignment if this is a script of native function indirection
	UPROPERTY()
	int32 ReturnBufferAlignment = 0;

	// Array index if this is an array indirection
	UPROPERTY()
	int32 ArrayIndex = INDEX_NONE;

	// Offset of this indirection within its containing object
	UPROPERTY()
	uint32 Offset = 0;

	// Object type if this is an object indirection
	UPROPERTY()
	EPropertyAccessObjectType ObjectType = EPropertyAccessObjectType::None;

	// The type of this indirection
	UPROPERTY()
	EPropertyAccessIndirectionType Type = EPropertyAccessIndirectionType::Offset;
};

// A single property access list. This is a list of FPropertyAccessIndirection
USTRUCT()
struct FPropertyAccessIndirectionChain
{
	GENERATED_BODY()

	FPropertyAccessIndirectionChain() = default;

private:
	friend struct ::FPropertyAccessSystem;

	// Leaf property
	UPROPERTY()
	TFieldPath<FProperty> Property = nullptr;

	// Index of the first indirection of a property access
	UPROPERTY()
	int32 IndirectionStartIndex = INDEX_NONE;

	// Index of the last indirection of a property access
	UPROPERTY()
	int32 IndirectionEndIndex = INDEX_NONE;

	// If this access is an event, then this will be the event Id of the property
	UPROPERTY()
	int32 EventId = INDEX_NONE;
};

// Flags for a segment of a property access path
// Note: NOT an UENUM as we dont support mixing flags and values properly in UENUMs, e.g. for serialization.
enum class EPropertyAccessSegmentFlags : uint16
{
	// Segment has not been resolved yet, we don't know anything about it
	Unresolved = 0,

	// Segment is a struct property
	Struct,

	// Segment is a leaf property
	Leaf,

	// Segment is an object
	Object,

	// Segment is a weak object
	WeakObject,

	// Segment is a soft object
	SoftObject,

	// Segment is a dynamic array. If the index is INDEX_NONE, then the entire array is referenced.
	Array,

	// Segment is a dynamic array of structs. If the index is INDEX_NONE, then the entire array is referenced.
	ArrayOfStructs,

	// Segment is a dynamic array of objects. If the index is INDEX_NONE, then the entire array is referenced.
	ArrayOfObjects,

	// Entries before this are exclusive values
	LastExclusiveValue = ArrayOfObjects,

	// Segment is an object key for an event (object)
	Event			= (1 << 14),

	// Segment is a function
	Function		= (1 << 15),

	// All modifier flags
	ModifierFlags = (Event | Function),
};

ENUM_CLASS_FLAGS(EPropertyAccessSegmentFlags);

// A segment of a 'property path' used to access an object's properties from another location
USTRUCT()
struct FPropertyAccessSegment
{
	GENERATED_BODY()

	FPropertyAccessSegment() = default;

private:
	friend struct FPropertyAccessSystem;
	friend struct FPropertyAccessEditorSystem;

	/** The sub-component of the property path, a single value between .'s of the path */
	UPROPERTY()
	FName Name = NAME_None;

	/** The Class or ScriptStruct that was used last to resolve Name to a property. */
	UPROPERTY()
	TObjectPtr<UStruct> Struct = nullptr;

	/** The cached property on the Struct that this Name resolved to at compile time. If this is a Function segment, then this is the return property of the function. */
	UPROPERTY()
	TFieldPath<FProperty> Property;

	/** If this segment is a function, EPropertyAccessSegmentFlags::Function flag will be present and this value will be valid */
	UPROPERTY()
	TObjectPtr<UFunction> Function = nullptr;

	/** The optional array index. */
	UPROPERTY()
	int32 ArrayIndex = INDEX_NONE;

	/** @see EPropertyAccessSegmentFlags */
	UPROPERTY()
	uint16 Flags = (uint16)EPropertyAccessSegmentFlags::Unresolved;
};

// A property access path. References a string of property access segments.
// These are resolved at load time to create corresponding FPropertyAccess entries
USTRUCT() 
struct FPropertyAccessPath
{
	GENERATED_BODY()

	FPropertyAccessPath()
		: PathSegmentStartIndex(INDEX_NONE)
		, PathSegmentCount(INDEX_NONE)
		, bHasEvents(false)
	{
	}

private:
	friend struct FPropertyAccessSystem;
	friend struct FPropertyAccessEditorSystem;

	// Index into the library's path segments. Used to provide a starting point for a path resolve
	UPROPERTY()
	int32 PathSegmentStartIndex = INDEX_NONE;

	// The count of the path segments.
	UPROPERTY()
	int32 PathSegmentCount = INDEX_NONE;

	// Whether this access has events in its path
	UPROPERTY()
	uint8 bHasEvents : 1;
};

UENUM()
enum class EPropertyAccessCopyType : uint8
{
	// No copying
	None,

	// For plain old data types, we do a simple memcpy.
	Plain,

	// For more complex data types, we need to call the properties copy function
	Complex,

	// Read and write properties using bool property helpers, as source/dest could be bitfield or boolean
	Bool,
	
	// Use struct copy operation, as this needs to correctly handle CPP struct ops
	Struct,

	// Read and write properties using object property helpers, as source/dest could be regular/weak/soft etc.
	Object,

	// FName needs special case because its size changes between editor/compiler and runtime.
	Name,

	// Array needs special handling for fixed size arrays
	Array,

	// Promote the type during the copy
	// Bool promotions
	PromoteBoolToByte,
	PromoteBoolToInt32,
	PromoteBoolToInt64,
	PromoteBoolToFloat,

	// Byte promotions
	PromoteByteToInt32,
	PromoteByteToInt64,
	PromoteByteToFloat,

	// Int32 promotions
	PromoteInt32ToInt64,
	PromoteInt32ToFloat,		// This is strictly sketchy because of potential data loss, but it is usually OK in the general case
};

// A property copy, represents a one-to-many copy operation
USTRUCT() 
struct FPropertyAccessCopy
{
	GENERATED_BODY()

	FPropertyAccessCopy() = default;

private:
	friend struct FPropertyAccessSystem;
	friend struct FPropertyAccessEditorSystem;

	// Index into the library's Accesses
	UPROPERTY()
	int32 AccessIndex = INDEX_NONE;

	// Index of the first of the library's DescAccesses
	UPROPERTY()
	int32 DestAccessStartIndex = INDEX_NONE;

	// Index of the last of the library's DescAccesses
	UPROPERTY()
	int32 DestAccessEndIndex = INDEX_NONE;

	UPROPERTY()
	EPropertyAccessCopyType Type = EPropertyAccessCopyType::Plain;
};

USTRUCT()
struct FPropertyAccessCopyBatch
{
	GENERATED_BODY()

	FPropertyAccessCopyBatch() = default;

private:
	friend struct FPropertyAccessSystem;
	friend struct FPropertyAccessEditorSystem;

	UPROPERTY()
	TArray<FPropertyAccessCopy> Copies;
};

/** A library of property paths used within a specific context (e.g. a class) */
USTRUCT()
struct FPropertyAccessLibrary
{
	GENERATED_BODY()

	FPropertyAccessLibrary() = default;

private:
	friend struct FPropertyAccessSystem;
	friend struct FPropertyAccessEditorSystem;

	// All path segments in this library.
	UPROPERTY()
	TArray<FPropertyAccessSegment> PathSegments;

	// All source paths
	UPROPERTY()
	TArray<FPropertyAccessPath> SrcPaths;

	// All destination paths
	UPROPERTY()
	TArray<FPropertyAccessPath> DestPaths;

	// All copy operations
	UPROPERTY()
	FPropertyAccessCopyBatch CopyBatches[(uint8)EPropertyAccessCopyBatch::Count];

	// All source property accesses
	UPROPERTY(Transient)
	TArray<FPropertyAccessIndirectionChain> SrcAccesses;

	// All destination accesses (that are copied to our instances).
	UPROPERTY(Transient)
	TArray<FPropertyAccessIndirectionChain> DestAccesses;

	// Indirections
	UPROPERTY(Transient)
	TArray<FPropertyAccessIndirection> Indirections;

	// Indexes into the SrcAccesses array to allow faster iteration of all event accesses
	UPROPERTY()
	TArray<int32> EventAccessIndices;

	// Whether this library has been post-loaded
	bool bHasBeenPostLoaded = false;

	// A per-class mapping
	struct FEventMapping
	{
		// The class that this mapping refers to
		TWeakObjectPtr<UClass> Class;

		// Mapping from class event Id to SrcAccesses index in this library
		TArray<int32> Mapping;
	};

	// Per-class event ID mappings. Built dynamically at runtime. Maps class event IDs to SrcAccesses index.
	TArray<FEventMapping> EventMappings;
};

// Broadcasts a property changed event.
// Arguments are of the form of a comma-separated list of property names, e.g.
// BROADCAST_PROPERTY_CHANGED("MyStructProperty", "MySubProperty");
#define BROADCAST_PROPERTY_CHANGED(...) \
	static int32 EventId_##__LINE__ = PropertyAccess::GetEventId(GetClass(), { __VA_ARGS__ }); \
	static_cast<IPropertyEventBroadcaster*>(this)->BroadcastPropertyChanged(EventId_##__LINE__);