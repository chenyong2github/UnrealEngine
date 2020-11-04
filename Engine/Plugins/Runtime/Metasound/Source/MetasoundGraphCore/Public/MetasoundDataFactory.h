// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundDataReference.h"
#include "MetasoundLiteral.h"
#include "MetasoundOperatorSettings.h"
#include "Misc/Variant.h"
#include <type_traits>

namespace Metasound
{
	namespace DataFactoryPrivate
	{
		/** Description of available constructors for a registered Metasound Data Type. 
		 *
		 * @tparam DataType - The registered Metasound Data Type.
		 * @tparam ArgTypes - A parameter pack of arguments to be passed to the data type's constructor.
		 */
		template<typename DataType, typename... ArgTypes>
		struct TDataTypeConstructorTraits
		{
			static constexpr bool bIsDefaultConstructible = std::is_constructible<DataType>::value;
			static constexpr bool bIsConstructibleWithSettings = std::is_constructible<DataType, const FOperatorSettings&>::value;
			static constexpr bool bIsConstructibleWithArgs = std::is_constructible<DataType, ArgTypes...>::value;
			static constexpr bool bIsConstructibleWithSettingsAndArgs = std::is_constructible<DataType, const FOperatorSettings&, ArgTypes...>::value;
		};

		/** Constructor traits for a for variant inputs. These traits test each
		 * individual type supported by the variant and determines if *any* of the
		 * types can be used as a single argument to construct the DataType.
		 *
		 * @tparam DataType - The registered Metasound Data Type.
		 * @tparam Types - The types supported by the variant.
		 */
		template<typename DataType, typename... Types>
		struct TDataTypeVariantConstructorTraits;

		// Specialization for single arg.
		template<typename DataType, typename ArgType>
		struct TDataTypeVariantConstructorTraits<DataType, ArgType>
		{
			using FConstructorTraits = TDataTypeConstructorTraits<DataType, ArgType>;

			public:

			static constexpr bool bIsDefaultConstructible = FConstructorTraits::bIsDefaultConstructible;
			static constexpr bool bIsConstructibleWithSettings = FConstructorTraits::bIsConstructibleWithSettings;
			static constexpr bool bIsConstructibleWithArgs = FConstructorTraits::bIsConstructibleWithArgs;
			static constexpr bool bIsConstructibleWithSettingsAndArgs = FConstructorTraits::bIsConstructibleWithSettingsAndArgs;
		};

		// Specialization for unrolling parameter packs. 
		template<typename DataType, typename ArgType, typename... AdditionalTypes>
		struct TDataTypeVariantConstructorTraits<DataType, ArgType, AdditionalTypes...>
		{
			private:

			using FAdditionalConstructorTraits = TDataTypeVariantConstructorTraits<DataType, AdditionalTypes...>;
			using FArgConstructorTraits = TDataTypeConstructorTraits<DataType, ArgType>;
			
			public:

			static constexpr bool bIsDefaultConstructible = FAdditionalConstructorTraits::bIsDefaultConstructible;
			static constexpr bool bIsConstructibleWithSettings = FAdditionalConstructorTraits::bIsConstructibleWithSettings;
			static constexpr bool bIsConstructibleWithArgs = FArgConstructorTraits::bIsConstructibleWithArgs || FAdditionalConstructorTraits::bIsConstructibleWithArgs;
			static constexpr bool bIsConstructibleWithSettingsAndArgs = FArgConstructorTraits::bIsConstructibleWithSettingsAndArgs || FAdditionalConstructorTraits::bIsConstructibleWithSettingsAndArgs;
		};

		// Specialization for unpacking the types supported by a TVariant. 
		template<typename DataType, typename FirstVariantType, typename... AdditionalVariantTypes>
		struct TDataTypeVariantConstructorTraits<DataType, TVariant<FirstVariantType, AdditionalVariantTypes...>>
		{
			private:

			using FConstructorTraits = TDataTypeVariantConstructorTraits<DataType, FirstVariantType, AdditionalVariantTypes...>;
			
			public:

			static constexpr bool bIsDefaultConstructible = FConstructorTraits::bIsDefaultConstructible;
			static constexpr bool bIsConstructibleWithSettings = FConstructorTraits::bIsConstructibleWithSettings;
			static constexpr bool bIsConstructibleWithArgs = FConstructorTraits::bIsConstructibleWithArgs;
			static constexpr bool bIsConstructibleWithSettingsAndArgs = FConstructorTraits::bIsConstructibleWithSettingsAndArgs;
		};


