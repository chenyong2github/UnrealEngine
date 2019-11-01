// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "InputDevice.h"
#include "PixelStreamerInputComponent.h"
#include "PixelStreamingSettings.h"
#include "IPixelStreamingModule.h"
#include "ProtocolDefs.h"
#include "JavaScriptKeyCodes.inl"
#include "IPixelStreamingModule.h"

#include "Engine/Engine.h"
#include "Engine/GameEngine.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SWindow.h"
#include "Misc/ScopeLock.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Framework/Application/SlateUser.h"

DECLARE_LOG_CATEGORY_EXTERN(PixelStreamerInputDevice, Log, VeryVerbose);
DEFINE_LOG_CATEGORY(PixelStreamerInputDevice);

DECLARE_LOG_CATEGORY_EXTERN(PixelStreamerInput, Log, VeryVerbose);
DEFINE_LOG_CATEGORY(PixelStreamerInput);

/**
 * When reading input from a browser then the cursor position will be sent
 * across with mouse events. We want to use this position and avoid getting the
 * cursor position from the operating system. This is not relevant to touch
 * events.
 */
class FCursor : public ICursor
{
public:
	FCursor() {}
	virtual ~FCursor() = default;
	virtual void* CreateCursorFromFile(const FString& InPathToCursorWithoutExtension, FVector2D HotSpot) override { return nullptr; }
	virtual void* CreateCursorFromRGBABuffer(const FColor* Pixels, int32 Width, int32 Height, FVector2D InHotSpot) override { return nullptr; }
	virtual FVector2D GetPosition() const override { return Position; }
	virtual void SetPosition(const int32 X, const int32 Y) override { Position = FVector2D(X, Y); };
	virtual void SetType(const EMouseCursor::Type InNewCursor) override {};
	virtual EMouseCursor::Type GetType() const override { return EMouseCursor::Type::Default; };
	virtual void GetSize(int32& Width, int32& Height) const override {};
	virtual void Show(bool bShow) override {};
	virtual void Lock(const RECT* const Bounds) override {};
	virtual void SetTypeShape(EMouseCursor::Type InCursorType, void* CursorHandle) override {};

private:
	/** The cursor position sent across with mouse events. */
	FVector2D Position;
};

/**
* Wrap the GenericApplication layer so we can replace the cursor and override
* certain behavior.
*/
class FApplicationWrapper : public GenericApplication
{
public:
	FApplicationWrapper(TSharedPtr<GenericApplication> InWrappedApplication)
		: GenericApplication(MakeShareable(new FCursor()))
		, WrappedApplication(InWrappedApplication)
	{
	}

	/**
	 * Functions passed directly to the wrapped application.
	 */

