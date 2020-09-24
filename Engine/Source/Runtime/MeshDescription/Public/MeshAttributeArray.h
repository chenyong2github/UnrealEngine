// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "MeshTypes.h"
#include "MeshElementRemappings.h"
#include "Misc/TVariant.h"
#include "Templates/CopyQualifiersFromTo.h"
#include "UObject/EditorObjectVersion.h"
#include "UObject/ReleaseObjectVersion.h"
#include "UObject/UE5MainStreamObjectVersion.h"
#include "Templates/IsArray.h"


/**
 * List of attribute types which are supported.
 *
 * IMPORTANT NOTE: Do not reorder or remove any type from this tuple, or serialization will fail.
 * Types may be added at the end of this list as required.
 */
using AttributeTypes = TTuple
<
	FVector4,
	FVector,
	FVector2D,
	float,
	int32,
	bool,
	FName
>;


/**
 * Helper template which generates a TVariant of all supported attribute types.
 */
template <typename Tuple> struct TVariantFromTuple;
template <typename... Ts> struct TVariantFromTuple<TTuple<Ts...>> { using Type = TVariant<FEmptyVariantState, Ts...>; };


/**
 * Helper template which, given an array type, can break into its element type and number of elements.
 */
template <typename T> struct TBreakArray;
template <typename T, SIZE_T N> struct TBreakArray<T[N]> : TIntegralConstant<uint32, N> { using Type = T; };


/**
 * Class which implements a function jump table to be automatically generated at compile time.
 * This is used by TAttributesSet to provide O(1) dispatch by attribute type at runtime.
 */
template <typename FnType, uint32 Size>
struct TJumpTable
{
	template <typename... T>
	explicit constexpr TJumpTable( T... Ts ) : Fns{ Ts... } {}

	FnType* Fns[Size];
};


/**
 * Traits class to specify which attribute types can be bulk serialized.
 */
template <typename T> struct TIsBulkSerializable { static const bool Value = true; };
template <> struct TIsBulkSerializable<FName> { static const bool Value = false; };


/**
 * This defines the container used to hold mesh element attributes of a particular name and index.
 * It is a simple TArray, so that all attributes are packed contiguously for each element ID.
 *
 * Note that the container may grow arbitrarily as new elements are inserted, but it will never be
 * shrunk as elements are removed. The only operations that will shrink the container are Initialize() and Remap().
 */
template <typename AttributeType>
class TMeshAttributeArrayBase
{
public:
	explicit TMeshAttributeArrayBase(uint32 InExtent = 1)
		: Extent(InExtent)
	{}

	/** Custom serialization for TMeshAttributeArrayBase. */
	template <typename T> friend typename TEnableIf<!TIsBulkSerializable<T>::Value, FArchive>::Type& operator<<( FArchive& Ar, TMeshAttributeArrayBase<T>& Array );
	template <typename T> friend typename TEnableIf<TIsBulkSerializable<T>::Value, FArchive>::Type& operator<<( FArchive& Ar, TMeshAttributeArrayBase<T>& Array );

	/** Return size of container */
	FORCEINLINE int32 Num() const { return Container.Num() / Extent; }

	/** Return base of data */
	UE_DEPRECATED(4.25, "This method will be removed.")
	FORCEINLINE const AttributeType* GetData() const { return Container.GetData(); }

	/** Initializes the array to the given size with the default value */
	FORCEINLINE void Initialize(const int32 ElementCount, const AttributeType& Default)
	{
		Container.Reset(ElementCount * Extent);
		if (ElementCount > 0)
		{
			Insert(ElementCount - 1, Default);
		}
	}

	void SetNum(const int32 ElementCount, const AttributeType& Default)
	{
		if (Container.Num() >= ElementCount)
		{
			Container.SetNum(ElementCount);
		}
		else
		{
			Initialize(ElementCount, Default);
		}
	}

	uint32 GetHash(uint32 Crc = 0) const
	{
		return FCrc::MemCrc32(Container.GetData(), Container.Num() * sizeof(AttributeType), Crc);
	}

	/** Expands the array if necessary so that the passed element index is valid. Newly created elements will be assigned the default value. */
	void Insert(const int32 Index, const AttributeType& Default);

	/** Fills the index with the default value */
	void SetToDefault(const int32 Index, const AttributeType& Default)
	{
		for (uint32 I = 0; I < Extent; ++I)
		{
			Container[Index * Extent + I] = Default;
		}
	}

	/** Remaps elements according to the passed remapping table */
	void Remap(const TSparseArray<int32>& IndexRemap, const AttributeType& Default);

	/** Element accessors */
	UE_DEPRECATED(4.25, "Please use GetElementBase() instead.")
	FORCEINLINE const AttributeType& operator[](const int32 Index) const { return Container[Index]; }

	UE_DEPRECATED(4.25, "Please use GetElementBase() instead.")
	FORCEINLINE AttributeType& operator[](const int32 Index) { return Container[Index]; }

	FORCEINLINE const AttributeType* GetElementBase(const int32 Index) const { return &Container[Index * Extent]; }
	FORCEINLINE AttributeType* GetElementBase(const int32 Index) { return &Container[Index * Extent]; }

	FORCEINLINE uint32 GetExtent() const { return Extent; }

protected:
	/** The actual container, represented by a regular array */
	TArray<AttributeType> Container;

	/** Number of array elements in this attribute type */
	uint32 Extent;
};


template <typename AttributeType>
void TMeshAttributeArrayBase<AttributeType>::Insert(const int32 Index, const AttributeType& Default)
{
	int32 EndIndex = (Index + 1) * Extent;
	if (EndIndex > Container.Num())
	{
		// If the index is off the end of the container, add as many elements as required to make it the last valid index.
		int32 StartIndex = Container.AddUninitialized(EndIndex - Container.Num());
		AttributeType* Data = Container.GetData() + StartIndex;

		// Construct added elements with the default value passed in

		while (StartIndex < EndIndex)
		{
			new(Data) AttributeType(Default);
			StartIndex++;
			Data++;
		}
	}
}

template <typename AttributeType>
void TMeshAttributeArrayBase<AttributeType>::Remap(const TSparseArray<int32>& IndexRemap, const AttributeType& Default)
{
	TMeshAttributeArrayBase NewAttributeArray(Extent);

	for (typename TSparseArray<int32>::TConstIterator It(IndexRemap); It; ++It)
	{
		const int32 OldElementIndex = It.GetIndex();
		const int32 NewElementIndex = IndexRemap[OldElementIndex];

		NewAttributeArray.Insert(NewElementIndex, Default);
		AttributeType* DestElementBase = NewAttributeArray.GetElementBase(NewElementIndex);
		AttributeType* SrcElementBase = GetElementBase(OldElementIndex);
		for (uint32 Index = 0; Index < Extent; ++Index)
		{
			DestElementBase[Index] = MoveTemp(SrcElementBase[Index]);
		}
	}

	Container = MoveTemp(NewAttributeArray.Container);
}

template <typename T>
inline typename TEnableIf<!TIsBulkSerializable<T>::Value, FArchive>::Type& operator<<( FArchive& Ar, TMeshAttributeArrayBase<T>& Array )
{
	if (Ar.IsLoading() &&
		Ar.CustomVer(FReleaseObjectVersion::GUID) != FReleaseObjectVersion::MeshDescriptionNewFormat &&
		Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::MeshDescriptionNewFormat)
	{
		Array.Extent = 1;
	}
	else
	{
		Ar << Array.Extent;
	}

	// Serialize types which aren't bulk serializable, which need to be serialized element-by-element
	Ar << Array.Container;
	return Ar;
}

template <typename T>
inline typename TEnableIf<TIsBulkSerializable<T>::Value, FArchive>::Type& operator<<( FArchive& Ar, TMeshAttributeArrayBase<T>& Array )
{
	if (Ar.IsLoading() &&
		Ar.CustomVer(FReleaseObjectVersion::GUID) != FReleaseObjectVersion::MeshDescriptionNewFormat &&
		Ar.CustomVer(FUE5MainStreamObjectVersion::GUID) < FUE5MainStreamObjectVersion::MeshDescriptionNewFormat)
	{
		Array.Extent = 1;
	}
	else
	{
		Ar << Array.Extent;
	}

	if( Ar.IsLoading() && Ar.CustomVer( FReleaseObjectVersion::GUID ) < FReleaseObjectVersion::MeshDescriptionNewSerialization )
	{
		// Legacy path for old format attribute arrays. BulkSerialize has a different format from regular serialization.
		Ar << Array.Container;
	}
	else
	{
		// Serialize types which are bulk serializable, i.e. which can be memcpy'd in bulk
		Array.Container.BulkSerialize( Ar );
	}

	return Ar;
}


/**
 * Flags specifying properties of an attribute
 */
enum class EMeshAttributeFlags : uint32
{
	None				= 0,
	Lerpable			= (1 << 0),		/** Attribute can be automatically lerped according to the value of 2 or 3 other attributes */
	AutoGenerated		= (1 << 1),		/** Attribute is auto-generated by importer or editable mesh, rather than representing an imported property */	
	Mergeable			= (1 << 2),		/** If all vertices' attributes are mergeable, and of near-equal value, they can be welded */
	Transient			= (1 << 3),		/** Attribute is not serialized */
	IndexReference		= (1 << 4),		/** Attribute is a reference to another element index */
	Mandatory			= (1 << 5),		/** Attribute is required in the mesh description */
};

ENUM_CLASS_FLAGS(EMeshAttributeFlags);


/**
 * This is the base class for an attribute array set.
 * An attribute array set is a container which holds attribute arrays, one per attribute index.
 * Many attributes have only one index, while others (such as texture coordinates) may want to define many.
 *
 * All attribute array set instances will be of derived types; this type exists for polymorphism purposes,
 * so that they can be managed by a generic TUniquePtr<FMeshAttributeArraySetBase>.
 *
 * In general, we avoid accessing them via virtual dispatch by insisting that their type be passed as
 * a template parameter in the accessor. This can be checked against the Type field to ensure that we are
 * accessing an instance by its correct type.
 */
class FMeshAttributeArraySetBase
{
public:
	/** Constructor */
	FORCEINLINE FMeshAttributeArraySetBase(const uint32 InType, const EMeshAttributeFlags InFlags, const int32 InNumberOfElements, const uint32 InExtent)
		: Type(InType),
		  Extent(InExtent),
		  NumElements(InNumberOfElements),
		  Flags(InFlags)
	{}

	/** Virtual interface */
	virtual ~FMeshAttributeArraySetBase() = default;
	virtual TUniquePtr<FMeshAttributeArraySetBase> Clone() const = 0;
	virtual void Insert(const int32 Index) = 0;
	virtual void Remove(const int32 Index) = 0;
	virtual void Initialize(const int32 Count) = 0;
	virtual void SetNumElements(const int32 Count) = 0;
	virtual uint32 GetHash() const = 0;
	virtual void Serialize(FArchive& Ar) = 0;
	virtual void Remap(const TSparseArray<int32>& IndexRemap) = 0;

//	UE_DEPRECATED(4.26, "Please use GetNumChannels().")
	virtual int32 GetNumIndices() const = 0;
//	UE_DEPRECATED(4.26, "Please use SetNumChannels().")
	virtual void SetNumIndices(const int32 NumIndices) = 0;
//	UE_DEPRECATED(4.26, "Please use InsertChannel().")
	virtual void InsertIndex(const int32 Index) = 0;
//	UE_DEPRECATED(4.26, "Please use RemoveChannel().")
	virtual void RemoveIndex(const int32 Index) = 0;

	virtual int32 GetNumChannels() const = 0;
	virtual void SetNumChannels(const int32 NumChannels) = 0;
	virtual void InsertChannel(const int32 Index) = 0;
	virtual void RemoveChannel(const int32 Index) = 0;

	/** Determine whether this attribute array set is of the given type */
	template <typename T>
	FORCEINLINE bool HasType() const
	{
		return TTupleIndex<T, AttributeTypes>::Value == Type;
	}

	/** Get the type index of this attribute array set */
	FORCEINLINE uint32 GetType() const { return Type; }

	/** Get the type extent of this attribute array set */
	FORCEINLINE uint32 GetExtent() const { return Extent; }

	/** Get the flags for this attribute array set */
	FORCEINLINE EMeshAttributeFlags GetFlags() const { return Flags; }

	/** Set the flags for this attribute array set */
	FORCEINLINE void SetFlags(const EMeshAttributeFlags InFlags) { Flags = InFlags; }

