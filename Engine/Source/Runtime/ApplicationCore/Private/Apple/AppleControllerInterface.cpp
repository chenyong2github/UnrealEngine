// Copyright Epic Games, Inc. All Rights Reserved.

#include "AppleControllerInterface.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY(LogAppleController);

#define APPLE_CONTROLLER_DEBUG 0

TSharedRef< FAppleControllerInterface > FAppleControllerInterface::Create(  const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler )
{
	return MakeShareable( new FAppleControllerInterface( InMessageHandler ) );
}

FAppleControllerInterface::FAppleControllerInterface( const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler )
	: MessageHandler( InMessageHandler )
	, bAllowControllers(true)
{
	if(!IS_PROGRAM)
	{
		NSNotificationCenter* notificationCenter = [NSNotificationCenter defaultCenter];
		NSOperationQueue* currentQueue = [NSOperationQueue currentQueue];

		[notificationCenter addObserverForName:GCControllerDidDisconnectNotification object:nil queue:currentQueue usingBlock:^(NSNotification* Notification)
		{
			HandleDisconnect(Notification.object);
		}];

		[notificationCenter addObserverForName:GCControllerDidConnectNotification object:nil queue:currentQueue usingBlock:^(NSNotification* Notification)
		{
			HandleConnection(Notification.object);
            SetCurrentController(Notification.object);
		}];

		dispatch_async(dispatch_get_main_queue(), ^
		{
		   [GCController startWirelessControllerDiscoveryWithCompletionHandler:^{ }];
		});
		
		FMemory::Memzero(Controllers, sizeof(Controllers));
		
		for (GCController* Cont in [GCController controllers])
		{
			HandleConnection(Cont);
		}
	}
}

void FAppleControllerInterface::SetMessageHandler( const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler )
{
	MessageHandler = InMessageHandler;
}

void FAppleControllerInterface::Tick( float DeltaTime )
{
	// NOP
}

void FAppleControllerInterface::SetControllerType(uint32 ControllerIndex)
{
    GCController *Controller = Controllers[ControllerIndex].Controller;

    if ([Controller.productCategory isEqualToString:@"DualShock 4"])
    {
        Controllers[ControllerIndex].ControllerType = ControllerType::DualShockGamepad;
    }
    else if ([Controller.productCategory isEqualToString:@"Xbox One"])
    {
        Controllers[ControllerIndex].ControllerType = ControllerType::XboxGamepad;
    }
    else if (Controller.extendedGamepad != nil)
    {
        Controllers[ControllerIndex].ControllerType = ControllerType::ExtendedGamepad;
    }
    else if (Controller.microGamepad != nil)
    {
        Controllers[ControllerIndex].ControllerType = ControllerType::SiriRemote;
    }
    else
    {
        Controllers[ControllerIndex].ControllerType = ControllerType::Unassigned;
        UE_LOG(LogAppleController, Warning, TEXT("Controller type is not recognized"));
    }
}

void FAppleControllerInterface::SetCurrentController(GCController* Controller)
{
    int32 ControllerIndex = 0;

    for (ControllerIndex = 0; ControllerIndex < UE_ARRAY_COUNT(Controllers); ControllerIndex++)
    {
        if (Controllers[ControllerIndex].Controller == Controller)
        {
            break;
        }
    }
    if (ControllerIndex == UE_ARRAY_COUNT(Controllers))
    {
        HandleConnection(Controller);
    }


    for (ControllerIndex = 0; ControllerIndex < UE_ARRAY_COUNT(Controllers); ControllerIndex++)
    {
        if (Controllers[ControllerIndex].Controller == Controller)
        {
            Controllers[ControllerIndex].PlayerIndex = PlayerIndex::PlayerOne;
        }
        else if (Controllers[ControllerIndex].PlayerIndex == PlayerIndex::PlayerOne)
        {
            Controllers[ControllerIndex].PlayerIndex = PlayerIndex::PlayerUnset;
        }
    }
}

