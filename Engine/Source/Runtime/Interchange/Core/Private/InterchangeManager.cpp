// Copyright Epic Games, Inc. All Rights Reserved.

#include "InterchangeManager.h"

#include "CoreMinimal.h"
#include "InterchangeFactoryBase.h"
#include "InterchangeLogPrivate.h"
#include "InterchangeSourceData.h"
#include "InterchangeTranslatorBase.h"
#include "InterchangeWriterBase.h"
#include "Internationalization/Internationalization.h"
#include "Misc/App.h"
#include "Misc/AsyncTaskNotification.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "PackageUtils/PackageUtils.h"
#include "Tasks/InterchangeTaskParsing.h"
#include "Tasks/InterchangeTaskPipeline.h"
#include "Tasks/InterchangeTaskTranslator.h"
#include "UObject/Class.h"
#include "UObject/NameTypes.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectIterator.h"
#include "UObject/WeakObjectPtrTemplates.h"

#if WITH_ENGINE
#include "AssetDataTagMap.h"
#include "AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#endif //WITH_ENGINE

namespace InternalInterchangePrivate
{
	const FLogCategoryBase* GetLogInterchangePtr()
	{
#if NO_LOGGING
		return nullptr;
#else
		return &LogInterchangeCore;
#endif
	}
}

Interchange::FScopedSourceData::FScopedSourceData(const FString& Filename)
{
	//Found the translator
	SourceDataPtr = TStrongObjectPtr<UInterchangeSourceData>(UInterchangeManager::GetInterchangeManager().CreateSourceData(Filename));
	check(SourceDataPtr.IsValid());
}

UInterchangeSourceData* Interchange::FScopedSourceData::GetSourceData() const
{
	return SourceDataPtr.Get();
}

Interchange::FScopedTranslator::FScopedTranslator(const UInterchangeSourceData* SourceData)
{
	//Found the translator
	ScopedTranslatorPtr = TStrongObjectPtr<UInterchangeTranslatorBase>(UInterchangeManager::GetInterchangeManager().GetTranslatorForSourceData(SourceData));
}

UInterchangeTranslatorBase* Interchange::FScopedTranslator::GetTranslator()
{
	return ScopedTranslatorPtr.Get();
}

Interchange::FImportAsyncHelper::FImportAsyncHelper()
{
	RootObjectCompletionEvent = FGraphEvent::CreateGraphEvent();
}

void Interchange::FImportAsyncHelper::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (UInterchangeSourceData* SourceData : SourceDatas)
	{
		Collector.AddReferencedObject(SourceData);
	}
	for (UInterchangeTranslatorBase* Translator : Translators)
	{
		Collector.AddReferencedObject(Translator);
	}
	for (UInterchangePipelineBase* Pipeline : Pipelines)
	{
		Collector.AddReferencedObject(Pipeline);
	}
	for (UInterchangeFactoryBase* Factory : Factories)
	{
		Collector.AddReferencedObject(Factory);
	}
}

void Interchange::FImportAsyncHelper::CleanUp()
{
	//Release the graph
	BaseNodeContainers.Empty();

	for (UInterchangeSourceData* SourceData : SourceDatas)
	{
		if (SourceData)
		{
			SourceData->RemoveFromRoot();
			SourceData->MarkPendingKill();
			SourceData = nullptr;
		}
	}
	SourceDatas.Empty();

	for (UInterchangeTranslatorBase* Translator : Translators)
	{
		if(Translator)
		{
			Translator->ImportFinish();
			Translator->RemoveFromRoot();
			Translator->MarkPendingKill();
			Translator = nullptr;
		}
	}
	Translators.Empty();

	for (UInterchangePipelineBase* Pipeline : Pipelines)
	{
		if(Pipeline)
		{
			Pipeline->RemoveFromRoot();
			Pipeline->MarkPendingKill();
			Pipeline = nullptr;
		}
	}
	Pipelines.Empty();

	//Factories are not instantiate, we use the registered one directly
	Factories.Empty();
}

Interchange::FAsyncImportResult::FAsyncImportResult( TFuture< UObject* >&& InFutureObject, const FGraphEventRef& InGraphEvent )
	: FutureObject( MoveTemp( InFutureObject ) )
	, GraphEvent( InGraphEvent )
{
}

bool Interchange::FAsyncImportResult::IsValid() const
{
	return FutureObject.IsValid();
}

