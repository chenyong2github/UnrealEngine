// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MovieSceneEntityIDs.h"
#include "Async/TaskGraphInterfaces.h"
#include "EntitySystem/MovieSceneSystemTaskDependencies.h"

#include <initializer_list>

namespace UE
{
namespace MovieScene
{


struct FComponentAccessor
{
	enum { SupportsDirectEntityIteration = true };
	FComponentTypeID ComponentType;
};
struct FRead : FComponentAccessor
{
	FRead(FComponentTypeID InComponentType) : FComponentAccessor{ InComponentType } {}
};
struct FWrite : FComponentAccessor
{
	FWrite(FComponentTypeID InComponentType) : FComponentAccessor{ InComponentType } {}
};


struct FOptionalComponentAccessor
{
	enum { SupportsDirectEntityIteration = true };
	FComponentTypeID ComponentType;
};
struct FReadOptional : FOptionalComponentAccessor
{
	FReadOptional(FComponentTypeID InComponentType) : FOptionalComponentAccessor{ InComponentType } {}
};
struct FWriteOptional : FOptionalComponentAccessor
{
	FWriteOptional(FComponentTypeID InComponentType) : FOptionalComponentAccessor{ InComponentType } {}
};


struct FErasedIterState
{
	const uint8* ComponentPtr;
	int32 Sizeof;

	void operator++()
	{
		ComponentPtr += Sizeof;
	}

	const void* operator*()
	{
		return ComponentPtr;
	}

	const void* operator[](const int32 Index)
	{
		return ComponentPtr += Index*Sizeof;
	}
};

struct FErasedOptionalIterState
{
	uint8* ComponentPtr;
	int32 Sizeof;

	void operator++()
	{
		if (ComponentPtr)
		{
			ComponentPtr += Sizeof;
		}
	}

	void* operator*()
	{
		return ComponentPtr;
	}
};

template<typename T>
struct TComponentIterState
{
	T* ComponentPtr;

	void operator++()
	{
		++ComponentPtr;
	}
	T& operator*()
	{
		return *ComponentPtr;
	}
};

template<typename T, typename ProjectionType>
struct TProjectedComponentIterState
{
	T* ComponentPtr;
	ProjectionType Projection;

	void operator++()
	{
		++ComponentPtr;
	}
	auto operator*()
	{
		return Invoke(Projection, *ComponentPtr);
	}
};

template<typename T>
struct TOptionalIterState
{
	T* ComponentPtr;

	void operator++()
	{
		if (ComponentPtr)
		{
			++ComponentPtr;
		}
	}
	T* operator*()
	{
		return ComponentPtr;
	}
};

struct FReadEntityIDs
{
	enum { SupportsDirectEntityIteration = true };

	using Type        = FMovieSceneEntityID;
	using AccessType  = const FMovieSceneEntityID;

	TComponentIterState<AccessType> CreateIterState(const FEntityAllocation* Allocation) const
	{
		AccessType* ComponentArray = Resolve(Allocation);
		return TComponentIterState<AccessType>{ ComponentArray };
	}

	AccessType* Resolve(const FEntityAllocation* Allocation) const
	{
		return Allocation->GetRawEntityIDs();
	}

	TArrayView<AccessType> ResolveAsArray(const FEntityAllocation* Allocation) const
	{
		AccessType* Resolved = Resolve(Allocation);
		return MakeArrayView(Resolved, Allocation->Num());
	}
};




template<typename T>
struct TRead : FRead
{
	using Type        = T;
	using AccessType  = const T;

	TRead(FComponentTypeID InComponentTypeID)
		: FRead{ InComponentTypeID }
	{}

	TComponentIterState<AccessType> CreateIterState(const FEntityAllocation* Allocation) const
	{
		AccessType* ComponentArray = Resolve(Allocation);
		return TComponentIterState<AccessType>{ ComponentArray };
	}

	AccessType* Resolve(const FEntityAllocation* Allocation) const
	{
		const FComponentHeader& Header = Allocation->GetComponentHeaderChecked(this->ComponentType);
		return reinterpret_cast<AccessType*>(Header.Components);
	}

