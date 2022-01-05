// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using System.Threading;
using System.Threading.Tasks;
using EnvDTE;
using Microsoft.Extensions.Logging.Abstractions;

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
			string TempFileName = P4Automation.PrintToTempFile(null, DepotPath, NullLogger.Instance).GetAwaiter().GetResult();

			string? ErrorMessage;
			if (!VisualStudioAutomation.OpenFile(TempFileName, out ErrorMessage, Line))
			{
				return new UriResult() { Error = ErrorMessage ?? "Unknown Visual Studio Error" };
			}

			return new UriResult() { Success = true };
		}

	}

}