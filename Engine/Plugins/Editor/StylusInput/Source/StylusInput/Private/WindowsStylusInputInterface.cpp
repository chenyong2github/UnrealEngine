// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "WindowsStylusInputInterface.h"
#include "WindowsRealTimeStylusPlugin.h"
#include "Interfaces/IMainFrameModule.h"

#if PLATFORM_WINDOWS

class FWindowsStylusInputInterfaceImpl
{
public:
	~FWindowsStylusInputInterfaceImpl();

	TComPtr<IRealTimeStylus> RealTimeStylus;
	TSharedPtr<FWindowsRealTimeStylusPlugin> StylusPlugin;
	void* DLLHandle { nullptr };
};

FWindowsStylusInputInterface::FWindowsStylusInputInterface(FWindowsStylusInputInterfaceImpl* InImpl)
{
	Impl = InImpl;
}

FWindowsStylusInputInterface::~FWindowsStylusInputInterface()
{
	delete Impl;
}

int32 FWindowsStylusInputInterface::NumInputDevices() const
{
	return Impl->StylusPlugin->TabletContexts.Num();
}

IStylusInputDevice* FWindowsStylusInputInterface::GetInputDevice(int32 Index) const
{
	if (Index < 0 || Index >= Impl->StylusPlugin->TabletContexts.Num())
	{
		return nullptr;
	}

	return &Impl->StylusPlugin->TabletContexts[Index];
}

FWindowsStylusInputInterfaceImpl::~FWindowsStylusInputInterfaceImpl()
{
	RealTimeStylus->RemoveAllStylusSyncPlugins();
	RealTimeStylus.Reset();

	StylusPlugin.Reset();

	if (DLLHandle != nullptr)
	{
		FPlatformProcess::FreeDllHandle(DLLHandle);
		DLLHandle = nullptr;
	}
}

static void OnMainFrameCreated(FWindowsStylusInputInterfaceImpl& WindowsImpl, TSharedPtr<SWindow> Window)
{
	TSharedPtr<FGenericWindow> NativeWindow = Window->GetNativeWindow();
	HWND Hwnd = reinterpret_cast<HWND>(NativeWindow->GetOSWindowHandle());

	WindowsImpl.RealTimeStylus->put_HWND(reinterpret_cast<uint64>(Hwnd));

	// We desire to receive everything, but what we actually will receive is determined in AddTabletContext
	TArray<GUID> DesiredPackets = {
		GUID_PACKETPROPERTY_GUID_X,
		GUID_PACKETPROPERTY_GUID_Y,
		GUID_PACKETPROPERTY_GUID_Z,
		GUID_PACKETPROPERTY_GUID_PACKET_STATUS,
		GUID_PACKETPROPERTY_GUID_NORMAL_PRESSURE,
		GUID_PACKETPROPERTY_GUID_TANGENT_PRESSURE,
		GUID_PACKETPROPERTY_GUID_X_TILT_ORIENTATION,
		GUID_PACKETPROPERTY_GUID_Y_TILT_ORIENTATION,
		GUID_PACKETPROPERTY_GUID_TWIST_ORIENTATION,
		GUID_PACKETPROPERTY_GUID_WIDTH,
		GUID_PACKETPROPERTY_GUID_HEIGHT,
		// Currently not needed.
		//GUID_PACKETPROPERTY_GUID_BUTTON_PRESSURE,
		//GUID_PACKETPROPERTY_GUID_AZIMUTH_ORIENTATION,
		//GUID_PACKETPROPERTY_GUID_ALTITUDE_ORIENTATION,
	};

	WindowsImpl.RealTimeStylus->SetDesiredPacketDescription(DesiredPackets.Num(), DesiredPackets.GetData());

	WindowsImpl.RealTimeStylus->put_Enabled(Windows::TRUE);
}

TSharedPtr<IStylusInputInterfaceInternal> CreateStylusInputInterface()
{
	FWindowsStylusInputInterfaceImpl* WindowsImpl = new FWindowsStylusInputInterfaceImpl();
	TSharedPtr<IStylusInputInterfaceInternal> InterfaceImpl = MakeShareable(new FWindowsStylusInputInterface(WindowsImpl));

	if (!FWindowsPlatformMisc::CoInitialize()) 
	{
		UE_LOG(LogStylusInput, Error, TEXT("Could not initialize COM library!"));
		return nullptr;
	}

	// Load RealTimeStylus DLL
	const FString InkDLLDirectory = TEXT("C:\\Program Files\\Common Files\\microsoft shared\\ink");
	const FString RTSComDLL = TEXT("RTSCom.dll");
	FPlatformProcess::PushDllDirectory(*InkDLLDirectory);

	WindowsImpl->DLLHandle = FPlatformProcess::GetDllHandle(*(InkDLLDirectory / RTSComDLL));
	if (WindowsImpl->DLLHandle == nullptr)
	{
		FWindowsPlatformMisc::CoUninitialize();
		UE_LOG(LogStylusInput, Error, TEXT("Could not load RTSCom.dll!"));
		return nullptr;
	}

	FPlatformProcess::PopDllDirectory(*InkDLLDirectory);

	// Create RealTimeStylus interface
	void* OutInstance { nullptr };
	HRESULT hr = ::CoCreateInstance(__uuidof(RealTimeStylus), nullptr, CLSCTX_INPROC, __uuidof(IRealTimeStylus), &OutInstance);
	if (FAILED(hr))
	{
		FWindowsPlatformMisc::CoUninitialize();
		UE_LOG(LogStylusInput, Error, TEXT("Could not create RealTimeStylus!"));
		return nullptr;
	}

	WindowsImpl->RealTimeStylus = static_cast<IRealTimeStylus*>(OutInstance);
	WindowsImpl->StylusPlugin = MakeShareable(new FWindowsRealTimeStylusPlugin());
	
	// Create free-threaded marshaller for the plugin
	hr = ::CoCreateFreeThreadedMarshaler(WindowsImpl->StylusPlugin.Get(), 
		&WindowsImpl->StylusPlugin->FreeThreadedMarshaller);
	if (FAILED(hr))
	{
		FWindowsPlatformMisc::CoUninitialize();
		UE_LOG(LogStylusInput, Error, TEXT("Could not create FreeThreadedMarshaller!"));
		return nullptr;
	}

	// Add stylus plugin to the interface
	hr = WindowsImpl->RealTimeStylus->AddStylusSyncPlugin(0, WindowsImpl->StylusPlugin.Get());
	if (FAILED(hr))
	{
		FWindowsPlatformMisc::CoUninitialize();
		UE_LOG(LogStylusInput, Error, TEXT("Could not add stylus plugin to API!"));
		return nullptr;
	}

	// Set hook to catch main window creation
	IMainFrameModule& MainFrameModule = FModuleManager::LoadModuleChecked<IMainFrameModule>("MainFrame");
	if (!MainFrameModule.IsWindowInitialized())
	{
		MainFrameModule.OnMainFrameCreationFinished().AddLambda([WindowsImpl](TSharedPtr<SWindow> Window, bool)
		{
			OnMainFrameCreated(*WindowsImpl, Window);
		});
	}
	else
	{
		OnMainFrameCreated(*WindowsImpl, MainFrameModule.GetParentWindow());
	}

	return InterfaceImpl;
}

#endif // PLATFORM_WINDOWS