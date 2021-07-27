// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Class.h"
#include "UObject/WeakObjectPtr.h"
#include "Templates/Casts.h"

#include "InstancedStruct.generated.h"



///////////////////////////////////////////////////////////////// FConstBaseStruct /////////////////////////////////////////////////////////////////

/**
 * Immutable base functionality for struct pointer handling,
 * Do not use directly, use either FInstancedStruct, FStructView or FConstStructView.
 */
USTRUCT()
struct STRUCTUTILS_API FConstBaseStruct
{
	GENERATED_BODY()

public:

	FConstBaseStruct() = default;

	/** Copy and move should all be handled by subclasses, but it is impossible to delete them as it is a UStruct... */
	FConstBaseStruct(const FConstBaseStruct&) { checkf(false, TEXT("Should all be handled by subclasses")); }
	FConstBaseStruct& operator=(const FConstBaseStruct&) { checkf(false, TEXT("Should all be handled by subclasses")); return *this; }
	FConstBaseStruct(const FConstBaseStruct&&) { checkf(false, TEXT("Should all be handled by subclasses")); }
	FConstBaseStruct& operator=(const FConstBaseStruct&&) { checkf(false, TEXT("Should all be handled by subclasses")); return *this; }

	bool operator==(const FConstBaseStruct& Other) const
	{
		if ((GetScriptStruct() != Other.GetScriptStruct()) || (GetMemory() != Other.GetMemory()))
		{
			return false;
		}
		return true;
	}

	bool operator!=(const FConstBaseStruct& Other) const
	{
		return !operator==(Other);
	}

	/** Returns const pointer to struct memory. */
	const uint8* GetMemory() const
	{
		return StructMemory;
	}

	/** Returns const reference to the struct, this getter assumes that all data is valid. */
	template<typename T>
	const T& Get() const
	{
		check(StructMemory != nullptr);
		check(ScriptStruct != nullptr);
		check(ScriptStruct->IsChildOf(T::StaticStruct()));
		return *((T*)StructMemory);
	}

	/** Returns const pointer to the struct, or nullptr if cast is not valid. */
	template<typename T>
	const T* GetPtr() const
	{
		if (StructMemory != nullptr && ScriptStruct && ScriptStruct->IsChildOf(T::StaticStruct()))
		{
			return ((T*)StructMemory);
		}
		return nullptr;
	}

	/** Returns struct type. */
	const UScriptStruct* GetScriptStruct() const
	{
		return ScriptStruct;
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

	FConstBaseStruct(const UScriptStruct* InScriptStruct, const uint8* InStructMemory = nullptr)
		: ScriptStruct(InScriptStruct)
		, StructMemory(InStructMemory)
	{
	}

	void SetMemory(const uint8* InStructMemory)
	{
		StructMemory = InStructMemory;
	}

	void SetScriptStruct(const UScriptStruct* InScriptStruct)
	{
		ScriptStruct = InScriptStruct;
	}

	void SetStructData(const UScriptStruct* InScriptStruct, const uint8* InStructMemory)
	{
		ScriptStruct = InScriptStruct;
		StructMemory = InStructMemory;
	}

protected:
	const UScriptStruct* ScriptStruct = nullptr;
	const uint8* StructMemory = nullptr;
};

///////////////////////////////////////////////////////////////// FBaseStruct /////////////////////////////////////////////////////////////////

/**
 * Base functionality for mutable struct pointer handling. 
 * The idea here is that it's only possible for FBaseStruct to be setup from mutable non const memory. This makes the const_cast here safe in GetMutableMemory().
 * Do not use directly, use either FInstancedStruct, or FStructView.
 * Note const FBaseStruct only makes the members of this class immutable NOT the struct data pointed at immutable.
 */
USTRUCT()
struct FBaseStruct : public FConstBaseStruct
{
	GENERATED_BODY()

public:

	FBaseStruct() = default;

