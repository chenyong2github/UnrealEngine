// Copyright Epic Games, Inc. All Rights Reserved.


#include "MetasoundFrontend.h"

#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "MetasoundAudioBuffer.h"
#include "MetasoundDataTypeRegistrationMacro.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendGraph.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendRegistryTransaction.h"
#include "MetasoundJsonBackend.h"
#include "MetasoundOperatorBuilder.h"
#include "MetasoundPrimitives.h"
#include "MetasoundRouter.h"
#include "MetasoundVertex.h"
#include "Modules/ModuleManager.h"
#include "StructDeserializer.h"
#include "StructSerializer.h"
#include "Serialization/MemoryReader.h"

namespace Metasound
{
	namespace Frontend
	{
		TArray<FNodeClassInfo> GetAllAvailableNodeClasses(FRegistryTransactionID* OutCurrentRegistryTransactionID)
		{
			FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get();
			if (ensure(nullptr != Registry))
			{
				return Registry->GetAllAvailableNodeClasses(OutCurrentRegistryTransactionID);
			}
			else
			{
				if (nullptr != OutCurrentRegistryTransactionID)
				{
					*OutCurrentRegistryTransactionID = GetOriginRegistryTransactionID();
				}
				return TArray<FNodeClassInfo>();
			}
		}

		TArray<const IRegistryTransaction*> GetRegistryTransactionsSince(FRegistryTransactionID InTransactionID, FRegistryTransactionID* OutCurrentTransactionID)
		{
			FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get();
			if (ensure(nullptr != Registry))
			{
				return Registry->GetRegistryTransactionsSince(InTransactionID, OutCurrentTransactionID);
			}
			else
			{
				if (nullptr != OutCurrentTransactionID)
				{
					*OutCurrentTransactionID = GetOriginRegistryTransactionID();
				}
				return TArray<const IRegistryTransaction*>();
			}
		}

		// gets all metadata (name, description, author, what to say if it's missing) for a given node.
		FMetasoundFrontendClassMetadata GenerateClassMetadata(const FNodeClassInfo& InInfo)
		{
			return GenerateClassDescription(InInfo).Metadata;
		}

		FMetasoundFrontendClass GenerateClassDescription(const FNodeClassMetadata& InNodeMetadata, EMetasoundFrontendClassType ClassType)
		{
			FMetasoundFrontendClass ClassDescription;

			ClassDescription.Metadata = FMetasoundFrontendClassMetadata(InNodeMetadata);
			ClassDescription.Metadata.Type = ClassType;

			FMetasoundFrontendClassStyleDisplay DisplayStyle(InNodeMetadata.DisplayStyle);
			ClassDescription.Style = FMetasoundFrontendClassStyle
			{
				DisplayStyle
			};

			FMetasoundFrontendClassInterface& ClassInterface = ClassDescription.Interface;

			// External metasounds aren't dependent on any other nodes by definition, so all we need to do
			// is populate the Input and Output sets.
			const FInputVertexInterface& InputInterface = InNodeMetadata.DefaultInterface.GetInputInterface();
			TArray<int>& InputSortOrder = ClassInterface.InputStyle.DefaultSortOrder;
			for (auto& InputTuple : InputInterface)
			{
				FMetasoundFrontendClassInput ClassInput;

				ClassInput.Name = InputTuple.Value.GetVertexName();
				ClassInput.TypeName = InputTuple.Value.GetDataTypeName();
				ClassInput.VertexID = FGuid::NewGuid();
				ClassInput.Metadata.DisplayName = FText::FromString(InputTuple.Value.GetVertexName());

				const FDataVertexMetadata& VertexMetadata = InputTuple.Value.GetMetadata();
				ClassInput.Metadata.Description = VertexMetadata.Description;
				ClassInput.Metadata.bIsAdvancedDisplay = VertexMetadata.bIsAdvancedDisplay;

				FLiteral DefaultLiteral = InputTuple.Value.GetDefaultLiteral();
				if (DefaultLiteral.GetType() != ELiteralType::Invalid)
				{
					ClassInput.DefaultLiteral.SetFromLiteral(DefaultLiteral);
				}

				// Advanced display items are pushed to bottom of sort order
				int32 OrderIndex = InputInterface.GetOrderIndex(InputTuple.Key);
				if (ClassInput.Metadata.bIsAdvancedDisplay)
				{
					OrderIndex += InputInterface.Num();
				}
				InputSortOrder.Add(OrderIndex);

				ClassInterface.Inputs.Add(ClassInput);
			}

			const FOutputVertexInterface& OutputInterface = InNodeMetadata.DefaultInterface.GetOutputInterface();
			TArray<int32>& OutputSortOrder = ClassInterface.InputStyle.DefaultSortOrder;
			for (auto& OutputTuple : OutputInterface)
			{
				FMetasoundFrontendClassOutput ClassOutput;

				ClassOutput.Name = OutputTuple.Value.GetVertexName();
				ClassOutput.TypeName = OutputTuple.Value.GetDataTypeName();
				ClassOutput.VertexID = FGuid::NewGuid();
				ClassOutput.Metadata.DisplayName = FText::FromString(OutputTuple.Value.GetVertexName());

				const FDataVertexMetadata& VertexMetadata = OutputTuple.Value.GetMetadata();
				ClassOutput.Metadata.Description = VertexMetadata.Description;
				ClassOutput.Metadata.bIsAdvancedDisplay = VertexMetadata.bIsAdvancedDisplay;

				// Advanced display items are pushed to bottom below non-advanced
				int32 OrderIndex = OutputInterface.GetOrderIndex(OutputTuple.Key);
				if (ClassOutput.Metadata.bIsAdvancedDisplay)
				{
					OrderIndex += OutputInterface.Num();
				}
				ClassInterface.OutputStyle.DefaultSortOrder.Add(OrderIndex);

				ClassDescription.Interface.Outputs.Add(ClassOutput);
			}

			for (auto& EnvTuple : InNodeMetadata.DefaultInterface.GetEnvironmentInterface())
			{
				FMetasoundFrontendClassEnvironmentVariable EnvVar;

				EnvVar.Name = EnvTuple.Value.GetVertexName();
				EnvVar.Metadata.DisplayName = FText::FromString(EnvTuple.Value.GetVertexName());
				EnvVar.Metadata.Description = EnvTuple.Value.GetDescription();
				EnvVar.bIsRequired = true;

				ClassDescription.Interface.Environment.Add(EnvVar);
			}

			return ClassDescription;
		}

