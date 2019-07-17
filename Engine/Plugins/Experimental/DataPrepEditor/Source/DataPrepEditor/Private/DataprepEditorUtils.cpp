// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepEditorUtils.h"

#include "DataPrepAsset.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"

void FDataprepEditorUtils::NotifySystemOfChangeInPipeline(UObject* SourceObject)
{
	UBlueprint* Blueprint = nullptr;
	UDataprepAsset* DataprepAsset = nullptr;
	UObject* Object = SourceObject;
	while ( Object )
	{
		UClass* Class = Object->GetClass();
		if ( Class->IsChildOf<UBlueprint>() )
		{
			Blueprint = static_cast<UBlueprint*>( Object );
		}
		else if ( Class == UDataprepAsset::StaticClass() )
		{
			DataprepAsset = static_cast<UDataprepAsset*>( Object );
			break;
		}
		Object = Object->GetOuter();
	}

	if ( DataprepAsset )
	{
		UDataprepAsset::FDataprepPipelineChangeNotifier::NotifyDataprepOfPipelineChange( *DataprepAsset, SourceObject );
	}
	else if ( Blueprint )
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified( Blueprint );
	}
}