	TArrayView<AccessType> ResolveAsArray(const FEntityAllocation* Allocation) const
	{
		AccessType* Resolved = Resolve(Allocation);
		return MakeArrayView(Resolved, Allocation->Num());
	}
};


struct FReadErased : FRead
{
	using Type        = uint8;
	using AccessType  = const uint8;

	FReadErased(FComponentTypeID InComponentTypeID)
		: FRead{ InComponentTypeID }
	{}

	FErasedIterState CreateIterState(const FEntityAllocation* Allocation) const
	{
		const FComponentHeader& Header = Allocation->GetComponentHeaderChecked(this->ComponentType);
		return FErasedIterState{ Header.Components, Header.Sizeof };
	}
};


template<typename T, typename ProjectionType>
struct TReadProjected : FRead
{
	using Type        = T;
	using AccessType  = const T;

	ProjectionType Projection;

	static_assert(TNot<TIsReferenceType<ProjectionType>>::Value, "Reference projections are not supported");

	TReadProjected(FComponentTypeID InComponentTypeID, ProjectionType InProjection)
		: FRead{ InComponentTypeID }
		, Projection(InProjection)
	{}

	TProjectedComponentIterState<AccessType, ProjectionType> CreateIterState(const FEntityAllocation* Allocation) const
	{
		AccessType* ComponentArray = Resolve(Allocation);
		return TProjectedComponentIterState<AccessType, ProjectionType>{ ComponentArray, Projection };
	}

	AccessType* Resolve(const FEntityAllocation* Allocation) const
	{
		const FComponentHeader& Header = Allocation->GetComponentHeaderChecked(this->ComponentType);
		return reinterpret_cast<AccessType*>(Header.Components);
	}
};




template<typename T>
struct TWrite : FWrite
{
	using Type        = T;
	using AccessType  = T;

	TWrite(FComponentTypeID InComponentTypeID)
		: FWrite{ InComponentTypeID }
	{}

	TComponentIterState<AccessType> CreateIterState(const FEntityAllocation* Allocation) const
	{
		AccessType* ComponentArray = Resolve(Allocation);
		return TComponentIterState<AccessType>{ ComponentArray };
	}

	AccessType* Resolve(const FEntityAllocation* Allocation) const
	{
		const FComponentHeader& Header = Allocation->GetComponentHeaderChecked(this->ComponentType);
		return reinterpret_cast<AccessType*>(Header.Components);
	}

	TArrayView<AccessType> ResolveAsArray(const FEntityAllocation* Allocation) const
	{
		AccessType* Resolved = Resolve(Allocation);
		return MakeArrayView(Resolved, Allocation->Num());
	}
};




template<typename T>
struct TReadOptional : FReadOptional
{
	using Type        = T;
	using AccessType  = const T;

	TReadOptional(FComponentTypeID InComponentTypeID)
		: FReadOptional{InComponentTypeID}
	{}

	TOptionalIterState<AccessType> CreateIterState(const FEntityAllocation* Allocation) const
	{
		AccessType* ComponentArray = Resolve(Allocation);
		return TOptionalIterState<AccessType>{ ComponentArray };
	}

	AccessType* Resolve(const FEntityAllocation* Allocation) const
	{
		const FComponentHeader* Header = Allocation->FindComponentHeader(this->ComponentType);
		if (Header)
		{
			return reinterpret_cast<AccessType*>(Header->Components);
		}
		return nullptr;
	}

	TArrayView<AccessType> ResolveAsArray(const FEntityAllocation* Allocation) const
	{
		AccessType* Resolved = Resolve(Allocation);
		return MakeArrayView(Resolved, Resolved ? Allocation->Num() : 0);
	}
};




template<typename T>
struct TWriteOptional : FWriteOptional
{
	using Type        = T;
	using AccessType  = T;

	TWriteOptional(FComponentTypeID InComponentTypeID)
		: FWriteOptional{ InComponentTypeID }
	{}

	TOptionalIterState<AccessType> CreateIterState(const FEntityAllocation* Allocation) const
	{
		AccessType* ComponentArray = Resolve(Allocation);
		return TOptionalIterState<AccessType>{ ComponentArray };
	}

	AccessType* Resolve(const FEntityAllocation* Allocation) const
	{
		const FComponentHeader* Header = Allocation->FindComponentHeader(this->ComponentType);
		if (Header)
		{
			return reinterpret_cast<AccessType*>(Header->Components);
		}
		return nullptr;
	}