		FMetasoundFrontendClass GenerateClassDescription(const FNodeClassInfo& InNodeInfo)
		{
			FMetasoundFrontendClass OutClass;

			FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get();

			if (ensure(nullptr != Registry))
			{
				bool bSuccess = Registry->FindFrontendClassFromRegistered(InNodeInfo, OutClass);
				ensureAlwaysMsgf(bSuccess, TEXT("Cannot generate description of unregistered node [RegistryKey:%s]"), *InNodeInfo.LookupKey);
			}

			return OutClass;
		}

		TArray<FName> GetAllAvailableDataTypes()
		{
			return FMetasoundFrontendRegistryContainer::Get()->GetAllValidDataTypes();
		}

		bool GetTraitsForDataType(FName InDataType, FDataTypeRegistryInfo& OutInfo)
		{
			return FMetasoundFrontendRegistryContainer::Get()->GetInfoForDataType(InDataType, OutInfo);
		}

		bool ImportJSONToMetasound(const FString& InJSON, FMetasoundFrontendDocument& OutMetasoundDocument)
		{
			TArray<uint8> ReadBuffer;
			ReadBuffer.SetNumUninitialized(InJSON.Len() * sizeof(ANSICHAR));
			FMemory::Memcpy(ReadBuffer.GetData(), StringCast<ANSICHAR>(*InJSON).Get(), InJSON.Len() * sizeof(ANSICHAR));
			FMemoryReader MemReader(ReadBuffer);

			TJsonStructDeserializerBackend<DefaultCharType> Backend(MemReader);
			bool DeserializeResult = FStructDeserializer::Deserialize(OutMetasoundDocument, Backend);

			MemReader.Close();
			return DeserializeResult && !MemReader.IsError();
		}

		bool ImportJSONAssetToMetasound(const FString& InPath, FMetasoundFrontendDocument& OutMetasoundDocument)
		{
			if (TUniquePtr<FArchive> FileReader = TUniquePtr<FArchive>(IFileManager::Get().CreateFileReader(*InPath)))
			{
				TJsonStructDeserializerBackend<DefaultCharType> Backend(*FileReader);
				bool DeserializeResult = FStructDeserializer::Deserialize(OutMetasoundDocument, Backend);

				FileReader->Close();
				return DeserializeResult && !FileReader->IsError();
			}

			return false;
		}

	}
}

class FMetasoundFrontendModule : public IModuleInterface
{
	virtual void StartupModule() override
	{
		FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get();
		if (ensure(nullptr != Registry))
		{
			Registry->RegisterPendingNodes();
		}
	}
};

IMPLEMENT_MODULE(FMetasoundFrontendModule, MetasoundFrontend);