	/** Return number of elements each attribute index has */
	FORCEINLINE int32 GetNumElements() const { return NumElements; }

protected:
	/** Type of the attribute array (based on the tuple element index from AttributeTypes) */
	uint32 Type;

	/** Extent of the type, i.e. the number of array elements it consists of */
	uint32 Extent;

	/** Number of elements in each index */
	int32 NumElements;

	/** Implementation-defined attribute name flags */
	EMeshAttributeFlags Flags;
};


/**
 * This is a type-specific attribute array, which is actually instanced in the attribute set.
 */
template <typename AttributeType>
class TMeshAttributeArraySet : public FMeshAttributeArraySetBase
{
	using Super = FMeshAttributeArraySetBase;

public:
	/** Constructors */
	FORCEINLINE explicit TMeshAttributeArraySet(const int32 Extent = 1)
		: Super(TTupleIndex<AttributeType, AttributeTypes>::Value, EMeshAttributeFlags::None, 0, Extent)
	{}

	FORCEINLINE explicit TMeshAttributeArraySet(const int32 NumberOfChannels, const AttributeType& InDefaultValue, const EMeshAttributeFlags InFlags, const int32 InNumberOfElements, const uint32 Extent)
		: Super(TTupleIndex<AttributeType, AttributeTypes>::Value, InFlags, InNumberOfElements, Extent),
		  DefaultValue(InDefaultValue)
	{
		SetNumChannels(NumberOfChannels);
	}

	/** Creates a copy of itself and returns a TUniquePtr to it */
	virtual TUniquePtr<FMeshAttributeArraySetBase> Clone() const override
	{
		return MakeUnique<TMeshAttributeArraySet>(*this);
	}

	/** Insert the element at the given index */
	virtual void Insert(const int32 Index) override
	{
		for (TMeshAttributeArrayBase<AttributeType>& ArrayForChannel : ArrayForChannels)
		{
			ArrayForChannel.Insert(Index, DefaultValue);
		}

		NumElements = FMath::Max(NumElements, Index + 1);
	}

	/** Remove the element at the given index, replacing it with a default value */
	virtual void Remove(const int32 Index) override
	{
		for (TMeshAttributeArrayBase<AttributeType>& ArrayForChannel : ArrayForChannels)
		{
			ArrayForChannel.SetToDefault(Index, DefaultValue);
		}
	}

	/** Sets the number of elements to the exact number provided, and initializes them to the default value */
	virtual void Initialize(const int32 Count) override
	{
		NumElements = Count;
		for (TMeshAttributeArrayBase<AttributeType>& ArrayForChannel : ArrayForChannels)
		{
			ArrayForChannel.Initialize(Count, DefaultValue);
		}
	}

	/** Sets the number of elements to the exact number provided, preserving existing elements if the number is bigger */
	virtual void SetNumElements(const int32 Count) override
	{
		NumElements = Count;
		for (TMeshAttributeArrayBase<AttributeType>& ArrayForChannel : ArrayForChannels)
		{
			ArrayForChannel.SetNum(Count, DefaultValue);
		}
	}

	virtual uint32 GetHash() const override
	{
		uint32 CrcResult = 0;
		for (const TMeshAttributeArrayBase<AttributeType>& ArrayForChannel : ArrayForChannels)
		{
			CrcResult = ArrayForChannel.GetHash(CrcResult);
		}
		return CrcResult;
	}

	/** Polymorphic serialization */
	virtual void Serialize(FArchive& Ar) override
	{
		Ar << (*this);
	}

	/** Performs an element index remap according to the passed array */
	virtual void Remap(const TSparseArray<int32>& IndexRemap) override
	{
		for (TMeshAttributeArrayBase<AttributeType>& ArrayForChannel : ArrayForChannels)
		{
			ArrayForChannel.Remap(IndexRemap, DefaultValue);
			NumElements = ArrayForChannel.Num();
		}
	}

	UE_DEPRECATED(4.26, "Please use GetNumChannels().")
	virtual inline int32 GetNumIndices() const override { return GetNumChannels(); }

	/** Return number of channels this attribute has */
	virtual inline int32 GetNumChannels() const override { return ArrayForChannels.Num(); }

	UE_DEPRECATED(4.26, "Please use SetNumChannels().")
	virtual void SetNumIndices(const int32 NumIndices) override { SetNumChannels(NumIndices); }

	/** Sets number of channels this attribute has */
	virtual void SetNumChannels(const int32 NumChannels) override
	{
		if (NumChannels < ArrayForChannels.Num())
		{
			ArrayForChannels.SetNum(NumChannels);
			return;
		}

		while (ArrayForChannels.Num() < NumChannels)
		{
			TMeshAttributeArrayBase<AttributeType>& Array = ArrayForChannels.Emplace_GetRef(Extent);
			Array.Initialize(NumElements, DefaultValue);
		}
	}

	UE_DEPRECATED(4.26, "Please use InsertChannel().")
	virtual void InsertIndex(const int32 Index) override
	{
		InsertChannel(Index);
	}

	/** Insert a new attribute channel */
	virtual void InsertChannel(const int32 Index) override
	{
		TMeshAttributeArrayBase<AttributeType>& Array = ArrayForChannels.EmplaceAt_GetRef(Index, Extent);
		Array.Initialize(NumElements, DefaultValue);
	}

	UE_DEPRECATED(4.26, "Please use RemoveChannel().")
	virtual void RemoveIndex(const int32 Index) override
	{
		RemoveChannel(Index);
	}

	/** Remove the channel at the given index */
	virtual void RemoveChannel(const int32 Index) override
	{
		ArrayForChannels.RemoveAt(Index);
	}


	UE_DEPRECATED(4.26, "Please use GetArrayForChannel().")
	FORCEINLINE const TMeshAttributeArrayBase<AttributeType>& GetArrayForIndex( const int32 Index ) const { return ArrayForChannels[ Index ]; }
	UE_DEPRECATED(4.26, "Please use GetArrayForChannel().")
	FORCEINLINE TMeshAttributeArrayBase<AttributeType>& GetArrayForIndex( const int32 Index ) { return ArrayForChannels[ Index ]; }

	/** Return the TMeshAttributeArrayBase corresponding to the given attribute channel */
	FORCEINLINE const TMeshAttributeArrayBase<AttributeType>& GetArrayForChannel( const int32 Index ) const { return ArrayForChannels[ Index ]; }
	FORCEINLINE TMeshAttributeArrayBase<AttributeType>& GetArrayForChannel( const int32 Index ) { return ArrayForChannels[ Index ]; }

	/** Return default value for this attribute type */
	FORCEINLINE AttributeType GetDefaultValue() const { return DefaultValue; }

	/** Serializer */
	friend FArchive& operator<<(FArchive& Ar, TMeshAttributeArraySet& AttributeArraySet)
	{
		Ar << AttributeArraySet.NumElements;
		Ar << AttributeArraySet.ArrayForChannels;
		Ar << AttributeArraySet.DefaultValue;
		Ar << AttributeArraySet.Flags;

		return Ar;
	}

protected:
	/** An array of MeshAttributeArrays, one per channel */
	TArray<TMeshAttributeArrayBase<AttributeType>, TInlineAllocator<1>> ArrayForChannels;

	/** The default value for an attribute of this name */
	AttributeType DefaultValue;
};


template <typename> struct TIsArrayView { enum { Value = false }; };
template <typename T> struct TIsArrayView<TArrayView<T>> { enum { Value = true }; };


template <typename> struct TBreakArrayView;
template <typename T> struct TBreakArrayView<TArrayView<T>> { using Type = T; };


template <typename T, bool IsArray = TIsArrayView<T>::Value> struct TConstAttribute;
template <typename T> struct TConstAttribute<T, false> { using Type = const T; };
template <typename T> struct TConstAttribute<TArrayView<T>, true> { using Type = TArrayView<const T>; };

/**
 * This is the class used to access attribute values.
 * It is a proxy object to a TMeshAttributeArraySet<> and should be passed by value.
 * It is valid for as long as the owning FMeshDescription exists.
 */
template <typename ElementIDType, typename AttributeType, bool IsArray = TIsArrayView<AttributeType>::Value>
class TMeshAttributesRef;

template <typename ElementIDType, typename AttributeType, bool IsArray = TIsArrayView<AttributeType>::Value>
using TMeshAttributesConstRef = TMeshAttributesRef<ElementIDType, typename TConstAttribute<AttributeType>::Type, IsArray>;

template <typename AttributeType, bool IsArray = TIsArrayView<AttributeType>::Value>
using TMeshAttributesArray = TMeshAttributesRef<int32, AttributeType, IsArray>;

template <typename AttributeType, bool IsArray = TIsArrayView<AttributeType>::Value>
using TMeshAttributesConstArray = TMeshAttributesRef<int32, typename TConstAttribute<AttributeType>::Type, IsArray>;


template <typename ElementIDType, typename AttributeType>
class TMeshAttributesRef<ElementIDType, AttributeType, false>
{
	template <typename T, typename U, bool IsArray> friend class TMeshAttributesRef;

public:
	using Type = AttributeType;
	using ArrayType = typename TCopyQualifiersFromTo<AttributeType, TMeshAttributeArraySet<typename TRemoveCV<AttributeType>::Type>>::Type;

	/** Constructor taking a pointer to a TMeshAttributeArraySet */
	FORCEINLINE explicit TMeshAttributesRef(ArrayType* InArrayPtr = nullptr)
		: ArrayPtr(InArrayPtr)
	{}

	/** Implicitly construct a TMeshAttributesRef-to-const from a regular one */
	template <typename T = AttributeType, typename TEnableIf<TIsSame<T, const T>::Value, int>::Type = 0>
	FORCEINLINE TMeshAttributesRef(TMeshAttributesRef<ElementIDType, typename TRemoveCV<T>::Type, false> InRef)
		: ArrayPtr(InRef.ArrayPtr)
	{}

	/** Implicitly construct a TMeshAttributesRef from a TMeshAttributesArray **/
	template <typename T = ElementIDType, typename U = AttributeType, typename TEnableIf<!TIsSame<T, int32>::Value, int>::Type = 0>
	FORCEINLINE TMeshAttributesRef(TMeshAttributesRef<int32, U, false> InRef)
		: ArrayPtr(InRef.ArrayPtr)
	{}

	/** Implicitly construct a TMeshAttributesRef-to-const from a TMeshAttributesArray */
	template <typename T = ElementIDType, typename U = AttributeType, typename TEnableIf<TIsSame<U, const U>::Value, int>::Type = 0, typename TEnableIf<!TIsSame<T, int32>::Value, int>::Type = 0>
	FORCEINLINE TMeshAttributesRef(TMeshAttributesRef<int32, typename TRemoveCV<U>::Type, false> InRef)
		: ArrayPtr(InRef.ArrayPtr)
	{}

	/** Access elements from attribute channel 0 */
	template <typename T = ElementIDType, typename TEnableIf<TIsDerivedFrom<T, FElementID>::Value, int>::Type = 0>
	FORCEINLINE AttributeType& operator[](const ElementIDType ElementID) const
	{
		return ArrayPtr->GetArrayForChannel(0).GetElementBase(ElementID.GetValue())[0];
	}

	/** Get the element with the given ID from channel 0 */
	template <typename T = ElementIDType, typename TEnableIf<TIsDerivedFrom<T, FElementID>::Value, int>::Type = 0>
	FORCEINLINE AttributeType Get(const ElementIDType ElementID) const
	{
		return ArrayPtr->GetArrayForChannel(0).GetElementBase(ElementID.GetValue())[0];
	}

	/** Get the element with the given ID and channel */
	template <typename T = ElementIDType, typename TEnableIf<TIsDerivedFrom<T, FElementID>::Value, int>::Type = 0>
	FORCEINLINE AttributeType Get(const ElementIDType ElementID, const int32 Channel) const
	{
		return ArrayPtr->GetArrayForChannel(Channel).GetElementBase(ElementID.GetValue())[0];
	}

	FORCEINLINE AttributeType& operator[](int32 ElementIndex) const
	{
		return ArrayPtr->GetArrayForChannel(0).GetElementBase(ElementIndex)[0];
	}

	FORCEINLINE AttributeType Get(int32 ElementIndex) const
	{
		return ArrayPtr->GetArrayForChannel(0).GetElementBase(ElementIndex)[0];
	}

	FORCEINLINE AttributeType Get(int32 ElementIndex, const int32 Index) const
	{
		return ArrayPtr->GetArrayForChannel(Index).GetElementBase(ElementIndex)[0];
	}

