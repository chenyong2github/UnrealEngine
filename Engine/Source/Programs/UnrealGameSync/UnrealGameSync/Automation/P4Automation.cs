// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;

namespace UnrealGameSync
{

	static class P4Automation
	{

		public static PerforceConnection GetConnection(out string ErrorMessage)
		{
			ErrorMessage = null;

			// Read the settings
			string ServerAndPort = null;
			string UserName = null;
			string DepotPathSettings = null;

			Utility.ReadGlobalPerforceSettings(ref ServerAndPort, ref UserName, ref DepotPathSettings);

			if (string.IsNullOrEmpty(UserName))
			{
				ErrorMessage = "Unable to get UGS perforce username from registry";
				return null;
			}

			if (string.IsNullOrEmpty(ServerAndPort))
			{
				ServerAndPort = "perforce:1666";
			}

			return new PerforceConnection(UserName, null, ServerAndPort);

		}

		public static bool PrintToTempFile(PerforceConnection Connection, string DepotPath, out string TempFileName, out string ErrorMessage)
		{
			TempFileName = null;
			ErrorMessage = null;

			if (Connection == null)
			{
				Connection = GetConnection(out ErrorMessage);
				if (Connection == null)
				{
					return false;
				}
			}

			BufferedTextWriter Log = new BufferedTextWriter();
			string DepotFileName = Path.GetFileName(DepotPath);

			// Reorder CL and extension
			int Index = DepotFileName.IndexOf('@');
			if (Index == -1)
			{
				DepotFileName += "@Latest";
				Index = DepotFileName.IndexOf('@');
			}

			string CL = DepotFileName.Substring(Index + 1);
			string FileName = DepotFileName.Substring(0, Index);
			TempFileName = string.Format("{0}@{1}{2}", Path.GetFileNameWithoutExtension(FileName), CL, Path.GetExtension(FileName));

			TempFileName = Path.Combine(Path.GetTempPath(), TempFileName);

			if (!Connection.PrintToFile(DepotPath, TempFileName, Log) || !File.Exists(TempFileName))
			{
				ErrorMessage = string.Format("Unable to p4 print file {0}\\n{1}\\n", DepotPath, string.Join("\n", Log.Lines));
				return false;
			}		

			return true;

		}

	}

}