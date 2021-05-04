// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"
#include "UObject/WeakObjectPtr.h"
#include "Templates/Casts.h"

#include "InstancedStruct.generated.h"

/**
 * Base functionality for struct pointer handling, 
 * Do not use directly, use either FInstancedStruct, FStructView
 */
USTRUCT()
struct STRUCTUTILS_API FBaseStruct
{
	GENERATED_BODY()

public:

	FBaseStruct()
	: StructMemory(nullptr)
	{
	}

	// Copy and move should all be handled by subclasses, but it is impossible to delete them as it is a UStruct...
	FBaseStruct(const FBaseStruct&) { checkf(false, TEXT("Should all be handled by subclasses")); }
	FBaseStruct& operator=(const FBaseStruct&) { checkf(false, TEXT("Should all be handled by subclasses")); return *this; }
	FBaseStruct(const FBaseStruct&&) { checkf(false, TEXT("Should all be handled by subclasses")); }
	FBaseStruct& operator=(const FBaseStruct&&) { checkf(false, TEXT("Should all be handled by subclasses")); return *this; }

	// Returns mutable reference to the struct, this getter assumes that all data is valid
	template<typename T>
	T& GetMutable()
	{
		check(StructMemory != nullptr);
		check(ScriptStruct != nullptr);
		check(ScriptStruct->IsChildOf(T::StaticStruct()));
		return *((T*)StructMemory);
	}

	// Returns const reference to the struct, this getter assumes that all data is valid
	template<typename T>
	const T& Get() const
	{
		check(StructMemory != nullptr);
		check(ScriptStruct != nullptr);
		check(ScriptStruct->IsChildOf(T::StaticStruct()));
		return *((T*)StructMemory);
	}

	// Returns mutable pointer to the struct, or nullptr if cast is not valid.
	template<typename T>
	T* GetMutablePtr()
	{
		if (StructMemory != nullptr && ScriptStruct && ScriptStruct->IsChildOf(T::StaticStruct()))
		{
			return ((T*)StructMemory);
		}
		return nullptr;
	}

	// Returns const pointer to the struct, or nullptr if cast is not valid.
	template<typename T>
	const T* GetPtr() const
	{
		if (StructMemory != nullptr && ScriptStruct && ScriptStruct->IsChildOf(T::StaticStruct()))
		{
			return ((T*)StructMemory);
		}
		return nullptr;
	}

	// Returns struct type.
	const UScriptStruct* GetScriptStruct() const
	{
		return ScriptStruct;
	}

	// Returns mutable pointer to struct memory.
	uint8* GetMutableMemory()
	{
		return StructMemory;
	}

	// Returns const pointer to struct memory.
	const uint8* GetMemory() const
	{
		return StructMemory;
	}

	/**
	 * @return True if the struct is valid.
	 */
	bool IsValid() const
	{
		return StructMemory != nullptr && ScriptStruct != nullptr;
	}

	/**
	 * Reset to empty.
	 */
	void Reset()
	{
		// We do not DestructStruct as we do not own it
		StructMemory = nullptr;
		ScriptStruct = nullptr;
	}


protected:

	FBaseStruct(const UScriptStruct* InScriptStruct, uint8* InStructMemory = nullptr)
		: ScriptStruct(InScriptStruct)
		, StructMemory(InStructMemory)
	{
	}

	void SetMemory(uint8* InStructMemory)
	{
		StructMemory = InStructMemory;
	}

	void SetScriptStruct(const UScriptStruct* InScriptStruct)
	{
		ScriptStruct = InScriptStruct;
	}

    void DestroyScriptStruct()
	{
		check(StructMemory != nullptr);
		if (ScriptStruct != nullptr)
		{
			ScriptStruct->DestroyStruct(StructMemory);
		}
	}

private:
	const UScriptStruct* ScriptStruct = nullptr;	// Struct type
	uint8* StructMemory = nullptr;					// Struct memory
};

