// Copyright Epic Games, Inc. All Rights Reserved.


// Includes
#include "AESGCMFaultHandler.h"
#include "Net/Core/Connection/NetConnectionFaultRecoveryBase.h"


/**
 * EAESGCMNetResult
 */

const TCHAR* LexToString(EAESGCMNetResult InResult)
{
	switch (InResult)
	{
	case EAESGCMNetResult::Unknown:
		return TEXT("Unknown");

	case EAESGCMNetResult::Success:
		return TEXT("Success");

	case EAESGCMNetResult::AESMissingIV:
		return TEXT("AESMissingIV");

	case EAESGCMNetResult::AESMissingAuthTag:
		return TEXT("AESMissingAuthTag");

	case EAESGCMNetResult::AESMissingPayload:
		return TEXT("AESMissingPayload");

	case EAESGCMNetResult::AESDecryptionFailed:
		return TEXT("AESDecryptionFailed");

	case EAESGCMNetResult::AESZeroLastByte:
		return TEXT("AESZeroLastByte");


	default:
		return TEXT("Invalid");
	}
}


/**
 * FAESGCMFaultHandler
 */

void FAESGCMFaultHandler::InitFaultRecovery(UE::Net::FNetConnectionFaultRecoveryBase* InFaultRecovery)
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

UE::Net::EHandleNetResult FAESGCMFaultHandler::HandleNetResult(UE::Net::FNetResult&& InResult)
{
	using namespace UE::Net;
	using FNetFaultEscalationHandler = FNetConnectionFaultRecoveryBase::FNetFaultEscalationHandler;

	EHandleNetResult ReturnVal = EHandleNetResult::Handled;
	TNetResult<EAESGCMNetResult>* CastedResult = Cast<EAESGCMNetResult>(&InResult);
	EAESGCMNetResult AESGCMResult = (CastedResult != nullptr ? CastedResult->GetResult() : EAESGCMNetResult::Unknown);
	FEscalationCounter CounterIncrement;

	switch (AESGCMResult)
	{
		case EAESGCMNetResult::AESMissingIV:
		case EAESGCMNetResult::AESMissingAuthTag:
		case EAESGCMNetResult::AESMissingPayload:
		case EAESGCMNetResult::AESDecryptionFailed:
		case EAESGCMNetResult::AESZeroLastByte:
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAESGCMNetResultEnumTest, "System.Core.Networking.EAESGCMNetResult.EnumTest",
									EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter);

bool FAESGCMNetResultEnumTest::RunTest(const FString& Parameters)
{
	const UEnum* AESGCMResultEnum = StaticEnum<EAESGCMNetResult>();

	if (TestTrue(TEXT("EAESGCMNetResult must exist"), AESGCMResultEnum != nullptr) && AESGCMResultEnum != nullptr)
	{
		const int64 AESGCMResultEnumLast = AESGCMResultEnum->GetMaxEnumValue() - 1;
		bool bLexMismatch = false;

		for (int64 EnumIdx=0; EnumIdx<=AESGCMResultEnumLast; EnumIdx++)
		{
			if (AESGCMResultEnum->GetNameStringByValue(EnumIdx) != LexToString((EAESGCMNetResult)EnumIdx))
			{
				bLexMismatch = true;
				break;
			}
		}

		TestFalse(TEXT("EAESGCMNetResult must not be missing LexToString entries"), bLexMismatch);
	}

	return true;
}
#endif

