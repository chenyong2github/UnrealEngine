// Copyright Epic Games, Inc. All Rights Reserved.

#include "WinDualShock.h"
#include "Modules/ModuleManager.h"
#include "IInputDeviceModule.h"
#include "IInputDevice.h"
#include "Misc/ConfigCacheIni.h"

DEFINE_LOG_CATEGORY_STATIC(LogWinDualShock, Log, All);

#if DUALSHOCK4_SUPPORT
// If these cannot be found please read Engine/Platforms/PS4/Source/ThirdParty/LibScePad/NoRedistReadme.txt
#include <pad.h>
#include <pad_windows_static.h>
#include "PS4Controllers.h"

class FWinDualShock : public IInputDevice
{

public:
	FWinDualShock(const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler)
		: MessageHandler(InMessageHandler)
	{
		// Configure touch and mouse events
		bool bDS4TouchEvents = false;
		bool bDS4TouchAxisButtons = false;
		bool bDS4MouseEvents = false;
		bool bDS4MotionEvents = false;

		if ( GConfig )
		{
			// Configure PS4Controllers to emit touch events from the DS4 touchpad if the application wants them.
			GConfig->GetBool( TEXT( "PS4Application" ), TEXT( "bDS4TouchEvents" ), bDS4TouchEvents, GEngineIni );

			// Configure PS4Controllers to emit axis events from the DS4 touchpad if the application wants them.
			GConfig->GetBool( TEXT("PS4Application"), TEXT("bDS4TouchAxisButtons"), bDS4TouchAxisButtons, GEngineIni );

			// Configure PS4Controllers to emit mouse events from the DS4 touchpad if the application wants them
			GConfig->GetBool( TEXT( "PS4Application" ), TEXT( "bDS4MouseEvents" ), bDS4MouseEvents, GEngineIni );

			// Configure PS4Controllers to emit motion events from the DS4 if the application wants them
			GConfig->GetBool(TEXT("PS4Application"), TEXT("bDS4MotionEvents"), bDS4MotionEvents, GEngineIni);
		}

		Controllers.SetEmitTouchEvents( bDS4TouchEvents );
		Controllers.SetEmitTouchAxisEvents( bDS4TouchAxisButtons );
		Controllers.SetEmitMouseEvents( bDS4MouseEvents );
		Controllers.SetEmitMotionEvents(bDS4MotionEvents);
	}

	virtual ~FWinDualShock()
	{
		WindowsCleanup();
	}

	virtual void Tick( float DeltaTime ) override
	{

	}

	virtual bool IsGamepadAttached() const override
	{
		return Controllers.IsGamepadAttached();
	}

	virtual void SendControllerEvents() override
	{
		Controllers.SendControllerEvents(MessageHandler);
	}

	void SetChannelValue (int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value)
	{
		Controllers.SetForceFeedbackChannelValue(ControllerId, ChannelType, Value);
	}

	void SetChannelValues (int32 ControllerId, const FForceFeedbackValues &Values)
	{
		Controllers.SetForceFeedbackChannelValues(ControllerId, Values);
	}

	virtual void SetMessageHandler( const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler ) override
	{
		MessageHandler = InMessageHandler;
	}

	virtual bool Exec( UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar ) override
	{
		return false;
	}

private:
	// handler to send all messages to
	TSharedRef<FGenericApplicationMessageHandler> MessageHandler;

	// the object that encapsulates all the controller logic
	FPlatformControllers Controllers;
};
#endif

class FWinDualShockPlugin : public IInputDeviceModule
{
	virtual void StartupModule() override
	{
		IInputDeviceModule::StartupModule();

#if DUALSHOCK4_SUPPORT
		UE_LOG(LogWinDualShock, Log, TEXT("Supported"));
#else
		UE_LOG(LogWinDualShock, Error, TEXT("Support Missing"));
#endif
	}

	virtual TSharedPtr< class IInputDevice > CreateInputDevice(const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler) override
	{
		UE_LOG(LogWinDualShock, Log, TEXT("Input Device Created"));

#if DUALSHOCK4_SUPPORT
		return TSharedPtr< class IInputDevice >(new FWinDualShock(InMessageHandler));
#else
		return TSharedPtr< class IInputDevice >(nullptr);
#endif
	}
};

IMPLEMENT_MODULE( FWinDualShockPlugin, WinDualShock)