/**
 * FInstancedStruct works similarly as instanced UObject* property but is USTRUCTs.
 * Example:
 *
 *	UPROPERTY(EditAnywhere, Category = Foo, meta = (BaseStruct = "TestStructBase"))
 *	FInstancedStruct Test;
 *
 *	UPROPERTY(EditAnywhere, Category = Foo, meta = (BaseStruct = "TestStructBase"))
 *	TArray<FInstancedStruct> TestArray;
 *
 * -----------------------------------------------------------------------------
 * DISCLAIMER: This will be removed when TUniquePtr<FScriptObject> is available!
 * -----------------------------------------------------------------------------
 */

USTRUCT()
struct STRUCTUTILS_API FInstancedStruct : public FBaseStruct
{
	GENERATED_BODY()

public:

	FInstancedStruct()
		: FBaseStruct()
	{
	}

	explicit FInstancedStruct(const UScriptStruct* InScriptStruct)
		: FBaseStruct()
	{
		InitializeAs(InScriptStruct, nullptr);
	}

	FInstancedStruct(const FBaseStruct& InOther)
		: FBaseStruct()
	{
		InitializeAs(InOther.GetScriptStruct(), InOther.GetMemory());
	}

	FInstancedStruct(const FInstancedStruct& InOther)
		: FBaseStruct()
	{
		InitializeAs(InOther.GetScriptStruct(), InOther.GetMemory());
	}

	FInstancedStruct(FInstancedStruct&& InOther)
		: FBaseStruct(InOther.GetScriptStruct(), InOther.GetMutableMemory())
	{
		InOther.SetScriptStruct(nullptr);
		InOther.SetMemory(nullptr);
	}


	~FInstancedStruct()
	{
		Reset();
	}

	FInstancedStruct& operator=(const FBaseStruct& InOther)
	{
		if (this != &InOther)
		{
			InitializeAs(InOther.GetScriptStruct(), InOther.GetMemory());
		}
		return *this;
	}

	FInstancedStruct& operator=(const FInstancedStruct& InOther)
	{
		if (this != &InOther)
		{
			InitializeAs(InOther.GetScriptStruct(), InOther.GetMemory());
		}
		return *this;
	}

	FInstancedStruct& operator=(FInstancedStruct&& InOther)
	{
		if (this != &InOther)
		{
			Reset();

			SetScriptStruct(InOther.GetScriptStruct());
			SetMemory(InOther.GetMutableMemory());

			InOther.SetScriptStruct(nullptr);
			InOther.SetMemory(nullptr);
		}
		return *this;
	}

	// Initializes from struct type and optional data.
	void InitializeAs(const UScriptStruct* InScriptStruct, const uint8* InStructMemory = nullptr);

	// Initializes from struct type and emplace construct
	template<typename T, typename... TArgs>
	void InitializeAs(TArgs&&... InArgs)
	{
		Reset();

		const UScriptStruct* InScriptStruct = T::StaticStruct();
		SetScriptStruct(InScriptStruct);

		const int32 RequiredSize = InScriptStruct->GetStructureSize();
		SetMemory((uint8*)FMemory::Malloc(FMath::Max(1, RequiredSize)));

		new (GetMutableMemory()) T(Forward<TArgs>(InArgs)...);
	}

	// Creates a new FInstancedStruct from templated struct type.
	template<typename T>
	static FInstancedStruct Make()
	{
		FInstancedStruct InstancedStruct;
		InstancedStruct.InitializeAs(T::StaticStruct(), nullptr);
		return InstancedStruct;
	}

	// Creates a new FInstancedStruct from templated struct.
	template<typename T>
	static FInstancedStruct Make(const T& Struct)
	{
		FInstancedStruct InstancedStruct;
		InstancedStruct.InitializeAs(T::StaticStruct(), reinterpret_cast<const uint8*>(&Struct));
		return InstancedStruct;
	}

