// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "StructView.h"
#include "StateTreePropertyBindings.generated.h"

class FProperty;


/**
 * Short lived pointer to an UOBJECT() or USTRUCT().
 * The data view expects a type (UStruct) when you pass in a valid memory. In case of null, the type can be empty too.
 */
struct STATETREEMODULE_API FStateTreeDataView
{
	FStateTreeDataView() = default;

	// USTRUCT() constructor.
	FStateTreeDataView(const UScriptStruct* InScriptStruct, uint8* InMemory) : Struct(InScriptStruct), Memory(InMemory)
	{
		// Must have type with valid pointer.
		check(!Memory || (Memory && Struct));
	}

	// UOBJECT() constructor.
	FStateTreeDataView(UObject* Object) : Struct(Object ? Object->GetClass() : nullptr), Memory(reinterpret_cast<uint8*>(Object))
	{
		// Must have type with valid pointer.
		check(!Memory || (Memory && Struct));
	}

	// USTRUCT() from a StructView.
	FStateTreeDataView(FStructView StructView) : Struct(StructView.GetScriptStruct()), Memory(StructView.GetMutableMemory())
	{
		// Must have type with valid pointer.
		check(!Memory || (Memory && Struct));
	}

	/**
	 * Check is the view is valid (both pointer and type are set). On valid views it is safe to call the Get<>() methods returning a reference.
	 * @return True if the view is valid.
	*/
	bool IsValid() const
	{
		return Memory != nullptr && Struct != nullptr;
	}

	/*
	 * UOBJECT() getters (reference & pointer, const & mutable)
	 */
	template <typename T>
    typename TEnableIf<TIsDerivedFrom<T, UObject>::IsDerived, const T&>::Type Get() const
	{
		check(Memory != nullptr);
		check(Struct != nullptr);
		check(Struct->IsChildOf(T::StaticClass()));
		return *((T*)Memory);
	}

	template <typename T>
	typename TEnableIf<TIsDerivedFrom<T, UObject>::IsDerived, T&>::Type GetMutable() const
	{
		check(Memory != nullptr);
		check(Struct != nullptr);
		check(Struct->IsChildOf(T::StaticClass()));
		return *((T*)Memory);
	}

	template <typename T>
	typename TEnableIf<TIsDerivedFrom<T, UObject>::IsDerived, const T*>::Type GetPtr() const
	{
		// If Memory is set, expect Struct too. Otherwise, let nulls pass through.
		check(!Memory || (Memory && Struct));
		check(!Struct || Struct->IsChildOf(T::StaticClass()));
		return ((T*)Memory);
	}

	template <typename T>
    typename TEnableIf<TIsDerivedFrom<T, UObject>::IsDerived, T*>::Type GetMutablePtr() const
	{
		// If Memory is set, expect Struct too. Otherwise, let nulls pass through.
		check(!Memory || (Memory && Struct));
		check(!Struct || Struct->IsChildOf(T::StaticClass()));
		return ((T*)Memory);
	}

	/*
	 * USTRUCT() getters (reference & pointer, const & mutable)
	 */
	template <typename T>
	typename TEnableIf<!TIsDerivedFrom<T, UObject>::IsDerived, const T&>::Type Get() const
	{
		check(Memory != nullptr);
		check(Struct != nullptr);
		check(Struct->IsChildOf(T::StaticStruct()));
		return *((T*)Memory);
	}

	template <typename T>
    typename TEnableIf<!TIsDerivedFrom<T, UObject>::IsDerived, T&>::Type GetMutable() const
	{
		check(Memory != nullptr);
		check(Struct != nullptr);
		check(Struct->IsChildOf(T::StaticStruct()));
		return *((T*)Memory);
	}

	template <typename T>
    typename TEnableIf<!TIsDerivedFrom<T, UObject>::IsDerived, const T*>::Type GetPtr() const
	{
		// If Memory is set, expect Struct too. Otherwise, let nulls pass through.
		check(!Memory || (Memory && Struct));
		check(!Struct || Struct->IsChildOf(T::StaticStruct()));
		return ((T*)Memory);
	}

	template <typename T>
    typename TEnableIf<!TIsDerivedFrom<T, UObject>::IsDerived, T*>::Type GetMutablePtr() const
	{
		// If Memory is set, expect Struct too. Otherwise, let nulls pass through.
		check(!Memory || (Memory && Struct));
		check(!Struct || Struct->IsChildOf(T::StaticStruct()));
		return ((T*)Memory);
	}

