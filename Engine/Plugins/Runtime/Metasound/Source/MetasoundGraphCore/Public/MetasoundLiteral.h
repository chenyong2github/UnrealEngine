// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "IAudioProxyInitializer.h"
#include "MetasoundOperatorSettings.h" 
#include "Misc/Guid.h"
#include "Misc/TVariant.h"
#include <type_traits>

#define METASOUND_DEBUG_LITERALS 1

namespace Metasound
{
	enum class ELiteralType : uint8
	{
		None, // If the literal is None, TType(const FOperatorSettings&) or TType() will be invoked.
		Boolean,
		Integer,
		Float,
		String,
		UObjectProxy,
		NoneArray, // If this is set, TType(const FOperatorSettings&) or TType() will be invoked for each element in the array. 
		BooleanArray,
		IntegerArray,
		FloatArray,
		StringArray,
		UObjectProxyArray,
		Invalid,
	};


	/**
	 * FLiteral 
	 * Convenience wrapper for safely invoking the correct constructor.
	 * This can be used for int32, float, bool, FString, Audio::IProxyData, or TArray<Audio::IProxyData>
	 *
	 * For example:
	 * // Somewhere before DECLARE_METASOUND_DATA_REFERENCE_TYPES(FAudioBuffer...), ParseFrom<FAudioBuffer>(int32) is defined
	 * // which means this data type can be created from an integer.
	 * Metasound::FAudioBuffer ::Metasound::ParseFrom<Metasound::FAudioBuffer>(int32 InNumFrames, const ::Metasound::FOperatorSettings&)
	 * {
	 *     return Metasound::FAudioBuffer(InNumFrames);
	 * }
	 *
	 * //...
	 * // In the frontend, we know that int and pass it to somewhere in MetasoundGraphCore...
	 * FLiteral InitParam(512);
	 *
	 * //...
	 * // In the backend, we can safely construct an FAudioBuffer.
	 * if(InitParam.IsCompatibleWithType<Metasound::FAudioBuffer>())
	 * {
	 *     Metasound::FAudioBuffer AudioBuffer = InitParam.ParseTo<Metasound::FAudioBuffer>();
	 *     //...
	 * }
	 */
	struct FLiteral
	{
		struct FInvalid {};

		/* FNone is used in scenarios where an object is constructed with an FLiteral
		 * where the expected object constructor is the default constructor, or a
		 * constructor which accepts an FOperatorSettings.
		 */
		struct FNone {};

		FLiteral(const FLiteral& Other) = delete;
		FLiteral(FLiteral&& Other) = default;
		FLiteral& operator=(FLiteral&& Other) = default;

		using FVariantType = TVariant<
			FNone, bool, int32, float, FString, Audio::IProxyDataPtr, // Single value types
			TArray<FNone>, TArray<bool>, TArray<int32>, TArray<float>, TArray<FString>, TArray<Audio::IProxyDataPtr>, // Array of values types
			FInvalid
		>;

		FVariantType Value;

#if METASOUND_DEBUG_LITERALS
		private:

		FString DebugString;

		void InitDebugString()
		{
			// GetIndex returns the index of the current type set in the TVariant.
			// The index refers to the order of the template parameters to TVariant.
			switch (Value.GetIndex())
			{
				case 0:
					DebugString = TEXT("NONE");
				break;

				case 1:
					DebugString = FString::Printf(TEXT("Bool: %s"), Value.Get<bool>() ? TEXT("true") : TEXT("false"));
				break;

				case 2:
					DebugString = FString::Printf(TEXT("Int32: %d"), Value.Get<int32>());
				break;

				case 3:
					DebugString = FString::Printf(TEXT("Float: %f"), Value.Get<float>());
				break;

				case 4:
					DebugString = FString::Printf(TEXT("String: %s"), *Value.Get<FString>());
				break;

				case 5:
					DebugString = FString::Printf(TEXT("Object: %f"), *Value.Get<Audio::IProxyDataPtr>()->GetProxyTypeName().ToString());
				break;

				case 6:
					DebugString = TEXT("TArray<NONE>");
				break;

				case 7:
					DebugString = FString::Printf(TEXT("TArray<Bool>"));
				break;

				case 8:
					DebugString = FString::Printf(TEXT("TArray<int32>"));
				break;

				case 9:
					DebugString = FString::Printf(TEXT("TArray<float>"));
				break;

				case 10:
					DebugString = FString::Printf(TEXT("TArray<FString>"));
				break;

				case 11:
					DebugString = FString::Printf(TEXT("TArray<UObject>"));
				break;

				case 12:
					DebugString = TEXT("INVALID");
				break;

				default:
					static_assert(TVariantSize<FVariantType>::Value == 13, "Possible missing FVariantType case coverage");
					checkNoEntry();
			}
		}

