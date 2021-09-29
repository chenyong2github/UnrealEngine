// Copyright Epic Games, Inc. All Rights Reserved.


// Includes
#include "OodleNetworkFaultHandler.h"
#include "Net/Core/Connection/NetConnectionFaultRecoveryBase.h"
#include "Engine/NetConnection.h"


/**
 * EOodleNetResult
 */

const TCHAR* LexToString(EOodleNetResult InResult)
{
	switch (InResult)
	{
	case EOodleNetResult::Unknown:
		return TEXT("Unknown");

	case EOodleNetResult::Success:
		return TEXT("Success");

	case EOodleNetResult::OodleDecodeFailed:
		return TEXT("OodleDecodeFailed");

	case EOodleNetResult::OodleSerializePayloadFail:
		return TEXT("OodleSerializePayloadFail");

	case EOodleNetResult::OodleBadDecompressedLength:
		return TEXT("OodleBadDecompressedLength");

	case EOodleNetResult::OodleNoDictionary:
		return TEXT("OodleNoDictionary");


	default:
		return TEXT("Invalid");
	}
}


/**
 * FOodleNetworkFaultHandler
 */

void FOodleNetworkFaultHandler::InitFaultRecovery(UE::Net::FNetConnectionFaultRecoveryBase* InFaultRecovery)
{
	using namespace UE::Net;

	if (FaultRecovery == nullptr)
	{
		FaultRecovery = InFaultRecovery;

		if (FaultRecovery != nullptr && CounterIndex == INDEX_NONE)
		{
			FaultRecovery->GetFaultManager().AddResultHandlerPtr(this);

			CounterIndex = FaultRecovery->AddNewCounter();

			FaultRecovery->RegisterCounterCategory(ENetFaultCounterCategory::NetworkCorruption, CounterIndex);
		}
	}
}

UE::Net::EHandleNetResult FOodleNetworkFaultHandler::HandleNetResult(UE::Net::FNetResult&& InResult)
{
	using namespace UE::Net;
	using FNetFaultEscalationHandler = FNetConnectionFaultRecoveryBase::FNetFaultEscalationHandler;

	EHandleNetResult ReturnVal = EHandleNetResult::Handled;
	TNetResult<EOodleNetResult>* CastedResult = Cast<EOodleNetResult>(&InResult);
	EOodleNetResult OodleResult = (CastedResult != nullptr ? CastedResult->GetResult() : EOodleNetResult::Unknown);
	FEscalationCounter CounterIncrement;

	switch (OodleResult)
	{
		case EOodleNetResult::OodleDecodeFailed:
		case EOodleNetResult::OodleSerializePayloadFail:
		case EOodleNetResult::OodleBadDecompressedLength:
		{
			CounterIncrement.Counter++;

			break;
		}

		default:
		{
			ReturnVal = EHandleNetResult::NotHandled;

			break;
		}
	}


	if (ReturnVal != EHandleNetResult::NotHandled && CounterIndex != INDEX_NONE)
	{
		FEscalationCounter& FrameCounter = FaultRecovery->GetFrameCounter(CounterIndex);

		FrameCounter.AccumulateCounter(CounterIncrement);

		ReturnVal = FaultRecovery->NotifyHandledFault(MoveTemp(InResult));
	}
		
	return ReturnVal;
}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOodleNetResultEnumTest, "System.Core.Networking.EOodleNetResult.EnumTest",
									EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter);

bool FOodleNetResultEnumTest::RunTest(const FString& Parameters)
{
	const UEnum* OodleResultEnum = StaticEnum<EOodleNetResult>();

	if (TestTrue(TEXT("EOodleNetResult must exist"), OodleResultEnum != nullptr) && OodleResultEnum != nullptr)
	{
		const int64 OodleResultEnumLast = OodleResultEnum->GetMaxEnumValue() - 1;
		bool bLexMismatch = false;

		for (int64 EnumIdx=0; EnumIdx<=OodleResultEnumLast; EnumIdx++)
		{
			if (OodleResultEnum->GetNameStringByValue(EnumIdx) != LexToString((EOodleNetResult)EnumIdx))
			{
				bLexMismatch = true;
				break;
			}
		}

		TestFalse(TEXT("EOodleNetResult must not be missing LexToString entries"), bLexMismatch);
	}

	return true;
}
#endif