	virtual void SetMessageHandler(const TSharedRef< FGenericApplicationMessageHandler >& InMessageHandler) { WrappedApplication->SetMessageHandler(InMessageHandler); }
	virtual void PollGameDeviceState(const float TimeDelta) { WrappedApplication->PollGameDeviceState(TimeDelta); }
	virtual void PumpMessages(const float TimeDelta) { WrappedApplication->PumpMessages(TimeDelta); }
	virtual void ProcessDeferredEvents(const float TimeDelta) { WrappedApplication->ProcessDeferredEvents(TimeDelta); }
	virtual void Tick(const float TimeDelta) { WrappedApplication->Tick(TimeDelta); }
	virtual TSharedRef< FGenericWindow > MakeWindow() { return WrappedApplication->MakeWindow(); }
	virtual void InitializeWindow(const TSharedRef< FGenericWindow >& Window, const TSharedRef< FGenericWindowDefinition >& InDefinition, const TSharedPtr< FGenericWindow >& InParent, const bool bShowImmediately) { WrappedApplication->InitializeWindow(Window, InDefinition, InParent, bShowImmediately); }
	virtual void SetCapture(const TSharedPtr< FGenericWindow >& InWindow) { WrappedApplication->SetCapture(InWindow); }
	virtual void* GetCapture(void) const { return WrappedApplication->GetCapture(); }
	virtual FModifierKeysState GetModifierKeys() const { return WrappedApplication->GetModifierKeys(); }
	virtual TSharedPtr< FGenericWindow > GetWindowUnderCursor() { return WrappedApplication->GetWindowUnderCursor(); }
	virtual void SetHighPrecisionMouseMode(const bool Enable, const TSharedPtr< FGenericWindow >& InWindow) { WrappedApplication->SetHighPrecisionMouseMode(Enable, InWindow); };
	virtual bool IsUsingHighPrecisionMouseMode() const { return WrappedApplication->IsUsingHighPrecisionMouseMode(); }
	virtual bool IsUsingTrackpad() const { return WrappedApplication->IsUsingTrackpad(); }
	virtual bool IsMouseAttached() const { return WrappedApplication->IsMouseAttached(); }
	virtual bool IsGamepadAttached() const { return WrappedApplication->IsGamepadAttached(); }
	virtual void RegisterConsoleCommandListener(const FOnConsoleCommandListener& InListener) { WrappedApplication->RegisterConsoleCommandListener(InListener); }
	virtual void AddPendingConsoleCommand(const FString& InCommand) { WrappedApplication->AddPendingConsoleCommand(InCommand); }
	virtual FPlatformRect GetWorkArea(const FPlatformRect& CurrentWindow) const { return WrappedApplication->GetWorkArea(CurrentWindow); }
	virtual bool TryCalculatePopupWindowPosition(const FPlatformRect& InAnchor, const FVector2D& InSize, const FVector2D& ProposedPlacement, const EPopUpOrientation::Type Orientation, /*OUT*/ FVector2D* const CalculatedPopUpPosition) const { return WrappedApplication->TryCalculatePopupWindowPosition(InAnchor, InSize, ProposedPlacement, Orientation, CalculatedPopUpPosition); }
	virtual void GetInitialDisplayMetrics(FDisplayMetrics& OutDisplayMetrics) const { WrappedApplication->GetInitialDisplayMetrics(OutDisplayMetrics); }
	virtual EWindowTitleAlignment::Type GetWindowTitleAlignment() const { return WrappedApplication->GetWindowTitleAlignment(); }
	virtual EWindowTransparency GetWindowTransparencySupport() const { return WrappedApplication->GetWindowTransparencySupport(); }
	virtual void DestroyApplication() { WrappedApplication->DestroyApplication(); }
	virtual IInputInterface* GetInputInterface() { return WrappedApplication->GetInputInterface(); }
	virtual ITextInputMethodSystem* GetTextInputMethodSystem() { return WrappedApplication->GetTextInputMethodSystem(); }
	virtual void SendAnalytics(IAnalyticsProvider* Provider) { WrappedApplication->SendAnalytics(Provider); }
	virtual bool SupportsSystemHelp() const { return WrappedApplication->SupportsSystemHelp(); }
	virtual void ShowSystemHelp() { WrappedApplication->ShowSystemHelp(); }
	virtual bool ApplicationLicenseValid(FPlatformUserId PlatformUser = PLATFORMUSERID_NONE) { return WrappedApplication->ApplicationLicenseValid(PlatformUser); }

	/**
	 * Functions with overridden behavior.
	 */
	virtual bool IsCursorDirectlyOverSlateWindow() const { return true; }

	TSharedPtr<GenericApplication> WrappedApplication;
};

const FVector2D FInputDevice::UnfocusedPos(-1.0f, -1.0f);

FInputDevice::FInputDevice(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler, TArray<UPixelStreamerInputComponent*>& InInputComponents)
	: PixelStreamerApplicationWrapper(MakeShareable(new FApplicationWrapper(FSlateApplication::Get().GetPlatformApplication())))
	, MessageHandler(InMessageHandler)
	, InputComponents(InInputComponents)
	, bAllowCommands(FParse::Param(FCommandLine::Get(), TEXT("AllowPixelStreamingCommands")))
	, bFakingTouchEvents(FSlateApplication::Get().IsFakingTouchEvents())
	, FocusedPos(UnfocusedPos)
	, PixelStreamingModule(FModuleManager::Get().GetModulePtr<IPixelStreamingModule>("PixelStreaming"))
{
	if (GEngine->GameViewport && !GEngine->GameViewport->HasSoftwareCursor(EMouseCursor::Default))
	{
		// Pixel streaming always requires a default software cursor as it needs
		// to be shown on the browser to allow the user to click UI elements.
		const UPixelStreamingSettings* Settings = GetDefault<UPixelStreamingSettings>();
		check(Settings);
		
		GEngine->GameViewport->AddSoftwareCursor(EMouseCursor::Default, Settings->PixelStreamerDefaultCursorClassName);
		GEngine->GameViewport->AddSoftwareCursor(EMouseCursor::TextEditBeam, Settings->PixelStreamerTextEditBeamCursorClassName);
	}
}

// XY positions are the ratio (0.0..1.0) along a viewport axis, quantized
// into an uint16 (0..65536). This allows the browser viewport and player
// viewport to have a different size.
void QuantizeAndNormalize(const FVector2D& InPos, uint16& OutX, uint16& OutY)
{
	FIntPoint SizeXY = GEngine->GameViewport->Viewport->GetSizeXY();
	OutX = InPos.X / SizeXY.X * 65536.0f;
	OutY = InPos.Y / SizeXY.Y * 65536.0f;
}

