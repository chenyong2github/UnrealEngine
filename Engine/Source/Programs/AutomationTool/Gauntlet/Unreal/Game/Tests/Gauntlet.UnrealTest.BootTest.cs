// Copyright Epic Games, Inc. All Rights Reserved.

using AutomationTool;
using EpicGame;
using Gauntlet;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using UE4Game;

namespace Gauntlet.UnrealTest
{
	/// <summary>
	/// Test that waits for the client and server to get to the front-end then quits
	/// </summary>
	public class BootTest : UnrealTestNode<UE4TestConfig>
	{
		/// <summary>
		/// Used to track progress via logging
		/// </summary>
		int			LogLinesLastTick = 0;

		/// <summary>
		/// Time we last saw a change in logging
		/// </summary>
		DateTime LastLogTime = DateTime.Now;

		/// <summary>
		/// Set to true once we detect the game has launched correctly
		/// </summary>
		bool DidDetectLaunch = false;

		/// <summary>
		/// Default constructor
		/// </summary>
		/// <param name="InContext"></param>
		public BootTest(Gauntlet.UnrealTestContext InContext)
			: base(InContext)
		{
		}

		/// <summary>
		/// Returns the configuration description for this test
		/// </summary>
		/// <returns></returns>
		public override UE4TestConfig GetConfiguration()
		{
			UE4TestConfig Config = base.GetConfiguration();

			UnrealTestRole Client = Config.RequireRole(UnrealTargetRole.Client);

			return Config;
		}

		/// <summary>
		/// Called to begin the test.
		/// </summary>
		/// <param name="Pass"></param>
		/// <param name="InNumPasses"></param>
		/// <returns></returns>
		public override bool StartTest(int Pass, int InNumPasses)
		{
			// Call the base class to actually start the test running
			if (!base.StartTest(Pass, InNumPasses))
			{
				return false;
			}

			// track our starting condition
			LastLogTime = DateTime.Now;
			LogLinesLastTick = 0;
			DidDetectLaunch = true;

			return true;
		}

		/// <summary>
		/// Called periodically while the test is running to allow code to monitor health.
		/// </summary>
		public override void TickTest()
		{
			const int kTimeOutDuration = 2;
			const string kStartupCompleteString = "Bringing up level for play took";

			// run the base class tick;
			base.TickTest();

			// Get the log of the first client app
			IAppInstance RunningInstance = this.TestInstance.ClientApps.First();

			UnrealLogParser LogParser = new UnrealLogParser(RunningInstance.StdOut);

			// count how many lines there are in the log to check progress
			int LogLines = LogParser.Content.Split(new[] { '\n' }, StringSplitOptions.RemoveEmptyEntries).Length;

			if (LogLines > LogLinesLastTick)
			{
				LastLogTime = DateTime.Now;
			}
			// Gauntlet will timeout tests based on the -timeout argument, but we have greater insight here so can bail earlier to save
			// tests from idling on the farm needlessly.
			if ((DateTime.Now - LastLogTime).TotalMinutes > kTimeOutDuration)
			{
				Log.Error("No logfile activity observed in last {0} minutes. Ending test", kTimeOutDuration);
				MarkTestComplete();
				SetUnrealTestResult(TestResult.TimedOut);
			}

			// now see if the game has brought the first world up for play
			IEnumerable<string> LogWorldLines = LogParser.GetLogChannel("World");

			if (LogWorldLines.Any(L => L.IndexOf(kStartupCompleteString) >= 0))
			{
				Log.Info("Found world is ready for play. Ending Test");
				MarkTestComplete();
				DidDetectLaunch = true;
				SetUnrealTestResult(TestResult.Passed);
			}
		}

		/// <summary>
		/// Called after a test finishes to create an overall summary based on looking at the artifacts
		/// </summary>
		/// <param name="Result"></param>
		/// <param name="Context"></param>
		/// <param name="Build"></param>
		/// <param name="Artifacts"></param>
		/// <param name="InArtifactPath"></param>
		public override void CreateReport(TestResult Result, UnrealTestContext Context, UnrealBuildSource Build, IEnumerable<UnrealRoleArtifacts> Artifacts, string InArtifactPath)
		{
			// only check for artifacts if the test passed
			if (Result !=TestResult.Passed)
			{
				return;
			}

			if (!DidDetectLaunch)
			{
				SetUnrealTestResult(TestResult.Failed);
				Log.Error("Failed to detect completion of launch");
				return;
			}

			bool MissingFiles = false;

			foreach(var RoleArtifact in Artifacts)
			{
				DirectoryInfo RoleDir = new DirectoryInfo(RoleArtifact.ArtifactPath);

				IEnumerable<FileInfo> ArtifactFiles = RoleDir.EnumerateFiles("*.*", SearchOption.AllDirectories);

				// user may not have cleared paths between runs, so throw away anything that's older than 2m
				ArtifactFiles = ArtifactFiles.Where(F => (DateTime.Now - F.LastWriteTime).TotalMinutes < 2);

				if (ArtifactFiles.Any() == false)
				{
					MissingFiles = true;
					Log.Error("No artifact files found for {0}. Were they not retrieved from the device?", RoleArtifact.SessionRole);
				}

				IEnumerable<FileInfo> LogFiles = ArtifactFiles.Where(F => F.Extension.Equals(".log", StringComparison.OrdinalIgnoreCase));

				if (LogFiles.Any() == false)
				{
					MissingFiles = true;
					Log.Error("No log files found for {0}. Were they not retrieved from the device?", RoleArtifact.SessionRole);
				}
			}

			if (MissingFiles)
			{
				SetUnrealTestResult(TestResult.Failed);
				Log.Error("One or more roles did not generated any artifacts");
			}

			Log.Info("Found valid artifacts for test");
		}
	}
}