		/** Denotes that both FOperatorSettings and parameter pack arguments must be
		 * used in the constructor of the Metasound Data Type.
		 *
		 * @tparam DataType - The registered Metasound Data Type.
		 * @tparam ArgTypes - A parameter pack of arguments to be passed to the data type's constructor.
		 */
		template<typename DataType, typename... ArgTypes>
		struct TExplicitConstructorForwarding
		{
			using TConstructorTraits = TDataTypeConstructorTraits<DataType, ArgTypes...>;

			static constexpr bool bForwardSettingsAndArgs = TConstructorTraits::bIsConstructibleWithSettingsAndArgs;
			static constexpr bool bForwardArgs = false;
			static constexpr bool bForwardSettings = false;
			static constexpr bool bForwardNone = false;

			static constexpr bool bCannotForwardToConstructor = !(bForwardSettingsAndArgs || bForwardArgs || bForwardSettings || bForwardNone);
		};

		/** Denotes that parameter pack arguments must be used in the constructor of
		 * the Metasound Data Type. The use of the FOperatorSettings is optional.
		 *
		 * @tparam DataType - The registered Metasound Data Type.
		 * @tparam ArgTypes - A parameter pack of arguments to be passed to the data type's constructor.
		 */
		template<typename DataType, typename... ArgTypes>
		struct TExplicitArgsConstructorForwarding
		{
			using TConstructorTraits = TDataTypeConstructorTraits<DataType, ArgTypes...>;

			static constexpr bool bForwardSettingsAndArgs = TConstructorTraits::bIsConstructibleWithSettingsAndArgs;
			static constexpr bool bForwardArgs = !bForwardSettingsAndArgs && TConstructorTraits::bIsConstructibleWithArgs;
			static constexpr bool bForwardSettings = false;
			static constexpr bool bForwardNone = false;

			static constexpr bool bCannotForwardToConstructor = !(bForwardSettingsAndArgs || bForwardArgs || bForwardSettings || bForwardNone);
		};

		/** Denotes that parameter pack arguments and/or FOperatorSettings is optional
		 * when constructing the Metasound Data Type. 
		 *
		 * @tparam DataType - The registered Metasound Data Type.
		 * @tparam ArgTypes - A parameter pack of arguments to be passed to the data type's constructor.
		 */
		template<typename DataType, typename... ArgTypes>
		struct TAnyConstructorForwarding
		{
			using TConstructorTraits = TDataTypeConstructorTraits<DataType, ArgTypes...>;

			static constexpr bool bForwardSettingsAndArgs = TConstructorTraits::bIsConstructibleWithSettingsAndArgs;
			static constexpr bool bForwardArgs = !bForwardSettingsAndArgs && TConstructorTraits::bIsConstructibleWithArgs;
			static constexpr bool bForwardSettings = !(bForwardArgs || bForwardSettingsAndArgs) && TConstructorTraits::bIsConstructibleWithSettings;
			static constexpr bool bForwardNone = !(bForwardArgs || bForwardSettingsAndArgs || bForwardSettings) && TConstructorTraits::bIsDefaultConstructible;

			static constexpr bool bCannotForwardToConstructor = !(bForwardSettingsAndArgs || bForwardArgs || bForwardSettings || bForwardNone);
		};

		/** Create a DataType. For use in TDataFactory. */
		template<typename DataType>
		struct TDataTypeCreator
		{
			template<typename... ArgTypes>
			static DataType CreateNew(ArgTypes&&... Args)
			{
				return DataType(Forward<ArgTypes>(Args)...);
			}
		};

	MSVC_PRAGMA(warning(push))
	MSVC_PRAGMA(warning(disable : 4800)) // Disable warning when converting int to bool.

		/** Core factory type for creating objects related to metasound data types. 
		 *
		 * TDataFactoryInternal provides a unified interface for constructing objects
		 * using a FOperatorSettings and a parameter pack of arguments. The ArgForwardType
		 * uses SFINAE to choose whether to forward all arguments, a subset of arguments
		 * or no arguments to the underlying CreatorType.
		 *
		 * @tparam DataType - The registered metasound data type which defines the supported constructors.
		 * @tparam CreatorType - A class providing a static "CreateNew(...)" function which
		 *                       accepts the forwarded arguments and returns an object.
		 */
		template<typename DataType, typename CreatorType>
		struct TDataFactoryInternal
		{
			/** Create a new object using FOperatorSettings and a parameter pack of
			 * arguments. 
			 *
			 * Note: In this implementation, all arguments are forwarded to CreatorType::CreateNew(...)
			 *
			 * @tparam ArgForwardType - A type which describes which settings and arguments
			 *                          should be forwarded to CreateType::CreateNew(...)
			 * @tparam ArgTypes - A parameter pack of types to be forwarded to CreateType::CreateNew(...)
			 */
			template<
				typename ArgForwardType,
				typename... ArgTypes,
				typename std::enable_if< ArgForwardType::bForwardSettingsAndArgs, int>::type = 0
			>
			static auto CreateNew(const FOperatorSettings& InSettings, ArgTypes&&... Args)
			{
				return CreatorType::CreateNew(InSettings, Forward<ArgTypes>(Args)...);
			}

