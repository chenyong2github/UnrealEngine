// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <type_traits>

#include "Containers/UnrealString.h"
#include "HAL/Platform.h"
#include "Internationalization/Text.h"
#include "Templates/UniquePtr.h"
#include "UObject/NameTypes.h"


namespace Audio
{
	// Forward declare
	class IProxyData;
}

using FMetasoundDataTypeId = void const*;

namespace Metasound
{
	/** Helper class to enforce specialization of TDataReferenceTypeInfo */
	template<typename DataType>
	struct TSpecializationHelper 
	{
		enum { Value = false };
	};

	/** Info for templated data reference types help perform runtime type 
	 * verification. 
	 */
	template<typename DataType>
	struct TDataReferenceTypeInfo
	{
		// This static assert is triggered if TDataReferenceTypeInfo is used 
		// without specialization.
		static_assert(TSpecializationHelper<DataType>::Value, "TDataReferenceTypeInfo is not specialized.  Use macro DECLARE_METASOUND_DATA_REFERENCE_TYPES to declare a new type, or ensure that an existing DECLARE_METASOUND_DATA_REFERENCE_TYPES exists in the include path.");
	};

	/** Return the data type FName for a registered data type. */
	template<typename DataType>
	const FName& GetMetasoundDataTypeName()
	{
		static const FName TypeName = FName(TDataReferenceTypeInfo<std::decay_t<DataType>>::TypeName);

		return TypeName;
	}

	/** Return the data type string for a registered data type. */
	template<typename DataType>
	const FString& GetMetasoundDataTypeString()
	{
		static const FString TypeString = FString(TDataReferenceTypeInfo<std::decay_t<DataType>>::TypeName);

		return TypeString;
	}

	/** Return the display text for a registered data type. */
	template<typename DataType>
	const FText& GetMetasoundDataTypeDisplayText()
	{
		return TDataReferenceTypeInfo<std::decay_t<DataType>>::GetTypeDisplayText();
	}

	/** Return the data type ID for a registered data type.
	 *
	 * This ID is runtime constant but may change between executions and builds.
	 */
	template<typename DataType>
	const void* const GetMetasoundDataTypeId()
	{
		return TDataReferenceTypeInfo<std::decay_t<DataType>>::TypeId;
	}

	/** Returns array type associated with the base datatype provided(ex. 'Float:Array' if 'Float' is provided) */
	METASOUNDGRAPHCORE_API FName CreateArrayTypeNameFromElementTypeName(const FName InTypeName);

	/** Returns the base data type with the array extension(ex. 'Float' if 'Float:Array' is provided) */
	METASOUNDGRAPHCORE_API FName CreateElementTypeNameFromArrayTypeName(const FName InArrayTypeName);

	/** Specialize void data type for internal use. */
	template<>
	struct TDataReferenceTypeInfo<void>
	{
		static METASOUNDGRAPHCORE_API const TCHAR* TypeName;
		static METASOUNDGRAPHCORE_API const void* const TypeId;
		static METASOUNDGRAPHCORE_API const FText& GetTypeDisplayText();

		private:

		static const void* const TypePtr;
	};


	/** A Data Reference Interface.
	 *
	 * A parameter references provides information and access to a shared object in the graph.
	 */
	class METASOUNDGRAPHCORE_API IDataReference
	{
		public:
		static const FName RouterName;

		virtual ~IDataReference() = default;

		/** Returns the name of the data type. */
		virtual const FName& GetDataTypeName() const = 0;

		/** Returns the ID of the parameter type. */
		virtual const void* const GetDataTypeId() const = 0;

		/** Creates a copy of the parameter type. */
		virtual TUniquePtr<IDataReference> Clone() const = 0;
	};
	
	/** Test if an IDataReference contains the same data type as the template
	 * parameter.
	 *
	 * @return True if the IDataReference contains the DataType. False otherwise. 
	 */
	template<typename DataType>
	bool IsDataReferenceOfType(const IDataReference& InReference)
	{
		static const FName TypeName = GetMetasoundDataTypeName<DataType>();
		static const void* const TypeId = GetMetasoundDataTypeId<DataType>();

		bool bEqualTypeName = InReference.GetDataTypeName() == TypeName;
		bool bEqualTypeId = InReference.GetDataTypeId() == TypeId;

		return (bEqualTypeName && bEqualTypeId);
	}

