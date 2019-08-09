// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Serialization/StructuredArchiveFormatter.h"
#include "Formatters/BinaryArchiveFormatter.h"
#include "Misc/Optional.h"
#include "Concepts/Insertable.h"
#include "Concepts/Serializable.h"
#include "Templates/Models.h"
#include "Containers/Array.h"
#include "Serialization/ArchiveProxy.h"
#include "Templates/UniqueObj.h"

/**
 * Class to contain a named value for serialization. Intended to be created as a temporary and passed to object serialization methods.
 */
template<typename T> struct TNamedValue
{
	FArchiveFieldName Name;
	T& Value;

	FORCEINLINE TNamedValue(FArchiveFieldName InName, T& InValue)
		: Name(InName)
		, Value(InValue)
	{
	}
};

/**
 * Class to contain a named attribute for serialization. Intended to be created as a temporary and passed to object serialization methods.
 */
template<typename T> struct TNamedAttribute
{
	FArchiveFieldName Name;
	T& Value;

	explicit FORCEINLINE TNamedAttribute(FArchiveFieldName InName, T& InValue)
		: Name(InName)
		, Value(InValue)
	{
	}
};

/**
 * Class to contain a named attribute for serialization, with a default. Intended to be created as a temporary and passed to object
 * serialization methods, which can choose not to serialize the attribute if it matches the default.
 */
template<typename T> struct TDefaultedNamedAttribute
{
	FArchiveFieldName Name;
	T& Value;
	const T& Default;

	explicit FORCEINLINE TDefaultedNamedAttribute(FArchiveFieldName InName, T& InValue, const T& InDefault)
		: Name(InName)
		, Value(InValue)
		, Default(InDefault)
	{
	}
};

/**
 * Helper function to construct a TNamedValue, deducing the value type.
 */
template<typename T> FORCEINLINE TNamedValue<T> MakeNamedValue(FArchiveFieldName Name, T& Value)
{
	return TNamedValue<T>(Name, Value);
}

/**
 * Helper function to construct a TNamedAttribute, deducing the value type.
 */
template<typename T> FORCEINLINE TNamedAttribute<T> MakeNamedAttribute(FArchiveFieldName Name, T& Value)
{
	return TNamedAttribute<T>(Name, Value);
}

/**
 * Helper function to construct a TDefaultedNamedAttribute, deducing the value type.
 */
template<typename T> FORCEINLINE TDefaultedNamedAttribute<T> MakeDefaultedNamedAttribute(FArchiveFieldName Name, T& Value, const typename TIdentity<T>::Type& Default)
{
	return TDefaultedNamedAttribute<T>(Name, Value, Default);
}

/** Construct a TNamedValue given an ANSI string and value reference. */
#if WITH_TEXT_ARCHIVE_SUPPORT
	#define SA_VALUE(Name, Value) MakeNamedValue(FArchiveFieldName(Name), Value)
#else
	#define SA_VALUE(Name, Value) MakeNamedValue(FArchiveFieldName(), Value)
#endif

/** Construct a TNamedAttribute given an ANSI string and value reference. */
#if WITH_TEXT_ARCHIVE_SUPPORT
	#define SA_ATTRIBUTE(Name, Value) MakeNamedAttribute(FArchiveFieldName(Name), Value)
#else
	#define SA_ATTRIBUTE(Name, Value) MakeNamedAttribute(FArchiveFieldName(), Value)
#endif

/** Construct a TDefaultedNamedAttribute given an ANSI string and value reference. */
#if WITH_TEXT_ARCHIVE_SUPPORT
	#define SA_DEFAULTED_ATTRIBUTE(Name, Value, Default) MakeDefaultedNamedAttribute(FArchiveFieldName(Name), Value, Default)
#else
	#define SA_DEFAULTED_ATTRIBUTE(Name, Value) MakeNamedAttribute(FArchiveFieldName(), Value)
#endif

/** Typedef for which formatter type to support */
#if WITH_TEXT_ARCHIVE_SUPPORT
	typedef FStructuredArchiveFormatter FArchiveFormatterType;
#else
	typedef FBinaryArchiveFormatter FArchiveFormatterType;
#endif

/**
 * Manages the state of an underlying FStructuredArchiveFormatter, and provides a consistent API for reading and writing to a structured archive.
 * 
 * Both reading and writing to the archive are *forward only* from an interface point of view. There is no point at which it is possible to 
 * require seeking.
 */
