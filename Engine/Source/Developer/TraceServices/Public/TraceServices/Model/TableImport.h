// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "AllocationsProvider.h"
#include "CoreMinimal.h"
#include "TraceServices/Containers/Tables.h"

namespace TraceServices
{
class FImportTableRow;

class ITableImportData
{
	TSharedPtr<ITable<FImportTableRow>> GetTable();
};

class FTableImportService
{
public:
	typedef TFunction<void(FName TableId, TSharedPtr<ITable<FImportTableRow>> Data)> TableImportCallback;

	TRACESERVICES_API static void ImportTable(const FString& InPath, FName TableId, TableImportCallback InCallback);
};

} // namespace TraceServices