	/** Copy and move should all be handled by subclasses, but it is impossible to delete them as it is a UStruct... */
	FBaseStruct(const FBaseStruct&) { checkf(false, TEXT("Should all be handled by subclasses")); }
	FBaseStruct& operator=(const FBaseStruct&) { checkf(false, TEXT("Should all be handled by subclasses")); return *this; }
	FBaseStruct(const FBaseStruct&&) { checkf(false, TEXT("Should all be handled by subclasses")); }
	FBaseStruct& operator=(const FBaseStruct&&) { checkf(false, TEXT("Should all be handled by subclasses")); return *this; }

	/** Returns a mutable pointer to struct memory. This const_cast here is safe as a FBaseStruct can only be setup from mutable non const memory. */
	uint8* GetMutableMemory() const
	{
		return const_cast<uint8*>(StructMemory);
	}

	/** Returns mutable reference to the struct, this getter assumes that all data is valid. */
	template<typename T>
	T& GetMutable() const
	{
		check(StructMemory != nullptr);
		check(ScriptStruct != nullptr);
		check(ScriptStruct->IsChildOf(T::StaticStruct()));
		return *((T*)GetMutableMemory());
	}

	/** Returns mutable pointer to the struct, or nullptr if cast is not valid. */
	template<typename T>
	T* GetMutablePtr() const
	{
		if (StructMemory != nullptr && ScriptStruct && ScriptStruct->IsChildOf(T::StaticStruct()))
		{
			return ((T*)GetMutableMemory());
		}
		return nullptr;
	}

protected:
	FBaseStruct(const UScriptStruct* InScriptStruct, uint8* InStructMemory = nullptr)
		: FConstBaseStruct(InScriptStruct, InStructMemory)
	{}

	void DestroyScriptStruct() const
	{
		check(StructMemory != nullptr);
		if (ScriptStruct != nullptr)
		{
			ScriptStruct->DestroyStruct(GetMutableMemory());
		}
	}
};

///////////////////////////////////////////////////////////////// FInstancedStruct /////////////////////////////////////////////////////////////////

/**
 * FInstancedStruct works similarly as instanced UObject* property but is USTRUCTs.
 * Example:
 *
 *	UPROPERTY(EditAnywhere, Category = Foo, meta = (BaseStruct = "TestStructBase"))
 *	FInstancedStruct Test;
 *
 *	UPROPERTY(EditAnywhere, Category = Foo, meta = (BaseStruct = "TestStructBase"))
 *	TArray<FInstancedStruct> TestArray;
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

	FInstancedStruct(const FConstBaseStruct& InOther)
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

	FInstancedStruct& operator=(const FConstBaseStruct& InOther)
	{
		if (*this != InOther)
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

	/** Initializes from struct type and optional data. */
	void InitializeAs(const UScriptStruct* InScriptStruct, const uint8* InStructMemory = nullptr);

	/** Initializes from struct type and emplace construct. */
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

	/** Creates a new FInstancedStruct from templated struct type. */
	template<typename T>
	static FInstancedStruct Make()
	{
		FInstancedStruct InstancedStruct;
		InstancedStruct.InitializeAs(T::StaticStruct(), nullptr);
		return InstancedStruct;
	}

	/** Creates a new FInstancedStruct from templated struct. */
	template<typename T>
	static FInstancedStruct Make(const T& Struct)
	{
		FInstancedStruct InstancedStruct;
		InstancedStruct.InitializeAs(T::StaticStruct(), reinterpret_cast<const uint8*>(&Struct));
		return InstancedStruct;
	}

	/** Creates a new FInstancedStruct from the templated type and forward all arguments to constructor. */
	template<typename T, typename... TArgs>
	static inline FInstancedStruct Make(TArgs&&... InArgs)
	{
		FInstancedStruct InstancedStruct;
		InstancedStruct.InitializeAs<T>(Forward<TArgs>(InArgs)...);
		return InstancedStruct;
	}

	/** Reset to empty. */
	void Reset();

