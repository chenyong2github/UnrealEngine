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
		// gets all metadata (name, description, author, what to say if it's missing) for a given node.
		FMetasoundFrontendClassMetadata GenerateClassMetadata(const FNodeRegistryKey& InKey)
		{
			return GenerateClassDescription(InKey).Metadata;
		}

		FMetasoundFrontendClass GenerateClassDescription(const FNodeClassMetadata& InNodeMetadata, EMetasoundFrontendClassType ClassType)
		{
			FMetasoundFrontendClass ClassDescription;

			ClassDescription.Metadata = FMetasoundFrontendClassMetadata(InNodeMetadata);
			ClassDescription.Metadata.SetType(ClassType);

			FMetasoundFrontendClassStyleDisplay DisplayStyle(InNodeMetadata.DisplayStyle);
			ClassDescription.Style = FMetasoundFrontendClassStyle
			{
				DisplayStyle
			};

			FMetasoundFrontendClassInterface& ClassInterface = ClassDescription.Interface;

			const FInputVertexInterface& InputInterface = InNodeMetadata.DefaultInterface.GetInputInterface();
			FMetasoundFrontendInterfaceStyle InputStyle;
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
				InputStyle.DefaultSortOrder.Add(OrderIndex);

				ClassInterface.Inputs.Add(ClassInput);
			}
			ClassInterface.SetInputStyle(InputStyle);

			const FOutputVertexInterface& OutputInterface = InNodeMetadata.DefaultInterface.GetOutputInterface();
			FMetasoundFrontendInterfaceStyle OutputStyle;
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
				OutputStyle.DefaultSortOrder.Add(OrderIndex);

				ClassInterface.Outputs.Add(ClassOutput);
			}
			ClassInterface.SetOutputStyle(OutputStyle);

			for (auto& EnvTuple : InNodeMetadata.DefaultInterface.GetEnvironmentInterface())
			{
				FMetasoundFrontendClassEnvironmentVariable EnvVar;

				EnvVar.Name = EnvTuple.Value.GetVertexName();
				EnvVar.Metadata.DisplayName = FText::FromString(EnvTuple.Value.GetVertexName());
				EnvVar.Metadata.Description = EnvTuple.Value.GetDescription();
				EnvVar.bIsRequired = true;

				ClassInterface.Environment.Add(EnvVar);
			}

			return ClassDescription;
		}

		FMetasoundFrontendClass GenerateClassDescription(const FNodeRegistryKey& InKey)
		{
			FMetasoundFrontendClass OutClass;

			FMetasoundFrontendRegistryContainer* Registry = FMetasoundFrontendRegistryContainer::Get();

			if (ensure(nullptr != Registry))
			{
				bool bSuccess = Registry->FindFrontendClassFromRegistered(InKey, OutClass);
				ensureAlwaysMsgf(bSuccess, TEXT("Cannot generate description of unregistered node [RegistryKey:%s]"), *InKey);
			}

			return OutClass;
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
