// Copyright Epic Games, Inc. All Rights Reserved.
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Xml.Linq;
using UnrealBuildBase;
using UnrealBuildTool;

public class LowLevelTests : ModuleRules
{
	private readonly XNamespace BuildGraphNamespace = XNamespace.Get("http://www.epicgames.com/BuildGraph");
	private readonly XNamespace SchemaInstance = XNamespace.Get("http://www.w3.org/2001/XMLSchema-instance");
	private readonly XNamespace SchemaLocation = XNamespace.Get("http://www.epicgames.com/BuildGraph ../../Build/Graph/Schema.xsd");

	public virtual string TestName => throw new Exception("TestName must be overwritten in subclasses.");
	public virtual string TestShortName => throw new Exception("TestShortName must be overwritten in subclasses.");
	public virtual string ResourcesPath => string.Empty;

	public virtual bool UsesCatch2 => true;

	public virtual bool Disabled => false;

	public LowLevelTests(ReadOnlyTargetRules Target) : base(Target)
	{
		if (UsesCatch2)
		{
			PCHUsage = PCHUsageMode.NoPCHs;
			PrecompileForTargets = PrecompileTargetsType.None;

			if (Target.Configuration == UnrealTargetConfiguration.Debug && Target.Platform == UnrealTargetPlatform.Linux)
			{
				OptimizeCode = CodeOptimization.Never;
			}

			bAllowConfidentialPlatformDefines = true;
			bLegalToDistributeObjectCode = true;

			// Required false for catch.hpp
			bUseUnity = false;

			// Disable exception handling so that tests can assert for exceptions
			bEnableObjCExceptions = false;
			bEnableExceptions = false;

			PrivateDependencyModuleNames.AddRange(
				new string[] {
				"Core",
				"Projects",
				"LowLevelTestsRunner"
				});

			PrivateIncludePaths.Add("Runtime/Launch/Private");
			PrivateIncludePathModuleNames.Add("Launch");

			// Platforms specific setup
			if (Target.Platform == UnrealTargetPlatform.Android)
			{
				PublicDefinitions.Add("CATCH_CONFIG_NOSTDOUT");
			}
		}

		if (this.GetType() != typeof(LowLevelTests))
		{
			UpdateBuildGraphPropertiesFile();
		}
	}

	/// <summary>
	/// Generates or updates include file for LowLevelTests.xml containing test flags: name, short name, target name, relative binaries path, supported platforms etc.
	/// <paramref name="TestModule">The test module build class that inherits form LowLevelTests</paramref>
	/// </summary>
	private void UpdateBuildGraphPropertiesFile()
	{
		bool IsPublic = false;
		string GeneratedPropertiesScriptFile;

		string RestrictedFolder = Path.Combine(Unreal.EngineDirectory.FullName, "Restricted");
		string NotForLicenseesFolder = Path.Combine(RestrictedFolder, "NotForLicensees");
		string NonPublicFolder = Path.Combine(NotForLicenseesFolder, "Build");
		string NonPublicPath = Path.Combine(NonPublicFolder, "LowLevelTests_GenProps.xml");

		if (IsRestrictedPath(ModuleDirectory))
		{
			GeneratedPropertiesScriptFile = NonPublicPath;
		}
		else
		{
			IsPublic = true;
			GeneratedPropertiesScriptFile = Path.Combine(Unreal.EngineDirectory.FullName, "Build", "LowLevelTests_GenProps.xml");
		}

		// UE-133126
		if (File.Exists(NonPublicPath) && Directory.GetFileSystemEntries(NonPublicFolder).Length == 1)
		{
			File.Delete(NonPublicPath);
			Directory.Delete(NonPublicFolder);
			Directory.Delete(NotForLicenseesFolder);
			Directory.Delete(RestrictedFolder);
		}

		if (!File.Exists(GeneratedPropertiesScriptFile))
		{
			Directory.CreateDirectory(Path.GetDirectoryName(GeneratedPropertiesScriptFile));
			using (FileStream fileStream = File.Create(GeneratedPropertiesScriptFile))
			{
				XDocument initFile = new XDocument(new XElement(BuildGraphNamespace + "BuildGraph", new XAttribute(XNamespace.Xmlns + "xsi", SchemaInstance), new XAttribute(SchemaInstance + "schemaLocation", SchemaLocation)));
				initFile.Root.Add(
					new XElement(
						BuildGraphNamespace + "Property",
						new XAttribute("Name", "TestNames" + (!IsPublic ? "Restricted" : "")),
						new XAttribute("Value", string.Empty)));

				initFile.Save(fileStream);
			}
		}

		// All relevant properties
		string TestTargetName = Target.LaunchModuleName;
		string TestBinariesPath = TryGetBinariesPath();
		string TestResourcesPath = ResourcesPath;

		// Do not save full paths
		if (Path.IsPathRooted(TestBinariesPath))
		{
			TestBinariesPath = Path.GetRelativePath(Unreal.RootDirectory.FullName, TestBinariesPath);
		}
		if (Path.IsPathRooted(ResourcesPath))
		{
			TestResourcesPath = Path.GetRelativePath(Unreal.RootDirectory.FullName, ResourcesPath);
		}

		XDocument GenPropsDoc = XDocument.Load(GeneratedPropertiesScriptFile);
		XElement Root = GenPropsDoc.Root;
		// First descendant must be TestNames
		XElement TestNames = (XElement)Root.FirstNode;
		string AllTestNames = TestNames.Attribute("Value").Value;
		if (!AllTestNames.Contains(TestName))
		{
			if (string.IsNullOrEmpty(AllTestNames))
			{
				AllTestNames += TestName;
			}
			else if (!AllTestNames.Contains(TestName))
			{
				AllTestNames += ";" + TestName;
			}
		}
		TestNames.Attribute("Value").SetValue(AllTestNames);

		XElement lastUpdatedNode = TestNames;
		InsertOrUpdateTestFlag(ref lastUpdatedNode, TestName, "Disabled", Disabled.ToString());
		InsertOrUpdateTestFlag(ref lastUpdatedNode, TestName, "Short", TestShortName);
		InsertOrUpdateTestFlag(ref lastUpdatedNode, TestName, "Target", TestTargetName);
		InsertOrUpdateTestFlag(ref lastUpdatedNode, TestName, "BinariesRelative", TestBinariesPath);
		InsertOrUpdateTestFlag(ref lastUpdatedNode, TestName, "Resources", TestResourcesPath);
		InsertOrUpdateTestFlag(ref lastUpdatedNode, TestName, "ResourcesDest", Path.GetFileName(TestResourcesPath));

		InsertOrUpdateTestOption(ref lastUpdatedNode, TestName, TestShortName, "Run", "Tests", false.ToString());

		List<UnrealTargetPlatform> AllSupportedPlatforms = new List<UnrealTargetPlatform>();
		var SupportedPlatforms = GetType().GetCustomAttributes(typeof(SupportedPlatformsAttribute), false);
		// If none specified we assume all platforms are supported by default
		if (SupportedPlatforms.Length == 0)
		{
			AllSupportedPlatforms.AddRange(UnrealTargetPlatform.GetValidPlatforms().ToList());
		}
		else
		{
			foreach (var Platform in SupportedPlatforms)
			{
				AllSupportedPlatforms.AddRange(((SupportedPlatformsAttribute)Platform).Platforms);
			}
		}

		InsertOrUpdateTestFlag(ref lastUpdatedNode, TestName, "SupportedPlatforms", AllSupportedPlatforms.Aggregate("", (current, next) => (current == "" ? next.ToString() : current + ";" + next.ToString())));

		try
		{
			GenPropsDoc.Save(GeneratedPropertiesScriptFile);
		}
		catch (UnauthorizedAccessException)
		{
			// Expected on build machines.
			// TODO: Ability to build for generate files and runnable tests.
		}
	}

