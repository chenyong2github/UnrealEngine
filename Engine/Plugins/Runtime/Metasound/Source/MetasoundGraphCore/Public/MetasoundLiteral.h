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
		Boolean,
		Integer,
		Float,
		String,
		UObjectProxy,
		UObjectProxyArray,
		None, // If this is set, we will invoke TType(const FOperatorSettings&) If that constructor exists, or the default constructor if not.
		Invalid,
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
		FORCEINLINE ELiteralType GetLiteralArgTypeFromDecayed<TArray<Audio::IProxyDataPtr>>()
		{
			return ELiteralType::UObjectProxyArray;
		}

		template<>
		FORCEINLINE ELiteralType GetLiteralArgTypeFromDecayed<>()
		{
			return ELiteralType::None;
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
		struct FInvalidLiteralState {};
		struct FEmptyLiteralState {};

		FLiteral(const FLiteral& Other) = delete;
		FLiteral(FLiteral&& Other) = default;
		FLiteral& operator=(FLiteral&& Other) = default;

		using FVariantType = TVariant<FInvalidLiteralState, FEmptyLiteralState, bool, int, float, FString, Audio::IProxyDataPtr, TArray<Audio::IProxyDataPtr>>;

		FVariantType Value;

#if METASOUND_DEBUG_LITERALS
		private:
		FString DebugString;

		void InitDebugString()
		{
			switch (Value.GetIndex())
			{
				case 0:
					DebugString = TEXT("INVALID");
				break;

				case 1:
					DebugString = TEXT("EMPTY");
				break;

				case 2:
					DebugString = FString::Printf(TEXT("Bool: %s"), Value.Get<bool>() ? TEXT("true") : TEXT("false"));
				break;

				case 3:
					DebugString = FString::Printf(TEXT("Int32: %d"), Value.Get<int32>());
				break;

				case 4:
					DebugString = FString::Printf(TEXT("Float: %f"), Value.Get<float>());
				break;

				case 5:
					DebugString = FString::Printf(TEXT("String: %s"), *Value.Get<FString>());
				break;

				case 6:
					DebugString = FString::Printf(TEXT("Object: %f"), *Value.Get<Audio::IProxyDataPtr>()->GetProxyTypeName().ToString());
				break;

				case 7:
				default:
					checkNoEntry();
			}
		}

		public:
#endif // METAOUND_DEBUG_LITERALS


		// builds an invalid FLiteral.
		static FLiteral CreateInvalid()
		{
			return FLiteral(FInvalidLiteralState());
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
					return ELiteralType::Invalid;

				case 1:
					return ELiteralType::None;

				case 2:
					return ELiteralType::Boolean;

				case 3:
					return ELiteralType::Integer;

				case 4:
					return ELiteralType::Float;

				case 5:
					return ELiteralType::String;

				case 6:
					return ELiteralType::UObjectProxy;

				case 7:
					return ELiteralType::UObjectProxyArray;

				default:
					checkNoEntry();
					return ELiteralType::Invalid;
			}
		}

		FLiteral Clone() const
		{
			switch (GetType())
			{
			case ELiteralType::Invalid:
				return CreateInvalid();

			case ELiteralType::None:
				return FLiteral(FEmptyLiteralState());

			case ELiteralType::Boolean:
				return FLiteral(Value.Get<bool>());

			case ELiteralType::Float:
				return FLiteral(Value.Get<float>());

			case ELiteralType::Integer:
				return FLiteral(Value.Get<int32>());

			case ELiteralType::String:
				return FLiteral(Value.Get<FString>());

			case ELiteralType::UObjectProxy:
			{
				TUniquePtr<Audio::IProxyDataPtr> THing;
				return FLiteral(Value.Get<Audio::IProxyDataPtr>()->Clone());
			}

			case ELiteralType::UObjectProxyArray:
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
			Value.Set<FEmptyLiteralState>(FEmptyLiteralState());
#if METASOUND_DEBUG_LITERALS
			InitDebugString();
#endif
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
