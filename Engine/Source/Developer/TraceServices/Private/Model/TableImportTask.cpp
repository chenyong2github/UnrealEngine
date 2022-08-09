// Copyright Epic Games, Inc. All Rights Reserved.

#include "TableImportTask.h"

#include "Async/TaskGraphInterfaces.h"
#include "Misc/FileHelper.h"
#include "Tasks/Task.h"

namespace TraceServices
{

FTableImportTask::FTableImportTask(const FString& InFilePath, FName InTableId, FTableImportService::TableImportCallback InCallback)
	: Callback(InCallback)
	, FilePath(InFilePath)
	, TableId(InTableId)
{
}

FTableImportTask::~FTableImportTask()
{
}

void FTableImportTask::operator()()
{
	TArray<FString> Lines;
	FFileHelper::LoadFileToStringArray(Lines, *FilePath);

	if (FilePath.EndsWith(TEXT(".csv")))
	{
		Separator = TEXT(",");
	}
	else if (FilePath.EndsWith(TEXT(".tsv")))
	{
		Separator = TEXT("\t");
	}

	Table = MakeShared<TImportTable<FImportTableRow>>();

	if (!ParseHeader(Lines[0]))
	{
		return;
	}

	if (!CreateLayout(Lines[1]))
	{
		return;
	}

	if (!ParseData(Lines))
	{
		return;
	}

	FFunctionGraphTask::CreateAndDispatchWhenReady(
		[Callback = this->Callback, Table = this->Table, TableId = this->TableId]()
		{
			Callback(TableId, Table);
		},
		TStatId(),
		nullptr,
		ENamedThreads::GameThread);
}

bool FTableImportTask::ParseHeader(const FString& HeaderLine)
{
	HeaderLine.ParseIntoArray(ColumnNames, *Separator);

	return ColumnNames.Num() > 0;
}

bool FTableImportTask::CreateLayout(const FString& Line)
{
	TArray<FString> Values;
	SplitLineIntoValues(Line, Values);

	TTableLayout<FImportTableRow>& Layout = Table->EditLayout();
	for (int32 Index = 0; Index < Values.Num(); ++Index)
	{
		auto ProjectorFunc = [Index](const FImportTableRow& Row)
		{
			return Row.GetValue(Index);
		};

		if (Values[Index].IsNumeric())
		{
			if (Values[Index].Contains(TEXT(".")))
			{
				Layout.AddColumn<double>(*ColumnNames[Index], ProjectorFunc);
			}
			else
			{
				Layout.AddColumn<uint32>(*ColumnNames[Index], ProjectorFunc);
			}
		}
		else
		{
			Layout.AddColumn<const TCHAR*>(*ColumnNames[Index], ProjectorFunc);
		}
	}

	return true;
}

bool FTableImportTask::ParseData(TArray<FString>& Lines)
{
	TTableLayout<FImportTableRow>& Layout = Table->EditLayout();
	bool Restart = false;

	for (int32 LineIndex = 1; LineIndex < Lines.Num(); ++LineIndex)
	{
		TArray<FString> Values;
		SplitLineIntoValues(Lines[LineIndex], Values);

		FImportTableRow& NewRow = Table->AddRow();
		NewRow.SetNumValues(Values.Num());
		for (int32 ValueIndex = 0; ValueIndex < Values.Num(); ++ValueIndex)
		{
			ETableColumnType ColumnType = Layout.GetColumnType(ValueIndex);
			const TCHAR* Value = *Values[ValueIndex];
			if (ColumnType == ETableColumnType::TableColumnType_CString)
			{
				const TCHAR* StoredValue = Table->GetStringStore().Store(Value);
				NewRow.SetValue(ValueIndex, StoredValue);
			}
			else if (ColumnType == ETableColumnType::TableColumnType_Double)
			{
				if (!Values[ValueIndex].IsNumeric())
				{
					Layout.SetColumnType(ValueIndex, ETableColumnType::TableColumnType_CString);
					Restart = true;
					break;
				}
				
				NewRow.SetValue(ValueIndex, FCString::Atod(Value));
			}
			else if (ColumnType == ETableColumnType::TableColumnType_Int)
			{
				if (!Values[ValueIndex].IsNumeric())
				{
					Layout.SetColumnType(ValueIndex, ETableColumnType::TableColumnType_CString);
					Restart = true;
					break;
				}
				else if (Values[ValueIndex].Contains(TEXT(".")))
				{
					Layout.SetColumnType(ValueIndex, ETableColumnType::TableColumnType_Double);
					Restart = true;
					break;
				}

				NewRow.SetValue(ValueIndex, FCString::Atoi(Value));
			}
		}

		if (Restart)
		{
			TSharedPtr<TImportTable<FImportTableRow >> NewTable = MakeShared<TImportTable<FImportTableRow>>();
			NewTable->EditLayout() = Layout;
			Table = NewTable;
			ParseData(Lines);

			break;
		}
	}

	return true;
}

void FTableImportTask::SplitLineIntoValues(const FString& InLine, TArray<FString>& OutValues)
{
	//Parse the line into values. Separators inside quotes are ignored.
	int Start = 0;
	bool IsInQuotes = false;
	int Index;
	for (Index = 0; Index < InLine.Len(); ++Index)
	{
		if (InLine[Index] == TEXT('"') && (Index == 0 || InLine[Index - 1] != TEXT('\\')))
		{
			IsInQuotes = !IsInQuotes;
		}
		else if(!IsInQuotes && InLine[Index] == Separator[0])
		{
			FString Value = InLine.Mid(Start, Index - Start);
			OutValues.Add(Value.TrimQuotes());
			Start = Index + 1;
		}
	}

	FString Value = InLine.Mid(Start, Index - Start);
	OutValues.Add(Value.TrimQuotes());
}

void FTableImportService::ImportTable(const FString& InPath, FName TableId, FTableImportService::TableImportCallback InCallback)
{
	UE::Tasks::Launch(UE_SOURCE_LOCATION, FTableImportTask(InPath, TableId, InCallback));
}

} // namespace TraceServices
