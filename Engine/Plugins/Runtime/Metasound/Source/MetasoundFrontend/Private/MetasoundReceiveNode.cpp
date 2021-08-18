// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundReceiveNode.h"

namespace Metasound
{
	namespace ReceiveNodeInfo
	{
		const FString& GetAddressInputName()
		{
			static const FString InputName(TEXT("Address"));
			return InputName;
		}

		const FString& GetDefaultDataInputName()
		{
			static const FString DefaultDataName(TEXT("Default"));
			return DefaultDataName;
		}

		const FString& GetOutputName()
		{
			static const FString OutputName(TEXT("Out"));
			return OutputName;
		}

		FNodeClassName GetClassNameForDataType(const FName& InDataTypeName)
		{
			return FNodeClassName{ "Receive", InDataTypeName, FName() };
		}

		int32 GetCurrentMajorVersion() { return 1; }

		int32 GetCurrentMinorVersion() { return 0; }
	}
}