	TArrayView<AccessType> ResolveAsArray(const FEntityAllocation* Allocation) const
	{
		AccessType* Resolved = Resolve(Allocation);
		return MakeArrayView(Resolved, Resolved ? Allocation->Num() : 0);
	}
};




template<typename... T>
struct TReadOneOf
{
	enum { SupportsDirectEntityIteration = false };

	using AccessType  = TTuple<const T*...>;

	TReadOneOf(TComponentTypeID<T>... InComponentTypeIDs)
		: ComponentTypes{ InComponentTypeIDs... }
	{}

	void CreateIterState(...) const
	{
		static_assert(!TAnd<TIsSame<T, T>...>::Value, "PerEntity iteration is not supported for ReadOneOf accessors. Must use PerAllocation iterators and resolve the tuple manually.");
	}

	AccessType Resolve(const FEntityAllocation* Allocation) const
	{
		auto ResolveImpl = [Allocation](const auto& InRead)
		{
			return InRead.Resolve(Allocation);
		};

		return TransformTuple(ComponentTypes, ResolveImpl);
	}

	TTuple<TArrayView<const T*>...> ResolveAsArrays(const FEntityAllocation* Allocation) const
	{
		const int32 AllocationSize = Allocation->Num();
		AccessType Resolved = Resolve(Allocation);

		auto ResolveImpl = [AllocationSize](const auto& InRead)
		{
			return MakeArrayView(InRead, InRead ? AllocationSize : 0);
		};
		return TransformTuple(Resolved, ResolveImpl);
	}

	void ResolveAsArrays(const FEntityAllocation* Allocation, TArrayView<const T*>*... OutArray) const
	{
		const int32 AllocationSize = Allocation->Num();
		TTuple<TArrayView<const T> *...> OutArrayTuple(MakeTuple(OutArray...));
		AccessType Resolved = Resolve(Allocation);

		auto ResolveImpl = [AllocationSize](const auto& InRead, auto* OutWrite)
		{
			*OutWrite = MakeArrayView(InRead, InRead ? AllocationSize : 0);
		};
		VisitTupleElements(ResolveImpl, Resolved, OutArrayTuple);
	}

	TTuple< TReadOptional<T>... > ComponentTypes;
};




template<typename... T>
struct TReadOneOrMoreOf
{
	enum { SupportsDirectEntityIteration = false };

	using AccessType = TTuple<const T *...>;

	TReadOneOrMoreOf(TComponentTypeID<T>... InComponentTypeIDs)
		: ComponentTypes{ InComponentTypeIDs... }
	{}

	void CreateIterState(...) const
	{
		static_assert(!TAnd<TIsSame<T, T>...>::Value, "PerEntity iteration is not supported for ReadOneOf accessors. Must use PerAllocation iterators and resolve the tuple manually.");
	}

	AccessType Resolve(const FEntityAllocation* Allocation) const
	{
		auto ResolveImpl = [Allocation](const auto& InRead)
		{
			return InRead.Resolve(Allocation);
		};

		return TransformTuple(ComponentTypes, ResolveImpl);
	}

	TTuple<TArrayView<const T>...> ResolveAsArrays(const FEntityAllocation* Allocation) const
	{
		const int32 AllocationSize = Allocation->Num();
		AccessType Resolved = Resolve(Allocation);

		auto ResolveImpl = [AllocationSize](const auto& InRead)
		{
			return MakeArrayView(InRead, InRead ? AllocationSize : 0);
		};
		return TransformTuple(Resolved, ResolveImpl);
	}

	void ResolveAsArrays(const FEntityAllocation* Allocation, TArrayView<const T>*... OutArray) const
	{
		const int32 AllocationSize = Allocation->Num();
		TTuple<TArrayView<const T>*...> OutArrayTuple(MakeTuple(OutArray...));
		AccessType Resolved = Resolve(Allocation);

		auto ResolveImpl = [AllocationSize](const auto& InRead, auto* OutWrite)
		{
			*OutWrite = MakeArrayView(InRead, InRead ? AllocationSize : 0);
		};
		VisitTupleElements(ResolveImpl, Resolved, OutArrayTuple);
	}

