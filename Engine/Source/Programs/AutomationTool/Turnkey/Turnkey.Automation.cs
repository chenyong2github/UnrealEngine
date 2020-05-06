
// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using Tools.DotNETCommon;
using UnrealBuildTool;
using AutomationTool;

namespace Turnkey
{
	class Turnkey : BuildCommand
	{
		public override void ExecuteBuild()
		{
			TurnkeyUtils.Initialize(new ConsoleIOProvider(), this);
			TurnkeySettings.Initialize();

			// cache some settings for other classes
			TurnkeyUtils.SetVariable("EngineDir", EngineDirectory.FullName);

			//			Microsoft.Win32.SystemEvents.UserPreferenceChanged += new Microsoft.Win32.UserPreferenceChangedEventHandler((sender, args) => TurnkeyUtils.Log("Got a change! {0}", args.Category));

			Console.CancelKeyPress += delegate
			{
				TurnkeyUtils.CleanupPaths();

				TurnkeyUtils.Log("");
				TurnkeyUtils.Log("If you installed an SDK, you should NOT \"Terminate batch job\"!");
				TurnkeyUtils.Log("");
				Environment.ExitCode = 0;
			};

			try
			{
				// supplied command
				string SuppliedCommand = TurnkeyUtils.CommandUtilHelper.ParseParamValue("Command", null);
				if (SuppliedCommand != null)
				{
					TurnkeyCommand.ExecuteCommand(SuppliedCommand);
				}
				else
				{
					// no command will prompt
					while (TurnkeyCommand.ExecuteCommand())
					{
						TurnkeyUtils.Log("");
					}
				}
			}
			catch (Exception)
			{

			}
			finally
			{
				TurnkeyUtils.CleanupPaths();
			}

			if (Environment.ExitCode != 0)
			{
				// @todo turnkey - would be nice to return a failure, without an exception in UAT, which looks violent
				AutomationTool.ExitCode ExitCode = (AutomationTool.ExitCode)Environment.ExitCode;
				throw new AutomationException(ExitCode, ExitCode.ToString());
			}
		}
	}
}
