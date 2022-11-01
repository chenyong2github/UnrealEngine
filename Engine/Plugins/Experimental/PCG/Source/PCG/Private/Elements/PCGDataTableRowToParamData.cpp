// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/PCGDataTableRowToParamData.h"
#include "Helpers/PCGAsync.h"
#include "Helpers/PCGSettingsHelpers.h"
#include "Metadata/PCGMetadata.h"
#include "Engine/DataTable.h"

FPCGElementPtr UPCGDataTableRowToParamDataSettings::CreateElement() const
{
	return MakeShared<FPCGDataTableRowToParamData>();
}

TArray<FPCGPinProperties> UPCGDataTableRowToParamDataSettings::OutputPinProperties() const
{
	TArray<FPCGPinProperties> PinProperties;
	PinProperties.Emplace(PCGPinConstants::DefaultOutputLabel, EPCGDataType::Param);

	return PinProperties;
}

FName UPCGDataTableRowToParamDataSettings::AdditionalTaskName() const
{
	return FName(FString::Printf(TEXT("%s[ %s ]"), DataTable ? *DataTable->GetFName().ToString() : TEXT("None"), *RowName.ToString()));
}

bool FPCGDataTableRowToParamData::ExecuteInternal(FPCGContext* Context) const
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPCGDataTableRowToParamData::Execute);

	const UPCGDataTableRowToParamDataSettings* Settings = Context->GetInputSettings<UPCGDataTableRowToParamDataSettings>();
	check(Settings);

	const UDataTable* DataTable = Settings->DataTable.Get();
	if (!DataTable)
	{
		PCGE_LOG(Error, TEXT("FPCGDataTableRowToParamData: Data table is invalid"));
		return true;
	}

	const uint8* const* RowDataItr = DataTable->GetRowMap().Find(Settings->RowName);
	const uint8* RowDataPtr = RowDataItr ? *RowDataItr : nullptr;
	if (!RowDataPtr)
	{
		PCGE_LOG(Error, TEXT("FPCGDataTableRowToParamData: Data table '%s' has no row named '%s'"), *DataTable->GetPathName(), *Settings->RowName.ToString());
		return true;
	}

	// From there, we should be able to create the data.
	UPCGParamData* ParamData = NewObject<UPCGParamData>();
	UPCGMetadata* Metadata = ParamData->MutableMetadata();
	check(Metadata);
	PCGMetadataEntryKey EntryKey = Metadata->AddEntry();

	for (TFieldIterator<const FProperty> FieldIt(DataTable->GetRowStruct(), EFieldIterationFlags::IncludeSuper); FieldIt; ++FieldIt)
	{
		const FString FieldName = DataTable->GetRowStruct()->GetAuthoredNameForField(*FieldIt);
		const FName AttributeName(FieldName);

		if (!Metadata->SetAttributeFromDataProperty(AttributeName, EntryKey, RowDataPtr, *FieldIt, /*bCreate=*/ true))
		{
			PCGE_LOG(Warning, "Error while creating a attribute '%s'. Either the property type is not supported by PCG or attribute creation failed.", *AttributeName.ToString());
		}
	}
	
	TArray<FPCGTaggedData>& Outputs = Context->OutputData.TaggedData;
	FPCGTaggedData& Output = Outputs.Emplace_GetRef();
	Output.Data = ParamData;

	return true;
}
