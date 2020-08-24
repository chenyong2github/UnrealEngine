// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "MetasoundDataReference.h"
#include "MetasoundDataReferenceCollection.h"
#include "MetasoundOperatorSettings.h"
#include "IAudioProxyInitializer.h"

namespace Metasound
{
	/** IOperator
	 *
	 *  IOperator defines the interface for render time operations.  IOperators are created using an INodeOperatorFactory.
	 */
	class IOperator
	{
	public:
		/** Pointer to execute function for an operator.
		 *
		 * @param IOperator* - The operator associated with the function pointer.
		 */
		typedef void(*FExecuteFunction)(IOperator*);

		virtual ~IOperator() {}

		/** Return the input parameters associated with this IOperator.
		 *
		 * Implementations of IOperator should populate and return their input parameters
		 * if they want to enable callers to query and write to their parameters. Most IOperator
		 * implementations will return an empty FDataReferenceCollection because thier inputs are
		 * set during the IOperator's construction and do not need to be updated afterwards. The
		 * exceptions are input operators and graph operators which need to interface with external
		 * systems even after the operator is created.
		 */
		virtual const FDataReferenceCollection& GetInputs() const = 0;

		/** Return the output parameters associated with this IOperator.
		 *
		 * Implementations of IOperator should return a collection of their output parameters
		 * which other nodes can read.
		 */
		virtual const FDataReferenceCollection& GetOutputs() const = 0;

		/** Return the execution function to call during graph execution.
		 *
		 * The IOperator* argument to the FExecutionFunction will be the same IOperator instance
		 * which returned the execution function.
		 *
		 * nullptr return values are valid and signal an IOperator which does not need to be
		 * executed.
		 */
		virtual FExecuteFunction GetExecuteFunction() = 0;
	};
}

template <typename TDataType, typename TTypeToParse>
struct TTestIfDataTypeCtorIsImplemented
{
private:
	static constexpr bool bSupportsConstructionWithSettings = 
		TIsConstructible<TDataType, TTypeToParse, const ::Metasound::FOperatorSettings&>::Value
		|| TIsConstructible<TDataType, TTypeToParse, ::Metasound::FOperatorSettings&>::Value
		|| TIsConstructible<TDataType, TTypeToParse, ::Metasound::FOperatorSettings>::Value;

	static constexpr bool bSupportsConstructionWithoutSettings = TIsConstructible<TDataType, TTypeToParse>::Value;

public:
	/*
	static constexpr bool Value = TIsConstructible<TDataType, TTypeToParse, const ::Metasound::FOperatorSettings&>::Value
		|| TIsConstructible<TDataType, TTypeToParse>::Value;
		*/

	static constexpr bool Value = bSupportsConstructionWithSettings || bSupportsConstructionWithoutSettings;
};

template <typename TDataType>
struct TTestIfDataTypeDefaultCtorIsImplemented
{
public:
	static constexpr bool Value = TIsConstructible<TDataType, const ::Metasound::FOperatorSettings&>::Value;
};

namespace Metasound
{
	/*
	class IGraphOperator : public IOperator
	{
		public:

			virtual ~IGraphOperator() {}
	};
	*/

	/*
	const TCHAR* ToString(EConstructorArgType InType)
	{
		switch (InType)
		{
		case Metasound::EConstructorArgType::Boolean:
			return TEXT("Boolean");
			break;
		case Metasound::EConstructorArgType::Integer:
			return TEXT("Integer");
			break;
		case Metasound::EConstructorArgType::Float:
			return TEXT("Float");
			break;
		case Metasound::EConstructorArgType::String:
			return TEXT("String");
			break;
		default:
			return TEXT("");
			break;
		}
	}
	*/

	/**
	 * Convenience wrapper for safely invoking the correct constructor.
	 * This can be used for int32, float, bool, or FString.
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
	 * FDataTypeLiteralParam InitParam(512);
	 *
	 * //...
	 * // In the backend, we can safely construct an FAudioBuffer.
	 * if(InitParam.IsCompatibleWithType<Metasound::FAudioBuffer>())
	 * {
	 *     Metasound::FAudioBuffer AudioBuffer = InitParam.ParseTo<Metasound::FAudioBuffer>();
	 *     //...
	 * }
	 */
	struct FDataTypeLiteralParam
	{
		FDataTypeLiteralParam(const FDataTypeLiteralParam& Other) = delete;
		FDataTypeLiteralParam(FDataTypeLiteralParam&& Other) = default;
		FDataTypeLiteralParam& operator=(FDataTypeLiteralParam&& Other) = default;

