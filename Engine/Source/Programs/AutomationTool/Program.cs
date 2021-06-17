// Copyright Epic Games, Inc. All Rights Reserved.
// This software is provided "as-is," without any express or implied warranty. 
// In no event shall the author, nor Epic Games, Inc. be held liable for any damages arising from the use of this software.
// This software will not be supported.
// Use at your own risk.
using System;
using System.Threading;
using System.Diagnostics;
using System.Reflection;
using EpicGames.Core;
using System.IO;
using System.Collections.Generic;
using System.Text;
using UnrealBuildBase;

namespace AutomationToolDriver
{
	public class Program
	{
		// Do not add [STAThread] here. It will cause deadlocks in platform automation code.
		public static int Main(string[] Arguments)
		{
			// Wait for a debugger to be attached
			if (ParseParam(Arguments, "-WaitForDebugger"))
			{
				Console.WriteLine("Waiting for debugger to be attached...");
				while (Debugger.IsAttached == false)
				{
					Thread.Sleep(100);
				}
				Debugger.Break();
			}

			Stopwatch Timer = Stopwatch.StartNew();
			
			// Ensure UTF8Output flag is respected, since we are initializing logging early in the program.
			if (ParseParam(Arguments, "-Utf8output"))
            {
                Console.OutputEncoding = new System.Text.UTF8Encoding(false, false);
            }

			// Parse the log level argument
			if(ParseParam(Arguments, "-Verbose"))
			{
				Log.OutputLevel = LogEventType.Verbose;
			}
			if(ParseParam(Arguments, "-VeryVerbose"))
			{
				Log.OutputLevel = LogEventType.VeryVerbose;
			}

			// Initialize the log system, buffering the output until we can create the log file
			StartupTraceListener StartupListener = new StartupTraceListener();
			Trace.Listeners.Add(StartupListener);

			// Configure log timestamps
			Log.IncludeTimestamps = ParseParam(Arguments, "-Timestamps");

			// Enter the main program section
            ExitCode ReturnCode = ExitCode.Success;
			try
			{
				// Set the working directory to the UE4 root
				Environment.CurrentDirectory = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(Assembly.GetExecutingAssembly().GetOriginalLocation()), "..", "..", "..", ".."));


				// Ensure we can resolve any external assemblies as necessary.
				string PathToBinariesDotNET = Path.GetDirectoryName(Assembly.GetEntryAssembly().GetOriginalLocation());
				AssemblyUtils.InstallAssemblyResolver(PathToBinariesDotNET);
				AssemblyUtils.InstallRecursiveAssemblyResolver(PathToBinariesDotNET);

				// Log the operating environment. Since we usually compile to AnyCPU, we may be executed using different system paths under WOW64.
				Log.TraceVerbose("{2}: Running on {0} as a {1}-bit process.", RuntimePlatform.Current.ToString(), Environment.Is64BitProcess ? 64 : 32, DateTime.UtcNow.ToString("o"));

				// Log if we're running from the launcher
				string ExecutingAssemblyLocation = Assembly.GetExecutingAssembly().Location;
				if (string.Compare(ExecutingAssemblyLocation, Assembly.GetEntryAssembly().GetOriginalLocation(), StringComparison.OrdinalIgnoreCase) != 0)
				{
					Log.TraceVerbose("Executed from AutomationToolLauncher ({0})", ExecutingAssemblyLocation);
				}
				Log.TraceVerbose("CWD={0}", Environment.CurrentDirectory);

				// Log the application version
				FileVersionInfo Version = AssemblyUtils.ExecutableVersion;
				Log.TraceVerbose("{0} ver. {1}", Version.ProductName, Version.ProductVersion);

				bool bWaitForUATMutex = ParseParam(Arguments, "-WaitForUATMutex");

				// Don't allow simultaneous execution of AT (in the same branch)

				ReturnCode = ProcessSingleton.RunSingleInstance(() => MainProc(Arguments, StartupListener), bWaitForUATMutex);
			}
			catch (Exception Ex)
            {
				Log.TraceError("Exception occurred between AutomationToolDriver.Main() and Automation.Process()" + ExceptionUtils.FormatException(Ex));
            }
            finally
            {
				// Write the exit code
                Log.TraceInformation("AutomationTool executed for {0}", Timer.Elapsed.ToString("h'h 'm'm 's's'"));
                Log.TraceInformation("AutomationTool exiting with ExitCode={0} ({1})", (int)ReturnCode, ReturnCode);

                // Can't use NoThrow here because the code logs exceptions. We're shutting down logging!
                Trace.Close();
            }
            return (int)ReturnCode;
        }

		static ExitCode MainProc(string[] Arguments, StartupTraceListener StartupListener)
		{
			ExitCode Result = AutomationTool.Automation.Process(Arguments, StartupListener);
			return Result;
		}

		// Code duplicated from CommandUtils.cs
		/// <summary>
		/// Parses the argument list for a parameter and returns whether it is defined or not.
		/// </summary>
		/// <param name="ArgList">Argument list.</param>
		/// <param name="Param">Param to check for.</param>
		/// <returns>True if param was found, false otherwise.</returns>
		public static bool ParseParam(string[] ArgList, string Param)
		{
            string ValueParam = Param;
            if (!ValueParam.EndsWith("="))
            {
                ValueParam += "=";
            }

            foreach (string ArgStr in ArgList)
			{
                if (ArgStr.Equals(Param, StringComparison.InvariantCultureIgnoreCase) || ArgStr.StartsWith(ValueParam, StringComparison.InvariantCultureIgnoreCase))
				{
					return true;
				}
			}
			return false;
		}
	}
}
