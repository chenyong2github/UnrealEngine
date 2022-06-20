// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.Perforce;
using Microsoft.Extensions.Logging;
using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;

namespace UnrealGameSync
{

	static class P4Automation
	{

		public static IPerforceSettings GetConnectionSettings()
		{
			// Read the settings
			string? ServerAndPort = null;
			string? UserName = null;
			string? DepotPathSettings = null;
			bool bPreview = false;

			GlobalPerforceSettings.ReadGlobalPerforceSettings(ref ServerAndPort, ref UserName, ref DepotPathSettings, ref bPreview);

			return Utility.OverridePerforceSettings(PerforceSettings.Default, ServerAndPort, UserName);

		}

		public static Task<string> PrintToTempFile(IPerforceConnection? Connection, string DepotPath, ILogger Logger)
		{
			return PrintToTempFileAsync(Connection, DepotPath, CancellationToken.None, Logger);
		}

		public static async Task<string> PrintToTempFileAsync(IPerforceConnection? Connection, string DepotPath, CancellationToken CancellationToken, ILogger Logger)
		{
			bool bCreateNewConnection = (Connection == null);

			try
			{
				if (Connection == null)
				{
					IPerforceSettings Settings = GetConnectionSettings();
					Connection = await PerforceConnection.CreateAsync(Logger);
				}

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
				string TempFileName = string.Format("{0}@{1}{2}", Path.GetFileNameWithoutExtension(FileName), CL, Path.GetExtension(FileName));

				TempFileName = Path.Combine(Path.GetTempPath(), TempFileName);
				await Connection.PrintAsync(TempFileName, DepotFileName, CancellationToken);

				return TempFileName;
			}
			finally
			{
				// If we created a new connection, tear it down now.
				if (bCreateNewConnection)
				{
					Connection?.Dispose();
					Connection = null;
				}
			}
		}

	}

}