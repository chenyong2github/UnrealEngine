// Copyright Epic Games, Inc. All Rights Reserved.

using AutomationTool;
using Gauntlet;
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using UnrealBuildTool;

namespace UE
{
	/// <summary>
	/// Define a Config class for UE.EditorAutomation and UE.TargetAutomation with the
	/// options that can be used
	/// </summary>
	public class AutomationTestConfig : UnrealTestConfiguration
	{
		/// <summary>
		/// Run with d3d12 RHI
		/// </summary>
		[AutoParam]
		public bool D3D12 = false;

		/// <summary>
		/// Run with Nvidia cards for raytracing support
		/// </summary>
		[AutoParam]
		public bool PreferNvidia = false;

		/// <summary>
		/// Run forcing raytracing 
		/// </summary>
		[AutoParam]
		public bool RayTracing = false;

		/// <summary>
		/// Run forcing raytracing 
		/// </summary>
		[AutoParam]
		public bool StompMalloc = false;

		/// <summary>
		/// Run with d3ddebug
		/// </summary>
		[AutoParam]
		public bool D3DDebug = false;

		/// <summary>
		/// Disable capturing frame trace for image based tests
		/// </summary>
		[AutoParam]
		public bool DisableFrameTraceCapture = true;

		/// <summary>
		/// Filter or groups of tests to apply
		/// </summary>
		[AutoParam]
		public string RunTest = "";
		
		/// <summary>
		/// Absolute or project relative path to write an automation report to.
		/// </summary>
		[AutoParam]
		public string ReportExportPath = "";

		/// <summary>
		/// Path the report can be found at
		/// </summary>
		[AutoParam]
		public string ReportURL = "";

		/// <summary>
		/// Validate DDC during tests
		/// </summary>
		[AutoParam]
		public bool VerifyDDC = false;

		/// <summary>
		/// Validate DDC during tests
		/// </summary>
		[AutoParam]
		public string DDC = "";

		/// <summary>
		/// Used for having the editor and any client communicate
		/// </summary>
		public string SessionID = Guid.NewGuid().ToString();


		/// <summary>
		/// Implement how the settings above are applied to different roles in the session. This is the main avenue for converting options
		/// into command line parameters for the different roles
		/// </summary>
		/// <param name="AppConfig"></param>
		/// <param name="ConfigRole"></param>
		/// <param name="OtherRoles"></param>
		public override void ApplyToConfig(UnrealAppConfig AppConfig, UnrealSessionRole ConfigRole, IEnumerable<UnrealSessionRole> OtherRoles)
		{
			base.ApplyToConfig(AppConfig, ConfigRole, OtherRoles);

			// The "RunTest" argument is required since it is what is passed to the editor to specify which tests to execute
			if (string.IsNullOrEmpty(RunTest))
			{
				throw new AutomationException("No AutomationTest argument specified. Use -RunTest=\"Group:AI\", -RunTest=\"Project\", -RunTest=\"Navigation.Landscape Ramp Test\" etc.");
			}

			// Are we writing out info for Horde?
			if (WriteTestResultsForHorde)
			{
				if (string.IsNullOrEmpty(HordeTestDataPath))
				{
					HordeTestDataPath = HordeReport.DefaultTestDataDir;
				}
				if (string.IsNullOrEmpty(HordeArtifactPath))
				{
					HordeArtifactPath = HordeReport.DefaultArtifactsDir;
				}
			}

		
			// Arguments for writing out the report and providing a URL where it can be viewed
			string ReportArgs = string.IsNullOrEmpty(ReportExportPath) ? "" : string.Format("-ReportExportPath=\"{0}\"", ReportExportPath);

			if (!string.IsNullOrEmpty(ReportURL))
			{
				ReportArgs += string.Format("-ReportURL=\"{0}\"", ReportURL);
			}

			string AutomationTestArgument = string.Format("RunTest {0};", RunTest);

			// if this is not attended then we'll quit the editor after the tests and disable any user interactions
			if (this.Attended == false)
			{
				AutomationTestArgument += "Quit;";
				AppConfig.CommandLine += " -unattended";
			}

			// If there's only one role and it's the editor then tests are running under the editor with no target
			if (ConfigRole.RoleType == UnrealTargetRole.Editor && OtherRoles.Any() == false)
			{ 
				AppConfig.CommandLine += string.Format(" {0} -ExecCmds=\"Automation {1}\"", ReportArgs, AutomationTestArgument);		
			}
			else
			{
				// If the test isnt using the editor for both roles then pass the IP of the editor (us) to the client
				string HostIP = UnrealHelpers.GetHostIpAddress();

				if (ConfigRole.RoleType.IsClient())
				{
					// have the client list the tests it knows about. useful for troubleshooting discrepencies
					AppConfig.CommandLine += string.Format(" -sessionid={0} -messaging -log -TcpMessagingConnect={1}:6666 -ExecCmds=\"Automation list\"", SessionID, HostIP);
				}
				else if (ConfigRole.RoleType.IsEditor())
				{
					AppConfig.CommandLine += string.Format(" -ExecCmds=\"Automation StartRemoteSession {0}; {1}\" -TcpMessagingListen={2}:6666 -multihome={3} {4}", SessionID, AutomationTestArgument, HostIP, HostIP, ReportArgs);
				}
			}

			if (DisableFrameTraceCapture || RayTracing)
			{
				AppConfig.CommandLine += " -DisableFrameTraceCapture";
			}

			if (RayTracing)
			{
				AppConfig.CommandLine += " -dpcvars=r.RayTracing=1,r.SkinCache.CompileShaders=1,AutomationAllowFrameTraceCapture=0";
			}

			// Options specific to windows
			if (ConfigRole.Platform == UnrealTargetPlatform.Win64)
			{
				if (PreferNvidia)
				{
					AppConfig.CommandLine += " -preferNvidia";
				}

				if (D3D12)
				{
					AppConfig.CommandLine += " -d3d12";
				}

				if (D3DDebug)
				{
					AppConfig.CommandLine += " -d3ddebug";
				}

				if (StompMalloc)
				{
					AppConfig.CommandLine += " -stompmalloc";
				}
			}

			// Options specific to roles running under the editor
			if (ConfigRole.RoleType.UsesEditor())
			{
				if (VerifyDDC)
				{
					AppConfig.CommandLine += " -VerifyDDC";
				}

				if (!string.IsNullOrEmpty(DDC))
				{
					AppConfig.CommandLine += string.Format(" -ddc={0}", DDC);
				}
			}
		}
	}