	// This enum is used as a token to explicitly delineate when we should create a new object for the reference,
	// or use a different constructor.
	enum class EDataRefShouldConstruct
	{
		NewObject
	};

	/** Template class for a paramter reference. 
	 *
	 * This fulfills the IParamterRef interface, utilizing TDataReferenceTypeInfo to
	 * define the the TypeName and TypeId of the parameter. 
	 */
	template <typename DataType>
	class TDataReference : public IDataReference
	{
		static_assert(std::is_same<DataType, std::decay_t<DataType>>::value, "Data types used as data references must not decay");
	protected:
		/**
		 * This constructor forwards arguments to an underlying constructor.
		 */
		template <typename... ArgTypes>
		TDataReference(EDataRefShouldConstruct InToken, ArgTypes&&... Args)
			: ObjectReference(MakeShared<DataType, ESPMode::NotThreadSafe>(Forward<ArgTypes>(Args)...))
		{
		}

		public:
			typedef TSharedRef<DataType, ESPMode::NotThreadSafe> FRefType;

			typedef TDataReferenceTypeInfo<DataType> FInfoType;

			/** This should be used to construct a new DataType object and return this TDataReference as a wrapper around it.
			 */
			template <typename... ArgTypes>
			static TDataReference<DataType> CreateNew(ArgTypes&&... Args)
			{
				static_assert(std::is_constructible<DataType, ArgTypes...>::value, "Tried to call TDataReference::CreateNew with args that don't match any constructor for an underlying type!");
				return TDataReference<DataType>(EDataRefShouldConstruct::NewObject, Forward<ArgTypes>(Args)...);
			}

			/** Enable copy constructor */
			TDataReference(const TDataReference<DataType>& Other) = default;

			/** Enable move constructor */
			TDataReference(TDataReference<DataType>&& Other) = default;

			/** Enable copy operator */
			TDataReference<DataType>& operator=(const TDataReference<DataType>& Other) = default;

			/** Enable move operator */
			TDataReference<DataType>& operator=(TDataReference<DataType>&& Other) = default;

			/** Return the name of the underlying type. */
			virtual const FName& GetDataTypeName() const override
			{
				static const FName Name = GetMetasoundDataTypeName<DataType>();

				return Name;
			}

			/** Return the ID of the underlying type. */
			virtual const void* const GetDataTypeId() const override
			{
				return GetMetasoundDataTypeId<DataType>();
			}

		protected:

			// Protected object reference is utilized by subclasses which define what
			// access is provided to the ObjectReference. 
			FRefType ObjectReference;
	};


	// Forward declare
	template <typename DataType>
	class TDataReadReference;

	/** TDataWriteReference provides write access to a shared parameter reference. */
	template <typename DataType>
	class TDataWriteReference : public TDataReference<DataType>
	{
		// Construct operator with no arguments if the DataType has a default constructor.
		template <typename... ArgTypes>
		TDataWriteReference(EDataRefShouldConstruct InToken, ArgTypes&&... Args)
			: FDataReference(InToken, Forward<ArgTypes>(Args)...)
		{
		}

		public:
			typedef TDataReference<DataType> FDataReference;

			/** This should be used to construct a new DataType object and return this TDataWriteReference as a wrapper around it. */
			template <typename... ArgTypes>
			static TDataWriteReference<DataType> CreateNew(ArgTypes&&... Args)
			{
				static_assert(std::is_constructible<DataType, ArgTypes...>::value, "TDataWriteReference::CreateNew underlying type is not constructible with provided arguments.");
				return TDataWriteReference<DataType>(EDataRefShouldConstruct::NewObject, Forward<ArgTypes>(Args)...);
			}

			/** Enable copy constructor */
			TDataWriteReference(const TDataWriteReference<DataType>& Other) = default;

			/** Enable move constructor */
			TDataWriteReference(TDataWriteReference<DataType>&& Other) = default;

			/** Enable assignment operator. */
			TDataWriteReference<DataType>& operator=(const TDataWriteReference<DataType>& Other) = default;

			/** Enable move operator. */
			TDataWriteReference<DataType>& operator=(TDataWriteReference<DataType>&& Other) = default;

