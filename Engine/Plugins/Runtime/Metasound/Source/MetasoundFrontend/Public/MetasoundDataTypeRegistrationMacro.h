// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Casts.h"

#include "MetasoundAutoConverterNode.h"
#include "MetasoundConverterNodeRegistrationMacro.h"
#include "MetasoundDataFactory.h"
#include "MetasoundDataReference.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundInputNode.h"
#include "MetasoundNodeRegistrationMacro.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundOutputNode.h"
#include "MetasoundPrimitives.h"
#include "MetasoundReceiveNode.h"
#include "MetasoundRouter.h"
#include "MetasoundSendNode.h"

#include <type_traits>

namespace Metasound
{
	// SFINAE used to optionally invoke subclasses of IAudioProxyDataFactory when we can.
	template<typename UClassToUse, typename TEnableIf<TIsDerivedFrom<UClassToUse, IAudioProxyDataFactory>::Value, bool>::Type = true>
	IAudioProxyDataFactory* CastToAudioProxyDataFactory(UObject* InObject)
	{
		UClassToUse* DowncastObject = Cast<UClassToUse>(InObject);
		if (ensureAlways(DowncastObject))
		{
			return static_cast<IAudioProxyDataFactory*>(DowncastObject);
		}
		else
		{
			return nullptr;
		}
	}

	template<typename UClassToUse, typename TEnableIf<!TIsDerivedFrom<UClassToUse, IAudioProxyDataFactory>::Value, bool>::Type = true>
	IAudioProxyDataFactory* CastToAudioProxyDataFactory(UObject* InObject)
	{
		return nullptr;
	}

	// Helper utility to test if we can transmit a datatype between a send and a receive node.
	template <typename TDataType>
	struct TIsTransmittable
	{
	private:
		static constexpr bool bCanBeTransmitted =
			std::is_copy_constructible<TDataType>::value|| TIsDerivedFrom<TDataType, IAudioDataType>::Value;

	public:

		static constexpr bool Value = bCanBeTransmitted;
	};

	// This utility function can be used to optionally check to see if we can transmit a data type, and autogenerate send and receive nodes for that datatype.
	template<typename TDataType, typename TEnableIf<TIsTransmittable<TDataType>::Value, bool>::Type = true>
	void AttemptToRegisterSendAndReceiveNodes()
	{
		ensureAlways(RegisterNodeWithFrontend<Metasound::TSendNode<TDataType>>());
		ensureAlways(RegisterNodeWithFrontend<Metasound::TReceiveNode<TDataType>>());
	}

	template<typename TDataType, typename TEnableIf<!TIsTransmittable<TDataType>::Value, bool>::Type = true>
	void AttemptToRegisterSendAndReceiveNodes()
	{
		// This implementation intentionally noops, because Metasound::TIsTransmittable is false for this datatype.
		// This is either because the datatype is not trivially copyable, and thus can't be buffered between threads,
		// or it's not an audio buffer type, which we use Audio::FPatchMixerSplitter instances for.
	}



	// This utility function can be used to check to see if we can static cast between two types, and autogenerate a node for that static cast.
	template<typename TFromDataType, typename TToDataType, typename std::enable_if<std::is_convertible<TFromDataType, TToDataType>::value, int>::type = 0>
	void AttemptToRegisterConverter()
	{
		using FConverterNode = Metasound::TAutoConverterNode<TFromDataType, TToDataType>;

		const FNodeInfo& Metadata = FConverterNode::GetAutoConverterNodeMetadata();
		const Metasound::Frontend::FNodeRegistryKey Key = FMetasoundFrontendRegistryContainer::GetRegistryKey(Metadata);

		if (!std::is_same<TFromDataType, TToDataType>::value && !FMetasoundFrontendRegistryContainer::Get()->IsNodeRegistered(Key))
		{
			ensureAlways(RegisterNodeWithFrontend<FConverterNode>(Metadata));
			
			bool bSucessfullyRegisteredConversionNode = RegisterConversionNode<FConverterNode, TFromDataType, TToDataType>(FConverterNode::GetInputName(), FConverterNode::GetOutputName(), Metadata);
			ensureAlways(bSucessfullyRegisteredConversionNode);
		}
	}

	template<typename TFromDataType, typename TToDataType, typename std::enable_if<!std::is_convertible<TFromDataType, TToDataType>::value, int>::type = 0>
	void AttemptToRegisterConverter()
	{
		// This implementation intentionally noops, because static_cast<TFromDataType>(TToDataType&) is invalid.
	}