	/// <summary>
	/// Implements a node that runs Unreal automation tests using the editor. The primary argument is "RunTest". E.g
	/// RunUnreal -test=UE.EditorAutomation -RunTest="Group:Animation"
	/// </summary>
	public class EditorAutomation : AutomationNodeBase
	{
		public EditorAutomation(Gauntlet.UnrealTestContext InContext)
			: base(InContext)
		{

		}

		/// <summary>
		/// Define the configuration for this test. Most options are applied by the test config above
		/// </summary>
		/// <returns></returns>
		public override AutomationTestConfig GetConfiguration()
		{
			AutomationTestConfig Config = base.GetConfiguration();

			// Tests in the editor only require a single role
			UnrealTestRole EditorRole = Config.RequireRole(UnrealTargetRole.Editor);
			EditorRole.CommandLineParams.AddRawCommandline("-NoWatchdog -stdout -FORCELOGFLUSH -CrashForUAT -log");

			return Config;
		}
	}

	/// <summary>
	/// Implements a node that runs Unreal automation tests on a target, monitored by an editor. The primary argument is "RunTest". E.g
	/// RunUnreal -test=UE.EditorAutomation -RunTest="Group:Animation"
	/// </summary>
	public class TargetAutomation : AutomationNodeBase
	{
		public TargetAutomation(Gauntlet.UnrealTestContext InContext)
			: base(InContext)
		{
		}

		/// <summary>
		/// Define the configuration for this test. Most options are applied by the test config above
		/// </summary>
		/// <returns></returns>
		public override AutomationTestConfig GetConfiguration()
		{
			AutomationTestConfig Config = base.GetConfiguration();

			// Target tests require an editor which hosts the process
			UnrealTestRole EditorRole = Config.RequireRole(UnrealTargetRole.Editor);
			EditorRole.CommandLineParams.AddRawCommandline("-NoWatchdog -stdout -FORCELOGFLUSH -CrashForUAT -log");

			if (Config.Attended == false)
			{
				// if this is unattended we don't need the UI ( this also alows a wider range of machines to run the test under CIS)
				EditorRole.CommandLineParams.Add("nullrhi");
			}

			// target tests also require a client
			Config.RequireRole(UnrealTargetRole.Client);
			return Config;
		}
	}

