// Copyright Epic Games, Inc. All Rights Reserved.
using System;
using System.Reflection;
using System.IO;
using System.Linq;
using System.Diagnostics;
using System.Dynamic;

namespace AutomationToolLauncher
{
	class Launcher
	{
		static int Main(string[] Arguments)
		{
			// net core does not support shadow copying so we just have to run the executable
#if !NET_CORE
			if (Arguments.Contains("-compile", StringComparer.OrdinalIgnoreCase))
			{
				return RunInAppDomain(Arguments);
			}
#endif
			return Run(Arguments);

		}
#if !NET_CORE
		static int RunInAppDomain(string[] Arguments)
		{
			// Create application domain setup information.
			AppDomainSetup Domaininfo = new AppDomainSetup();
			Domaininfo.ApplicationBase = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
			Domaininfo.ShadowCopyFiles = "true";

			// Create the application domain.			
			AppDomain Domain = AppDomain.CreateDomain("AutomationTool", AppDomain.CurrentDomain.Evidence, Domaininfo);
			// Execute assembly and pass through command line
			string UATExecutable = Path.Combine(Domaininfo.ApplicationBase, "AutomationTool.exe");
			// Default exit code in case UAT does not even start, otherwise we always return UAT's exit code.
			int ExitCode = 193;

			try
			{
				ExitCode = Domain.ExecuteAssembly(UATExecutable, Arguments);
				// Unload the application domain.
				AppDomain.Unload(Domain);
			}
			catch (Exception Ex)
			{
				Console.WriteLine(Ex.Message);
				Console.WriteLine(Ex.StackTrace);

				// We want to terminate the launcher process regardless of any crash dialogs, threads, etc
				Environment.Exit(ExitCode);
			}

			return ExitCode;
		}
#endif

		static int Run(string[] Arguments)
		{

			string ApplicationBase = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
#if NET_CORE
			string UATExecutable = Path.Combine(ApplicationBase, "..\\AutomationTool", "AutomationTool.exe");
#else
			string UATExecutable = Path.Combine(ApplicationBase, "AutomationTool.exe");
#endif

			if (!File.Exists(UATExecutable))
			{
				Console.WriteLine(string.Format("AutomationTool does not exist at: {0}", UATExecutable));
				return -1;
			}

			try
			{
#if NET_CORE
				ProcessStartInfo StartInfo = new ProcessStartInfo(UATExecutable);
				foreach (string s in Arguments)
				{
					StartInfo.ArgumentList.Add(s);
				}
				Process uatProcess = Process.Start(StartInfo);
				uatProcess.WaitForExit();
				Environment.Exit(uatProcess.ExitCode);
#else
				Assembly UAT = Assembly.LoadFile(UATExecutable);
				Environment.Exit((int) UAT.EntryPoint.Invoke(null, new object[] { Arguments }));
#endif
			}
			catch (Exception Ex)
			{
				Console.WriteLine(Ex.Message);
				Console.WriteLine(Ex.StackTrace);
			}

			return -1;

		}
	}
}
