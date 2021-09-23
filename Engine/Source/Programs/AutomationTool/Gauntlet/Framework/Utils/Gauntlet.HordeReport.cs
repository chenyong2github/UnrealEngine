// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using System.Text.Json.Serialization;
using AutomationTool;

namespace Gauntlet
{
	public static class HordeReport
	{
		/// <summary>
		/// Default location to store Test Data 
		/// </summary>
		public static string DefaultTestDataDir
		{
			get
			{
				return Path.GetFullPath(Environment.GetEnvironmentVariable("UE_TESTDATA_DIR") ?? Path.Combine(CommandUtils.CmdEnv.EngineSavedFolder, "TestData"));
			}
		}
		/// <summary>
		/// Default location to store test Artifacts 
		/// </summary>
		public static string DefaultArtifactsDir
		{
			get
			{
				return Path.GetFullPath(Environment.GetEnvironmentVariable("UE_ARTIFACTS_DIR") ?? CommandUtils.CmdEnv.LogFolder);
			}
		}
		/// <summary>
		/// Is Environement set by Horde Agent 
		/// </summary>
		public static bool IsUnderHordeAgent
		{
			get
			{
				return !string.IsNullOrEmpty(Environment.GetEnvironmentVariable("UE_TESTDATA_DIR"));
			}
		}
		/// <summary>
		/// Default Job Horde link if running under Horde agent, otherwise return an empty string
		/// </summary>
		public static string DefaultHordeJobLink
		{
			get
			{
				string HordeJobId = Environment.GetEnvironmentVariable("UE_HORDE_JOBID");
				if (!string.IsNullOrEmpty(HordeJobId))
				{
					return string.Format("https://horde.devtools.epicgames.com/job/{0}", HordeJobId);
				}

				return string.Empty;
			}
		}
		/// <summary>
		/// Default Job Step Horde link if running under Horde agent, otherwise return an empty string
		/// </summary>
		public static string DefaultHordeJobStepLink
		{
			get
			{
				string HordeJobStepId = Environment.GetEnvironmentVariable("UE_HORDE_STEPID");
				string HordeJobLink = DefaultHordeJobLink;
				if (!string.IsNullOrEmpty(HordeJobStepId))
				{
					return string.Format("{0}?step={1}", HordeJobLink, HordeJobStepId);
				}
				else if(!string.IsNullOrEmpty(HordeJobLink))
				{
					return HordeJobLink;
				}

				return string.Empty;
			}
		}		

		public abstract class BaseHordeReport : BaseTestReport
		{
			protected string OutputArtifactPath;

			/// <summary>
			/// Attach Artifact to the Test Report
			/// </summary>
			/// <param name="ArtifactPath"></param>
			/// <param name="Name"></param>
			/// <returns>return true if the file was successfully attached</returns>
			public override bool AttachArtifact(string ArtifactPath, string Name = null)
			{
				if (string.IsNullOrEmpty(OutputArtifactPath))
				{
					throw new InvalidOperationException("OutputArtifactPath must be set before attaching any artifact");
				}

				if (File.Exists(ArtifactPath))
				{
					try
					{
						string TargetPath = Path.Combine(OutputArtifactPath, Name ?? Path.GetFileName(ArtifactPath));
						string TargetDirectry = Path.GetDirectoryName(TargetPath);
						if (!Directory.Exists(TargetDirectry)) { Directory.CreateDirectory(TargetDirectry); }
						File.Copy(Utils.SystemHelpers.GetFullyQualifiedPath(ArtifactPath), Utils.SystemHelpers.GetFullyQualifiedPath(TargetPath));
						return true;
					}
					catch (Exception Ex)
					{
						Log.Error("Failed to copy artifact '{0}'. {1}", Path.GetFileName(ArtifactPath), Ex);
					}
				}
				return false;
			}

			/// <summary>
			/// Set Output Artifact Path and create the directory if missing
			/// </summary>
			/// <param name="InPath"></param>
			/// <returns></returns>
			public void SetOutputArtifactPath(string InPath)
			{
				OutputArtifactPath = Path.GetFullPath(InPath);

				if (!Directory.Exists(OutputArtifactPath))
				{
					Directory.CreateDirectory(OutputArtifactPath);
				}
				Log.Verbose(string.Format("Test Report output artifact path is set to: {0}", OutputArtifactPath));
			}
		}

