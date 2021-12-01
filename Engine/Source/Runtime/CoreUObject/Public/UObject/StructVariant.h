// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"
#include "UObject/WeakObjectPtr.h"

/**
 * A variant type holding an instance of a USTRUCT which acts as a value type (copyable, movable) with comparison and serialization support.
 * This can be used as a UPROPERTY to provide a struct type picker and inline editing of the struct instance.
 * 
 * UPROPERTY(..., meta=(MetaStruct=MyStructType))
 * FStructVariant MyStruct;
 */
struct COREUOBJECT_API FStructVariant
{
public:
	/**
	 * Constructor/Destructor.
	 */
	FStructVariant();
	~FStructVariant();

	/**
	 * Copyable (deep copies the struct instance).
	 */
	FStructVariant(const FStructVariant& InOther);
	FStructVariant& operator=(const FStructVariant& InOther);

	/**
	 * Movable (steals the struct instance)
	 */
	FStructVariant(FStructVariant&& InOther);
	FStructVariant& operator=(FStructVariant&& InOther);

	/**
	 * Comparison.
	 */
	bool operator==(const FStructVariant& InOther) const;
	bool operator!=(const FStructVariant& InOther) const;
	bool Identical(const FStructVariant* InOther, uint32 PortFlags) const;

	/**
	 * Import/ExportText.
	 */
	bool ExportTextItem(FString& ValueStr, const FStructVariant& DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope) const;
	bool ImportTextItem(const TCHAR*& Buffer, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText);

	/**
	 * Reference collection.
	 */
	void AddStructReferencedObjects(class FReferenceCollector& Collector);

	/**
	 * Get the struct type of this variant.
	 */
	const UScriptStruct* GetStructType() const;

	/**
	 * Set the struct type of this variant and prepare the struct instance for use.
	 * @note Does nothing if the struct type already matches the requested type.
	 */
	void SetStructType(const UScriptStruct* InStructType);

	/**
	 * Set the struct type of this variant and prepare the struct instance for use.
	 * @note Does nothing if the struct type already matches the requested type.
	 */
	template <typename StructType>
	void SetStructType()
	{
		SetStructType(TBaseStructure<StructType>::Get());
	}

	/**
	 * Get the raw struct instance for this variant, optionally verifying that it is the expected type.
	 * @return The raw struct instance if this variant has been initialized, and if it matches the expected type. Null otherwise.
	 */
	void* GetStructInstance(const UScriptStruct* InExpectedType = nullptr);

	/**
	 * Get the raw struct instance for this variant, optionally verifying that it is the expected type.
	 * @return The raw struct instance if this variant has been initialized, and if it matches the expected type. Null otherwise.
	 */
	const void* GetStructInstance(const UScriptStruct* InExpectedType = nullptr) const;

	/**
	 * Get the typed struct instance for this variant, verifying that it is the expected type.
	 * @return The typed struct instance if this variant has been initialized, and if it matches the expected type. Null otherwise.
	 */
	template <typename StructType>
	StructType* GetStructInstance()
	{
		return static_cast<StructType*>(GetStructInstance(TBaseStructure<StructType>::Get()));
	}

	/**
	 * Get the typed struct instance for this variant, verifying that it is the expected type.
	 * @return The typed struct instance if this variant has been initialized, and if it matches the expected type. Null otherwise.
	 */
	template <typename StructType>
	const StructType* GetStructInstance() const
	{
		return static_cast<const StructType*>(GetStructInstance(TBaseStructure<StructType>::Get()));
	}

	/**
	 * Return all objects that will be Preload()ed when this is serialized at load time.
	 */
	void GetPreloadDependencies(TArray<UObject*>& OutDeps);

	/**
	 * Serialize the type and instance for this struct.
	 * @note Uses the standard struct serializer for the instance.
	 */
	bool Serialize(FStructuredArchive::FSlot Slot);

private:
	/**
	 * Allocate the struct instance of this variant, if the struct type is set.
	 * @note StructInstance must be empty when this is called, which can be achieved by calling FreeStructInstance first.
	 */
	void AllocateStructInstance();

	/**
	 * Free the struct instance of this variant, if the struct type is set.
	 * @note StructInstance may be empty when this is called.
	 */
	void FreeStructInstance();

	/**
	 * Initialize the struct instance of this variant from the source variant, deep copying the source struct instance.
	 * @note StructInstance may be in any state when this is called.
	 */
	void InitializeInstanceFrom(const FStructVariant& InOther);

	/**
	 * Initialize the struct instance of this variant from the source variant, stealing the source struct instance.
	 * @note StructInstance may be in any state when this is called.
	 */
	void InitializeInstanceFrom(FStructVariant&& InOther);

private:
	/** The type of this struct variant */
	TWeakObjectPtr<const UScriptStruct> StructType;

	/** The instance of this struct variant */
	void* StructInstance = nullptr;

	/** Needed to avoid member access issues in UHT generated code */
	friend struct Z_Construct_UScriptStruct_FStructVariant_Statics;
};

#if !CPP
/**
 * A variant type holding an instance of a USTRUCT which acts as a value type (copyable, movable) with comparison and serialization support.
 * This can be used as a UPROPERTY to provide a struct type picker and inline editing of the struct instance.
 */
USTRUCT(noexport, BlueprintType)
struct FStructVariant
{
	TWeakObjectPtr<const UScriptStruct> StructType;
	void* StructInstance = nullptr;
};
#endif

template<>
struct TStructOpsTypeTraits<FStructVariant> : public TStructOpsTypeTraitsBase2<FStructVariant>
{
	enum
	{
		WithIdentical = true,
		WithExportTextItem = true,
		WithImportTextItem = true,
		WithAddStructReferencedObjects = true,
		WithStructuredSerializer = true,
		WithGetPreloadDependencies = true,
	};
};

template<> struct TBaseStructure<FStructVariant>
{
	COREUOBJECT_API static UScriptStruct* Get();
};