	TTuple< TReadOptional<T>... > ComponentTypes;
};




inline void AddAccessorToFilter(const FReadEntityIDs*, FEntityComponentFilter* OutFilter)
{
}
inline void AddAccessorToFilter(const FComponentAccessor* In, FEntityComponentFilter* OutFilter)
{
	check(In->ComponentType);
	OutFilter->All({ In->ComponentType });
}
inline void AddAccessorToFilter(const FOptionalComponentAccessor* In, FEntityComponentFilter* OutFilter)
{
}
template<typename... T>
void AddAccessorToFilter(const TReadOneOf<T...>* In, FEntityComponentFilter* OutFilter)
{
	FComponentMask Mask;
	VisitTupleElements([&Mask](FReadOptional Composite){ if (Composite.ComponentType) { Mask.Set(Composite.ComponentType); } }, In->ComponentTypes);

	check(Mask.NumComponents() != 0);
	OutFilter->Complex(Mask, EComplexFilterMode::OneOf);
}
template<typename... T>
void AddAccessorToFilter(const TReadOneOrMoreOf<T...>* In, FEntityComponentFilter* OutFilter)
{
	FComponentMask Mask;
	VisitTupleElements([&Mask](FReadOptional Composite) { if (Composite.ComponentType) { Mask.Set(Composite.ComponentType); } }, In->ComponentTypes);

	check(Mask.NumComponents() != 0);
	OutFilter->Complex(Mask, EComplexFilterMode::OneOrMoreOf);
}


inline void PopulatePrerequisites(const FReadEntityIDs*, const FSystemTaskPrerequisites& InPrerequisites, FGraphEventArray* OutGatheredPrereqs)
{
}
inline void PopulatePrerequisites(const FComponentAccessor* In, const FSystemTaskPrerequisites& InPrerequisites, FGraphEventArray* OutGatheredPrereqs)
{
	check(In->ComponentType);
	InPrerequisites.FilterByComponent(*OutGatheredPrereqs, In->ComponentType);
}
inline void PopulatePrerequisites(const FOptionalComponentAccessor* In, const FSystemTaskPrerequisites& InPrerequisites, FGraphEventArray* OutGatheredPrereqs)
{
	if (In->ComponentType)
	{
		check(In->ComponentType);
		InPrerequisites.FilterByComponent(*OutGatheredPrereqs, In->ComponentType);
	}
}
template<typename... T>
void PopulatePrerequisites(const TReadOneOf<T...>* In, const FSystemTaskPrerequisites& InPrerequisites, FGraphEventArray* OutGatheredPrereqs)
{
	VisitTupleElements(
		[&InPrerequisites, OutGatheredPrereqs](FReadOptional Composite)
		{
			PopulatePrerequisites(&Composite, InPrerequisites, OutGatheredPrereqs);
		}
	, In->ComponentTypes);
}
template<typename... T>
void PopulatePrerequisites(const TReadOneOrMoreOf<T...>* In, const FSystemTaskPrerequisites& InPrerequisites, FGraphEventArray* OutGatheredPrereqs)
{
	VisitTupleElements(
		[&InPrerequisites, OutGatheredPrereqs](FReadOptional Composite)
		{
			PopulatePrerequisites(&Composite, InPrerequisites, OutGatheredPrereqs);
		}
	, In->ComponentTypes);
}



inline void PopulateSubsequents(const FWrite* In, const FGraphEventRef& InEvent, FSystemSubsequentTasks& OutSubsequents)
{
	check(In->ComponentType);
	OutSubsequents.AddComponentTask(In->ComponentType, InEvent);
}
inline void PopulateSubsequents(const FWriteOptional* In, const FGraphEventRef& InEvent, FSystemSubsequentTasks& OutSubsequents)
{
	if (In->ComponentType)
	{
		OutSubsequents.AddComponentTask(In->ComponentType, InEvent);
	}
}
inline void PopulateSubsequents(const void* In, const FGraphEventRef& InEvent, FSystemSubsequentTasks& OutSubsequents)
{
}



inline void LockHeader(const FReadEntityIDs* In, FEntityAllocation* Allocation)
{
}
inline void LockHeader(const FRead* In, FEntityAllocation* Allocation)
{
	Allocation->GetComponentHeaderChecked(In->ComponentType).ReadWriteLock.ReadLock();
}
inline void LockHeader(const FReadOptional* In, FEntityAllocation* Allocation)
{
	if (const FComponentHeader* Header = Allocation->FindComponentHeader(In->ComponentType))
	{
		Header->ReadWriteLock.ReadLock();
	}
}
inline void LockHeader(const FWrite* In, FEntityAllocation* Allocation)
{
	FComponentHeader& Header = Allocation->GetComponentHeaderChecked(In->ComponentType);
	Header.ReadWriteLock.WriteLock();
}
inline void LockHeader(const FWriteOptional* In, FEntityAllocation* Allocation)
{
	if (FComponentHeader* Header = Allocation->FindComponentHeader(In->ComponentType))
	{
		Header->ReadWriteLock.WriteLock();
	}
}
template<typename... T>
void LockHeader(const TReadOneOf<T...>* In, FEntityAllocation* Allocation)
{
	VisitTupleElements([Allocation](FReadOptional It){ LockHeader(&It, Allocation); }, In->ComponentTypes);
}
template<typename... T>
void LockHeader(const TReadOneOrMoreOf<T...>* In, FEntityAllocation* Allocation)
{
	VisitTupleElements([Allocation](FReadOptional It) { LockHeader(&It, Allocation); }, In->ComponentTypes);
}


inline void UnlockHeader(const FReadEntityIDs* In, FEntityAllocation* Allocation, uint64 InSystemSerial)
{
}
inline void UnlockHeader(const FRead* In, FEntityAllocation* Allocation, uint64 InSystemSerial)
{
	Allocation->GetComponentHeaderChecked(In->ComponentType).ReadWriteLock.ReadUnlock();
}
inline void UnlockHeader(const FReadOptional* In, FEntityAllocation* Allocation, uint64 InSystemSerial)
{
	if (FComponentHeader* Header = Allocation->FindComponentHeader(In->ComponentType))
	{
		Header->ReadWriteLock.ReadUnlock();
	}
}
inline void UnlockHeader(const FWrite* In, FEntityAllocation* Allocation, uint64 InSystemSerial)
{
	FComponentHeader& Header = Allocation->GetComponentHeaderChecked(In->ComponentType);
	Header.PostWriteComponents(InSystemSerial);
	Header.ReadWriteLock.WriteUnlock();
}
inline void UnlockHeader(const FWriteOptional* In, FEntityAllocation* Allocation, uint64 InSystemSerial)
{
	if (FComponentHeader* Header = Allocation->FindComponentHeader(In->ComponentType))
	{
		Header->PostWriteComponents(InSystemSerial);
		Header->ReadWriteLock.WriteUnlock();
	}
}
template<typename... T>
void UnlockHeader(const TReadOneOf<T...>* In, FEntityAllocation* Allocation, uint64 InSystemSerial)
{
	VisitTupleElements([Allocation, InSystemSerial](FReadOptional It){ UnlockHeader(&It, Allocation, InSystemSerial); }, In->ComponentTypes);
}
template<typename... T>
void UnlockHeader(const TReadOneOrMoreOf<T...>* In, FEntityAllocation* Allocation, uint64 InSystemSerial)
{
	VisitTupleElements([Allocation, InSystemSerial](FReadOptional It) { UnlockHeader(&It, Allocation, InSystemSerial); }, In->ComponentTypes);
}


inline bool HasBeenWrittenToSince(const FReadEntityIDs* In, FEntityAllocation* Allocation, uint64 InSystemSerial)
{
	return Allocation->HasStructureChangedSince(InSystemSerial);
}
inline bool HasBeenWrittenToSince(const FComponentAccessor* In, FEntityAllocation* Allocation, uint64 InSystemSerial)
{
	return Allocation->GetComponentHeaderChecked(In->ComponentType).HasBeenWrittenToSince(InSystemSerial);
}
inline bool HasBeenWrittenToSince(const FOptionalComponentAccessor* In, FEntityAllocation* Allocation, uint64 InSystemSerial)
{
	if (FComponentHeader* Header = Allocation->FindComponentHeader(In->ComponentType))
	{
		return Header->HasBeenWrittenToSince(InSystemSerial);
	}
	return false;
}
template<typename... T>
bool HasBeenWrittenToSince(const TReadOneOf<T...>* In, FEntityAllocation* Allocation, uint64 InSystemSerial)
{
	bool bAnyWrittenTo = false;
	VisitTupleElements([Allocation, &bAnyWrittenTo, InSystemSerial](FReadOptional It){ bAnyWrittenTo |= HasBeenWrittenToSince(&It, Allocation, InSystemSerial); }, In->ComponentTypes);
	return bAnyWrittenTo;
}
template<typename... T>
bool HasBeenWrittenToSince(const TReadOneOrMoreOf<T...>* In, FEntityAllocation* Allocation, uint64 InSystemSerial)
{
	bool bAnyWrittenTo = false;
	VisitTupleElements([Allocation, &bAnyWrittenTo, InSystemSerial](FReadOptional It) { bAnyWrittenTo |= HasBeenWrittenToSince(&It, Allocation, InSystemSerial); }, In->ComponentTypes);
	return bAnyWrittenTo;
}



inline bool IsAccessorValid(const FReadEntityIDs*)
{
	return true;
}
inline bool IsAccessorValid(const FComponentAccessor* In)
{
	return In->ComponentType != FComponentTypeID::Invalid();
}
inline bool IsAccessorValid(const FOptionalComponentAccessor* In)
{
	return true;
}
template<typename... T>
inline bool IsAccessorValid(const TReadOneOf<T...>* In)
{
	bool bValid = false;
	VisitTupleElements([&bValid](FReadOptional It){ bValid |= IsAccessorValid(&It); }, In->ComponentTypes);
	return bValid;
}
template<typename... T>
inline bool IsAccessorValid(const TReadOneOrMoreOf<T...>* In)
{
	bool bValid = false;
	VisitTupleElements([&bValid](FReadOptional It) { bValid |= IsAccessorValid(&It); }, In->ComponentTypes);
	return bValid;
}

#if UE_MOVIESCENE_ENTITY_DEBUG

