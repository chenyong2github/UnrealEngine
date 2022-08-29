// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertLogFilter_FrontendRoot.h"

#include "ConcertFrontendLogFilter_Ack.h"
#include "ConcertFrontendLogFilter_Client.h"
#include "ConcertFrontendLogFilter_MessageAction.h"
#include "ConcertFrontendLogFilter_MessageType.h"
#include "ConcertFrontendLogFilter_TextSearch.h"
#include "ConcertFrontendLogFilter_Time.h"
#include "ConcertFrontendLogFilter_Size.h"
#include "ConcertLogFilterTypes.h"


namespace UE::MultiUserServer
{
	namespace Private
	{
		static TArray<TSharedRef<FConcertFrontendLogFilter>> CreateCommonFilters()
		{
			return {
				MakeShared<FConcertFrontendLogFilter_MessageAction>(),
				MakeShared<FConcertFrontendLogFilter_MessageType>(),
				MakeShared<FConcertFrontendLogFilter_Time>(ETimeFilter::AllowAfter),
				MakeShared<FConcertFrontendLogFilter_Time>(ETimeFilter::AllowBefore),
				MakeShared<FConcertFrontendLogFilter_Size>(),
				MakeShared<FConcertFrontendLogFilter_Ack>()
			};
		}
	}
	
	TSharedRef<FConcertLogFilter_FrontendRoot> MakeGlobalLogFilter(TSharedRef<FConcertLogTokenizer> Tokenizer)
	{
		return MakeShared<FConcertLogFilter_FrontendRoot>(
			MoveTemp(Tokenizer),
			Private::CreateCommonFilters()
			);
	}

	TSharedRef<FConcertLogFilter_FrontendRoot> MakeClientLogFilter(TSharedRef<FConcertLogTokenizer> Tokenizer, const FGuid& ClientMessageNodeId, const TSharedRef<FEndpointToUserNameCache>& EndpointCache)
	{
		const TArray<TSharedRef<FConcertFrontendLogFilter>> CommonFilters = Private::CreateCommonFilters();
		const TArray<TSharedRef<FConcertLogFilter>> NonVisuals = {
			MakeShared<FConcertLogFilter_Client>(ClientMessageNodeId, EndpointCache)
		};
		return MakeShared<FConcertLogFilter_FrontendRoot>(
			MoveTemp(Tokenizer),
			CommonFilters,
			NonVisuals
			);
	}
}