void FInputDevice::Tick(float DeltaTime)
{
	FEvent Event;
	while (Events.Dequeue(Event))
	{
		switch (Event.Event)
		{
		case EventType::UNDEFINED:
		{
			checkNoEntry();
		}
		break;
		case EventType::KEY_DOWN:
		{
			uint8 JavaScriptKeyCode;
			bool IsRepeat;
			Event.GetKeyDown(JavaScriptKeyCode, IsRepeat);
			const FKey* AgnosticKey = JavaScriptKeyCodeToFKey[JavaScriptKeyCode];
			const uint32* KeyCodePtr;
			const uint32* CharacterCodePtr;
			FInputKeyManager::Get().GetCodesFromKey(*AgnosticKey, KeyCodePtr, CharacterCodePtr);
			uint32 KeyCode = KeyCodePtr ? *KeyCodePtr : 0;
			uint32 CharacterCode = CharacterCodePtr ? *CharacterCodePtr : 0;
			MessageHandler->OnKeyDown(KeyCode, CharacterCode, IsRepeat);
			UE_LOG(PixelStreamerInputDevice, Verbose, TEXT("KEY_DOWN: KeyCode = %d; CharacterCode = %d; IsRepeat = %s"), KeyCode, CharacterCode, IsRepeat ? TEXT("True") : TEXT("False"));
		}
		break;
		case EventType::KEY_UP:
		{
			uint8 JavaScriptKeyCode;
			Event.GetKeyUp(JavaScriptKeyCode);
			const FKey* AgnosticKey = JavaScriptKeyCodeToFKey[JavaScriptKeyCode];
			const uint32* KeyCodePtr;
			const uint32* CharacterCodePtr;
			FInputKeyManager::Get().GetCodesFromKey(*AgnosticKey, KeyCodePtr, CharacterCodePtr);
			uint32 KeyCode = KeyCodePtr ? *KeyCodePtr : 0;
			uint32 CharacterCode = CharacterCodePtr ? *CharacterCodePtr : 0;
			MessageHandler->OnKeyUp(KeyCode, CharacterCode, false);   // Key up events are never repeats.
			UE_LOG(PixelStreamerInputDevice, Verbose, TEXT("KEY_UP: KeyCode = %d; CharacterCode = %d"), KeyCode, CharacterCode);
		}
		break;
		case EventType::KEY_PRESS:
		{
			TCHAR UnicodeCharacter;
			Event.GetCharacterCode(UnicodeCharacter);
			MessageHandler->OnKeyChar(UnicodeCharacter, false);   // Key press repeat not yet available but are not intrinsically used.
			UE_LOG(PixelStreamerInputDevice, Verbose, TEXT("KEY_PRESSED: Character = '%c'"), UnicodeCharacter);
		}
		break;
		case EventType::MOUSE_ENTER:
		{
			// Override application layer to special pixel streaming version.
			FSlateApplication::Get().OverridePlatformApplication(PixelStreamerApplicationWrapper);
			FSlateApplication::Get().OnCursorSet();

			// Make sure the viewport is active.
			FSlateApplication::Get().ProcessApplicationActivationEvent(true);

			UE_LOG(PixelStreamerInputDevice, Verbose, TEXT("MOUSE_ENTER"));
		}
		break;
		case EventType::MOUSE_LEAVE:
		{
			// Restore normal application layer.
			FSlateApplication::Get().OverridePlatformApplication(PixelStreamerApplicationWrapper->WrappedApplication);

			UE_LOG(PixelStreamerInputDevice, Verbose, TEXT("MOUSE_LEAVE"));
		}
		break;
		case EventType::MOUSE_MOVE:
		{
			// Ensure the cursor will be updated so it can change appearance.
			FSlateApplication::Get().OnCursorSet();

			uint16 PosX;
			uint16 PosY;
			int16 DeltaX;
			int16 DeltaY;
			Event.GetMouseDelta(PosX, PosY, DeltaX, DeltaY);
			FVector2D CursorPos = GEngine->GameViewport->GetWindow()->GetPositionInScreen() + FVector2D(PosX, PosY);
			PixelStreamerApplicationWrapper->Cursor->SetPosition(CursorPos.X, CursorPos.Y);
			MessageHandler->OnRawMouseMove(DeltaX, DeltaY);
			UE_LOG(PixelStreamerInputDevice, VeryVerbose, TEXT("MOUSE_MOVE: Pos = (%d, %d); CursorPos = (%d, %d); Delta = (%d, %d)"), PosX, PosY, static_cast<int>(CursorPos.X), static_cast<int>(CursorPos.Y), DeltaX, DeltaY);
		}
		break;
		case EventType::MOUSE_DOWN:
		{
			// If a user clicks on the application window and then clicks on the
			// browser then this will move the focus away from the application
			// window which will deactivate the application, so we need to check
			// if we must reactivate the application.
			if (!FSlateApplication::Get().IsActive())
			{
				FSlateApplication::Get().ProcessApplicationActivationEvent(true);
			}

			EMouseButtons::Type Button;
			uint16 PosX;
			uint16 PosY;
			Event.GetMouseClick(Button, PosX, PosY);
			FVector2D CursorPos = GEngine->GameViewport->GetWindow()->GetPositionInScreen() + FVector2D(PosX, PosY);
			PixelStreamerApplicationWrapper->Cursor->SetPosition(CursorPos.X, CursorPos.Y);
			MessageHandler->OnMouseDown(GEngine->GameViewport->GetWindow()->GetNativeWindow(), Button, CursorPos);
			UE_LOG(PixelStreamerInputDevice, Verbose, TEXT("MOUSE_DOWN: Button = %d; Pos = (%d, %d); CursorPos = (%d, %d)"), Button, PosX, PosY, static_cast<int>(CursorPos.X), static_cast<int>(CursorPos.Y));

			// The browser may be faking a mouse when touching so it will send
			// over a mouse down event.
			FindFocusedWidget();
		}
		break;
		case EventType::MOUSE_UP:
		{
			EMouseButtons::Type Button;
			uint16 PosX;
			uint16 PosY;
			Event.GetMouseClick(Button, PosX, PosY);
			FVector2D CursorPos = GEngine->GameViewport->GetWindow()->GetPositionInScreen() + FVector2D(PosX, PosY);
			PixelStreamerApplicationWrapper->Cursor->SetPosition(CursorPos.X, CursorPos.Y);
			MessageHandler->OnMouseUp(Button);
			UE_LOG(PixelStreamerInputDevice, Verbose, TEXT("MOUSE_UP: Button = %d; Pos = (%d, %d); CursorPos = (%d, %d)"), Button, PosX, PosY, static_cast<int>(CursorPos.X), static_cast<int>(CursorPos.Y));
		}
		break;
		case EventType::MOUSE_WHEEL:
		{
			int16 Delta;
			uint16 PosX;
			uint16 PosY;
			Event.GetMouseWheel(Delta, PosX, PosY);
			const float SpinFactor = 1 / 120.0f;
			FVector2D CursorPos = GEngine->GameViewport->GetWindow()->GetPositionInScreen() + FVector2D(PosX, PosY);
			MessageHandler->OnMouseWheel(Delta * SpinFactor, CursorPos);
			UE_LOG(PixelStreamerInputDevice, Verbose, TEXT("MOUSE_WHEEL: Delta = %d; Pos = (%d, %d); CursorPos = (%d, %d)"), Delta, PosX, PosY, static_cast<int>(CursorPos.X), static_cast<int>(CursorPos.Y));
		}
		break;
		case EventType::TOUCH_START:
		{
			uint8 TouchIndex;
			uint16 PosX;
			uint16 PosY;
			uint8 Force;   // Force is between 0.0 and 1.0 so will need to unquantize from byte.
			Event.GetTouch(TouchIndex, PosX, PosY, Force);
			FVector2D CursorPos = GEngine->GameViewport->GetWindow()->GetPositionInScreen() + FVector2D(PosX, PosY);
			MessageHandler->OnTouchStarted(GEngine->GameViewport->GetWindow()->GetNativeWindow(), CursorPos, Force / 255.0f, TouchIndex, 0);   // TODO: ControllerId?
			UE_LOG(PixelStreamerInputDevice, Verbose, TEXT("TOUCH_START: TouchIndex = %d; Pos = (%d, %d); CursorPos = (%d, %d); Force = %.3f"), TouchIndex, PosX, PosY, static_cast<int>(CursorPos.X), static_cast<int>(CursorPos.Y), Force / 255.0f);

			// If the user starts a touch then they may be focusing an editable
			// widget.
			FindFocusedWidget();
		}
		break;
		case EventType::TOUCH_END:
		{
			uint8 TouchIndex;
			uint16 PosX;
			uint16 PosY;
			uint8 Force;
			Event.GetTouch(TouchIndex, PosX, PosY, Force);
			FVector2D CursorPos = GEngine->GameViewport->GetWindow()->GetPositionInScreen() + FVector2D(PosX, PosY);
			MessageHandler->OnTouchEnded(CursorPos, TouchIndex, 0);   // TODO: ControllerId?
			UE_LOG(PixelStreamerInputDevice, Verbose, TEXT("TOUCH_END: TouchIndex = %d; Pos = (%d, %d); CursorPos = (%d, %d)"), TouchIndex, PosX, PosY, static_cast<int>(CursorPos.X), static_cast<int>(CursorPos.Y));
		}
		break;
		case EventType::TOUCH_MOVE:
		{
			uint8 TouchIndex;
			uint16 PosX;
			uint16 PosY;
			uint8 Force;   // Force is between 0.0 and 1.0 so will need to unquantize from byte.
			Event.GetTouch(TouchIndex, PosX, PosY, Force);
			FVector2D CursorPos = GEngine->GameViewport->GetWindow()->GetPositionInScreen() + FVector2D(PosX, PosY);
			MessageHandler->OnTouchMoved(CursorPos, Force / 255.0f, TouchIndex, 0);   // TODO: ControllerId?
			UE_LOG(PixelStreamerInputDevice, VeryVerbose, TEXT("TOUCH_MOVE: TouchIndex = %d; Pos = (%d, %d); CursorPos = (%d, %d); Force = %.3f"), TouchIndex, PosX, PosY, static_cast<int>(CursorPos.X), static_cast<int>(CursorPos.Y), Force / 255.0f);
		}
		break;
		default:
		{
			UE_LOG(PixelStreamerInputDevice, Error, TEXT("Unknown Pixel Streaming event %d with word 0x%016llx"), static_cast<int>(Event.Event), Event.Data.Word);
		}
		break;
		}
	}

	FString UIInteraction;
	while (UIInteractions.Dequeue(UIInteraction))
	{
		for (UPixelStreamerInputComponent* InputComponent : InputComponents)
		{
			InputComponent->OnInputEvent.Broadcast(UIInteraction);
			UE_LOG(PixelStreamerInputDevice, Verbose, TEXT("UIInteraction = %s"), *UIInteraction);
		}
	}

	FString Command;
	while (Commands.Dequeue(Command))
	{
		for (UPixelStreamerInputComponent* InputComponent : InputComponents)
		{
			if (InputComponent->OnCommand(Command))
			{
				UE_LOG(PixelStreamerInputDevice, Verbose, TEXT("Command = %s"), *Command);
			}
			else
			{
				UE_LOG(PixelStreamerInputDevice, Warning, TEXT("Failed to run Command = %s"), *Command);
			}
		}
	}
}

