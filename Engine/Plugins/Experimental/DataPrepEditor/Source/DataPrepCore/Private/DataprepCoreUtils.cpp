// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepCoreUtils.h"

#include "IDataprepProgressReporter.h"

#include "UObject/UObjectHash.h"

#if WITH_EDITOR
#include "ObjectTools.h"
#include "Toolkits/AssetEditorManager.h"
#endif

void FDataprepCoreUtils::PurgeObjects(TArray<UObject*> Objects)
{
	TArray<UObject*> ObjectsToPurge;
	ObjectsToPurge.Reserve( Objects.Num() );
	TArray<UObject*> PublicObjectsToPurge;
	PublicObjectsToPurge.Reserve( Objects.Num() );

	auto MakeObjectPurgeable = [&ObjectsToPurge, &PublicObjectsToPurge](UObject* InObject)
	{
#if WITH_EDITOR
		if ( InObject->IsAsset() )
		{
			FAssetEditorManager::Get().CloseAllEditorsForAsset( InObject );
		}
#endif
		if ( InObject->IsRooted() )
		{
			InObject->RemoveFromRoot();
		}

		if ( InObject->HasAnyFlags( RF_Public ) )
		{
			PublicObjectsToPurge.Add(InObject);
		}

		InObject->ClearFlags( RF_Public | RF_Standalone );
		InObject->MarkPendingKill();
		ObjectsToPurge.Add( InObject );
	};

	auto MakeSourceObjectPurgeable = [MakeObjectPurgeable](UObject* InSourceObject)
	{
		MakeObjectPurgeable( InSourceObject );
		ForEachObjectWithOuter( InSourceObject, [MakeObjectPurgeable](UObject* InObject)
		{
			MakeObjectPurgeable( InObject );
		});
	};

	// Clean-up any in-memory packages that should be purged and check if we are purging the current map
	for ( UObject* Object : Objects )
	{
		if (Object)
		{
			MakeSourceObjectPurgeable(Object);
		}
	}

	// If we have any object that were made purgeable, null out their references so we can garbage collect
	if ( PublicObjectsToPurge.Num() > 0 )
	{
#if WITH_EDITOR
		ObjectTools::ForceReplaceReferences(nullptr, PublicObjectsToPurge);
#endif // WITH_EDITOR
	}

	// if we have object to purge but the map isn't one of them collect garbage (if we purged the map it has already been done)
	if ( ObjectsToPurge.Num() > 0 )
	{
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	}
}

FDataprepProgressTask::FDataprepProgressTask(IDataprepProgressReporter& InReporter, const FText& InDescription, float InAmountOfWork, float InIncrementOfWork)
	: Reporter( InReporter )
	, DefaultIncrementOfWork( InIncrementOfWork )
{
	Reporter.PushTask( InDescription, InAmountOfWork );
}

FDataprepProgressTask::~FDataprepProgressTask()
{
	Reporter.PopTask();
}

void FDataprepProgressTask::ReportNextStep(const FText & InMessage, float InIncrementOfWork )
{
	Reporter.ReportProgress( InIncrementOfWork, InMessage );
}