		/// <summary>
		/// Contains detailed information about device that run tests
		/// </summary>
		public class Device
		{
			public string DeviceName { get; set; }
			public string Instance { get; set; }
			public string Platform { get; set; }
			public string OSVersion { get; set; }
			public string Model { get; set; }
			public string GPU { get; set; }
			public string CPUModel { get; set; }
			public int RAMInGB { get; set; }
			public string RenderMode { get; set; }
			public string RHI { get; set; }
		}

		/// <summary>
		/// Contains reference to files used or generated for file comparison
		/// </summary>
		public class ComparisonFiles
		{
			public string Difference { get; set; }
			public string Approved { get; set; }
			public string Unapproved { get; set; }
		}
		/// <summary>
		/// Contains information about test artifact
		/// </summary>
		public class Artifact
		{
			public Artifact()
			{
				Files = new ComparisonFiles();
			}

			public string Id { get; set; }
			public string Name { get; set; }
			public string Type { get; set; }
			public ComparisonFiles Files { get; set; }
		}
		/// <summary>
		/// Contains information about test entry event
		/// </summary>
		public class Event
		{
			public EventType Type { get; set; }
			public string Message { get; set; }
			public string Context { get; set; }
			public string Artifact { get; set; }
		}
		/// <summary>
		/// Contains information about test entry
		/// </summary>
		public class Entry
		{
			public Entry()
			{
				Event = new Event();
			}

			public Event Event { get; set; }
			public string Filename { get; set; }
			public int LineNumber { get; set; }
			public string Timestamp { get; set; }
		}
		/// <summary>
		/// Contains detailed information about test result. This is to what TestPassResult refere to for each test result. 
		/// </summary>
		public class TestResultDetailed
		{
			public TestResultDetailed()
			{
				Artifacts = new List<Artifact>();
				Entries = new List<Entry>();
			}

			public string TestDisplayName { get; set; }
			public string FullTestPath { get; set; }
			public TestStateType State { get; set; }
			public string DeviceInstance { get; set; }
			public int Warnings { get; set; }
			public int Errors { get; set; }
			public List<Artifact> Artifacts { get; set; }
			public List<Entry> Entries { get; set; }

			/// <summary>
			/// Add a new Artifact to the test result and return it 
			/// </summary>
			public Artifact AddNewArtifact()
			{
				Artifact NewArtifact = new Artifact();
				Artifacts.Add(NewArtifact);

				return NewArtifact;
			}

			/// <summary>
			/// Add a new Entry to the test result and return it 
			/// </summary>
			public Entry AddNewEntry()
			{
				Entry NewEntry = new Entry();
				Entries.Add(NewEntry);

				return NewEntry;
			}
		}
		/// <summary>
		/// Contains a brief information about test result.
		/// </summary>
		public class TestResult
		{
			public TestResult()
			{
				TestDetailed = new TestResultDetailed();
			}

			public string TestDisplayName
			{
				get { return TestDetailed.TestDisplayName; }
				set { TestDetailed.TestDisplayName = value; }
			}
			public string FullTestPath
			{
				get { return TestDetailed.FullTestPath; }
				set { TestDetailed.FullTestPath = value; }
			}
			public TestStateType State
			{
				get { return TestDetailed.State; }
				set { TestDetailed.State = value; }
			}
			public string DeviceInstance
			{
				get { return TestDetailed.DeviceInstance; }
				set { TestDetailed.DeviceInstance = value; }
			}
			public int Errors
			{
				get { return TestDetailed.Errors; }
				set { TestDetailed.Errors = value; }
			}
			public int Warnings
			{
				get { return TestDetailed.Warnings; }
				set { TestDetailed.Warnings = value; }
			}

			public string ArtifactName { get; set; }


			private TestResultDetailed TestDetailed { get; set; }

