// Copyright Epic Games, Inc. All Rights Reserved.

#include "SwitchboardEditorSettings.h"
#include "GenericPlatform/GenericPlatformProperties.h"
#include "Misc/Paths.h"


namespace UE::Switchboard::Private
{

TTuple<FString, FString> PlatformAndExtension()
{
#if PLATFORM_WINDOWS
	FString PlatformName = TEXT("Win64");
	FString ExeExt = TEXT(".exe");
#elif PLATFORM_LINUX
	FString ExeExt = TEXT("");
	FString PlatformName = TEXT("Linux");
#endif
	return TTuple<FString,FString>{MoveTemp(PlatformName), MoveTemp(ExeExt)};
}

template <typename... TPaths>
FString ConcatPaths(FString BaseDir, TPaths... InPaths)
{
	FString BaseConverted = FPaths::ConvertRelativePathToFull(BaseDir);
	BaseConverted /= ( InPaths / ... );
	return BaseConverted;
}

FString DefaultListenerPath()
{
	const auto [PlatformName, ExeExt] = UE::Switchboard::Private::PlatformAndExtension();

	return ConcatPaths(FPaths::EngineDir(),
					   FString(TEXT("Binaries")),
					   PlatformName,
					   FString(TEXT("SwitchboardListener") + ExeExt));
}

}

USwitchboardEditorSettings::USwitchboardEditorSettings()
{
	using namespace UE::Switchboard::Private;
	SwitchboardPath = {ConcatPaths(FPaths::EnginePluginsDir() + FString(TEXT("VirtualProduction")),
								   FString(TEXT("Switchboard")),
								   FString(TEXT("Source")),
								   FString(TEXT("Switchboard")))};

	ListenerPath = {DefaultListenerPath()};
}

USwitchboardEditorSettings* USwitchboardEditorSettings::GetSwitchboardEditorSettings()
{
	return GetMutableDefault<USwitchboardEditorSettings>();
}