	FORCEINLINE TArrayView<Type> GetRawArray(const int32 AttributeChannel = 0) const
	{
		if (ArrayPtr == nullptr)
		{
			return TArrayView<Type>();
		}

		Type* Element = ArrayPtr->GetArrayForChannel(AttributeChannel).GetElementBase(0);
		return TArrayView<Type>(Element, GetNumElements());
	}

	/** Return whether the reference is valid or not */
	FORCEINLINE bool IsValid() const { return (ArrayPtr != nullptr); }

	/** Return default value for this attribute type */
	FORCEINLINE Type GetDefaultValue() const { return ArrayPtr->GetDefaultValue(); }

	UE_DEPRECATED(4.26, "Please use GetNumChannels().")
	FORCEINLINE int32 GetNumIndices() const
	{
		return ArrayPtr->ArrayType::GetNumChannels();	// note: override virtual dispatch
	}

	/** Return number of indices this attribute has */
	FORCEINLINE int32 GetNumChannels() const
	{
		return ArrayPtr->ArrayType::GetNumChannels();	// note: override virtual dispatch
	}

	/** Get the number of elements in this attribute array */
	FORCEINLINE int32 GetNumElements() const
	{
		return ArrayPtr->GetNumElements();
	}

	/** Get the flags for this attribute array set */
	FORCEINLINE EMeshAttributeFlags GetFlags() const { return ArrayPtr->GetFlags(); }

	FORCEINLINE uint32 GetExtent() const { return 1; }

	/** Set the element with the given ID and index 0 to the provided value */
	template <typename T = ElementIDType, typename TEnableIf<TIsDerivedFrom<T, FElementID>::Value, int>::Type = 0>
	FORCEINLINE void Set(const ElementIDType ElementID, const Type& Value) const
	{
		ArrayPtr->GetArrayForChannel(0).GetElementBase(ElementID.GetValue())[0] = Value;
	}

	/** Set the element with the given ID and channel to the provided value */
	template <typename T = ElementIDType, typename TEnableIf<TIsDerivedFrom<T, FElementID>::Value, int>::Type = 0>
	FORCEINLINE void Set(const ElementIDType ElementID, const int32 Channel, const Type& Value) const
	{
		ArrayPtr->GetArrayForChannel(Channel).GetElementBase(ElementID.GetValue())[0] = Value;
	}

	FORCEINLINE void Set(int32 ElementIndex, const Type& Value) const
	{
		ArrayPtr->GetArrayForChannel(0).GetElementBase(ElementIndex)[0] = Value;
	}

	FORCEINLINE void Set(int32 ElementIndex, const int32 Channel, const Type& Value) const
	{
		ArrayPtr->GetArrayForChannel(Channel).GetElementBase(ElementIndex)[0] = Value;
	}

	/** Copies the given attribute array and index to this index */
	void Copy(TMeshAttributesRef<ElementIDType, const AttributeType, false> Src, const int32 DestIndex = 0, const int32 SrcIndex = 0);

	UE_DEPRECATED(4.26, "Please use SetNumChannels().")
	FORCEINLINE void SetNumIndices(const int32 NumChannels) const
	{
		ArrayPtr->ArrayType::SetNumChannels(NumChannels);	// note: override virtual dispatch
	}

	/** Sets number of channels this attribute has */
	FORCEINLINE void SetNumChannels(const int32 NumChannels) const
	{
		ArrayPtr->ArrayType::SetNumChannels(NumChannels);	// note: override virtual dispatch
	}

	UE_DEPRECATED(4.26, "Please use InsertChannel().")
	FORCEINLINE void InsertIndex(const int32 Index) const
	{
		ArrayPtr->ArrayType::InsertChannel(Index);		// note: override virtual dispatch
	}

	/** Inserts an attribute channel */
	FORCEINLINE void InsertChannel(const int32 Index) const
	{
		ArrayPtr->ArrayType::InsertChannel(Index);		// note: override virtual dispatch
	}

	UE_DEPRECATED(4.26, "Please use RemoveChannel().")
	FORCEINLINE void RemoveIndex(const int32 Index) const
	{
		ArrayPtr->ArrayType::RemoveChannel(Index);		// note: override virtual dispatch
	}

	/** Removes an attribute channel */
	FORCEINLINE void RemoveChannel(const int32 Index) const
	{
		ArrayPtr->ArrayType::RemoveChannel(Index);		// note: override virtual dispatch
	}

protected:
	ArrayType* ArrayPtr;
};

template <typename ElementIDType, typename AttributeType>
void TMeshAttributesRef<ElementIDType, AttributeType, false>::Copy(TMeshAttributesRef<ElementIDType, const AttributeType, false> Src, const int32 DestIndex, const int32 SrcIndex)
{
	check(Src.IsValid());
	const TMeshAttributeArrayBase<AttributeType>& SrcArray = Src->ArrayPtr->GetArrayForChannel(SrcIndex);
	TMeshAttributeArrayBase<AttributeType>& DestArray = ArrayPtr->GetArrayForChannel(DestIndex);
	const int32 Num = FMath::Min(SrcArray.Num(), DestArray.Num());
	for (int32 Index = 0; Index < Num; Index++)
	{
		DestArray.GetElementBase(Index)[0] = SrcArray.GetElementBase(Index)[0];
	}
}


template <typename ElementIDType, typename AttributeType>
class TMeshAttributesRef<ElementIDType, AttributeType, true>
{
	template <typename T, typename U, bool IsArray> friend class TMeshAttributesRef;

public:
	using Type = typename TBreakArrayView<AttributeType>::Type;
	using ArrayType = typename TCopyQualifiersFromTo<Type, TMeshAttributeArraySet<typename TRemoveCV<Type>::Type>>::Type;

	/** Constructor taking a pointer to a TMeshAttributeArraySet */
	FORCEINLINE explicit TMeshAttributesRef(ArrayType* InArrayPtr = nullptr, uint32 InExtent = 1)
		: ArrayPtr(InArrayPtr),
		  Extent(InExtent)
	{}

	/** Implicitly construct a TMeshAttributesRef-to-const from a regular one */
	template <typename T = Type, typename TEnableIf<TIsSame<T, const T>::Value, int>::Type = 0>
	FORCEINLINE TMeshAttributesRef(TMeshAttributesRef<ElementIDType, TArrayView<typename TRemoveCV<T>::Type>, true> InRef)
		: ArrayPtr(InRef.ArrayPtr),
		  Extent(InRef.Extent)
	{}

	/** Implicitly construct a TMeshAttributesRef from a TMeshAttributesArray **/
	template <typename T = ElementIDType, typename U = AttributeType, typename TEnableIf<!TIsSame<T, int32>::Value, int>::Type = 0>
	FORCEINLINE TMeshAttributesRef(TMeshAttributesRef<int32, U, true> InRef)
		: ArrayPtr(InRef.ArrayPtr),
		  Extent(InRef.Extent)
	{}

	/** Implicitly construct a TMeshAttributesRef-to-const from a TMeshAttributesArray */
	template <typename T = ElementIDType, typename U = Type, typename TEnableIf<TIsSame<U, const U>::Value, int>::Type = 0, typename TEnableIf<!TIsSame<T, int32>::Value, int>::Type = 0>
	FORCEINLINE TMeshAttributesRef(TMeshAttributesRef<int32, TArrayView<typename TRemoveCV<U>::Type>, true> InRef)
		: ArrayPtr(InRef.ArrayPtr),
		  Extent(InRef.Extent)
	{}


	/** Access elements from attribute channel 0 */
	template <typename T = ElementIDType, typename TEnableIf<TIsDerivedFrom<T, FElementID>::Value, int>::Type = 0>
	FORCEINLINE TArrayView<Type> operator[](const ElementIDType ElementID) const
	{
		Type* Element = ArrayPtr->GetArrayForChannel(0).GetElementBase(ElementID.GetValue());
		return TArrayView<Type>(Element, Extent);
	}

	/** Get the element with the given ID from channel 0 */
	template <typename T = ElementIDType, typename TEnableIf<TIsDerivedFrom<T, FElementID>::Value, int>::Type = 0>
	FORCEINLINE TArrayView<Type> Get(const ElementIDType ElementID) const
	{
		Type* Element = ArrayPtr->GetArrayForChannel(0).GetElementBase(ElementID.GetValue());
		return TArrayView<Type>(Element, Extent);
	}

	/** Get the element with the given ID and channel */
	template <typename T = ElementIDType, typename TEnableIf<TIsDerivedFrom<T, FElementID>::Value, int>::Type = 0>
	FORCEINLINE TArrayView<Type> Get(const ElementIDType ElementID, const int32 Channel) const
	{
		Type* Element = ArrayPtr->GetArrayForChannel(Channel).GetElementBase(ElementID.GetValue());
		return TArrayView<Type>(Element, Extent);
	}

	FORCEINLINE TArrayView<Type> operator[](int32 ElementIndex) const
	{
		Type* Element = ArrayPtr->GetArrayForChannel(0).GetElementBase(ElementIndex);
		return TArrayView<Type>(Element, Extent);
	}

	FORCEINLINE TArrayView<Type> Get(int32 ElementIndex) const
	{
		Type* Element = ArrayPtr->GetArrayForChannel(0).GetElementBase(ElementIndex);
		return TArrayView<Type>(Element, Extent);
	}

	FORCEINLINE TArrayView<Type> Get(int32 ElementIndex, const int32 Channel) const
	{
		Type* Element = ArrayPtr->GetArrayForChannel(Channel).GetElementBase(ElementIndex);
		return TArrayView<Type>(Element, Extent);
	}

	FORCEINLINE TArrayView<Type> GetRawArray(const int32 ChannelIndex = 0) const
	{
		if (ArrayPtr == nullptr)
		{
			return TArrayView<Type>();
		}

		Type* Element = ArrayPtr->GetArrayForChannel(ChannelIndex).GetElementBase(0);
		return TArrayView<Type>(Element, GetNumElements() * Extent);
	}

	/** Return whether the reference is valid or not */
	FORCEINLINE bool IsValid() const { return (ArrayPtr != nullptr); }

	/** Return default value for this attribute type */
	FORCEINLINE Type GetDefaultValue() const { return ArrayPtr->GetDefaultValue(); }

	UE_DEPRECATED(4.26, "Please use GetNumChannels().")
	FORCEINLINE int32 GetNumIndices() const
	{
		return ArrayPtr->ArrayType::GetNumChannels();	// note: override virtual dispatch
	}

	/** Return number of channels this attribute has */
	FORCEINLINE int32 GetNumChannels() const
	{
		return ArrayPtr->ArrayType::GetNumChannels();	// note: override virtual dispatch
	}

	/** Get the number of elements in this attribute array */
	FORCEINLINE int32 GetNumElements() const
	{
		return ArrayPtr->GetNumElements();
	}

	/** Get the flags for this attribute array set */
	FORCEINLINE EMeshAttributeFlags GetFlags() const { return ArrayPtr->GetFlags(); }

	FORCEINLINE uint32 GetExtent() const { return Extent; }

	/** Copies the given attribute array and index to this index */
	void Copy(TMeshAttributesConstRef<ElementIDType, AttributeType, true> Src, const int32 DestIndex = 0, const int32 SrcIndex = 0);

	UE_DEPRECATED(4.26, "Please use SetNumChannels().")
	FORCEINLINE void SetNumIndices(const int32 NumChannels) const
	{
		ArrayPtr->ArrayType::SetNumChannels(NumChannels);	// note: override virtual dispatch
	}

	/** Sets number of channels this attribute has */
	FORCEINLINE void SetNumChannels(const int32 NumChannels) const
	{
		ArrayPtr->ArrayType::SetNumChannels(NumChannels);	// note: override virtual dispatch
	}

	UE_DEPRECATED(4.26, "Please use InsertChannel().")
	FORCEINLINE void InsertIndex(const int32 Index) const
	{
		ArrayPtr->ArrayType::InsertChannel(Index);		// note: override virtual dispatch
	}

	/** Inserts an attribute channel */
	FORCEINLINE void InsertChannel(const int32 Index) const
	{
		ArrayPtr->ArrayType::InsertChannel(Index);		// note: override virtual dispatch
	}

	UE_DEPRECATED(4.26, "Please use RemoveChannel().")
	FORCEINLINE void RemoveIndex(const int32 Index) const
	{
		ArrayPtr->ArrayType::RemoveChannel(Index);		// note: override virtual dispatch
	}

