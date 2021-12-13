// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Text;
using System.Threading;

namespace UnrealBuildBase
{
	public class ProcessSingleton
	{
		public static string GetUniqueMutexForPath(string Name, string UniquePath)
		{
			// generate a md5 hash of the path, as GetHashCode is not guaranteed to generate a stable hash
			return string.Format("Global\\{0}_{1}", Name, ContentHash.MD5(UniquePath.ToUpperInvariant()));
		}

		/// <summary>
		/// Returns true/false based on whether this is the only instance
		/// running (checked at startup).
		/// </summary>
		public static bool IsSoleInstance { get; private set; }

		/// <summary>
		/// Runs the specified delegate checking if this is the only instance of the application.
		/// </summary>
		/// <param name="Main"></param>
		/// <param name="Param"></param>
		public static ExitCode RunSingleInstance(Func<ExitCode> Main, bool bWaitForUATMutex)
		{
			bool AllowMultipleInsances = (Environment.GetEnvironmentVariable("uebp_UATMutexNoWait") == "1");
	
            string EntryAssemblyLocation = Assembly.GetEntryAssembly()!.GetOriginalLocation();

			string MutexName = GetUniqueMutexForPath(Path.GetFileNameWithoutExtension(EntryAssemblyLocation), EntryAssemblyLocation);
			using (Mutex SingleInstanceMutex = new Mutex(true, MutexName, out bool bCreatedMutex))
			{
				IsSoleInstance = bCreatedMutex;

				if (!IsSoleInstance && AllowMultipleInsances == false)
				{
					if (bWaitForUATMutex)
					{
						Log.TraceWarning("Another instance of UAT at '{0}' is running, and the -WaitForUATMutex parameter has been used. Waiting for other UAT to finish...", EntryAssemblyLocation);
						int Seconds = 0;
						while (WaitMutexNoExceptions(SingleInstanceMutex, 15 * 1000) == false)
						{
							Seconds += 15;
							Log.TraceInformation("Still waiting for Mutex. {0} seconds has passed...", Seconds);
						}
					}
					else
					{
						throw new Exception($"A conflicting instance of AutomationTool is already running. Current location: {EntryAssemblyLocation}. A process manager may be used to determine the conflicting process and what tool may have launched it");
					}
				}

				ExitCode Result = Main();

				if (IsSoleInstance)
				{
					SingleInstanceMutex.ReleaseMutex();
				}

				return Result;
			}
		}

		static bool WaitMutexNoExceptions(Mutex Mutex, int TimeoutMs)
		{
			try
			{
				return Mutex.WaitOne(15 * 1000);
			}
			catch (AbandonedMutexException)
			{
				return true;
			}
		}
	}
}