class CORE_API FStructuredArchive
{
public:
	class FSlot;
	class FRecord;
	class FArray;
	class FStream;
	class FMap;

	/**
	 * Constructor.
	 *
	 * @param InFormatter Formatter for the archive data
	 */
	FStructuredArchive(FArchiveFormatterType& InFormatter);
	
	/**
	 * Default destructor. Closes the archive.
	 */
	~FStructuredArchive();

	/**
	 * Start writing to the archive, and gets an interface to the root slot.
	 */
	FSlot Open();

	/**
	 * Flushes any remaining scope to the underlying formatter and closes the archive.
	 */
	void Close();

	/**
	 * Gets the serialization context from the underlying archive.
	 */
	FORCEINLINE FArchive& GetUnderlyingArchive()
	{
		return Formatter.GetUnderlyingArchive();
	}

	FStructuredArchive(const FStructuredArchive&) = delete;
	FStructuredArchive& operator=(const FStructuredArchive&) = delete;

private:
	class CORE_API FSlotBase
	{
	public:
		FORCEINLINE FArchive& GetUnderlyingArchive()
		{
			return Ar.GetUnderlyingArchive();
		}

	protected:
		FStructuredArchive& Ar;
#if WITH_TEXT_ARCHIVE_SUPPORT
		const int32 Depth;
		const int32 ElementId;

		FORCEINLINE explicit FSlotBase(FStructuredArchive& InAr, int32 InDepth, int32 InElementId)
			: Ar(InAr)
			, Depth(InDepth)
			, ElementId(InElementId)
		{
		}
#else
		FORCEINLINE FSlotBase(FStructuredArchive& InAr)
			: Ar(InAr)
		{
		}
#endif
	};

public:
	/**
	 * Contains a value in the archive; either a field or array/map element. A slot does not know its name or location,
	 * and can merely have a value serialized into it. That value may be a literal (eg. int, float) or compound object
	 * (eg. object, array, map).
	 */
	class CORE_API FSlot final : public FSlotBase
	{
	public:
		FRecord EnterRecord();
		FRecord EnterRecord_TextOnly(TArray<FString>& OutFieldNames);
		FArray EnterArray(int32& Num);
		FStream EnterStream();
		FStream EnterStream_TextOnly(int32& OutNumElements);
		FMap EnterMap(int32& Num);
		FSlot EnterAttribute(FArchiveFieldName AttributeName);
		TOptional<FSlot> TryEnterAttribute(FArchiveFieldName AttributeName, bool bEnterWhenWriting);

		// We don't support chaining writes to a single slot, so this returns void.
		template <typename ArgType>
		typename TEnableIf<
			TModels<CSerializable<FArchiveFormatterType>, ArgType>::Value
		>::Type operator<<(ArgType&& Arg)
		{
#if WITH_TEXT_ARCHIVE_SUPPORT
			Ar.EnterSlot(Depth, ElementId);
			Ar.Formatter.Serialize(Forward<ArgType>(Arg));
			Ar.LeaveSlot();
#else
			Ar.Formatter.Serialize(Forward<ArgType>(Arg));
#endif
		}

		template <typename T>
		FORCEINLINE void operator<<(TNamedAttribute<T> Item)
		{
			EnterAttribute(Item.Name) << Item.Value;
		}

		template <typename T>
		FORCEINLINE void operator<<(TDefaultedNamedAttribute<T> Item)
		{
			if (TOptional<FSlot> Attribute = TryEnterAttribute(Item.Name, Item.Value != Item.Default))
			{
				Attribute.GetValue() << Item.Value;
			}
			else
			{
				Item.Value = Item.Default;
			}
		}

		void Serialize(TArray<uint8>& Data);
		void Serialize(void* Data, uint64 DataSize);

		FORCEINLINE bool IsFilled() const
		{
#if WITH_TEXT_ARCHIVE_SUPPORT
			return Ar.CurrentSlotElementId != ElementId;
#else
			return true;
#endif
		}

	private:
		friend FStructuredArchive;
		friend class FStructuredArchiveChildReader;

		using FSlotBase::FSlotBase;
	};