void FAppleControllerInterface::HandleConnection(GCController* Controller)
{
	static_assert(GCControllerPlayerIndex1 == 0 && GCControllerPlayerIndex4 == 3, "Apple changed the player index enums");

	if (!bAllowControllers)
	{
		return;
	}
	
	static_assert(GCControllerPlayerIndex1 == 0 && GCControllerPlayerIndex4 == 3, "Apple changed the player index enums");

	// find a good controller index to use
	bool bFoundSlot = false;
	for (int32 ControllerIndex = 0; ControllerIndex < UE_ARRAY_COUNT(Controllers); ControllerIndex++)
	{
        if (Controllers[ControllerIndex].ControllerType != ControllerType::Unassigned)
        {
            continue;
        }
        
        Controllers[ControllerIndex].PlayerIndex = (PlayerIndex)ControllerIndex;
        Controllers[ControllerIndex].Controller = Controller;
        SetControllerType(ControllerIndex);
        
		// Deprecated but buttonMenu behavior is unreliable in iOS/tvOS 14.0.1
        Controllers[ControllerIndex].bPauseWasPressed = false;
        Controller.controllerPausedHandler = ^(GCController* Cont)
        {
            Controllers[ControllerIndex].bPauseWasPressed = true;
        };
        
        bFoundSlot = true;
        
        UE_LOG(LogAppleController, Log, TEXT("New %s controller inserted, assigned to playerIndex %d"),
               Controllers[ControllerIndex].ControllerType == ControllerType::ExtendedGamepad ||
               Controllers[ControllerIndex].ControllerType == ControllerType::XboxGamepad ||
               Controllers[ControllerIndex].ControllerType == ControllerType::DualShockGamepad
               ? TEXT("Gamepad") : TEXT("Remote"), Controllers[ControllerIndex].PlayerIndex);
        break;
	}
	checkf(bFoundSlot, TEXT("Used a fifth controller somehow!"));
}

void FAppleControllerInterface::HandleDisconnect(GCController* Controller)
{
		// if we don't allow controllers, there could be unset player index here
	if (!bAllowControllers)
	{
        return;
	}
	
    for (int32 ControllerIndex = 0; ControllerIndex < UE_ARRAY_COUNT(Controllers); ControllerIndex++)
    {
        if (Controllers[ControllerIndex].Controller == Controller)
        {
            FMemory::Memzero(&Controllers[ControllerIndex], sizeof(Controllers[ControllerIndex]));
            UE_LOG(LogAppleController, Log, TEXT("Controller for playerIndex %d was removed"), Controllers[ControllerIndex].PlayerIndex);
            return;
            
        }
    }
}