void FInputDevice::SendControllerEvents()
{
}

void FInputDevice::SetMessageHandler(const TSharedRef<FGenericApplicationMessageHandler>& InMessageHandler)
{
	MessageHandler = InMessageHandler;
}

bool FInputDevice::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	return true;
}

void FInputDevice::SetChannelValue(int32 ControllerId, FForceFeedbackChannelType ChannelType, float Value)
{
}

void FInputDevice::SetChannelValues(int32 ControllerId, const FForceFeedbackValues &values)
{
}

void FInputDevice::ProcessEvent(const FEvent& InEvent)
{
	bool Success = Events.Enqueue(InEvent);
	checkf(Success, TEXT("Unable to enqueue new event of type %d"), static_cast<int>(InEvent.Event));
}

void FInputDevice::ProcessUIInteraction(const FString& InDescriptor)
{
	bool Success = UIInteractions.Enqueue(InDescriptor);
	checkf(Success, TEXT("Unable to enqueue new UI Interaction %s"), *InDescriptor);
}

void FInputDevice::ProcessCommand(const FString& InDescriptor)
{
	if (bAllowCommands)
	{
		bool Success = Commands.Enqueue(InDescriptor);
		checkf(Success, TEXT("Unable to enqueue new Command %s"), *InDescriptor);
	}
}