	// Here we attempt to infer and autogenerate conversions for basic datatypes.
	template<typename TDataType>
	void RegisterConverterNodes()
	{
		// Conversions to this data type:
		AttemptToRegisterConverter<bool, TDataType>();
		AttemptToRegisterConverter<int32, TDataType>();
		AttemptToRegisterConverter<float, TDataType>();
		AttemptToRegisterConverter<FString, TDataType>();

		// Conversions from this data type:
		AttemptToRegisterConverter<TDataType, bool>();
		AttemptToRegisterConverter<TDataType, int32>();
		AttemptToRegisterConverter<TDataType, float>();
		AttemptToRegisterConverter<TDataType, FString>();
	}

	template<typename TDataType, ELiteralArgType PreferredArgType = ELiteralArgType::None, typename UClassToUse = UObject>
	bool RegisterDataTypeWithFrontend()
	{
		// if we reenter this code (because DECLARE_METASOUND_DATA_REFERENCE_TYPES was called twice with the same type),
		// we catch it here.
		static bool bAlreadyRegisteredThisDataType = false;
		if (bAlreadyRegisteredThisDataType)
		{
			UE_LOG(LogTemp, Display, TEXT("Tried to call REGISTER_METASOUND_DATATYPE twice with the same class %s. ignoring the second call. Likely because REGISTER_METASOUND_DATATYPE is in a header that's used in multiple modules. Consider moving it to a private header or cpp file."), TDataReferenceTypeInfo<TDataType>::TypeName)
			return false;
		}

		bAlreadyRegisteredThisDataType = true;

		// Lambdas that generate our template-instantiated input and output nodes:
		FCreateInputNodeFunction InputNodeConstructor = [](FInputNodeConstructorParams&& InParams) -> TUniquePtr<INode>
		{
			return TUniquePtr<INode>(new TInputNode<TDataType>(InParams.InNodeName, InParams.InVertexName, MoveTemp(InParams.InitParam)));
		};

		FCreateOutputNodeFunction OutputNodeConstructor = [](const FOutputNodeConstrutorParams& InParams) -> TUniquePtr<INode>
		{
			return TUniquePtr<INode>(new TOutputNode<TDataType>(InParams.InNodeName, InParams.InVertexName));
		};
		
		// By default, this function should not be used, unless the preferred arg type is UObjectProxy or UObjectProxyArray, and UClassToUse should be specified.
		FCreateAudioProxyFunction ProxyGenerator = [](UObject* InObject) -> Audio::IProxyDataPtr
		{
			checkNoEntry();
			return Audio::IProxyDataPtr(nullptr);
		};

		// If this datatype uses a UObject or UObject array literal, we generate a lambda to build a proxy here:
		constexpr bool bSpecifiedUClassForProxy = !TIsSame<UClassToUse, UObject>::Value;
		if (bSpecifiedUClassForProxy)
		{
			static_assert(!bSpecifiedUClassForProxy || std::is_base_of<IAudioProxyDataFactory, UClassToUse>::value, "If a Metasound Datatype uses a UObject as a literal, the UClass of that object needs to also derive from Audio::IProxyDataFactory. See USoundWave as an example.");
			ProxyGenerator = [](UObject* InObject) -> Audio::IProxyDataPtr
			{
				IAudioProxyDataFactory* ObjectAsFactory = CastToAudioProxyDataFactory<UClassToUse>(InObject);

				if (ensureAlways(ObjectAsFactory))
				{
					static FName ProxySubsystemName = TEXT("Metasound");
					
					Audio::FProxyDataInitParams ProxyInitParams;
					ProxyInitParams.NameOfFeatureRequestingProxy = ProxySubsystemName;

					return ObjectAsFactory->CreateNewProxyData(ProxyInitParams);
				}
				else
				{
					return Audio::IProxyDataPtr(nullptr);
				}
			};
		}

		static const FName DataTypeName = GetMetasoundDataTypeName<TDataType>();

		// Pack all of our various constructor lambdas to a single struct.
		FDataTypeConstructorCallbacks Callbacks = { MoveTemp(InputNodeConstructor), MoveTemp(OutputNodeConstructor), MoveTemp(ProxyGenerator) };


		FDataTypeRegistryInfo RegistryInfo;

		RegistryInfo.DataTypeName = DataTypeName;
		RegistryInfo.PreferredLiteralType = PreferredArgType;

		RegistryInfo.bIsBoolParsable = TIsParsable<TDataType, bool>::Value;
		RegistryInfo.bIsIntParsable = TIsParsable<TDataType, int32>::Value;
		RegistryInfo.bIsFloatParsable = TIsParsable<TDataType, float>::Value;
		RegistryInfo.bIsStringParsable = TIsParsable<TDataType, FString>::Value;
		RegistryInfo.bIsProxyParsable = TIsParsable<TDataType, const Audio::IProxyDataPtr&>::Value;
		RegistryInfo.bIsProxyArrayParsable = TIsParsable<TDataType, const TArray<Audio::IProxyDataPtr>& >::Value;
		RegistryInfo.bIsDefaultParsable = TIsParsable<TDataType>::Value;
		
		RegistryInfo.ProxyGeneratorClass = UClassToUse::StaticClass();

		bool bSucceeded = FMetasoundFrontendRegistryContainer::Get()->RegisterDataType(RegistryInfo, MoveTemp(Callbacks));
		ensureAlwaysMsgf(bSucceeded, TEXT("Failed to register data type %s in the node registry!"), *GetMetasoundDataTypeString<TDataType>());
		
		RegisterConverterNodes<TDataType>();
		AttemptToRegisterSendAndReceiveNodes<TDataType>();
		
		return bSucceeded;
	}

