// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading.Tasks;

namespace Gauntlet
{
	/// <summary>
	/// Result for a single automation result
	/// </summary>
	public class AutomationTestResult
	{
		/// <summary>
		/// Short friendly name of this test
		/// </summary>
		public string		DisplayName;

		/// <summary>
		/// Full test name
		/// </summary>
		public string		TestName;

		/// <summary>
		/// True if the test passed
		/// </summary>
		public bool			Passed;

		/// <summary>
		/// Events logged during the test. Should contain errors if the test failed
		/// </summary>
		public List<string> Events;

		/// <summary>
		/// Result contstructor
		/// </summary>
		/// <param name="InDisplayName"></param>
		/// <param name="InTestName"></param>
		/// <param name="bInPassed"></param>
		public AutomationTestResult(string InDisplayName, string InTestName, bool bInPassed)
		{
			DisplayName = InDisplayName;
			TestName = InTestName;
			Passed = bInPassed;
			Events = new List<string>();
		}
	};

	/// <summary>
	/// Helper class for parsing AutomationTest results from either an UnrealLogParser or log contents
	/// </summary>
	public class AutomationLogParser
	{
		protected UnrealLogParser Parser;

		/// <summary>
		/// Constructor that uses an existing log parser
		/// </summary>
		/// <param name="InParser"></param>
		public AutomationLogParser(UnrealLogParser InParser)
		{
			Parser = InParser;
		}

		/// <summary>
		/// Constructor that takes raw log contents
		/// </summary>
		/// <param name="InContents"></param>
		public AutomationLogParser(string InContents)
		{
			Parser = new UnrealLogParser(InContents);
		}
		
		/// <summary>
		/// Returns all results found in our construction content.
		/// </summary>
		/// <returns></returns>
		public IEnumerable<AutomationTestResult> GetResults()
		{
			// Find all automation results that succeeded/failed
			// [2019.04.30-18.49.51:329][244]LogAutomationController: Display: Automation Test Succeeded (ST_PR04 - Project.Functional Tests./Game/Tests/Rendering/PlanarReflection.ST_PR04)
			// [2019.04.30-18.49.51:330] [244] LogAutomationController: BeginEvents: Project.Functional Tests./Game/Tests/Rendering/PlanarReflection.ST_PR04
			// [2019.04.30 - 18.49.51:331][244] LogAutomationController: Screenshot 'ST_PR04' was similar!  Global Difference = 0.001377, Max Local Difference = 0.037953
			// [2019.04.30 - 18.49.51:332][244]LogAutomationController: EndEvents: Project.Functional Tests./Game/Tests/Rendering/PlanarReflection.ST_PR04
			IEnumerable<Match> TestMatches = Parser.GetAllMatches(@"LogAutomationController.+Test\s+(Succeeded|Failed)\s+\((.+)\s+-\s+(.+)\)");

			string[] AutomationChannel = Parser.GetLogChannel("AutomationController").ToArray();

			// Convert these lines into results by parsing out the details and then adding the events
			IEnumerable<AutomationTestResult> Results = TestMatches.Select(M =>
			{
				bool TestPassed = M.Groups[1].ToString().ToLower() == "succeeded" ? true : false;
				string DisplayName = M.Groups[2].ToString();
				string TestName = M.Groups[3].ToString();

				AutomationTestResult Result = new AutomationTestResult(DisplayName, TestName, TestPassed);

				string EventName = string.Format("BeginEvents: {0}", TestName);
				int EventIndex = Array.FindIndex(AutomationChannel, S => S.Contains(EventName)) + 1;

				if (EventIndex > 0)
				{
					while (EventIndex < AutomationChannel.Length)
					{
						string Event = AutomationChannel[EventIndex++];

						if (Event.Contains("EndEvents"))
						{
							break;
						}

						Result.Events.Add(Event);
					}
				}

				return Result;
			});

			return Results;
		}
	}
}