	/** @return Struct describing the data type. */
	const UStruct* GetStruct() const { return Struct; }

	/** @return Raw const pointer to the data. */
	const uint8* GetMemory() const { return Memory; }

	/** @return Raw mutable pointer to the data. */
	uint8* GetMutableMemory() const { return Memory; }
	
protected:
	/** UClass or UScriptStruct of the data. */
	const UStruct* Struct = nullptr;

	/** Memory pointing at the class or struct */
	uint8* Memory = nullptr;
};


/**
 * Descriptor for a struct or class that can be a binding source or target.
 * Each struct has unique identifier, which is used to distinguish them, and name that is mostly for debugging and UI.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeBindableStructDesc
{
	GENERATED_BODY()

#if WITH_EDITORONLY_DATA
	bool operator==(const FStateTreeBindableStructDesc& RHS) const
	{
		return ID == RHS.ID && Struct == RHS.Struct; // Not checking name, it's cosmetic.
	}
#endif

	/** The type of the struct or class. */
	UPROPERTY()
	const UStruct* Struct = nullptr;

	/** Name of the container (cosmetic). */
	UPROPERTY()
	FName Name;

#if WITH_EDITORONLY_DATA
	/** Unique identifier of the struct. */
	UPROPERTY()
	FGuid ID;
#endif
};


/**
 * The type of access a property path describes.
 */
UENUM()
enum class EStateTreePropertyAccessType : uint8
{
	Offset,			// Access node is a simple basePtr + offset
	Object,			// Access node needs to dereference an object at its current address
	WeakObject,		// Access is a weak object
	SoftObject,		// Access is a soft object
	IndexArray,		// Access node indexes a dynamic array
};

/**
 * Describes how the copy should be performed.
 */
UENUM()
enum class EStateTreePropertyCopyType : uint8
{
	None,						// No copying
	
	CopyPlain,					// For plain old data types, we do a simple memcpy.
	CopyComplex,				// For more complex data types, we need to call the properties copy function
	CopyBool,					// Read and write properties using bool property helpers, as source/dest could be bitfield or boolean
	CopyStruct,					// Use struct copy operation, as this needs to correctly handle CPP struct ops
	CopyObject,					// Read and write properties using object property helpers, as source/dest could be regular/weak/soft etc.
	CopyName,					// FName needs special case because its size changes between editor/compiler and runtime.
	CopyFixedArray,				// Array needs special handling for fixed size TArrays

	/* Promote the type during the copy */

	/* Bool promotions */
	PromoteBoolToByte,
	PromoteBoolToInt32,
	PromoteBoolToUInt32,
	PromoteBoolToInt64,
	PromoteBoolToFloat,
	PromoteBoolToDouble,

	/* Byte promotions */
	PromoteByteToInt32,
	PromoteByteToUInt32,
	PromoteByteToInt64,
	PromoteByteToFloat,
	PromoteByteToDouble,

	/* Int32 promotions */
	PromoteInt32ToInt64,
	PromoteInt32ToFloat,	// This is strictly sketchy because of potential data loss, but it is usually OK in the general case
	PromoteInt32ToDouble,

	/* UInt32 promotions */
	PromoteUInt32ToInt64,
    PromoteUInt32ToFloat,	// This is strictly sketchy because of potential data loss, but it is usually OK in the general case

	/* Float promotions */
	// LWC_TODO: Float/double should become synonyms in state trees? Certainly BPs.
	PromoteFloatToDouble,
	DemoteDoubleToFloat,	// LWC_TODO: This should not ship!
};


/**
 * Describes a segment of a property path (see FStateTreePropertyPath). Used for storage only.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreePropertySegment
{
	GENERATED_BODY()

	/** Property name. */
	UPROPERTY()
	FName Name;

	/** Index in the array the property points at. */
	UPROPERTY()
	int32 ArrayIndex = 0;

	/** Type of access/indirection. */
	UPROPERTY()
	EStateTreePropertyAccessType Type = EStateTreePropertyAccessType::Offset;
};

/**
 * Describes a property path made of segments (i.e. Foo.Bar consists of two segments). Used for storage only.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreePropertyPath
{
	GENERATED_BODY()

	/** Index to first segment of the property path. */
	UPROPERTY()
	int32 SegmentsBegin = 0;

	/** Index to one past the last segment in the property path. */
	UPROPERTY()
	int32 SegmentsEnd = 0;
};

