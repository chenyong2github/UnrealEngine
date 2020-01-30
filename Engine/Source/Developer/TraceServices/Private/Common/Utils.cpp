// Copyright Epic Games, Inc. All Rights Reserved.

#include "Utils.h"
#include "TraceServices/Containers/Tables.h"
#include "Templates/SharedPointer.h"
#include "HAL/FileManager.h"

namespace Trace
{

void Table2Csv(const Trace::IUntypedTable& Table, const TCHAR* Filename)
{
	TSharedPtr<FArchive> OutputFile = MakeShareable(IFileManager::Get().CreateFileWriter(Filename));
	check(OutputFile);
	FString Header;
	const Trace::ITableLayout& Layout = Table.GetLayout();
	int32 ColumnCount = Layout.GetColumnCount();
	for (int32 ColumnIndex = 0; ColumnIndex < ColumnCount; ++ColumnIndex)
	{
		Header += Layout.GetColumnName(ColumnIndex);
		if (ColumnIndex < ColumnCount - 1)
		{
			Header += TEXT(",");
		}
		else
		{
			Header += TEXT("\n");
		}
	}
	auto AnsiHeader = StringCast<ANSICHAR>(*Header);
	OutputFile->Serialize((void*)AnsiHeader.Get(), AnsiHeader.Length());
	TUniquePtr<Trace::IUntypedTableReader> TableReader(Table.CreateReader());
	for (; TableReader->IsValid(); TableReader->NextRow())
	{
		FString Line;
		for (int32 ColumnIndex = 0; ColumnIndex < ColumnCount; ++ColumnIndex)
		{
			switch (Layout.GetColumnType(ColumnIndex))
			{
			case Trace::TableColumnType_Bool:
				Line += TableReader->GetValueBool(ColumnIndex) ? "true" : "false";
				break;
			case Trace::TableColumnType_Int:
				Line += FString::Printf(TEXT("%lld"), TableReader->GetValueInt(ColumnIndex));
				break;
			case Trace::TableColumnType_Float:
				Line += FString::Printf(TEXT("%f"), TableReader->GetValueFloat(ColumnIndex));
				break;
			case Trace::TableColumnType_Double:
				Line += FString::Printf(TEXT("%f"), TableReader->GetValueDouble(ColumnIndex));
				break;
			case Trace::TableColumnType_CString:
				FString ValueString = TableReader->GetValueCString(ColumnIndex);
				ValueString.ReplaceInline(TEXT(","), TEXT(" "));
				Line += ValueString;
				break;
			}
			if (ColumnIndex < ColumnCount - 1)
			{
				Line += TEXT(",");
			}
			else
			{
				Line += TEXT("\n");
			}
		}
		auto AnsiLine = StringCast<ANSICHAR>(*Line);
		OutputFile->Serialize((void*)AnsiLine.Get(), AnsiLine.Length());
	}
	OutputFile->Close();
}

}
