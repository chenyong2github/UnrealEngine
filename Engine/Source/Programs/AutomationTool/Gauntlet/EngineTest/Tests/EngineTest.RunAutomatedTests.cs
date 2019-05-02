// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using EpicGame;
using Gauntlet;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Net;
using System.Text.RegularExpressions;

namespace EngineTest
{

	public class AutomatedTestConfig : UnrealTestConfiguration
	{
		/// <summary>
		/// Set to true if the editor executes all these tests in is own process and PIE
		/// </summary>
		[AutoParam]
		public bool UseEditor = false;

		/// <summary>
		/// Filter or groups of tests to apply
		/// </summary>
		[AutoParam]
		public string TestFilter = "Filter:Project+Filter:System";

		/// <summary>
		/// Path to write the automation report to
		/// </summary>
		[AutoParam]
		public string ReportOutputPath = "";

		/// <summary>
		/// Path the report can be found at
		/// </summary>
		[AutoParam]
		public string ReportOutputURL = "";

		/// <summary>
		/// Used for having the editor and any client communicate
		/// </summary>
		public string SessionID = Guid.NewGuid().ToString();

		public override void ApplyToConfig(UnrealAppConfig AppConfig, UnrealSessionRole ConfigRole, IEnumerable<UnrealSessionRole> OtherRoles)
		{
			base.ApplyToConfig(AppConfig, ConfigRole, OtherRoles);

			string ReportOutputArg = string.IsNullOrEmpty(ReportOutputPath) ? "" : string.Format("-ReportOutputPath=\"{0}\"", ReportOutputPath);

			if (UseEditor)
			{
				if (ConfigRole.RoleType == UnrealTargetRole.Editor)
				{
					AppConfig.CommandLine += string.Format(" -NoWatchdog -stdout -FORCELOGFLUSH -CrashForUAT -unattended -buildmachine {0} -ExecCmds=\"Automation RunTests {1}; Quit;\"", ReportOutputArg, TestFilter);
				}
			}
			else
			{
				string HostIP = UnrealHelpers.GetHostIpAddress();

				if (ConfigRole.RoleType == UnrealTargetRole.Client)
				{
					AppConfig.CommandLine += string.Format("-sessionid={0} -messaging -log -TcpMessagingConnect={1}:6666", SessionID, HostIP);
				}
				else if (ConfigRole.RoleType == UnrealTargetRole.Editor)
				{
					AppConfig.CommandLine += string.Format("-nullrhi -ExecCmds=\"Automation StartRemoteSession {0};RunTests {1}; Quit\" -TcpMessagingListen={2}:6666 -log {3}", SessionID, TestFilter, HostIP, ReportOutputArg);
				}
			}
		}
	}


	/// <summary>
	/// Runs automated tests on a platform
	/// </summary>
	public class RunAutomatedTests : UnrealTestNode<AutomatedTestConfig>
	{
		private int LastAutomationEntryCount = 0;

		private DateTime LastAutomationEntryTime = DateTime.MinValue;

		public RunAutomatedTests(Gauntlet.UnrealTestContext InContext)
			: base(InContext)
		{
		}

		public override AutomatedTestConfig GetConfiguration()
		{
			AutomatedTestConfig Config = base.GetConfiguration();

			if (Config.UseEditor)
			{
				Config.RequireRole(UnrealTargetRole.Editor);
			}
			else
			{
				Config.RequireRole(UnrealTargetRole.Editor);
				Config.RequireRole(UnrealTargetRole.Client);
			}

			Config.MaxDuration = Context.TestParams.ParseValue("MaxDuration", 3600);

			return Config;
		}

		public override string Name
		{
			get
			{
				string BaseName = base.Name;

				var Config = GetConfiguration();

				if (!string.IsNullOrEmpty(Config.TestFilter))
				{
					BaseName += " " + Config.TestFilter;
				}

				return BaseName;
			}
		}

