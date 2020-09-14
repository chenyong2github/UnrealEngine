// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "Components/ActorComponent.h"
#include "Selection.generated.h"

class UTypedElementList;

namespace Selection_Private
{

class ISelectionStoreSink
{
public:
	virtual ~ISelectionStoreSink() = default;

	/**
	 * Called when the given object is selected within the underlying store.
	 */
	virtual void OnObjectSelected(UObject* InObject, const bool bNotify) = 0;

	/**
	 * Called when the given object is deselected within the underlying store.
	 */
	virtual void OnObjectDeselected(UObject* InObject, const bool bNotify) = 0;

	/**
	 * Called when the the underlying store changes in an unknown way.
	 */
	virtual void OnSelectedChanged(const bool bSyncState, const bool bNotify) = 0;
};

class ISelectionStore
{
public:
	virtual ~ISelectionStore() = default;

	/**
	 * Set the notification sink instance for this store.
	 */
	virtual void SetSink(ISelectionStoreSink* InSink) = 0;

	/**
	 * Set the element list instance for this store.
	 * @note Does nothing if element list stores aren't enabled. Asserts for non-element list stores if they are!
	 */
	virtual void SetElementList(UTypedElementList* InElementList) = 0;

	/**
	 * Get the number of objects within the underlying store.
	 * This is the total number of objects within the store (for use as an upper limit of GetObjectAtIndex),
	 * however not all of those objects may be valid so it should not be used as a public selection count.
	 */
	virtual int32 GetNumObjects() const = 0;

	/**
	 * Get the object at the internal index of the underlying store.
	 * This object may be null, both in cases where the underlying store is using weak references, 
	 * and also in the case that the object does not match the type managed by the underlying store.
	 */
	virtual UObject* GetObjectAtIndex(const int32 InIndex) const = 0;

	/**
	 * Test to see whether the given object is valid to be added to the underlying store.
	 */
	virtual bool IsValidObjectToSelect(const UObject* InObject) const = 0;

	/**
	 * Test to see whether the given object is currently in the underlying store.
	 */
	virtual bool IsObjectSelected(const UObject* InObject) const = 0;

	/**
	 * Add the given object to the underlying store.
	 */
	virtual void SelectObject(UObject* InObject) = 0;

	/**
	 * Remove the given object from the underlying store.
	 */
	virtual void DeselectObject(UObject* InObject) = 0;

	/**
	 * Remove any objects that match the predicate from the underlying store.
	 */
	virtual int32 DeselectObjects(TFunctionRef<bool(UObject*)> InPredicate) = 0;

	/**
	 * Called to begin a batch selection.
	 */
	virtual void BeginBatchSelection() = 0;

	/**
	 * Called to end a batch selection.
	 */
	virtual void EndBatchSelection(const bool InNotify) = 0;

	/**
	 * Are we currently batch selecting?
	 */
	virtual bool IsBatchSelecting() const = 0;

	/**
	 * Forcibly mark this batch as being dirty.
	 */
	virtual void ForceBatchDirty() = 0;
};

} // namespace Selection_Private

/**
 * Manages selections of objects.
 * Used in the editor for selecting objects in the various browser windows.
 */
UCLASS(transient)
class UNREALED_API USelection : public UObject, public Selection_Private::ISelectionStoreSink
{
	GENERATED_BODY()

private:
	/** Contains info about each class and how many objects of that class are selected */
	struct FSelectedClassInfo
	{
		/** The selected class */
		const UClass* Class;
		/** How many objects of that class are selected */
		int32 SelectionCount;

		FSelectedClassInfo(const UClass* InClass)
			: Class(InClass)
			, SelectionCount(0)
		{}

		FSelectedClassInfo(const UClass* InClass, int32 InSelectionCount)
			: Class(InClass)
			, SelectionCount(InSelectionCount)
		{}
		bool operator==(const FSelectedClassInfo& Info) const
		{
			return Class == Info.Class;
		}

		friend uint32 GetTypeHash(const FSelectedClassInfo& Info)
		{
			return GetTypeHash(Info.Class);
		}
	};

	typedef TSet<FSelectedClassInfo> ClassArray;

	template<typename SelectionFilter>
	friend class TSelectionIterator;

public:
	static USelection* CreateObjectSelection(FUObjectAnnotationSparseBool* InSelectionAnnotation, UObject* InOuter = GetTransientPackage(), FName InName = NAME_None, EObjectFlags InFlags = RF_NoFlags);
	static USelection* CreateActorSelection(FUObjectAnnotationSparseBool* InSelectionAnnotation, UObject* InOuter = GetTransientPackage(), FName InName = NAME_None, EObjectFlags InFlags = RF_NoFlags);
	static USelection* CreateComponentSelection(FUObjectAnnotationSparseBool* InSelectionAnnotation, UObject* InOuter = GetTransientPackage(), FName InName = NAME_None, EObjectFlags InFlags = RF_NoFlags);

