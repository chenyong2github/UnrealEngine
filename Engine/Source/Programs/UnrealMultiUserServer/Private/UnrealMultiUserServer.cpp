// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ConcertSettings.h"
#include "ConcertSyncServerLoop.h"

#include "RequiredProgramMainCPPInclude.h"

IMPLEMENT_APPLICATION(UnrealMultiUserServer, "UnrealMultiUserServer");

int32 RunUnrealMutilUserServer(int ArgC, TCHAR* ArgV[])
{
	FString Role(TEXT("MultiUser"));
	FConcertSyncServerLoopInitArgs ServerLoopInitArgs;
	ServerLoopInitArgs.SessionFlags = EConcertSyncSessionFlags::Default_MultiUserSession;
	ServerLoopInitArgs.ServiceRole = Role;
	ServerLoopInitArgs.ServiceFriendlyName = TEXT("Multi-User Editing Server");

	ServerLoopInitArgs.GetServerConfigFunc = [Role]() -> const UConcertServerConfig*
	{
		UConcertServerConfig* ServerConfig = IConcertSyncServerModule::Get().ParseServerSettings(FCommandLine::Get());
		if (ServerConfig->WorkingDir.IsEmpty())
		{
			ServerConfig->WorkingDir = FPaths::ProjectIntermediateDir() / Role;
		}
		if (ServerConfig->ArchiveDir.IsEmpty())
		{
			ServerConfig->ArchiveDir = FPaths::ProjectSavedDir() / Role;
		}
		return ServerConfig;
	};

	return ConcertSyncServerLoop(ArgC, ArgV, ServerLoopInitArgs);
}


#if PLATFORM_MAC // On Mac, to get a properly logging console that play nice, we need to build a mac application (.app) rather than a console application.

class CommandLineArguments
{
public:
	CommandLineArguments() : ArgC(0), ArgV(nullptr) {}
	CommandLineArguments(int InArgC, char* InUtf8ArgV[]) { Init(InArgC, InUtf8ArgV); }

	void Init(int InArgC, char* InUtf8ArgV[])
	{
		ArgC = InArgC;
		ArgV = new TCHAR*[ArgC];
		for (int32 a = 0; a < ArgC; a++)
		{
			FUTF8ToTCHAR ConvertFromUtf8(InUtf8ArgV[a]);
			ArgV[a] = new TCHAR[ConvertFromUtf8.Length() + 1];
			FCString::Strcpy(ArgV[a], ConvertFromUtf8.Length() + 1, ConvertFromUtf8.Get());
		}
	}

	~CommandLineArguments()
	{
		for (int32 a = 0; a < ArgC; a++)
		{
			delete[] ArgV[a];
		}
		delete[] ArgV;
	}

	int ArgC;
	TCHAR** ArgV;
};

#include "Mac/CocoaThread.h"

static CommandLineArguments GSavedCommandLine;

@interface UE4AppDelegate : NSObject <NSApplicationDelegate, NSFileManagerDelegate>
{
}

@end

@implementation UE4AppDelegate

//handler for the quit apple event used by the Dock menu
- (void)handleQuitEvent:(NSAppleEventDescriptor*)Event withReplyEvent:(NSAppleEventDescriptor*)ReplyEvent
{
	[NSApp terminate:self];
}

- (void) runGameThread:(id)Arg
{
	FPlatformMisc::SetGracefulTerminationHandler();
	FPlatformMisc::SetCrashHandler(nullptr);
	
	RunUnrealMutilUserServer(GSavedCommandLine.ArgC, GSavedCommandLine.ArgV);
	
	[NSApp terminate: self];
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)Sender;
{
	if(!IsEngineExitRequested() || ([NSThread gameThread] && [NSThread gameThread] != [NSThread mainThread]))
	{
		RequestEngineExit(TEXT("UnrealMultiUserServer Requesting Exist"));
		return NSTerminateLater;
	}
	else
	{
		return NSTerminateNow;
	}
}

- (void)applicationDidFinishLaunching:(NSNotification *)Notification
{
	//install the custom quit event handler
	NSAppleEventManager* appleEventManager = [NSAppleEventManager sharedAppleEventManager];
	[appleEventManager setEventHandler:self andSelector:@selector(handleQuitEvent:withReplyEvent:) forEventClass:kCoreEventClass andEventID:kAEQuitApplication];

	// Add a menu bar to the application.
	id menubar = [[NSMenu new] autorelease];
	id appMenuItem = [[NSMenuItem new] autorelease];
	[menubar addItem:appMenuItem];
	[NSApp setMainMenu:menubar];

	// Populate the menu bar.
	id appMenu = [[NSMenu new] autorelease];
	id quitMenuItem = [[[NSMenuItem alloc] initWithTitle:NSLOCTEXT("UMUS_Quit", "QuitApp", "Quit").ToString().GetNSString() action:@selector(terminate:) keyEquivalent:@"q"] autorelease];
	[appMenu addItem:quitMenuItem];
	[appMenuItem setSubmenu:appMenu];

	RunGameThread(self, @selector(runGameThread:));
}

int main(int argc, char *argv[])
{
	// Record the command line.
	GSavedCommandLine.Init(argc, argv);

	// Launch the application.
	SCOPED_AUTORELEASE_POOL;
	[NSApplication sharedApplication];
	[NSApp setDelegate:[UE4AppDelegate new]];
	[NSApp run];
	return 0;
}

@end

#else // Windows/Linux

INT32_MAIN_INT32_ARGC_TCHAR_ARGV()
{
	return RunUnrealMutilUserServer(ArgC, ArgV);
}

#endif
