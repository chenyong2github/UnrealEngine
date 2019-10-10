// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TableCellValueFormatter.h"

#include "Insights/Common/TimeUtils.h"
//#include "Insights/Table/ViewModels/BaseTreeNode.h"
#include "Insights/Table/ViewModels/TableColumn.h"

#define LOCTEXT_NAMESPACE "TableCellValueFormatter"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FTableCellValueFormatter::FormatValue(const FTableColumn& Column, const FBaseTreeNode& Node) const
{
	return FormatValue(Column.GetValue(Node));
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FBoolValueFormatterAsTrueFalse::FormatValue(const TOptional<FTableCellValue>& InValue) const
{
	if (InValue.IsSet())
	{
		return FText::FromString(InValue.GetValue().Bool ? TEXT("True") : TEXT("False"));
	}
	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FBoolValueFormatterAsOnOff::FormatValue(const TOptional<FTableCellValue>& InValue) const
{
	if (InValue.IsSet())
	{
		return FText::FromString(InValue.GetValue().Bool ? TEXT("On") : TEXT("Off"));
	}
	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FInt64ValueFormatterAsMemory::FormatValue(const TOptional<FTableCellValue>& InValue) const
{
	if (InValue.IsSet())
	{
		const int64 Value = InValue.GetValue().Int64;
		return FText::AsMemory(Value);
	}
	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FInt64ValueFormatterAsMemory::FormatValueForTooltip(const TOptional<FTableCellValue>& InValue) const
{
	if (InValue.IsSet())
	{
		const int64 Value = InValue.GetValue().Int64;
		if (Value == 0)
		{
			return FText::FromString(TEXT("0"));
		}
		else
		{
			return FText::FromString(FString::Printf(TEXT("%d bytes (%s)"), Value, *FText::AsMemory(Value).ToString()));
		}
	}
	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FFloatValueFormatterAsNumber::FormatValue(const TOptional<FTableCellValue>& InValue) const
{
	if (InValue.IsSet())
	{
		const float Value = InValue.GetValue().Float;
		if (Value == 0.0f)
		{
			return FText::FromString(TEXT("0"));
		}
		else
		{
			return FText::FromString(FString::Printf(TEXT("%f"), Value));
		}
	}
	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FFloatValueFormatterAsTimeAuto::FormatValue(const TOptional<FTableCellValue>& InValue) const
{
	if (InValue.IsSet())
	{
		const float Value = InValue.GetValue().Float;
		return FText::FromString(TimeUtils::FormatTimeAuto(static_cast<double>(Value)));
	}
	return FText::GetEmpty();
}

FText FFloatValueFormatterAsTimeAuto::FormatValueForTooltip(const TOptional<FTableCellValue>& InValue) const
{
	if (InValue.IsSet())
	{
		const float Value = InValue.GetValue().Float;
		if (Value == 0.0f)
		{
			return FText::FromString(TEXT("0"));
		}
		else
		{
			return FText::FromString(FString::Printf(TEXT("%f (%s)"), Value, *TimeUtils::FormatTimeAuto(static_cast<double>(Value))));
		}
	}
	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FDoubleValueFormatterAsNumber::FormatValue(const TOptional<FTableCellValue>& InValue) const
{
	if (InValue.IsSet())
	{
		const double Value = InValue.GetValue().Double;
		if (Value == 0.0)
		{
			return FText::FromString(TEXT("0"));
		}
		else
		{
			return FText::FromString(FString::Printf(TEXT("%f"), Value));
		}
	}
	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FDoubleValueFormatterAsTimeAuto::FormatValue(const TOptional<FTableCellValue>& InValue) const
{
	if (InValue.IsSet())
	{
		const double Value = InValue.GetValue().Double;
		return FText::FromString(TimeUtils::FormatTimeAuto(Value));
	}
	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FDoubleValueFormatterAsTimeAuto::FormatValueForTooltip(const TOptional<FTableCellValue>& InValue) const
{
	if (InValue.IsSet())
	{
		const double Value = InValue.GetValue().Double;
		if (Value == 0.0)
		{
			return FText::FromString(TEXT("0"));
		}
		else
		{
			return FText::FromString(FString::Printf(TEXT("%f (%s)"), Value, *TimeUtils::FormatTimeAuto(Value)));
		}
	}
	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FDoubleValueFormatterAsTimeMs::FormatValue(const TOptional<FTableCellValue>& InValue) const
{
	if (InValue.IsSet())
	{
		const double Value = InValue.GetValue().Double;
		return FText::FromString(TimeUtils::FormatTimeMs(Value));
	}
	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FDoubleValueFormatterAsTimeMs::FormatValueForTooltip(const TOptional<FTableCellValue>& InValue) const
{
	if (InValue.IsSet())
	{
		const double Value = InValue.GetValue().Double;
		if (Value == 0.0)
		{
			return FText::FromString(TEXT("0"));
		}
		else
		{
			return FText::FromString(FString::Printf(TEXT("%f (%s)"), Value, *TimeUtils::FormatTimeMs(Value)));
		}
	}
	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FText FCStringValueFormatterAsText::FormatValue(const TOptional<FTableCellValue>& InValue) const
{
	if (InValue.IsSet())
	{
		const TCHAR* Value = InValue.GetValue().CString;
		return FText::FromString(Value);
	}
	return FText::GetEmpty();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
