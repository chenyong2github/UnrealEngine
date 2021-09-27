// Copyright Epic Games, Inc. All Rights Reserved.
using System;
using System.Collections.Generic;
using System.Linq;
using AutomationTool;

public class WindowsCookedEditor : Win64Platform
{
	public override string GetCookPlatform(bool bDedicatedServer, bool bIsClientOnly)
	{
		return "WindowsCookedEditor";
	}
	public override TargetPlatformDescriptor GetTargetPlatformDescriptor()
	{
		return new TargetPlatformDescriptor(TargetPlatformType, "CookedEditor");
	}
}

public class LinuxCookedEditor : GenericLinuxPlatform
{
	public override string GetCookPlatform(bool bDedicatedServer, bool bIsClientOnly)
	{
		return "LinuxCookedEditor";
	}
	public override TargetPlatformDescriptor GetTargetPlatformDescriptor()
	{
		return new TargetPlatformDescriptor(TargetPlatformType, "CookedEditor");
	}

}

public class MacCookedEditor : MacPlatform
{
	public override string GetCookPlatform(bool bDedicatedServer, bool bIsClientOnly)
	{
		return "MacCookedEditor";
	}
	public override TargetPlatformDescriptor GetTargetPlatformDescriptor()
	{
		return new TargetPlatformDescriptor(TargetPlatformType, "CookedEditor");
	}

}