	/** Removes an attribute channel */
	FORCEINLINE void RemoveChannel(const int32 Index) const
	{
		ArrayPtr->ArrayType::RemoveChannel(Index);		// note: override virtual dispatch
	}

protected:
	ArrayType* ArrayPtr;
	uint32 Extent;
};

template <typename ElementIDType, typename AttributeType>
void TMeshAttributesRef<ElementIDType, AttributeType, true>::Copy(TMeshAttributesConstRef<ElementIDType, AttributeType, true> Src, const int32 DestIndex, const int32 SrcIndex)
{
	check(Src.IsValid());
	check(Src.Extent == Extent);
	const TMeshAttributeArrayBase<Type>& SrcArray = Src->ArrayPtr->GetArrayForChannel(SrcIndex);
	TMeshAttributeArrayBase<Type>& DestArray = ArrayPtr->GetArrayForChannel(DestIndex);
	const int32 Num = FMath::Min(SrcArray.Num(), DestArray.Num());
	for (int32 Index = 0; Index < Num; Index++)
	{
		for (uint32 Count = 0; Count < Extent; Count++)
		{
			DestArray.GetElementBase(Index)[Count] = SrcArray.GetElementBase(Index)[Count];
		}
	}
}

/**
 * This is the class used to provide a 'view' of the specified type on an attribute array.
 * Like TMeshAttributesRef, it is a proxy object which is valid for as long as the owning FMeshDescription exists,
 * and should be passed by value.
 *
 * This is the base class, and shouldn't be instanced directly.
 */
template <typename ViewType>
class TMeshAttributesViewBase
{
public:
	using Type = ViewType;
	using ArrayType = typename TCopyQualifiersFromTo<ViewType, FMeshAttributeArraySetBase>::Type;

	/** Return whether the reference is valid or not */
	FORCEINLINE bool IsValid() const { return ( ArrayPtr != nullptr ); }

	/** Return number of indices this attribute has */
	FORCEINLINE int32 GetNumIndices() const { return ArrayPtr->GetNumIndices(); }

	/** Return default value for this attribute type */
	FORCEINLINE ViewType GetDefaultValue() const;

	/** Get the number of elements in this attribute array */
	FORCEINLINE int32 GetNumElements() const
	{
		return ArrayPtr->GetNumElements();
	}

protected:
	/** Constructor taking a pointer to a FMeshAttributeArraySetBase */
	FORCEINLINE explicit TMeshAttributesViewBase( ArrayType* InArrayPtr )
		: ArrayPtr( InArrayPtr )
	{}

	/** Get the element with the given ID from index 0 */
	FORCEINLINE ViewType GetByIndex( const int32 ElementIndex ) const;

	/** Get the element with the given element and attribute indices */
	FORCEINLINE ViewType GetByIndex( const int32 ElementIndex, const int32 AttributeIndex ) const;

	/** Set the attribute index 0 element with the given index to the provided value */
	FORCEINLINE void SetByIndex( const int32 ElementIndex, const ViewType& Value ) const;

	/** Set the element with the given element and attribute indices to the provided value */
	FORCEINLINE void SetByIndex( const int32 ElementIndex, const int32 AttributeIndex, const ViewType& Value ) const;

	ArrayType* ArrayPtr;
};


/**
 * This is a derived version with typesafe element accessors, which is returned by TAttributesSet<>.
 */
template <typename ElementIDType, typename ViewType>
class TMeshAttributesView final : public TMeshAttributesViewBase<ViewType>
{
	using Super = TMeshAttributesViewBase<ViewType>;

	template <typename T, typename U> friend class TMeshAttributesView;

public:
	/** Constructor taking a pointer to a FMeshAttributeArraySetBase */
	FORCEINLINE explicit TMeshAttributesView( typename Super::ArrayType* InArrayPtr = nullptr )
		: Super( InArrayPtr )
	{}

	/** Implicitly construct a TMeshAttributesViewBase-to-const from a regular one */
	template <typename T = ViewType, typename TEnableIf<TIsSame<T, const T>::Value, int>::Type = 0>
	FORCEINLINE TMeshAttributesView( TMeshAttributesView<ElementIDType, typename TRemoveCV<T>::Type> InView )
		: Super( InView.ArrayPtr )
	{}

	/** Get the element with the given ID from index 0. This version has a typesafe element ID accessor. */
	FORCEINLINE ViewType Get( const ElementIDType ElementID ) const { return this->GetByIndex( ElementID.GetValue() ); }

	/** Get the element with the given ID and index. This version has a typesafe element ID accessor. */
	FORCEINLINE ViewType Get( const ElementIDType ElementID, const int32 Index ) const { return this->GetByIndex( ElementID.GetValue(), Index ); }

	/** Set the element with the given ID and index 0 to the provided value. This version has a typesafe element ID accessor. */
	FORCEINLINE void Set( const ElementIDType ElementID, const ViewType& Value ) const { this->SetByIndex( ElementID.GetValue(), Value ); }

	/** Set the element with the given ID and index to the provided value. This version has a typesafe element ID accessor. */
	FORCEINLINE void Set( const ElementIDType ElementID, const int32 Index, const ViewType& Value ) const { this->SetByIndex( ElementID.GetValue(), Index, Value ); }

	/** Sets number of indices this attribute has */
	FORCEINLINE void SetNumIndices( const int32 NumIndices ) const { this->ArrayPtr->SetNumChannels( NumIndices ); }

	/** Inserts an attribute index */
	FORCEINLINE void InsertIndex( const int32 Index ) const { this->ArrayPtr->InsertIndex( Index ); }

	/** Removes an attribute index */
	FORCEINLINE void RemoveIndex( const int32 Index ) const { this->ArrayPtr->RemoveIndex( Index ); }
};

template <typename ElementIDType, typename AttributeType>
using TMeshAttributesConstView = TMeshAttributesView<ElementIDType, const AttributeType>;


/**
 * This is a wrapper for an allocated attributes array.
 * It holds a TUniquePtr pointing to the actual attributes array, and performs polymorphic copy and assignment,
 * as per the actual array type.
 */
class FAttributesSetEntry
{
public:
	/**
	 * Default constructor.
	 * This breaks the invariant that Ptr be always valid, but is necessary so that it can be the value type of a TMap.
	 */
	FORCEINLINE FAttributesSetEntry() = default;

	/**
	 * Construct a valid FAttributesSetEntry of the concrete type specified.
	 */
	template <typename AttributeType>
	FORCEINLINE FAttributesSetEntry(const int32 NumberOfChannels, const AttributeType& Default, const EMeshAttributeFlags Flags, const int32 NumElements, const int32 Extent)
		: Ptr(MakeUnique<TMeshAttributeArraySet<AttributeType>>(NumberOfChannels, Default, Flags, NumElements, Extent))
	{}

	/** Default destructor */
	FORCEINLINE ~FAttributesSetEntry() = default;

	/** Polymorphic copy: a new copy of Other is created */
	FAttributesSetEntry(const FAttributesSetEntry& Other)
		: Ptr(Other.Ptr ? Other.Ptr->Clone() : nullptr)
	{}

	/** Default move constructor */
	FAttributesSetEntry(FAttributesSetEntry&&) = default;

	/** Polymorphic assignment */
	FAttributesSetEntry& operator=(const FAttributesSetEntry& Other)
	{
		FAttributesSetEntry Temp(Other);
		Swap(*this, Temp);
		return *this;
	}

	/** Default move assignment */
	FAttributesSetEntry& operator=(FAttributesSetEntry&&) = default;

	/** Transparent access through the TUniquePtr */
	FORCEINLINE const FMeshAttributeArraySetBase* Get() const { return Ptr.Get(); }
	FORCEINLINE const FMeshAttributeArraySetBase* operator->() const { return Ptr.Get(); }
	FORCEINLINE const FMeshAttributeArraySetBase& operator*() const { return *Ptr; }
	FORCEINLINE FMeshAttributeArraySetBase* Get() { return Ptr.Get(); }
	FORCEINLINE FMeshAttributeArraySetBase* operator->() { return Ptr.Get(); }
	FORCEINLINE FMeshAttributeArraySetBase& operator*() { return *Ptr; }

	/** Object can be coerced to bool to indicate if it is valid */
	FORCEINLINE explicit operator bool() const { return Ptr.IsValid(); }
	FORCEINLINE bool operator!() const { return !Ptr.IsValid(); }

	/** Given a type at runtime, allocate an attribute array of that type, owned by Ptr */
	void CreateArrayOfType(const uint32 Type, const uint32 Extent);

	/** Serialization */
	friend FArchive& operator<<(FArchive& Ar, FAttributesSetEntry& Entry);

private:
	TUniquePtr<FMeshAttributeArraySetBase> Ptr;
};


/**
 * This is the container for all attributes and their arrays. It wraps a TMap, mapping from attribute name to attribute array.
 * An attribute may be of any arbitrary type; we use a mixture of polymorphism and compile-time templates to handle the different types.
 */
class FAttributesSetBase
{
public:
	/** Constructor */
	FAttributesSetBase()
		: NumElements(0)
	{}

	/**
	 * Register a new attribute name with the given type (must be a member of the AttributeTypes tuple).
	 * If the attribute name is already registered, it will update it to use the new type, number of channels and flags.
	 *
	 * Example of use:
	 *
	 *		VertexInstanceAttributes().RegisterAttribute<FVector2D>( "UV", 8 );
	 *                        . . .
	 *		TVertexInstanceAttributeArray<FVector2D>& UV0 = VertexInstanceAttributes().GetAttributes<FVector2D>( "UV", 0 );
	 *		UV0[ VertexInstanceID ] = FVector2D( 1.0f, 1.0f );
	 */
	template <typename AttributeType,
			  typename TEnableIf<!TIsBoundedArray<AttributeType>::Value, int>::Type = 0>
	TMeshAttributesArray<AttributeType> RegisterAttribute(const FName AttributeName, const int32 NumberOfChannels = 1, const AttributeType& Default = AttributeType(), const EMeshAttributeFlags Flags = EMeshAttributeFlags::None );

	template <typename ArrayType,
			  typename TEnableIf<TIsBoundedArray<ArrayType>::Value, int>::Type = 0,
			  typename AttributeType = typename TBreakArray<ArrayType>::Type,
			  uint32 Extent = TBreakArray<ArrayType>::Value>
	TMeshAttributesArray<TArrayView<AttributeType>> RegisterAttribute(const FName AttributeName, const int32 NumberOfChannels = 1, const typename TBreakArray<ArrayType>::Type& Default = AttributeType(), const EMeshAttributeFlags Flags = EMeshAttributeFlags::None);

	/**
	 * Register a new index attribute (type is implicitly int).
	 *
	 * If the attribute name is already registered, it will update it to use the new type, number of channels and flags.
	 */
	template <typename AttributeType,
			  typename TEnableIf<TIsSame<AttributeType, int>::Value || TIsDerivedFrom<AttributeType, FElementID>::Value, int>::Type = 0>
	TMeshAttributesArray<AttributeType> RegisterIndexAttribute(const FName AttributeName, const int32 NumberOfChannels = 1, const EMeshAttributeFlags Flags = EMeshAttributeFlags::None);

	/**
	 * Register a new attribute denoting an array of indices (type is implicitly int).
	 *
	 * If the attribute name is already registered, it will update it to use the new type, number of channels and flags.
	 */
	template <typename ArrayType,
			  typename AttributeType = typename TBreakArray<ArrayType>::Type,
			  typename TEnableIf<TIsSame<AttributeType, int>::Value || TIsDerivedFrom<AttributeType, FElementID>::Value, int>::Type = 0,
			  uint32 Extent = TBreakArray<ArrayType>::Value>
	TMeshAttributesArray<TArrayView<AttributeType>> RegisterIndexAttribute(const FName AttributeName, const int32 NumberOfChannels = 1, const EMeshAttributeFlags Flags = EMeshAttributeFlags::None);

	/**
	 * Unregister an attribute with the given name.
	 */
	void UnregisterAttribute(const FName AttributeName)
	{
		Map.Remove(AttributeName);
	}

	/** Determines whether an attribute exists with the given name */
	bool HasAttribute(const FName AttributeName) const
	{
		return Map.Contains(AttributeName);
	}

