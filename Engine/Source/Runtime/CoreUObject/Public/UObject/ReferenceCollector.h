// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectPtr.h"

/** Used by garbage collector to collect references via virtual AddReferencedObjects calls */
class COREUOBJECT_API FReferenceCollector
{
public:
	virtual ~FReferenceCollector() {}
	
	/** Preferred way to add a reference that allows batching. Object must outlive GC tracing, can't be used for temporary/stack references. */
	virtual void AddStableReference(TObjectPtr<UObject>* Object);
	
	/** Preferred way to add a reference array that allows batching. Can't be used for temporary/stack array. */
	virtual void AddStableReferenceArray(TArray<TObjectPtr<UObject>>* Objects);

	/** Preferred way to add a reference set that allows batching. Can't be used for temporary/stack set. */
	virtual void AddStableReferenceSet(TSet<TObjectPtr<UObject>>* Objects);

	template <typename KeyType, typename ValueType, typename Allocator, typename KeyFuncs>
	FORCEINLINE_DEBUGGABLE void AddStableReferenceMap(TMapBase<KeyType, ValueType, Allocator, KeyFuncs>& Map)
	{
#if UE_DEPRECATE_RAW_UOBJECTPTR_ARO
		static constexpr bool bKeyReference = IsTObjectPtr<KeyType>::Value;
		static constexpr bool bValueReference = IsTObjectPtr<ValueType>::Value;
		static_assert(bKeyReference || bValueReference);
		static_assert(!(std::is_pointer_v<KeyType> && std::is_convertible_v<KeyType, const UObjectBase*>));
		static_assert(!(std::is_pointer_v<ValueType> && std::is_convertible_v<ValueType, const UObjectBase*>));
#else		 
		static constexpr bool bKeyReference =	std::is_convertible_v<KeyType, const UObjectBase*>;
		static constexpr bool bValueReference =	std::is_convertible_v<ValueType, const UObjectBase*>;
		static_assert(bKeyReference || bValueReference, "Key or value must be pointer to fully-defined UObject type");
#endif
		
		for (TPair<KeyType, ValueType>& Pair : Map)
		{
			if constexpr (bKeyReference)
			{
				AddStableReference(&Pair.Key);
			}
			if constexpr (bValueReference)
			{
				AddStableReference(&Pair.Value);
			}
		}
	}

#if !UE_DEPRECATE_RAW_UOBJECTPTR_ARO
	/** Preferred way to add a reference that allows batching. Object must outlive GC tracing, can't be used for temporary/stack references. */
	virtual void AddStableReference(UObject** Object);
	
	/** Preferred way to add a reference array that allows batching. Can't be used for temporary/stack array. */
	virtual void AddStableReferenceArray(TArray<UObject*>* Objects);

	/** Preferred way to add a reference set that allows batching. Can't be used for temporary/stack set. */
	virtual void AddStableReferenceSet(TSet<UObject*>* Objects);

	template<class UObjectType>
	FORCEINLINE void AddStableReference(UObjectType** Object)
	{
		static_assert(sizeof(UObjectType) > 0, "Element must be a pointer to a fully-defined type");
		static_assert(std::is_convertible_v<UObjectType*, const UObjectBase*>, "Element must be a pointer to a type derived from UObject");
		AddStableReference(reinterpret_cast<UObject**>(Object));
	}

	template<class UObjectType>
	FORCEINLINE void AddStableReferenceArray(TArray<UObjectType*>* Objects)
	{
		static_assert(sizeof(UObjectType) > 0, "Element must be a pointer to a fully-defined type");
		static_assert(std::is_convertible_v<UObjectType*, const UObjectBase*>, "Element must be a pointer to a type derived from UObject");
		AddStableReferenceArray(reinterpret_cast<TArray<UObject*>*>(Objects)); 
	}

	template<class UObjectType>
	FORCEINLINE void AddStableReferenceSet(TSet<UObjectType*>* Objects)
	{
		static_assert(sizeof(UObjectType) > 0, "Element must be a pointer to a fully-defined type");
		static_assert(std::is_convertible_v<UObjectType*, const UObjectBase*>, "Element must be a pointer to a type derived from UObject");
		AddStableReferenceSet(reinterpret_cast<TSet<UObject*>*>(Objects)); 
	}
#endif

	template<class UObjectType>
	FORCEINLINE void AddStableReference(TObjectPtr<UObjectType>* Object)
	{
		AddStableReference(reinterpret_cast<TObjectPtr<UObject>*>(Object));
	}