	template<typename DataType>
	struct TMetasoundDataTypeRegistration
	{
		static_assert(std::is_same<DataType, typename std::decay<DataType>::type>::value, "DataType and decayed DataType must be the same");
		
		static constexpr bool bCanRegister = TInputNode<DataType>::bCanRegister;

		static const bool bSuccessfullyRegistered;
	};
}

// This should be used to expose a datatype as a potential input or output for a metasound graph.
// The first argument to the macro is the class to expose.
// the second argument is the display name of that type in the Metasound editor.
// Optionally, a Metasound::ELiteralArgType can be passed in to designate a preferred literal type-
// For example, if Metasound::ELiteralArgType::Float is passed in, we will default to using a float parameter to create this datatype.
// If no argument is passed in, we will infer a literal type to use.
// If 
// Metasound::ELiteralArgType::Invalid can be used to enforce that we don't provide space for a literal, in which case you should have a default constructor or a constructor that takes [const FOperatorSettings&] implemented.
// If you pass in a preferred arg type, please make sure that the passed in datatype has a matching constructor, since we won't check this until runtime.

#define CANNOT_REGISTER_METASOUND_DATA_TYPE_ASSERT_STRING(DataType) \
"To register " #DataType " to be used as a Metasounds input or output type, it needs a default constructor or one of the following constructors must be implemented:  " \
#DataType "(const ::Metasound::FOperatorSettings& InSettings), " \
#DataType "(const ::Metasound::FOperatorSettings& InSettings, bool InValue), " \
#DataType "(const ::Metasound::FOperatorSettings& InSettings, int32 InValue), " \
#DataType "(const ::Metasound::FOperatorSettings& InSettings, float InValue), " \
#DataType "(const ::Metasound::FOperatorSettings& InSettings, const FString& InString)" \
#DataType "(const ::Metasound::FOperatorSettings& InSettings, const Audio::IProxyDataPtr& InData),  or " \
#DataType "(const ::Metasound::FOperatorSettings& InSettings, const TArray<Audio::IProxyDataPtr>& InProxyArray)."

#define REGISTER_METASOUND_DATATYPE(DataType, DataTypeName, ...) \
	DEFINE_METASOUND_DATA_TYPE(DataType, DataTypeName); \
	static_assert(::Metasound::TMetasoundDataTypeRegistration<DataType>::bCanRegister, CANNOT_REGISTER_METASOUND_DATA_TYPE_ASSERT_STRING(DataType)); \
	template<> const bool ::Metasound::TMetasoundDataTypeRegistration<DataType>::bSuccessfullyRegistered = ::FMetasoundFrontendRegistryContainer::Get()->EnqueueInitCommand([](){ ::Metasound::RegisterDataTypeWithFrontend<DataType, ##__VA_ARGS__>(); }); // This static bool is useful for debugging, but also is the only way the compiler will let us call this function outside of an expression.