void FAppleControllerInterface::SendControllerEvents()
{
    @autoreleasepool
    {
		for(int32 i = 0; i < UE_ARRAY_COUNT(Controllers); ++i)
		{
			GCController* Cont = Controllers[i].Controller;
			
			GCExtendedGamepad* ExtendedGamepad = nil;

			if (@available(iOS 13, tvOS 13, *))
			{
				ExtendedGamepad = [Cont capture].extendedGamepad;
			}
			else
			{
				ExtendedGamepad = [Cont.extendedGamepad saveSnapshot];
			}

			GCMotion* Motion = Cont.motion;
			
			// make sure the connection handler has run on this guy
			if (Controllers[i].PlayerIndex == PlayerIndex::PlayerUnset)
			{
				continue;
			}

			FUserController& Controller = Controllers[i];
			
			if (Controller.bPauseWasPressed)
			{
				MessageHandler->OnControllerButtonPressed(FGamepadKeyNames::SpecialRight, Controllers[i].PlayerIndex, false);
				MessageHandler->OnControllerButtonReleased(FGamepadKeyNames::SpecialRight, Controllers[i].PlayerIndex, false);

				Controller.bPauseWasPressed = false;
			}
			
			if (ExtendedGamepad != nil)
			{
				const GCExtendedGamepad* PreviousExtendedGamepad = Controller.PreviousExtendedGamepad;

				HandleButtonGamepad(FGamepadKeyNames::FaceButtonBottom, i);
				HandleButtonGamepad(FGamepadKeyNames::FaceButtonLeft, i);
				HandleButtonGamepad(FGamepadKeyNames::FaceButtonRight, i);
				HandleButtonGamepad(FGamepadKeyNames::FaceButtonTop, i);
				HandleButtonGamepad(FGamepadKeyNames::LeftShoulder, i);
				HandleButtonGamepad(FGamepadKeyNames::RightShoulder, i);
				HandleButtonGamepad(FGamepadKeyNames::LeftTriggerThreshold, i);
				HandleButtonGamepad(FGamepadKeyNames::RightTriggerThreshold, i);
				HandleButtonGamepad(FGamepadKeyNames::DPadUp, i);
				HandleButtonGamepad(FGamepadKeyNames::DPadDown, i);
				HandleButtonGamepad(FGamepadKeyNames::DPadRight, i);
				HandleButtonGamepad(FGamepadKeyNames::DPadLeft, i);
				HandleButtonGamepad(FGamepadKeyNames::SpecialRight, i);
				HandleButtonGamepad(FGamepadKeyNames::SpecialLeft, i);
				
				HandleAnalogGamepad(FGamepadKeyNames::LeftAnalogX, i);
				HandleAnalogGamepad(FGamepadKeyNames::LeftAnalogY, i);
				HandleAnalogGamepad(FGamepadKeyNames::RightAnalogX, i);
				HandleAnalogGamepad(FGamepadKeyNames::RightAnalogY, i);
				HandleAnalogGamepad(FGamepadKeyNames::RightTriggerAnalog, i);
				HandleAnalogGamepad(FGamepadKeyNames::LeftTriggerAnalog, i);


				HandleVirtualButtonGamepad(FGamepadKeyNames::LeftStickRight, FGamepadKeyNames::LeftStickLeft, i);
				HandleVirtualButtonGamepad(FGamepadKeyNames::LeftStickDown, FGamepadKeyNames::LeftStickUp, i);
				HandleVirtualButtonGamepad(FGamepadKeyNames::RightStickLeft, FGamepadKeyNames::RightStickRight, i);
				HandleVirtualButtonGamepad(FGamepadKeyNames::RightStickDown, FGamepadKeyNames::RightStickUp, i);
				HandleButtonGamepad(FGamepadKeyNames::LeftThumb, i);
				HandleButtonGamepad(FGamepadKeyNames::RightThumb, i);

				[Controller.PreviousExtendedGamepad release];
				Controller.PreviousExtendedGamepad = ExtendedGamepad;
				[Controller.PreviousExtendedGamepad retain];
			}
		}
    } // @autoreleasepool
}

bool FAppleControllerInterface::IsControllerAssignedToGamepad(int32 ControllerId) const
{
	return ControllerId < UE_ARRAY_COUNT(Controllers) &&
		(Controllers[ControllerId].ControllerType != ControllerType::Unassigned);
}

bool FAppleControllerInterface::IsGamepadAttached() const
{
	bool bIsAttached = false;
	for(int32 i = 0; i < UE_ARRAY_COUNT(Controllers); ++i)
	{
		bIsAttached |= IsControllerAssignedToGamepad(i);
	}
	return bIsAttached && bAllowControllers;
}