	template<class UObjectType>
	FORCEINLINE void AddStableReferenceArray(TArray<TObjectPtr<UObjectType>>* Objects)
	{
		AddStableReferenceArray(reinterpret_cast<TArray<TObjectPtr<UObject>>*>(Objects)); 
	}

	template<class UObjectType>
	FORCEINLINE void AddStableReferenceSet(TSet<TObjectPtr<UObjectType>>* Objects)
	{
		AddStableReferenceSet(reinterpret_cast<TSet<TObjectPtr<UObject>>*>(Objects)); 
	}

#if !UE_DEPRECATE_RAW_UOBJECTPTR_ARO	
	/**
	 * Adds object reference.
	 *
	 * @param Object Referenced object.
	 * @param ReferencingObject Referencing object (if available).
	 * @param ReferencingProperty Referencing property (if available).
	 */
	template<class UObjectType>
	void AddReferencedObject(UObjectType*& Object, const UObject* ReferencingObject = nullptr, const FProperty* ReferencingProperty = nullptr)
	{
		AROPrivate::AddReferencedObject<UObjectType>(*this, Object, ReferencingObject, ReferencingProperty);
	}

	/**
	 * Adds const object reference, this reference can still be nulled out if forcefully collected.
	 *
	 * @param Object Referenced object.
	 * @param ReferencingObject Referencing object (if available).
	 * @param ReferencingProperty Referencing property (if available).
	 */
	template<class UObjectType>
	void AddReferencedObject(const UObjectType*& Object, const UObject* ReferencingObject = nullptr, const FProperty* ReferencingProperty = nullptr)
	{
		AROPrivate::AddReferencedObject<UObjectType>(*this, Object, ReferencingObject, ReferencingProperty);
	}

	/**
	* Adds references to an array of objects.
	*
	* @param ObjectArray Referenced objects array.
	* @param ReferencingObject Referencing object (if available).
	* @param ReferencingProperty Referencing property (if available).
	*/
	template<class UObjectType>
	void AddReferencedObjects(TArray<UObjectType*>& ObjectArray, const UObject* ReferencingObject = nullptr, const FProperty* ReferencingProperty = nullptr)
	{
		AROPrivate::AddReferencedObjects<UObjectType>(*this, ObjectArray, ReferencingObject, ReferencingProperty);
	}

	/**
	* Adds references to an array of const objects, these objects can still be nulled out if forcefully collected.
	*
	* @param ObjectArray Referenced objects array.
	* @param ReferencingObject Referencing object (if available).
	* @param ReferencingProperty Referencing property (if available).
	*/
	template<class UObjectType>
	void AddReferencedObjects(TArray<const UObjectType*>& ObjectArray, const UObject* ReferencingObject = nullptr, const FProperty* ReferencingProperty = nullptr)
	{
		AROPrivate::AddReferencedObjects<UObjectType>(*this, ObjectArray, ReferencingObject, ReferencingProperty);
	}

	/**
	* Adds references to a set of objects.
	*
	* @param ObjectSet Referenced objects set.
	* @param ReferencingObject Referencing object (if available).
	* @param ReferencingProperty Referencing property (if available).
	*/
	template<class UObjectType>
	void AddReferencedObjects(TSet<UObjectType*>& ObjectSet, const UObject* ReferencingObject = nullptr, const FProperty* ReferencingProperty = nullptr)
	{
		AROPrivate::AddReferencedObjects<UObjectType>(*this, ObjectSet, ReferencingObject, ReferencingProperty);
	}

	/**
	 * Adds references to a map of objects.
	 *
	 * @param Map Referenced objects map.
	 * @param ReferencingObject Referencing object (if available).
	 * @param ReferencingProperty Referencing property (if available).
	 */
	template <typename KeyType, typename ValueType, typename Allocator, typename KeyFuncs>
	void AddReferencedObjects(TMapBase<KeyType*, ValueType, Allocator, KeyFuncs>& Map, const UObject* ReferencingObject = nullptr, const FProperty* ReferencingProperty = nullptr)
	{
		AROPrivate::AddReferencedObjects<KeyType, ValueType, Allocator, KeyFuncs>(*this, Map, ReferencingObject, ReferencingProperty);
	}

