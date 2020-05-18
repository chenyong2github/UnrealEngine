// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using UnrealBuildTool;
using AutomationTool;


namespace Turnkey.Commands
{
	class ListSdks : TurnkeyCommand
	{
		protected override void Execute(string[] CommandOptions)
		{
			TurnkeyUtils.Log("");
			TurnkeyUtils.Log("Available Installers:");
			foreach (SdkInfo Sdk in TurnkeyManifest.GetDiscoveredSdks())
			{
				TurnkeyUtils.Log(Sdk.ToString(2));
			}
		}
	}
}
