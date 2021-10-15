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
	private static string GeneratedPropertiesScriptFile = null;

	private static XNamespace xmlns = XNamespace.Get("http://www.epicgames.com/BuildGraph");
	private static XNamespace xsi = XNamespace.Get("http://www.w3.org/2001/XMLSchema-instance");
	private static XNamespace schemaLocation = XNamespace.Get("http://www.epicgames.com/BuildGraph ../../Build/Graph/Schema.xsd");

	public LowLevelTests(ReadOnlyTargetRules Target) : base(Target)
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

	/// <summary>
	/// Generates or updates include file for LowLevelTests.xml containing test flags: name, short name, target name, relative binaries path, supported platforms etc.
	/// Static for now until HeadlessChaos can inherit from LowLevelTests.	
	/// </summary>
	/// <param name="ModuleClassType">Type of test build module class that inherits from LowLevelTests.</param>
	/// <param name="TestName">Name of low level test.</param>
	/// <param name="TestShortName">Test short name.</param>
	/// <param name="TestTargetName">Test target executable name.</param>
	/// <param name="BinariesPath">Test binaries path.</param>
	/// <param name="ResourcesPath">Optional test resources to be copied.</param>
	public static void UpdateGeneratedPropertiesScriptFile(Type ModuleClassType, string TestName, string TestShortName, string TestTargetName, string BinariesPath, string ResourcesPath = "")
	{
		if (GeneratedPropertiesScriptFile == null)
		{
			GeneratedPropertiesScriptFile = Path.Combine(Unreal.EngineDirectory.FullName, "Restricted", "NotForLicensees", "Build", "LowLevelTests_GenProps.xml");
		}

		if (!File.Exists(GeneratedPropertiesScriptFile))
		{
			Directory.CreateDirectory(Path.GetDirectoryName(GeneratedPropertiesScriptFile));
			using (FileStream fileStream = File.Create(GeneratedPropertiesScriptFile))
			{
				XDocument initFile = new XDocument(new XElement(xmlns + "BuildGraph", new XAttribute(XNamespace.Xmlns + "xsi", xsi), new XAttribute(xsi + "schemaLocation", schemaLocation)));
				initFile.Root.Add(new XElement(xmlns + "Property", new XAttribute("Name", "TestNames"), new XAttribute("Value", string.Empty)));

				initFile.Save(fileStream);
			}
		}
		XDocument genPropsDoc = XDocument.Load(GeneratedPropertiesScriptFile);
		XElement root = genPropsDoc.Root;
		// First descendant must be TestNames
		XElement testNames = (XElement)root.FirstNode;
		string allTestNames = testNames.Attribute("Value").Value;
		if (!allTestNames.Contains(TestName))
		{
			if (string.IsNullOrEmpty(allTestNames))
			{
				allTestNames += TestName;
			}
			else if (!allTestNames.Contains(TestName))
			{
				allTestNames += ";" + TestName;
			}
		}
		testNames.Attribute("Value").SetValue(allTestNames);

		XElement lastUpdatedNode = testNames;
		InsertOrUpdateTestFlag(ref lastUpdatedNode, TestName, "Short", TestShortName);
		InsertOrUpdateTestFlag(ref lastUpdatedNode, TestName, "Target", TestTargetName);

		if (Path.IsPathRooted(BinariesPath))
		{
			BinariesPath = Path.GetRelativePath(Unreal.RootDirectory.FullName, BinariesPath);
		}
		InsertOrUpdateTestFlag(ref lastUpdatedNode, TestName, "BinariesRelative", BinariesPath);

		if (Path.IsPathRooted(ResourcesPath))
		{
			ResourcesPath = Path.GetRelativePath(Unreal.RootDirectory.FullName, ResourcesPath);
		}
		InsertOrUpdateTestFlag(ref lastUpdatedNode, TestName, "Resources", ResourcesPath);
		InsertOrUpdateTestFlag(ref lastUpdatedNode, TestName, "ResourcesDest", Path.GetFileName(ResourcesPath));

		InsertOrUpdateTestOption(ref lastUpdatedNode, TestName, TestShortName, "Run", "Tests", false.ToString());

		List<UnrealTargetPlatform> allSupportedPlatforms = new List<UnrealTargetPlatform>();
		var supportedPlatforms = ModuleClassType.GetCustomAttributes(typeof(SupportedPlatformsAttribute), false);
		// If none specified we assume all platforms are supported by default
		if (supportedPlatforms.Length == 0)
		{
			allSupportedPlatforms.AddRange(UnrealTargetPlatform.GetValidPlatforms().ToList());
		}
		else
		{
			foreach (var platform in supportedPlatforms)
			{
				allSupportedPlatforms.AddRange(((SupportedPlatformsAttribute)platform).Platforms);
			}
		}

		InsertOrUpdateTestFlag(ref lastUpdatedNode, TestName, "SupportedPlatforms", allSupportedPlatforms.Aggregate("", (current, next) => (current == "" ? next.ToString() : current + ";" + next.ToString())));

		try
		{
			genPropsDoc.Save(GeneratedPropertiesScriptFile);
		}
		catch (UnauthorizedAccessException)
		{
			// Expected on build machines.
			// TODO: Ability to build for generate files and runnable tests.
		}
	}

	private static void InsertOrUpdateTestFlag(ref XElement ElementUpsertAfter, string TestName, string FlagSuffix, string FlagValue)
	{
		IEnumerable<XElement> NextChunk = ElementUpsertAfter.ElementsAfterSelf(xmlns + "Property")
			.Where(prop => prop.Attribute("Name").Value.EndsWith(FlagSuffix));
		if (NextChunk
			.Where(prop => prop.Attribute("Name").Value == TestName + FlagSuffix)
			.Count() == 0)
		{
			XElement ElementInsert = new XElement(xmlns + "Property");
			ElementInsert.SetAttributeValue("Name", TestName + FlagSuffix);
			ElementInsert.SetAttributeValue("Value", FlagValue);
			ElementUpsertAfter.AddAfterSelf(ElementInsert);
		} else
		{
			NextChunk
				.Where(prop => prop.Attribute("Name").Value == TestName + FlagSuffix).First().SetAttributeValue("Value", FlagValue);
		}
		ElementUpsertAfter = NextChunk.Last();
	}

	private static void InsertOrUpdateTestOption(ref XElement ElementUpsertAfter, string TestName, string TestShortName, string OptionPrefix, string OptionSuffix, string DefaultValue)
	{
		IEnumerable<XElement> NextChunk = ElementUpsertAfter.ElementsAfterSelf(xmlns + "Option")
			.Where(prop => prop.Attribute("Name").Value.StartsWith(OptionPrefix) && prop.Attribute("Name").Value.EndsWith(OptionSuffix));
		if (NextChunk
			.Where(prop => prop.Attribute("Name").Value == OptionPrefix + TestName + OptionSuffix)
			.Count() == 0)
		{
			XElement ElementInsert = new XElement(xmlns + "Option");
			ElementInsert.SetAttributeValue("Name", OptionPrefix + TestName + OptionSuffix);
			ElementInsert.SetAttributeValue("DefaultValue", DefaultValue);
			ElementInsert.SetAttributeValue("Description", string.Format("{0} {1} {2}", OptionPrefix, TestShortName, OptionSuffix));
			ElementUpsertAfter.AddAfterSelf(ElementInsert);
		} else
		{
			XElement ElementUpdate = NextChunk
				.Where(prop => prop.Attribute("Name").Value == OptionPrefix + TestName + OptionSuffix).First();
			ElementUpdate.SetAttributeValue("Description", string.Format("{0} {1} {2}", OptionPrefix, TestShortName, OptionSuffix));
			ElementUpdate.SetAttributeValue("DefaultValue", DefaultValue);
		}
		ElementUpsertAfter = NextChunk.Last();
	}
}