GCControllerButtonInput* FAppleControllerInterface::GetGCControllerButton(const FGamepadKeyNames::Type& ButtonKey, uint32 ControllerIndex)
{
    GCController* Cont = Controllers[ControllerIndex].Controller;
    
    GCExtendedGamepad* ExtendedGamepad = Cont.extendedGamepad;
    GCControllerButtonInput *ButtonToReturn = nullptr;

    if (ButtonKey == FGamepadKeyNames::FaceButtonBottom){ButtonToReturn = ExtendedGamepad.buttonA;}
    else if (ButtonKey == FGamepadKeyNames::FaceButtonRight){ButtonToReturn = ExtendedGamepad.buttonB;}
    else if (ButtonKey == FGamepadKeyNames::FaceButtonLeft){ButtonToReturn = ExtendedGamepad.buttonX;}
    else if (ButtonKey == FGamepadKeyNames::FaceButtonTop){ButtonToReturn = ExtendedGamepad.buttonY;}
    else if (ButtonKey == FGamepadKeyNames::LeftShoulder){ButtonToReturn = ExtendedGamepad.leftShoulder;}
    else if (ButtonKey == FGamepadKeyNames::RightShoulder){ButtonToReturn = ExtendedGamepad.rightShoulder;}
    else if (ButtonKey == FGamepadKeyNames::LeftTriggerThreshold){ButtonToReturn = ExtendedGamepad.leftTrigger;}
    else if (ButtonKey == FGamepadKeyNames::RightTriggerThreshold){ButtonToReturn = ExtendedGamepad.rightTrigger;}
    else if (ButtonKey == FGamepadKeyNames::LeftTriggerAnalog){ButtonToReturn = ExtendedGamepad.leftTrigger;}
    else if (ButtonKey == FGamepadKeyNames::RightTriggerAnalog){ButtonToReturn = ExtendedGamepad.rightTrigger;}
    else if (ButtonKey == FGamepadKeyNames::LeftThumb){ButtonToReturn = ExtendedGamepad.leftThumbstickButton;}
    else if (ButtonKey == FGamepadKeyNames::RightThumb){ButtonToReturn = ExtendedGamepad.rightThumbstickButton;}

    return ButtonToReturn;
}

const ControllerType FAppleControllerInterface::GetControllerType(uint32 ControllerIndex)
{
    if (Controllers[ControllerIndex].Controller != nullptr)
    {
        return Controllers[ControllerIndex].ControllerType;
    }
    return ControllerType::Unassigned;
}

void FAppleControllerInterface::HandleInputInternal(const FGamepadKeyNames::Type& UEButton, uint32 ControllerIndex, bool bIsPressed, bool bWasPressed)
{
    const double CurrentTime = FPlatformTime::Seconds();
    const float InitialRepeatDelay = 0.2f;
    const float RepeatDelay = 0.1;
    GCController* Cont = Controllers[ControllerIndex].Controller;

    if (bWasPressed != bIsPressed)
    {
#if APPLE_CONTROLLER_DEBUG
        NSLog(@"%@ button %s on controller %d", bIsPressed ? @"Pressed" : @"Released", TCHAR_TO_ANSI(*UEButton.ToString()), Controllers[ControllerIndex].PlayerIndex);
#endif
        bIsPressed ? MessageHandler->OnControllerButtonPressed(UEButton, Controllers[ControllerIndex].PlayerIndex, false) : MessageHandler->OnControllerButtonReleased(UEButton, Controllers[ControllerIndex].PlayerIndex, false);
        NextKeyRepeatTime.FindOrAdd(UEButton) = CurrentTime + InitialRepeatDelay;
    }
    else if(bIsPressed)
    {
        double* NextRepeatTime = NextKeyRepeatTime.Find(UEButton);
        if(NextRepeatTime && *NextRepeatTime <= CurrentTime)
        {
            MessageHandler->OnControllerButtonPressed(UEButton, Controllers[ControllerIndex].PlayerIndex, true);
            *NextRepeatTime = CurrentTime + RepeatDelay;
        }
    }
    else
    {
        NextKeyRepeatTime.Remove(UEButton);
    }
}