	/**
	 * Determines whether an attribute of the given type exists with the given name
	 */
	template <typename AttributeType,
			  typename TEnableIf<!TIsBoundedArray<AttributeType>::Value, int>::Type = 0>
	bool HasAttributeOfType(const FName AttributeName) const
	{
		if (const FAttributesSetEntry* ArraySetPtr = Map.Find(AttributeName))
		{
			return (*ArraySetPtr)->HasType<AttributeType>() && (*ArraySetPtr)->GetExtent() == 1;
		}

		return false;
	}

	template <typename ArrayType,
			  typename TEnableIf<TIsBoundedArray<ArrayType>::Value, int>::Type = 0,
			  typename AttributeType = typename TBreakArray<ArrayType>::Type,
			  uint32 Extent = TBreakArray<ArrayType>::Value>
	bool HasAttributeOfType(const FName AttributeName) const
	{
		if (const FAttributesSetEntry* ArraySetPtr = Map.Find(AttributeName))
		{
			return (*ArraySetPtr)->HasType<AttributeType>() && (*ArraySetPtr)->GetExtent() == Extent;
		}

		return false;
	}

	/** Initializes all attributes to have the given number of elements with the default value */
	void Initialize(const int32 Count)
	{
		NumElements = Count;
		for (auto& MapEntry : Map)
		{
			MapEntry.Value->Initialize(Count);
		}
	}

	/** Sets all attributes to have the given number of elements, preserving existing values and filling extra elements with the default value */
	void SetNumElements(const int32 Count)
	{
		NumElements = Count;
		for (auto& MapEntry : Map)
		{
			MapEntry.Value->SetNumElements(Count);
		}
	}

	/** Gets the number of elements in the attribute set */
	int32 GetNumElements() const
	{
		return NumElements;
	}

	/** Applies the given remapping to the attributes set */
	void Remap(const TSparseArray<int32>& IndexRemap);

	/** Returns an array of all the attribute names registered */
	template <typename Allocator>
	void GetAttributeNames(TArray<FName, Allocator>& OutAttributeNames) const
	{
		Map.GetKeys(OutAttributeNames);
	}

	/** Determine whether an attribute has any of the given flags */
	bool DoesAttributeHaveAnyFlags(const FName AttributeName, EMeshAttributeFlags AttributeFlags) const
	{
		if (const FAttributesSetEntry* ArraySetPtr = Map.Find(AttributeName))
		{
			return EnumHasAnyFlags((*ArraySetPtr)->GetFlags(), AttributeFlags);
		}

		return false;
	}

	/** Determine whether an attribute has all of the given flags */
	bool DoesAttributeHaveAllFlags(const FName AttributeName, EMeshAttributeFlags AttributeFlags) const
	{
		if (const FAttributesSetEntry* ArraySetPtr = Map.Find(AttributeName))
		{
			return EnumHasAllFlags((*ArraySetPtr)->GetFlags(), AttributeFlags);
		}

		return false;
	}

	uint32 GetHash(const FName AttributeName) const
	{
		if (const FAttributesSetEntry* ArraySetPtr = Map.Find(AttributeName))
		{
			return (*ArraySetPtr)->GetHash();
		}
		return 0;
	}

	/**
	 * Insert a new element at the given index.
	 * The public API version of this function takes an ID of ElementIDType instead of a typeless index.
	 */
	void Insert(const int32 Index)
	{
		NumElements = FMath::Max(NumElements, Index + 1);

		for (auto& MapEntry : Map)
		{
			MapEntry.Value->Insert(Index);
			check(MapEntry.Value->GetNumElements() == NumElements);
		}
	}

	/**
	 * Remove an element at the given index.
	 * The public API version of this function takes an ID of ElementIDType instead of a typeless index.
	 */
	void Remove(const int32 Index)
	{
		for (auto& MapEntry : Map)
		{
			MapEntry.Value->Remove(Index);
		}
	}

	/**
	 * Get an attribute array with the given type and name.
	 * The attribute type must correspond to the type passed as the template parameter.
	 */
	template <typename AttributeType,
			  typename TEnableIf<!TIsArrayView<AttributeType>::Value, int>::Type = 0>
	TMeshAttributesConstRef<int32, AttributeType> GetAttributesRef(const FName AttributeName) const
	{
		if (const FAttributesSetEntry* ArraySetPtr = this->Map.Find(AttributeName))
		{
			using Type = typename TChooseClass<TIsDerivedFrom<AttributeType, FElementID>::Value, int, AttributeType>::Result;
			if ((*ArraySetPtr)->HasType<Type>() && (*ArraySetPtr)->GetExtent() == 1)
			{
				return TMeshAttributesConstRef<int32, AttributeType>(static_cast<const TMeshAttributeArraySet<AttributeType>*>(ArraySetPtr->Get()));
			}
		}

		return TMeshAttributesConstRef<int32, AttributeType>();
	}

	template <typename AttributeType,
			  typename TEnableIf<!TIsArrayView<AttributeType>::Value, int>::Type = 0>
	TMeshAttributesRef<int32, AttributeType> GetAttributesRef(const FName AttributeName)
	{
		if (FAttributesSetEntry* ArraySetPtr = this->Map.Find(AttributeName))
		{
			using Type = typename TChooseClass<TIsDerivedFrom<AttributeType, FElementID>::Value, int, AttributeType>::Result;
			if ((*ArraySetPtr)->HasType<Type>() && (*ArraySetPtr)->GetExtent() == 1)
			{
				return TMeshAttributesRef<int32, AttributeType>(static_cast<TMeshAttributeArraySet<AttributeType>*>(ArraySetPtr->Get()));
			}
		}

		return TMeshAttributesRef<int32, AttributeType>();
	}

	template <typename ArrayType,
			  typename TEnableIf<TIsArrayView<ArrayType>::Value, int>::Type = 0,
			  typename AttributeType = typename TBreakArrayView<ArrayType>::Type>
	TMeshAttributesConstRef<int32, ArrayType> GetAttributesRef(const FName AttributeName) const
	{
		if (const FAttributesSetEntry* ArraySetPtr = this->Map.Find(AttributeName))
		{
			using Type = typename TChooseClass<TIsDerivedFrom<AttributeType, FElementID>::Value, int, AttributeType>::Result;
			if ((*ArraySetPtr)->HasType<Type>())
			{
				return TMeshAttributesConstRef<int32, ArrayType>(static_cast<const TMeshAttributeArraySet<AttributeType>*>(ArraySetPtr->Get()), (*ArraySetPtr)->GetExtent());
			}
		}

		return TMeshAttributesConstRef<int32, ArrayType>();
	}

	template <typename ArrayType,
			  typename TEnableIf<TIsArrayView<ArrayType>::Value, int>::Type = 0,
			  typename AttributeType = typename TBreakArrayView<ArrayType>::Type>
	TMeshAttributesRef<int32, ArrayType> GetAttributesRef(const FName AttributeName)
	{
		if (FAttributesSetEntry* ArraySetPtr = this->Map.Find(AttributeName))
		{
			using Type = typename TChooseClass<TIsDerivedFrom<AttributeType, FElementID>::Value, int, AttributeType>::Result;
			if ((*ArraySetPtr)->HasType<Type>())
			{
				return TMeshAttributesRef<int32, ArrayType>(static_cast<TMeshAttributeArraySet<AttributeType>*>(ArraySetPtr->Get()), (*ArraySetPtr)->GetExtent());
			}
		}

		return TMeshAttributesRef<int32, ArrayType>();
	}

	void AppendAttributesFrom(const FAttributesSetBase& OtherAttributesSet);

protected:
	/** Serialization */
	friend MESHDESCRIPTION_API FArchive& operator<<(FArchive& Ar, FAttributesSetBase& AttributesSet);

	template <typename T>
	friend void SerializeLegacy(FArchive& Ar, FAttributesSetBase& AttributesSet);

	/** The actual container */
	TMap<FName, FAttributesSetEntry> Map;

	/** The number of elements in each attribute array */
	int32 NumElements;
};


template <typename AttributeType,
		  typename TEnableIf<!TIsBoundedArray<AttributeType>::Value, int>::Type>
TMeshAttributesArray<AttributeType> FAttributesSetBase::RegisterAttribute(const FName AttributeName, const int32 NumberOfChannels, const AttributeType& Default, const EMeshAttributeFlags Flags)
{
	using ArrayType = TMeshAttributeArraySet<AttributeType>;
	if (FAttributesSetEntry* ArraySetPtr = Map.Find(AttributeName))
	{
		if ((*ArraySetPtr)->HasType<AttributeType>() && (*ArraySetPtr)->GetExtent() == 1)
		{
			static_cast<ArrayType*>(ArraySetPtr->Get())->ArrayType::SetNumChannels(NumberOfChannels);	// note: override virtual dispatch
			(*ArraySetPtr)->SetFlags(Flags);
			return TMeshAttributesArray<AttributeType>(static_cast<ArrayType*>(ArraySetPtr->Get()));
		}
		else
		{
			Map.Remove(AttributeName);
			FAttributesSetEntry& Entry = Map.Emplace(AttributeName, FAttributesSetEntry(NumberOfChannels, Default, Flags, NumElements, 1));
			return TMeshAttributesArray<AttributeType>(static_cast<ArrayType*>(Entry.Get()));
		}
	}
	else
	{
		FAttributesSetEntry& Entry = Map.Emplace(AttributeName, FAttributesSetEntry(NumberOfChannels, Default, Flags, NumElements, 1));
		return TMeshAttributesArray<AttributeType>(static_cast<ArrayType*>(Entry.Get()));
	}
}


template <typename ArrayType,
		  typename TEnableIf<TIsBoundedArray<ArrayType>::Value, int>::Type,
		  typename AttributeType,
		  uint32 Extent>
TMeshAttributesArray<TArrayView<AttributeType>> FAttributesSetBase::RegisterAttribute(const FName AttributeName, const int32 NumberOfChannels, const typename TBreakArray<ArrayType>::Type& Default, const EMeshAttributeFlags Flags)
{
	using ContainerType = TMeshAttributeArraySet<AttributeType>;
	if (FAttributesSetEntry* ArraySetPtr = Map.Find(AttributeName))
	{
		if ((*ArraySetPtr)->HasType<AttributeType>() && (*ArraySetPtr)->GetExtent() == Extent)
		{
			static_cast<ContainerType*>(ArraySetPtr->Get())->ContainerType::SetNumChannels(NumberOfChannels);	// note: override virtual dispatch
			(*ArraySetPtr)->SetFlags(Flags);
			return TMeshAttributesArray<TArrayView<AttributeType>>(static_cast<ContainerType*>(ArraySetPtr->Get()), Extent);
		}
		else
		{
			Map.Remove(AttributeName);
			FAttributesSetEntry& Entry = Map.Emplace(AttributeName, FAttributesSetEntry(NumberOfChannels, Default, Flags, NumElements, Extent));
			return TMeshAttributesArray<TArrayView<AttributeType>>(static_cast<ContainerType*>(Entry.Get()), Extent);
		}
	}
	else
	{
		FAttributesSetEntry& Entry = Map.Emplace(AttributeName, FAttributesSetEntry(NumberOfChannels, Default, Flags, NumElements, Extent));
		return TMeshAttributesArray<TArrayView<AttributeType>>(static_cast<ContainerType*>(Entry.Get()), Extent);
	}
}


template <typename AttributeType,
		  typename TEnableIf<TIsSame<AttributeType, int>::Value || TIsDerivedFrom<AttributeType, FElementID>::Value, int>::Type>
TMeshAttributesArray<AttributeType> FAttributesSetBase::RegisterIndexAttribute(const FName AttributeName, const int32 NumberOfChannels, const EMeshAttributeFlags Flags)
{
	using ArrayType = TMeshAttributeArraySet<AttributeType>;
	if (FAttributesSetEntry* ArraySetPtr = Map.Find(AttributeName))
	{
		if ((*ArraySetPtr)->HasType<int>() && (*ArraySetPtr)->GetExtent() == 1)
		{
			static_cast<ArrayType*>(ArraySetPtr->Get())->ArrayType::SetNumChannels(NumberOfChannels);	// note: override virtual dispatch
			(*ArraySetPtr)->SetFlags(Flags);
			return TMeshAttributesArray<AttributeType>(static_cast<ArrayType*>(ArraySetPtr->Get()));
		}
		else
		{
			Map.Remove(AttributeName);
			FAttributesSetEntry& Entry = Map.Emplace(AttributeName, FAttributesSetEntry(NumberOfChannels, int32(INDEX_NONE), Flags | EMeshAttributeFlags::IndexReference, NumElements, 1));
			return TMeshAttributesArray<AttributeType>(static_cast<ArrayType*>(Entry.Get()));
		}
	}
	else
	{
		FAttributesSetEntry& Entry = Map.Emplace(AttributeName, FAttributesSetEntry(NumberOfChannels, int32(INDEX_NONE), Flags | EMeshAttributeFlags::IndexReference, NumElements, 1));
		return TMeshAttributesArray<AttributeType>(static_cast<ArrayType*>(Entry.Get()));
	}

}