UObject* Interchange::FAsyncImportResult::Get() const
{
	if ( !FutureObject.IsReady() )
	{
		// Tick the task graph until our FutureObject is ready
		FTaskGraphInterface::Get().WaitUntilTaskCompletes( GraphEvent );
	}

	return FutureObject.Get();
}

Interchange::FAsyncImportResult Interchange::FAsyncImportResult::Next( TFunction< UObject*( UObject* ) > Continuation )
{
	return Interchange::FAsyncImportResult{ FutureObject.Next( Continuation ), GraphEvent };
}

void Interchange::SanitizeInvalidChar(FString& String)
{
	const TCHAR* InvalidChar = INVALID_OBJECTPATH_CHARACTERS;
	while (*InvalidChar)
	{
		String.ReplaceCharInline(*InvalidChar, TCHAR('_'), ESearchCase::CaseSensitive);
		++InvalidChar;
	}
}

bool UInterchangeManager::RegisterTranslator(const UClass* TranslatorClass)
{
	if(!TranslatorClass)
	{
		return false;
	}

	if(RegisteredTranslators.Contains(TranslatorClass))
	{
		return true;
	}
	UInterchangeTranslatorBase* TranslatorToRegister = NewObject<UInterchangeTranslatorBase>(GetTransientPackage(), TranslatorClass, NAME_None);
	if(!TranslatorToRegister)
	{
		return false;
	}
	RegisteredTranslators.Add(TranslatorClass, TranslatorToRegister);
	return true;
}

bool UInterchangeManager::RegisterFactory(const UClass* FactoryClass)
{
	if (!FactoryClass)
	{
		return false;
	}

	UInterchangeFactoryBase* FactoryToRegister = NewObject<UInterchangeFactoryBase>(GetTransientPackage(), FactoryClass, NAME_None);
	if (!FactoryToRegister)
	{
		return false;
	}
	if (FactoryToRegister->GetFactoryClass() == nullptr || RegisteredFactories.Contains(FactoryToRegister->GetFactoryClass()))
	{
		FactoryToRegister->MarkPendingKill();
		return FactoryToRegister->GetFactoryClass() == nullptr ? false : true;
	}
	RegisteredFactories.Add(FactoryToRegister->GetFactoryClass(), FactoryToRegister);
	return true;
}

bool UInterchangeManager::RegisterWriter(const UClass* WriterClass)
{
	if (!WriterClass)
	{
		return false;
	}

	if (RegisteredFactories.Contains(WriterClass))
	{
		return true;
	}
	UInterchangeWriterBase* WriterToRegister = NewObject<UInterchangeWriterBase>(GetTransientPackage(), WriterClass, NAME_None);
	if (!WriterToRegister)
	{
		return false;
	}
	RegisteredWriters.Add(WriterClass, WriterToRegister);
	return true;
}

bool UInterchangeManager::CanTranslateSourceData(const UInterchangeSourceData* SourceData) const
{
	//Found the translator
	Interchange::FScopedTranslator ScopeDataTranslator(SourceData);
	const UInterchangeTranslatorBase* SourceDataTranslator = ScopeDataTranslator.GetTranslator();
	if (SourceDataTranslator)
	{
		return true;
	}
	return false;
}

bool UInterchangeManager::ImportAsset(const FString& ContentPath, const UInterchangeSourceData* SourceData, const FImportAssetParameters& ImportAssetParameters)
{
	return ImportAssetAsync( ContentPath, SourceData, ImportAssetParameters ).IsValid();
}