	template <typename KeyType, typename ValueType, typename Allocator, typename KeyFuncs>
	void AddReferencedObjects(TMapBase<KeyType, ValueType*, Allocator, KeyFuncs>& Map, const UObject* ReferencingObject = nullptr, const FProperty* ReferencingProperty = nullptr)
	{
		AROPrivate::AddReferencedObjects<KeyType, ValueType, Allocator, KeyFuncs>(*this, Map, ReferencingObject, ReferencingProperty);
	}

	template <typename KeyType, typename ValueType, typename Allocator, typename KeyFuncs>
	void AddReferencedObjects(TMapBase<KeyType*, ValueType*, Allocator, KeyFuncs>& Map, const UObject* ReferencingObject = nullptr, const FProperty* ReferencingProperty = nullptr)
	{
		AROPrivate::AddReferencedObjects<KeyType, ValueType, Allocator, KeyFuncs>(*this, Map, ReferencingObject, ReferencingProperty);
	}
#endif

	/**
	 * Adds object reference.
	 *
	 * @param Object Referenced object.
	 * @param ReferencingObject Referencing object (if available).
	 * @param ReferencingProperty Referencing property (if available).
	 */
	template<class UObjectType>
	void AddReferencedObject(TObjectPtr<UObjectType>& Object, const UObject* ReferencingObject = nullptr, const FProperty* ReferencingProperty = nullptr)
	{
		if (Object.IsResolved())
		{
			HandleObjectReference(*reinterpret_cast<UObject**>(&Object), ReferencingObject, ReferencingProperty);
		}
	}

	/**
	 * Adds const object reference, this reference can still be nulled out if forcefully collected.
	 *
	 * @param Object Referenced object.
	 * @param ReferencingObject Referencing object (if available).
	 * @param ReferencingProperty Referencing property (if available).
	 */
	template<class UObjectType>
	void AddReferencedObject(TObjectPtr<const UObjectType>& Object, const UObject* ReferencingObject = nullptr, const FProperty* ReferencingProperty = nullptr)
	{
		static_assert(sizeof(UObjectType) > 0, "AddReferencedObject: Element must be a pointer to a fully-defined type");
		static_assert(TPointerIsConvertibleFromTo<UObjectType, const UObjectBase>::Value, "AddReferencedObject: Element must be a pointer to a type derived from UObject");
		if (Object.IsResolved())
		{
			HandleObjectReference(*reinterpret_cast<UObject**>(&Object), ReferencingObject, ReferencingProperty);
		}
	}

	/**
	* Adds references to an array of objects.
	*
	* @param ObjectArray Referenced objects array.
	* @param ReferencingObject Referencing object (if available).
	* @param ReferencingProperty Referencing property (if available).
	*/
	template<class UObjectType>
	void AddReferencedObjects(TArray<TObjectPtr<UObjectType>>& ObjectArray, const UObject* ReferencingObject = nullptr, const FProperty* ReferencingProperty = nullptr)
	{
		static_assert(sizeof(UObjectType) > 0, "AddReferencedObjects: Elements must be pointers to a fully-defined type");
		static_assert(TPointerIsConvertibleFromTo<UObjectType, const UObjectBase>::Value, "AddReferencedObjects: Elements must be pointers to a type derived from UObject");
		// Cannot use a reinterpret_cast due to MSVC (and not Clang) emitting a warning:
		// C4946: reinterpret_cast used between related classes: ...
		HandleObjectReferences((FObjectPtr*)(ObjectArray.GetData()), ObjectArray.Num(), ReferencingObject, ReferencingProperty);
	}

	/**
	* Adds references to an array of const objects, these objects can still be nulled out if forcefully collected.
	*
	* @param ObjectArray Referenced objects array.
	* @param ReferencingObject Referencing object (if available).
	* @param ReferencingProperty Referencing property (if available).
	*/
	template<class UObjectType>
	void AddReferencedObjects(TArray<TObjectPtr<const UObjectType>>& ObjectArray, const UObject* ReferencingObject = nullptr, const FProperty* ReferencingProperty = nullptr)
	{
		static_assert(sizeof(UObjectType) > 0, "AddReferencedObjects: Elements must be pointers to a fully-defined type");
		static_assert(TPointerIsConvertibleFromTo<UObjectType, const UObjectBase>::Value, "AddReferencedObjects: Elements must be pointers to a type derived from UObject");
		// Cannot use a reinterpret_cast due to MSVC (and not Clang) emitting a warning:
		// C4946: reinterpret_cast used between related classes: ...
		HandleObjectReferences((FObjectPtr*)(ObjectArray.GetData()), ObjectArray.Num(), ReferencingObject, ReferencingProperty);
	}

