// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundNodeAsset.h"
#include "CoreMinimal.h"

#include "Backends/JsonStructSerializerBackend.h"
#include "StructSerializer.h"

UMetasound::UMetasound(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, AccessPoint(new Metasound::Frontend::FDescriptionAccessPoint(RootMetasoundDocument))
{
}

bool UMetasound::ExportToJSON(const FString& InAbsolutePath)
{
	if (TUniquePtr<FArchive> FileWriter = TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*InAbsolutePath)))
	{
		FJsonStructSerializerBackend Backend(*FileWriter, EStructSerializerBackendFlags::Default);
		FStructSerializer::Serialize<FMetasoundDocument>(RootMetasoundDocument, Backend);
		FileWriter->Close();

		return true;
	}
	else
	{
		ensureAlwaysMsgf(false, TEXT("Failed to create a filewriter with the given path."));
		return false;
	}
}
