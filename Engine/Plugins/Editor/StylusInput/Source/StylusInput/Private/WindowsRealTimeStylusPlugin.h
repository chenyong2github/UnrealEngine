// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include "Windows/PreWindowsApi.h"
#include "Windows/COMPointer.h"
	#include <guiddef.h>
	#include <RTSCom.h>
#include "Windows/PostWindowsApi.h"
#include "Windows/HideWindowsPlatformTypes.h"

#include "IStylusState.h"

/**
 * Packet types as derived from IRealTimeStylus::GetPacketDescriptionData.
 */
enum class EWindowsPacketType
{
	None,
	X,
	Y,
	Z,
	Status,
	NormalPressure,
	TangentPressure,
	ButtonPressure,
	Azimuth,
	Altitude,
	Twist,
	XTilt,
	YTilt,
	Width,
	Height,
};

/**
 * Stylus state for a single frame.
 */
struct FWindowsStylusState
{
	FVector2D Position;
	float Z;
	FVector2D Tilt;
	float Twist;
	float NormalPressure;
	float TangentPressure;
	FVector2D Size;
	bool IsTouching : 1;
	bool IsInverted : 1;

	FWindowsStylusState() :
		Position(0, 0), Z(0), Tilt(0, 0), Twist(0), NormalPressure(0), TangentPressure(0),
		Size(0, 0), IsTouching(false), IsInverted(false)
	{
	}

	FStylusState ToPublicState() const
	{
		return FStylusState(Position, Z, Tilt, Twist, NormalPressure, TangentPressure, Size, IsTouching, IsInverted);
	}
};

/**
 * Description of a packet's information, as derived from IRealTimeStylus::GetPacketDescriptionData.
 */
struct FPacketDescription
{
	EWindowsPacketType Type { EWindowsPacketType::None };
	int32 Minimum { 0 };
	int32 Maximum { 0 };
	float Resolution { 0 };
};

struct FTabletContextInfo : public IStylusInputDevice
{
	int32 Index;

	TABLET_CONTEXT_ID ID;
	TArray<FPacketDescription> PacketDescriptions;
	TArray<EWindowsPacketType> SupportedPackets;

	FWindowsStylusState WindowsState;

	void AddSupportedInput(EStylusInputType Type) { SupportedInputs.Add(Type); }
	void SetDirty() { Dirty = true; }

	virtual void Tick() override
	{
		PreviousState = CurrentState;
		CurrentState = WindowsState.ToPublicState();
		Dirty = false;
	}
};

/**
 * An implementation of an IStylusSyncPlugin for use with the RealTimeStylus API.
 */
class FWindowsRealTimeStylusPlugin : public IStylusSyncPlugin
{
public:
	FWindowsRealTimeStylusPlugin() = default;
	virtual ~FWindowsRealTimeStylusPlugin()
	{
		if (FreeThreadedMarshaller != nullptr)
		{
			FreeThreadedMarshaller->Release();
		}
	}

	virtual ULONG AddRef() override { return ++RefCount; }
	virtual ULONG Release() override
	{
		int NewRefCount = --RefCount;
		if (NewRefCount == 0)
			delete this;

		return NewRefCount;
	}

	virtual HRESULT QueryInterface(const IID& InterfaceID, void** Pointer) override;

	virtual HRESULT TabletAdded(IRealTimeStylus* RealTimeStylus, IInkTablet* InkTablet) override;
	virtual HRESULT TabletRemoved(IRealTimeStylus* RealTimeStylus, LONG iTabletIndex) override;

	virtual HRESULT RealTimeStylusEnabled(IRealTimeStylus* RealTimeStylus, ULONG Num, const TABLET_CONTEXT_ID* InTabletContexts) override;
	virtual HRESULT RealTimeStylusDisabled(IRealTimeStylus* RealTimeStylus, ULONG Num, const TABLET_CONTEXT_ID* InTabletContexts) override;

	virtual HRESULT StylusInRange(IRealTimeStylus* RealTimeStylus, TABLET_CONTEXT_ID TabletContext, STYLUS_ID StylusID) override { return S_OK; }
	virtual HRESULT StylusOutOfRange(IRealTimeStylus* RealTimeStylus, TABLET_CONTEXT_ID TabletContext, STYLUS_ID StylusID) override { return S_OK; }

	virtual HRESULT StylusDown(IRealTimeStylus* RealTimeStylus, const StylusInfo* StylusInfo, ULONG PacketSize, LONG* Packet, LONG** InOutPackets) override;
	virtual HRESULT StylusUp(IRealTimeStylus* RealTimeStylus, const StylusInfo* StylusInfo, ULONG PacketSize, LONG* Packet, LONG** InOutPackets) override;

	virtual HRESULT StylusButtonDown(IRealTimeStylus* RealTimeStylus, STYLUS_ID StylusID, const GUID* GUID, POINT* Position) override { return S_OK; }
	virtual HRESULT StylusButtonUp(IRealTimeStylus* RealTimeStylus, STYLUS_ID StylusID, const GUID* GUID, POINT* Position) override { return S_OK; }

	virtual HRESULT InAirPackets(IRealTimeStylus* RealTimeStylus, const StylusInfo* StylusInfo,
		ULONG PacketCount, ULONG PacketBufferLength, LONG* Packets, ULONG* NumOutPackets, LONG** PtrOutPackets) override;
	virtual HRESULT Packets(IRealTimeStylus* RealTimeStylus, const StylusInfo* StylusInfo,
		ULONG PacketCount, ULONG PacketBufferSize, LONG* Packets, ULONG* NumOutPackets, LONG** PtrOutPackets) override;

	virtual HRESULT CustomStylusDataAdded(IRealTimeStylus* RealTimeStylus, const GUID* GUID, ULONG Data, const BYTE* ByteData) override { return S_OK; }

	virtual HRESULT SystemEvent(IRealTimeStylus* RealTimeStylus, TABLET_CONTEXT_ID TabletContext, STYLUS_ID StylusID, SYSTEM_EVENT EventType, SYSTEM_EVENT_DATA EventData) override { return S_OK; }
	virtual HRESULT Error(IRealTimeStylus* RealTimeStylus, IStylusPlugin* Plugin, RealTimeStylusDataInterest DataInterest, HRESULT ErrorCode, LONG_PTR* Key) override { return S_OK; }

	virtual HRESULT DataInterest(RealTimeStylusDataInterest* OutDataInterest) override
	{
		*OutDataInterest = RTSDI_AllData;
		return S_OK;
	}

	virtual HRESULT UpdateMapping(IRealTimeStylus* RealTimeStylus) override { return S_OK; }

	FTabletContextInfo* FindTabletContext(TABLET_CONTEXT_ID TabletID);

	IUnknown* FreeThreadedMarshaller;
	TArray<FTabletContextInfo> TabletContexts;
	bool HasChanges { false };

private:
	int RefCount { 1 };

	void HandlePacket(IRealTimeStylus* RealTimeStylus, const StylusInfo* StylusInfo, ULONG PacketCount, ULONG PacketBufferLength, LONG* Packets);

	void AddTabletContext(IRealTimeStylus* RealTimeStylus, TABLET_CONTEXT_ID TabletID);
	void RemoveTabletContext(IRealTimeStylus* RealTimeStylus, TABLET_CONTEXT_ID TabletID);
};

#endif // PLATFORM_WINDOWS