// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MetasoundNodeInterface.h"
#include "Templates/IntegralConstant.h"
#include "MetasoundFrontendRegistries.h"

// Utility class to ensure that a node class can use the constructor the frontend uses.
template <typename NodeClass>
struct ConstructorTakesNodeInitData {

	// Use SFINAE trick to see if we have a valid constructor:
	template<typename T>
	static uint16 TestForConstructor(decltype(T(::Metasound::FNodeInitData()))*);

	template<typename T>
	static uint8 TestForConstructor(...);

	static const bool Value = sizeof(TestForConstructor<NodeClass>(nullptr)) == sizeof(uint16);
};

template <typename FNodeType>
bool RegisterNodeWithFrontend()
{
	// if we reenter this code (because DECLARE_METASOUND_DATA_REFERENCE_TYPES was called twice with the same type),
	// we catch it here.
	static bool bAlreadyRegisteredThisDataType = false;
	if (bAlreadyRegisteredThisDataType)
	{
		UE_LOG(LogTemp, Display, TEXT("Tried to call METASOUND_REGISTER_NODE twice with the same class. ignoring the second call. Likely because METASOUND_REGISTER_NODE is in a header that's used in multiple modules. Consider moving it to a private header or cpp file."));
		return false;
	}

	bAlreadyRegisteredThisDataType = true;

	bool bSuccessfullyRegisteredNode = FMetasoundFrontendRegistryContainer::Get()->RegisterExternalNode(
		[](const Metasound::FNodeInitData& InInitData) -> TUniquePtr<Metasound::INode>
		{
			return TUniquePtr<Metasound::INode>(new FNodeType(InInitData));
		});

	ensureAlwaysMsgf(bSuccessfullyRegisteredNode, TEXT("Registering node class failed. Please check the logs."));
	return bSuccessfullyRegisteredNode;
}

#define METASOUND_REGISTER_NODE(NodeClass) \
	 static_assert(std::is_base_of<::Metasound::INodeBase, NodeClass>::value, "To be registered as a  Metasound Node," #NodeClass "need to be a derived class from Metasound::INodeBase, Metasound::INode, or Metasound::FNode."); \
	 static_assert(::ConstructorTakesNodeInitData<NodeClass>::Value, "In order to be registered as a Metasound Node, " #NodeClass " needs to implement the following public constructor: " #NodeClass "(const Metasound::FNodeInitData& InInitData);"); \
	 static bool bSuccessfullyRegistered##NodeClass  = FMetasoundFrontendRegistryContainer::Get()->EnqueueInitCommand([](){ ::RegisterNodeWithFrontend<NodeClass>(); }); // This static bool is useful for debugging, but also is the only way the compiler will let us call this function outside of an expression.

