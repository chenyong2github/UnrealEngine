// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundValueNode.h"

#include "MetasoundNodeRegistrationMacro.h"

#define LOCTEXT_NAMESPACE "MetasoundStandardNodes"

namespace Metasound
{
	using FValueNodeInt32 = TValueNode<int32>;
 	METASOUND_REGISTER_NODE(FValueNodeInt32)

	using FValueNodeFloat = TValueNode<float>;
	METASOUND_REGISTER_NODE(FValueNodeFloat)

	using FValueNodeBool = TValueNode<bool>;
	METASOUND_REGISTER_NODE(FValueNodeBool)

	using FValueNodeString = TValueNode<FString>;
	METASOUND_REGISTER_NODE(FValueNodeString)
}

#undef LOCTEXT_NAMESPACE