/**
 * Describes property binding, the property from source path is copied into the property at the target path.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreePropertyBinding
{
	GENERATED_BODY()

	/** Index to source property path. */
	UPROPERTY()
	int32 SourcePathIndex = 0;

	/** Index to target property path. */
	UPROPERTY()
	int32 TargetPathIndex = 0;

	/** Index to the source struct the source path refers to, sources are stored in FStateTreePropertyBindings. */
	UPROPERTY()
	int32 SourceStructIndex = 0;

	/** The type of copy to use between the properties. */
	UPROPERTY()
	EStateTreePropertyCopyType CopyType = EStateTreePropertyCopyType::None;
};

/**
 * Property indirection is a resolved property path segment, used for accessing properties in structs.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreePropertyIndirection
{
	GENERATED_BODY()

	/** Index in the array the property points at. */
	UPROPERTY()
	int32 ArrayIndex = 0;

	/** Cached offset of the property */
	UPROPERTY()
	int32 Offset = 0;

	/** Type of access/indirection. */
	UPROPERTY()
	EStateTreePropertyAccessType Type = EStateTreePropertyAccessType::Offset;

	/** Cached array property. */
	const FArrayProperty* ArrayProperty = nullptr;
};

/**
 * Property access is a resolved property path, used for accessing properties in structs via one or more indirections.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreePropertyAccess
{
	GENERATED_BODY()

	/** Index to first indirection of the property access. */
	UPROPERTY()
	int32 IndirectionsBegin = 0;

	/** Index to one past the last indirection in the property access. */
	UPROPERTY()
	int32 IndirectionsEnd = 0;

	/** Cached pointer to the leaf property of the access. */
	const FProperty* LeafProperty = nullptr;
};


/**
 * Describes property copy, the property from source is copied into the property at the target.
 * Copy target struct is described in the property copy batch.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreePropCopy
{
	GENERATED_BODY()

	/** Index to source property access. */
	UPROPERTY()
	int32 SourceAccessIndex = 0;

	/** Index to target property access. */
	UPROPERTY()
	int32 TargetAccessIndex = 0;

	/** Index to the struct the source path refers to, sources are stored in FStateTreePropertyBindings. */
	UPROPERTY()
	int32 SourceStructIndex = 0;

	/** Type of the copy */
	UPROPERTY()
	EStateTreePropertyCopyType Type = EStateTreePropertyCopyType::None;

	/** Cached property element size * dim. */
	UPROPERTY()
	int32 CopySize = 0;
};


/**
 * Describes a batch of property copies from many sources to one target struct.
 * Note: The batch is used to reference both bindings and copies (a binding turns into copy when resolved).
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreePropCopyBatch
{
	GENERATED_BODY()

	/** Expected target struct */
	UPROPERTY()
	FStateTreeBindableStructDesc TargetStruct;

	/** Index to first binding/copy. */
	UPROPERTY()
	int32 BindingsBegin = 0;

	/** Index to one past the last binding/copy. */
	UPROPERTY()
	int32 BindingsEnd = 0;
};

/**
 * Runtime storage and execution of property bindings.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreePropertyBindings
{
	GENERATED_BODY()

	/**
	 * Clears all bindings.
	 */
	void Reset();

	/**
	 * Resolves paths to indirections.
	 * @return True if resolve succeeded.
	 */
	bool ResolvePaths();

	/**
	 * @return True if bindings have been resolved.
	 */
	bool IsValid() const { return bBindingsResolved; }

	/**
	 * @return Number of source structs the copy expects.
	 */
	int32 GetSourceStructNum() const { return SourceStructs.Num(); }

	/**
	 * Copies a batch of properties from source structs to target struct.
	 * @param SourceStructViews Views to structs where properties are copied from.
	 * @param TargetBatchIndex Batch index to copy (see FStateTreePropertyBindingCompiler).
	 * @param TargetStructView View to struct where properties are copied to.
	 */
	void CopyTo(TConstArrayView<FStateTreeDataView> SourceStructViews, const int32 TargetBatchIndex, FStateTreeDataView TargetStructView) const;

	void DebugPrintInternalLayout(FString& OutString) const;