			/// <summary>
			/// Return the underlying TestResultDetailed 
			/// </summary>
			public TestResultDetailed GetTestResultDetailed()
			{
				return TestDetailed;
			}
			/// <summary>
			/// Set the underlying TestResultDetailed
			/// </summary>
			public void SetTestResultDetailed(TestResultDetailed InTestResultDetailed)
			{
				TestDetailed = InTestResultDetailed;
			}
		}

		/// <summary>
		/// Contains information about an entire test pass 
		/// </summary>
		public class UnrealEngineTestPassResults : BaseHordeReport
		{
			public override string Type
			{
				get { return "Unreal Automated Tests"; }
			}

			public UnrealEngineTestPassResults() : base()
			{
				Devices = new List<Device>();
				Tests = new List<TestResult>();
			}

			public List<Device> Devices { get; set; }
			public string ReportCreatedOn { get; set; }
			public string ReportURL { get; set; }
			public int SucceededCount { get; set; }
			public int SucceededWithWarningsCount { get; set; }
			public int FailedCount { get; set; }
			public int NotRunCount { get; set; }
			public int InProcessCount { get; set; }
			public float TotalDurationSeconds { get; set; }
			public List<TestResult> Tests { get; set; }

			/// <summary>
			/// Add a new Device to the pass results and return it 
			/// </summary>
			private Device AddNewDevice()
			{
				Device NewDevice = new Device();
				Devices.Add(NewDevice);

				return NewDevice;
			}

			/// <summary>
			/// Add a new TestResult to the pass results and return it 
			/// </summary>
			private TestResult AddNewTestResult()
			{
				TestResult NewTestResult = new TestResult();
				Tests.Add(NewTestResult);

				return NewTestResult;
			}

			public override void AddEvent(EventType Type, string Message, object Context = null)
			{
				throw new System.NotImplementedException("AddEvent not implemented");
			}

