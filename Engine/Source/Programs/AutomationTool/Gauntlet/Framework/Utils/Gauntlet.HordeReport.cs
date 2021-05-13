// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using Tools.DotNETCommon;
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

		public class BaseHordeReport : BaseTestReport
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
		/// Contains reference to files used or generated for file comparison
		/// </summary>
		public class ComparisonFiles
		{
			public string Difference;
			public string Approved;
			public string Unapproved;
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

			public string Id;
			public string Name;
			public string Type;
			public ComparisonFiles Files;
		}
		/// <summary>
		/// Contains information about test entry event
		/// </summary>
		public class Event
		{
			public string Type;
			public string Message;
			public string Context;
			public string Artifact;
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

			public Event Event;
			public string Filename;
			public int LineNumber;
			public string Timestamp;
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

			public string TestDisplayName;
			public string FullTestPath;
			public string State;
			public int Warnings;
			public int Errors;
			public List<Artifact> Artifacts;
			public List<Entry> Entries;

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
			public string State
			{
				get { return TestDetailed.State; }
				set { TestDetailed.State = value; }
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

			public string ArtifactName;

			private TestResultDetailed TestDetailed;

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

			public UnrealEngineTestPassResults()
			{
				Tests = new List<TestResult>();
			}

			public string ClientDescriptor;
			public string ReportCreatedOn;
			public string ReportURL;
			public int SucceededCount;
			public int SucceededWithWarningsCount;
			public int FailedCount;
			public int NotRunCount;
			public int InProcessCount;
			public float TotalDurationSeconds;
			public List<TestResult> Tests;

			/// <summary>
			/// Add a new TestResult to the pass results and return it 
			/// </summary>
			private TestResult AddNewTestResult()
			{
				TestResult NewTestResult = new TestResult();
				Tests.Add(NewTestResult);

				return NewTestResult;
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
				OutTestPassResults.ClientDescriptor = InTestPassResults.clientDescriptor;
				OutTestPassResults.ReportCreatedOn = InTestPassResults.reportCreatedOn;
				OutTestPassResults.ReportURL = ReportURL;
				OutTestPassResults.SucceededCount = InTestPassResults.succeeded;
				OutTestPassResults.SucceededWithWarningsCount = InTestPassResults.succeededWithWarnings;
				OutTestPassResults.FailedCount = InTestPassResults.failed;
				OutTestPassResults.NotRunCount = InTestPassResults.notRun;
				OutTestPassResults.InProcessCount = InTestPassResults.inProcess;
				OutTestPassResults.TotalDurationSeconds = InTestPassResults.totalDuration;
				if (InTestPassResults.tests != null)
				{
					foreach (UnrealAutomatedTestResult InTestResult in InTestPassResults.tests)
					{
						TestResult ConvertedTestResult = OutTestPassResults.AddNewTestResult();
						ConvertedTestResult.TestDisplayName = InTestResult.testDisplayName;
						ConvertedTestResult.FullTestPath = InTestResult.fullTestPath;
						ConvertedTestResult.State = InTestResult.state;
						Guid TestGuid = Guid.NewGuid();
						ConvertedTestResult.ArtifactName = TestGuid + ".json";
						InTestResult.artifactName = ConvertedTestResult.ArtifactName;
						// Copy Test Result Detail
						TestResultDetailed ConvertedTestResultDetailed = ConvertedTestResult.GetTestResultDetailed();
						ConvertedTestResultDetailed.Errors = InTestResult.errors;
						ConvertedTestResultDetailed.Warnings = InTestResult.warnings;
						foreach (UnrealAutomationArtifact InTestArtifact in InTestResult.artifacts)
						{
							Artifact NewArtifact = ConvertedTestResultDetailed.AddNewArtifact();
							NewArtifact.Id = InTestArtifact.id;
							NewArtifact.Name = InTestArtifact.name;
							NewArtifact.Type = InTestArtifact.type;
							ComparisonFiles ArtifactFiles = NewArtifact.Files;
							ArtifactFiles.Difference = !string.IsNullOrEmpty(InTestArtifact.files.difference)?Path.Combine(ReportPath, InTestArtifact.files.difference):null;
							ArtifactFiles.Approved = !string.IsNullOrEmpty(InTestArtifact.files.approved)?Path.Combine(ReportPath, InTestArtifact.files.approved):null;
							ArtifactFiles.Unapproved = !string.IsNullOrEmpty(InTestArtifact.files.unapproved)?Path.Combine(ReportPath, InTestArtifact.files.unapproved):null;
						}
						foreach (UnrealAutomationEntry InTestEntry in InTestResult.entries)
						{
							Entry NewEntry = ConvertedTestResultDetailed.AddNewEntry();
							NewEntry.Filename = InTestEntry.filename;
							NewEntry.LineNumber = InTestEntry.lineNumber;
							NewEntry.Timestamp = InTestEntry.timestamp;
							Event EntryEvent = NewEntry.Event;
							EntryEvent.Artifact = InTestEntry.@event.artifact;
							EntryEvent.Context = InTestEntry.@event.context;
							EntryEvent.Message = InTestEntry.@event.message;
							EntryEvent.Type = InTestEntry.@event.type;
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
						string[] ArtifactPaths = { TestArtifact.Files.Difference, TestArtifact.Files.Approved, TestArtifact.Files.Unapproved };
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
						Json.Save(new FileReference(TestResultFilePath), OutputTestResultDetailed);
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
			public SimpleTestReport()
			{
				Logs = new List<String>();
				Errors = new List<String>();
				Warnings = new List<String>();
			}

			public string Description;
			public string ReportCreatedOn;
			public float TotalDurationSeconds;
			public bool HasSucceeded;
			public string Status;
			public string URLLink;
			public List<String> Logs;
			public List<String> Errors;
			public List<String> Warnings;

			public override void AddEvent(string Type, string Message, object Context = null)
			{
				if (Type.ToLower() == "error")
				{
					Errors.Add(Message);
				}
				else if (Type.ToLower() == "warning")
				{
					Warnings.Add(Message);
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
				public string Key;
				public object Data;
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
				NewDataItem.Key = InData.Type + "::" + InKey;
				NewDataItem.Data = InData;
				Items.Add(NewDataItem);
				return NewDataItem;
			}

			public List<DataItem> Items;

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
					Json.Save(new FileReference(OutputTestDataFilePath), this);
				}
				catch (Exception Ex)
				{
					Log.Error("Failed to save Test Data Collection for Horde. {0}", Ex);
				}
			}
		}
	}
}