template <typename ArrayType,
		  typename AttributeType,
		  typename TEnableIf<TIsSame<AttributeType, int>::Value || TIsDerivedFrom<AttributeType, FElementID>::Value, int>::Type,
		  uint32 Extent>
TMeshAttributesArray<TArrayView<AttributeType>> FAttributesSetBase::RegisterIndexAttribute(const FName AttributeName, const int32 NumberOfChannels, const EMeshAttributeFlags Flags)
{
	if (FAttributesSetEntry* ArraySetPtr = Map.Find(AttributeName))
	{
		if ((*ArraySetPtr)->HasType<int>() && (*ArraySetPtr)->GetExtent() == Extent)
		{
			using ContainerType = TMeshAttributeArraySet<int>;
			static_cast<ContainerType*>(ArraySetPtr->Get())->ContainerType::SetNumChannels(NumberOfChannels);	// note: override virtual dispatch
			(*ArraySetPtr)->SetFlags(Flags);
			return TMeshAttributesArray<TArrayView<AttributeType>>(static_cast<TMeshAttributeArraySet<AttributeType>*>(ArraySetPtr->Get()), Extent);
		}
		else
		{
			Map.Remove(AttributeName);
			FAttributesSetEntry& Entry = Map.Emplace(AttributeName, FAttributesSetEntry(NumberOfChannels, int32(INDEX_NONE), Flags | EMeshAttributeFlags::IndexReference, NumElements, Extent));
			return TMeshAttributesArray<TArrayView<AttributeType>>(static_cast<TMeshAttributeArraySet<AttributeType>*>(Entry.Get()), Extent);
		}
	}
	else
	{
		FAttributesSetEntry& Entry = Map.Emplace(AttributeName, FAttributesSetEntry(NumberOfChannels, int32(INDEX_NONE), Flags | EMeshAttributeFlags::IndexReference, NumElements, Extent));
		return TMeshAttributesArray<TArrayView<AttributeType>>(static_cast<TMeshAttributeArraySet<AttributeType>*>(Entry.Get()), Extent);
	}
}


/**
 * This is a version of the attributes set container which accesses elements by typesafe IDs.
 * This prevents access of (for example) vertex instance attributes by vertex IDs.
 */
template <typename ElementIDType>
class TAttributesSet final : public FAttributesSetBase
{
	using FAttributesSetBase::Insert;
	using FAttributesSetBase::Remove;

public:
	/**
	 * Get an attribute array with the given type and name.
	 * The attribute type must correspond to the type passed as the template parameter.
	 *
	 * Example of use:
	 *
	 *		TVertexAttributesConstRef<FVector> VertexPositions = VertexAttributes().GetAttributesRef<FVector>( "Position" ); // note: assign to value type
	 *		for( const FVertexID VertexID : GetVertices().GetElementIDs() )
	 *		{
	 *			const FVector Position = VertexPositions.Get( VertexID );
	 *			DoSomethingWith( Position );
	 *		}
	 *
	 * Note that the returned object is a value type which should be assigned and passed by value, not reference.
	 * It is valid for as long as this TAttributesSet object exists.
	 */
	template <typename AttributeType,
			  typename TEnableIf<!TIsArrayView<AttributeType>::Value, int>::Type = 0>
	TMeshAttributesConstRef<ElementIDType, AttributeType> GetAttributesRef(const FName AttributeName) const
	{
		if (const FAttributesSetEntry* ArraySetPtr = this->Map.Find(AttributeName))
		{
			using Type = typename TChooseClass<TIsDerivedFrom<AttributeType, FElementID>::Value, int32, AttributeType>::Result;
			if ((*ArraySetPtr)->HasType<Type>() && (*ArraySetPtr)->GetExtent() == 1)
			{
				return TMeshAttributesConstRef<ElementIDType, AttributeType>(static_cast<const TMeshAttributeArraySet<AttributeType>*>(ArraySetPtr->Get()));
			}
		}

		return TMeshAttributesConstRef<ElementIDType, AttributeType>();
	}

	template <typename AttributeType,
			  typename TEnableIf<!TIsArrayView<AttributeType>::Value, int>::Type = 0>
	TMeshAttributesRef<ElementIDType, AttributeType> GetAttributesRef(const FName AttributeName)
	{
		if (FAttributesSetEntry* ArraySetPtr = this->Map.Find(AttributeName))
		{
			using Type = typename TChooseClass<TIsDerivedFrom<AttributeType, FElementID>::Value, int32, AttributeType>::Result;
			if ((*ArraySetPtr)->HasType<Type>() && (*ArraySetPtr)->GetExtent() == 1)
			{
				return TMeshAttributesRef<ElementIDType, AttributeType>(static_cast<TMeshAttributeArraySet<AttributeType>*>(ArraySetPtr->Get()));
			}
		}

		return TMeshAttributesRef<ElementIDType, AttributeType>();
	}

	template <typename ArrayType,
			  typename TEnableIf<TIsArrayView<ArrayType>::Value, int>::Type = 0,
			  typename AttributeType = typename TBreakArrayView<ArrayType>::Type>
	TMeshAttributesConstRef<ElementIDType, ArrayType> GetAttributesRef(const FName AttributeName) const
	{
		if (const FAttributesSetEntry* ArraySetPtr = this->Map.Find(AttributeName))
		{
			using Type = typename TChooseClass<TIsDerivedFrom<AttributeType, FElementID>::Value, int32, AttributeType>::Result;
			if ((*ArraySetPtr)->HasType<Type>())
			{
				return TMeshAttributesConstRef<ElementIDType, ArrayType>(static_cast<const TMeshAttributeArraySet<AttributeType>*>(ArraySetPtr->Get()), (*ArraySetPtr)->GetExtent());
			}
		}

		return TMeshAttributesConstRef<ElementIDType, ArrayType>();
	}

	template <typename ArrayType,
			  typename TEnableIf<TIsArrayView<ArrayType>::Value, int>::Type = 0,
			  typename AttributeType = typename TBreakArrayView<ArrayType>::Type>
	TMeshAttributesRef<ElementIDType, ArrayType> GetAttributesRef(const FName AttributeName)
	{
		if (FAttributesSetEntry* ArraySetPtr = this->Map.Find(AttributeName))
		{
			using Type = typename TChooseClass<TIsDerivedFrom<AttributeType, FElementID>::Value, int32, AttributeType>::Result;
			if ((*ArraySetPtr)->HasType<Type>())
			{
				return TMeshAttributesRef<ElementIDType, ArrayType>(static_cast<TMeshAttributeArraySet<AttributeType>*>(ArraySetPtr->Get()), (*ArraySetPtr)->GetExtent());
			}
		}

		return TMeshAttributesRef<ElementIDType, ArrayType>();
	}

	/**
	 * Get a view on an attribute array with the given name, accessing elements as the given type.
	 * Access to elements will be slightly slower than with GetAttributesRef, but element access is not strongly typed.
	 *
	 * Example of use:
	 *
	 *		const TVertexInstanceAttributesView<FVector> VertexNormals = VertexInstanceAttributes().GetAttributesView<FVector>( "Normal" );
	 *		for( const FVertexInstanceID VertexInstanceID : GetVertexInstances().GetElementIDs() )
	 *		{
	 *          // This will work even if the Normals array has a different internal type, e.g. FPackedVector
	 *			const FVector Normal = VertexNormals.Get( VertexInstanceID );
	 *			DoSomethingWith( Normal );
	 *		}
	 *
	 * Note that the returned object is a value type which should be assigned and passed by value, not reference.
	 * It is valid for as long as this TAttributesSet object exists.
	 */
	template <typename ViewType>
	UE_DEPRECATED(4.25, "Views are due to be deprecated. Please use MeshAttributeRefs instead.")
	TMeshAttributesConstView<ElementIDType, ViewType> GetAttributesView(const FName AttributeName) const
	{
		if (const FAttributesSetEntry* ArraySetPtr = this->Map.Find(AttributeName))
		{
			return TMeshAttributesConstView<ElementIDType, ViewType>(ArraySetPtr->Get());
		}

		return TMeshAttributesConstView<ElementIDType, ViewType>();
	}

	template <typename ViewType>
	UE_DEPRECATED(4.25, "Views are due to be deprecated. Please use MeshAttributeRefs instead.")
	TMeshAttributesView<ElementIDType, ViewType> GetAttributesView(const FName AttributeName)
	{
		if (FAttributesSetEntry* ArraySetPtr = this->Map.Find(AttributeName))
		{
			return TMeshAttributesView<ElementIDType, ViewType>(ArraySetPtr->Get());
		}

		return TMeshAttributesView<ElementIDType, ViewType>();
	}

	UE_DEPRECATED(4.26, "Please use GetAttributeChannelCount() instead.")
	int32 GetAttributeIndexCount(const FName AttributeName) const
	{
		return GetAttributeChannelCount(AttributeName);
	}

	/** Returns the number of indices for the attribute with the given name */
	int32 GetAttributeChannelCount(const FName AttributeName) const
	{
		if (const FAttributesSetEntry* ArraySetPtr = this->Map.Find(AttributeName))
		{
			return (*ArraySetPtr)->GetNumChannels();
		}

		return 0;
	}

	template <typename AttributeType>
	UE_DEPRECATED(4.25, "Please use GetAttributeChannelCount() instead.")
	int32 GetAttributeIndexCount(const FName AttributeName) const
	{
		if (const FAttributesSetEntry* ArraySetPtr = this->Map.Find(AttributeName))
		{
			if ((*ArraySetPtr)->HasType<AttributeType>())
			{
				using ArrayType = TMeshAttributeArraySet<AttributeType>;
				return static_cast<const ArrayType*>( ArraySetPtr->Get() )->ArrayType::GetNumChannels();	// note: override virtual dispatch
			}
		}

		return 0;
	}

	UE_DEPRECATED(4.26, "Please use SetAttributeChannelCount() instead.")
	void SetAttributeIndexCount(const FName AttributeName, const int32 NumChannels)
	{
		SetAttributeChannelCount(AttributeName, NumChannels);
	}

	/** Sets the number of indices for the attribute with the given name */
	void SetAttributeChannelCount(const FName AttributeName, const int32 NumChannels)
	{
		if (FAttributesSetEntry* ArraySetPtr = this->Map.Find(AttributeName))
		{
			(*ArraySetPtr)->SetNumChannels(NumChannels);
		}
	}

	template <typename AttributeType>
	UE_DEPRECATED(4.25, "Please use untemplated SetAttributeChannelCount() instead.")
	void SetAttributeIndexCount(const FName AttributeName, const int32 NumIndices)
	{
		if (FAttributesSetEntry* ArraySetPtr = this->Map.Find(AttributeName))
		{
			if ((*ArraySetPtr)->HasType<AttributeType>())
			{
				using ArrayType = TMeshAttributeArraySet<AttributeType>;
				static_cast<ArrayType*>(ArraySetPtr->Get())->ArrayType::SetNumChannels(NumIndices);	// note: override virtual dispatch
			}
		}
	}

	UE_DEPRECATED(4.26, "Please use InsertAttributeChannel() instead.")
	void InsertAttributeIndex_Old(const FName AttributeName, const int32 Index)
	{
		InsertAttributeChannel(AttributeName, Index);
	}

	/** Insert a new index for the attribute with the given name */
	void InsertAttributeChannel(const FName AttributeName, const int32 Index)
	{
		if (FAttributesSetEntry* ArraySetPtr = this->Map.Find(AttributeName))
		{
			(*ArraySetPtr)->InsertChannel(Index);
		}
	}

	template <typename AttributeType>
	UE_DEPRECATED(4.25, "Please use untemplated InsertAttributeIndexCount() instead.")
	void InsertAttributeIndex(const FName AttributeName, const int32 Index)
	{
		if (FAttributesSetEntry* ArraySetPtr = this->Map.Find(AttributeName))
		{
			if ((*ArraySetPtr)->HasType<AttributeType>())
			{
				using ArrayType = TMeshAttributeArraySet<AttributeType>;
				static_cast<ArrayType*>(ArraySetPtr->Get())->ArrayType::InsertChannel(Index);	// note: override virtual dispatch
			}
		}
	}