void FInputDevice::FindFocusedWidget()
{
	FSlateApplication::Get().ForEachUser([&](FSlateUser& User) {
		TSharedPtr<SWidget> FocusedWidget = User.GetFocusedWidget();

		static FName SEditableTextType(TEXT("SEditableText"));
		static FName SMultiLineEditableTextType(TEXT("SMultiLineEditableText"));
		bool bEditable = FocusedWidget && (FocusedWidget->GetType() == SEditableTextType || FocusedWidget->GetType() == SMultiLineEditableTextType);

		// Check to see if the focus has changed.
		FVector2D Pos = bEditable ? FocusedWidget->GetCachedGeometry().GetAbsolutePosition() : UnfocusedPos;
		if (Pos != FocusedPos)
		{
			FocusedPos = Pos;

			// Tell the browser that the focus has changed.
			TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject);
			JsonObject->SetStringField(TEXT("command"), TEXT("onScreenKeyboard"));
			JsonObject->SetBoolField(TEXT("showOnScreenKeyboard"), bEditable);

			if (bEditable)
			{
				Pos = Pos - GEngine->GameViewport->GetWindow()->GetPositionInScreen();
				uint16 PosX;
				uint16 PosY;
				QuantizeAndNormalize(Pos, PosX, PosY);
				JsonObject->SetNumberField(TEXT("x"), PosX);
				JsonObject->SetNumberField(TEXT("y"), PosY);
			}

			FString Descriptor;
			TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Descriptor);
			FJsonSerializer::Serialize(JsonObject.ToSharedRef(), JsonWriter);

			PixelStreamingModule->SendCommand(Descriptor);
		}
	});
}