void FAppleControllerInterface::HandleVirtualButtonGamepad(const FGamepadKeyNames::Type& UEButtonNegative, const FGamepadKeyNames::Type& UEButtonPositive, uint32 ControllerIndex)
{
    GCController* Cont = Controllers[ControllerIndex].Controller;
    GCExtendedGamepad *ExtendedGamepad = Cont.extendedGamepad;
    GCExtendedGamepad *ExtendedPreviousGamepad = Controllers[ControllerIndex].PreviousExtendedGamepad;;

    // Send controller events any time we are passed the given input threshold similarly to PC/Console (see: XInputInterface.cpp)
    const float RepeatDeadzone = 0.24;
    
    bool bWasNegativePressed = false;
    bool bNegativePressed = false;
    bool bWasPositivePressed = false;
    bool bPositivePressed = false;
    
    if (UEButtonNegative == FGamepadKeyNames::LeftStickLeft && UEButtonPositive == FGamepadKeyNames::LeftStickRight)
    {
        bWasNegativePressed = ExtendedPreviousGamepad != nil && ExtendedPreviousGamepad.leftThumbstick.xAxis.value <= -RepeatDeadzone;
        bNegativePressed = ExtendedGamepad.leftThumbstick.xAxis.value <= -RepeatDeadzone;
        bWasPositivePressed = ExtendedPreviousGamepad != nil && ExtendedPreviousGamepad.leftThumbstick.xAxis.value >= RepeatDeadzone;
        bPositivePressed = ExtendedGamepad.leftThumbstick.xAxis.value >= RepeatDeadzone;

        HandleInputInternal(FGamepadKeyNames::LeftStickDown, ControllerIndex, bNegativePressed, bWasNegativePressed);
        HandleInputInternal(FGamepadKeyNames::LeftStickUp, ControllerIndex, bPositivePressed, bWasPositivePressed);
    }
    else if (UEButtonNegative == FGamepadKeyNames::LeftStickDown && UEButtonPositive == FGamepadKeyNames::LeftStickUp)
    {
        bWasNegativePressed = ExtendedPreviousGamepad != nil && ExtendedPreviousGamepad.leftThumbstick.yAxis.value <= -RepeatDeadzone;
        bNegativePressed = ExtendedGamepad.leftThumbstick.yAxis.value <= -RepeatDeadzone;
        bWasPositivePressed = ExtendedPreviousGamepad != nil && ExtendedPreviousGamepad.leftThumbstick.yAxis.value >= RepeatDeadzone;
        bPositivePressed = ExtendedGamepad.leftThumbstick.yAxis.value >= RepeatDeadzone;

        HandleInputInternal(FGamepadKeyNames::LeftStickDown, ControllerIndex, bNegativePressed, bWasNegativePressed);
        HandleInputInternal(FGamepadKeyNames::LeftStickUp, ControllerIndex, bPositivePressed, bWasPositivePressed);
    }
    else if (UEButtonNegative == FGamepadKeyNames::RightStickLeft && UEButtonPositive == FGamepadKeyNames::RightStickRight)
    {
        bWasNegativePressed = ExtendedPreviousGamepad != nil && ExtendedPreviousGamepad.rightThumbstick.xAxis.value <= -RepeatDeadzone;
        bNegativePressed = ExtendedGamepad.rightThumbstick.xAxis.value <= -RepeatDeadzone;
        bWasPositivePressed = ExtendedPreviousGamepad != nil && ExtendedPreviousGamepad.rightThumbstick.xAxis.value >= RepeatDeadzone;
        bPositivePressed = ExtendedGamepad.rightThumbstick.xAxis.value >= RepeatDeadzone;

        HandleInputInternal(FGamepadKeyNames::LeftStickDown, ControllerIndex, bNegativePressed, bWasNegativePressed);
        HandleInputInternal(FGamepadKeyNames::LeftStickUp, ControllerIndex, bPositivePressed, bWasPositivePressed);
    }
    else if (UEButtonNegative == FGamepadKeyNames::RightStickDown && UEButtonPositive == FGamepadKeyNames::RightStickUp)
    {
        bWasNegativePressed = ExtendedPreviousGamepad != nil && ExtendedPreviousGamepad.rightThumbstick.yAxis.value <= -RepeatDeadzone;
        bNegativePressed = ExtendedGamepad.rightThumbstick.yAxis.value <= -RepeatDeadzone;
        bWasPositivePressed = ExtendedPreviousGamepad != nil && ExtendedPreviousGamepad.rightThumbstick.yAxis.value >= RepeatDeadzone;
        bPositivePressed = ExtendedGamepad.rightThumbstick.yAxis.value >= RepeatDeadzone;

        HandleInputInternal(FGamepadKeyNames::LeftStickDown, ControllerIndex, bNegativePressed, bWasNegativePressed);
        HandleInputInternal(FGamepadKeyNames::LeftStickUp, ControllerIndex, bPositivePressed, bWasPositivePressed);
    }
}