	/// <summary>
	/// Base class for automatinon tests. Most of the magic is in here with the Editor/Target forms simply defining the roles
	/// </summary>
	public abstract class AutomationNodeBase : UnrealTestNode<AutomationTestConfig>
	{
		// used to track stdout from the processes 
		private int LastAutomationEntryCount = 0;

		private DateTime LastAutomationEntryTime = DateTime.MinValue;

		public AutomationNodeBase(Gauntlet.UnrealTestContext InContext)
			: base(InContext)
		{
			// We format warnings ourselves so don't show these
			LogWarningsAndErrorsAfterSummary = false;
		}

	
		/// <summary>
		/// Override our name to include the filter we're testing
		/// </summary>
		public override string Name
		{
			get
			{
				string BaseName = base.Name;

				var Config = GetConfiguration();

				if (!string.IsNullOrEmpty(Config.RunTest))
				{
					BaseName += string.Format("(RunTest={0})", Config.RunTest);
				}

				return BaseName;
			}
		}

		/// <summary>
		/// Override TickTest to log interesting things and make sure nothing has stalled
		/// </summary>
		public override void TickTest()
		{
			const float IdleTimeout = 30 * 60;

			List<string> ChannelEntries = new List<string>();

			// We are primarily interested in what the editor is doing
			var AppInstance = TestInstance.EditorApp;

			UnrealLogParser Parser = new UnrealLogParser(AppInstance.StdOut);
			ChannelEntries.AddRange(Parser.GetEditorBusyChannels());

			// Any new entries?
			if (ChannelEntries.Count > LastAutomationEntryCount)
			{
				// log new entries so people have something to look at
				ChannelEntries.Skip(LastAutomationEntryCount).ToList().ForEach(S => Log.Info("{0}", S));
				LastAutomationEntryTime = DateTime.Now;
				LastAutomationEntryCount = ChannelEntries.Count;
			}
			else
			{
				// Check for timeouts
				if (LastAutomationEntryTime == DateTime.MinValue)
				{
					LastAutomationEntryTime = DateTime.Now;
				}

				double ElapsedTime = (DateTime.Now - LastAutomationEntryTime).TotalSeconds;

				// Check for timeout
				if (ElapsedTime > IdleTimeout)
				{
					Log.Error("No activity observed in last {0:0.00} minutes. Aborting test", IdleTimeout / 60);
					MarkTestComplete();
					SetUnrealTestResult(TestResult.TimedOut);
				}
			}

			base.TickTest();
		}

		/// <summary>
		/// Override GetExitCodeAndReason to provide additional checking of success / failure based on what occurred
		/// </summary>
		/// <param name="InArtifacts"></param>
		/// <param name="ExitReason"></param>
		/// <returns></returns>
		protected override int GetExitCodeAndReason(UnrealRoleArtifacts InArtifacts, out string ExitReason)
		{
			int ExitCode = base.GetExitCodeAndReason(InArtifacts, out ExitReason);

			// The editor is an additional arbiter of success
			if (InArtifacts.SessionRole.RoleType == UnrealTargetRole.Editor 
				&& InArtifacts.LogSummary.HasAbnormalExit == false)
			{
				// if no fatal errors, check test results
				if (InArtifacts.LogParser.GetFatalError() == null)
				{
					AutomationLogParser Parser = new AutomationLogParser(InArtifacts.LogParser);

					IEnumerable<AutomationTestResult> TotalTests = Parser.GetResults();
					IEnumerable<AutomationTestResult> FailedTests = TotalTests.Where(R => !R.Passed);

					// Tests failed so list that as our primary cause of failure
					if (FailedTests.Any())
					{
						ExitReason = string.Format("{0}/{1} tests failed", FailedTests.Count(), TotalTests.Count());
						ExitCode = -1;
					}

					// If no tests were run then that's a failure (possibly a bad RunTest argument?)
					if (!TotalTests.Any())
					{
						ExitReason = "No tests were executed!";
						ExitCode = -1;
					}
				}
			}

			return ExitCode;
		}