// player message handling

namespace
{
	template<typename T>
	const T& Get(const uint8*& Data, uint32& Size)
	{
		checkf(sizeof(T) <= Size, TEXT("%d - %d"), sizeof(T), Size);
		const T& Value = *reinterpret_cast<const T*>(Data);
		Data += sizeof(T);
		Size -= sizeof(T);
		return Value;
	}

#define GET(Type, Var) Type Var = Get<Type>(Data, Size)

	FString GetString(const uint8*& Data, uint32& Size)
	{
		GET(uint16, StrLen);
		checkf(StrLen * sizeof(TCHAR) <= Size, TEXT("%d - %d"), StrLen, Size);
		FString Res;
		Res.GetCharArray().SetNumUninitialized(StrLen + 1);
		FMemory::Memcpy(Res.GetCharArray().GetData(), Data, StrLen * sizeof(TCHAR));
		Res.GetCharArray()[StrLen] = '\0';
		Data += StrLen * sizeof(TCHAR);
		Size -= StrLen * sizeof(TCHAR);
		return Res;
	}

	// XY positions are the ratio (0.0..1.0) along a viewport axis, quantized
	// into an uint16 (0..65536). This allows the browser viewport and player
	// viewport to have a different size.
	void UnquantizeAndDenormalize(uint16& InOutX, uint16& InOutY)
	{
		checkf(GEngine->GameViewport != nullptr, TEXT("PixelStreaming is not supported in editor, and should have been disabled earlier"));
		FIntPoint SizeXY = GEngine->GameViewport->Viewport->GetSizeXY();
		InOutX = InOutX / 65536.0f * SizeXY.X;
		InOutY = InOutY / 65536.0f * SizeXY.Y;
	}

	// XY deltas are the ratio (-1.0..1.0) along a viewport axis, quantized
	// into an int16 (-32767..32767). This allows the browser viewport and
	// player viewport to have a different size.
	void UnquantizeAndDenormalize(int16& InOutX, int16& InOutY)
	{
		FIntPoint SizeXY = GEngine->GameViewport->Viewport->GetSizeXY();
		InOutX = InOutX / 32767.0f * SizeXY.X;
		InOutY = InOutY / 32767.0f * SizeXY.Y;
	}

	/**
	* A touch is a specific finger placed on the canvas as a specific position.
	*/
	struct FTouch
	{
		uint16 PosX;   // X position of finger.
		uint16 PosY;   // Y position of finger.
		uint8 TouchIndex;   // Index of finger for tracking multi-touch events.
		uint8 Force;   // Amount of pressure being applied by the finger.
	};

	using FKeyCodeType = uint8;
	using FCharacterType = TCHAR;
	using FRepeatType = uint8;
	using FButtonType = uint8;
	using FPosType = uint16;
	using FDeltaType = int16;
	using FTouchesType = TArray<FTouch>;

	/**
	* Get the array of touch positions and touch indices for a touch event,
	* consumed from the receive buffer.
	* @param Consumed - The number of bytes consumed from the receive buffer.
	* @param OutTouches - The array of touches.
	* @return False if there insufficient room in the receive buffer to read the entire event.
	*/
	FTouchesType GetTouches(const uint8*& Data, uint32& Size)
	{
		// Get the number of touches in the array.
		GET(uint8, NumTouches);

		FTouchesType Touches;
		// Get the value of each touch position and then the touch index.
		for (int Touch = 0; Touch < NumTouches; Touch++)
		{
			GET(FPosType, PosX);
			GET(FPosType, PosY);
			UnquantizeAndDenormalize(PosX, PosY);
			GET(uint8, TouchIndex);
			GET(uint8, Force);
			Touches.Add({ PosX, PosY, TouchIndex, Force });
		}

		return Touches;
	}
	
	/**
	* Convert the given array of touches to a friendly string for logging.
	* @param InTouches - The array of touches.
	* @return The string representation of the array.
	*/
	FString TouchesToString(const FTouchesType& InTouches)
	{
		FString String;
		for (const FTouch& Touch : InTouches)
		{
			String += FString::Printf(TEXT("F[%d]=(%d, %d)(%.3f)"), Touch.TouchIndex, Touch.PosX, Touch.PosY, Touch.Force / 255.0f);
		}
		return String;
	}

