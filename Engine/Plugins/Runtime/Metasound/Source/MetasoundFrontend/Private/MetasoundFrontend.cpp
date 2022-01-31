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
		FMetasoundFrontendClass GenerateClass(const FNodeClassMetadata& InNodeMetadata, EMetasoundFrontendClassType ClassType)
		{
			FMetasoundFrontendClass ClassDescription;

			ClassDescription.Metadata = FMetasoundFrontendClassMetadata::GenerateClassMetadata(InNodeMetadata, ClassType);
			ClassDescription.Interface = FMetasoundFrontendClassInterface::GenerateClassInterface(InNodeMetadata.DefaultInterface);
			ClassDescription.Style = FMetasoundFrontendClassStyle::GenerateClassStyle(InNodeMetadata.DisplayStyle);

			return ClassDescription;
		}

		FMetasoundFrontendClass GenerateClass(const FNodeRegistryKey& InKey)
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