		ELiteralArgType ConstructorArgType;
		TVariant<bool, int, float, FString, Audio::IProxyDataPtr, TArray<Audio::IProxyDataPtr>> ConstructorArg;

		// builds an invalid FDataTypeLiteralParam.
		static FDataTypeLiteralParam InvalidParam()
		{
			FDataTypeLiteralParam InitParam;
			InitParam.ConstructorArgType = ELiteralArgType::Invalid;
			return MoveTemp(InitParam);
		}

		bool IsValid() const
		{
			return ConstructorArgType != ELiteralArgType::Invalid;
		}

		FDataTypeLiteralParam()
			: ConstructorArgType(ELiteralArgType::None)
		{}

		FDataTypeLiteralParam(bool bInValue)
			: ConstructorArgType(ELiteralArgType::Boolean)
		{
			ConstructorArg.Set<bool>(bInValue);
		}

		FDataTypeLiteralParam(int32 InValue)
			: ConstructorArgType(ELiteralArgType::Integer)
		{
			ConstructorArg.Set<int32>(InValue);
		}

		FDataTypeLiteralParam(float InValue)
			: ConstructorArgType(ELiteralArgType::Float)
		{
			ConstructorArg.Set<float>(InValue);
		}

		FDataTypeLiteralParam(const FString& InString)
			: ConstructorArgType(ELiteralArgType::String)
		{
			ConstructorArg.Set<FString>(InString);
		}

		FDataTypeLiteralParam(Audio::IProxyDataPtr&& InProxy)
			: ConstructorArgType(ELiteralArgType::UObjectProxy)
		{
			ConstructorArg.Set<Audio::IProxyDataPtr>(MoveTemp(InProxy));
		}

		FDataTypeLiteralParam(TArray<Audio::IProxyDataPtr>&& InProxyArray)
			: ConstructorArgType(ELiteralArgType::UObjectProxyArray)
		{
			ConstructorArg.Set<TArray<Audio::IProxyDataPtr>>(MoveTemp(InProxyArray));
		}

		template<typename DataType>
		bool IsCompatibleWithType()
		{
			switch (ConstructorArgType)
			{
				case ELiteralArgType::Float:
				{
					return TTestIfDataTypeCtorIsImplemented<DataType, float>::Value;
				}
				case ELiteralArgType::Integer:
				{
					return TTestIfDataTypeCtorIsImplemented<DataType, int32>::Value;
				}
				case ELiteralArgType::Boolean:
				{
					return TTestIfDataTypeCtorIsImplemented<DataType, bool>::Value;
				}
				case ELiteralArgType::String:
				{
					return TTestIfDataTypeCtorIsImplemented<DataType, const FString&>::Value;
				}
				case ELiteralArgType::UObjectProxy:
				{
					return TTestIfDataTypeCtorIsImplemented<DataType, const Audio::IProxyData&>::Value;
				}
				case ELiteralArgType::UObjectProxyArray:
				{
					return TTestIfDataTypeCtorIsImplemented<DataType, const TArray<Audio::IProxyDataPtr>&>::Value;
				}
				case ELiteralArgType::None:
				{
					return TTestIfDataTypeDefaultCtorIsImplemented<DataType>::Value || TIsConstructible<DataType>::Value;
				}
				default:
				{
					checkNoEntry();
					return false;
				}
			}
		}

	private:
		// This is a bucket constructor for when ConstructorArgType is unsupported by the data type we're trying to generate.
		// This will only be invoked if something has gone terribly wrong in the frontend.
		template<typename DataType>
		DataType InvalidConstructor()
		{
			checkNoEntry();

			// We don't know which constructors DataType supports, and we should never enter this code,
			// so we just dereference garbage memory here.
			return *reinterpret_cast<DataType*>(0x1);
		}

		// These are SFINAE-specialized to ensure we safely invoke the correct function,
		// or invoke InvalidConstructor if the correct function doesn't exist.
		// For each literal type, there are three SFINAE variants from which we generate a function at compile time:
		// 1: If there's a constructor that can take the literal type and FOperatorSettings, we use that.
		// 2: Otherwise, if there's a constructor that just takes the literal type, we'll use that.
		// 3: If neither constructor exists, we crash. Ideally this has already been caught by the static assert in the REGISTER_METASOUND_DATATYPE macro.

		template<typename DataType, typename TEnableIf<
			TIsConstructible<DataType, const TArray<Audio::IProxyDataPtr>&, const FOperatorSettings&>::Value
			, bool >::Type = true >
			DataType ParseAsAudioProxyArray(const FOperatorSettings& WithOperatorSettings)
		{
			check(ConstructorArgType == ELiteralArgType::UObjectProxyArray);
			const TArray<Audio::IProxyDataPtr>& ProxyArrayToParse = ConstructorArg.Get<TArray<Audio::IProxyDataPtr>>();
			return DataType(ProxyArrayToParse, WithOperatorSettings);
		}