	/**
	* Adds references to a set of objects.
	*
	* @param ObjectSet Referenced objects set.
	* @param ReferencingObject Referencing object (if available).
	* @param ReferencingProperty Referencing property (if available).
	*/
	template<class UObjectType>
	void AddReferencedObjects(TSet<TObjectPtr<UObjectType>>& ObjectSet, const UObject* ReferencingObject = nullptr, const FProperty* ReferencingProperty = nullptr)
	{
		static_assert(sizeof(UObjectType) > 0, "AddReferencedObjects: Elements must be pointers to a fully-defined type");
		static_assert(TPointerIsConvertibleFromTo<UObjectType, const UObjectBase>::Value, "AddReferencedObjects: Elements must be pointers to a type derived from UObject");
		for (auto& Object : ObjectSet)
		{
			if (Object.IsResolved())
			{
				HandleObjectReference(*reinterpret_cast<UObject**>(&Object), ReferencingObject, ReferencingProperty);
			}
		}
	}

	/**
	 * Adds references to a map of objects.
	 *
	 * @param ObjectArray Referenced objects map.
	 * @param ReferencingObject Referencing object (if available).
	 * @param ReferencingProperty Referencing property (if available).
	 */
	template <typename KeyType, typename ValueType, typename Allocator, typename KeyFuncs>
	void AddReferencedObjects(TMapBase<TObjectPtr<KeyType>, ValueType, Allocator, KeyFuncs>& Map, const UObject* ReferencingObject = nullptr, const FProperty* ReferencingProperty = nullptr)
	{
		static_assert(sizeof(KeyType) > 0, "AddReferencedObjects: Keys must be pointers to a fully-defined type");
		static_assert(TPointerIsConvertibleFromTo<KeyType, const UObjectBase>::Value, "AddReferencedObjects: Keys must be pointers to a type derived from UObject");
		static_assert(!UE_DEPRECATE_RAW_UOBJECTPTR_ARO ||
									!(std::is_pointer_v<ValueType> && std::is_convertible_v<ValueType, const UObjectBase*>));
		for (auto& It : Map)
		{
			if (It.Key.IsResolved())
			{
				HandleObjectReference(*reinterpret_cast<UObject**>(&It.Key), ReferencingObject, ReferencingProperty);
			}
		}
	}
	template <typename KeyType, typename ValueType, typename Allocator, typename KeyFuncs>
	void AddReferencedObjects(TMapBase<KeyType, TObjectPtr<ValueType>, Allocator, KeyFuncs>& Map, const UObject* ReferencingObject = nullptr, const FProperty* ReferencingProperty = nullptr)
	{
		static_assert(sizeof(ValueType) > 0, "AddReferencedObjects: Values must be pointers to a fully-defined type");
		static_assert(TPointerIsConvertibleFromTo<ValueType, const UObjectBase>::Value, "AddReferencedObjects: Values must be pointers to a type derived from UObject");
		static_assert(!UE_DEPRECATE_RAW_UOBJECTPTR_ARO ||
									!(std::is_pointer_v<KeyType> && std::is_convertible_v<KeyType, const UObjectBase*>));
		for (auto& It : Map)
		{
			if (It.Value.IsResolved())
			{
				HandleObjectReference(*reinterpret_cast<UObject**>(&It.Value), ReferencingObject, ReferencingProperty);
			}
		}
	}
	template <typename KeyType, typename ValueType, typename Allocator, typename KeyFuncs>
	void AddReferencedObjects(TMapBase<TObjectPtr<KeyType>, TObjectPtr<ValueType>, Allocator, KeyFuncs>& Map, const UObject* ReferencingObject = nullptr, const FProperty* ReferencingProperty = nullptr)
	{
		static_assert(sizeof(KeyType) > 0, "AddReferencedObjects: Keys must be pointers to a fully-defined type");
		static_assert(sizeof(ValueType) > 0, "AddReferencedObjects: Values must be pointers to a fully-defined type");
		static_assert(TPointerIsConvertibleFromTo<KeyType, const UObjectBase>::Value, "AddReferencedObjects: Keys must be pointers to a type derived from UObject");
		static_assert(TPointerIsConvertibleFromTo<ValueType, const UObjectBase>::Value, "AddReferencedObjects: Values must be pointers to a type derived from UObject");
		for (auto& It : Map)
		{
			if (It.Key.IsResolved())
			{
				HandleObjectReference(*reinterpret_cast<UObject**>(&It.Key), ReferencingObject, ReferencingProperty);
			}
			if (It.Value.IsResolved())
			{
				HandleObjectReference(*reinterpret_cast<UObject**>(&It.Value), ReferencingObject, ReferencingProperty);
			}
		}
	}
	
