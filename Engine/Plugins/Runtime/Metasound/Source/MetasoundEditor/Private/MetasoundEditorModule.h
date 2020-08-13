// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Modules/ModuleInterface.h"
#include "MetasoundFrontendRegistries.h"
#include "Templates/Function.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMetasoundEditor, Log, All);


namespace Metasound
{
	namespace Editor
	{
		struct FEditorDataType
		{
			FEdGraphPinType PinType;
			FDataTypeRegistryInfo RegistryInfo;

			FEditorDataType(FName InGraphDataType, const FDataTypeRegistryInfo& InRegistryInfo)
				: PinType(InGraphDataType, NAME_None, nullptr, EPinContainerType::None, false, FEdGraphTerminalType())
				, RegistryInfo(InRegistryInfo)
			{
			}
		};

		class METASOUNDEDITOR_API IMetasoundEditorModule : public IModuleInterface
		{
		public:
			virtual void RegisterDataType(FName InPinCategoryName, const FDataTypeRegistryInfo& InRegistryInfo) = 0;
			virtual const FEditorDataType& FindDataType(FName InDataTypeName) const = 0;
			virtual void IterateDataTypes(TUniqueFunction<void(const FEditorDataType&)> InDataTypeFunction) const = 0;
		};
	} // namespace Editor
} // namespace Metasound