	/**
	 * Represents a record in the structured archive. An object contains slots that are identified by FArchiveName,
	 * which may be compiled out with binary-only archives.
	 */
	class CORE_API FRecord final : public FSlotBase
	{
	public:
		FSlot EnterField(FArchiveFieldName Name);
		FSlot EnterField_TextOnly(FArchiveFieldName Name, EArchiveValueType& OutType);
		FRecord EnterRecord(FArchiveFieldName Name);
		FRecord EnterRecord_TextOnly(FArchiveFieldName Name, TArray<FString>& OutFieldNames);
		FArray EnterArray(FArchiveFieldName Name, int32& Num);
		FStream EnterStream(FArchiveFieldName Name);
		FStream EnterStream_TextOnly(FArchiveFieldName Name, int32& OutNumElements);
		FMap EnterMap(FArchiveFieldName Name, int32& Num);

		TOptional<FSlot> TryEnterField(FArchiveFieldName Name, bool bEnterForSaving);

		template<typename T> FORCEINLINE FRecord& operator<<(TNamedValue<T> Item)
		{
			EnterField(Item.Name) << Item.Value;
			return *this;
		}

	private:
		friend FStructuredArchive;

		using FSlotBase::FSlotBase;
	};

	/**
	 * Represents an array in the structured archive. An object contains slots that are identified by a FArchiveFieldName,
	 * which may be compiled out with binary-only archives.
	 */
	class CORE_API FArray final : public FSlotBase
	{
	public:
		FSlot EnterElement();
		FSlot EnterElement_TextOnly(EArchiveValueType& OutType);

		template<typename T> FORCEINLINE FArray& operator<<(T& Item)
		{
			EnterElement() << Item;
			return *this;
		}

	private:
		friend FStructuredArchive;

		using FSlotBase::FSlotBase;
	};

	/**
	 * Represents an unsized sequence of slots in the structured archive (similar to an array, but without a known size).
	 */
	class CORE_API FStream final : public FSlotBase
	{
	public:
		FSlot EnterElement();
		FSlot EnterElement_TextOnly(EArchiveValueType& OutType);

		template<typename T> FORCEINLINE FStream& operator<<(T& Item)
		{
			EnterElement() << Item;
			return *this;
		}

	private:
		friend FStructuredArchive;

		using FSlotBase::FSlotBase;
	};

	/**
	 * Represents a map in the structured archive. A map is similar to a record, but keys can be read back out from an archive.
	 * (This is an important distinction for binary archives).
	 */
	class CORE_API FMap final : public FSlotBase
	{
	public:
		FSlot EnterElement(FString& Name);
		FSlot EnterElement_TextOnly(FString& Name, EArchiveValueType& OutType);

	private:
		friend FStructuredArchive;

		using FSlotBase::FSlotBase;
	};

private:

	friend class FStructuredArchiveChildReader;

	/**
	* Reference to the formatter that actually writes out the data to the underlying archive
	*/
	FArchiveFormatterType& Formatter;

#if WITH_TEXT_ARCHIVE_SUPPORT
	/**
	 * Whether the formatter requires structural metadata. This allows optimizing the path for binary archives in editor builds.
	 */
	const bool bRequiresStructuralMetadata;

	enum class EElementType : unsigned char
	{
		Root,
		Record,
		Array,
		Stream,
		Map,
		AttributedValue,
	};

	enum class EEnteringAttributeState
	{
		NotEnteringAttribute,
		EnteringAttribute,
	};

	struct FElement
	{
		int Id;
		EElementType Type;

		FElement(int InId, EElementType InType)
			: Id(InId)
			, Type(InType)
		{
		}
	};

#if DO_GUARD_SLOW
	struct FContainer;
#endif

	/**
	 * The next element id to be assigned
	 */
	int NextElementId;

	/**
	 * The element ID assigned for the current slot. Slots are transient, and only exist as placeholders until something is written into them. This is reset to INDEX_NONE when something is created in a slot, and the created item can assume the element id.
	 */
	int CurrentSlotElementId;

	/**
	 * Tracks the current stack of objects being written. Used by SetScope() to ensure that scopes are always closed correctly in the underlying formatter,
	 * and to make sure that the archive is always written in a forwards-only way (ie. writing to an element id that is not in scope will assert)
	 */
	TArray<FElement, TNonRelocatableInlineAllocator<32>> CurrentScope;

#if DO_GUARD_SLOW
	/**
	 * For arrays and maps, stores the loop counter and size of the container. Also stores key names for records and maps in builds with DO_GUARD_SLOW enabled.
	 */
	TArray<FContainer*> CurrentContainer;
#endif