	template <typename T>
	void AddReferencedObject(TWeakObjectPtr<T>& P,
													 const UObject* ReferencingObject = nullptr,
													 const FProperty* ReferencingProperty = nullptr)
	{
		AddReferencedObject(reinterpret_cast<FWeakObjectPtr&>(P),
												ReferencingObject,
												ReferencingProperty);
	}

	void AddReferencedObject(FWeakObjectPtr& P,
													 const UObject* ReferencingObject = nullptr,
													 const FProperty* ReferencingProperty = nullptr);

	/**
	 * Adds all strong property references from a UScriptStruct instance including the struct itself
	 * 
	 * Only necessary to handle cases of an unreflected/non-UPROPERTY struct that wants to have references emitted.
	 *
	 * Calls AddStructReferencedObjects() but not recursively on nested structs. 
	 * 
	 * This and other AddPropertyReferences functions will hopefully merge into a single function in the future.
	 * They're kept separate initially to maintain exact semantics while replacing the much slower
	 * SerializeBin/TPropertyValueIterator/GetVerySlowReferenceCollectorArchive paths.
	 */
#if !UE_DEPRECATE_RAW_UOBJECTPTR_ARO
	void AddReferencedObjects(const UScriptStruct*& ScriptStruct, void* Instance, const UObject* ReferencingObject = nullptr, const FProperty* ReferencingProperty = nullptr);
#endif

	void AddReferencedObjects(TObjectPtr<const UScriptStruct>& ScriptStruct, void* Instance, const UObject* ReferencingObject = nullptr, const FProperty* ReferencingProperty = nullptr);
	void AddReferencedObjects(TWeakObjectPtr<const UScriptStruct>& ScriptStruct, void* Instance, const UObject* ReferencingObject = nullptr, const FProperty* ReferencingProperty = nullptr);
	
	/** Adds all strong property references from a struct instance, but not the struct itself. Skips AddStructReferencedObjects. */
	void AddPropertyReferences(const UStruct* Struct, void* Instance, const UObject* ReferencingObject = nullptr);
	
	/** Same as AddPropertyReferences but also calls AddStructReferencedObjects on Struct and all nested structs */
	void AddPropertyReferencesWithStructARO(const UScriptStruct* Struct, void* Instance, const UObject* ReferencingObject = nullptr);

	/** Same as AddPropertyReferences but also calls AddStructReferencedObjects on all nested structs */
	void AddPropertyReferencesWithStructARO(const UClass* Class, void* Instance, const UObject* ReferencingObject = nullptr);

	/** Internal use only. Same as AddPropertyReferences but skips field path and interface properties. Might get removed. */
	void AddPropertyReferencesLimitedToObjectProperties(const UStruct* Struct, void* Instance, const UObject* ReferencingObject = nullptr);

	/**
	 * Make Add[OnlyObject]PropertyReference/AddReferencedObjects(UScriptStruct) use AddReferencedObjects(UObject*&) callbacks
	 * with ReferencingObject and ReferencingProperty context supplied and check for null references before making a callback.
	 * 
	 * Return false to use context free AddStableReference callbacks without null checks that avoid sync cache misses when batch processing references.
	 */
	virtual bool NeedsPropertyReferencer() const { return true; }

	/**
	 * If true archetype references should not be added to this collector.
	 */
	virtual bool IsIgnoringArchetypeRef() const = 0;
	/**
	 * If true transient objects should not be added to this collector.
	 */
	virtual bool IsIgnoringTransient() const = 0;
	/**
	 * Allows reference elimination by this collector.
	 */
	virtual void AllowEliminatingReferences(bool bAllow) {}


	/**
	 * Sets the property that is currently being serialized
	 */
	virtual void SetSerializedProperty(class FProperty* Inproperty) {}
	/**
	 * Gets the property that is currently being serialized
	 */
	virtual class FProperty* GetSerializedProperty() const { return nullptr; }
	/** 
	 * Marks a specific object reference as a weak reference. This does not affect GC but will be freed at a later point
	 * The default behavior returns false as weak references must be explicitly supported
	 */
	virtual bool MarkWeakObjectReferenceForClearing(UObject** WeakReference) { return false; }
	/**
	 * Sets whether this collector is currently processing native references or not.
	 */
	virtual void SetIsProcessingNativeReferences(bool bIsNative) {}
	/**
	 * If true, this collector is currently processing native references (true by default).
	 */
	virtual bool IsProcessingNativeReferences() const { return true; }