	UE_DEPRECATED(4.26, "Please use RemoveAttributeChannel() instead.")
	void RemoveAttributeIndex_Old(const FName AttributeName, const int32 Index)
	{
		RemoveAttributeChannel(AttributeName, Index);
	}

	/** Remove an existing index from the attribute with the given name */
	void RemoveAttributeChannel(const FName AttributeName, const int32 Index)
	{
		if (FAttributesSetEntry* ArraySetPtr = this->Map.Find(AttributeName))
		{
			(*ArraySetPtr)->RemoveChannel(Index);
		}
	}

	template <typename AttributeType>
	UE_DEPRECATED(4.25, "Please use untemplated RemoveAttributeIndexCount() instead.")
	void RemoveAttributeIndex(const FName AttributeName, const int32 Index)
	{
		if (FAttributesSetEntry* ArraySetPtr = this->Map.Find(AttributeName))
		{
			if ((*ArraySetPtr)->HasType<AttributeType>())
			{
				using ArrayType = TMeshAttributeArraySet<AttributeType>;
				static_cast<ArrayType*>(ArraySetPtr->Get())->ArrayType::RemoveChannel(Index);	// note: override virtual dispatch
			}
		}
	}

	/**
	 * Get an attribute value for the given element ID.
	 * Note: it is generally preferable to get a TMeshAttributesRef and access elements through that, if you wish to access more than one.
	 */
	template <typename AttributeType,
			  typename TEnableIf<!TIsArrayView<AttributeType>::Value, int>::Type = 0>
	AttributeType GetAttribute(const ElementIDType ElementID, const FName AttributeName, const int32 AttributeChannel = 0) const
	{
		const FMeshAttributeArraySetBase* ArraySetPtr = this->Map.FindChecked(AttributeName).Get();
		check(ArraySetPtr->HasType<AttributeType>());
		check(ArraySetPtr->GetExtent() == 1);
		const AttributeType* ElementBase = static_cast<const TMeshAttributeArraySet<AttributeType>*>(ArraySetPtr)->GetArrayForChannel(AttributeChannel).GetElementBase(ElementID.GetValue());
		return ElementBase[0];
	}

	/** Get an attribute value for the given element ID. This is a compound (array) attribute, and returns an array view */
	template <typename ArrayType,
			  typename TEnableIf<TIsArrayView<ArrayType>::Value, int>::Type = 0,
			  typename AttributeType = typename TBreakArrayView<ArrayType>::Type>
	TArrayView<AttributeType> GetAttribute(const ElementIDType ElementID, const FName AttributeName, const int32 AttributeChannel = 0) const
	{
		const FMeshAttributeArraySetBase* ArraySetPtr = this->Map.FindChecked(AttributeName).Get();
		check(ArraySetPtr->HasType<AttributeType>());
		const AttributeType* ElementBase = static_cast<const TMeshAttributeArraySet<AttributeType>*>(ArraySetPtr)->GetArrayForChannel(AttributeChannel).GetElementBase(ElementID.GetValue());
		return TArrayView<AttributeType>(ElementBase[0], ArraySetPtr->GetExtent());
	}


	/**
	 * Set an attribute value for the given element ID.
	 * Note: it is generally preferable to get a TMeshAttributesRef and set multiple elements through that.
	 */
	template <typename AttributeType,
			  typename TEnableIf<!TIsArrayView<AttributeType>::Value, int>::Type = 0>
	void SetAttribute(const ElementIDType ElementID, const FName AttributeName, const int32 AttributeChannel, const AttributeType& AttributeValue)
	{
		FMeshAttributeArraySetBase* ArraySetPtr = this->Map.FindChecked(AttributeName).Get();
		check(ArraySetPtr->HasType<AttributeType>());
		check(ArraySetPtr->GetExtent() == 1);
		AttributeType* ElementBase = static_cast<TMeshAttributeArraySet<AttributeType>*>(ArraySetPtr)->GetArrayForChannel(AttributeChannel).GetElementBase(ElementID.GetValue());
		ElementBase[0] = AttributeValue;
	}

	template <typename ArrayType,
			  typename TEnableIf<TIsArrayView<ArrayType>::Value, int>::Type = 0,
			  typename AttributeType = typename TRemoveCV<typename TBreakArrayView<ArrayType>::Type>::Type>
	void SetAttribute(const ElementIDType ElementID, const FName AttributeName, const int32 AttributeChannel, const ArrayType& AttributeValue)
	{
		FMeshAttributeArraySetBase* ArraySetPtr = this->Map.FindChecked(AttributeName).Get();
		check(ArraySetPtr->HasType<AttributeType>());
		check(ArraySetPtr->GetExtent() == AttributeValue.Num());
		AttributeType* ElementBase = static_cast<TMeshAttributeArraySet<AttributeType>*>(ArraySetPtr)->GetArrayForChannel(AttributeChannel).GetElementBase(ElementID.GetValue());
		for (int32 I = 0; I < AttributeValue.Num(); I++)
		{
			ElementBase[I] = AttributeValue[I];
		}
	}

	/** Inserts a default-initialized value for all attributes of the given ID */
	FORCEINLINE void Insert(const ElementIDType ElementID)
	{
		this->Insert(ElementID.GetValue());
	}

	/** Removes all attributes with the given ID */
	FORCEINLINE void Remove(const ElementIDType ElementID)
	{
		this->Remove(ElementID.GetValue());
	}

	/**
	 * Call the supplied function on each attribute.
	 * The prototype should be Func( const FName AttributeName, auto AttributesRef );
	 */
	template <typename ForEachFunc> void ForEach(ForEachFunc Func);

	/**
	* Call the supplied function on each attribute.
	* The prototype should be Func( const FName AttributeName, auto AttributesConstRef );
	*/
	template <typename ForEachFunc> void ForEach(ForEachFunc Func) const;
};


/**
 * We need a mechanism by which we can iterate all items in the attribute map and perform an arbitrary operation on each.
 * We require polymorphic behavior, as attribute arrays are templated on their attribute type, and derived from a generic base class.
 * However, we cannot have a virtual templated method, so we use a different approach.
 *
 * Effectively, we wish to cast the attribute array depending on the type member of the base class as we iterate through the map.
 * This might look something like this:
 *
 *    template <typename FuncType>
 *    void ForEach(FuncType Func)
 *    {
 *        for (const auto& MapEntry : Map)
 *        {
 *            const uint32 Type = MapEntry.Value->GetType();
 *            switch (Type)
 *            {
 *                case 0: Func(static_cast<TMeshAttributeArraySet<FVector>*>(MapEntry.Value.Get()); break;
 *                case 1: Func(static_cast<TMeshAttributeArraySet<FVector4>*>(MapEntry.Value.Get()); break;
 *                case 2: Func(static_cast<TMeshAttributeArraySet<FVector2D>*>(MapEntry.Value.Get()); break;
 *                case 3: Func(static_cast<TMeshAttributeArraySet<float>*>(MapEntry.Value.Get()); break;
 *                      ....
 *            }
 *        }
 *    }
 *
 * (The hope is that the compiler would optimize the switch into a jump table so we get O(1) dispatch even as the number of attribute types
 * increases.)
 *
 * The approach taken here is to generate a jump table at compile time, one entry per possible attribute type.
 * The function Dispatch(...) is the actual function which gets called.
 * MakeJumpTable() is the constexpr function which creates a static jump table at compile time.
 */
namespace ForEachImpl
{
	// Declare type of jump table used to dispatch functions
	template <typename ElementIDType, typename ForEachFunc>
	using JumpTableType = TJumpTable<void(FName, ForEachFunc, FMeshAttributeArraySetBase*), TTupleArity<AttributeTypes>::Value>;

	// Define dispatch function
	template <typename ElementIDType, typename ForEachFunc, uint32 I>
	static void Dispatch(FName Name, ForEachFunc Fn, FMeshAttributeArraySetBase* Attributes)
	{
		using AttributeType = typename TTupleElement<I, AttributeTypes>::Type;
		if (Attributes->GetExtent() == 1)
		{
			Fn(Name, TMeshAttributesRef<ElementIDType, AttributeType>(static_cast<TMeshAttributeArraySet<AttributeType>*>(Attributes)));
		}
		else
		{
			// @todo: allow ForEach to iterate through array types
//			Fn(Name, TMeshAttributesRef<ElementIDType, TArrayView<AttributeType>(static_cast<TMeshAttributeArraySet<AttributeType>*>(Attributes), Attributes->GetExtent()));
		}
	}

	// Build ForEach jump table at compile time, a separate instantiation of Dispatch for each attribute type
	template <typename ElementIDType, typename ForEachFunc, uint32... Is>
	static constexpr JumpTableType<ElementIDType, ForEachFunc> MakeJumpTable(TIntegerSequence< uint32, Is...>)
	{
		return JumpTableType<ElementIDType, ForEachFunc>(Dispatch<ElementIDType, ForEachFunc, Is>...);
	}
}

template <typename ElementIDType>
template <typename ForEachFunc>
void TAttributesSet<ElementIDType>::ForEach(ForEachFunc Func)
{
	// Construct compile-time jump table for dispatching ForEachImpl::Dispatch() by the attribute type at runtime
	static constexpr ForEachImpl::JumpTableType<ElementIDType, ForEachFunc>
		JumpTable = ForEachImpl::MakeJumpTable<ElementIDType, ForEachFunc>(TMakeIntegerSequence<uint32, TTupleArity<AttributeTypes>::Value>());

	for (auto& MapEntry : this->Map)
	{
		const uint32 Type = MapEntry.Value->GetType();
		JumpTable.Fns[Type](MapEntry.Key, Func, MapEntry.Value.Get());
	}
}

namespace ForEachConstImpl
{
	// Declare type of jump table used to dispatch functions
	template <typename ElementIDType, typename ForEachFunc>
	using JumpTableType = TJumpTable<void(FName, ForEachFunc, const FMeshAttributeArraySetBase*), TTupleArity<AttributeTypes>::Value>;

	// Define dispatch function
	template <typename ElementIDType, typename ForEachFunc, uint32 I>
	static void Dispatch(FName Name, ForEachFunc Fn, const FMeshAttributeArraySetBase* Attributes)
	{
		using AttributeType = typename TTupleElement<I, AttributeTypes>::Type;
		if (Attributes->GetExtent() == 1)
		{
			Fn(Name, TMeshAttributesConstRef<ElementIDType, AttributeType>(static_cast<const TMeshAttributeArraySet<AttributeType>*>(Attributes)));
		}
		else
		{
			// @todo: allow ForEach to iterate through array types
//			Fn(Name, TMeshAttributesConstRef<ElementIDType, TArrayView<const AttributeType>>(static_cast<const TMeshAttributeArraySet<AttributeType>*>(Attributes), Attributes->GetExtent()));
		}
	}

	// Build ForEach jump table at compile time, a separate instantiation of Dispatch for each attribute type
	template <typename ElementIDType, typename ForEachFunc, uint32... Is>
	static constexpr JumpTableType<ElementIDType, ForEachFunc> MakeJumpTable(TIntegerSequence< uint32, Is...>)
	{
		return JumpTableType<ElementIDType, ForEachFunc>(Dispatch<ElementIDType, ForEachFunc, Is>...);
	}
}

template <typename ElementIDType>
template <typename ForEachFunc>
void TAttributesSet<ElementIDType>::ForEach(ForEachFunc Func) const
{
	// Construct compile-time jump table for dispatching ForEachImpl::Dispatch() by the attribute type at runtime
	static constexpr ForEachConstImpl::JumpTableType<ElementIDType, ForEachFunc>
		JumpTable = ForEachConstImpl::MakeJumpTable<ElementIDType, ForEachFunc>(TMakeIntegerSequence<uint32, TTupleArity<AttributeTypes>::Value>());

	for (const auto& MapEntry : this->Map)
	{
		const uint32 Type = MapEntry.Value->GetType();
		JumpTable.Fns[Type](MapEntry.Key, Func, MapEntry.Value.Get());
	}
}

/**
 * This is a similar approach to ForEach, above.
 * Given a type index, at runtime, we wish to create an attribute array of the corresponding type; essentially a factory.
 *
 * We generate a jump table at compile time, containing generated functions to register attributes of each type.
 */