	MOVIESCENE_API void AccessorToString(const FRead* In, FEntityManager* EntityManager, FString& OutString);
	MOVIESCENE_API void AccessorToString(const FWrite* In, FEntityManager* EntityManager, FString& OutString);
	MOVIESCENE_API void AccessorToString(const FReadOptional* In, FEntityManager* EntityManager, FString& OutString);
	MOVIESCENE_API void AccessorToString(const FWriteOptional* In, FEntityManager* EntityManager, FString& OutString);
	MOVIESCENE_API void AccessorToString(const FReadEntityIDs*, FEntityManager* EntityManager, FString& OutString);
	MOVIESCENE_API void OneOfAccessorToString(const FReadOptional* In, FEntityManager* EntityManager, FString& OutString);

	template<typename... T>
	void AccessorToString(const TReadOneOf<T...>* In, FEntityManager* EntityManager, FString& OutString)
	{
		TArray<FString> Strings;
		VisitTupleElements([&Strings, EntityManager](FReadOptional ReadOptional)
		{
			OneOfAccessorToString(&ReadOptional, EntityManager, Strings.Emplace_GetRef());
		}, In->ComponentTypes);

		OutString += FString::Printf(TEXT("\n\tRead One Of: [ %s ]"), *FString::Join(Strings, TEXT(",")));
	}

	template<typename... T>
	void AccessorToString(const TReadOneOrMoreOf<T...>* In, FEntityManager* EntityManager, FString& OutString)
	{
		TArray<FString> Strings;
		VisitTupleElements([&Strings, EntityManager](FReadOptional ReadOptional)
			{
				OneOfAccessorToString(&ReadOptional, EntityManager, Strings.Emplace_GetRef());
			}, In->ComponentTypes);

		OutString += FString::Printf(TEXT("\n\tRead One Or More Of: [ %s ]"), *FString::Join(Strings, TEXT(",")));
	}

#endif // UE_MOVIESCENE_ENTITY_DEBUG

template<typename T>
struct TSupportsDirectEntityIteration
{
	enum { Value = T::SupportsDirectEntityIteration };
};

} // namespace MovieScene
} // namespace UE