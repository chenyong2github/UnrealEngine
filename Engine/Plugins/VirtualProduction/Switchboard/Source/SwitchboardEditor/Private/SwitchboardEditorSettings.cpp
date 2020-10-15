// Copyright Epic Games, Inc. All Rights Reserved.

#include "SwitchboardEditorSettings.h"
#include "GenericPlatform/GenericPlatformProperties.h"
#include "Misc/Paths.h"

USwitchboardEditorSettings::USwitchboardEditorSettings()
{
	FString DefaultSwitchboardPath = FPaths::ConvertRelativePathToFull(FPaths::EnginePluginsDir() + FString(TEXT("VirtualProduction")));
	DefaultSwitchboardPath /= FString(TEXT("Switchboard")) / FString(TEXT("Source")) / FString(TEXT("Switchboard"));
	SwitchboardPath = { DefaultSwitchboardPath };

#if PLATFORM_WINDOWS
	const FString PlatformName = TEXT("Win64");
	const FString ExeExt = TEXT(".exe");
#elif PLATFORM_LINUX
	const FString ExeExt = TEXT("");
	const FString PlatformName = TEXT("Linux");
#endif

	FString DefaultListenerPath = FPaths::ConvertRelativePathToFull(FPaths::EngineDir());
	DefaultListenerPath /= FString(TEXT("Binaries")) / PlatformName;
	DefaultListenerPath /= (TEXT("SwitchboardListener") + ExeExt);
	ListenerPath = { DefaultListenerPath };
}

USwitchboardEditorSettings* USwitchboardEditorSettings::GetSwitchboardEditorSettings()
{
	return GetMutableDefault<USwitchboardEditorSettings>();
}
