// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealGameSync
{

	/// <summary>
	/// P4 URI handler
	/// </summary>
	static class P4Handler
	{
		[UriHandler(true)]
		public static UriResult Timelapse(string DepotPath, int Line = -1)
		{
			string CommandLine = string.Format("timelapse {0}{1}", Line == -1 ? "" : string.Format(" -l {0} ", Line), DepotPath);

			if (!Utility.SpawnHiddenProcess("p4vc.exe", CommandLine))
			{
				return new UriResult() { Error = string.Format("Error spawning p4vc.exe with command line: {0}", CommandLine) };
			}

			return new UriResult() { Success = true };
		}		
	}
}