	/** Used by parallel reachability analysis to pre-collect and then exclude some initial FGCObject references */
	virtual bool NeedsInitialReferences() const { return true; }

	/**
	* Get archive to collect references via SerializeBin / Serialize.
	*
	* NOTE: Prefer using AddPropertyReferences or AddReferencedObjects(const UScriptStruct&) instead, they're much faster.
	*/
	FReferenceCollectorArchive& GetVerySlowReferenceCollectorArchive()
	{
		if (!DefaultReferenceCollectorArchive)
		{
			CreateVerySlowReferenceCollectorArchive();
		}
		return *DefaultReferenceCollectorArchive;
	}


	struct AROPrivate final // nb: internal use only
	{
		template<class UObjectType>
		static void AddReferencedObject(FReferenceCollector& Coll,
																		UObjectType*& Object, const UObject* ReferencingObject = nullptr, const FProperty* ReferencingProperty = nullptr)
		{
			// @todo: should be uncommented when proper usage is fixed everywhere
			// static_assert(sizeof(UObjectType) > 0, "AddReferencedObject: Element must be a pointer to a fully-defined type");
			// static_assert(TPointerIsConvertibleFromTo<UObjectType, const UObjectBase>::Value, "AddReferencedObject: Element must be a pointer to a type derived from UObject");
			Coll.HandleObjectReference(*(UObject**)&Object, ReferencingObject, ReferencingProperty);
		}

		template<class UObjectType>
		static void AddReferencedObject(FReferenceCollector& Coll,
																		const UObjectType*& Object,
																		const UObject* ReferencingObject = nullptr,
																		const FProperty* ReferencingProperty = nullptr)
		{
			// @todo: should be uncommented when proper usage is fixed everywhere
			// static_assert(sizeof(UObjectType) > 0, "AddReferencedObject: Element must be a pointer to a fully-defined type");
			// static_assert(TPointerIsConvertibleFromTo<UObjectType, const UObjectBase>::Value, "AddReferencedObject: Element must be a pointer to a type derived from UObject");
			Coll.HandleObjectReference(*(UObject**)const_cast<UObjectType**>(&Object), ReferencingObject, ReferencingProperty);
		}

		template<class UObjectType>
		static void AddReferencedObjects(FReferenceCollector& Coll,
																		 TArray<UObjectType*>& ObjectArray,
																		 const UObject* ReferencingObject = nullptr,
																		 const FProperty* ReferencingProperty = nullptr)
		{
			static_assert(sizeof(UObjectType) > 0, "AddReferencedObjects: Elements must be pointers to a fully-defined type");
			static_assert(TPointerIsConvertibleFromTo<UObjectType, const UObjectBase>::Value, "AddReferencedObjects: Elements must be pointers to a type derived from UObject");
			Coll.HandleObjectReferences(reinterpret_cast<UObject**>(ObjectArray.GetData()), ObjectArray.Num(), ReferencingObject, ReferencingProperty);
		}
		
		/**
		 * Adds references to an array of const objects, these objects can still be nulled out if forcefully collected.
		 *
		 * @param ObjectArray Referenced objects array.
		 * @param ReferencingObject Referencing object (if available).
		 * @param ReferencingProperty Referencing property (if available).
		 */
		template<class UObjectType>
		static void AddReferencedObjects(FReferenceCollector& Coll,
																		 TArray<const UObjectType*>& ObjectArray,
																		 const UObject* ReferencingObject = nullptr,
																		 const FProperty* ReferencingProperty = nullptr)
		{
			static_assert(sizeof(UObjectType) > 0, "AddReferencedObjects: Elements must be pointers to a fully-defined type");
			static_assert(TPointerIsConvertibleFromTo<UObjectType, const UObjectBase>::Value, "AddReferencedObjects: Elements must be pointers to a type derived from UObject");
			Coll.HandleObjectReferences(reinterpret_cast<UObject**>(const_cast<UObjectType**>(ObjectArray.GetData())), ObjectArray.Num(), ReferencingObject, ReferencingProperty);
		}