	private bool IsRestrictedPath(string ModuleDirectory)
	{
		foreach(string RestrictedFolderName in RestrictedFolder.GetNames())
		{
			if (ModuleDirectory.Contains(RestrictedFolderName))
			{
				return true;
			}
		}

		return false;
	}

	private string TryGetBinariesPath()
	{
		int SourceFolderIndex = ModuleDirectory.IndexOf("Source");
		if (SourceFolderIndex < 0)
		{
			throw new Exception("Could not detect source folder path for module " + GetType());
		}
		return ModuleDirectory.Substring(0, SourceFolderIndex) + "Binaries";
	}

	private void InsertOrUpdateTestFlag(ref XElement ElementUpsertAfter, string TestName, string FlagSuffix, string FlagValue)
	{
		IEnumerable<XElement> NextChunk = ElementUpsertAfter.ElementsAfterSelf(BuildGraphNamespace + "Property")
			.Where(prop => prop.Attribute("Name").Value.EndsWith(FlagSuffix));
		if (NextChunk
			.Where(prop => prop.Attribute("Name").Value == TestName + FlagSuffix)
			.Count() == 0)
		{
			XElement ElementInsert = new XElement(BuildGraphNamespace + "Property");
			ElementInsert.SetAttributeValue("Name", TestName + FlagSuffix);
			ElementInsert.SetAttributeValue("Value", FlagValue);
			ElementUpsertAfter.AddAfterSelf(ElementInsert);
		}
		else
		{
			NextChunk
				.Where(prop => prop.Attribute("Name").Value == TestName + FlagSuffix).First().SetAttributeValue("Value", FlagValue);
		}
		ElementUpsertAfter = NextChunk.Last();
	}

	private void InsertOrUpdateTestOption(ref XElement ElementUpsertAfter, string TestName, string TestShortName, string OptionPrefix, string OptionSuffix, string DefaultValue)
	{
		IEnumerable<XElement> NextChunk = ElementUpsertAfter.ElementsAfterSelf(BuildGraphNamespace + "Option")
			.Where(prop => prop.Attribute("Name").Value.StartsWith(OptionPrefix) && prop.Attribute("Name").Value.EndsWith(OptionSuffix));
		if (NextChunk
			.Where(prop => prop.Attribute("Name").Value == OptionPrefix + TestName + OptionSuffix)
			.Count() == 0)
		{
			XElement ElementInsert = new XElement(BuildGraphNamespace + "Option");
			ElementInsert.SetAttributeValue("Name", OptionPrefix + TestName + OptionSuffix);
			ElementInsert.SetAttributeValue("DefaultValue", DefaultValue);
			ElementInsert.SetAttributeValue("Description", string.Format("{0} {1} {2}", OptionPrefix, TestShortName, OptionSuffix));
			ElementUpsertAfter.AddAfterSelf(ElementInsert);
		}
		else
		{
			XElement ElementUpdate = NextChunk
				.Where(prop => prop.Attribute("Name").Value == OptionPrefix + TestName + OptionSuffix).First();
			ElementUpdate.SetAttributeValue("Description", string.Format("{0} {1} {2}", OptionPrefix, TestShortName, OptionSuffix));
			ElementUpdate.SetAttributeValue("DefaultValue", DefaultValue);
		}
		ElementUpsertAfter = NextChunk.Last();
	}
}