			/** Create a new object using FOperatorSettings and a parameter pack of
			 * arguments. 
			 *
			 * Note: In this implementation, the parameter pack is forwarded to CreateType::CreateNew(...), 
			 * while the FOperatorSettings are ignored.
			 *
			 * @tparam ArgForwardType - A type which describes which settings and arguments
			 *                          should be forwarded to CreateType::CreateNew(...)
			 * @tparam ArgTypes - A parameter pack of types to be forwarded to CreateType::CreateNew(...)
			 */
			template<
				typename ArgForwardType,
				typename... ArgTypes,
				typename std::enable_if< ArgForwardType::bForwardArgs, int>::type = 0
			>
			static auto CreateNew(const FOperatorSettings& InSettings, ArgTypes&&... Args)
			{
				return CreatorType::CreateNew(Forward<ArgTypes>(Args)...);
			}

			/** Create a new object using FOperatorSettings and a parameter pack of
			 * arguments. 
			 *
			 * Note: In this implementation, the FOperatorSettings are forwarded to CreateType::CreateNew(...), 
			 * while the paramter pack is ignored.
			 *
			 * @tparam ArgForwardType - A type which describes which settings and arguments
			 *                          should be forwarded to CreateType::CreateNew(...)
			 * @tparam ArgTypes - A parameter pack of types to be forwarded to CreateType::CreateNew(...)
			 */
			template<
				typename ArgForwardType,
				typename... ArgTypes,
				typename std::enable_if< ArgForwardType::bForwardSettings, int>::type = 0
			>
			static auto CreateNew(const FOperatorSettings& InSettings, ArgTypes&&... Args)
			{
				return CreatorType::CreateNew(InSettings);
			}

			/** Create a new object using FOperatorSettings and a parameter pack of
			 * arguments. 
			 *
			 * Note: In this implementation, the FOperatorSettings and parameter pack
			 * are ignored.
			 *
			 * @tparam ArgForwardType - A type which describes which settings and arguments
			 *                          should be forwarded to CreateType::CreateNew(...)
			 * @tparam ArgTypes - A parameter pack of types to be forwarded to CreateType::CreateNew(...)
			 */
			template<
				typename ArgForwardType,
				typename... ArgTypes,
				typename std::enable_if< ArgForwardType::bForwardNone, int>::type = 0
			>
			static auto CreateNew(const FOperatorSettings& InSettings, ArgTypes&&... Args)
			{
				return CreatorType::CreateNew();
			}

			/** Create a new object using FOperatorSettings and a parameter pack of
			 * arguments. 
			 *
			 * Note: In this implementation, the FOperatorSettings and parameter pack
			 * are ignored.
			 *
			 * @tparam ArgForwardType - A type which describes which settings and arguments
			 *                          should be forwarded to CreateType::CreateNew(...)
			 * @tparam ArgTypes - A parameter pack of types to be forwarded to CreateType::CreateNew(...)
			 */
			template<
				typename ArgForwardType,
				typename... ArgTypes,
				typename std::enable_if< ArgForwardType::bCannotForwardToConstructor, int>::type = 0
			>
			static auto CreateNew(const FOperatorSettings& InSettings, ArgTypes&&... Args)
			{
				static_assert(!ArgForwardType::bCannotForwardToConstructor, "No constructor exists for the DataType which matches the given arguments and argument forwarding type.");
			}
		};

	MSVC_PRAGMA(warning(pop))

		/** Core factory type for creating objects related to Metasound DataTypes. 
		 *
		 * TDataVariantFactoryInternal provides a unified interface for constructing objects
		 * using a FOperatorSettings and TVariant arguments. The VariantParseType
		 * uses SFINAE to choose how to interpret the TVariant argument.
		 *
		 * @tparam DataType - The registered metasound data type which defines the supported constructors.
		 * @tparam CreatorType - A class providing a static "CreateNewFromVariant(...)" function which
		 *                       accepts the forwarded arguments and returns an object.
		 */
		template<typename DataType, typename CreatorType>
		struct TDataVariantFactoryInternal
		{
			using FInternalFactory = TDataFactoryInternal<DataType, CreatorType>;