	/** Params: UObject* NewSelection */
	DECLARE_EVENT_OneParam(USelection, FOnSelectionChanged, UObject*);

	/** Called when selection in editor has changed */
	static FOnSelectionChanged SelectionChangedEvent;
	/** Called when an object has been selected (generally an actor) */
	static FOnSelectionChanged SelectObjectEvent;
	/** Called to deselect everything */
	static FSimpleMulticastDelegate SelectNoneEvent;

	typedef ClassArray::TIterator TClassIterator;
	typedef ClassArray::TConstIterator TClassConstIterator;

	TClassIterator			ClassItor()				{ return TClassIterator( SelectedClasses ); }
	TClassConstIterator		ClassConstItor() const	{ return TClassConstIterator( SelectedClasses ); }

	/**
	 * Set the element list instance for this selection set.
	 * @note Does nothing if element list stores aren't enabled. Asserts for non-element list stores if they are!
	 */
	void SetElementList(UTypedElementList* InElementList)
	{
		SelectionStore->SetElementList(InElementList);
	}

	/**
	 * Returns the number of objects in the selection set.  This function is used by clients in
	 * conjunction with op::() to iterate over selected objects.  Note that some of these objects
	 * may be NULL, and so clients should use CountSelections() to get the true number of
	 * non-NULL selected objects.
	 * 
	 * @return		Number of objects in the selection set.
	 */
	int32 Num() const
	{
		return SelectionStore->GetNumObjects();
	}

	/**
	 * @return	The Index'th selected objects.  May be NULL.
	 */
	UObject* GetSelectedObject(const int32 InIndex)
	{
		return SelectionStore->GetObjectAtIndex(InIndex);
	}

	/**
	 * @return	The Index'th selected objects.  May be NULL.
	 */
	const UObject* GetSelectedObject(const int32 InIndex) const
	{
		return SelectionStore->GetObjectAtIndex(InIndex);
	}

	/**
	 * Call before beginning selection operations
	 */
	void BeginBatchSelectOperation()
	{
		SelectionStore->BeginBatchSelection();
	}

	/**
	 * Should be called when selection operations are complete.  If all selection operations are complete, notifies all listeners
	 * that the selection has been changed.
	 */
	void EndBatchSelectOperation(bool bNotify = true)
	{
		SelectionStore->EndBatchSelection(bNotify);
	}

	/**
	 * @return	Returns whether or not the selection object is currently in the middle of a batch select block.
	 */
	bool IsBatchSelecting() const
	{
		return SelectionStore->IsBatchSelecting();
	}

	/**
	 * Selects the specified object.
	 *
	 * @param	InObject	The object to select/deselect.  Must be non-NULL.
	 */
	void Select(UObject* InObject);

	/**
	 * Deselects the specified object.
	 *
	 * @param	InObject	The object to deselect.  Must be non-NULL.
	 */
	void Deselect(UObject* InObject);

	/**
	 * Selects or deselects the specified object, depending on the value of the bSelect flag.
	 *
	 * @param	InObject	The object to select/deselect.  Must be non-NULL.
	 * @param	bSelect		true selects the object, false deselects.
	 */
	void Select(UObject* InObject, bool bSelect);

	/**
	 * Toggles the selection state of the specified object.
	 *
	 * @param	InObject	The object to select/deselect.  Must be non-NULL.
	 */
	void ToggleSelect(UObject* InObject);

	/**
	 * Deselects all objects of the specified class, if no class is specified it deselects all objects.
	 *
	 * @param	InClass		The type of object to deselect.  Can be NULL.
	 */
	void DeselectAll( UClass* InClass = NULL );

	/**
	 * If batch selection is active, sets flag indicating something actually changed.
	 */
	void ForceBatchDirty();

	/**
	 * Manually invoke a selection changed notification for this set.
	 */
	void NoteSelectionChanged();

	/**
	 * Manually invoke a selection changed notification for no specific set.
	 * @note Legacy BSP code only!
	 */
	static void NoteUnknownSelectionChanged();