	enum class KeyState { Alt = 1 << 0, Ctrl = 1 << 1, Shift = 1 << 2 };
	enum class MouseButtonState { Left = 1 << 0, Right = 1 << 1, Middle = 1 << 2, Button4 = 1 << 3, Button5 = 1 << 4, Button6 = 1 << 5, Button7 = 1 << 6, Button8 = 1 << 7 };
}

void FInputDevice::OnMessage(const uint8* Data, uint32 Size)
{
	using namespace PixelStreamingProtocol;

	GET(EToStreamerMsg, MsgType);

	switch (MsgType)
	{
	case EToStreamerMsg::UIInteraction:
	{
		FString Descriptor = GetString(Data, Size);
		checkf(Size == 0, TEXT("%d, %d"), Size, Descriptor.Len());
		UE_LOG(PixelStreamerInput, Verbose, TEXT("UIInteraction: %s"), *Descriptor);
		ProcessUIInteraction(Descriptor);
		break;
	}
	case EToStreamerMsg::Command:
	{
		FString Descriptor = GetString(Data, Size);
		checkf(Size == 0, TEXT("%d, %d"), Size, Descriptor.Len());
		UE_LOG(PixelStreamerInput, Verbose, TEXT("Command: %s"), *Descriptor);
		ProcessCommand(Descriptor);
	}
	case EToStreamerMsg::KeyDown:
	{
		GET(FKeyCodeType, KeyCode);
		GET(FRepeatType, Repeat);
		checkf(Size == 0, TEXT("%d"), Size);
		UE_LOG(PixelStreamerInput, Verbose, TEXT("key down: %d, repeat: %d"), KeyCode, Repeat);

		FEvent KeyDownEvent(EventType::KEY_DOWN);
		KeyDownEvent.SetKeyDown(KeyCode, Repeat != 0);
		ProcessEvent(KeyDownEvent);
		break;
	}
	case EToStreamerMsg::KeyUp:
	{
		GET(FKeyCodeType, KeyCode);
		checkf(Size == 0, TEXT("%d"), Size);
		UE_LOG(PixelStreamerInput, Verbose, TEXT("key up: %d"), KeyCode);

		FEvent KeyUpEvent(EventType::KEY_UP);
		KeyUpEvent.SetKeyUp(KeyCode);
		ProcessEvent(KeyUpEvent);
		break;
	}
	case EToStreamerMsg::KeyPress:
	{
		GET(FCharacterType, Character);
		checkf(Size == 0, TEXT("%d"), Size);
		UE_LOG(PixelStreamerInput, Verbose, TEXT("key press: '%c'"), Character);

		FEvent KeyPressEvent(EventType::KEY_PRESS);
		KeyPressEvent.SetCharCode(Character);
		ProcessEvent(KeyPressEvent);		
		break;
	}
	case EToStreamerMsg::MouseEnter:
	{
		checkf(Size == 0, TEXT("%d"), Size);
		UE_LOG(PixelStreamerInput, Verbose, TEXT("mouseEnter"));
		ProcessEvent(FEvent(FInputDevice::EventType::MOUSE_ENTER));
		break;
	}
	case EToStreamerMsg::MouseLeave:
	{
		checkf(Size == 0, TEXT("%d"), Size);
		ProcessEvent(FEvent(FInputDevice::EventType::MOUSE_LEAVE));
		UE_LOG(PixelStreamerInput, Verbose, TEXT("mouseLeave"));
		break;
	}
	case EToStreamerMsg::MouseDown:
	{
		GET(FButtonType, Button);
		GET(FPosType, PosX);
		GET(FPosType, PosY);
		checkf(Size == 0, TEXT("%d"), Size);
		UE_LOG(PixelStreamerInput, Verbose, TEXT("mouseDown at (%d, %d), button %d"), PosX, PosY, Button);

		UnquantizeAndDenormalize(PosX, PosY);

		FEvent MouseDownEvent(EventType::MOUSE_DOWN);
		MouseDownEvent.SetMouseClick(Button, PosX, PosY);
		ProcessEvent(MouseDownEvent);
		break;
	}
	case EToStreamerMsg::MouseUp:
	{
		GET(FButtonType, Button);
		GET(FPosType, PosX);
		GET(FPosType, PosY);
		checkf(Size == 0, TEXT("%d"), Size);
		UE_LOG(PixelStreamerInput, Verbose, TEXT("mouseUp at (%d, %d), button %d"), PosX, PosY, Button);

		UnquantizeAndDenormalize(PosX, PosY);

		FEvent MouseDownEvent(EventType::MOUSE_UP);
		MouseDownEvent.SetMouseClick(Button, PosX, PosY);
		ProcessEvent(MouseDownEvent);		
		break;
	}
	case EToStreamerMsg::MouseMove:
	{
		GET(FPosType, PosX);
		GET(FPosType, PosY);
		GET(FDeltaType, DeltaX);
		GET(FDeltaType, DeltaY);
		checkf(Size == 0, TEXT("%d"), Size);
		UE_LOG(PixelStreamerInput, Verbose, TEXT("mouseMove to (%d, %d), delta (%d, %d)"), PosX, PosY, DeltaX, DeltaY);

		UnquantizeAndDenormalize(PosX, PosY);
		UnquantizeAndDenormalize(DeltaX, DeltaY);

		FEvent MouseMoveEvent(EventType::MOUSE_MOVE);
		MouseMoveEvent.SetMouseDelta(PosX, PosY, DeltaX, DeltaY);
		ProcessEvent(MouseMoveEvent);
		break;
	}
	case EToStreamerMsg::MouseWheel:
	{
		GET(FDeltaType, Delta);
		GET(FPosType, PosX);
		GET(FPosType, PosY);
		checkf(Size == 0, TEXT("%d"), Size);
		UE_LOG(PixelStreamerInput, Verbose, TEXT("mouseWheel, delta %d"), Delta);

		UnquantizeAndDenormalize(PosX, PosY);

		FEvent MouseWheelEvent(EventType::MOUSE_WHEEL);
		MouseWheelEvent.SetMouseWheel(Delta, PosX, PosY);
		ProcessEvent(MouseWheelEvent);
		break;
	}
	case EToStreamerMsg::TouchStart:
	{
		FTouchesType Touches = GetTouches(Data, Size);
		checkf(Size == 0, TEXT("%d"), Size);
		UE_LOG(PixelStreamerInput, Verbose, TEXT("TouchStart: %s"), *TouchesToString(Touches));

		for (const FTouch& Touch : Touches)
		{
			FEvent TouchStartEvent(EventType::TOUCH_START);
			TouchStartEvent.SetTouch(Touch.TouchIndex, Touch.PosX, Touch.PosY, Touch.Force);
			ProcessEvent(TouchStartEvent);
		}
		break;
	}
	case EToStreamerMsg::TouchEnd:
	{
		FTouchesType Touches = GetTouches(Data, Size);
		checkf(Size == 0, TEXT("%d"), Size);
		UE_LOG(PixelStreamerInput, Verbose, TEXT("TouchEnd: %s"), *TouchesToString(Touches));

		for (const FTouch& Touch : Touches)
		{
			FEvent TouchEndEvent(EventType::TOUCH_END);
			TouchEndEvent.SetTouch(Touch.TouchIndex, Touch.PosX, Touch.PosY, Touch.Force);
			ProcessEvent(TouchEndEvent);
		}
		break;
	}
	case EToStreamerMsg::TouchMove:
	{
		FTouchesType Touches = GetTouches(Data, Size);
		checkf(Size == 0, TEXT("%d"), Size);
		UE_LOG(PixelStreamerInput, Verbose, TEXT("TouchMove: %s"), *TouchesToString(Touches));

		for (const FTouch& Touch : Touches)
		{
			FEvent TouchMoveEvent(EventType::TOUCH_MOVE);
			TouchMoveEvent.SetTouch(Touch.TouchIndex, Touch.PosX, Touch.PosY, Touch.Force);
			ProcessEvent(TouchMoveEvent);
		}
		break;
	}
	}
}

#undef GET

void FInputDevice::FEvent::GetMouseClick(EMouseButtons::Type& OutButton, uint16& OutPosX, uint16& OutPosY)
{
	check(Event == EventType::MOUSE_DOWN || Event == EventType::MOUSE_UP);
	// https://developer.mozilla.org/en-US/docs/Web/Events/mousedown
	uint8 Button = Data.MouseButton.Button;
	switch (Button)
	{
	case 0:
	{
		OutButton = EMouseButtons::Left;
	}
	break;
	case 1:
	{
		OutButton = EMouseButtons::Middle;
	}
	break;
	case 2:
	{
		OutButton = EMouseButtons::Right;
	}
	break;
	case 3:
	{
		OutButton = EMouseButtons::Thumb01;
	}
	break;
	case 4:
	{
		OutButton = EMouseButtons::Thumb02;
	}
	break;
	default:
	{
		UE_LOG(PixelStreamerInputDevice, Error, TEXT("Unknown Pixel Streaming mouse click with button %d and word 0x%016llx"), Button, Data.Word);
	}
	break;
	}
	OutPosX = Data.MouseButton.PosX;
	OutPosY = Data.MouseButton.PosY;
}