			/** Create a new object using FOperatorSettings and TVariant.
			 *
			 * Note: In this implementation, all arguments are forwarded to CreatorType::CreateNew(...)
			 * and the TVariant is parsed as the `VariantType`. FOperatorSettings are
			 * forwarded to CreatorType::CreateNew(...) if it is supported.
			 *
			 * @tparam VariantParseType - A type which describes how to parse the InVariant
			 *                            argument.
			 * @tparam VariantType - The expected underlying type stored in InVariant.
			 */
			template<
				typename VariantParseType,
				typename VariantType,
				typename std::enable_if< VariantParseType::bCreateWithArgType, int>::type = 0
			>
			static auto CreateNewFromVariant(const FOperatorSettings& InSettings, const VariantType& InVariant)
			{
				// The constructor must use the desired variant argument.
				using TExplicitArgsConstructorForwarding = TExplicitArgsConstructorForwarding<DataType, typename VariantParseType::ArgType>;

				return FInternalFactory::template CreateNew<TExplicitArgsConstructorForwarding>(InSettings, InVariant.template Get<typename VariantParseType::ArgType>());
			}

			/** Create a new object using FOperatorSettings and TVariant.
			 *
			 * Note: In this implementation, the InVariant object is ignore. FOperatorSettings 
			 * are forwarded to CreatorType::CreateNew(...) if it is supported.
			 *
			 * @tparam VariantParseType - A type which describes how to parse the InVariant
			 *                            argument.
			 * @tparam VariantType - The expected underlying type stored in InVariant.
			 */
			template<
				typename VariantParseType,
				typename VariantType,
				typename std::enable_if< VariantParseType::bCreateWithoutArg, int>::type = 0
			>
			static auto CreateNewFromVariant(const FOperatorSettings& InSettings, const VariantType& InVariant)
			{
				// The constructor must be the default constructor or accept a FOperatorSettings.
				using TExplicitArgsConstructorForwarding = TExplicitArgsConstructorForwarding<DataType>;

				// When constructing an object from a variant, callers expect the variant value
				// to be used in the constructor. It is an error to ignore the variant value.
				// This error can be fixed by changing the InVariant object passed in 
				// or by adding a constructor to the DataType which accepts the VariantType.
				checkf(false, TEXT("The value passed to the constructor is being ignored")); 

				return FInternalFactory::template CreateNew<TExplicitArgsConstructorForwarding>(InSettings);
			}

			/** Create a new object using FOperatorSettings and TVariant.
			 *
			 * Note: In this implementation, the TVariant is parsed as a fallback type.
			 * FOperatorSettings are forwarded to CreatorType::CreateNew(...) if it 
			 * is supported.
			 *
			 * @tparam VariantParseType - A type which describes how to parse the InVariant
			 *                            argument.
			 * @tparam VariantType - The expected underlying type stored in InVariant.
			 */
			template<
				typename VariantParseType,
				typename VariantType,
				typename std::enable_if< VariantParseType::bCreateWithFallbackArgType, int>::type = 0
			>
			static auto CreateNewFromVariant(const FOperatorSettings& InSettings, const VariantType& InVariant)
			{
				// The constructor must use the fallback variant argument.
				using TExplicitArgsConstructorForwarding = TExplicitArgsConstructorForwarding<DataType, typename VariantParseType::FallbackArgType>;

				// When constructing an object from a variant, callers expect the variant value
				// to be used in the constructor. It is an error to interpret the variant value
				// as something other than the expected type. Likely, this error is due
				// to the DataType's constructor not supporting the VariantType as an argument.
				// This error can be fixed by changing the InVariant object passed in 
				// or by adding a constructor to the DataType which accepts the VariantType.
				checkf(false, TEXT("The value passed to the constructor is parsed as an unrelated type.")); 

				return FInternalFactory::template CreateNew<TExplicitArgsConstructorForwarding>(InSettings, InVariant.template Get<typename VariantParseType::FallbackArgType>());
			}
			
			/** Create a new object using FOperatorSettings and TVariant.
			 *
			 * Note: In this implementation signals a static error as not possible ways
			 * to construct that underlying object could be found.
			 *
			 * @tparam VariantParseType - A type which describes how to parse the InVariant
			 *                            argument.
			 * @tparam VariantType - The expected underlying type stored in InVariant.
			 */
			template<
				typename VariantParseType,
				typename VariantType,
				typename std::enable_if< VariantParseType::bCannotForwardToConstructor, int>::type = 0
			>
			static auto CreateNewFromVariant(const FOperatorSettings& InSettings, const VariantType& InVariant)
			{
				// There is no DataType constructor which can be used to create the 
				// DataType. This includes no default constructor, nor any constructor
				// which accepts any of the variant types. 
				static_assert(!VariantParseType::bCannotForwardToConstructor, "Constructor has no default constructor and does not support any supplied variant type");
			}
		};

