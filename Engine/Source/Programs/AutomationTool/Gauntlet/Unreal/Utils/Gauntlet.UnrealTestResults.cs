// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using System.Collections.Generic;
using System.Text.Json;

namespace Gauntlet
{
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
		public string Type { get; set; }
		public string Message { get; set; }
		public string Context { get; set; }
		public string Artifact { get; set; }
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
		public string State { get; set; }
		public int Warnings { get; set; }
		public int Errors { get; set; }
		public List<UnrealAutomationArtifact> Artifacts { get; set; }
		public List<UnrealAutomationEntry> Entries { get; set; }
	}
	public class UnrealAutomatedTestPassResults
	{
		public string ClientDescriptor { get; set; }
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
	}
}
