// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeAssetImportData.h"
#include "InterchangeManager.h"
#include "InterchangePipelineBase.h"

#include "InterchangeCustomVersion.h"

#include "JsonObjectConverter.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Serialization/LargeMemoryReader.h"
#include "UObject/CoreRedirects.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(InterchangeAssetImportData)

void UInterchangeAssetImportData::PostLoad()
{
	Super::PostLoad();

	if (NodeContainer_DEPRECATED)
	{
		SetNodeContainer(NodeContainer_DEPRECATED.Get());

		NodeContainer_DEPRECATED = nullptr;
	}

	if (Pipelines_DEPRECATED.Num() > 0)
	{
		TransientPipelines.Empty();

		for (TObjectPtr<UObject>& PipelineObject : Pipelines_DEPRECATED)
		{
			if (PipelineObject)
			{
				TransientPipelines.Add(PipelineObject.Get());
			}
		}

		Pipelines_DEPRECATED.Empty();
	}
}

UObject* DeSerializePipeline(const FString& PipelineStr, UClass* PipelineClass)
{
	UInterchangePipelineBase* GeneratedPipeline = NewObject<UInterchangePipelineBase>(GetTransientPackage(), PipelineClass);

	TSharedPtr<FJsonObject> RootObject;
	TSharedRef< TJsonReader<> > JsonReader = TJsonReaderFactory<>::Create(PipelineStr);
	if (FJsonSerializer::Deserialize(JsonReader, RootObject))
	{
		const TSharedPtr<FJsonObject> JsonPipelineProperties = RootObject->GetObjectField(TEXT("GeneratedPipeline"));
		FJsonObjectConverter::JsonObjectToUStruct(JsonPipelineProperties.ToSharedRef(), PipelineClass, GeneratedPipeline, 0, 0);
	}

	GeneratedPipeline->UpdateWeakObjectPtrs();

	return GeneratedPipeline;
}

FString SerializePipeline(UObject* Pipeline)
{
	if (UClass* PipelineClass = Pipeline->GetClass())
	{
		TSharedRef<FJsonObject> RootObject = MakeShareable(new FJsonObject);
		TSharedRef<FJsonObject> PipelinePropertiesObject = MakeShareable(new FJsonObject);
		if (FJsonObjectConverter::UStructToJsonObject(PipelineClass, Pipeline, PipelinePropertiesObject, 0, 0))
		{
			RootObject->SetField(TEXT("GeneratedPipeline"), MakeShareable(new FJsonValueObject(PipelinePropertiesObject)));
		}
		//Write the json file
		FString Json;
		TSharedRef<TJsonWriter<> > JsonWriter = TJsonWriterFactory<>::Create(&Json, 0);
		if (FJsonSerializer::Serialize(RootObject, JsonWriter))
		{
			return Json;
		}
	}
	
	return TEXT("");
}

void UInterchangeAssetImportData::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FInterchangeCustomVersion::GUID);
	int32 CustomVersion = Ar.CustomVer(FInterchangeCustomVersion::GUID);

	if (Ar.IsSaving())
	{
		if (UInterchangeManager::IsInterchangeImportEnabled())
		{
			//serialize
			if (TransientNodeContainer)
			{
				FLargeMemoryWriter NodeContainerAr;
				TransientNodeContainer->SerializeNodeContainerData(NodeContainerAr);
				CachedNodeContainer = TArray64<uint8>(NodeContainerAr.GetData(), NodeContainerAr.TotalSize());
			}
			else
			{
				CachedNodeContainer.Reset();
			}

			CachedPipelines.Reset(TransientPipelines.Num());
			for (TObjectPtr<UObject>& PipelineObjectPtr : TransientPipelines)
			{
				UObject* PipelineObject = PipelineObjectPtr.Get();
				if (PipelineObject)
				{
					FString PipelineJSON = SerializePipeline(PipelineObject);

					FString PipelineFullName = PipelineObject->GetClass()->GetFullName();
					CachedPipelines.Emplace(PipelineFullName, PipelineJSON);
				}
			}
		}
	}

	if (CustomVersion >= FInterchangeCustomVersion::SerializedInterchangeObjectStoring)
	{
		Ar << CachedNodeContainer;
		Ar << CachedPipelines;
	}
}


