// Copyright Epic Games, Inc. All Rights Reserved.

// Includes
#include "Net/Core/Connection/NetCloseResult.h"
#include "Net/Core/Connection/NetEnums.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Class.h"


/**
 * ENetCloseResult
 */

// Avoid replacing with UEnum lookup - this is faster, and is used in constructor
const TCHAR* LexToString(ENetCloseResult InResult)
{
	switch (InResult)
	{
	case ENetCloseResult::NetDriverAlreadyExists:
		return TEXT("NetDriverAlreadyExists");

	case ENetCloseResult::NetDriverCreateFailure:
		return TEXT("NetDriverCreateFailure");

	case ENetCloseResult::NetDriverListenFailure:
		return TEXT("NetDriverListenFailure");

	case ENetCloseResult::ConnectionLost:
		return TEXT("ConnectionLost");

	case ENetCloseResult::ConnectionTimeout:
		return TEXT("ConnectionTimeout");

	case ENetCloseResult::FailureReceived:
		return TEXT("FailureReceived");

	case ENetCloseResult::OutdatedClient:
		return TEXT("OutdatedClient");

	case ENetCloseResult::OutdatedServer:
		return TEXT("OutdatedServer");

	case ENetCloseResult::PendingConnectionFailure:
		return TEXT("PendingConnectionFailure");

	case ENetCloseResult::NetGuidMismatch:
		return TEXT("NetGuidMismatch");

	case ENetCloseResult::NetChecksumMismatch:
		return TEXT("NetChecksumMismatch");

	case ENetCloseResult::SecurityMalformedPacket:
		return TEXT("SecurityMalformedPacket");

	case ENetCloseResult::SecurityInvalidData:
		return TEXT("SecurityInvalidData");

	case ENetCloseResult::SecurityClosed:
		return TEXT("SecurityClosed");

	case ENetCloseResult::Unknown:
		return TEXT("Unknown");

	case ENetCloseResult::Success:
		return TEXT("Success");

	case ENetCloseResult::Extended:
		return TEXT("Extended");

	case ENetCloseResult::RPCDoS:
		return TEXT("RPCDoS");

	case ENetCloseResult::Cleanup:
		return TEXT("Cleanup");

	case ENetCloseResult::MissingLevelPackage:
		return TEXT("MissingLevelPackage");

	case ENetCloseResult::PacketHandlerIncomingError:
		return TEXT("PacketHandlerIncomingError");

	case ENetCloseResult::ZeroLastByte:
		return TEXT("ZeroLastByte");

	case ENetCloseResult::ZeroSize:
		return TEXT("ZeroSize");

	case ENetCloseResult::ReadHeaderFail:
		return TEXT("ReadHeaderFail");

	case ENetCloseResult::ReadHeaderExtraFail:
		return TEXT("ReadHeaderExtraFail");

	case ENetCloseResult::AckSequenceMismatch:
		return TEXT("AckSequenceMismatch");

	case ENetCloseResult::BunchBadChannelIndex:
		return TEXT("BunchBadChannelIndex");

	case ENetCloseResult::BunchChannelNameFail:
		return TEXT("BunchChannelNameFail");

	case ENetCloseResult::BunchWrongChannelType:
		return TEXT("BunchWrongChannelType");

	case ENetCloseResult::BunchHeaderOverflow:
		return TEXT("BunchHeaderOverflow");

	case ENetCloseResult::BunchDataOverflow:
		return TEXT("BunchDataOverflow");

	case ENetCloseResult::BunchPrematureControlChannel:
		return TEXT("BunchPrematureControlChannel");

	case ENetCloseResult::BunchPrematureChannel:
		return TEXT("BunchPrematureChannel");

	case ENetCloseResult::BunchPrematureControlClose:
		return TEXT("BunchPrematureControlClose");

	case ENetCloseResult::UnknownChannelType:
		return TEXT("UnknownChannelType");

	case ENetCloseResult::PrematureSend:
		return TEXT("PrematureSend");

	case ENetCloseResult::CorruptData:
		return TEXT("CorruptData");

	case ENetCloseResult::SocketSendFailure:
		return TEXT("SocketSendFailure");

	case ENetCloseResult::BadChildConnectionIndex:
		return TEXT("BadChildConnectionIndex");

	case ENetCloseResult::LogLimitInstant:
		return TEXT("LogLimitInstant");

	case ENetCloseResult::LogLimitSustained:
		return TEXT("LogLimitSustained");

	case ENetCloseResult::ReceivedNetGUIDBunchFail:
		return TEXT("ReceivedNetGUIDBunchFail");

	case ENetCloseResult::MaxReliableExceeded:
		return TEXT("MaxReliableExceeded");

	case ENetCloseResult::ReceivedNextBunchFail:
		return TEXT("ReceivedNextBunchFail");

	case ENetCloseResult::ReceivedNextBunchQueueFail:
		return TEXT("ReceivedNextBunchQueueFail");

	case ENetCloseResult::PartialInitialReliableDestroy:
		return TEXT("PartialInitialReliableDestroy");

	case ENetCloseResult::PartialMergeReliableDestroy:
		return TEXT("PartialMergeReliableDestroy");

	case ENetCloseResult::PartialInitialNonByteAligned:
		return TEXT("PartialInitialNonByteAligned");

	case ENetCloseResult::PartialNonByteAligned:
		return TEXT("PartialNonByteAligned");

	case ENetCloseResult::PartialFinalPackageMapExports:
		return TEXT("PartialFinalPackageMapExports");

	case ENetCloseResult::PartialTooLarge:
		return TEXT("PartialTooLarge");

	case ENetCloseResult::AlreadyOpen:
		return TEXT("AlreadyOpen");

	case ENetCloseResult::ReliableBeforeOpen:
		return TEXT("ReliableBeforeOpen");

	case ENetCloseResult::ReliableBufferOverflow:
		return TEXT("ReliableBufferOverflow");

	case ENetCloseResult::ControlChannelClose:
		return TEXT("ControlChannelClose");

	case ENetCloseResult::ControlChannelEndianCheck:
		return TEXT("ControlChannelEndianCheck");

	case ENetCloseResult::ControlChannelPlayerChannelFail:
		return TEXT("ControlChannelPlayerChannelFail");

	case ENetCloseResult::ControlChannelMessageUnknown:
		return TEXT("ControlChannelMessageUnknown");

	case ENetCloseResult::ControlChannelMessageFail:
		return TEXT("ControlChannelMessageFail");

	case ENetCloseResult::ControlChannelMessagePayloadFail:
		return TEXT("ControlChannelMessagePayloadFail");

	case ENetCloseResult::ControlChannelBunchOverflowed:
		return TEXT("ControlChannelBunchOverflowed");

	case ENetCloseResult::ControlChannelQueueBunchOverflowed:
		return TEXT("ControlChannelQueueBunchOverflowed");

	case ENetCloseResult::ClientHasMustBeMappedGUIDs:
		return TEXT("ClientHasMustBeMappedGUIDs");

	case ENetCloseResult::UnregisteredMustBeMappedGUID:
		return TEXT("UnregisteredMustBeMappedGUID");

	case ENetCloseResult::ObjectReplicatorReceivedBunchFail:
		return TEXT("ObjectReplicatorReceivedBunchFail");

	case ENetCloseResult::ContentBlockFail:
		return TEXT("ContentBlockFail");

	case ENetCloseResult::ContentBlockHeaderRepLayoutFail:
		return TEXT("ContentBlockHeaderRepLayoutFail");

	case ENetCloseResult::ContentBlockHeaderIsActorFail:
		return TEXT("ContentBlockHeaderIsActorFail");

	case ENetCloseResult::ContentBlockHeaderObjFail:
		return TEXT("ContentBlockHeaderObjFail");

	case ENetCloseResult::ContentBlockHeaderPrematureEnd:
		return TEXT("ContentBlockHeaderPrematureEnd");

	case ENetCloseResult::ContentBlockHeaderSubObjectActor:
		return TEXT("ContentBlockHeaderSubObjectActor");

	case ENetCloseResult::ContentBlockHeaderBadParent:
		return TEXT("ContentBlockHeaderBadParent");

	case ENetCloseResult::ContentBlockHeaderInvalidCreate:
		return TEXT("ContentBlockHeaderInvalidCreate");

	case ENetCloseResult::ContentBlockHeaderStablyNamedFail:
		return TEXT("ContentBlockHeaderStablyNamedFail");

	case ENetCloseResult::ContentBlockHeaderNoSubObjectClass:
		return TEXT("ContentBlockHeaderNoSubObjectClass");

	case ENetCloseResult::ContentBlockHeaderUObjectSubObject:
		return TEXT("ContentBlockHeaderUObjectSubObject");

	case ENetCloseResult::ContentBlockHeaderAActorSubObject:
		return TEXT("ContentBlockHeaderAActorSubObject");

	case ENetCloseResult::ContentBlockHeaderFail:
		return TEXT("ContentBlockHeaderFail");

	case ENetCloseResult::ContentBlockPayloadBitsFail:
		return TEXT("ContentBlockPayloadBitsFail");

	case ENetCloseResult::FieldHeaderRepIndex:
		return TEXT("FieldHeaderRepIndex");

	case ENetCloseResult::FieldHeaderBadRepIndex:
		return TEXT("FieldHeaderBadRepIndex");

	case ENetCloseResult::FieldHeaderPayloadBitsFail:
		return TEXT("FieldHeaderPayloadBitsFail");

	case ENetCloseResult::FieldPayloadFail:
		return TEXT("FieldPayloadFail");

	case ENetCloseResult::FaultDisconnect:
		return TEXT("FaultDisconnect");

	case ENetCloseResult::NotRecoverable:
		return TEXT("NotRecoverable");


	default:
		return TEXT("Invalid");
	}
}

