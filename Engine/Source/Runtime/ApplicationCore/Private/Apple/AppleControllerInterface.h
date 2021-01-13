// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericPlatform/IForceFeedbackSystem.h"
#include "Misc/CoreMisc.h"
#include "GenericPlatform/GenericApplicationMessageHandler.h"
#import <GameController/GameController.h>

DECLARE_LOG_CATEGORY_EXTERN(LogAppleController, Log, All);

enum ControllerType
{
	Unassigned,
	SiriRemote,
	ExtendedGamepad,
	XboxGamepad,
	DualShockGamepad
};

enum PlayerIndex
{
	PlayerOne,
	PlayerTwo,
	PlayerThree,
	PlayerFour,
	PlayerUnset
};

/**
 * Interface class for Apple Controllers
 */
class FAppleControllerInterface : public IForceFeedbackSystem
{
public:

	static TSharedRef< FAppleControllerInterface > Create(  const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler );
	static TSharedPtr< FAppleControllerInterface > Get();

public:

	virtual ~FAppleControllerInterface() {}

	void SetMessageHandler( const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler );

	/** Tick the interface (i.e check for new controllers) */
	void Tick( float DeltaTime );

	/**
	 * Poll for controller state and send events if needed
	 */
	void SendControllerEvents();

	/**
	 * IForceFeedbackSystem implementation
	 */
	virtual void SetForceFeedbackChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value) override {}
	virtual void SetForceFeedbackChannelValues(int32 ControllerId, const FForceFeedbackValues &values) override {}
	virtual void SetLightColor(int32 ControllerId, FColor Color) override {}
	virtual void ResetLightColor(int32 ControllerId) override {}

	bool IsControllerAssignedToGamepad(int32 ControllerId) const;
	bool IsGamepadAttached() const;

	const ControllerType GetControllerType(uint32 ControllerIndex);
    void SetControllerType(uint32 ControllerIndex);
    
    GCControllerButtonInput* GetGCControllerButton(const FGamepadKeyNames::Type& ButtonKey, uint32 ControllerIndex);
    
    void HandleInputInternal(const FGamepadKeyNames::Type& UEButton, uint32 ControllerIndex, bool bIsPressed, bool bWasPressed);
    void HandleButtonGamepad(const FGamepadKeyNames::Type& UEButton, uint32 ControllerIndex);
    void HandleAnalogGamepad(const FGamepadKeyNames::Type& UEAxis, uint32 ControllerIndex);
    void HandleVirtualButtonGamepad(const FGamepadKeyNames::Type& UEButtonNegative, const FGamepadKeyNames::Type& UEButtonPositive, uint32 ControllerIndex);

protected:

	FAppleControllerInterface( const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler );


	void HandleConnection(GCController* Controller);
	void HandleDisconnect(GCController* Controller);
	
    void SetCurrentController(GCController* Controller);

protected:
	
    TSharedRef< FGenericApplicationMessageHandler > MessageHandler;


	/** Game controller objects (per user)*/
	struct FUserController
	{
        GCController* Controller;
        
        ControllerType ControllerType;
        
        PlayerIndex PlayerIndex;

        GCExtendedGamepad* PreviousExtendedGamepad;
        GCMicroGamepad* PreviousMicroGamepad;
        
#if !PLATFORM_MAC
		FQuat ReferenceAttitude;
		bool bNeedsReferenceAttitude;
		bool bHasReferenceAttitude;
#endif
        
        // Deprecated but buttonMenu in iOS 14 is not working in current Beta (August 2020).
        bool bPauseWasPressed;
	};
	
    // there is a hardcoded limit of 4 controllers in the API
	FUserController Controllers[4];
	
	TMap<FName, double> NextKeyRepeatTime;
	
	// should we allow controllers to send input
	bool bAllowControllers;
};