		public override void TickTest()
		{
			const float IdleTimeout = 5 * 60;

			List<string> ChannelEntries = new List<string>();

			//var AppInstance = GetConfiguration().UseEditor ? TestInstance.EditorApp : TestInstance.ClientApps.FirstOrDefault();
			var AppInstance = TestInstance.EditorApp;

			UnrealLogParser Parser = new UnrealLogParser(AppInstance.StdOut);	
			ChannelEntries.AddRange(Parser.GetLogChannels(new string[] { "Automation", "FunctionalTest" }, false));

			if (ChannelEntries.Count > LastAutomationEntryCount)
			{
				// log new entries so people have something to look at
				ChannelEntries.Skip(LastAutomationEntryCount).ToList().ForEach(S => Log.Info("{0}", S));
				LastAutomationEntryTime = DateTime.Now;
				LastAutomationEntryCount = ChannelEntries.Count;
			}
			else
			{
				if (LastAutomationEntryTime == DateTime.MinValue)
				{
					LastAutomationEntryTime = DateTime.Now;
				}

				double ElapsedTime = (DateTime.Now - LastAutomationEntryTime).TotalSeconds;

				if (ElapsedTime > IdleTimeout)
				{
					Log.Error("No automation activity observed in last {0:0.00} minutes. Aborting test", IdleTimeout / 60);
					MarkTestComplete();
					SetUnrealTestResult(TestResult.TimedOut);
				}
			}

			base.TickTest();
		}
		 
		protected override int GetExitCodeAndReason(UnrealRoleArtifacts InArtifacts, out string ExitReason)
		{
			int ExitCode = base.GetExitCodeAndReason(InArtifacts, out ExitReason);

			if (InArtifacts.SessionRole.RoleType == UnrealTargetRole.Editor)
			{
				// if no fatal errors, check test results
				if (InArtifacts.LogParser.GetFatalError() == null)
				{
					AutomationLogParser Parser = new AutomationLogParser(InArtifacts.LogParser);

					IEnumerable<AutomationTestResult> TotalTests = Parser.GetResults();
					IEnumerable<AutomationTestResult> FailedTests = TotalTests.Where(R => !R.Passed);

					if (FailedTests.Any())
					{
						ExitReason = string.Format("{0} of {1} tests failed", FailedTests.Count(), TotalTests.Count());
						ExitCode = -1;
					}
				}
			}

			return ExitCode;
		}

		/// <summary>
		/// Override the summary report so we can insert a link to our report and the failed tests
		/// </summary>
		/// <returns></returns>
		protected override string GetTestSummaryHeader()
		{
			MarkdownBuilder MB = new MarkdownBuilder(base.GetTestSummaryHeader());

			string ReportLink = GetConfiguration().ReportOutputURL;

			if (string.IsNullOrEmpty(ReportLink) == false)
			{
				MB.Paragraph(string.Format("Report: {0}", ReportLink));
			}

			var EditorArtifacts = SessionArtifacts.Where(A => A.SessionRole.RoleType == UnrealTargetRole.Editor).FirstOrDefault();

			if (EditorArtifacts != null)
			{
				AutomationLogParser Parser = new AutomationLogParser(EditorArtifacts.LogParser);

				IEnumerable<AutomationTestResult> AllTests = Parser.GetResults();
				IEnumerable<AutomationTestResult> FailedTests = AllTests.Where(R => !R.Passed);

				MB.Paragraph(string.Format("{0} of {1} tests passed", AllTests.Count() - FailedTests.Count(), AllTests.Count()));

				if (FailedTests.Count() > 0)
				{
					MB.H3("Test Failures");

					foreach (AutomationTestResult Result in FailedTests)
					{
						MB.H4(string.Format("{0} Failed", Result.DisplayName));
						MB.UnorderedList(Result.Events);
					}
				}
			}

			return MB.ToString();
		}

		/// <summary>
		/// Override the role summary so we can display the actual test failures
		/// </summary>
		/// <param name="InArtifacts"></param>
		/// <param name="Summary"></param>
		/// <returns></returns>
		protected override int GetRoleSummary(UnrealRoleArtifacts InArtifacts, out string Summary)
		{
			int ExitCode = base.GetRoleSummary(InArtifacts, out Summary);

			if (InArtifacts.SessionRole.RoleType == UnrealTargetRole.Editor)
			{
				
			}

			return ExitCode;
		}

		/// <summary>
		/// Returns Errors found during tests. By default only fatal errors are considered
		/// </summary>
		public override IEnumerable<string> GetErrors()
		{
			List<string> AllErrors = new List<string>( base.GetErrors() );

			foreach (var Artifact in GetArtifactsWithFailures())
			{
				if (Artifact.SessionRole.RoleType == UnrealTargetRole.Editor)
				{
					AutomationLogParser Parser = new AutomationLogParser(Artifact.LogParser);
					AllErrors.AddRange(
						Parser.GetResults().Where(R => !R.Passed)
						.SelectMany(R => R.Events
							.Where(E => E.ToLower().Contains("error"))
							)
						);
				}
			}

			return AllErrors;
		}

	}
}
