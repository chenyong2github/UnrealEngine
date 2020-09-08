// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundOperatorSettings.h"
#include <type_traits>

/** Macro to make declaring a metasound parameter simple.  */
// Declares a metasound parameter type by
// - Adding typedefs for commonly used template types.
// - Defining parameter type traits.
#define DECLARE_METASOUND_DATA_REFERENCE_TYPES(DataType, ModuleApi, DataTypeInfoTypeName, DataReadReferenceTypeName, DataWriteReferenceTypeName) \
	template<> \
	struct ::Metasound::TDataReferenceTypeInfo<DataType> \
	{ \
		static ModuleApi const TCHAR* TypeName; \
		static constexpr ::Metasound::FMetasoundDataTypeId TypeId = static_cast<::Metasound::FMetasoundDataTypeId>(::Metasound::GetMetasoundDataTypeIdPtr<DataType>()); \
		static constexpr bool bIsStringParsable = TTestIfDataTypeCtorIsImplemented<DataType, const FString&>::Value; \
		static constexpr bool bIsBoolParsable = ::Metasound::TTestIfDataTypeCtorIsImplemented<DataType, bool>::Value; \
		static constexpr bool bIsIntParsable = ::Metasound::TTestIfDataTypeCtorIsImplemented<DataType, int32>::Value; \
		static constexpr bool bIsFloatParsable = ::Metasound::TTestIfDataTypeCtorIsImplemented<DataType, float>::Value; \
		static constexpr bool bIsProxyParsable =  ::Metasound::TTestIfDataTypeCtorIsImplemented<DataType, const Audio::IProxyData&>::Value; \
		static constexpr bool bIsProxyArrayParsable =  ::Metasound::TTestIfDataTypeCtorIsImplemented<DataType, const Audio::IProxyData&>::Value; \
		static constexpr bool bIsConstructableWithSettings = ::Metasound::TTestIfDataTypeSettingsCtorIsImplemented<DataType>::Value; \
		static constexpr bool bCanUseDefaultConstructor = std::is_constructible<DataType>::value; \
		static constexpr bool bIsValidSpecialization = true; \
	}; \
	\
	typedef ::Metasound::TDataReferenceTypeInfo<DataType> DataTypeInfoTypeName; \
	\
	typedef ::Metasound::TDataReadReference<DataType> DataReadReferenceTypeName; \
	typedef ::Metasound::TDataWriteReference<DataType> DataWriteReferenceTypeName; \

// This only needs to be called if you don't plan on calling REGISTER_METASOUND_DATATYPE.
#define IMPL_METASOUND_DATA_TYPE(DataType, DataTypeName) \
	const TCHAR* ::Metasound::TDataReferenceTypeInfo<DataType>::TypeName = TEXT(DataTypeName);

namespace Audio
{
	// Forward declare
	class IProxyData;
}

namespace Metasound
{
	/** Used to generate a unique ID for each registered data type. */
	template<typename DataType>
	struct TMetasoundDataTypeIdPtr
	{
		/** static const address of specific type as an ID. */
		static const DataType* const Ptr;
	};

	template<typename DataType>
	const DataType* const TMetasoundDataTypeIdPtr<DataType>::Ptr = nullptr;

	/** Returns an address of a pointer which is defined once per a Metasound
	 * data type. 
	 */
	template <typename DataType>
	constexpr const DataType* const* GetMetasoundDataTypeIdPtr() noexcept 
	{
		return &TMetasoundDataTypeIdPtr<DataType>::Ptr;
	}

	/** ID for parameter type. Should be unique for different C++ types. */
	using FMetasoundDataTypeId = const void*;

	/** Test if a data type has a constructor which accepts FOperatorSettings */
	template <typename TDataType>
	struct TTestIfDataTypeSettingsCtorIsImplemented
	{
	public:
		static constexpr bool Value = std::is_constructible<TDataType, const ::Metasound::FOperatorSettings&>::value;
	};

	/** Test if a stat type has a constructor which accepts the given data type 
	 * and a FOperatorSettings.
	 */
	template <typename TDataType, typename TTypeToParse>
	struct TTestIfDataTypeCtorIsImplemented
	{
	private:
		static constexpr bool bSupportsConstructionWithSettings = 
			std::is_constructible<TDataType, TTypeToParse, const ::Metasound::FOperatorSettings&>::value
			|| std::is_constructible<TDataType, TTypeToParse, ::Metasound::FOperatorSettings>::value;

		static constexpr bool bSupportsConstructionWithoutSettings = std::is_constructible<TDataType, TTypeToParse>::value;

	public:

		static constexpr bool Value = bSupportsConstructionWithSettings || bSupportsConstructionWithoutSettings;
	};


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
		/** Name of the data type. Internally, these are converted to FNames.
		 * Users should be aware of FName case-insensitivity and other 
		 * limitiations. 
		 */
		static constexpr const TCHAR TypeName[] = TEXT("");

		/** Magic number used to check the data type when casting. */
		static constexpr const FMetasoundDataTypeId TypeId = nullptr;

