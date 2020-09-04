// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapARPinFunctionLibrary.h"
#include "IMagicLeapARPinFeature.h"

EMagicLeapPassableWorldError UMagicLeapARPinFunctionLibrary::CreateTracker()
{
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	return (ARPinImpl != nullptr) ? ARPinImpl->CreateTracker() : EMagicLeapPassableWorldError::NotImplemented;
}

EMagicLeapPassableWorldError UMagicLeapARPinFunctionLibrary::DestroyTracker()
{
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	return (ARPinImpl != nullptr) ? ARPinImpl->DestroyTracker() : EMagicLeapPassableWorldError::NotImplemented;
}

bool UMagicLeapARPinFunctionLibrary::IsTrackerValid()
{
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	return (ARPinImpl != nullptr) ? ARPinImpl->IsTrackerValid() : false;
}

EMagicLeapPassableWorldError UMagicLeapARPinFunctionLibrary::GetNumAvailableARPins(int32& Count)
{
	Count = 0;
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	return (ARPinImpl != nullptr) ? ARPinImpl->GetNumAvailableARPins(Count) : EMagicLeapPassableWorldError::NotImplemented;
}

EMagicLeapPassableWorldError UMagicLeapARPinFunctionLibrary::GetAvailableARPins(int32 NumRequested, TArray<FGuid>& Pins)
{
	if (NumRequested <= 0)
	{
		GetNumAvailableARPins(NumRequested);
	}

	if (NumRequested == 0)
	{
		// There are no coordinate frames to return, so this call did succeed without any errors, it just returned an array of size 0.
		Pins.Reset();
		return EMagicLeapPassableWorldError::None;
	}

	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	return (ARPinImpl != nullptr) ? ARPinImpl->GetAvailableARPins(NumRequested, Pins) : EMagicLeapPassableWorldError::NotImplemented;
}

EMagicLeapPassableWorldError UMagicLeapARPinFunctionLibrary::GetClosestARPin(const FVector& SearchPoint, FGuid& PinID)
{
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	return (ARPinImpl != nullptr) ? ARPinImpl->GetClosestARPin(SearchPoint, PinID) : EMagicLeapPassableWorldError::NotImplemented;
}

EMagicLeapPassableWorldError UMagicLeapARPinFunctionLibrary::QueryARPins(const FMagicLeapARPinQuery& Query, TArray<FGuid>& Pins)
{
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	return (ARPinImpl != nullptr) ? ARPinImpl->QueryARPins(Query, Pins) : EMagicLeapPassableWorldError::NotImplemented;
}

bool UMagicLeapARPinFunctionLibrary::GetARPinPositionAndOrientation_TrackingSpace(const FGuid& PinID, FVector& Position, FRotator& Orientation, bool& PinFoundInEnvironment)
{
	PinFoundInEnvironment = false;
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	return (ARPinImpl != nullptr) ? ARPinImpl->GetARPinPositionAndOrientation_TrackingSpace(PinID, Position, Orientation, PinFoundInEnvironment) : false;
}

bool UMagicLeapARPinFunctionLibrary::GetARPinPositionAndOrientation(const FGuid& PinID, FVector& Position, FRotator& Orientation, bool& PinFoundInEnvironment)
{
	PinFoundInEnvironment = false;
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	return (ARPinImpl != nullptr) ? ARPinImpl->GetARPinPositionAndOrientation(PinID, Position, Orientation, PinFoundInEnvironment) : false;
}

EMagicLeapPassableWorldError UMagicLeapARPinFunctionLibrary::GetARPinState(const FGuid& PinID, FMagicLeapARPinState& State)
{
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	return (ARPinImpl != nullptr) ? ARPinImpl->GetARPinState(PinID, State) : EMagicLeapPassableWorldError::NotImplemented;
}

FString UMagicLeapARPinFunctionLibrary::GetARPinStateToString(const FMagicLeapARPinState& State)
{
	return State.ToString();
}

uint64 ReverseEndian(uint64 n)
{
	uint64 result = 0;
	for (size_t bitshift = 0; bitshift < 64; bitshift += 8)
	{
		result <<= 8;
		result |= 0xFF & (n >> bitshift);
	}
	return result;
}

// impl from MLCoordinateFrameUIDToString()
FString UMagicLeapARPinFunctionLibrary::ARPinIdToString(const FGuid& ARPinId)
{
	// Allow platform specific impls, if available
	const IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	if (ARPinImpl != nullptr)
	{
		FString Result;
		if (ARPinImpl->ARPinIdToString(ARPinId, Result) != EMagicLeapPassableWorldError::NotImplemented)
		{
			return Result;
		}
	}

	uint64_t data[2];
	FMemory::Memcpy(data, &ARPinId, sizeof(FGuid));

	// NOTE: because of some endian confusion on platform, each uint64_t in a CFUID needs to be
	// represented in little-endian order. Since PRIX64 and SCNx64 print/scan in big-endian order
	// regardless of what archtecture you are running on, we need to reverse the byte order so this will
	// always be in little-endian order.
	data[0] = ReverseEndian(data[0]);
	data[1] = ReverseEndian(data[1]);

	return FString::Printf(TEXT("%.8llX-%.4llX-%.4llX-%.4llX-%.12llX"),
							((0xFFFFFFFF00000000u & data[0]) >> 0x20),
							((0x00000000FFFF0000u & data[0]) >> 0x10),
							((0x000000000000FFFFu & data[0]) >> 0x00),
							((0xFFFF000000000000u & data[1]) >> 0x30),
							((0x0000FFFFFFFFFFFFu & data[1]) >> 0x00)
	);
}