		template<class UObjectType>
		static void AddReferencedObjects(FReferenceCollector& Coll,
																		 TSet<UObjectType*>& ObjectSet,
																		 const UObject* ReferencingObject = nullptr,
																		 const FProperty* ReferencingProperty = nullptr)
		{
			static_assert(sizeof(UObjectType) > 0, "AddReferencedObjects: Elements must be pointers to a fully-defined type");
			static_assert(TPointerIsConvertibleFromTo<UObjectType, const UObjectBase>::Value, "AddReferencedObjects: Elements must be pointers to a type derived from UObject");
			for (auto& Object : ObjectSet)
			{
				Coll.HandleObjectReference(*(UObject**)&Object, ReferencingObject, ReferencingProperty);
			}
		}
	
		template <typename KeyType, typename ValueType, typename Allocator, typename KeyFuncs>
		static void AddReferencedObjects(FReferenceCollector& Coll,
																		 TMapBase<KeyType*, ValueType, Allocator, KeyFuncs>& Map,
																		 const UObject* ReferencingObject = nullptr,
																		 const FProperty* ReferencingProperty = nullptr)
		{
			static_assert(sizeof(KeyType) > 0, "AddReferencedObjects: Keys must be pointers to a fully-defined type");
			static_assert(TPointerIsConvertibleFromTo<KeyType, const UObjectBase>::Value, "AddReferencedObjects: Keys must be pointers to a type derived from UObject");
			for (auto& It : Map)
			{
				Coll.HandleObjectReference(*(UObject**)&It.Key, ReferencingObject, ReferencingProperty);
			}
		}

		template <typename KeyType, typename ValueType, typename Allocator, typename KeyFuncs>
		static void AddReferencedObjects(FReferenceCollector& Coll,
																		 TMapBase<KeyType, ValueType*, Allocator, KeyFuncs>& Map,
																		 const UObject* ReferencingObject = nullptr,
																		 const FProperty* ReferencingProperty = nullptr)
		{
			static_assert(sizeof(ValueType) > 0, "AddReferencedObjects: Values must be pointers to a fully-defined type");
			static_assert(TPointerIsConvertibleFromTo<ValueType, const UObjectBase>::Value, "AddReferencedObjects: Values must be pointers to a type derived from UObject");
			for (auto& It : Map)
			{
				Coll.HandleObjectReference(*(UObject**)&It.Value, ReferencingObject, ReferencingProperty);
			}
		}

		template <typename KeyType, typename ValueType, typename Allocator, typename KeyFuncs>
		static void AddReferencedObjects(FReferenceCollector& Coll, TMapBase<KeyType*, ValueType*, Allocator, KeyFuncs>& Map, const UObject* ReferencingObject = nullptr, const FProperty* ReferencingProperty = nullptr)
		{
			static_assert(sizeof(KeyType) > 0, "AddReferencedObjects: Keys must be pointers to a fully-defined type");
			static_assert(sizeof(ValueType) > 0, "AddReferencedObjects: Values must be pointers to a fully-defined type");
			static_assert(TPointerIsConvertibleFromTo<KeyType, const UObjectBase>::Value, "AddReferencedObjects: Keys must be pointers to a type derived from UObject");
			static_assert(TPointerIsConvertibleFromTo<ValueType, const UObjectBase>::Value, "AddReferencedObjects: Values must be pointers to a type derived from UObject");
			for (auto& It : Map)
			{
				Coll.HandleObjectReference(*(UObject**)&It.Key, ReferencingObject, ReferencingProperty);
				Coll.HandleObjectReference(*(UObject**)&It.Value, ReferencingObject, ReferencingProperty);
			}
		}

		static void AddReferencedObjects(FReferenceCollector& Coll,
																		 const UScriptStruct*& ScriptStruct,
																		 void* Instance, const UObject* ReferencingObject = nullptr, const FProperty* ReferencingProperty = nullptr);
	};  
  
protected:
	/**
	 * Handle object reference. Called by AddReferencedObject.
	 *
	 * @param Object Referenced object.
	 * @param ReferencingObject Referencing object (if available).
	 * @param ReferencingProperty Referencing property (if available).
	 */
	virtual void HandleObjectReference(UObject*& InObject, const UObject* InReferencingObject, const FProperty* InReferencingProperty) = 0;