		// Promote which type to use for fallback 
		template<bool ValidType, typename ThisType, typename PreviousType = void>
		struct TTypePromoter
		{
			typedef PreviousType Type;
			static constexpr bool bIsValidType = ValidType;
		};

		// TTypePromoter partial specialization for ValidType=true
		template<typename ThisType, typename PreviousType>
		struct TTypePromoter<true, ThisType, PreviousType>
		{
			typedef PreviousType Type;
			static constexpr bool bIsValidType = true;
		};

		// TTypePromoter partial specialization for ValidType=false
		template<typename ThisType, typename PreviousType>
		struct TTypePromoter<false, ThisType, PreviousType>
		{
			typedef PreviousType Type;
			static constexpr bool bIsValidType = !std::is_same<PreviousType, void>::value;
		};

		// TTypePromoter partial specialization for ValidType=true and no PreviousType.
		template<typename ThisType>
		struct TTypePromoter<true, ThisType>
		{
			typedef ThisType Type;
			static constexpr bool bIsValidType = true;
		};


		// Helper for determine whether constructor accepts the variant type. Supports
		// unrolling the parameter pack via recursive template.
		template<typename DataType, typename ThisArgType, typename... VariantArgTypes>
		struct TDataTypeVariantFallbackHelper
		{
		private:

			static constexpr bool bIsConstructibleWithThisArg = std::is_constructible<DataType, ThisArgType>::value;
			static constexpr bool bIsConstructibleWithSettingsAndThisArg = std::is_constructible<DataType, const FOperatorSettings&, ThisArgType>::value;

			static constexpr bool bPromotThisArgType = bIsConstructibleWithThisArg || bIsConstructibleWithSettingsAndThisArg;
			using FVariantPromoter = TTypePromoter<bPromotThisArgType, ThisArgType, typename TDataTypeVariantFallbackHelper<DataType, VariantArgTypes...>::FallbackType>;

		public:
			
			// True if the DataType is default constructible or constructible with any 
			// of the variant types.
			static constexpr bool bIsConstructibleWithArgs = bIsConstructibleWithThisArg || TDataTypeVariantFallbackHelper<DataType, VariantArgTypes...>::bIsConstructibleWithArgs;

			// True if the DataType is default constructible or constructible with any 
			// of the variant types.
			static constexpr bool bIsConstructibleWithSettingsAndArgs = bIsConstructibleWithSettingsAndThisArg || TDataTypeVariantFallbackHelper<DataType, VariantArgTypes...>::bIsConstructibleWithSettingsAndArgs;

			// Fallback type to use if there is no other way to construct the object.
			typedef typename FVariantPromoter::Type FallbackType;

			// True if the constructor accepts the fallback type.
			static constexpr bool bIsConstructibleWithFallbackArg = FVariantPromoter::bIsValidType; 
		};

		// Specialization of TDataTypeVariantFallbackHelper to handle end of recursion on 
		// VariantArgTypes.
		template<typename DataType, typename ThisArgType>
		struct TDataTypeVariantFallbackHelper<DataType, ThisArgType>
		{
		private:
			static constexpr bool bIsConstructibleWithThisArg = std::is_constructible<DataType, ThisArgType>::value;
			static constexpr bool bIsConstructibleWithSettingsAndThisArg = std::is_constructible<DataType, const FOperatorSettings&, ThisArgType>::value;

			static constexpr bool bPromotThisArgType = bIsConstructibleWithThisArg || bIsConstructibleWithSettingsAndThisArg;
			using FVariantPromoter = TTypePromoter<bPromotThisArgType, ThisArgType>;

		public:
			// True if the DataType is default constructible or constructible with any 
			// of the variant types.
			static constexpr bool bIsConstructibleWithArgs = bIsConstructibleWithThisArg;

			// True if the DataType is default constructible or constructible with any 
			// of the variant types.
			static constexpr bool bIsConstructibleWithSettingsAndArgs = bIsConstructibleWithSettingsAndThisArg;

			// Fallback type to use if there is no other way to construct the object.
			typedef typename FVariantPromoter::Type FallbackType;
			// True if the constructor accepts the fallback type.
			static constexpr bool bIsConstructibleWithFallbackArg = FVariantPromoter::bIsValidType; 
		};

		/** TDataTypeVariantParsing informs the TDataVariantFactoryInternal on which factory
		 * method to instatiate.
		 */
		template<typename DataType, typename DesiredArgType, typename FirstVariantType, typename... AdditionalVariantTypes>
		struct TDataTypeVariantParsing
		{
			using FDesiredConstructorTraits = TDataTypeConstructorTraits<DataType, DesiredArgType>; 
			using FFallbackHelper = TDataTypeVariantFallbackHelper<DataType, FirstVariantType, AdditionalVariantTypes...>;