	/** For StructOpsTypeTraits */
	bool Serialize(FArchive& Ar);
	bool Identical(const FInstancedStruct* Other, uint32 PortFlags) const;
	void AddStructReferencedObjects(class FReferenceCollector& Collector);
	bool ExportTextItem(FString& ValueStr, FInstancedStruct const& DefaultValue, class UObject* Parent, int32 PortFlags, class UObject* ExportRootScope) const;
	bool ImportTextItem(const TCHAR*& Buffer, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText, FArchive* InSerializingArchive = nullptr);

private:
	/** Initializes for new struct type (does nothing if same type) and returns mutable struct. */
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

///////////////////////////////////////////////////////////////// FConstStructView /////////////////////////////////////////////////////////////////

/**
 * FConstStructView is "typed" struct pointer, it contains const pointer to struct plus UScriptStruct pointer.
 * FConstStructView does not own the memory and will not free it when out of scope.
 * It should be only used to pass struct pointer in a limited scope, or when the user controls the lifetime of the struct being stored.
 * E.g. instead of passing ref or pointer to a FInstancedStruct, you should use FConstStructView or FStructView to pass around a view to the contents.
 * FConstStructView is passed by value.
 * FConstStructView is similar to FStructOnScope, but FConstStructView is a view only (FStructOnScope can either own the memory or be a view)
 */
struct STRUCTUTILS_API FConstStructView : public FConstBaseStruct
{

public:

	FConstStructView() = default;

	FConstStructView(const UScriptStruct* InScriptStruct, const uint8* InStructMemory)
		: FConstBaseStruct(InScriptStruct, InStructMemory)
	{}

	FConstStructView(const FConstBaseStruct& ConstBaseStruct)
		: FConstBaseStruct(ConstBaseStruct.GetScriptStruct(), ConstBaseStruct.GetMemory())
	{}

	FConstStructView(const FConstStructView& Other)
		: FConstBaseStruct(Other.GetScriptStruct(), Other.GetMemory())
	{}

	FConstStructView& operator=(const FConstBaseStruct& ConstBaseStruct)
	{
		SetStructData(ConstBaseStruct.GetScriptStruct(), ConstBaseStruct.GetMemory());
		return *this;
	}

	FConstStructView& operator=(const FConstStructView& Other)
	{
		SetStructData(Other.GetScriptStruct(), Other.GetMemory());
		return *this;
	}

	/** Creates a new FStructView from the templated struct */
	template<typename T>
	static FConstStructView Make(const T& Struct)
	{
		return FConstStructView(T::StaticStruct(), reinterpret_cast<const uint8*>(&Struct));
	}
};

///////////////////////////////////////////////////////////////// FStructView /////////////////////////////////////////////////////////////////

/**
 * FStructView is "typed" struct pointer, it contains pointer to struct plus UScriptStruct pointer.
 * FStructView does not own the memory and will not free it when out of scope.
 * It should be only used to pass struct pointer in a limited scope, or when the user controls the lifetime of the struct being stored.
 * E.g. instead of passing ref or pointer to a FInstancedStruct, you should use FConstStructView or FStructView to pass around a view to the contents.
 * FStructView is passed by value.
 * FStructView is similar to FStructOnScope, but FStructView is a view only (FStructOnScope can either own the memory or be a view)
 */
struct STRUCTUTILS_API FStructView : public FBaseStruct
{

public:

	FStructView()
		: FBaseStruct()
	{
	}

	FStructView(const UScriptStruct* InScriptStruct, uint8* InStructMemory)
		: FBaseStruct(InScriptStruct, InStructMemory)
	{}

	FStructView(const FBaseStruct& BaseStruct)
		: FBaseStruct(BaseStruct.GetScriptStruct(), BaseStruct.GetMutableMemory())
	{}

	FStructView(const FStructView& Other)
		: FBaseStruct(Other.GetScriptStruct(), Other.GetMutableMemory())
	{}

	FStructView& operator=(const FBaseStruct& BaseStruct)
	{
		SetStructData(BaseStruct.GetScriptStruct(), BaseStruct.GetMutableMemory());
		return *this;
	}

	FStructView& operator=(const FStructView& Other)
	{
		SetStructData(Other.GetScriptStruct(), Other.GetMutableMemory());
		return *this;
	}

	/** Creates a new FStructView from the templated struct. Note its not safe to make InStruct const ref as the original object may have been declared const */
	template<typename T>
	static FStructView Make(T& InStruct)
	{
		return FStructView(T::StaticStruct(), reinterpret_cast<uint8*>(&InStruct));
	}
};