	/**
	 * Whether or not we've just entered an attribute
	 */
	EEnteringAttributeState CurrentEnteringAttributeState = EEnteringAttributeState::NotEnteringAttribute;

	/**
	 * Enters the current slot for serializing a value. Asserts if the archive is not in a state about to write to an empty-slot.
	 */
	void EnterSlot(int32 ParentDepth, int32 ElementId);

	/**
	 * Enters the current slot, adding an element onto the stack. Asserts if the archive is not in a state about to write to an empty-slot.
	 *
	 * @return  The depth of the newly-entered slot.
	 */
	int32 EnterSlot(int32 ParentDepth, int32 ElementId, EElementType ElementType);

	/**
	 * Leaves slot at the top of the current scope
	 */
	void LeaveSlot();

	/**
	 * Switches to the scope at the given element id, updating the formatter state as necessary
	 */
	void SetScope(int32 Depth, int32 ElementId);
#endif
};

template <typename T>
FORCEINLINE_DEBUGGABLE void operator<<(FStructuredArchive::FSlot Slot, TArray<T>& InArray)
{
	int32 NumElements = InArray.Num();
	FStructuredArchive::FArray Array = Slot.EnterArray(NumElements);

	if (Slot.GetUnderlyingArchive().IsLoading())
	{
		InArray.SetNum(NumElements);
	}

	for (int32 ElementIndex = 0; ElementIndex < NumElements; ++ElementIndex)
	{
		FStructuredArchive::FSlot ElementSlot = Array.EnterElement();
		ElementSlot << InArray[ElementIndex];
	}
}

template <>
FORCEINLINE_DEBUGGABLE void operator<<(FStructuredArchive::FSlot Slot, TArray<uint8>& InArray)
{
	Slot.Serialize(InArray);
}

/**
 * FStructuredArchiveChildReader
 *
 * Utility class for easily creating a structured archive that covers the data hierarchy underneath
 * the given slot
 *
 * Allows serialization code to get an archive instance for the current location, so that it can return to it
 * later on after the master archive has potentially moved on into a different location in the file.
 */
class CORE_API FStructuredArchiveChildReader
{
public:

	FStructuredArchiveChildReader(FStructuredArchive::FSlot InSlot);
	~FStructuredArchiveChildReader();

	FORCEINLINE FStructuredArchive::FSlot GetRoot() { return Root.GetValue(); }

private:

	FStructuredArchiveFormatter* OwnedFormatter;
	FStructuredArchive* Archive;
	TOptional<FStructuredArchive::FSlot> Root;
};

class CORE_API FStructuredArchiveFromArchive
{
	UE_NONCOPYABLE(FStructuredArchiveFromArchive)

	struct FImpl;

public:
	explicit FStructuredArchiveFromArchive(FArchive& Ar);
	~FStructuredArchiveFromArchive();

	FStructuredArchive::FSlot GetSlot();

private:
	// Implmented as a pimpl in order to reduce dependencies
	TUniqueObj<FImpl> Pimpl;
};

#if WITH_TEXT_ARCHIVE_SUPPORT

class CORE_API FArchiveFromStructuredArchiveImpl : public FArchiveProxy
{
	UE_NONCOPYABLE(FArchiveFromStructuredArchiveImpl)

	struct FImpl;

public:
	explicit FArchiveFromStructuredArchiveImpl(FStructuredArchive::FSlot Slot);
	virtual ~FArchiveFromStructuredArchiveImpl();

	virtual void Flush() override;
	virtual bool Close() override;

	virtual int64 Tell() override;
	virtual int64 TotalSize() override;
	virtual void Seek(int64 InPos) override;
	virtual bool AtEnd() override;

	using FArchive::operator<<; // For visibility of the overloads we don't override

	//~ Begin FArchive Interface
	virtual FArchive& operator<<(class FName& Value) override;
	virtual FArchive& operator<<(class UObject*& Value) override;
	virtual FArchive& operator<<(class FText& Value) override;
	//~ End FArchive Interface

	virtual void Serialize(void* V, int64 Length) override;

	virtual FArchive* GetCacheableArchive() override;

	bool ContainsData() const;

protected:
	virtual void SerializeInternal(FStructuredArchive::FRecord Record);
	void OpenArchive();

private:
	void Commit();

	// Implmented as a pimpl in order to reduce dependencies
	TUniqueObj<FImpl> Pimpl;
};

