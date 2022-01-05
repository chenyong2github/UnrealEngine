// Copyright Epic Games, Inc. All Rights Reserved.

namespace UnrealGameSync
{

	/// <summary>
	/// P4 URI handler
	/// </summary>
	static class P4Handler
	{
		[UriHandler(true)]
		public static UriResult P4V(string DepotPath)
		{
			string CommandLine = string.Format("-s \"{0}\"", DepotPath);

			if (!Utility.SpawnHiddenProcess("p4v.exe", CommandLine))
			{
				return new UriResult() { Error = string.Format("Error spawning p4v.exe with command line: {0}", CommandLine) };
			}

			return new UriResult() { Success = true };
		}

		[UriHandler(true)]
		public static UriResult Timelapse(string DepotPath, int Line = -1)
		{
			string CommandLine = string.Format("timelapse {0}{1}", Line == -1 ? "" : string.Format(" -l {0} ", Line), DepotPath);

			Utility.SpawnP4VC(CommandLine);

			return new UriResult() { Success = true };
		}		
	}
}