			/// <summary>
			/// Convert UnrealAutomatedTestPassResults to Horde data model
			/// </summary>
			/// <param name="InTestPassResults"></param>
			/// <param name="ReportPath"></param>
			/// <param name="ReportURL"></param>
			public static UnrealEngineTestPassResults FromUnrealAutomatedTests(UnrealAutomatedTestPassResults InTestPassResults, string ReportPath, string ReportURL)
			{
				UnrealEngineTestPassResults OutTestPassResults = new UnrealEngineTestPassResults();
				if (InTestPassResults.Devices != null)
				{
					foreach (UnrealAutomationDevice InDevice in InTestPassResults.Devices)
					{
						Device ConvertedDevice = OutTestPassResults.AddNewDevice();
						ConvertedDevice.DeviceName = InDevice.DeviceName;
						ConvertedDevice.Instance = InDevice.Instance;
						ConvertedDevice.Platform = InDevice.Platform;
						ConvertedDevice.OSVersion = InDevice.OSVersion;
						ConvertedDevice.Model = InDevice.Model;
						ConvertedDevice.GPU = InDevice.GPU;
						ConvertedDevice.CPUModel = InDevice.CPUModel;
						ConvertedDevice.RAMInGB = InDevice.RAMInGB;
						ConvertedDevice.RenderMode = InDevice.RenderMode;
						ConvertedDevice.RHI = InDevice.RHI;
					}
				}
				OutTestPassResults.ReportCreatedOn = InTestPassResults.ReportCreatedOn;
				OutTestPassResults.ReportURL = ReportURL;
				OutTestPassResults.SucceededCount = InTestPassResults.Succeeded;
				OutTestPassResults.SucceededWithWarningsCount = InTestPassResults.SucceededWithWarnings;
				OutTestPassResults.FailedCount = InTestPassResults.Failed;
				OutTestPassResults.NotRunCount = InTestPassResults.NotRun;
				OutTestPassResults.InProcessCount = InTestPassResults.InProcess;
				OutTestPassResults.TotalDurationSeconds = InTestPassResults.TotalDuration;
				if (InTestPassResults.Tests != null)
				{
					foreach (UnrealAutomatedTestResult InTestResult in InTestPassResults.Tests)
					{
						TestResult ConvertedTestResult = OutTestPassResults.AddNewTestResult();
						ConvertedTestResult.TestDisplayName = InTestResult.TestDisplayName;
						ConvertedTestResult.FullTestPath = InTestResult.FullTestPath;
						ConvertedTestResult.State = InTestResult.State;
						ConvertedTestResult.DeviceInstance = InTestResult.DeviceInstance;
						Guid TestGuid = Guid.NewGuid();
						ConvertedTestResult.ArtifactName = TestGuid + ".json";
						InTestResult.ArtifactName = ConvertedTestResult.ArtifactName;
						// Copy Test Result Detail
						TestResultDetailed ConvertedTestResultDetailed = ConvertedTestResult.GetTestResultDetailed();
						ConvertedTestResultDetailed.Errors = InTestResult.Errors;
						ConvertedTestResultDetailed.Warnings = InTestResult.Warnings;
						foreach (UnrealAutomationArtifact InTestArtifact in InTestResult.Artifacts)
						{
							Artifact NewArtifact = ConvertedTestResultDetailed.AddNewArtifact();
							NewArtifact.Id = InTestArtifact.Id;
							NewArtifact.Name = InTestArtifact.Name;
							NewArtifact.Type = InTestArtifact.Type;
							ComparisonFiles ArtifactFiles = NewArtifact.Files;
							ArtifactFiles.Difference = !string.IsNullOrEmpty(InTestArtifact.Files.Difference)?Path.Combine(ReportPath, InTestArtifact.Files.Difference):null;
							ArtifactFiles.Approved = !string.IsNullOrEmpty(InTestArtifact.Files.Approved)?Path.Combine(ReportPath, InTestArtifact.Files.Approved):null;
							ArtifactFiles.Unapproved = !string.IsNullOrEmpty(InTestArtifact.Files.Unapproved)?Path.Combine(ReportPath, InTestArtifact.Files.Unapproved):null;
						}
						foreach (UnrealAutomationEntry InTestEntry in InTestResult.Entries)
						{
							Entry NewEntry = ConvertedTestResultDetailed.AddNewEntry();
							NewEntry.Filename = InTestEntry.Filename;
							NewEntry.LineNumber = InTestEntry.LineNumber;
							NewEntry.Timestamp = InTestEntry.Timestamp;
							Event EntryEvent = NewEntry.Event;
							EntryEvent.Artifact = InTestEntry.Event.Artifact;
							EntryEvent.Context = InTestEntry.Event.Context;
							EntryEvent.Message = InTestEntry.Event.Message;
							EntryEvent.Type = InTestEntry.Event.Type;
						}
					}
				}
				return OutTestPassResults;
			}

			/// <summary>
			/// Copy Test Results Artifacts
			/// </summary>
			/// <param name="OutputArtifactPath"></param>
			public void CopyTestResultsArtifacts(string InOutputArtifactPath)
			{
				SetOutputArtifactPath(InOutputArtifactPath);
				int ArtifactsCount = 0;
				foreach (TestResult OutputTestResult in Tests)
				{
					TestResultDetailed OutputTestResultDetailed = OutputTestResult.GetTestResultDetailed();
					// copy artifacts
					foreach (Artifact TestArtifact in OutputTestResultDetailed.Artifacts)
					{
						string[] ArtifactPaths= { TestArtifact.Files.Difference, TestArtifact.Files.Approved, TestArtifact.Files.Unapproved };
						foreach (string ArtifactPath in ArtifactPaths)
						{
							if (AttachArtifact(ArtifactPath)) { ArtifactsCount++; }
						}
						TestArtifact.Files.Difference = Path.GetFileName(TestArtifact.Files.Difference);
						TestArtifact.Files.Approved = Path.GetFileName(TestArtifact.Files.Approved);
						TestArtifact.Files.Unapproved = Path.GetFileName(TestArtifact.Files.Unapproved);
					}
					// write test json blob
					string TestResultFilePath = Path.Combine(OutputArtifactPath, OutputTestResult.ArtifactName);
					try
					{
						File.WriteAllText(TestResultFilePath, JsonSerializer.Serialize(OutputTestResultDetailed, GetDefaultJsonOptions()));
						ArtifactsCount++;
					}
					catch (Exception Ex)
					{
						Log.Error("Failed to save Test Result for '{0}'. {1}", OutputTestResult.TestDisplayName, Ex);
					}
				}
				Log.Verbose("Copied {0} artifacts for Horde to {1}", ArtifactsCount, OutputArtifactPath);
			}
		}