namespace CreateTypeImpl
{
	// Declare type of jump table used to dispatch functions
	using JumpTableType = TJumpTable<TUniquePtr<FMeshAttributeArraySetBase>(uint32), TTupleArity<AttributeTypes>::Value>;

	// Define dispatch function
	template <uint32 I>
	static TUniquePtr<FMeshAttributeArraySetBase> Dispatch(uint32 Extent)
	{
		using AttributeType = typename TTupleElement<I, AttributeTypes>::Type;
		return MakeUnique<TMeshAttributeArraySet<AttributeType>>(Extent);
	}

	// Build RegisterAttributeOfType jump table at compile time, a separate instantiation of Dispatch for each attribute type
	template <uint32... Is>
	static constexpr JumpTableType MakeJumpTable(TIntegerSequence< uint32, Is...>)
	{
		return JumpTableType(Dispatch<Is>...);
	}
}

inline void FAttributesSetEntry::CreateArrayOfType(const uint32 Type, const uint32 Extent)
{
	static constexpr CreateTypeImpl::JumpTableType JumpTable = CreateTypeImpl::MakeJumpTable(TMakeIntegerSequence<uint32, TTupleArity<AttributeTypes>::Value>());
	Ptr = JumpTable.Fns[Type](Extent);
}


/**
 * Helper struct which determines whether ViewType and the I'th type from AttributeTypes are mutually constructible from each other.
 */
template <typename ViewType, uint32 I>
struct TIsViewable
{
	enum { Value = TIsConstructible<ViewType, typename TTupleElement<I, AttributeTypes>::Type>::Value &&
				   TIsConstructible<typename TTupleElement<I, AttributeTypes>::Type, ViewType>::Value };
};

/**
 * Implementation for TMeshAttributesConstViewBase::Get(ElementIndex).
 *
 * This is implemented similarly to the above. A jump table is built, so the correct implementation is dispatched according to the array type.
 * This cannot be a regular virtual function because the return type depends on the array type.
 */
namespace AttributesViewGetImpl
{
	// Declare type of jump table used to dispatch functions
	template <typename ViewType>
	using JumpTableType = TJumpTable<ViewType( const FMeshAttributeArraySetBase*, int32 ), TTupleArity<AttributeTypes>::Value>;

	// Define dispatch functions
	template <typename ViewType, uint32 I, typename TEnableIf<TIsViewable<ViewType, I>::Value, int>::Type = 0>
	static ViewType Dispatch( const FMeshAttributeArraySetBase* Array, const int32 Index )
	{
		// Implementation when the attribute type is convertible to the view type
		return ViewType( static_cast<const TMeshAttributeArraySet<typename TTupleElement<I, AttributeTypes>::Type>*>( Array )->GetArrayForChannel( 0 )[ Index ] );
	}

	template <typename ViewType, uint32 I, typename TEnableIf<!TIsViewable<ViewType, I>::Value, int>::Type = 0>
	static ViewType Dispatch( const FMeshAttributeArraySetBase* Array, const int32 Index )
	{
		// Implementation when the attribute type is not convertible to the view type
		check( false );
		return ViewType();
	}

	// Build jump table at compile time, a separate instantiation of Dispatch for each view type
	template <typename ViewType, uint32... Is>
	static constexpr JumpTableType<ViewType> MakeJumpTable( TIntegerSequence< uint32, Is...> )
	{
		return JumpTableType<ViewType>( Dispatch<ViewType, Is>... );
	}
}

template <typename ViewType>
FORCEINLINE ViewType TMeshAttributesViewBase<ViewType>::GetByIndex( const int32 ElementIndex ) const
{
	static constexpr AttributesViewGetImpl::JumpTableType<ViewType>
		JumpTable = AttributesViewGetImpl::MakeJumpTable<ViewType>( TMakeIntegerSequence<uint32, TTupleArity<AttributeTypes>::Value>() );

	return JumpTable.Fns[ ArrayPtr->GetType() ]( ArrayPtr, ElementIndex );
}


/**
 * Implementation for TMeshAttributesConstViewBase::Get(ElementIndex, AttributeIndex).
 */
namespace AttributesViewGetWithIndexImpl
{
	// Declare type of jump table used to dispatch functions
	template <typename ViewType>
	using JumpTableType = TJumpTable<ViewType( const FMeshAttributeArraySetBase*, int32, int32 ), TTupleArity<AttributeTypes>::Value>;

	// Define dispatch functions
	template <typename ViewType, uint32 I, typename TEnableIf<TIsViewable<ViewType, I>::Value, int>::Type = 0>
	static ViewType Dispatch( const FMeshAttributeArraySetBase* Array, const int32 ElementIndex, const int32 AttributeIndex )
	{
		return ViewType( static_cast<const TMeshAttributeArraySet<typename TTupleElement<I, AttributeTypes>::Type>*>( Array )->GetArrayForChannel( AttributeIndex )[ ElementIndex ] );
	}

	template <typename ViewType, uint32 I, typename TEnableIf<!TIsViewable<ViewType, I>::Value, int>::Type = 0>
	static ViewType Dispatch( const FMeshAttributeArraySetBase* Array, const int32 ElementIndex, const int32 AttributeIndex )
	{
		check( false );
		return ViewType();
	}

	// Build jump table at compile time, a separate instantiation of Dispatch for each attribute type
	template <typename ViewType, uint32... Is>
	static constexpr JumpTableType<ViewType> MakeJumpTable( TIntegerSequence< uint32, Is...> )
	{
		return JumpTableType<ViewType>( Dispatch<ViewType, Is>... );
	}
}

template <typename ViewType>
FORCEINLINE ViewType TMeshAttributesViewBase<ViewType>::GetByIndex( const int32 ElementIndex, const int32 AttributeIndex ) const
{
	static constexpr AttributesViewGetWithIndexImpl::JumpTableType<ViewType>
		JumpTable = AttributesViewGetWithIndexImpl::MakeJumpTable<ViewType>( TMakeIntegerSequence<uint32, TTupleArity<AttributeTypes>::Value>() );

	return JumpTable.Fns[ ArrayPtr->GetType() ]( ArrayPtr, ElementIndex, AttributeIndex );
}


/**
 * Implementation for TMeshAttributesViewBase::Set(ElementIndex, Value).
 */
namespace AttributesViewSetImpl
{
	// Declare type of jump table used to dispatch functions
	template <typename ViewType>
	using JumpTableType = TJumpTable<void( FMeshAttributeArraySetBase*, int32, const ViewType& ), TTupleArity<AttributeTypes>::Value>;

	// Define dispatch functions
	template <typename ViewType, uint32 I, typename TEnableIf<TIsViewable<ViewType, I>::Value, int>::Type = 0>
	static void Dispatch( FMeshAttributeArraySetBase* Array, const int32 Index, const ViewType& Value )
	{
		// Implementation when the attribute type is convertible to the view type
		using AttributeType = typename TTupleElement<I, AttributeTypes>::Type;
		static_cast<TMeshAttributeArraySet<AttributeType>*>( Array )->GetArrayForChannel( 0 )[ Index ] = AttributeType( Value );
	}

	template <typename ViewType, uint32 I, typename TEnableIf<!TIsViewable<ViewType, I>::Value, int>::Type = 0>
	static void Dispatch( FMeshAttributeArraySetBase* Array, const int32 Index, const ViewType& Value )
	{
		// Implementation when the attribute type is not convertible to the view type
		check( false );
	}

	// Build jump table at compile time, a separate instantiation of Dispatch for each view type
	template <typename ViewType, uint32... Is>
	static constexpr JumpTableType<ViewType> MakeJumpTable( TIntegerSequence< uint32, Is...> )
	{
		return JumpTableType<ViewType>( Dispatch<ViewType, Is>... );
	}
}

template <typename ViewType>
FORCEINLINE void TMeshAttributesViewBase<ViewType>::SetByIndex( const int32 ElementIndex, const ViewType& Value ) const
{
	static constexpr AttributesViewSetImpl::JumpTableType<ViewType>
		JumpTable = AttributesViewSetImpl::MakeJumpTable<ViewType>( TMakeIntegerSequence<uint32, TTupleArity<AttributeTypes>::Value>() );

	JumpTable.Fns[ ArrayPtr->GetType() ]( ArrayPtr, ElementIndex, Value );
}


/**
 * Implementation for TMeshAttributesViewBase::Set(ElementIndex, AttributeIndex, Value).
 */
namespace AttributesViewSetWithIndexImpl
{
	// Declare type of jump table used to dispatch functions
	template <typename ViewType>
	using JumpTableType = TJumpTable<void( FMeshAttributeArraySetBase*, int32, int32, const ViewType& ), TTupleArity<AttributeTypes>::Value>;

	// Define dispatch functions
	template <typename ViewType, uint32 I, typename TEnableIf<TIsViewable<ViewType, I>::Value, int>::Type = 0>
	static void Dispatch( FMeshAttributeArraySetBase* Array, const int32 ElementIndex, const int32 AttributeIndex, const ViewType& Value )
	{
		// Implementation when the attribute type is convertible to the view type
		using AttributeType = typename TTupleElement<I, AttributeTypes>::Type;
		static_cast<TMeshAttributeArraySet<AttributeType>*>( Array )->GetArrayForChannel( AttributeIndex )[ ElementIndex ] = AttributeType( Value );
	}

	template <typename ViewType, uint32 I, typename TEnableIf<!TIsViewable<ViewType, I>::Value, int>::Type = 0>
	static void Dispatch( FMeshAttributeArraySetBase* Array, const int32 ElementIndex, const int32 AttributeIndex, const ViewType& Value )
	{
		// Implementation when the attribute type is not convertible to the view type
		check( false );
	}

	// Build jump table at compile time, a separate instantiation of Dispatch for each view type
	template <typename ViewType, uint32... Is>
	static constexpr JumpTableType<ViewType> MakeJumpTable( TIntegerSequence< uint32, Is...> )
	{
		return JumpTableType<ViewType>( Dispatch<ViewType, Is>... );
	}
}

template <typename ViewType>
FORCEINLINE void TMeshAttributesViewBase<ViewType>::SetByIndex( const int32 ElementIndex, const int32 AttributeIndex, const ViewType& Value ) const
{
	static constexpr AttributesViewSetWithIndexImpl::JumpTableType<ViewType>
		JumpTable = AttributesViewSetWithIndexImpl::MakeJumpTable<ViewType>( TMakeIntegerSequence<uint32, TTupleArity<AttributeTypes>::Value>() );

	JumpTable.Fns[ ArrayPtr->GetType() ]( ArrayPtr, ElementIndex, AttributeIndex, Value );
}


/**
 * Implementation for TMeshAttributesViewBase::GetDefaultValue().
 */
namespace AttributesViewGetDefaultImpl
{
	// Declare type of jump table used to dispatch functions
	template <typename ViewType>
	using JumpTableType = TJumpTable<ViewType( const FMeshAttributeArraySetBase* ), TTupleArity<AttributeTypes>::Value>;

	// Define dispatch functions
	template <typename ViewType, uint32 I, typename TEnableIf<TIsViewable<ViewType, I>::Value, int>::Type = 0>
	static ViewType Dispatch( const FMeshAttributeArraySetBase* Array )
	{
		// Implementation when the attribute type is convertible to the view type
		return ViewType( static_cast<const TMeshAttributeArraySet<typename TTupleElement<I, AttributeTypes>::Type>*>( Array )->GetDefaultValue() );
	}

	template <typename ViewType, uint32 I, typename TEnableIf<!TIsViewable<ViewType, I>::Value, int>::Type = 0>
	static ViewType Dispatch( const FMeshAttributeArraySetBase* Array )
	{
		// Implementation when the attribute type is not convertible to the view type
		check( false );
		return ViewType();
	}

	// Build jump table at compile time, a separate instantiation of Dispatch for each view type
	template <typename ViewType, uint32... Is>
	static constexpr JumpTableType<ViewType> MakeJumpTable( TIntegerSequence< uint32, Is...> )
	{
		return JumpTableType<ViewType>( Dispatch<ViewType, Is>... );
	}
}

template <typename ViewType>
FORCEINLINE ViewType TMeshAttributesViewBase<ViewType>::GetDefaultValue() const
{
	static constexpr AttributesViewGetDefaultImpl::JumpTableType<ViewType>
		JumpTable = AttributesViewGetDefaultImpl::MakeJumpTable<ViewType>( TMakeIntegerSequence<uint32, TTupleArity<AttributeTypes>::Value>() );

	return JumpTable.Fns[ ArrayPtr->GetType() ]( ArrayPtr );
}