			static constexpr bool bCreateWithArgType = FDesiredConstructorTraits::bIsConstructibleWithArgs || FDesiredConstructorTraits::bIsConstructibleWithSettingsAndArgs;
			static constexpr bool bCreateWithoutArg = !bCreateWithArgType && (FDesiredConstructorTraits::bIsConstructibleWithSettings || FDesiredConstructorTraits::bIsDefaultConstructible);
			static constexpr bool bCreateWithFallbackArgType = !(bCreateWithArgType || bCreateWithoutArg) && FFallbackHelper::bIsConstructibleWithFallbackArg;
			static constexpr bool bCannotForwardToConstructor = !(bCreateWithArgType || bCreateWithoutArg || bCreateWithFallbackArgType);

			typedef DesiredArgType ArgType;
			typedef typename FFallbackHelper::FallbackType FallbackArgType;
		};



	} // End namespace DataFactoryPrivate

	/** Determines whether a DataType supports a constructor which accepts and FOperatorSettings
	 * with ArgTypes and/or just ArgTypes. 
	 *
	 * @tparam DataType - The registered Metasound Data Type.
	 * @tparam ArgTypes - A parameter pack of arguments to be passed to the data type's constructor.
	 */
	template<typename DataType, typename... ArgTypes>
	struct TIsParsable
	{
		using TConstructorTraits = DataFactoryPrivate::TDataTypeConstructorTraits<DataType, ArgTypes...>;

		/* True if the DataType supports a constructor which accepts and FOperatorSettings 
		 * with ArgTypes and/or just ArgTypes. False otherwise.
		 */
		static constexpr bool Value = TConstructorTraits::bIsConstructibleWithArgs || TConstructorTraits::bIsConstructibleWithSettingsAndArgs;
	};

	/** Determines whether a DataType supports construction using the given literal.
	 *
	 * @tparam DataType - The registered Metasound Data Type.
	 */
	template<typename DataType>
	struct TLiteralTraits
	{
		using TVariantConstructorTraits = DataFactoryPrivate::TDataTypeVariantConstructorTraits<DataType, FDataTypeLiteralParam::FVariantType>;

		static constexpr bool bIsParsableFromAnyLiteralType = TVariantConstructorTraits::bIsDefaultConstructible || TVariantConstructorTraits::bIsConstructibleWithSettings || TVariantConstructorTraits::bIsConstructibleWithArgs || TVariantConstructorTraits::bIsConstructibleWithSettingsAndArgs;

		/** Determines if a constructor for the DataType exists which accepts 
		 * an FOperatorSettings with the literals constructor arg type, and/or one that
		 * accpets the literal constructor arg type. 
		 *
		 * @param InLiteral - The literal containing the constructor argument.
		 *
		 * @return True if a constructor for the DataType exists which accepts 
		 * an FOperatorSettings with the literals constructor arg type, or one that
		 * accpets the literal constructor arg type. It returns False otherwise.
		 */
		static const bool IsParsable(const FDataTypeLiteralParam& InLiteral)
		{
			switch (InLiteral.ConstructorArgType)
			{
				case ELiteralArgType::Boolean:

					return TIsParsable<DataType, bool>::Value;

				case ELiteralArgType::Integer:

					return TIsParsable<DataType, int32>::Value;

				case ELiteralArgType::Float:

					return TIsParsable<DataType, float>::Value;

				case ELiteralArgType::String:

					return TIsParsable<DataType, FString>::Value;

				case ELiteralArgType::UObjectProxy:

					return TIsParsable<DataType, Audio::IProxyDataPtr>::Value;

				case ELiteralArgType::UObjectProxyArray:

					return TIsParsable<DataType, TArray<Audio::IProxyDataPtr>>::Value;

				case ELiteralArgType::None:

					return TIsParsable<DataType>::Value;

				default:

					checkNoEntry();
					return false;
			}
		}
	};

	/** A base factory type for creating objects related to Metasound DataTypes. 
	 *
	 * TDataFactory provides a unified interface for constructing objects using 
	 * a FOperatorSettings and a parameter pack of arguments. The various factory 
	 * methods determine how strictly the DataType constructors must match the 
	 * arguments to the factory method.
	 *
	 * @tparam DataType - The registered metasound data type which defines the supported constructors.
	 * @tparam CreatorType - A class providing a static "CreateNew(...)" function which
	 *                       accepts the forwarded arguments and returns an object.
	 */
	template<typename DataType, typename CreatorType>
	struct TDataFactory
	{
        using FInternalFactory = DataFactoryPrivate::TDataFactoryInternal<DataType, CreatorType>;