void FAppleControllerInterface::HandleButtonGamepad(const FGamepadKeyNames::Type& UEButton, uint32 ControllerIndex)
{
    GCController* Cont = Controllers[ControllerIndex].Controller;

    
    bool bWasPressed = false;
    bool bIsPressed = false;

    GCExtendedGamepad *ExtendedGamepad = nil;
    GCExtendedGamepad *ExtendedPreviousGamepad = nil;

    GCMicroGamepad *MicroGamepad = nil;
    GCMicroGamepad *MicroPreviousGamepad = nil;

#define SET_PRESSED(Gamepad, PreviousGamepad, GCButton, UEButton) \
{ \
bWasPressed = PreviousGamepad.GCButton.pressed; \
bIsPressed = Gamepad.GCButton.pressed; \
}
    switch (Controllers[ControllerIndex].ControllerType)
    {
        case ControllerType::ExtendedGamepad:
        case ControllerType::DualShockGamepad:
        case ControllerType::XboxGamepad:
            
            ExtendedGamepad = Cont.extendedGamepad;
            ExtendedPreviousGamepad = Controllers[ControllerIndex].PreviousExtendedGamepad;
       
            if (UEButton == FGamepadKeyNames::FaceButtonLeft){SET_PRESSED(ExtendedGamepad, ExtendedPreviousGamepad, buttonX, UEButton);}
            else if (UEButton == FGamepadKeyNames::FaceButtonBottom){SET_PRESSED(ExtendedGamepad, ExtendedPreviousGamepad, buttonA, UEButton);}
            else if (UEButton == FGamepadKeyNames::FaceButtonRight){SET_PRESSED(ExtendedGamepad, ExtendedPreviousGamepad, buttonB, UEButton);}
            else if (UEButton == FGamepadKeyNames::FaceButtonTop){SET_PRESSED(ExtendedGamepad, ExtendedPreviousGamepad, buttonY, UEButton);}
            else if (UEButton == FGamepadKeyNames::LeftShoulder){SET_PRESSED(ExtendedGamepad, ExtendedPreviousGamepad, leftShoulder, UEButton);}
            else if (UEButton == FGamepadKeyNames::RightShoulder){SET_PRESSED(ExtendedGamepad, ExtendedPreviousGamepad, rightShoulder, UEButton);}
            else if (UEButton == FGamepadKeyNames::LeftTriggerThreshold){SET_PRESSED(ExtendedGamepad, ExtendedPreviousGamepad, leftTrigger, UEButton);}
            else if (UEButton == FGamepadKeyNames::RightTriggerThreshold){SET_PRESSED(ExtendedGamepad, ExtendedPreviousGamepad, rightTrigger, UEButton);}
            else if (UEButton == FGamepadKeyNames::DPadUp){SET_PRESSED(ExtendedGamepad, ExtendedPreviousGamepad, dpad.up, UEButton);}
            else if (UEButton == FGamepadKeyNames::DPadDown){SET_PRESSED(ExtendedGamepad, ExtendedPreviousGamepad, dpad.down, UEButton);}
            else if (UEButton == FGamepadKeyNames::DPadRight){SET_PRESSED(ExtendedGamepad, ExtendedPreviousGamepad, dpad.right, UEButton);}
            else if (UEButton == FGamepadKeyNames::DPadLeft){SET_PRESSED(ExtendedGamepad, ExtendedPreviousGamepad, dpad.left, UEButton);}
            else if (UEButton == FGamepadKeyNames::SpecialRight){SET_PRESSED(ExtendedGamepad, ExtendedPreviousGamepad, buttonMenu, UEButton);}
            else if (UEButton == FGamepadKeyNames::SpecialLeft){SET_PRESSED(ExtendedGamepad, ExtendedPreviousGamepad, buttonOptions, UEButton);}
            else if (UEButton == FGamepadKeyNames::LeftThumb){SET_PRESSED(ExtendedGamepad, ExtendedPreviousGamepad, leftThumbstickButton, UEButton);}
            else if (UEButton == FGamepadKeyNames::RightThumb){SET_PRESSED(ExtendedGamepad, ExtendedPreviousGamepad, rightThumbstickButton, UEButton);}
            break;
        case ControllerType::SiriRemote:
            
            MicroGamepad = Cont.microGamepad;
            MicroPreviousGamepad = Controllers[ControllerIndex].PreviousMicroGamepad;

            if (UEButton == FGamepadKeyNames::LeftStickUp){SET_PRESSED(MicroGamepad, MicroPreviousGamepad, dpad.up, UEButton);}
            else if (UEButton == FGamepadKeyNames::LeftStickDown){SET_PRESSED(MicroGamepad, MicroPreviousGamepad, dpad.down, UEButton);}
            else if (UEButton == FGamepadKeyNames::LeftStickRight){SET_PRESSED(MicroGamepad, MicroPreviousGamepad, dpad.right, UEButton);}
            else if (UEButton == FGamepadKeyNames::LeftStickLeft){SET_PRESSED(MicroGamepad, MicroPreviousGamepad, dpad.left, UEButton);}
            else if (UEButton == FGamepadKeyNames::FaceButtonBottom){SET_PRESSED(MicroGamepad, MicroPreviousGamepad, buttonA, UEButton);}
            else if (UEButton == FGamepadKeyNames::FaceButtonLeft){SET_PRESSED(MicroGamepad, MicroPreviousGamepad, buttonX, UEButton);}
            else if (UEButton == FGamepadKeyNames::SpecialRight){SET_PRESSED(MicroGamepad, MicroPreviousGamepad, buttonMenu, UEButton);}
            break;
    }
    HandleInputInternal(UEButton, ControllerIndex, bIsPressed, bWasPressed);
}