ENetCloseResult FromNetworkFailure(ENetworkFailure::Type Val)
{
	const UEnum* NetFailEnum = StaticEnum<ENetworkFailure::Type>();
	const uint32 RawVal = (uint32)Val;

	if (NetFailEnum != nullptr && RawVal < (uint32)NetFailEnum->GetMaxEnumValue())
	{
		return (ENetCloseResult)RawVal;
	}

	return ENetCloseResult::Unknown;
}

ENetworkFailure::Type ToNetworkFailure(ENetCloseResult Val)
{
	const UEnum* NetFailEnum = StaticEnum<ENetworkFailure::Type>();
	const uint32 RawVal = (uint32)Val;

	if (NetFailEnum != nullptr && RawVal < (uint32)NetFailEnum->GetMaxEnumValue())
	{
		return (ENetworkFailure::Type)RawVal;
	}

	return ENetworkFailure::Type::ConnectionLost;
}

ENetCloseResult FromSecurityEvent(ESecurityEvent::Type Val)
{
	if (const UEnum* NetFailEnum = StaticEnum<ENetworkFailure::Type>())
	{
		const uint32 FirstSecurityEvent = (uint32)NetFailEnum->GetMaxEnumValue();
		const uint32 ConvertedVal = FirstSecurityEvent + (uint32)Val;

		if (ConvertedVal < (uint32)ENetCloseResult::Unknown)
		{
			return (ENetCloseResult)(ConvertedVal);
		}
	}

	return ENetCloseResult::Unknown;
}