		/// <summary>
		/// Optional function that is called on test completion and gives an opportunity to create a report
		/// </summary>
		/// <param name="Result"></param>
		/// <param name="Context"></param>
		/// <param name="Build"></param>
		public override void CreateReport(TestResult Result, UnrealTestContext Context, UnrealBuildSource Build, IEnumerable<UnrealRoleArtifacts> Artifacts, string ArtifactPath)
		{
			// Save test result data for Horde build system
			string ReportPath = GetConfiguration().ReportExportPath;
			bool WriteTestResultsForHorde = GetConfiguration().WriteTestResultsForHorde;
			if (WriteTestResultsForHorde)
			{
				ITestReport Report = null;
				if (!string.IsNullOrEmpty(ReportPath))
				{
					string JsonReportPath = Path.Combine(ReportPath, "index.json");
					if (File.Exists(JsonReportPath))
					{
						Log.Verbose("Reading json Unreal Automated test report from {0}", JsonReportPath);
						UnrealAutomatedTestPassResults JsonTestPassResults = UnrealAutomatedTestPassResults.LoadFromJson(JsonReportPath);
						// write test results for Horde
						string HordeArtifactPath = GetConfiguration().HordeArtifactPath;
						HordeReport.UnrealEngineTestPassResults HordeTestPassResults = HordeReport.UnrealEngineTestPassResults.FromUnrealAutomatedTests(JsonTestPassResults, ReportPath, GetConfiguration().ReportURL);
						HordeTestPassResults.CopyTestResultsArtifacts(HordeArtifactPath);
						Report = HordeTestPassResults;
					}
					else
					{
						Log.Info("Could not find Unreal Automated test report at {0}. Generating a simple report instead.", JsonReportPath);
					}
				}
				if (Report == null)
				{
					Report = CreateSimpleReportForHorde(Result);
				}
				// write test data collection for Horde
				string HordeTestDataKey = GetConfiguration().HordeTestDataKey;
				string HordeTestDataFilePath = Path.Combine(GetConfiguration().HordeTestDataPath, "TestDataCollection.json");
				HordeReport.TestDataCollection HordeTestDataCollection = new HordeReport.TestDataCollection();
				HordeTestDataCollection.AddNewTestReport(HordeTestDataKey, Report);
				HordeTestDataCollection.WriteToJson(HordeTestDataFilePath);
			}
		}


