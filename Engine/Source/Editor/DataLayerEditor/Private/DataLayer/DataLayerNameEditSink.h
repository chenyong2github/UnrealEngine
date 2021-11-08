// Copyright Epic Games, Inc. All Rights Reserved

#pragma once

#include "DataLayer/DataLayerEditorSubsystem.h"
#include "WorldPartition/DataLayer/DataLayer.h"
#include "DataLayerTransaction.h"
#include "IObjectNameEditSink.h"

#define LOCTEXT_NAMESPACE "DataLayer"

class FDataLayerNameEditSink : public UE::EditorWidgets::IObjectNameEditSink
{
	virtual UClass* GetSupportedClass() const override
	{
		return UDataLayer::StaticClass();
	}

	virtual FText GetObjectDisplayName(UObject* Object) const override
	{
		return FText::FromName(CastChecked<UDataLayer>(Object)->GetDataLayerLabel());
	}

	virtual bool IsObjectDisplayNameReadOnly(UObject* Object) const override
	{
		UDataLayer* DataLayer = CastChecked<UDataLayer>(Object);
		return DataLayer->IsLocked();
	};

	virtual bool SetObjectDisplayName(UObject* Object, FString DisplayName) override
	{
		UDataLayer* DataLayer = CastChecked<UDataLayer>(Object);

		if (DataLayer->GetDataLayerLabel().ToString() == DisplayName)
		{
			return false;
		}

		const FScopedDataLayerTransaction Transaction(LOCTEXT("DataLayerNameEditSinkRenameDataLayerTransaction", "Rename Data Layer"), DataLayer->GetWorld());
		UDataLayerEditorSubsystem::Get()->RenameDataLayer(DataLayer, *DisplayName);

		return true;
	}

	virtual FText GetObjectNameTooltip(UObject* Object) const override
	{
		return FText::Format(LOCTEXT("EditableDataLayerLabel_TooltipFmt", "Rename the selected {0}"), FText::FromString(Object->GetClass()->GetName()));
	}
};

#undef LOCTEXT_NAMESPACE