	// Creates a new FInstancedStruct from the templated type and forward all arguments to constructor
	template<typename T, typename... TArgs>
	static inline FInstancedStruct Make(TArgs&&... InArgs)
	{
		FInstancedStruct InstancedStruct;
		InstancedStruct.InitializeAs<T>(Forward<TArgs>(InArgs)...);
		return InstancedStruct;
	}

	// Reset to empty.
	void Reset();

	// For StructOpsTypeTraits
	bool Serialize(FArchive& Ar);
	bool Identical(const FInstancedStruct* Other, uint32 PortFlags) const;
	void AddStructReferencedObjects(class FReferenceCollector& Collector);
	bool ExportTextItem(FString& ValueStr, FInstancedStruct const& DefaultValue, class UObject* Parent, int32 PortFlags, class UObject* ExportRootScope) const;
	bool ImportTextItem(const TCHAR*& Buffer, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText, FArchive* InSerializingArchive = nullptr);

private:
	// Initializes for new struct type (does nothing if same type) and returns mutable struct
	UScriptStruct* ReinitializeAs(const UScriptStruct* InScriptStruct);
};

template<>
struct TStructOpsTypeTraits<FInstancedStruct> : public TStructOpsTypeTraitsBase2<FInstancedStruct>
{
	enum
	{
		WithSerializer = true,
		WithIdentical = true,
		WithExportTextItem = true,
		WithImportTextItem = true,
		WithAddStructReferencedObjects = true,
	};
};

/** 
 * FStructView is "typed" struct pointer, it contains pointer to struct plus UScriptStruct pointer.
 * FStructView does not own the memory and will not free it when out of scope.
 * It should be only used to pass struct pointer in a limited scope, or when the user controls the lifetime of the struct being stored.
 * I.e. instead of passing ref or pointer to FInstancedStruct, you should use FStructView to pass around view to the contents.
 * FStructView is passed by value.
 * FStructView is similar to FStructOnScope, but FStructView is a view only (FStructOnScope can either own the memory or be a view)
 *
 * -----------------------------------------------------------------------------
 * DISCLAIMER: This will be removed when FScriptObject is available!
 * -----------------------------------------------------------------------------
 */
struct STRUCTUTILS_API FStructView : public FBaseStruct
{

public:

	FStructView()
		: FBaseStruct()
	{
	}

	FStructView(const UScriptStruct* InScriptStruct, const uint8* InStructMemory)
		: FBaseStruct(InScriptStruct, const_cast<uint8*>(InStructMemory))
	{}

	FStructView(const FBaseStruct& InOther)
		: FStructView(InOther.GetScriptStruct(), InOther.GetMemory())
	{
	}

	FStructView(const FStructView& InOther)
		: FStructView(InOther.GetScriptStruct(), InOther.GetMemory())
	{
	}

	FStructView(FStructView&& InOther)
		: FStructView(InOther.GetScriptStruct(), InOther.GetMemory())
	{
	}

	~FStructView()
	{
		Reset();
	}

	FStructView& operator=(const FBaseStruct& InOther)
	{
		SetScriptStruct(InOther.GetScriptStruct());
		SetMemory(const_cast<uint8*>(InOther.GetMemory()));

		return *this;
	}

	FStructView& operator=(const FStructView& InOther)
	{
		SetScriptStruct(InOther.GetScriptStruct());
		SetMemory(const_cast<uint8*>(InOther.GetMemory()));

		return *this;
	}

	FStructView& operator=(FStructView&& InOther)
	{
		SetScriptStruct(InOther.GetScriptStruct());
		SetMemory(const_cast<uint8*>(InOther.GetMemory()));

		return *this;
	}

	// Creates a new FStructView from the templated struct
	template<typename T>
	static FStructView Make(const T& Struct)
	{
		return FStructView(T::StaticStruct(), reinterpret_cast<const uint8*>(&Struct));
	}
};