class FArchiveFromStructuredArchive
{
public:
	explicit FArchiveFromStructuredArchive(FStructuredArchive::FSlot InSlot)
		: Impl(InSlot)
	{
	}

	      FArchive& GetArchive()       { return Impl; }
	const FArchive& GetArchive() const { return Impl; }

private:
	FArchiveFromStructuredArchiveImpl Impl;
};

#else

class FArchiveFromStructuredArchive
{
public:
	explicit FArchiveFromStructuredArchive(FStructuredArchive::FSlot InSlot)
		: Ar(InSlot.GetUnderlyingArchive())
	{
	}

	      FArchive& GetArchive()       { return Ar; }
	const FArchive& GetArchive() const { return Ar; }

private:
	FArchive& Ar;
};

#endif

/**
 * Adapter operator which allows a type to stream to an FArchive when it already supports streaming to an FStructuredArchive::FSlot.
 *
 * @param  Ar   The archive to read from or write to.
 * @param  Obj  The object to read or write.
 *
 * @return  A reference to the same archive as Ar.
 */
template <typename T>
typename TEnableIf<
	!TModels<CInsertable<FArchive&>, T>::Value && TModels<CInsertable<FStructuredArchive::FSlot>, T>::Value,
	FArchive&
>::Type operator<<(FArchive& Ar, T& Obj)
{
	FStructuredArchiveFromArchive ArAdapt(Ar);
	ArAdapt.GetSlot() << Obj;
	return Ar;
}

/**
 * Adapter operator which allows a type to stream to an FStructuredArchive::FSlot when it already supports streaming to an FArchive.
 *
 * @param  Slot  The slot to read from or write to.
 * @param  Obj   The object to read or write.
 */
template <typename T>
typename TEnableIf<
	TModels<CInsertable<FArchive&>, T>::Value &&
	!TModels<CInsertable<FStructuredArchive::FSlot>, T>::Value
>::Type operator<<(FStructuredArchive::FSlot Slot, T& Obj)
{
#if WITH_TEXT_ARCHIVE_SUPPORT
	FArchiveFromStructuredArchive Adapter(Slot);
	FArchive& Ar = Adapter.GetArchive();
#else
	FArchive& Ar = Slot.GetUnderlyingArchive();
#endif
	Ar << Obj;
}

