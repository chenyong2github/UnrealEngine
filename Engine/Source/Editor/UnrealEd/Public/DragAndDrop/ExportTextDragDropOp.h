// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameFramework/Actor.h"
#include "Input/DragAndDrop.h"
#include "Editor.h"
#include "UObject/UObjectIterator.h"
#include "UObject/PropertyPortFlags.h"
#include "Exporters/Exporter.h"
#include "UnrealExporter.h"

class FExportTextDragDropOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FExportTextDragDropOp, FDragDropOperation)

	FString ActorExportText;
	int32 NumActors;

	/** The widget decorator to use */
	//virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	//{
		
	//}

	static TSharedRef<FExportTextDragDropOp> New(const TArray<AActor*>& InActors)
	{
		TSharedRef<FExportTextDragDropOp> Operation = MakeShareable(new FExportTextDragDropOp);

		for(int32 i=0; i<InActors.Num(); i++)
		{
			AActor* Actor = InActors[i];

			GEditor->SelectActor(Actor, true, true);
		}

		FStringOutputDevice Ar;
		const FSelectedActorExportObjectInnerContext Context;
		UExporter::ExportToOutputDevice( &Context, GWorld, NULL, Ar, TEXT("copy"), 0, PPF_DeepCompareInstances | PPF_ExportsNotFullyQualified);
		Operation->ActorExportText = Ar;
		Operation->NumActors = InActors.Num();
		
		Operation->Construct();

		return Operation;
	}
};