Interchange::FAsyncImportResult UInterchangeManager::ImportAssetAsync(const FString& ContentPath, const UInterchangeSourceData* SourceData, const FImportAssetParameters& ImportAssetParameters)
{
	FString PackageBasePath = ContentPath;
	if(!ImportAssetParameters.ReimportAsset)
	{
		Interchange::SanitizeInvalidChar(PackageBasePath);
	}

	bool bCanShowDialog = !ImportAssetParameters.bIsAutomated && !IsAttended();

	//Create a task for every source data
	Interchange::FImportAsyncHelperData TaskData;
	TaskData.bIsAutomated = ImportAssetParameters.bIsAutomated;
	TaskData.ImportType = Interchange::EImportType::ImportType_Asset;
	TaskData.ReimportObject = ImportAssetParameters.ReimportAsset;
	TWeakPtr<Interchange::FImportAsyncHelper, ESPMode::ThreadSafe> WeakAsyncHelper = CreateAsyncHelper(TaskData);
	TSharedPtr<Interchange::FImportAsyncHelper, ESPMode::ThreadSafe> AsyncHelper = WeakAsyncHelper.Pin();
	check(AsyncHelper.IsValid());

	FText TitleText = NSLOCTEXT("Interchange", "Asynchronous_import_start", "Importing");
	if(!Notification.IsValid())
	{
		FAsyncTaskNotificationConfig NotificationConfig;
		NotificationConfig.bIsHeadless = false;
		NotificationConfig.bKeepOpenOnFailure = true;
		NotificationConfig.TitleText = TitleText;
		NotificationConfig.LogCategory = InternalInterchangePrivate::GetLogInterchangePtr();
		NotificationConfig.bCanCancel.Set(false);
		NotificationConfig.bKeepOpenOnFailure.Set(true);

		Notification = MakeShared<FAsyncTaskNotification>(NotificationConfig);
		Notification->SetNotificationState(FAsyncNotificationStateData(TitleText, FText::GetEmpty(), EAsyncTaskNotificationState::Pending));
	}
	
	//Create a duplicate of the source data, we need to be multithread safe so we copy it to control the life cycle. The async helper will hold it and delete it when the import task will be completed.
	UInterchangeSourceData* DuplicateSourceData = Cast<UInterchangeSourceData>(StaticDuplicateObject(SourceData, GetTransientPackage()));
	//Array of source data to build one graph per source
	AsyncHelper->SourceDatas.Add(DuplicateSourceData);
		

	//Get all the translators for the source datas
	for (int32 SourceDataIndex = 0; SourceDataIndex < AsyncHelper->SourceDatas.Num(); ++SourceDataIndex)
	{
		ensure(AsyncHelper->Translators.Add(GetTranslatorForSourceData(AsyncHelper->SourceDatas[SourceDataIndex])) == SourceDataIndex);
	}

	//Create the node graphs for each source data (StrongObjectPtr has to be created on the main thread)
	for (int32 SourceDataIndex = 0; SourceDataIndex < AsyncHelper->SourceDatas.Num(); ++SourceDataIndex)
	{
		AsyncHelper->BaseNodeContainers.Add(TStrongObjectPtr<UInterchangeBaseNodeContainer>(NewObject<UInterchangeBaseNodeContainer>(GetTransientPackage(), NAME_None)));
		check(AsyncHelper->BaseNodeContainers[SourceDataIndex].IsValid());
	}

	if ( ImportAssetParameters.OverridePipeline == nullptr )
	{
		//Get all pipeline candidate we want for this import
		/*TArray<UClass*> PipelineCandidates;
		FindPipelineCandidate(PipelineCandidates);

		// Stack all pipelines, for this import proto. TODO: We need to be able to control which pipeline we use for the import
		// It can be set in the project settings and can also be set by a UI where the user create the pipeline stack he want.
		// This should be a list of available pipelines that can be drop into a stack where you can control the order.
		for (int32 GraphPipelineNumber = 0; GraphPipelineNumber < PipelineCandidates.Num(); ++GraphPipelineNumber)
		{
			UInterchangePipelineBase* GeneratedPipeline = NewObject<UInterchangePipelineBase>(GetTransientPackage(), PipelineCandidates[GraphPipelineNumber], NAME_None, RF_NoFlags);
			AsyncHelper->Pipelines.Add(GeneratedPipeline);
		}*/
	}
	else
	{
		AsyncHelper->Pipelines.Add(ImportAssetParameters.OverridePipeline);
	}

	//Create/Start import tasks
	FGraphEventArray PipelinePrerequistes;
	check(AsyncHelper->Translators.Num() == AsyncHelper->SourceDatas.Num());
	for (int32 SourceDataIndex = 0; SourceDataIndex < AsyncHelper->SourceDatas.Num(); ++SourceDataIndex)
	{
		int32 TranslatorTaskIndex = AsyncHelper->TranslatorTasks.Add(TGraphTask<Interchange::FTaskTranslator>::CreateTask().ConstructAndDispatchWhenReady(SourceDataIndex, WeakAsyncHelper));
		PipelinePrerequistes.Add(AsyncHelper->TranslatorTasks[TranslatorTaskIndex]);
	}
		
	FGraphEventArray GraphParsingPrerequistes;
	for(int32 GraphPipelineIndex = 0; GraphPipelineIndex < AsyncHelper->Pipelines.Num(); ++GraphPipelineIndex)
	{
		UInterchangePipelineBase* GraphPipeline = AsyncHelper->Pipelines[GraphPipelineIndex];
		TWeakObjectPtr<UInterchangePipelineBase> WeakPipelinePtr = GraphPipeline;
		int32 GraphPipelineTaskIndex = INDEX_NONE;
		GraphPipelineTaskIndex = AsyncHelper->PipelineTasks.Add(TGraphTask<Interchange::FTaskPipeline>::CreateTask(&PipelinePrerequistes).ConstructAndDispatchWhenReady(WeakPipelinePtr, WeakAsyncHelper));
		//Ensure we run the pipeline in the same order we create the task, since pipeline modify the node container, its important that its not process in parallel, Adding the one we start to the prerequisites
		//is the way to go here
		PipelinePrerequistes.Add(AsyncHelper->PipelineTasks[GraphPipelineTaskIndex]);

		//Add pipeline to the graph parsing prerequisites
		GraphParsingPrerequistes.Add(AsyncHelper->PipelineTasks[GraphPipelineTaskIndex]);
	}

	if (GraphParsingPrerequistes.Num() > 0)
	{
		AsyncHelper->ParsingTask = TGraphTask<Interchange::FTaskParsing>::CreateTask(&GraphParsingPrerequistes).ConstructAndDispatchWhenReady(this, PackageBasePath, WeakAsyncHelper);
	}
	else
	{
		//Fallback on the translator pipeline prerequisites (translator must be done if there is no pipeline)
		AsyncHelper->ParsingTask = TGraphTask<Interchange::FTaskParsing>::CreateTask(&PipelinePrerequistes).ConstructAndDispatchWhenReady(this, PackageBasePath, WeakAsyncHelper);
	}

	//The graph parsing task will create the FCreateAssetTask that will run after them, the FAssetImportTask will call the appropriate Post asset import pipeline when the asset is completed

	return Interchange::FAsyncImportResult{ AsyncHelper->RootObject.GetFuture(), AsyncHelper->RootObjectCompletionEvent };
}