		// This static assert is triggered if TDataReferenceTypeInfo is used 
		// without specialization.
		static_assert(TSpecializationHelper<DataType>::Value, "TDataReferenceTypeInfo must be specialized.  Use macro DECLARE_METASOUND_DATA_REFERENCE_TYPES");
	
		static constexpr bool bIsStringParsable = false;
		static constexpr bool bIsBoolParsable = false;
		static constexpr bool bIsIntParsable = false;
		static constexpr bool bIsFloatParsable = false;
		static constexpr bool bIsProxyParsable = false;
		static constexpr bool bIsProxyArrayParsable = false;
		static constexpr bool bIsConstructableWithSettings = false;
		static constexpr bool bCanUseDefaultConstructor = false;
		static constexpr bool bIsValidSpecialization = false;
	};


	/** Return the data type name for a registered data type. */
	template<typename DataType>
	const FName GetMetasoundDataTypeName() 
	{
		static const FName TypeName = FName(TDataReferenceTypeInfo<std::decay_t<DataType>>::TypeName);

		return TypeName;
	}

	/** Return the data type ID for a registered data type. 
	 *
	 * This ID is runtime constant but may change between executions and builds.
	 */
	template<typename DataType>
	const FMetasoundDataTypeId GetMetasoundDataTypeId() 
	{
		return TDataReferenceTypeInfo<std::decay_t<DataType>>::TypeId;
	}

	/** Specialize void data type for internal use. */
	template<>
	struct TDataReferenceTypeInfo<void>
	{
		static METASOUNDGRAPHCORE_API const TCHAR* TypeName;
		static constexpr FMetasoundDataTypeId TypeId = static_cast<FMetasoundDataTypeId>(GetMetasoundDataTypeIdPtr<void>());
		static constexpr bool bIsStringParsable = false;
		static constexpr bool bIsBoolParsable = false;
		static constexpr bool bIsIntParsable = false;
		static constexpr bool bIsFloatParsable = false;
		static constexpr bool bIsProxyParsable = false;
		static constexpr bool bIsProxyArrayParsable = false;
		static constexpr bool bIsConstructableWithSettings = false;
		static constexpr bool bCanUseDefaultConstructor = false;
		static constexpr bool bIsValidSpecialization = false;
	};

	/** A Data Reference Interface.
	 *
	 * A parameter references provides information and access to a shared object in the graph. 
	 */
	class IDataReference
	{
		public:

		virtual ~IDataReference() 
		{
		}

		/** Returns the name of the data type. */
		virtual const FName& GetDataTypeName() const = 0;

		/** Returns the ID of the parameter type. */
		virtual FMetasoundDataTypeId GetDataTypeId() const = 0;

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
		static const FMetasoundDataTypeId TypeId = GetMetasoundDataTypeId<DataType>();

		bool bEqualTypeName = InReference.GetDataTypeName() == TypeName;
		// TODO: need to move data type ID definition to traits implementation.
		//bool bEqualTypeId = InReference.GetDataTypeId() == TypeId;
		bool bEqualTypeId = true;

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
			virtual FMetasoundDataTypeId GetDataTypeId() const override
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
				static_assert(std::is_constructible<DataType, ArgTypes...>::value, "Tried to call TDataWriteReference::CreateNew with args that don't match any constructor for an underlying type!");
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
				static_assert(std::is_constructible<DataType, ArgTypes...>::value, "Tried to call TDataReadReference::CreateNew with args that don't match any constructor for an underlying type!");
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
				//ObjectReference = Other.ObjectReference;
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
				//return *ObjectReference;
			}

			/** Const access to the underlying parameter object. */
			FORCEINLINE const DataType* operator->() const
			{
				return TDataReference<DataType>::ObjectReference.operator->();
				//return ObjectReference.operator->();
			}

			/** Create a clone of this parameter reference. */
			virtual TUniquePtr<IDataReference> Clone() const override
			{
				typedef TDataReadReference<DataType> FDataReadReference;

				return MakeUnique< FDataReadReference >(*this);
			}
	};

	enum class ELiteralArgType : uint8
	{
		Boolean,
		Integer,
		Float,
		String,
		UObjectProxy,
		UObjectProxyArray,
		None, // If this is set, we will invoke TType(const FOperatorSettings&) If that constructor exists, or the default constructor if not.
		Invalid,
	};

	// This empty base class is used so that we can specialize various nodes (Send, Receive, etc.) for subclasses of IAudioDatatype.
	class METASOUNDGRAPHCORE_API IAudioDatatype
	{
		/**
		 * Audio datatypes require the following functions:
		 * int32 GetNumChannels() const { return NumChannels; }
		 *
		 * int32 GetMaxNumChannels() const { return MaxNumChannels; }

		 * const TArrayView<const FAudioBufferReadRef> GetBuffers() const { return ReadableBuffers; }

		 * const TArrayView<const FAudioBufferWriteRef> GetBuffers() { return WritableBuffers; }
		 */
	};
}

