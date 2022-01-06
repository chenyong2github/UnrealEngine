// Copyright Epic Games, Inc. All Rights Reserved.


// Includes
#include "Analytics/EngineNetAnalytics.h"
#include "AnalyticsEventAttribute.h"
#include "Interfaces/IAnalyticsProvider.h"
#include "EngineLogs.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Net/Core/Connection/NetCloseResult.h"
#include "Misc/StringBuilder.h"
#include "Net/Core/Connection/NetResult.h"


/**
 * FNetConnAnalyticsVars
 */

FNetConnAnalyticsVars::FNetConnAnalyticsVars()
	: OutAckOnlyCount(0)
	, OutKeepAliveCount(0)
{
}

bool FNetConnAnalyticsVars::operator == (const FNetConnAnalyticsVars& A) const
{
	return OutAckOnlyCount == A.OutAckOnlyCount &&
			OutKeepAliveCount == A.OutKeepAliveCount &&
			OutOfOrderPacketsLostCount == A.OutOfOrderPacketsLostCount &&
			OutOfOrderPacketsRecoveredCount == A.OutOfOrderPacketsRecoveredCount &&
			OutOfOrderPacketsDuplicateCount == A.OutOfOrderPacketsDuplicateCount &&
			/** Close results can't be shared - if either are set, equality comparison fails */
			!CloseReason.IsValid() && !A.CloseReason.IsValid() &&
			ClientCloseReasons == A.ClientCloseReasons &&
			RecoveredFaults.OrderIndependentCompareEqual(A.RecoveredFaults);
}

void FNetConnAnalyticsVars::CommitAnalytics(FNetConnAnalyticsVars& AggregatedData)
{
	AggregatedData.OutAckOnlyCount += OutAckOnlyCount;
	AggregatedData.OutKeepAliveCount += OutKeepAliveCount;
	AggregatedData.OutOfOrderPacketsLostCount += OutOfOrderPacketsLostCount;
	AggregatedData.OutOfOrderPacketsRecoveredCount += OutOfOrderPacketsRecoveredCount;
	AggregatedData.OutOfOrderPacketsDuplicateCount += OutOfOrderPacketsDuplicateCount;

	for (TMap<FString, int32>::TConstIterator It(RecoveredFaults); It; ++It)
	{
		int32& CountValue = AggregatedData.RecoveredFaults.FindOrAdd(It.Key());

		CountValue += It.Value();
	}

	FPerNetConnData& CurData = AggregatedData.PerConnectionData.AddDefaulted_GetRef();

	if (CloseReason.IsValid())
	{
		CurData.CloseReason = MoveTemp(CloseReason);
		CurData.ClientCloseReasons = MoveTemp(ClientCloseReasons);
	}
}


/**
 * FNetConnAnalyticsData
 */