		template<typename DataType, typename TEnableIf<
			TIsConstructible<DataType, const TArray<Audio::IProxyDataPtr>&>::Value && !TIsConstructible<DataType, const TArray<Audio::IProxyDataPtr>&, const FOperatorSettings&>::Value
			, bool >::Type = true >
			DataType ParseAsAudioProxyArray(const FOperatorSettings& WithOperatorSettings)
		{
			check(ConstructorArgType == ELiteralArgType::UObjectProxyArray);
			const TArray<Audio::IProxyDataPtr>& ProxyArrayToParse = ConstructorArg.Get<TArray<Audio::IProxyDataPtr>>();
			return DataType(ProxyArrayToParse);
		}

		template<typename DataType>
		DataType ParseAsAudioProxyArray(...)
		{
			return InvalidConstructor<DataType>();
		}

		template<typename DataType, typename TEnableIf<
			TIsConstructible<DataType, const Audio::IProxyData&, const FOperatorSettings&>::Value
			, bool >::Type = true >
		DataType ParseAsAudioProxy(const FOperatorSettings& WithOperatorSettings)
		{
			check(ConstructorArgType == ELiteralArgType::UObjectProxy);
			const Audio::IProxyDataPtr& ProxyToParse = ConstructorArg.Get<Audio::IProxyDataPtr>();
			return DataType(ProxyToParse, WithOperatorSettings);
		}

		template<typename DataType, typename TEnableIf<
			TIsConstructible<DataType, const Audio::IProxyData&>::Value && !TIsConstructible<DataType, const Audio::IProxyData&, const FOperatorSettings&>::Value
			, bool >::Type = true >
		DataType ParseAsAudioProxy(const FOperatorSettings& WithOperatorSettings)
		{
			check(ConstructorArgType == ELiteralArgType::UObjectProxy);
			const Audio::IProxyDataPtr& ProxyToParse = ConstructorArg.Get<Audio::IProxyDataPtr>();
			check(ProxyToParse.IsValid());
			return DataType(*ProxyToParse);
		}

		template<typename DataType>
		DataType ParseAsAudioProxy(...)
		{
			return InvalidConstructor<DataType>();
		}

		template<typename DataType, typename TEnableIf< 
			TIsConstructible<DataType, const FString&, const FOperatorSettings&>::Value
			, bool >::Type = true >
		DataType ParseAsString(const FOperatorSettings& WithOperatorSettings)
		{
			check(ConstructorArgType == ELiteralArgType::String);
			FString StringToParse = ConstructorArg.Get<FString>();
			return DataType(StringToParse, WithOperatorSettings);
		}

		template<typename DataType, typename TEnableIf< 
			TIsConstructible<DataType, const FString&>::Value && !TIsConstructible<DataType, const FString&, const FOperatorSettings&>::Value
			, bool >::Type = true >
		DataType ParseAsString(const FOperatorSettings& WithOperatorSettings)
		{
			check(ConstructorArgType == ELiteralArgType::String);
			FString StringToParse = ConstructorArg.Get<FString>();
			return DataType(StringToParse);
		}

		template<typename DataType>
		DataType ParseAsString(...)
		{
			return InvalidConstructor<DataType>();
		}

		template<typename DataType, typename TEnableIf< 
			TIsConstructible<DataType, float, const FOperatorSettings&>::Value
			, bool >::Type = true >
		DataType ParseAsFloat(const FOperatorSettings& WithOperatorSettings)
		{
			check(ConstructorArgType == ELiteralArgType::Float);
			float ValueToParse = ConstructorArg.Get<float>();
			return DataType(ValueToParse, WithOperatorSettings);
		}

		template<typename DataType, typename TEnableIf< 
			TIsConstructible<DataType, float >::Value && !TIsConstructible<DataType, float, const FOperatorSettings&>::Value
			, bool >::Type = true >
		DataType ParseAsFloat(const FOperatorSettings& WithOperatorSettings)
		{
			check(ConstructorArgType == ELiteralArgType::Float);
			float ValueToParse = ConstructorArg.Get<float>();
			return DataType(ValueToParse);
		}

		template<typename DataType>
		DataType ParseAsFloat(...)
		{
			return InvalidConstructor<DataType>();
		}