#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNetCloseResultEnumTest, "System.Core.Networking.FNetCloseResult.EnumTest",
									EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter);

bool FNetCloseResultEnumTest::RunTest(const FString& Parameters)
{
	using namespace UE::Net;

	// Search by name due to remapping being required for old enums that have been moved
	const UEnum* NetFailEnum = StaticEnum<ENetworkFailure::Type>();
	const UEnum* NetCloseResultEnum = StaticEnum<ENetCloseResult>();
	int64 NetFailEnumLast = 0;

	// Until ENetworkFailure is deprecated
	if (TestTrue(TEXT("ENetworkFailure must exist"), NetFailEnum != nullptr) && NetFailEnum != nullptr)
	{
		// If a new element is added to the end of ENetworkFailure, update this
		const ENetworkFailure::Type LastNetworkFailureEntry = ENetworkFailure::NetChecksumMismatch;
		const ENetCloseResult LastNetworkFailureDuplicate = ENetCloseResult::NetChecksumMismatch;

		NetFailEnumLast = NetFailEnum->GetMaxEnumValue() - 1;

		TestTrue(TEXT("ENetCloseResult must contain (start with) all ENetworkFailure elements"),
					(NetFailEnumLast == (int64)LastNetworkFailureEntry) &&
					((int64)LastNetworkFailureEntry == (int64)LastNetworkFailureDuplicate));


		if (NetCloseResultEnum != nullptr)
		{
			bool bConversionMismatch = false;

			for (int64 EnumIdx=0; EnumIdx<=NetFailEnumLast && !bConversionMismatch; EnumIdx++)
			{
				bConversionMismatch = NetCloseResultEnum->GetNameStringByValue((int64)FromNetworkFailure((ENetworkFailure::Type)EnumIdx)) !=
										NetFailEnum->GetNameStringByValue(EnumIdx);
			}

			TestFalse(TEXT("Start of ENetCloseResult entries must match ENetworkFailure entries"), bConversionMismatch);
		}
	}

	// ESecurityEvent (to be deprecated eventually)
	if (NetFailEnum != nullptr)
	{
		const int64 LastSecurityEvent = (int64)ESecurityEvent::Closed;

		TestTrue(TEXT("Tests must cover all ESecurityEvent entries"),
					FCString::Strlen(ESecurityEvent::ToString((ESecurityEvent::Type)(LastSecurityEvent + 1))) == 0);

		if (NetCloseResultEnum != nullptr)
		{
			const int64 FirstSecurityEventDuplicate = NetFailEnumLast + 1;
			bool bFirstMismatch = false;
			bool bListMismatch = false;

			auto ConvertSecurityEnumName =
				[](ESecurityEvent::Type InVal) -> FString
				{
					TStringBuilder<256> ConvertedElement;

					ConvertedElement.Append(TEXT("Security"));
					ConvertedElement.Append(ToCStr(FString(ESecurityEvent::ToString((ESecurityEvent::Type)InVal)).Replace(TEXT("_"), TEXT(""))));

					return ConvertedElement.ToString();
				};

			for (int64 EnumIdx=0; EnumIdx<=LastSecurityEvent; EnumIdx++)
			{
				bListMismatch = bListMismatch || NetCloseResultEnum->GetNameStringByValue(FirstSecurityEventDuplicate + EnumIdx) !=
													ConvertSecurityEnumName((ESecurityEvent::Type)EnumIdx);

				if (EnumIdx == 0)
				{
					bFirstMismatch = bListMismatch;
				}
			}

			TestFalse(TEXT("ENetCloseResult must contain ESecurityEvent entries, after ENetworkFailure entries"), bFirstMismatch);
			TestFalse(TEXT("ENetCloseResult must contain all ESecurityEvent entries"), bListMismatch);

			bool bConversionMismatch = false;

			for (int64 EnumIdx=0; EnumIdx<=LastSecurityEvent && !bConversionMismatch; EnumIdx++)
			{
				bConversionMismatch = NetCloseResultEnum->GetNameStringByValue((int64)FromSecurityEvent((ESecurityEvent::Type)EnumIdx)) !=
										ConvertSecurityEnumName((ESecurityEvent::Type)EnumIdx);
			}

			TestFalse(TEXT("Start of ENetCloseResult entries must match ESecurityEvent entries"), bConversionMismatch);
		}
	}


	if (TestTrue(TEXT("ENetCloseResult must exist"), NetCloseResultEnum != nullptr) && NetCloseResultEnum != nullptr)
	{
		const int64 NetCloseResultEnumLast = NetCloseResultEnum->GetMaxEnumValue() - 1;
		bool bLexMismatch = false;

		for (int64 EnumIdx=0; EnumIdx<=NetCloseResultEnumLast; EnumIdx++)
		{
			if (NetCloseResultEnum->GetNameStringByValue(EnumIdx) != LexToString((ENetCloseResult)EnumIdx))
			{
				bLexMismatch = true;
				break;
			}
		}

		TestFalse(TEXT("ENetCloseResult must not be missing LexToString entries"), bLexMismatch);
	}

	return true;
}
#endif