void FAppleControllerInterface::HandleAnalogGamepad(const FGamepadKeyNames::Type& UEAxis, uint32 ControllerIndex)
{
    GCController* Cont = Controllers[ControllerIndex].Controller;
    
    // Send controller events any time we are passed the given input threshold similarly to PC/Console (see: XInputInterface.cpp)
    const float RepeatDeadzone = 0.24;
    bool bWasPositivePressed = false;
    bool bPositivePressed = false;
    bool bWasNegativePressed = false;
    bool bNegativePressed = false;
    float axisValue = 0;

    GCExtendedGamepad *ExtendedGamepad = Cont.extendedGamepad;
    GCExtendedGamepad *ExtendedPreviousGamepad = Controllers[ControllerIndex].PreviousExtendedGamepad;;

    GCMicroGamepad *MicroGamepad = Cont.microGamepad;
    GCMicroGamepad *MicroPreviousGamepad = Controllers[ControllerIndex].PreviousMicroGamepad;
    
    switch (Controllers[ControllerIndex].ControllerType)
    {
        case ControllerType::ExtendedGamepad:
        case ControllerType::DualShockGamepad:
        case ControllerType::XboxGamepad:
            
            if (UEAxis == FGamepadKeyNames::LeftAnalogX){if ((ExtendedPreviousGamepad != nil && ExtendedGamepad.leftThumbstick.xAxis.value != ExtendedPreviousGamepad.leftThumbstick.xAxis.value) ||
                                                             (ExtendedGamepad.leftThumbstick.xAxis.value < -RepeatDeadzone || ExtendedGamepad.leftThumbstick.xAxis.value > RepeatDeadzone)){axisValue = ExtendedGamepad.leftThumbstick.xAxis.value;}}
            else if (UEAxis == FGamepadKeyNames::LeftAnalogY){if ((ExtendedPreviousGamepad != nil && ExtendedGamepad.leftThumbstick.yAxis.value != ExtendedPreviousGamepad.leftThumbstick.yAxis.value) ||
                                                                  (ExtendedGamepad.leftThumbstick.yAxis.value < -RepeatDeadzone || ExtendedGamepad.leftThumbstick.yAxis.value > RepeatDeadzone)){axisValue = ExtendedGamepad.leftThumbstick.yAxis.value;}}
            else if (UEAxis == FGamepadKeyNames::RightAnalogX){if ((ExtendedPreviousGamepad != nil && ExtendedGamepad.rightThumbstick.xAxis.value != ExtendedPreviousGamepad.rightThumbstick.xAxis.value) ||
                                                                   (ExtendedGamepad.rightThumbstick.xAxis.value < -RepeatDeadzone || ExtendedGamepad.rightThumbstick.xAxis.value > RepeatDeadzone)){axisValue = ExtendedGamepad.rightThumbstick.xAxis.value;}}
            else if (UEAxis == FGamepadKeyNames::RightAnalogY){if ((ExtendedPreviousGamepad != nil && ExtendedGamepad.rightThumbstick.yAxis.value != ExtendedPreviousGamepad.rightThumbstick.yAxis.value) ||
                                                                   (ExtendedGamepad.rightThumbstick.yAxis.value < -RepeatDeadzone || ExtendedGamepad.rightThumbstick.yAxis.value > RepeatDeadzone)){axisValue = ExtendedGamepad.rightThumbstick.yAxis.value;}}
            else if (UEAxis == FGamepadKeyNames::LeftTriggerAnalog){if ((ExtendedPreviousGamepad != nil && ExtendedGamepad.leftTrigger.value != ExtendedPreviousGamepad.leftTrigger.value) ||
                                                                        (ExtendedGamepad.leftTrigger.value < -RepeatDeadzone || ExtendedGamepad.leftTrigger.value > RepeatDeadzone)){axisValue = ExtendedGamepad.leftTrigger.value;}}
            else if (UEAxis == FGamepadKeyNames::RightTriggerAnalog){if ((ExtendedPreviousGamepad != nil && ExtendedGamepad.rightTrigger.value != ExtendedPreviousGamepad.rightTrigger.value) ||
                                                                         (ExtendedGamepad.rightTrigger.value < -RepeatDeadzone || ExtendedGamepad.rightTrigger.value > RepeatDeadzone)){axisValue = ExtendedGamepad.rightTrigger.value;}}
            break;
            
        case ControllerType::SiriRemote:
            
            if (UEAxis == FGamepadKeyNames::LeftAnalogX){if ((ExtendedPreviousGamepad != nil && MicroGamepad.dpad.xAxis.value != ExtendedPreviousGamepad.dpad.xAxis.value) ||
                                                             (MicroGamepad.dpad.xAxis.value < -RepeatDeadzone || MicroGamepad.dpad.xAxis.value > RepeatDeadzone)){axisValue = MicroGamepad.dpad.xAxis.value;}}
            else if (UEAxis == FGamepadKeyNames::LeftAnalogY){if ((MicroPreviousGamepad != nil && MicroGamepad.dpad.yAxis.value != MicroPreviousGamepad.dpad.yAxis.value) ||
                                                                  (MicroGamepad.dpad.yAxis.value < -RepeatDeadzone || MicroGamepad.dpad.yAxis.value > RepeatDeadzone)){axisValue = MicroGamepad.dpad.yAxis.value;}}
            break;
    }
#if APPLE_CONTROLLER_DEBUG
    NSLog(@"Axis %s is %f", TCHAR_TO_ANSI(*UEAxis.ToString()), axisValue);
#endif
    MessageHandler->OnControllerAnalog(UEAxis, Controllers[ControllerIndex].PlayerIndex, axisValue);
}