		/** Create the object using any supported constructor. */
		template<typename... ArgTypes>
		static auto CreateAny(const FOperatorSettings& InSettings, ArgTypes&&... Args)
		{
			using TForwarding = DataFactoryPrivate::TAnyConstructorForwarding<DataType, ArgTypes...>;

			// CreateAny(...) tries to find a constructor which accepts the following
			// signatures. This static assert is triggered if none of these constructors
			// exist. 
			//
			// DataType()
			// DataType(const FOperatorSettings&)
			// DataType(ArgTypes...)
			// DataType(const FOperatorSettings&, ArgTypes...);
			//
			static_assert(!TForwarding::bCannotForwardToConstructor, "No constructor exists for the DataType which accepts the forwarded arguments");

			return FInternalFactory::template CreateNew<TForwarding>(InSettings, Forward<ArgTypes>(Args)...);
		}

		/** Create the object using only a constructor which exactly matches the
		 * arguments to this factory method. 
		 */
		template<typename... ArgTypes>
		static auto CreateExplicit(const FOperatorSettings& InSettings, ArgTypes&&... Args)
		{
			using TForwarding = DataFactoryPrivate::TExplicitConstructorForwarding<DataType, ArgTypes...>;

			// CreateExplicit(...) tries to find a constructor which accepts the 
			// exact same signature as this factory method. This static assert is 
			// triggered because the constructor does not exist.
			//
			// DataType(const FOperatorSettings&, ArgTypes...);
			//
			static_assert(!TForwarding::bCannotForwardToConstructor, "No constructor exists for the DataType which accepts the forwarded arguments");

			return FInternalFactory::template CreateNew<TForwarding>(InSettings, Forward<ArgTypes>(Args)...);
		}

		/** Create the object using only constructors which utilize all arguments
		 * in the parameter pack (ArgTypes...).
		 */
		template<typename... ArgTypes>
		static auto CreateExplicitArgs(const FOperatorSettings& InSettings, ArgTypes&&... Args)
		{
			using TForwarding = DataFactoryPrivate::TExplicitArgsConstructorForwarding<DataType, ArgTypes...>;

			// CreateExplicitArgs(...) tries to find a constructor which accepts the following
			// signatures. This static assert is triggered if none of these constructors
			// exist. 
			//
			// DataType(ArgTypes...)
			// DataType(const FOperatorSettings&, ArgTypes...);
			//
			static_assert(!TForwarding::bCannotForwardToConstructor, "No constructor exists for the DataType which accepts the forwarded arguments");

			return FInternalFactory::template CreateNew<TForwarding>(InSettings, Forward<ArgTypes>(Args)...);
		}
	};


	/** TDataTypeFactory creates a DataType.
	 *
	 * TDataTypeFactory provides several factory methods for forwarding 
	 * arguments from the factory method to the DataType constructor.  See TDataFactory
	 * for more information on the provided factory methods.
	 *
	 * @tparam DataType - The Metasound DataType.
	 */
	template<typename DataType>
	struct TDataTypeFactory : TDataFactory<DataType, DataFactoryPrivate::TDataTypeCreator<DataType>>
	{
	};

	/** TDataReadReferenceFactory creates TDataReadReferences for the given DataType.
	 *
	 * TDataReadReferenceFactory provides several factory methods for forwarding 
	 * arguments from the factory method to the DataType constructor.  See TDataFactory
	 * for more information on the provided factory methods.
	 *
	 * @tparam DataType - The Metasound DataType of the TDataReadReference.
	 */
	template<typename DataType>
	struct TDataReadReferenceFactory : TDataFactory<DataType, TDataReadReference<DataType>>
	{
	};

	/** TDataWriteReferenceFactory creates TDataWriteReferences for the given DataType.
	 *
	 * TDataWriteReferenceFactory provides several factory methods for forwarding 
	 * arguments from the factory method to the DataType constructor.  See TDataFactory
	 * for more information on the provided factory methods.
	 *
	 * @tparam DataType - The Metasound DataType of the TDataWriteReference.
	 */
	template<typename DataType>
	struct TDataWriteReferenceFactory : TDataFactory<DataType, TDataWriteReference<DataType>>
	{
	};

	
	/** A base factory type for creating objects related to Metasound DataTypes. 
	 *
	 * TDataLiteralFactory provides a unified interface for constructing objects using 
	 * a FOperatorSettings and FDataTypeLiteralParam. The various factory 
	 * methods determine how strictly the DataType constructors must match the 
	 * arguments to the factory method.
	 *
	 * @tparam DataType - The registered metasound data type which defines the supported constructors.
	 * @tparam CreatorType - A class providing a static "CreateNew(...)" function which
	 *                       accepts the forwarded arguments and returns an object.
	 */
	template<typename DataType, typename CreatorType>
	struct TDataLiteralFactory
	{
		using FInternalFactory = DataFactoryPrivate::TDataVariantFactoryInternal<DataType, CreatorType>;

