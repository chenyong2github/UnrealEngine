// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "IAudioProxyInitializer.h"
#include "MetasoundOperatorSettings.h" 
#include "Misc/TVariant.h"

namespace Metasound
{
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

		typedef TVariant<bool, int, float, FString, Audio::IProxyDataPtr, TArray<Audio::IProxyDataPtr>> FVariantType;

		ELiteralArgType ConstructorArgType;
		FVariantType ConstructorArg;

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

	};

	// This struct is passed to FInputNodeConstructorCallback and FOutputNodeConstructorCallback.
	struct FInputNodeConstructorParams
	{
		// the instance name and name of the specific connection that we should use.
		const FString& InNodeName;
		const FString& InVertexName;

		FDataTypeLiteralParam InitParam;
	};

	struct FOutputNodeConstrutorParams
	{
		// the instance name and name of the specific connection that we should use.
		const FString& InNodeName;
		const FString& InVertexName;
	};
}
