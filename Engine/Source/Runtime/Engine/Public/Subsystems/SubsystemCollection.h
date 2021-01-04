// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/Casts.h"
#include "Templates/SubclassOf.h"
#include "UObject/GCObject.h"

class USubsystem;
class UDynamicSubsystem;

class ENGINE_API FSubsystemCollectionBase : public FGCObject
{
public:
	/** Initialize the collection of systems, systems will be created and initialized */
	void Initialize(UObject* NewOuter);

	/* Clears the collection, while deinitializing the systems */
	void Deinitialize();

	/** 
	 * Only call from Initialize() of Systems to ensure initialization order
	 * Note: Dependencies only work within a collection
	 */
	USubsystem* InitializeDependency(TSubclassOf<USubsystem> SubsystemClass);

	/**
	 * Only call from Initialize() of Systems to ensure initialization order
	 * Note: Dependencies only work within a collection
	 */
	template <typename TSubsystemClass>
	TSubsystemClass* InitializeDependency()
	{
		return Cast<TSubsystemClass>(InitializeDependency(TSubsystemClass::StaticClass()));
	}

	/* FGCObject Interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override;

protected:
	/** protected constructor - for use by the template only(FSubsystemCollection<TBaseType>) */
	FSubsystemCollectionBase(UClass* InBaseType);

	/** protected constructor - Use the FSubsystemCollection<TBaseType> class */
	FSubsystemCollectionBase();

	/** Get a Subsystem by type */
	USubsystem* GetSubsystemInternal(UClass* SubsystemClass) const;

	/** Get a list of Subsystems by type */
	const TArray<USubsystem*>& GetSubsystemArrayInternal(UClass* SubsystemClass) const;

	/** Get the collection BaseType */
	const UClass* GetBaseType() const { return BaseType; }

private:
	USubsystem* AddAndInitializeSubsystem(UClass* SubsystemClass);

	void RemoveAndDeinitializeSubsystem(USubsystem* Subsystem);

	TMap<UClass*, USubsystem*> SubsystemMap;

	mutable TMap<UClass*, TArray<USubsystem*>> SubsystemArrayMap;

	UClass* BaseType;

	UObject* Outer;

	bool bPopulating;

private:
	friend class FSubsystemModuleWatcher;

	/** Add Instances of the specified Subsystem class to all existing SubsystemCollections of the correct type */
	static void AddAllInstances(UClass* SubsystemClass);

	/** Remove Instances of the specified Subsystem class from all existing SubsystemCollections of the correct type */
	static void RemoveAllInstances(UClass* SubsystemClass);

	static TArray<FSubsystemCollectionBase*> SubsystemCollections;
	static TMap<FName, TArray<TSubclassOf<UDynamicSubsystem>>> DynamicSystemModuleMap;
};

template<typename TBaseType>
class FSubsystemCollection : public FSubsystemCollectionBase
{
public:
	/** Get a Subsystem by type */
	template <typename TSubsystemClass>
	TSubsystemClass* GetSubsystem(const TSubclassOf<TSubsystemClass>& SubsystemClass) const
	{
		static_assert(TIsDerivedFrom<TSubsystemClass, TBaseType>::IsDerived, "TSubsystemClass must be derived from TBaseType");

		// A static cast is safe here because we know SubsystemClass derives from TSubsystemClass if it is not null
		return static_cast<TSubsystemClass*>(GetSubsystemInternal(SubsystemClass));
	}

	/** Get a list of Subsystems by type */
	template <typename TSubsystemClass>
	const TArray<TSubsystemClass*>& GetSubsystemArray(const TSubclassOf<TSubsystemClass>& SubsystemClass) const
	{
		// Force a compile time check that TSubsystemClass derives from TBaseType, the internal code only enforces it's a USubsystem
		TSubclassOf<TBaseType> SubsystemBaseClass = SubsystemClass;

		const TArray<USubsystem*>& Array = GetSubsystemArrayInternal(SubsystemBaseClass);
		const TArray<TSubsystemClass*>* SpecificArray = reinterpret_cast<const TArray<TSubsystemClass*>*>(&Array);
		return *SpecificArray;
	}

public:

	/** Construct a FSubsystemCollection, pass in the owning object almost certainly (this). */
	FSubsystemCollection()
		: FSubsystemCollectionBase(TBaseType::StaticClass())
	{
	}
};