	/**
	 * Returns the first selected object of the specified class.
	 *
	 * @param	InClass				The class of object to return.  Must be non-NULL.
	 * @param	RequiredInterface	[opt] Interface this class must implement to be returned.  May be NULL.
	 * @param	bArchetypesOnly		[opt] true to only return archetype objects, false otherwise
	 * @return						The first selected object of the specified class.
	 */
	UObject* GetTop(UClass* InClass, UClass* RequiredInterface=nullptr, bool bArchetypesOnly=false)
	{
		check( InClass );
		for( int32 i=0; i<Num(); ++i )
		{
			UObject* SelectedObject = GetSelectedObject(i);
			if (SelectedObject)
			{
				// maybe filter out non-archetypes
				if ( bArchetypesOnly && !SelectedObject->HasAnyFlags(RF_ArchetypeObject) )
				{
					continue;
				}

				if ( InClass->HasAnyClassFlags(CLASS_Interface) )
				{
					//~ Begin InClass is an Interface, and we want the top object that implements it
					if ( SelectedObject->GetClass()->ImplementsInterface(InClass) )
					{
						return SelectedObject;
					}
				}
				else if ( SelectedObject->IsA(InClass) )
				{
					//~ Begin InClass is a class, so we want the top object of that class that implements the required Interface, if specified
					if ( !RequiredInterface || SelectedObject->GetClass()->ImplementsInterface(RequiredInterface) )
					{
						return SelectedObject;
					}
				}
			}
		}
		return nullptr;
	}

	/**
	* Returns the last selected object of the specified class.
	*
	* @param	InClass		The class of object to return.  Must be non-NULL.
	* @return				The last selected object of the specified class.
	*/
	UObject* GetBottom(UClass* InClass)
	{
		check( InClass );
		for( int32 i = Num()-1 ; i > -1 ; --i )
		{
			UObject* SelectedObject = GetSelectedObject(i);
			if( SelectedObject && SelectedObject->IsA(InClass) )
			{
				return SelectedObject;
			}
		}
		return nullptr;
	}

	/**
	 * Returns the first selected object.
	 *
	 * @return				The first selected object.
	 */
	template< class T > T* GetTop()
	{
		UObject* Selected = GetTop(T::StaticClass());
		return Selected ? CastChecked<T>(Selected) : nullptr;
	}

	/**
	* Returns the last selected object.
	*
	* @return				The last selected object.
	*/
	template< class T > T* GetBottom()
	{
		UObject* Selected = GetBottom(T::StaticClass());
		return Selected ? CastChecked<T>(Selected) : nullptr;
	}

	/**
	 * Returns true if the specified object is non-NULL and selected.
	 *
	 * @param	InObject	The object to query.  Can be NULL.
	 * @return				true if the object is selected, or false if InObject is unselected or NULL.
	 */
	bool IsSelected(const UObject* InObject) const;

	/**
	 * Returns the number of selected objects of the specified type.
	 *
	 * @param	bIgnorePendingKill	specify true to count only those objects which are not pending kill (marked for garbage collection)
	 * @return						The number of objects of the specified type.
	 */
	template< class T >
	int32 CountSelections( bool bIgnorePendingKill=false )
	{
		return CountSelections(T::StaticClass(), bIgnorePendingKill);
	}

	/**
	 * Untemplated version of CountSelections.
	 */
	int32 CountSelections(UClass *ClassToCount, bool bIgnorePendingKill=false)
	{
		int32 Count = 0;
		for( int32 i=0; i<Num(); ++i )
		{
			UObject* SelectedObject = GetSelectedObject(i);
			if( SelectedObject && SelectedObject->IsA(ClassToCount) && !(bIgnorePendingKill && SelectedObject->IsPendingKill()) )
			{
				++Count;
			}
		}
		return Count;
	}

	bool IsClassSelected(UClass* Class) const
	{
		const FSelectedClassInfo* Info = SelectedClasses.Find(Class);
		return Info && Info->SelectionCount > 0;
	}

	//~ Begin UObject Interface
	virtual void Serialize(FArchive& Ar) override;
	virtual bool Modify( bool bAlwaysMarkDirty=true) override;
	virtual void BeginDestroy() override;

	//~ End UObject Interface


	/**
	 * Fills in the specified array with all selected objects of the desired type.
	 * 
	 * @param	OutSelectedObjects		[out] Array to fill with selected objects of type T
	 * @return							The number of selected objects of the specified type.
	 */
	template< class T > 
	int32 GetSelectedObjects(TArray<T*> &OutSelectedObjects)
	{
		OutSelectedObjects.Empty(Num());
		for (int32 Idx = 0; Idx < Num(); ++Idx)
		{
			UObject* SelectedObject = GetSelectedObject(Idx);
			if (SelectedObject && SelectedObject->IsA(T::StaticClass()))
			{
				OutSelectedObjects.Add((T*)SelectedObject);
			}
		}
		return OutSelectedObjects.Num();
	}

	int32 GetSelectedObjects( TArray<TWeakObjectPtr<UObject>>& OutSelectedObjects )
	{
		OutSelectedObjects.Empty(Num());
		for (int32 Idx = 0; Idx < Num(); ++Idx)
		{
			UObject* SelectedObject = GetSelectedObject(Idx);
			if (SelectedObject)
			{
				OutSelectedObjects.Add(SelectedObject);
			}
		}
		return OutSelectedObjects.Num();
	}

