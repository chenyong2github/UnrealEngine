// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Collections.Generic;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Linq;

namespace Gauntlet
{
	public class UnrealAutomationDevice
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
	public class UnrealAutomationComparisonFiles
	{
		public string Difference { get; set; }
		public string Approved { get; set; }
		public string Unapproved { get; set; }
	}
	public class UnrealAutomationArtifact
	{
		public string Id { get; set; }
		public string Name { get; set; }
		public string Type { get; set; }
		public UnrealAutomationComparisonFiles Files { get; set; }
	}
	public class UnrealAutomationEvent
	{
		public EventType Type { get; set; }
		public string Message { get; set; }
		public string Context { get; set; }
		public string Artifact { get; set; }

		public UnrealAutomationEvent()
		{ }
		public UnrealAutomationEvent(EventType InType, string InMessage)
		{
			Type = InType;
			Message = InMessage;
		}

		[JsonIgnore]
		public bool IsError
		{
			get
			{
				return Type == EventType.Error;
			}

		}

		[JsonIgnore]
		public bool IsWarning
		{
			get
			{
				return Type == EventType.Warning;
			}

		}
	}
	public class UnrealAutomationEntry
	{
		public UnrealAutomationEvent Event { get; set; }
		public string Filename { get; set; }
		public int LineNumber { get; set; }
		public string Timestamp { get; set; }
	}
	public class UnrealAutomatedTestResult
	{
		public string TestDisplayName { get; set; }
		public string FullTestPath { get; set; }
		public string ArtifactName { get; set; }
		public TestStateType State { get; set; }
		public String DeviceInstance { get; set; }
		public int Warnings { get; set; }
		public int Errors { get; set; }
		public List<UnrealAutomationArtifact> Artifacts { get; set; }
		public List<UnrealAutomationEntry> Entries { get; set; }

		public UnrealAutomatedTestResult()
		{
			Artifacts = new List<UnrealAutomationArtifact>();
			Entries = new List<UnrealAutomationEntry>();
		}

		public void AddEvent(EventType EventType, string Message)
		{
			var Event = new UnrealAutomationEvent(EventType, Message);
			var Entry = new UnrealAutomationEntry();
			Entry.Event = Event;
			Entry.Timestamp = DateTime.Now.ToString("yyyy.MM.dd-HH.mm.ss");
			Entries.Add(Entry);

			switch(EventType)
			{
				case EventType.Error:
					Errors++;
					break;
				case EventType.Warning:
					Warnings++;
					break;
			}
		}
		public void AddError(string Message)
		{
			AddEvent(EventType.Error, Message);
		}
		public void AddWarning(string Message)
		{
			AddEvent(EventType.Warning, Message);
		}
		public void AddInfo(string Message)
		{
			AddEvent(EventType.Info, Message);
		}

		[JsonIgnore]
		public bool IsComplete
		{
			get
			{
				return State != TestStateType.InProcess && State != TestStateType.NotRun;
			}
		}
		[JsonIgnore]
		public bool HasSucceeded
		{
			get
			{
				return State == TestStateType.Success;
			}
		}
		[JsonIgnore]
		public bool HasFailed
		{
			get
			{
				return State == TestStateType.Fail;
			}
		}
		[JsonIgnore]
		public bool WasSkipped
		{
			get
			{
				return State == TestStateType.Skipped;
			}
		}
		[JsonIgnore]
		public bool HasWarnings
		{
			get
			{
				return Warnings > 0;
			}
		}
		[JsonIgnore]
		public IEnumerable<UnrealAutomationEvent> ErrorEvents
		{
			get
			{
				return Entries.Where(E => E.Event.IsError).Select(E => E.Event);
			}
		}
		[JsonIgnore]
		public IEnumerable<UnrealAutomationEvent> WarningEvents
		{
			get
			{
				return Entries.Where(E => E.Event.IsWarning).Select(E => E.Event);
			}
		}
		[JsonIgnore]
		public IEnumerable<UnrealAutomationEvent> WarningAndErrorEvents
		{
			get
			{
				return Entries.Where(E => E.Event.IsError || E.Event.IsWarning).Select(E => E.Event);
			}
		}
	}
	public class UnrealAutomatedTestPassResults
	{
		public List<UnrealAutomationDevice> Devices { get; set; }
		public string ReportCreatedOn { get; set; }
		public int Succeeded { get; set; }
		public int SucceededWithWarnings { get; set; }
		public int Failed { get; set; }
		public int NotRun { get; set; }
		public int InProcess { get; set; }
		public float TotalDuration { get; set; }
		public bool ComparisonExported { get; set; }
		public string ComparisonExportDirectory { get; set; }
		public List<UnrealAutomatedTestResult> Tests { get; set; }

		public UnrealAutomatedTestPassResults()
		{
			Devices = new List<UnrealAutomationDevice>();
			Tests = new List<UnrealAutomatedTestResult>();
		}

		public UnrealAutomatedTestResult AddTest(string DisplayName, string FullName, TestStateType State)
		{
			var Test = new UnrealAutomatedTestResult();
			Test.TestDisplayName = DisplayName;
			Test.FullTestPath = FullName;
			Test.State = State;

			return AddTest(Test);
		}

		public UnrealAutomatedTestResult AddTest(UnrealAutomatedTestResult Test)
		{
			Tests.Add(Test);

			switch (Test.State)
			{
				case TestStateType.Success:
					if (Test.HasWarnings)
					{
						SucceededWithWarnings++;
					}
					else
					{
						Succeeded++;
					}
					break;
				case TestStateType.Fail:
					Failed++;
					break;
				case TestStateType.NotRun:
					NotRun++;
					break;
				case TestStateType.InProcess:
					InProcess++;
					break;
			}

			return Test;
		}

		/// <summary>
		/// Load Unreal Automated Test Results from json report
		/// </summary>
		/// <param name="FilePath"></param>
		public static UnrealAutomatedTestPassResults LoadFromJson(string FilePath)
		{
			JsonSerializerOptions Options = new JsonSerializerOptions
			{
				PropertyNameCaseInsensitive = true
			};
			string JsonString = File.ReadAllText(FilePath);
			UnrealAutomatedTestPassResults JsonTestPassResults = JsonSerializer.Deserialize<UnrealAutomatedTestPassResults>(JsonString, Options);
			return JsonTestPassResults;
		}

		/// <summary>
		/// Write json data into a file
		/// </summary>
		/// <param name="FilePath"></param>
		public void WriteToJson(string FilePath)
		{
			string OutputTestDataDir = Path.GetDirectoryName(FilePath);
			if (!Directory.Exists(OutputTestDataDir))
			{
				Directory.CreateDirectory(OutputTestDataDir);
			}

			Log.Verbose("Writing Json to file {0}", FilePath);
			try
			{
				JsonSerializerOptions Options = new JsonSerializerOptions
				{
					WriteIndented = true
				};
				File.WriteAllText(FilePath, JsonSerializer.Serialize(this, Options));
			}
			catch (Exception Ex)
			{
				Log.Error("Failed to save json file. {0}", Ex);
			}
		}
	}
}