protected:
	bool ResolvePath(const UStruct* Struct, const FStateTreePropertyPath& Path, FStateTreePropertyAccess& OutAccess);
	bool ValidateCopy(FStateTreePropCopy& Copy) const;
	void PerformCopy(const FStateTreePropCopy& Copy, const FProperty* SourceProperty, const uint8* SourceAddress, const FProperty* TargetProperty, uint8* TargetAddress) const;
	uint8* GetAddress(FStateTreeDataView InStructView, const FStateTreePropertyAccess& InAccess) const;
	FString GetPathAsString(const FStateTreePropertyPath& Path, const int32 HighlightedSegment = INDEX_NONE, const TCHAR* HighlightPrefix = nullptr, const TCHAR* HighlightPostfix = nullptr);

	/** Array of expected source structs. */
	UPROPERTY()
	TArray<FStateTreeBindableStructDesc> SourceStructs;

	/** Array of copy batches. */
	UPROPERTY()
	TArray<FStateTreePropCopyBatch> CopyBatches;

	/** Array of property bindings, resolved into arrays of copies before use. */
	UPROPERTY()
	TArray<FStateTreePropertyBinding> PropertyBindings;

	/** Array of property bindings, indexed by property paths. */
	UPROPERTY()
	TArray<FStateTreePropertyPath> PropertyPaths;

	/** Array of property segments, indexed by property paths. */
	UPROPERTY()
	TArray<FStateTreePropertySegment> PropertySegments;

	/** Array of property copies */
	UPROPERTY(Transient)
	TArray<FStateTreePropCopy> PropertyCopies;

	/** Array of property accesses, indexed by copies*/
	UPROPERTY(Transient)
	TArray<FStateTreePropertyAccess> PropertyAccesses;

	/** Array of property indirections, indexed by accesses*/
	UPROPERTY(Transient)
	TArray<FStateTreePropertyIndirection> PropertyIndirections;

	/** Flag indicating if the properties has been resolved successfully . */
	bool bBindingsResolved = false;

	friend struct FStateTreePropertyBindingCompiler;
};



/**
 * Editor representation of a property path in StateTree.
 * Note: This is defined here for IStateTreeBindingLookup.
 */
USTRUCT()
struct STATETREEMODULE_API FStateTreeEditorPropertyPath
{
	GENERATED_BODY()

	FStateTreeEditorPropertyPath() = default;
	FStateTreeEditorPropertyPath(const FGuid& InStructID, const TCHAR* PropertyName) : StructID(InStructID) { Path.Add(PropertyName); }
	FStateTreeEditorPropertyPath(const FStateTreeEditorPropertyPath& InPath) : StructID(InPath.StructID), Path(InPath.Path) {}

	/**
	 * Returns the property path as a one string. Highlight allows to decorate a specific segment.
	 * @param HighlightedSegment Index of the highlighted path segment
	 * @param HighlightPrefix String to append before highlighted segment
	 * @param HighlightPostfix String to append after highlighted segment
	 */
	FString ToString(const int32 HighlightedSegment = INDEX_NONE, const TCHAR* HighlightPrefix = nullptr, const TCHAR* HighlightPostfix = nullptr) const;

	/** Handle of the struct this property path is relative to. */
	UPROPERTY()
	FGuid StructID;

	/** Property path segments */
	UPROPERTY()
	TArray<FString> Path;

	bool IsValid() const { return StructID.IsValid() && Path.Num() > 0; }

	bool operator==(const FStateTreeEditorPropertyPath& RHS) const;
};

/**
 * Helper interface to reason about bound properties. The implementation is in the editor plugin.
 */
struct STATETREEMODULE_API IStateTreeBindingLookup
{
	/** @return Source path for given target path, or null if binding does not exists. */
	virtual const FStateTreeEditorPropertyPath* GetPropertyBindingSource(const FStateTreeEditorPropertyPath& InTargetPath) const PURE_VIRTUAL(IStateTreeBindingLookup::GetPropertyBindingSource, return nullptr; );

	/** @return Display name given property path. */
	virtual FText GetPropertyPathDisplayName(const FStateTreeEditorPropertyPath& InPath) const PURE_VIRTUAL(IStateTreeBindingLookup::GetPropertyPathDisplayName, return FText::GetEmpty(); );

	/** @return Leaf property based on property path. */
	virtual const FProperty* GetPropertyPathLeafProperty(const FStateTreeEditorPropertyPath& InPath) const PURE_VIRTUAL(IStateTreeBindingLookup::GetPropertyPathLeafProperty, return nullptr; );
};
