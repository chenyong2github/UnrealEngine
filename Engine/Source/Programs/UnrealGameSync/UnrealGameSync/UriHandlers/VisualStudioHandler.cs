// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using EnvDTE;

namespace UnrealGameSync
{

	/// <summary>
	/// VisualStudio Uri Handler
	/// </summary>
	static class VisualStudioHandler
	{
		[UriHandler(true)]
		public static UriResult VSOpen(string DepotPath, int Line = -1)
		{
			string ErrorMessage;
			string TempFileName;

			if (!P4Automation.PrintToTempFile(null, DepotPath, out TempFileName, out ErrorMessage))
			{
				return new UriResult() { Error = ErrorMessage ?? "Unknown P4 Error" };
			}

			if (!VisualStudioAutomation.OpenFile(TempFileName, out ErrorMessage, Line))
			{
				return new UriResult() { Error = ErrorMessage ?? "Unknown Visual Studio Error" };
			}

			return new UriResult() { Success = true };
		}

	}

}