	int32 GetSelectedObjects(UClass *FilterClass, TArray<UObject*> &OutSelectedObjects)
	{
		OutSelectedObjects.Empty(Num());
		for (int32 Idx = 0; Idx < Num(); ++Idx)
		{
			UObject* SelectedObject = GetSelectedObject(Idx);
			if (SelectedObject && SelectedObject->IsA(FilterClass))
			{
				OutSelectedObjects.Add(SelectedObject);
			}
		}
		return OutSelectedObjects.Num();
	}

protected:
	/** Initializes the selection set with an annotation used to quickly look up selection state */
	void Initialize(FUObjectAnnotationSparseBool* InSelectionAnnotation, TSharedRef<Selection_Private::ISelectionStore>&& InSelectionStore);

	//~ ISelectionStoreSink interface
	virtual void OnObjectSelected(UObject* InObject, const bool bNotify) override;
	virtual void OnObjectDeselected(UObject* InObject, const bool bNotify) override;
	virtual void OnSelectedChanged(const bool bSyncState, const bool bNotify) override;
	
	/** Sync the state of the underlying selection store to the annotation and classes data */
	void SyncSelectedState();

	/** Store of selected objects. */
	TSharedPtr<Selection_Private::ISelectionStore> SelectionStore = nullptr;

	/** Tracks the most recently selected actor classes.  Used for UnrealEd menus. */
	ClassArray SelectedClasses;
	
	/** Selection annotation for fast lookup */
	FUObjectAnnotationSparseBool* SelectionAnnotation = nullptr;
	bool bOwnsSelectionAnnotation = false;

private:
	// Hide IsSelected(), as calling IsSelected() on a selection set almost always indicates
	// an error where the caller should use IsSelected(UObject* InObject).
	bool IsSelected() const
	{
		return UObject::IsSelected();
	}
};


/** A filter for generic selection sets.  Simply allows objects which are non-null */
class FGenericSelectionFilter
{
public:
	bool IsObjectValid( const UObject* InObject ) const
	{
		return InObject != nullptr;
	}
};

/**
 * Manages selections of objects.  Used in the editor for selecting
 * objects in the various browser windows.
 */
template<typename SelectionFilter>
class TSelectionIterator
{
public:
	TSelectionIterator(USelection& InSelection)
		: Selection( InSelection )
		, Filter( SelectionFilter() )
	{
		Reset();
	}

	/** Advances iterator to the next valid element in the container. */
	void operator++()
	{
		while ( true )
		{
			++Index;

			// Halt if the end of the selection set has been reached.
			if ( !IsIndexValid() )
			{
				return;
			}

			// Halt if at a valid object.
			if ( IsObjectValid() )
			{
				return;
			}
		}
	}

	/** Element access. */
	UObject* operator*() const
	{
		return GetCurrentObject();
	}

	/** Element access. */
	UObject* operator->() const
	{
		return GetCurrentObject();
	}

	/** Returns true if the iterator has not yet reached the end of the selection set. */
	explicit operator bool() const
	{
		return IsIndexValid();
	}

	/** Resets the iterator to the beginning of the selection set. */
	void Reset()
	{
		Index = -1;
		++( *this );
	}

	/** Returns an index to the current element. */
	int32 GetIndex() const
	{
		return Index;
	}

private:
	UObject* GetCurrentObject() const
	{
		return Selection.GetSelectedObject(Index);
	}

	bool IsObjectValid() const
	{
		return Filter.IsObjectValid( GetCurrentObject() );
	}

	bool IsIndexValid() const
	{
		return Index >= 0 && Index < Selection.Num();
	}

	USelection&	Selection;
	SelectionFilter Filter;
	int32			Index;
};


class FSelectionIterator : public TSelectionIterator<FGenericSelectionFilter>
{
public:
	FSelectionIterator(USelection& InSelection)
		: TSelectionIterator<FGenericSelectionFilter>( InSelection )
	{}
};

/** A filter for only iterating through editable components */
class FSelectedEditableComponentFilter
{
public:
	bool IsObjectValid(const UObject* Object) const
	{
		if (const UActorComponent* Comp = Cast<UActorComponent>( Object ))
		{
			return Comp->IsEditableWhenInherited();
		}
		return false;
	}
};

/**
 * An iterator used to iterate through selected components that are editable (i.e. not created in a blueprint)
 */
class FSelectedEditableComponentIterator : public TSelectionIterator<FSelectedEditableComponentFilter>
{
public:
	FSelectedEditableComponentIterator(USelection& InSelection)
		: TSelectionIterator<FSelectedEditableComponentFilter>(InSelection)
	{}
};
