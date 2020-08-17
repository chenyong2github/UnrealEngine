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
			if (Arguments.Contains("-compile", StringComparer.OrdinalIgnoreCase))
			{
				return RunInAppDomain(Arguments);
			}

			return Run(Arguments);

		}

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

		static int Run(string[] Arguments)
		{

			string ApplicationBase = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
			string UATExecutable = Path.Combine(ApplicationBase, "AutomationTool.exe");

			if (!File.Exists(UATExecutable))
			{
				Console.WriteLine(string.Format("AutomationTool does not exist at: {0}", UATExecutable));
				return -1;
			}

			try
			{
				Assembly UAT = Assembly.LoadFile(UATExecutable);
				Environment.Exit((int) UAT.EntryPoint.Invoke(null, new object[] { Arguments }));
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