#if !WITH_TEXT_ARCHIVE_SUPPORT
	FORCEINLINE FStructuredArchiveChildReader::FStructuredArchiveChildReader(FStructuredArchive::FSlot InSlot)
		: OwnedFormatter(nullptr)
		, Archive(nullptr)
	{
		Archive = new FStructuredArchive(InSlot.Ar.Formatter);
		Root.Emplace(Archive->Open());
	}

	FORCEINLINE FStructuredArchiveChildReader::~FStructuredArchiveChildReader()
	{
	}

	//////////// FStructuredArchive ////////////

	FORCEINLINE FStructuredArchive::FStructuredArchive(FArchiveFormatterType& InFormatter)
		: Formatter(InFormatter)
	{
	}

	FORCEINLINE FStructuredArchive::~FStructuredArchive()
	{
	}

	FORCEINLINE FStructuredArchive::FSlot FStructuredArchive::Open()
	{
		return FSlot(*this);
	}

	FORCEINLINE void FStructuredArchive::Close()
	{
	}

	//////////// FStructuredArchive::FSlot ////////////

	FORCEINLINE FStructuredArchive::FRecord FStructuredArchive::FSlot::EnterRecord()
	{
		return FRecord(Ar);
	}

	FORCEINLINE FStructuredArchive::FRecord FStructuredArchive::FSlot::EnterRecord_TextOnly(TArray<FString>& OutFieldNames)
	{
		Ar.Formatter.EnterRecord_TextOnly(OutFieldNames);
		return FRecord(Ar);
	}

	FORCEINLINE FStructuredArchive::FArray FStructuredArchive::FSlot::EnterArray(int32& Num)
	{
		Ar.Formatter.EnterArray(Num);
		return FArray(Ar);
	}

	FORCEINLINE FStructuredArchive::FStream FStructuredArchive::FSlot::EnterStream()
	{
		return FStream(Ar);
	}

	FORCEINLINE FStructuredArchive::FStream FStructuredArchive::FSlot::EnterStream_TextOnly(int32& OutNumElements)
	{
		Ar.Formatter.EnterStream_TextOnly(OutNumElements);
		return FStream(Ar);
	}

	FORCEINLINE FStructuredArchive::FMap FStructuredArchive::FSlot::EnterMap(int32& Num)
	{
		Ar.Formatter.EnterMap(Num);
		return FMap(Ar);
	}

	FORCEINLINE void FStructuredArchive::FSlot::Serialize(TArray<uint8>& Data)
	{
		Ar.Formatter.Serialize(Data);
	}

	FORCEINLINE void FStructuredArchive::FSlot::Serialize(void* Data, uint64 DataSize)
	{
		Ar.Formatter.Serialize(Data, DataSize);
	}

	//////////// FStructuredArchive::FRecord ////////////

	FORCEINLINE FStructuredArchive::FSlot FStructuredArchive::FRecord::EnterField(FArchiveFieldName Name)
	{
		return FSlot(Ar);
	}

	FORCEINLINE FStructuredArchive::FSlot FStructuredArchive::FRecord::EnterField_TextOnly(FArchiveFieldName Name, EArchiveValueType& OutType)
	{
		Ar.Formatter.EnterField_TextOnly(Name, OutType);
		return FSlot(Ar);
	}

	FORCEINLINE FStructuredArchive::FRecord FStructuredArchive::FRecord::EnterRecord(FArchiveFieldName Name)
	{
		return EnterField(Name).EnterRecord();
	}

	FORCEINLINE FStructuredArchive::FRecord FStructuredArchive::FRecord::EnterRecord_TextOnly(FArchiveFieldName Name, TArray<FString>& OutFieldNames)
	{
		return EnterField(Name).EnterRecord_TextOnly(OutFieldNames);
	}

	FORCEINLINE FStructuredArchive::FArray FStructuredArchive::FRecord::EnterArray(FArchiveFieldName Name, int32& Num)
	{
		return EnterField(Name).EnterArray(Num);
	}

	FORCEINLINE FStructuredArchive::FStream FStructuredArchive::FRecord::EnterStream(FArchiveFieldName Name)
	{
		return EnterField(Name).EnterStream();
	}

	FORCEINLINE FStructuredArchive::FMap FStructuredArchive::FRecord::EnterMap(FArchiveFieldName Name, int32& Num)
	{
		return EnterField(Name).EnterMap(Num);
	}

	FORCEINLINE TOptional<FStructuredArchive::FSlot> FStructuredArchive::FRecord::TryEnterField(FArchiveFieldName Name, bool bEnterWhenWriting)
	{
		if (Ar.Formatter.TryEnterField(Name, bEnterWhenWriting))
		{
			return TOptional<FStructuredArchive::FSlot>(FSlot(Ar));
		}
		else
		{
			return TOptional<FStructuredArchive::FSlot>();
		}
	}

	//////////// FStructuredArchive::FArray ////////////

	FORCEINLINE FStructuredArchive::FSlot FStructuredArchive::FArray::EnterElement()
	{
		return FSlot(Ar);
	}

	FORCEINLINE FStructuredArchive::FSlot FStructuredArchive::FArray::EnterElement_TextOnly(EArchiveValueType& OutType)
	{
		Ar.Formatter.EnterArrayElement_TextOnly(OutType);
		return FSlot(Ar);
	}

	//////////// FStructuredArchive::FStream ////////////

	FORCEINLINE FStructuredArchive::FSlot FStructuredArchive::FStream::EnterElement()
	{
		return FSlot(Ar);
	}

	FORCEINLINE FStructuredArchive::FSlot FStructuredArchive::FStream::EnterElement_TextOnly(EArchiveValueType& OutType)
	{
		Ar.Formatter.EnterStreamElement_TextOnly(OutType);
		return FSlot(Ar);
	}

	//////////// FStructuredArchive::FMap ////////////

	FORCEINLINE FStructuredArchive::FSlot FStructuredArchive::FMap::EnterElement(FString& Name)
	{
		Ar.Formatter.EnterMapElement(Name);
		return FSlot(Ar);
	}

	FORCEINLINE FStructuredArchive::FSlot FStructuredArchive::FMap::EnterElement_TextOnly(FString& Name, EArchiveValueType& OutType)
	{
		Ar.Formatter.EnterMapElement_TextOnly(Name, OutType);
		return FSlot(Ar);
	}

#endif