		/// <summary>
		/// Override the summary report so we can create a custom summary with info about our tests and
		/// a link to the reports
		/// </summary>
		/// <returns></returns>
		protected override string GetTestSummaryHeader()
		{
			MarkdownBuilder MB = new MarkdownBuilder(base.GetTestSummaryHeader());

			// Everything we need is in the editor artifacts
			var EditorArtifacts = SessionArtifacts.Where(A => A.SessionRole.RoleType == UnrealTargetRole.Editor).FirstOrDefault();

			if (EditorArtifacts != null)
			{
				// Parse automaton info from the log (TODO - use the json version)
				AutomationLogParser Parser = new AutomationLogParser(EditorArtifacts.LogParser);

				// Filter our tests into categories
				IEnumerable<AutomationTestResult> AllTests = Parser.GetResults();
				IEnumerable<AutomationTestResult> IncompleteTests = AllTests.Where(R => !R.Completed);
				IEnumerable<AutomationTestResult> FailedTests = AllTests.Where(R => R.Completed && !R.Passed);
				IEnumerable<AutomationTestResult> TestsWithWarnings = AllTests.Where(R => R.Completed && R.Passed && R.WarningEvents.Any());

				// If there were abnormal exits then look only at the incomplete tests to avoid confusing things.
				if (GetArtifactsThatExitedAbnormally().Any())
				{
					if (AllTests.Count() == 0)
					{
						MB.H3("No tests were executed.");
					}
					else if (IncompleteTests.Count() > 0)
					{
						MB.H3("The following test(s) were incomplete:");

						foreach (AutomationTestResult Result in IncompleteTests)
						{
							MB.H4(string.Format("{0}", Result.FullName));
							MB.UnorderedList(Result.WarningAndErrorEvents.Distinct());
						}
					}
				}
				else
				{
					if (AllTests.Count() == 0)
					{
						MB.H3("No tests were executed.");

						IEnumerable<UnrealLogParser.LogEntry> WarningsAndErrors = Parser.AutomationWarningsAndErrors;

						if (WarningsAndErrors.Any())
						{
							MB.UnorderedList(WarningsAndErrors.Select(E => E.ToString()));
						}
						else
						{
							MB.Paragraph("Unknown failure.");
						}
					}
					else
					{
						// Now list the tests that failed
						if (FailedTests.Count() > 0)
						{
							MB.H3("The following test(s) failed:");

							foreach (AutomationTestResult Result in FailedTests)
							{
								MB.H4(Result.FullName);
								MB.UnorderedList(Result.Events.Distinct());
							}
						}

						if (TestsWithWarnings.Count() > 0)
						{
							MB.H3("The following test(s) completed with warnings:");

							foreach (AutomationTestResult Result in TestsWithWarnings)
							{
								MB.Paragraph(Result.FullName);
								MB.UnorderedList(Result.WarningEvents.Distinct());
							}
						}

						if (IncompleteTests.Count() > 0)
						{
							MB.H3("The following test(s) timed out or did not run:");

							foreach (AutomationTestResult Result in IncompleteTests)
							{
								MB.H4(string.Format("{0}", Result.FullName));
								MB.UnorderedList(Result.WarningAndErrorEvents.Distinct());
							}
						}

						// show a brief summary at the end where it's most visible
						List<string> TestSummary = new List<string>();

						int PassedTests = AllTests.Count() - (FailedTests.Count() + IncompleteTests.Count());
						int TestsPassedWithoutWarnings = PassedTests - TestsWithWarnings.Count();

						TestSummary.Add(string.Format("{0} Test(s) Requested", AllTests.Count()));

						// Print out a summary of each category of result
						if (TestsPassedWithoutWarnings > 0)
						{
							TestSummary.Add(string.Format("{0} Test(s) Passed", TestsPassedWithoutWarnings));
						}

						if (TestsWithWarnings.Count() > 0)
						{
							TestSummary.Add(string.Format("{0} Test(s) Passed with warnings", TestsWithWarnings.Count()));
						}

						if (FailedTests.Count() > 0)
						{
							TestSummary.Add(string.Format("{0} Test(s) Failed", FailedTests.Count()));
						}

						if (IncompleteTests.Count() > 0)
						{
							TestSummary.Add(string.Format("{0} Test(s) didn't complete", IncompleteTests.Count()));
						}

						MB.UnorderedList(TestSummary);
					}
				}

				if (EditorArtifacts.LogParser.GetSummary().EngineInitialized)
				{
					// Use the paths from the report. If we passed these in they should be the same, and if not
					// they'll be valid defaults
					string AutomationReportPath = string.IsNullOrEmpty(Parser.AutomationReportPath) ? GetConfiguration().ReportExportPath : Parser.AutomationReportPath;
					string AutomationReportURL = string.IsNullOrEmpty(Parser.AutomationReportURL) ? GetConfiguration().ReportURL : Parser.AutomationReportURL;
					if (!string.IsNullOrEmpty(AutomationReportPath) || !string.IsNullOrEmpty(AutomationReportURL))
					{
						MB.H3("Links");

						if (string.IsNullOrEmpty(AutomationReportURL) == false)
						{
							MB.Paragraph(string.Format("View results here: {0}", AutomationReportURL));
						}

						if (string.IsNullOrEmpty(AutomationReportPath) == false)
						{
							MB.Paragraph(string.Format("Open results in UnrealEd from {0}", AutomationReportPath));
						}
					}
				}
			}

			return MB.ToString();
		}

		/// <summary>
		/// Returns Errors found during tests. We call the base version to get standard errors then
		/// Add on any errors seen in tests
		/// </summary>
		public override IEnumerable<string> GetErrors()
		{
			List<string> AllErrors = new List<string>(base.GetErrors());

			foreach (var Artifact in GetArtifactsWithFailures())
			{
				if (Artifact.SessionRole.RoleType == UnrealTargetRole.Editor)
				{
					AutomationLogParser Parser = new AutomationLogParser(Artifact.LogParser);
					AllErrors.AddRange(
						Parser.GetResults().Where(R => !R.Passed)
							.SelectMany(R => R.Events
								.Where(E => E.ToLower().Contains("error"))
								.Distinct()
							)
						);
				}
			}

			return AllErrors;
		}

		/// <summary>
		/// Returns warnings found during tests. We call the base version to get standard warnings then
		/// Add on any errors seen in tests
		/// </summary>
		public override IEnumerable<string> GetWarnings()
		{
			List<string> AllWarnings = new List<string>(base.GetWarnings());

			if (SessionArtifacts == null)
			{
				return Enumerable.Empty<string>();
			}

			foreach (var Artifact in SessionArtifacts)
			{
				if (Artifact.SessionRole.RoleType == UnrealTargetRole.Editor)
				{
					AutomationLogParser Parser = new AutomationLogParser(Artifact.LogParser);
					AllWarnings.AddRange(
						Parser.GetResults()
							.SelectMany(R => R.Events
								.Where(E => E.ToLower().Contains("warning"))
								.Distinct()
							)
						);
				}
			}

			return AllWarnings;
		}
	}

}
