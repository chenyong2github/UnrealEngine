// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


/** Macros to make declaring a metasound parameter simple.  */

// Declares a metasound parameter type by
// - Adding typedefs for commonly used template types.
// - Defining parameter type traits.
#define DECLARE_METASOUND_DATA_REFERENCE_TYPES(DataType, DataTypeName, DataTypeMagicNumber, DataTypeInfoTypeName, DataReadReferenceTypeName, DataWriteReferenceTypeName) \
	template<> \
	struct TDataReferenceTypeInfo<DataType> \
	{ \
		static constexpr const TCHAR* TypeName = TEXT(DataTypeName); \
		static constexpr FDataTypeMagicNumber MagicNumber = (DataTypeMagicNumber); \
	}; \
	\
	typedef TDataReferenceTypeInfo<DataType> DataTypeInfoTypeName; \
	\
	typedef TDataReadReference<DataType> DataReadReferenceTypeName; \
	typedef TDataWriteReference<DataType> DataWriteReferenceTypeName; \



namespace Metasound
{
	/** ID for parameter type. Should be unique for different C++ types. */
	typedef int32 FDataTypeMagicNumber;

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
		virtual FDataTypeMagicNumber GetDataTypeMagicNumber() const = 0;

		/** Creates a copy of the parameter type. */
		virtual TUniquePtr<IDataReference> Clone() const = 0;
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
		static constexpr const TCHAR* TypeName = TEXT("");

		/** Magic number used to check the data type when casting. */
		static constexpr const FDataTypeMagicNumber MagicNumber = -1;
	};
	
	/** Template class for a paramter reference. 
	 *
	 * This fulfills the IParamterRef interface, utilizing TDataReferenceTypeInfo to
	 * define the the TypeName and TypeId of the parameter. 
	 */
	template <typename DataType>
	class TDataReference : public IDataReference
	{
		public:
			typedef TSharedRef<DataType, ESPMode::NotThreadSafe> FRefType;

			typedef TDataReferenceTypeInfo<DataType> FInfoType;

			/** This constructor forwards arguments to the constructor of the underlying DataType.
			 *
			 * SFINAE is used to disable copy and move constructors as those are defined separately. 
			 */
			template <
				typename... ArgTypes,
				typename = typename TEnableIf<
					TAndValue<
						sizeof...(ArgTypes) != 0,
						TOrValue<
							sizeof...(ArgTypes) != 1,
							TNot< TIsDerivedFrom< typename TDecay< typename TNthTypeFromParameterPack< 0, ArgTypes... >::Type >::Type, /*typename*/ TDataReference<DataType> > >
						>
					>::Value
				>::Type
			>
			TDataReference(ArgTypes&&... Args)
			:	ObjectReference(MakeShared<DataType, ESPMode::NotThreadSafe>(Forward<ArgTypes>(Args)...))
			{
			}

	
			/** Construct operator with no arguments if the DataType has a default constructor.  */
			template< typename = typename TEnableIf< TIsConstructible<DataType>::Value >::Type >
			TDataReference()
			:	ObjectReference(MakeShared<DataType>())
			{
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
				static const FName TypeName = FName(FInfoType::TypeName);
				return TypeName;
			}

			/** Return the ID of the underlying type. */
			virtual FDataTypeMagicNumber GetDataTypeMagicNumber() const override
			{
				return FInfoType::MagicNumber;
			}

			/** Create a clone of this object. 
			virtual TUniquePtr<IDataReference> Clone() const override
			{
				typedef TDataReference<DataType> FDataReference;

				return MakeUnique< FDataReference >(*this);
			}
			*/

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
		public:
			typedef TDataReference<DataType> FDataReference;

			/** Construct operator type with arguments to "DataType" constructor. */
			template<
				typename... ArgTypes,
				typename = typename TEnableIf<
					TAndValue<
						sizeof...(ArgTypes) != 0,
						TOrValue<
							sizeof...(ArgTypes) != 1,
							TNot< TIsDerivedFrom< typename TDecay< typename TNthTypeFromParameterPack< 0, ArgTypes... >::Type >::Type, /*typename*/ TDataReference<DataType> > >
						>
					>::Value
				>::Type
			>
			explicit TDataWriteReference(ArgTypes&&... Args)
			:	FDataReference(Forward<ArgTypes>(Args)...)
			{
			}

	
			/** Construct operator with no arguments if the DataType has a default constructor.  */
			template< typename = typename TEnableIf< TIsConstructible<DataType>::Value >::Type >
			TDataWriteReference()
			:	FDataReference()
			{
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
				//return *ObjectReference;
			}

			/** Non-const access to the underlying parameter object. */
			FORCEINLINE DataType* operator->() const
			{
				return TDataReference<DataType>::ObjectReference.operator->();
				//return ObjectReference.operator->();
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
		public:
			typedef TDataReference<DataType> FDataReference;

			/** Construct operator type with arguments to "DataType" constructor. */
			template<
				typename... ArgTypes,
				typename = typename TEnableIf<
					TAndValue<
						sizeof...(ArgTypes) != 0,
						TOrValue<
							sizeof...(ArgTypes) != 1,
							TNot< TIsDerivedFrom< typename TDecay< typename TNthTypeFromParameterPack< 0, ArgTypes... >::Type >::Type, /*typename*/ TDataReference<DataType> > >
						>
					>::Value
				>::Type
			>
			explicit TDataReadReference(ArgTypes&&... Args)
			:	FDataReference(Forward<ArgTypes>(Args)...)
			{
			}

			/** Construct operator with no arguments if the DataType has a default constructor.  */
			template< typename = typename TEnableIf< TIsConstructible<DataType>::Value >::Type >
			TDataReadReference()
			:	FDataReference()
			{
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
}