		/// <summary>
		/// Contains Test success/failed status and a list of errors and warnings
		/// </summary>
		public class SimpleTestReport : BaseHordeReport
		{
			public override string Type
			{
				get { return "Simple Report"; }
			}
			public SimpleTestReport() : base()
			{
				Logs = new List<String>();
				Errors = new List<String>();
				Warnings = new List<String>();
			}

			public string Description { get; set; }
			public string ReportCreatedOn { get; set; }
			public float TotalDurationSeconds { get; set; }
			public bool HasSucceeded { get; set; }
			public string Status { get; set; }
			public string URLLink { get; set; }
			public List<String> Logs { get; set; }
			public List<String> Errors { get; set; }
			public List<String> Warnings { get; set; }

			public override void AddEvent(EventType Type, string Message, object Context = null)
			{
				switch (Type)
				{
					case EventType.Error:
						Errors.Add(Message);
						break;
					case EventType.Warning:
						Warnings.Add(Message);
						break;
				}
				// The rest is ignored with this report type.
			}

			public override bool AttachArtifact(string ArtifactPath, string Name = null)
			{
				if (base.AttachArtifact(ArtifactPath, Name))
				{
					Logs.Add(Name ?? Path.GetFileName(ArtifactPath));
					return true;
				}
				return false;
			}
		}

		/// <summary>
		/// Container for Test Data items 
		/// </summary>
		public class TestDataCollection
		{
			public class DataItem
			{
				public string Key { get; set; }
				public object Data { get; set; }
			}
			public TestDataCollection()
			{
				Items = new List<DataItem>();
			}

			public DataItem AddNewTestReport(string InKey, ITestReport InData)
			{
				if (string.IsNullOrEmpty(InKey))
				{
					throw new System.ArgumentException("Test Data key can't be an empty string.");
				}
				DataItem NewDataItem = new DataItem();
				NewDataItem.Key = InData.Type +"::"+ InKey;
				NewDataItem.Data = InData;
				Items.Add(NewDataItem);
				return NewDataItem;
			}

			public List<DataItem> Items { get; set; }

			/// <summary>
			/// Write Test Data Collection to json
			/// </summary>
			/// <param name="OutputTestDataFilePath"></param>
			public void WriteToJson(string OutputTestDataFilePath, bool bIncrementNameIfFileExists = false)
			{
				string OutputTestDataDir = Path.GetDirectoryName(OutputTestDataFilePath);
				if (!Directory.Exists(OutputTestDataDir))
				{
					Directory.CreateDirectory(OutputTestDataDir);
				}
				if (File.Exists(OutputTestDataFilePath) && bIncrementNameIfFileExists)
				{
					// increment filename if file exists
					string Ext = Path.GetExtension(OutputTestDataFilePath);
					string Filename = OutputTestDataFilePath.Replace(Ext, "");
					int Incr = 0;
					do
					{
						Incr++;
						OutputTestDataFilePath = string.Format("{0}{1}{2}", Filename, Incr, Ext);
					} while (File.Exists(OutputTestDataFilePath));
				}
				// write test pass summary
				Log.Verbose("Writing Test Data Collection for Horde at {0}", OutputTestDataFilePath);
				try
				{
					File.WriteAllText(OutputTestDataFilePath, JsonSerializer.Serialize(this, GetDefaultJsonOptions()));
				}
				catch (Exception Ex)
				{
					Log.Error("Failed to save Test Data Collection for Horde. {0}", Ex);
				}
			}
		}
		private static JsonSerializerOptions GetDefaultJsonOptions()
		{
			return new JsonSerializerOptions
			{
				WriteIndented = true
			};
		}
	}
}