			/** Implicit conversion to a readable parameter. */
			operator TDataReadReference<DataType>() const
			{
				return TDataReadReference<DataType>(*this);
			}

			/** Non-const access to the underlying parameter object. */
			FORCEINLINE DataType& operator*() const
			{
				return *TDataReference<DataType>::ObjectReference;
			}

			/** Non-const access to the underlying parameter object. */
			FORCEINLINE DataType* operator->() const
			{
				return TDataReference<DataType>::ObjectReference.operator->();
			}

			/** Create a clone of this parameter reference. */
			virtual TUniquePtr<IDataReference> Clone() const override
			{
				typedef TDataWriteReference<DataType> FDataWriteReference;

				return MakeUnique< FDataWriteReference >(*this);
			}

			// Provide access to ObjectReference when converting from Write to Read.
			friend class TDataReadReference<DataType>;

			// Friend because it calls protected constructor
			template <typename T>
			friend TDataWriteReference<T> WriteCast(const TDataReadReference<T>& InReadableRef);

		protected:

			/** Create a writable ref from a blank parameter ref. Should be done with care and understanding
			 * of side-effects of converting a readable ref to a writable ref. 
			 */
			TDataWriteReference<DataType>(const FDataReference& InDataReference)
			:	FDataReference(InDataReference)
			{
			}
	};



	/** Cast a TDataReadReference to a TDataWriteReference. 
	 *
	 * In general TDataReadReferences should not be converted into TDataWriteReferences unless the caller
	 * can be certain that no other TDataWriteReference exists for the underlying parameter. Having multiple 
	 * TDataWriteReferences to the same parameter can cause confusion behavior as values are overwritten in
	 * an underterministic fashion.
	 */
	template <typename DataType>
	TDataWriteReference<DataType> WriteCast(const TDataReadReference<DataType>& InReadableRef)
	{
		const TDataReference<DataType>& Ref = static_cast<const TDataReference<DataType>&>(InReadableRef); 
		return TDataWriteReference<DataType>(Ref);
	}



	/** TDataReadReference provides read access to a shared parameter reference. */
	template <typename DataType>
	class TDataReadReference : public TDataReference<DataType>
	{
		// Construct operator with no arguments if the DataType has a default constructor.
		template <typename... ArgTypes>
		TDataReadReference(EDataRefShouldConstruct InToken, ArgTypes&&... Args)
			: FDataReference(InToken, Forward<ArgTypes>(Args)...)
		{
		}

	public:
		typedef TDataReference<DataType> FDataReference;

		// This should be used to construct a new DataType object and return this TDataReadReference as a wrapper around it.
		template <typename... ArgTypes>
		static TDataReadReference<DataType> CreateNew(ArgTypes&&... Args)
		{
			static_assert(std::is_constructible<DataType, ArgTypes...>::value, "DataType constructor does not support provided types.");
			return TDataReadReference<DataType>(EDataRefShouldConstruct::NewObject, Forward<ArgTypes>(Args)...);
		}

		TDataReadReference(const TDataReadReference& Other) = default;

		/** Construct a readable parameter ref from a writable parameter ref. */
		explicit TDataReadReference(const TDataWriteReference<DataType>& WritableRef)
		:	FDataReference(WritableRef)
		{
		}

		/** Assign a readable parameter ref from a writable parameter ref. */
		TDataReadReference<DataType>& operator=(const TDataWriteReference<DataType>& Other)
		{
			TDataReference<DataType>::ObjectReference = Other.ObjectReference;
			return *this;
		}

		/** Enable copy operator */
		TDataReadReference<DataType>& operator=(const TDataReadReference<DataType>& Other) = default;

		/** Enable move operator */
		TDataReadReference<DataType>& operator=(TDataReadReference<DataType>&& Other) = default;

		/** Const access to the underlying parameter object. */
		FORCEINLINE const DataType& operator*() const
		{
			return *TDataReference<DataType>::ObjectReference;
		}

		/** Const access to the underlying parameter object. */
		FORCEINLINE const DataType* operator->() const
		{
			return TDataReference<DataType>::ObjectReference.operator->();
		}

		/** Create a clone of this parameter reference. */
		virtual TUniquePtr<IDataReference> Clone() const override
		{
			typedef TDataReadReference<DataType> FDataReadReference;

			return MakeUnique<FDataReadReference>(*this);
		}
	};
}