		template<typename DataType, typename TEnableIf< 
			TIsConstructible<DataType, int32, const FOperatorSettings&>::Value
			, bool >::Type = true >
		DataType ParseAsInt(const FOperatorSettings& WithOperatorSettings)
		{
			check(ConstructorArgType == ELiteralArgType::Integer);
			int32 ValueToParse = ConstructorArg.Get<int32>();
			return DataType(ValueToParse, WithOperatorSettings);
		}

		template<typename DataType, typename TEnableIf< 
			TIsConstructible<DataType, int32>::Value && !TIsConstructible<DataType, int32, const FOperatorSettings&>::Value
			, bool >::Type = true >
		DataType ParseAsInt(const FOperatorSettings& WithOperatorSettings)
		{
			check(ConstructorArgType == ELiteralArgType::Integer);
			int32 ValueToParse = ConstructorArg.Get<int32>();
			return DataType(ValueToParse);
		}

		template<typename DataType>
		DataType ParseAsInt(...)
		{
			return InvalidConstructor<DataType>();
		}

		template<typename DataType, typename TEnableIf< 
			TIsConstructible<DataType, bool, const FOperatorSettings&>::Value
			, bool >::Type = true >
		DataType ParseAsBool(const FOperatorSettings& WithOperatorSettings)
		{
			check(ConstructorArgType == ELiteralArgType::Boolean);
			bool ValueToParse = ConstructorArg.Get<bool>();
			return DataType(ValueToParse, WithOperatorSettings);
		}

		template<typename DataType, typename TEnableIf< 
			TIsConstructible<DataType, bool>::Value && !TIsConstructible<DataType, bool, const FOperatorSettings&>::Value
			, bool >::Type = true >
		DataType ParseAsBool(const FOperatorSettings& WithOperatorSettings)
		{
			check(ConstructorArgType == ELiteralArgType::Boolean);
			bool ValueToParse = ConstructorArg.Get<bool>();
			return DataType(ValueToParse);
		}

		template<typename DataType>
		DataType ParseAsBool(...)
		{
			return InvalidConstructor<DataType>();
		}


		template<typename DataType, typename TEnableIf< 
			TIsConstructible<DataType, const FOperatorSettings&>::Value
			, bool >::Type = true >
		DataType ParseWithDefaultConstructor(const FOperatorSettings& WithOperatorSettings)
		{
			check(ConstructorArgType == ELiteralArgType::None);
			return DataType(WithOperatorSettings);
		}

		template<typename DataType, typename TEnableIf< 
			TIsConstructible<DataType>::Value && !TIsConstructible<DataType, const FOperatorSettings&>::Value
			, bool>::Type = true >
		DataType ParseWithDefaultConstructor(const FOperatorSettings& WithOperatorSettings)
		{
			check(ConstructorArgType == ELiteralArgType::None);
			return DataType();
		}

		template<typename DataType>
		DataType ParseWithDefaultConstructor(...)
		{
			return InvalidConstructor<DataType>();
		}

	public:

		// This can be used to invoke the correct constructor for this init param for a data type.
		template<typename DataType>
		DataType ParseTo(const FOperatorSettings& WithOperatorSettings)
		{
			switch (ConstructorArgType)
			{
				case ELiteralArgType::Float:
				{
					return ParseAsFloat<DataType>(WithOperatorSettings);
				}
				case ELiteralArgType::Integer:
				{
					return ParseAsInt<DataType>(WithOperatorSettings);
				}
				case ELiteralArgType::Boolean:
				{
					return ParseAsBool<DataType>(WithOperatorSettings);
				}
				case ELiteralArgType::String:
				{
					return ParseAsString<DataType>(WithOperatorSettings);
				}
				case ELiteralArgType::UObjectProxy:
				{
					return ParseAsAudioProxy<DataType>(WithOperatorSettings);
				}
				case ELiteralArgType::UObjectProxyArray:
				{
					return ParseAsAudioProxyArray<DataType>(WithOperatorSettings);
				}
				case ELiteralArgType::None:
				{
					return ParseWithDefaultConstructor<DataType>(WithOperatorSettings);
				}
				default:
				{
					return InvalidConstructor<DataType>();
				}
			}
		}
	};

	// This struct is passed to FInputNodeConstructorCallback and FOutputNodeConstructorCallback.
	struct FInputNodeConstructorParams
	{
		// the instance name and name of the specific connection that we should use.
		const FString& InNodeName;
		const FString& InVertexName;

		// the operator settings that we will compile with
		const FOperatorSettings& InSettings;

		FDataTypeLiteralParam InitParam;
	};

	struct FOutputNodeConstrutorParams
	{
		// the instance name and name of the specific connection that we should use.
		const FString& InNodeName;
		const FString& InVertexName;
	};
}
