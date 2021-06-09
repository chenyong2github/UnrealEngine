// Copyright Epic Games, Inc. All Rights Reserved.

#include "SlateScreenReaderEngineSubsystem.h"
#include "GenericPlatform/ScreenReaderBase.h"
#include "Application/SlateApplicationBase.h"
#include "GenericPlatform/IScreenReaderBuilder.h"
#include "SlateScreenReaderModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/Engine.h"


USlateScreenReaderEngineSubsystem::USlateScreenReaderEngineSubsystem()
{

}

USlateScreenReaderEngineSubsystem::~USlateScreenReaderEngineSubsystem()
{
	ensureMsgf(!ScreenReader, TEXT("Screen reader should already be destroyed and nulled at this point."));
}

USlateScreenReaderEngineSubsystem& USlateScreenReaderEngineSubsystem::Get()
{
	return *GEngine->GetEngineSubsystem<USlateScreenReaderEngineSubsystem>();
}

void USlateScreenReaderEngineSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// By the time this is called, Slate is all set up
	// I.e Platform applications already have subscribed to the delegate in the accessible
	// message handler. So any platform stuff e.g OSX trees and cache are all set up.  That needs to get nuked somehow when we subscribe to accessible events here
	FSlateApplication& SlateApplication = FSlateApplication::Get();
	SlateApplication.GetPlatformApplication()->GetAccessibleMessageHandler()->UnbindAccessibleEventDelegate();
	// @TODOAccessibility: We've unbound from the event, but we need to clean up the OS resources that are allocated for accessibility as well. 

	// @TODOAccessibility: Consider lazy initialization 
	IScreenReaderBuilder::FArgs Args(SlateApplication.GetPlatformApplication().ToSharedRef());
	// @TODOAccessibility: Allow a means of using custom screen reader builder 
	ScreenReader = ISlateScreenReaderModule::Get().GetDefaultScreenReaderBuilder()->Create(Args);
	// Slate could get shutdown prior to engine subsystems 
	SlateApplication.OnPreShutdown().AddSP(ScreenReader.ToSharedRef(), &FScreenReaderBase::Deactivate);
}

void USlateScreenReaderEngineSubsystem::Deinitialize()
{
	// should still be valid
	check(ScreenReader);
	// Engine subsystems are desroyed before FSlateApplication::Shutdown is called
	// We clean up here 
	if (ScreenReader->IsActive())
	{
		ScreenReader->Deactivate();
	}
	ScreenReader.Reset();
	Super::Deinitialize();
}

bool USlateScreenReaderEngineSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// On certain builds, Slate is not initialize, we don't need the screen reader in that case 
	return FSlateApplication::IsInitialized();
}

void USlateScreenReaderEngineSubsystem::Activate()
{
	if (!ScreenReader->IsActive())
	{
		ScreenReader->Activate();
	}
}

void USlateScreenReaderEngineSubsystem::Deactivate()
{
	checkf(ScreenReader, TEXT("Invalid screen reader on deactivation. A valid screen reader must always be available for deactivation."));
	if (ScreenReader->IsActive())
	{
		ScreenReader->Deactivate();
	}
}

TSharedRef<FScreenReaderBase> USlateScreenReaderEngineSubsystem::GetScreenReader() const
{
	return ScreenReader.ToSharedRef();
}