bool UInterchangeManager::ImportScene(const FString& ImportContext, const UInterchangeSourceData* SourceData, bool bIsReimport, bool bIsAutomated)
{
	return false;
}

bool UInterchangeManager::ExportAsset(const UObject* Asset, bool bIsAutomated)
{
	return false;
}

bool UInterchangeManager::ExportScene(const UObject* World, bool bIsAutomated)
{
	return false;
}

UInterchangeSourceData* UInterchangeManager::CreateSourceData(const FString& InFileName)
{
	UInterchangeSourceData* SourceDataAsset = NewObject<UInterchangeSourceData>(GetTransientPackage(), NAME_None);
	if(!InFileName.IsEmpty())
	{
		SourceDataAsset->SetFilename(InFileName);
	}
	return SourceDataAsset;
}

TWeakPtr<Interchange::FImportAsyncHelper, ESPMode::ThreadSafe> UInterchangeManager::CreateAsyncHelper(const Interchange::FImportAsyncHelperData& Data)
{
	TSharedPtr<Interchange::FImportAsyncHelper, ESPMode::ThreadSafe> AsyncHelper = MakeShared<Interchange::FImportAsyncHelper, ESPMode::ThreadSafe>();
	//Copy the task data
	AsyncHelper->TaskData = Data;
	int32 AsyncHelperIndex = ImportTasks.Add(AsyncHelper);
	//Update the asynchronous notification
	if (Notification.IsValid())
	{
		int32 ImportTaskNumber = ImportTasks.Num();
		FString ImportTaskNumberStr = TEXT(" (") + FString::FromInt(ImportTaskNumber) + TEXT(")");
		Notification->SetProgressText(FText::FromString(ImportTaskNumberStr));
	}
	
	return ImportTasks[AsyncHelperIndex];
}