void FNetConnAnalyticsData::SendAnalytics()
{
	using namespace UE::Net;

	FNetConnAnalyticsVars NullVars;
	const TSharedPtr<IAnalyticsProvider>& AnalyticsProvider = Aggregator->GetAnalyticsProvider();

	if (!(*this == NullVars) && AnalyticsProvider.IsValid())
	{
		struct FReasonCounter
		{
			FString				ReasonStr;
			int32				Counter		= 0;
		};

		TArray<FReasonCounter> HeadCloseReasons;
		TArray<FReasonCounter> FullCloseReasons;
		TArray<FReasonCounter> HeadClientCloseReasons;
		TArray<FReasonCounter> FullClientCloseReasons;

		auto FindOrAddReason =
			[](TArray<FReasonCounter>& InArray, FString InReasonStr) -> int32&
			{
				for (FReasonCounter& CurEntry : InArray)
				{
					if (CurEntry.ReasonStr == InReasonStr)
					{
						return CurEntry.Counter;
					}
				}

				FReasonCounter& NewReason = InArray.AddDefaulted_GetRef();

				NewReason.ReasonStr = InReasonStr;

				return NewReason.Counter;
			};

		for (const FPerNetConnData& CurData : PerConnectionData)
		{
			if (!CurData.CloseReason.IsValid())
			{
				int32& CurHeadVal = FindOrAddReason(HeadCloseReasons, LexToString(ENetCloseResult::Unknown));
				int32& CurFullVal = FindOrAddReason(FullCloseReasons, LexToString(ENetCloseResult::Unknown));

				CurHeadVal++;
				CurFullVal++;
			}
			else
			{
				int32& CurHeadVal = FindOrAddReason(HeadCloseReasons, CurData.CloseReason->DynamicToString(ENetResultString::ResultEnumOnly));

				CurHeadVal++;

				FString CurFullReason;

				for (FNetResult::FConstIterator It(*CurData.CloseReason); It; ++It)
				{
					FString CurReason = It->DynamicToString(ENetResultString::ResultEnumOnly);
					TStringBuilder<256> CurFormattedReason;

					if (!CurFullReason.IsEmpty())
					{
						CurFormattedReason.AppendChar(TEXT(','));
					}

					CurFormattedReason.Append(ToCStr(CurReason));

					CurFullReason += CurFormattedReason.ToString();
				}

				int32& CurFullVal = FindOrAddReason(FullCloseReasons, CurFullReason);

				CurFullVal++;
			}

			if (CurData.ClientCloseReasons.Num() > 0)
			{
				bool bFirstVal = true;
				FString CurFullReason;

				for (const FString& CurClientReason : CurData.ClientCloseReasons)
				{
					if (bFirstVal)
					{
						int32& CurHeadClientVal = FindOrAddReason(HeadClientCloseReasons, CurClientReason);

						CurHeadClientVal++;
					}

					bFirstVal = false;

					TStringBuilder<256> CurFormattedReason;

					if (!CurFullReason.IsEmpty())
					{
						CurFormattedReason.AppendChar(TEXT(','));
					}

					CurFormattedReason.Append(ToCStr(CurClientReason));

					CurFullReason += CurFormattedReason.ToString();
				}

				int32& CurFullVal = FindOrAddReason(FullClientCloseReasons, CurFullReason);

				CurFullVal++;
			}
		}

		auto CounterSort = [](const FReasonCounter& A, const FReasonCounter& B) -> bool
			{
				return A.Counter < B.Counter;
			};

		HeadCloseReasons.Sort(CounterSort);
		FullCloseReasons.Sort(CounterSort);
		HeadClientCloseReasons.Sort(CounterSort);
		FullClientCloseReasons.Sort(CounterSort);


		UE_LOG(LogNet, Log, TEXT("NetConnection Analytics:"));

		UE_LOG(LogNet, Log, TEXT(" - OutAckOnlyCount: %llu"), OutAckOnlyCount);
		UE_LOG(LogNet, Log, TEXT(" - OutKeepAliveCount: %llu"), OutKeepAliveCount);
		UE_LOG(LogNet, Log, TEXT(" - OutOfOrderPacketsLostCount: %llu"), OutOfOrderPacketsLostCount);
		UE_LOG(LogNet, Log, TEXT(" - OutOfOrderPacketsRecoveredCount: %llu"), OutOfOrderPacketsRecoveredCount);
		UE_LOG(LogNet, Log, TEXT(" - OutOfOrderPacketsDuplicateCount: %llu"), OutOfOrderPacketsDuplicateCount);


		UE_LOG(LogNet, Log, TEXT(" - CloseReasons:"));

		for (const FReasonCounter& CurCounter : HeadCloseReasons)
		{
			UE_LOG(LogNet, Log, TEXT("  - %s: %i"), ToCStr(CurCounter.ReasonStr), CurCounter.Counter);
		}

		UE_LOG(LogNet, Log, TEXT(" - FullCloseReasons:"));

		for (const FReasonCounter& CurCounter : FullCloseReasons)
		{
			UE_LOG(LogNet, Log, TEXT("  - %s: %i"), ToCStr(CurCounter.ReasonStr), CurCounter.Counter);
		}


		if (HeadClientCloseReasons.Num() > 0)
		{
			UE_LOG(LogNet, Log, TEXT(" - ClientCloseReasons:"));

			for (const FReasonCounter& CurCounter : HeadClientCloseReasons)
			{
				UE_LOG(LogNet, Log, TEXT("  - %s: %i"), ToCStr(CurCounter.ReasonStr), CurCounter.Counter);
			}
		}

		if (FullClientCloseReasons.Num() > 0)
		{
			UE_LOG(LogNet, Log, TEXT(" - FullClientCloseReasons:"));

			for (const FReasonCounter& CurCounter : FullClientCloseReasons)
			{
				UE_LOG(LogNet, Log, TEXT("  - %s: %i"), ToCStr(CurCounter.ReasonStr), CurCounter.Counter);
			}
		}


		UE_LOG(LogNet, Log, TEXT(" - RecoveredFaults:"));

		for (TMap<FString, int32>::TConstIterator It(RecoveredFaults); It; ++It)
		{
			UE_LOG(LogNet, Log, TEXT("  - %s: %i"), ToCStr(It.Key()), It.Value());
		}


		static const FString EZEventName = TEXT("Core.ServerNetConn");
		static const FString EZAttrib_OutAckOnlyCount = TEXT("OutAckOnlyCount");
		static const FString EZAttrib_OutKeepAliveCount = TEXT("OutKeepAliveCount");
		static const FString EZAttrib_OutOfOrderPacketsLostCount = TEXT("OutOfOrderPacketsLostCount");
		static const FString EZAttrib_OutOfOrderPacketsRecoveredCount = TEXT("OutOfOrderPacketsRecoveredCount");
		static const FString EZAttrib_OutOfOrderPacketsDuplicateCount = TEXT("OutOfOrderPacketsDuplicateCount");
		static const FString EZAttrib_CloseReasons = TEXT("CloseReasons");
		static const FString EZAttrib_FullCloseReasons = TEXT("FullCloseReasons");
		static const FString EZAttrib_ClientCloseReasons = TEXT("ClientCloseReasons");
		static const FString EZAttrib_FullClientCloseReasons = TEXT("FullClientCloseReasons");
		static const FString EZAttrib_RecoveredFaults = TEXT("RecoveredFaults");
		static const FString EZAttrib_Reason = TEXT("Reason");
		static const FString EZAttrib_Count = TEXT("Count");


		// Json writer subclass to allow us to avoid using a SharedPtr to write basic Json
		typedef TCondensedJsonPrintPolicy<TCHAR> FPrintPolicy;
		class FAnalyticsJsonWriter : public TJsonStringWriter<FPrintPolicy>
		{
		public:
			explicit FAnalyticsJsonWriter(FString* Out) : TJsonStringWriter<FPrintPolicy>(Out, 0)
			{
			}
		};


		FString CloseReasonsJsonStr;
		FAnalyticsJsonWriter CloseReasonsJsonWriter(&CloseReasonsJsonStr);

		CloseReasonsJsonWriter.WriteArrayStart();

		for (const FReasonCounter& CurCounter : HeadCloseReasons)
		{
			CloseReasonsJsonWriter.WriteObjectStart();

			CloseReasonsJsonWriter.WriteValue(EZAttrib_Reason, ToCStr(CurCounter.ReasonStr));
			CloseReasonsJsonWriter.WriteValue(EZAttrib_Count, CurCounter.Counter);

			CloseReasonsJsonWriter.WriteObjectEnd();
		}

		CloseReasonsJsonWriter.WriteArrayEnd();
		CloseReasonsJsonWriter.Close();

		FString FullCloseReasonsJsonStr;
		FAnalyticsJsonWriter FullCloseReasonsJsonWriter(&FullCloseReasonsJsonStr);

		FullCloseReasonsJsonWriter.WriteArrayStart();

		for (const FReasonCounter& CurCounter : FullCloseReasons)
		{
			FullCloseReasonsJsonWriter.WriteObjectStart();

			FullCloseReasonsJsonWriter.WriteValue(EZAttrib_Reason, ToCStr(CurCounter.ReasonStr));
			FullCloseReasonsJsonWriter.WriteValue(EZAttrib_Count, CurCounter.Counter);

			FullCloseReasonsJsonWriter.WriteObjectEnd();
		}

		FullCloseReasonsJsonWriter.WriteArrayEnd();
		FullCloseReasonsJsonWriter.Close();


		FString ClientCloseReasonsJsonStr;
		FAnalyticsJsonWriter ClientCloseReasonsJsonWriter(&ClientCloseReasonsJsonStr);

		ClientCloseReasonsJsonWriter.WriteArrayStart();

		for (const FReasonCounter& CurCounter : HeadClientCloseReasons)
		{
			ClientCloseReasonsJsonWriter.WriteObjectStart();

			ClientCloseReasonsJsonWriter.WriteValue(EZAttrib_Reason, ToCStr(CurCounter.ReasonStr));
			ClientCloseReasonsJsonWriter.WriteValue(EZAttrib_Count, CurCounter.Counter);

			ClientCloseReasonsJsonWriter.WriteObjectEnd();
		}

		ClientCloseReasonsJsonWriter.WriteArrayEnd();
		ClientCloseReasonsJsonWriter.Close();

		FString FullClientCloseReasonsJsonStr;
		FAnalyticsJsonWriter FullClientCloseReasonsJsonWriter(&FullClientCloseReasonsJsonStr);

		FullClientCloseReasonsJsonWriter.WriteArrayStart();

		for (const FReasonCounter& CurCounter : FullClientCloseReasons)
		{
			FullClientCloseReasonsJsonWriter.WriteObjectStart();

			FullClientCloseReasonsJsonWriter.WriteValue(EZAttrib_Reason, ToCStr(CurCounter.ReasonStr));
			FullClientCloseReasonsJsonWriter.WriteValue(EZAttrib_Count, CurCounter.Counter);

			FullClientCloseReasonsJsonWriter.WriteObjectEnd();
		}

		FullClientCloseReasonsJsonWriter.WriteArrayEnd();
		FullClientCloseReasonsJsonWriter.Close();


		FString RecoveredFaultsJsonStr;
		FAnalyticsJsonWriter RecoveredFaultsJsonWriter(&RecoveredFaultsJsonStr);

		RecoveredFaultsJsonWriter.WriteArrayStart();

		for (TMap<FString, int32>::TConstIterator It(RecoveredFaults); It; ++It)
		{
			RecoveredFaultsJsonWriter.WriteObjectStart();

			RecoveredFaultsJsonWriter.WriteValue(EZAttrib_Reason, ToCStr(It.Key()));
			RecoveredFaultsJsonWriter.WriteValue(EZAttrib_Count, It.Value());

			RecoveredFaultsJsonWriter.WriteObjectEnd();
		}

		RecoveredFaultsJsonWriter.WriteArrayEnd();
		RecoveredFaultsJsonWriter.Close();


		AnalyticsProvider->RecordEvent(EZEventName, MakeAnalyticsEventAttributeArray(
			EZAttrib_OutAckOnlyCount, OutAckOnlyCount,
			EZAttrib_OutKeepAliveCount, OutKeepAliveCount,
			EZAttrib_OutOfOrderPacketsLostCount, OutOfOrderPacketsLostCount,
			EZAttrib_OutOfOrderPacketsRecoveredCount, OutOfOrderPacketsRecoveredCount,
			EZAttrib_OutOfOrderPacketsDuplicateCount, OutOfOrderPacketsDuplicateCount,
			EZAttrib_CloseReasons, FJsonFragment(MoveTemp(CloseReasonsJsonStr)),
			EZAttrib_FullCloseReasons, FJsonFragment(MoveTemp(FullCloseReasonsJsonStr)),
			EZAttrib_ClientCloseReasons, FJsonFragment(MoveTemp(ClientCloseReasonsJsonStr)),
			EZAttrib_FullClientCloseReasons, FJsonFragment(MoveTemp(FullClientCloseReasonsJsonStr)),
			EZAttrib_RecoveredFaults, FJsonFragment(MoveTemp(RecoveredFaultsJsonStr))
		));
	}
}