		public:
#endif // METAOUND_DEBUG_LITERALS


		// builds an invalid FLiteral.
		static FLiteral CreateInvalid()
		{
			return FLiteral(FInvalid());
		}

		bool IsValid() const
		{
			return GetType() != ELiteralType::Invalid;
		}

		ELiteralType GetType() const
		{
			switch (Value.GetIndex())
			{
				case 0:
					return ELiteralType::None;

				case 1:
					return ELiteralType::Boolean;

				case 2:
					return ELiteralType::Integer;

				case 3:
					return ELiteralType::Float;

				case 4:
					return ELiteralType::String;

				case 5:
					return ELiteralType::UObjectProxy;

				case 6:
					return ELiteralType::NoneArray;

				case 7:
					return ELiteralType::BooleanArray;

				case 8:
					return ELiteralType::IntegerArray;

				case 9:
					return ELiteralType::FloatArray;

				case 10:
					return ELiteralType::StringArray;

				case 11:
					return ELiteralType::UObjectProxyArray;

				case 12:
				default:
					static_assert(TVariantSize<FVariantType>::Value == 13, "Possible missing FVariantType case coverage");
					return ELiteralType::Invalid;
			}
		}

		FLiteral Clone() const
		{
			// TODO: If Clone() is supported after reworking vertices,
			// wrap the Audio::IProxyData in a object that manages virtual
			// copy construction.  Then replace this switch with simple
			// TVariant copy constructor.
			switch (GetType())
			{
			case ELiteralType::Invalid:
				return CreateInvalid();

			case ELiteralType::None:
				return FLiteral(FNone());

			case ELiteralType::Boolean:
				return FLiteral(Value.Get<bool>());

			case ELiteralType::Float:
				return FLiteral(Value.Get<float>());

			case ELiteralType::Integer:
				return FLiteral(Value.Get<int32>());

			case ELiteralType::String:
				return FLiteral(Value.Get<FString>());

			case ELiteralType::UObjectProxy:
				if (const Audio::IProxyDataPtr& ProxyPtr = Value.Get<Audio::IProxyDataPtr>())
				{
					return FLiteral(ProxyPtr->Clone());
				}
				return CreateInvalid();

			case ELiteralType::NoneArray:
				return FLiteral(Value.Get<TArray<FNone>>());

			case ELiteralType::BooleanArray:
				return FLiteral(Value.Get<TArray<bool>>());

			case ELiteralType::FloatArray:
				return FLiteral(Value.Get<TArray<float>>());

			case ELiteralType::IntegerArray:
				return FLiteral(Value.Get<TArray<int32>>());

			case ELiteralType::StringArray:
				return FLiteral(Value.Get<TArray<FString>>());

			case ELiteralType::UObjectProxyArray:
				{
					TArray<Audio::IProxyDataPtr> ProxyPtrArrayCopy;
					for (const Audio::IProxyDataPtr& Ptr : Value.Get<TArray<Audio::IProxyDataPtr>>())
					{
						if (Ptr.IsValid())
						{
							ProxyPtrArrayCopy.Add(Ptr->Clone());
						}
					}
					return FLiteral(MoveTemp(ProxyPtrArrayCopy));
				}
				return CreateInvalid();

			default:
				checkNoEntry();
				return CreateInvalid();
			}
		}

		/** Construct a literal param with a single argument. */
		template<
			typename... ArgTypes,
			typename std::enable_if<1 == sizeof...(ArgTypes), int>::type = 0
			>
		FLiteral(ArgTypes&&... Args)
		{
			Value.Set<typename std::decay<ArgTypes...>::type>(Forward<ArgTypes>(Args)...);
#if METASOUND_DEBUG_LITERALS
			InitDebugString();
#endif
		}