	/**
	* Handle multiple object references. Called by AddReferencedObjects.
	* DEFAULT IMPLEMENTATION IS SLOW as it calls HandleObjectReference multiple times. In order to optimize it, provide your own implementation.
	*
	* @param Object Referenced object.
	* @param ReferencingObject Referencing object (if available).
	* @param ReferencingProperty Referencing property (if available).
	*/
	virtual void HandleObjectReferences(UObject** InObjects, const int32 ObjectNum, const UObject* InReferencingObject, const FProperty* InReferencingProperty)
	{
		for (int32 ObjectIndex = 0; ObjectIndex < ObjectNum; ++ObjectIndex)
		{
			UObject*& Object = InObjects[ObjectIndex];
			HandleObjectReference(Object, InReferencingObject, InReferencingProperty);
		}
	}

	/**
	* Handle multiple object references. Called by AddReferencedObjects.
	* DEFAULT IMPLEMENTATION IS SLOW as it calls HandleObjectReference multiple times. In order to optimize it, provide your own implementation.
	*
	* @param Object Referenced object.
	* @param ReferencingObject Referencing object (if available).
	* @param ReferencingProperty Referencing property (if available).
	*/
	virtual void HandleObjectReferences(FObjectPtr* InObjects, const int32 ObjectNum, const UObject* InReferencingObject, const FProperty* InReferencingProperty);

private:
	/** Creates the proxy archive that uses serialization to add objects to this collector */
	void CreateVerySlowReferenceCollectorArchive();

	/** Default proxy archive that uses serialization to add objects to this collector */
	TUniquePtr<FReferenceCollectorArchive> DefaultReferenceCollectorArchive;
};

/**
 * FReferenceFinder.
 * Helper class used to collect object references.
 */
class COREUOBJECT_API FReferenceFinder : public FReferenceCollector
{
public:

	/**
	 * Constructor
	 *
	 * @param InObjectArray Array to add object references to
	 * @param	InOuter					value for LimitOuter
	 * @param	bInRequireDirectOuter	value for bRequireDirectOuter
	 * @param	bShouldIgnoreArchetype	whether to disable serialization of ObjectArchetype references
	 * @param	bInSerializeRecursively	only applicable when LimitOuter != nullptr && bRequireDirectOuter==true;
	 *									serializes each object encountered looking for subobjects of referenced
	 *									objects that have LimitOuter for their Outer (i.e. nested subobjects/components)
	 * @param	bShouldIgnoreTransient	true to skip serialization of transient properties
	 */
	FReferenceFinder(TArray<UObject*>& InObjectArray, UObject* InOuter = nullptr, bool bInRequireDirectOuter = true, bool bInShouldIgnoreArchetype = false, bool bInSerializeRecursively = false, bool bInShouldIgnoreTransient = false);

	/**
	 * Finds all objects referenced by Object.
	 *
	 * @param Object Object which references are to be found.
	 * @param ReferencingObject object that's referencing the current object.
	 * @param ReferencingProperty property the current object is being referenced through.
	 */
	virtual void FindReferences(UObject* Object, UObject* ReferencingObject = nullptr, FProperty* ReferencingProperty = nullptr);

	// FReferenceCollector interface.
	virtual void HandleObjectReference(UObject*& Object, const UObject* ReferencingObject, const FProperty* InReferencingProperty) override;
	virtual bool IsIgnoringArchetypeRef() const override { return bShouldIgnoreArchetype; }
	virtual bool IsIgnoringTransient() const override { return bShouldIgnoreTransient; }
	virtual void SetSerializedProperty(class FProperty* Inproperty) override
	{
		SerializedProperty = Inproperty;
	}
	virtual class FProperty* GetSerializedProperty() const override
	{
		return SerializedProperty;
	}
protected:

	/** Stored reference to array of objects we add object references to. */
	TArray<UObject*>&		ObjectArray;
	/** Set that duplicates ObjectArray. Keeps ObjectArray unique and avoids duplicate recursive serializing. */
	TSet<const UObject*>	ObjectSet;
	/** Only objects within this outer will be considered, nullptr value indicates that outers are disregarded. */
	UObject*		LimitOuter;
	/** Property that is referencing the current object */
	class FProperty* SerializedProperty;
	/** Determines whether nested objects contained within LimitOuter are considered. */
	bool			bRequireDirectOuter;
	/** Determines whether archetype references are considered. */
	bool			bShouldIgnoreArchetype;
	/** Determines whether we should recursively look for references of the referenced objects. */
	bool			bSerializeRecursively;
	/** Determines whether transient references are considered. */
	bool			bShouldIgnoreTransient;
};