		/** Create the object using any supported constructor. */
		template<typename FirstVariantType, typename... AdditionalVariantTypes>
		static auto CreateAny(const FOperatorSettings& InSettings, const TVariant<FirstVariantType, AdditionalVariantTypes...>& InVariant)
		{
			using FDataVariantParsing = DataFactoryPrivate::TDataTypeVariantParsing<DataType, void, FirstVariantType, AdditionalVariantTypes...>;

			return FInternalFactory::template CreateNewFromVariant<FDataVariantParsing>(InSettings, InVariant);
		}

		/** Create the object using only constructors which utilize the InVariant. */
		template<typename DesiredArgType, typename FirstVariantType, typename... AdditionalVariantTypes>
		static auto CreateExplicitArgs(const FOperatorSettings& InSettings, const TVariant<FirstVariantType, AdditionalVariantTypes...>& InVariant)
		{
			using FDataVariantParsing = DataFactoryPrivate::TDataTypeVariantParsing<DataType, DesiredArgType, FirstVariantType, AdditionalVariantTypes...>;

			return FInternalFactory::template CreateNewFromVariant<FDataVariantParsing>(InSettings, InVariant);
		}

		/** Create the object using only constructors which utilize the InLiteral. 
		 *
		 * @param InSettings - The FOperatorSettings to be passed to the DataType 
		 *                     constructor if an appropriate constructor exists.
		 * @param InLiteral - The literal to be passed to the constructor.
		 *
		 * @return An object related to the DataType. The exact type determined by
		 *         the CreatorType class template parameter.
		 */
		static auto CreateExplicitArgs(const FOperatorSettings& InSettings, const FDataTypeLiteralParam& InLiteral)
		{
			switch (InLiteral.ConstructorArgType)
			{
				case ELiteralArgType::Boolean:

					return CreateExplicitArgs<bool>(InSettings, InLiteral.ConstructorArg);

				case ELiteralArgType::Integer:

					return CreateExplicitArgs<int>(InSettings, InLiteral.ConstructorArg);

				case ELiteralArgType::Float:

					return CreateExplicitArgs<float>(InSettings, InLiteral.ConstructorArg);

				case ELiteralArgType::String:

					return CreateExplicitArgs<FString>(InSettings, InLiteral.ConstructorArg);

				case ELiteralArgType::UObjectProxy:

					return CreateExplicitArgs<Audio::IProxyDataPtr>(InSettings, InLiteral.ConstructorArg);

				case ELiteralArgType::UObjectProxyArray:

					return CreateExplicitArgs<TArray<Audio::IProxyDataPtr>>(InSettings, InLiteral.ConstructorArg);

				case ELiteralArgType::None:

					return CreateExplicitArgs<void>(InSettings, InLiteral.ConstructorArg);

				case ELiteralArgType::Invalid:
				default:

					checkNoEntry();

					return CreateAny(InSettings, InLiteral.ConstructorArg);
			}
		}
	};

	/** TDataTypeLiteralFactory creates a DataType.
	 *
	 * TDataTypeLiteralFactory provides several factory methods for forwarding 
	 * arguments from the factory method to the DataType constructor.  See TDataFactory
	 * for more information on the provided factory methods.
	 *
	 * @tparam DataType - The Metasound DataType.
	 */
	template<typename DataType>
	struct TDataTypeLiteralFactory : TDataLiteralFactory<DataType, DataFactoryPrivate::TDataTypeCreator<DataType>>
	{
	};

	/** TDataReadReferenceLiteralFactory creates TDataReadReferences for the given DataType.
	 *
	 * TDataReadReferenceLiteralFactory provides several factory methods for forwarding 
	 * arguments from the factory method to the DataType constructor.  See TDataFactory
	 * for more information on the provided factory methods.
	 *
	 * @tparam DataType - The Metasound DataType of the TDataReadReference.
	 */
	template<typename DataType>
	struct TDataReadReferenceLiteralFactory : TDataLiteralFactory<DataType, TDataReadReference<DataType>>
	{
	};

	/** TDataWriteReferenceLiteralFactory creates TDataWriteReferences for the given DataType.
	 *
	 * TDataWriteReferenceLiteralFactory provides several factory methods for forwarding 
	 * arguments from the factory method to the DataType constructor.  See TDataFactory
	 * for more information on the provided factory methods.
	 *
	 * @tparam DataType - The Metasound DataType of the TDataWriteReference.
	 */
	template<typename DataType>
	struct TDataWriteReferenceLiteralFactory : TDataLiteralFactory<DataType, TDataWriteReference<DataType>>
	{
	};
}
