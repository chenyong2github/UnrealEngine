// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundReceiveNode.h"

namespace Metasound
{
	namespace ReceiveNodeInfo
	{
		const FVertexName& GetAddressInputName()
		{
			static const FVertexName InputName(TEXT("Address"));
			return InputName;
		}

		const FVertexName& GetDefaultDataInputName()
		{
			static const FVertexName DefaultDataName(TEXT("Default"));
			return DefaultDataName;
		}

		const FVertexName& GetOutputName()
		{
			static const FVertexName OutputName(TEXT("Out"));
			return OutputName;
		}

		FNodeClassName GetClassNameForDataType(const FName& InDataTypeName)
		{
			return FNodeClassName { "Receive", InDataTypeName, FName() };
		}

		int32 GetCurrentMajorVersion() { return 1; }

		int32 GetCurrentMinorVersion() { return 0; }
	}
}