bool TryParseHex64(const FString& HexString, uint64& HexNumber)
{
	for (int32 Index = 0; Index < HexString.Len(); ++Index)
	{
		if (!FChar::IsHexDigit(HexString[Index]))
		{
			return false;
		}
	}

	HexNumber = FParse::HexNumber64(*HexString);

	return true;
}

// impl from MLCoordinateFrameUIDFromString()
bool UMagicLeapARPinFunctionLibrary::ParseStringToARPinId(const FString& PinIdString, FGuid& ARPinId)
{
	// Allow platform specific impls, if available
	const IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	if (ARPinImpl != nullptr)
	{
		EMagicLeapPassableWorldError Result = ARPinImpl->ParseStringToARPinId(PinIdString, ARPinId);
		if (Result != EMagicLeapPassableWorldError::NotImplemented)
		{
			return Result == EMagicLeapPassableWorldError::None;
		}
	}

	if (PinIdString.Len() != 36)
	{
		return false;
	}

	if ((PinIdString[8] != TCHAR('-')) ||
		(PinIdString[13] != TCHAR('-')) ||
		(PinIdString[18] != TCHAR('-')) ||
		(PinIdString[23] != TCHAR('-')))
	{
		return false;
	}

	uint64 A = 0, B = 0, C = 0, D = 0, E = 0;
	if (TryParseHex64(PinIdString.Mid(0, 8), A)
		&& TryParseHex64(PinIdString.Mid(9, 4), B)
		&& TryParseHex64(PinIdString.Mid(14, 4), C)
		&& TryParseHex64(PinIdString.Mid(19, 4), D)
		&& TryParseHex64(PinIdString.Mid(24, 12), E)
	)
	{
		uint64 data[2];
		data[0] = ReverseEndian(
			(0xFFFFFFFF00000000u & (A << 0x20u)) |
			(0x00000000FFFF0000u & (B << 0x10u)) |
			(0x000000000000FFFFu & (C << 0x00u)));

		data[1] = ReverseEndian(
			(0xFFFF000000000000u & (D << 0x30u)) |
			(0x0000FFFFFFFFFFFFu & (E << 0x00u)));

		FMemory::Memcpy(&ARPinId, data, sizeof(FGuid));

		return true;
	}

	return false;
}

void UMagicLeapARPinFunctionLibrary::BindToOnMagicLeapARPinUpdatedDelegate(const FMagicLeapARPinUpdatedDelegate& Delegate)
{
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	if (ARPinImpl != nullptr)
	{
		ARPinImpl->BindToOnMagicLeapARPinUpdatedDelegate(Delegate);
	}
}

void UMagicLeapARPinFunctionLibrary::UnBindToOnMagicLeapARPinUpdatedDelegate(const FMagicLeapARPinUpdatedDelegate& Delegate)
{
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	if (ARPinImpl != nullptr)
	{
		ARPinImpl->UnBindToOnMagicLeapARPinUpdatedDelegate(Delegate);
	}
}

EMagicLeapPassableWorldError UMagicLeapARPinFunctionLibrary::SetGlobalQueryFilter(const FMagicLeapARPinQuery& InGlobalFilter)
{
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	if (ARPinImpl != nullptr)
	{
		ARPinImpl->SetGlobalQueryFilter(InGlobalFilter);
		return EMagicLeapPassableWorldError::None;
	}
	return EMagicLeapPassableWorldError::NotImplemented;
}

EMagicLeapPassableWorldError UMagicLeapARPinFunctionLibrary::GetGlobalQueryFilter(FMagicLeapARPinQuery& CurrentGlobalFilter)
{
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	if (ARPinImpl != nullptr)
	{
		CurrentGlobalFilter = ARPinImpl->GetGlobalQueryFilter();
		return EMagicLeapPassableWorldError::None;
	}
	return EMagicLeapPassableWorldError::NotImplemented;
}

void UMagicLeapARPinFunctionLibrary::BindToOnMagicLeapContentBindingFoundDelegate(const FMagicLeapContentBindingFoundDelegate& Delegate)
{
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	if (ARPinImpl != nullptr)
	{
		ARPinImpl->BindToOnMagicLeapContentBindingFoundDelegate(Delegate);
	}
}

void UMagicLeapARPinFunctionLibrary::UnBindToOnMagicLeapContentBindingFoundDelegate(const FMagicLeapContentBindingFoundDelegate& Delegate)
{
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	if (ARPinImpl != nullptr)
	{
		ARPinImpl->UnBindToOnMagicLeapContentBindingFoundDelegate(Delegate);
	}
}

int32 UMagicLeapARPinFunctionLibrary::GetContentBindingSaveGameUserIndex()
{
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	if (ARPinImpl != nullptr)
	{
		return ARPinImpl->GetContentBindingSaveGameUserIndex();
	}

	return 0;
}

void UMagicLeapARPinFunctionLibrary::SetContentBindingSaveGameUserIndex(int32 UserIndex)
{
	IMagicLeapARPinFeature* ARPinImpl = IMagicLeapARPinFeature::Get();
	if (ARPinImpl != nullptr)
	{
		return ARPinImpl->SetContentBindingSaveGameUserIndex(UserIndex);
	}
}
