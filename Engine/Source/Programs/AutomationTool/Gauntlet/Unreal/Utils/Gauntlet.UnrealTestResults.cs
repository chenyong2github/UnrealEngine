// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using Tools.DotNETCommon;

namespace Gauntlet
{
	// The following classes are used for json deserialization of the Unreal Automation Test Results.
	// Because of a bug in the json deserialization used here(Tools.DotNETCommon.Json),
	// the names of the properties need to match the case of the json properties.

	public class UnrealAutomationComparisonFiles
	{
		public string difference;
		public string approved;
		public string unapproved;
	}
	public class UnrealAutomationArtifact
	{
		public string id;
		public string name;
		public string type;
		public UnrealAutomationComparisonFiles files;
	}
	public class UnrealAutomationEvent
	{
		public string type;
		public string message;
		public string context;
		public string artifact;
	}
	public class UnrealAutomationEntry
	{
		public UnrealAutomationEvent @event;
		public string filename;
		public int lineNumber;
		public string timestamp;
	}
	public class UnrealAutomatedTestResult
	{
		public string testDisplayName;
		public string fullTestPath;
		public string artifactName;
		public string state;
		public int warnings;
		public int errors;
		public List<UnrealAutomationArtifact> artifacts;
		public List<UnrealAutomationEntry> entries;
	}
	public class UnrealAutomatedTestPassResults
	{
		public string clientDescriptor;
		public string reportCreatedOn;
		public int succeeded;
		public int succeededWithWarnings;
		public int failed;
		public int notRun;
		public int inProcess;
		public float totalDuration;
		public bool comparisonExported;
		public string comparisonExportDirectory;
		public List<UnrealAutomatedTestResult> tests;

		/// <summary>
		/// Load Unreal Automated Test Results from json report
		/// </summary>
		/// <param name="FilePath"></param>
		public static UnrealAutomatedTestPassResults LoadFromJson(string FilePath)
		{
			UnrealAutomatedTestPassResults JsonTestPassResults = Json.Load<UnrealAutomatedTestPassResults>(new FileReference(FilePath));
			return JsonTestPassResults;
		}
	}
}