		/** Construct a literal param with no arguments. */
		template<
			typename... ArgTypes,
			typename std::enable_if<0 == sizeof...(ArgTypes), int>::type = 0
			>
		FLiteral(ArgTypes&&... Args)
		{
			Value.Set<FNone>(FNone());
#if METASOUND_DEBUG_LITERALS
			InitDebugString();
#endif
		}

	};

	namespace MetasoundLiteralIntrinsics
	{
		// Default template for converting a template parameter to a literal argument type
		template<typename... ArgTypes>
		ELiteralType GetLiteralArgTypeFromDecayed()
		{
			return ELiteralType::Invalid;
		}

		template<>
		FORCEINLINE ELiteralType GetLiteralArgTypeFromDecayed<>()
		{
			return ELiteralType::None;
		}

		template<>
		FORCEINLINE ELiteralType GetLiteralArgTypeFromDecayed<bool>()
		{
			return ELiteralType::Boolean;
		}

		template<>
		FORCEINLINE ELiteralType GetLiteralArgTypeFromDecayed<int32>()
		{
			return ELiteralType::Integer;
		}

		template<>
		FORCEINLINE ELiteralType GetLiteralArgTypeFromDecayed<float>()
		{
			return ELiteralType::Float;
		}

		template<>
		FORCEINLINE ELiteralType GetLiteralArgTypeFromDecayed<FString>()
		{
			return ELiteralType::String;
		}

		template<>
		FORCEINLINE ELiteralType GetLiteralArgTypeFromDecayed<Audio::IProxyDataPtr>()
		{
			return ELiteralType::UObjectProxy;
		}

		template<>
		FORCEINLINE ELiteralType GetLiteralArgTypeFromDecayed<TArray<FLiteral::FNone>>()
		{
			return ELiteralType::NoneArray;
		}

		template<>
		FORCEINLINE ELiteralType GetLiteralArgTypeFromDecayed<TArray<bool>>()
		{
			return ELiteralType::BooleanArray;
		}

		template<>
		FORCEINLINE ELiteralType GetLiteralArgTypeFromDecayed<TArray<int32>>()
		{
			return ELiteralType::IntegerArray;
		}

		template<>
		FORCEINLINE ELiteralType GetLiteralArgTypeFromDecayed<TArray<float>>()
		{
			return ELiteralType::FloatArray;
		}

		template<>
		FORCEINLINE ELiteralType GetLiteralArgTypeFromDecayed<TArray<FString>>()
		{
			return ELiteralType::StringArray;
		}

		template<>
		FORCEINLINE ELiteralType GetLiteralArgTypeFromDecayed<TArray<Audio::IProxyDataPtr>>()
		{
			return ELiteralType::UObjectProxyArray;
		}
	}


	/** Provides literal type information for a given type. 
	 *
	 * @tparam ArgType - A C++ type.
	 */
	template<typename... ArgTypes>
	struct TLiteralTypeInfo
	{
		/** Returns the associated ELiteralType for the C++ type provided in the TLiteralTypeInfo<Type> */
		static const ELiteralType GetLiteralArgTypeEnum()
		{
			// Use decayed version of template arg to remove references and cv qualifiers. 
			return MetasoundLiteralIntrinsics::GetLiteralArgTypeFromDecayed<typename std::decay<ArgTypes...>::type>();
		}
	};

	/** Provides literal type information for a given type. 
	 *
	 * @tparam ArgType - A C++ type.
	 */
	template<>
	struct TLiteralTypeInfo<>
	{
		/** Returns the associated ELiteralType for the C++ type provided in the TLiteralTypeInfo<Type> */
		static const ELiteralType GetLiteralArgTypeEnum()
		{
			// Use decayed version of template arg to remove references and cv qualifiers. 
			return MetasoundLiteralIntrinsics::GetLiteralArgTypeFromDecayed<>();
		}
	};


	// This struct is passed to FInputNodeConstructorCallback and FOutputNodeConstructorCallback.
	struct FInputNodeConstructorParams
	{
		// the instance name and name of the specific connection that we should use.
		const FString& InNodeName;
		const FGuid& InInstanceID;
		const FString& InVertexName;

		FLiteral InitParam;
	};

	struct FOutputNodeConstructorParams
	{
		// the instance name and name of the specific connection that we should use.
		const FString& InNodeName;
		const FGuid& InInstanceID;
		const FString& InVertexName;
	};
}
