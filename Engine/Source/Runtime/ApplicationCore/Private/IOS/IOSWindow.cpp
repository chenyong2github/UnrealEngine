// Copyright Epic Games, Inc. All Rights Reserved.

#include "IOSWindow.h"
#include "IOS/IOSAppDelegate.h"
#include "IOS/IOSView.h"

FIOSWindow::~FIOSWindow()
{
	// NOTE: The Window is invalid here!
	//       Use NativeWindow_Destroy() instead.
}

TSharedRef<FIOSWindow> FIOSWindow::Make()
{
	return MakeShareable( new FIOSWindow() );
}

FIOSWindow::FIOSWindow()
{
}

void FIOSWindow::Initialize( class FIOSApplication* const Application, const TSharedRef< FGenericWindowDefinition >& InDefinition, const TSharedPtr< FIOSWindow >& InParent, const bool bShowImmediately )
{
	OwningApplication = Application;
	Definition = InDefinition;

	Window = [[UIApplication sharedApplication] keyWindow];

#if !PLATFORM_TVOS
	if(InParent.Get() != NULL)
	{
		dispatch_async(dispatch_get_main_queue(),^ {
			if ([UIAlertController class])
			{
				UIAlertController* AlertController = [UIAlertController alertControllerWithTitle:@""
														message:@"Error: Only one UIWindow may be created on iOS."
														preferredStyle:UIAlertControllerStyleAlert];
				UIAlertAction* okAction = [UIAlertAction
											actionWithTitle:NSLocalizedString(@"OK", nil)
											style:UIAlertActionStyleDefault
											handler:^(UIAlertAction* action)
											{
												[AlertController dismissViewControllerAnimated : YES completion : nil];
											}
				];

				[AlertController addAction : okAction];
				[[IOSAppDelegate GetDelegate].IOSController presentViewController : AlertController animated : YES completion : nil];
			}
		} );
	}
#endif
}

FPlatformRect FIOSWindow::GetScreenRect()
{
	// get the main view's frame
	IOSAppDelegate* AppDelegate = [IOSAppDelegate GetDelegate];
	UIView* View = AppDelegate.IOSView;
	CGRect Frame = [View frame];
	CGFloat Scale = View.contentScaleFactor;

	FPlatformRect ScreenRect;
	ScreenRect.Top = Frame.origin.y * Scale;
	ScreenRect.Bottom = (Frame.origin.y + Frame.size.height) * Scale;
	ScreenRect.Left = Frame.origin.x * Scale;
	ScreenRect.Right = (Frame.origin.x + Frame.size.width) * Scale;

	return ScreenRect;
}

FPlatformRect FIOSWindow::GetUIWindowRect()
{
	// get the main window's bounds
	IOSAppDelegate* AppDelegate = [IOSAppDelegate GetDelegate];
	UIWindow* Window = AppDelegate.Window;
	CGRect Bounds = [Window bounds];

	FPlatformRect WindowRect;
	WindowRect.Top = Bounds.origin.y;
	WindowRect.Bottom = Bounds.origin.y + Bounds.size.height;
	WindowRect.Left = Bounds.origin.x;
	WindowRect.Right = Bounds.origin.x + Bounds.size.width;

	return WindowRect;
}


bool FIOSWindow::GetFullScreenInfo( int32& X, int32& Y, int32& Width, int32& Height ) const
{
	FPlatformRect ScreenRect = GetScreenRect();

	X = ScreenRect.Left;
	Y = ScreenRect.Top;
	Width = ScreenRect.Right - ScreenRect.Left;
	Height = ScreenRect.Bottom - ScreenRect.Top;

	return true;
}
