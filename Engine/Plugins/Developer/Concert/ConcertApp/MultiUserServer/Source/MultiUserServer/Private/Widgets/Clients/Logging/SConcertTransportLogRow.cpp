// Copyright Epic Games, Inc. All Rights Reserved.

#include "SConcertTransportLogRow.h"

#include "Util/ConcertLogTokenizer.h"
#include "SConcertTransportLog.h"
#include "Session/Activity/PredefinedActivityColumns.h"

#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Text/STextBlock.h"

void SConcertTransportLogRow::Construct(const FArguments& InArgs, TSharedPtr<FConcertLogEntry> InLogEntry, const TSharedRef<STableViewBase>& InOwnerTableView, TSharedRef<FConcertLogTokenizer> InTokenizer, TSharedRef<FText> InHighlightText)
{
	LogEntry = MoveTemp(InLogEntry);
	Tokenizer = MoveTemp(InTokenizer);
	HighlightText = MoveTemp(InHighlightText);

	GetClientInfoFunc = InArgs._GetClientInfo;
	
	SMultiColumnTableRow<TSharedPtr<FConcertLogEntry>>::Construct(FSuperRowType::FArguments(), InOwnerTableView);
}

TSharedRef<SWidget> SConcertTransportLogRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	if (ColumnName == SConcertTransportLog::FirstColumnId)
	{
		return SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.Padding(2, 1)
			[
				SNew(SColorBlock)
				.Color_Lambda([this]()
				{
					const TOptional<FConcertClientInfo> ClientInfo = GetClientInfoFromLog();
					return ClientInfo.IsSet()
						? ClientInfo->AvatarColor
						: FLinearColor::Black;
				})
				.Size(FVector2D(4.f, 16.f))
			];
	}
	
	using FColumnWidgetFactoryFunc = TSharedRef<SWidget>(SConcertTransportLogRow::*)(const FName& InColumnName);
	const TMap<FName, FColumnWidgetFactoryFunc> OverrideFactories = {
		{ GET_MEMBER_NAME_CHECKED(FConcertLog, OriginEndpointId), &SConcertTransportLogRow::CreateClientNameColumn },
		{ GET_MEMBER_NAME_CHECKED(FConcertLog, DestinationEndpointId), &SConcertTransportLogRow::CreateClientNameColumn }
	};
	const FColumnWidgetFactoryFunc FallbackFactoryFunc = &SConcertTransportLogRow::CreateDefaultColumn;

	const FColumnWidgetFactoryFunc* FactoryFunc = OverrideFactories.Find(ColumnName);
	return FactoryFunc
		? Invoke(*FactoryFunc, this, ColumnName)
		: Invoke(FallbackFactoryFunc, this, ColumnName);
}

TSharedRef<SWidget> SConcertTransportLogRow::CreateDefaultColumn(const FName& PropertyName)
{
	return SNew(STextBlock)
		.Text_Lambda([PropertyName, WeakEntry = TWeakPtr<FConcertLogEntry>(LogEntry), WeakTokenizer = TWeakPtr<FConcertLogTokenizer>(Tokenizer)]()
		{
			TSharedPtr<FConcertLogEntry> PinnedEntry = WeakEntry.Pin();
			TSharedPtr<FConcertLogTokenizer> PinnedTokenizer = WeakTokenizer.Pin();
			const FProperty* Property = FConcertLog::StaticStruct()->FindPropertyByName(PropertyName);
			if (ensure(PinnedEntry && PinnedTokenizer && Property))
			{
				return FText::FromString(PinnedTokenizer->Tokenize(PinnedEntry->Log, *Property));
			}
				
			return FText::GetEmpty();
		})
		.HighlightText_Lambda([this](){ return *HighlightText.Get(); });
}

TSharedRef<SWidget> SConcertTransportLogRow::CreateClientNameColumn(const FName& PropertyName)
{
	const bool bUseOrigin = GET_MEMBER_NAME_CHECKED(FConcertLog, OriginEndpointId) == PropertyName;
	const bool bUseDestination = GET_MEMBER_NAME_CHECKED(FConcertLog, DestinationEndpointId) == PropertyName;
	check(bUseOrigin || bUseDestination);

	const FGuid& EndpointId = bUseOrigin
		? LogEntry->Log.OriginEndpointId
		: LogEntry->Log.DestinationEndpointId;
	const TOptional<FConcertClientInfo> ClientInfo = GetClientInfoFunc.IsBound()
		? GetClientInfoFunc.Execute(EndpointId)
		: TOptional<FConcertClientInfo>();
	return SNew(STextBlock)
		// If there is no client info available when the log is
		.Text(FText::FromString(ClientInfo.IsSet() ? ClientInfo->DisplayName : EndpointId.ToString()))
		.HighlightText_Lambda([this](){ return *HighlightText.Get(); });
}

TOptional<FConcertClientInfo> SConcertTransportLogRow::GetClientInfoFromLog() const
{
	if (!GetClientInfoFunc.IsBound())
	{
		return {};
	}
		
	const TOptional<FConcertClientInfo> OriginClientInfo = GetClientInfoFunc.IsBound() ? GetClientInfoFunc.Execute(LogEntry->Log.OriginEndpointId) : TOptional<FConcertClientInfo>();
	const TOptional<FConcertClientInfo> DestinationClientInfo = GetClientInfoFunc.IsBound() ? GetClientInfoFunc.Execute(LogEntry->Log.DestinationEndpointId) : TOptional<FConcertClientInfo>();

	// One of the two is definitely the server. If one of them is a client whose info could be found, this will return it.
	return OriginClientInfo.IsSet()
		? OriginClientInfo
		: DestinationClientInfo;
}
