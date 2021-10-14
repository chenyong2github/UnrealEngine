// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DetailCategoryBuilder.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "MetasoundFrontendDataTypeRegistry.h"
#include "MetasoundFrontendLiteral.h"
#include "MetasoundFrontendRegistries.h"
#include "Modules/ModuleInterface.h"
#include "PropertyHandle.h"
#include "Templates/Function.h"


class UMetasoundEditorGraph;
class UMetasoundEditorGraphInputLiteral;

DECLARE_LOG_CATEGORY_EXTERN(LogMetasoundEditor, Log, All);


namespace Metasound
{
	namespace Editor
	{
		using FDataTypeRegistryInfo = Frontend::FDataTypeRegistryInfo;

		struct FEditorDataType
		{
			FEdGraphPinType PinType;
			FDataTypeRegistryInfo RegistryInfo;

			FEditorDataType(FEdGraphPinType&& InPinType, const FDataTypeRegistryInfo& InRegistryInfo)
				: PinType(MoveTemp(InPinType))
				, RegistryInfo(InRegistryInfo)
			{
			}
		};

		class METASOUNDEDITOR_API IMetaSoundInputLiteralCustomization
		{
		public:
			virtual ~IMetaSoundInputLiteralCustomization() = default;

			virtual void CustomizeLiteral(UMetasoundEditorGraphInputLiteral& InLiteral, TSharedPtr<IPropertyHandle> InDefaultValueHandle) = 0;
		};

		class METASOUNDEDITOR_API IMetaSoundInputLiteralCustomizationFactory
		{
		public:
			virtual ~IMetaSoundInputLiteralCustomizationFactory() = default;

			virtual TUniquePtr<IMetaSoundInputLiteralCustomization> CreateLiteralCustomization(IDetailCategoryBuilder& DefaultCategoryBuilder) const = 0;
		};

		class METASOUNDEDITOR_API IMetasoundEditorModule : public IModuleInterface
		{
		public:
			virtual void RegisterDataType(FName InPinCategoryName, FName InPinSubCategoryName, const FDataTypeRegistryInfo& InRegistryInfo) = 0;
			virtual const FEditorDataType& FindDataType(FName InDataTypeName) const = 0;
			virtual bool IsMetaSoundAssetClass(const FName InClassName) const = 0;
			virtual bool IsRegisteredDataType(FName InDataTypeName) const = 0;
			virtual void IterateDataTypes(TUniqueFunction<void(const FEditorDataType&)> InDataTypeFunction) const = 0;

			virtual TUniquePtr<IMetaSoundInputLiteralCustomization> CreateInputLiteralCustomization(UClass& InClass, IDetailCategoryBuilder& DefaultCategoryBuilder) const = 0;

			virtual const TSubclassOf<UMetasoundEditorGraphInputLiteral> FindInputLiteralClass(EMetasoundFrontendLiteralType InLiteralType) const = 0;
		};
	} // namespace Editor
} // namespace Metasound
