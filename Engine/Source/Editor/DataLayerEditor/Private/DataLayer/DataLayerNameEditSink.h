// Copyright Epic Games, Inc. All Rights Reserved

#pragma once

#include "DataLayer/DataLayerEditorSubsystem.h"
#include "DataLayerTransaction.h"
#include "IObjectNameEditSink.h"

#define LOCTEXT_NAMESPACE "DataLayer"

class FDataLayerNameEditSink : public UE::EditorWidgets::IObjectNameEditSink
{
	virtual UClass* GetSupportedClass() const override
	{
		return UDataLayerInstance::StaticClass();
	}

	virtual FText GetObjectDisplayName(UObject* Object) const override
	{
		UDataLayerInstance* DataLayerInstance = CastChecked<UDataLayerInstance>(Object);
		if (DataLayerInstance->SupportRelabeling())
		{
			return FText::Format(FText::FromString("{0}"), FText::FromString(DataLayerInstance->GetDataLayerShortName()));
		}
		
		return FText::Format(FText::FromString("{0} ({1})"), FText::FromString(DataLayerInstance->GetDataLayerShortName()), FText::FromString(DataLayerInstance->GetDataLayerFullName()));
	}

	virtual bool IsObjectDisplayNameReadOnly(UObject* Object) const override
	{
		UDataLayerInstance* DataLayerInstance = CastChecked<UDataLayerInstance>(Object);
		return !DataLayerInstance->SupportRelabeling() || DataLayerInstance->IsLocked();
	};

	virtual bool SetObjectDisplayName(UObject* Object, FString DisplayName) override
	{
		UDataLayerInstance* DataLayerInstance = CastChecked<UDataLayerInstance>(Object);
		if(DataLayerInstance->SupportRelabeling())
		{
			if (!DisplayName.Equals(DataLayerInstance->GetDataLayerShortName(), ESearchCase::CaseSensitive))
			{
				const FScopedDataLayerTransaction Transaction(LOCTEXT("DataLayerNameEditSinkRenameDataLayerTransaction", "Rename Data Layer"), DataLayerInstance->GetWorld());

				PRAGMA_DISABLE_DEPRECATION_WARNINGS
				return UDataLayerEditorSubsystem::Get()->RenameDataLayer(DataLayerInstance, *DisplayName);
				PRAGMA_ENABLE_DEPRECATION_WARNINGS
			}
		}
		
		return false;
	}

	virtual FText GetObjectNameTooltip(UObject* Object) const override
	{
		return IsObjectDisplayNameReadOnly(Object) ? LOCTEXT("NonEditableDataLayerLabel_TooltipFmt", "Data Layer Name") : FText::Format(LOCTEXT("EditableDataLayerLabel_TooltipFmt", "Rename the selected {0}"), FText::FromString(Object->GetClass()->GetName()));
	}
};

#undef LOCTEXT_NAMESPACE