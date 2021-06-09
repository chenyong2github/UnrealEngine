// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Subsystems/EngineSubsystem.h"
#include "SlateScreenReaderEngineSubsystem.generated.h"

class FScreenReaderBase;
/**
* The engine susbsystem for the Slate screen reader.
* This class should be the entryway for C++ programmers and BP useres alike to interact with the screen reader system.
* The subsystem must be activated before the screen reader services can be used.
* For C++ users, please retrieve the screen reader and interact with the screen reader users from there.
* Example:
* USlateScreenReaderEngineSubsystem ::Get().Activate();
* // Registers a screen reader user with Id 0. A screen reader user should correspond to a hardware input device such as a keyboard or controller like FSlateUser
* USlateScreenReaderEngineSubsystem ::Get().GetScreenReader()->RegisterUser(0);
* TSharedRef<FScreenReaderUser> User = USlateScreenReaderEngineSubsystem::Get().GetScreenReader()->GetUser(0);
* // Screen reader users are inactive when they are first registered and need to be explicitly activated.
* User->Activate();
* static const FText HelloWorld = LOCTEXT("HelloWorld", "Hello World");
* // Requests "Hello World" to be spoken to the screen reader user
* User->RequestSpeak(FScreenReaderAnnouncement(HelloWorld.ToString(), FScreenReaderInfo::Important())); 
* @see FScreenReaderBase, FScreenReaderUser, FScreenReaderAnnouncement
*/
UCLASS()
class SLATESCREENREADER_API USlateScreenReaderEngineSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()
public:
	USlateScreenReaderEngineSubsystem();
	virtual ~USlateScreenReaderEngineSubsystem();

	/** Convenience method to retrieve the screen reader engine subsystem. */
	static USlateScreenReaderEngineSubsystem& Get();
	
	/** 
	* Activates the underlying screen reader. Use this to allow end users to register with the screen reader 
	* and receive accessible feedback via text to speech and get access to other screen reader services.
	*/
	UFUNCTION(BlueprintCallable, Category="ScreenReader")
	void Activate();
	/**
	* Deactivates the underlying screen reader and prevents end users from getting
	* any accessible feedback via text to speech or using any other screen reader services.
	*/
	UFUNCTION(BlueprintCallable, Category="ScreenReader")
	void Deactivate();
	/** Returns the underlying screen reader */
	TSharedRef<FScreenReaderBase> GetScreenReader() const;

// UEngineSubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	// ~
private:
	/** 
	* The underlying screen reader. This should be a TUniquePtr
	* but we make it a TSharedPtr to allow for easy delegate unbinding without needing to manually unbind from delegates.
	*/
	TSharedPtr<FScreenReaderBase> ScreenReader;
};
