// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Utils;
using Microsoft.VisualStudio.TestTools.UnitTesting;
using UnrealBuildTool.Modes;

namespace UnrealBuildToolTests
{
	[TestClass]
	public class UnrealHeaderToolTests
	{
		[TestMethod]
		public void Run()
		{
			string[] Arguments = new string[] { };
			CommandLineArguments CommandLineArguments = new CommandLineArguments(Arguments);
			UhtGlobalOptions Options = new UhtGlobalOptions(CommandLineArguments);

			// Initialize the attributes
			UhtAttributeScanner.Scan();

			// Initialize the config
			UhtConfigImpl.Read(CommandLineArguments);

			// Run the tests
			Assert.IsTrue(UhtTestHarness.RunTests(Options));
		}
	}
}