UInterchangeBaseNodeContainer* UInterchangeAssetImportData::GetNodeContainer() const
{
	ProcessContainerCache();

	return Cast<UInterchangeBaseNodeContainer>(TransientNodeContainer.Get());
}

void UInterchangeAssetImportData::SetNodeContainer(UInterchangeBaseNodeContainer* InNodeContainer) 
{
	TransientNodeContainer = InNodeContainer; 
}


void UInterchangeAssetImportData::SetPipelines(const TArray<UObject*>& InPipelines) 
{
	TransientPipelines.Reset();
	for (UObject* Pipeline : InPipelines)
	{
		TransientPipelines.Add(Pipeline);
	}
}

TArray<UObject*> UInterchangeAssetImportData::GetPipelines() const
{
	ProcessPipelinesCache();

	TArray<UObject*> OutPipelines;
	for (TObjectPtr<UObject>& Pipeline : TransientPipelines)
	{
		if (Pipeline.Get())
		{
			OutPipelines.Add(Pipeline.Get());
		}
	}
	return OutPipelines;
}

int32 UInterchangeAssetImportData::GetNumberOfPipelines() const
{
	ProcessPipelinesCache();
	return TransientPipelines.Num();
}


const UInterchangeBaseNode* UInterchangeAssetImportData::GetStoredNode(const FString& InNodeUniqueId) const
{
	UInterchangeBaseNodeContainer* NodeContainerResolved = TransientNodeContainer.Get();
	if (NodeContainerResolved)
	{
		return NodeContainerResolved->GetNode(InNodeUniqueId);
	}

	return nullptr;
}

UInterchangeFactoryBaseNode* UInterchangeAssetImportData::GetStoredFactoryNode(const FString& InNodeUniqueId) const
{
	UInterchangeBaseNodeContainer* NodeContainerResolved = TransientNodeContainer.Get();
	if (NodeContainerResolved)
	{
		return NodeContainerResolved->GetFactoryNode(InNodeUniqueId);
	}

	return nullptr;
}


void UInterchangeAssetImportData::ProcessContainerCache() const
{
	if (UInterchangeManager::IsInterchangeImportEnabled())
	{
		//de-serialize
		if (!TransientNodeContainer && CachedNodeContainer.Num() > 0)
		{
			FLargeMemoryReader NodeContainerAr(CachedNodeContainer.GetData(), CachedNodeContainer.Num());
			TransientNodeContainer = NewObject<UInterchangeBaseNodeContainer>();
			TransientNodeContainer->SerializeNodeContainerData(NodeContainerAr);
		}
	}
}

void UInterchangeAssetImportData::ProcessPipelinesCache() const
{
	if (UInterchangeManager::IsInterchangeImportEnabled())
	{
		if ((TransientPipelines.Num() == 0) && (CachedPipelines.Num() > 0))
		{
			TransientPipelines.Reset(CachedPipelines.Num());

			TMap<FString, UClass*> ClassPerName;
			for (FThreadSafeObjectIterator It(UClass::StaticClass()); It; ++It)
			{
				UClass* Class = Cast<UClass>(*It);
				if (Class->IsChildOf(UInterchangePipelineBase::StaticClass()))
				{
					ClassPerName.Add(Class->GetFullName(), Class);
				}
			}

			for (const TPair<FString, FString>& CachedPipeline : CachedPipelines)
			{
				FString ClassFullName = CachedPipeline.Key;
				FCoreRedirectObjectName RedirectedObjectName = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class, FCoreRedirectObjectName(ClassFullName));
				if (RedirectedObjectName.IsValid())
				{
					ClassFullName = RedirectedObjectName.ToString();
				}
				//This cannot fail to make sure we have a healty serialization
				if (!ensure(ClassPerName.Contains(ClassFullName)))
				{
					//We did not successfully serialize the content of the file into the node container
					return;
				}

				UClass* ToCreateClass = ClassPerName.FindChecked(ClassFullName);
				
				UObject* Pipeline = DeSerializePipeline(CachedPipeline.Value, ToCreateClass);

				TransientPipelines.Add(Pipeline);
			}
		}
	}
}