void UInterchangeManager::ReleaseAsyncHelper(TWeakPtr<Interchange::FImportAsyncHelper, ESPMode::ThreadSafe> AsyncHelper)
{
	check(AsyncHelper.IsValid());
	ImportTasks.RemoveSingle(AsyncHelper.Pin());
	check(!AsyncHelper.IsValid());

	int32 ImportTaskNumber = ImportTasks.Num();
	FString ImportTaskNumberStr = TEXT(" (") + FString::FromInt(ImportTaskNumber) + TEXT(")");
	if (ImportTasks.Num() == 0 && Notification.IsValid())
	{
		FText TitleText = NSLOCTEXT("Interchange", "Asynchronous_import_end", "Import Done");
		//TODO make sure any error are reported so we can control success or not
		const bool bSuccess = true;
		Notification->SetComplete(TitleText, FText::GetEmpty(), bSuccess);
		Notification = nullptr; //This should delete the notification
	}
	else if(Notification.IsValid())
	{
		Notification->SetProgressText(FText::FromString(ImportTaskNumberStr));
	}
}

UInterchangeTranslatorBase* UInterchangeManager::GetTranslatorForSourceData(const UInterchangeSourceData* SourceData) const
{
	if (RegisteredTranslators.Num() == 0)
	{
		return nullptr;
	}
	//Found the translator
	for (const auto Kvp : RegisteredTranslators)
	{
		if (Kvp.Value->CanImportSourceData(SourceData))
		{
			UInterchangeTranslatorBase* SourceDataTranslator = NewObject<UInterchangeTranslatorBase>(GetTransientPackage(), Kvp.Key, NAME_None);
			return SourceDataTranslator;
		}
	}
	return nullptr;
}

bool UInterchangeManager::IsAttended()
{
	if (FApp::IsGame())
	{
		return false;
	}
	if (FApp::IsUnattended())
	{
		return false;
	}
	return true;
}

void UInterchangeManager::FindPipelineCandidate(TArray<UClass*>& PipelineCandidates)
{
	//Find in memory pipeline class
	for (TObjectIterator< UClass > ClassIt; ClassIt; ++ClassIt)
	{
		UClass* Class = *ClassIt;
		// Only interested in native C++ classes
		if (!Class->IsNative())
		{
			continue;
		}
		// Ignore deprecated
		if (Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			continue;
		}

		// Check this class is a subclass of Base and not the base itself
		if (Class == UInterchangePipelineBase::StaticClass() || !Class->IsChildOf(UInterchangePipelineBase::StaticClass()))
		{
			continue;
		}

		//We found a candidate
		PipelineCandidates.AddUnique(Class);
	}

//Blueprint and python script discoverability is available only if we compile with the engine
#if WITH_ENGINE
	// Load the asset registry module
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked< FAssetRegistryModule >(FName("AssetRegistry"));
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray< FString > ContentPaths;
	ContentPaths.Add(TEXT("/Game"));
	//TODO do we have an other alternative, this call is synchronous and will wait unitl the registry database have finish the initial scan. If there is a lot of asset it can take multiple second the first time we call it.
	AssetRegistry.ScanPathsSynchronous(ContentPaths);

	FName BaseClassName = UInterchangePipelineBase::StaticClass()->GetFName();

	// Use the asset registry to get the set of all class names deriving from Base
	TSet< FName > DerivedNames;
	{
		TArray< FName > BaseNames;
		BaseNames.Add(BaseClassName);

		TSet< FName > Excluded;
		AssetRegistry.GetDerivedClassNames(BaseNames, Excluded, DerivedNames);
	}

	FARFilter Filter;
	Filter.ClassNames.Add(UBlueprint::StaticClass()->GetFName());
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;

	TArray< FAssetData > AssetList;
	AssetRegistry.GetAssets(Filter, AssetList);

	// Iterate over retrieved blueprint assets
	for (const FAssetData& Asset : AssetList)
	{
		//Only get the asset with the native parent class using UInterchangePipelineBase
		FAssetDataTagMapSharedView::FFindTagResult GeneratedClassPath = Asset.TagsAndValues.FindTag(TEXT("GeneratedClass"));
		if (GeneratedClassPath.IsSet())
		{
			// Convert path to just the name part
			const FString ClassObjectPath = FPackageName::ExportTextPathToObjectPath(*GeneratedClassPath.GetValue());
			const FString ClassName = FPackageName::ObjectPathToObjectName(ClassObjectPath);

			// Check if this class is in the derived set
			if (!DerivedNames.Contains(*ClassName))
			{
				continue;
			}

			UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
			check(Blueprint);
			check(Blueprint->ParentClass == UInterchangePipelineBase::StaticClass());
			PipelineCandidates.AddUnique(Blueprint->GeneratedClass);
		}
	}
#endif //WITH_ENGINE

}
