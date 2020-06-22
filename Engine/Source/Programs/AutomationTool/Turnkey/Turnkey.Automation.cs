// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using Tools.DotNETCommon;
using UnrealBuildTool;
using AutomationTool;
using System.Threading;

namespace Turnkey
{
	class Turnkey : BuildCommand
	{
		public override ExitCode Execute()
		{
			// 			Thread t = new Thread(Turnkey.ThreadProc);
			// 			Console.WriteLine("Before setting apartment state: {0}",
			// 				t.GetApartmentState());
			// 
			// 			t.SetApartmentState(ApartmentState.STA);
			// 			Console.WriteLine("After setting apartment state: {0}",
			// 				t.GetApartmentState());
			// 
			// 			t.Start(this);
			// 			t.Join();

			ThreadProc(this);
			return TurnkeyUtils.ExitCode;
		}

		private static void ThreadProc(object Data)
		{
			BuildCommand Build = (BuildCommand)Data;

			IOProvider IOProvider;
			if (Build.ParseParam("EditorIO"))
			{
				IOProvider = new HybridIOProvider();
			}
			else
			{
				string ReportFilename = Build.ParseParamValue("ReportFilename");
				if (!string.IsNullOrEmpty(ReportFilename))
				{
					IOProvider = new ReportIOProvider(ReportFilename);
				}
				else
				{
					IOProvider = new ConsoleIOProvider();
				}
			}

			Turnkey.Execute(IOProvider, Build);
		}


		public static AutomationTool.ExitCode Execute(IOProvider IOProvider, BuildCommand CommandUtilHelper)
		{
			TurnkeyUtils.Initialize(IOProvider, CommandUtilHelper);
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
				TurnkeyUtils.ExitCode = ExitCode.Success;
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
			catch (Exception Ex)
			{
				TurnkeyUtils.Log("Turnkey exception: {0}", Ex.ToString());
			}
			finally
			{
				TurnkeyUtils.CleanupPaths();
			}

			return TurnkeyUtils.ExitCode;
		}
	}
}
