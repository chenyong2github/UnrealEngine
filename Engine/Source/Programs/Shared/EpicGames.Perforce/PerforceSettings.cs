// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Win32;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;

namespace EpicGames.Perforce
{
	/// <summary>
	/// Settings for a new Perforce connection
	/// </summary>
	public interface IPerforceSettings
	{
		/// <summary>
		/// Server and port to connect to
		/// </summary>
		public string ServerAndPort { get; }

		/// <summary>
		/// Username to log in with
		/// </summary>
		public string UserName { get; }

		/// <summary>
		/// Password to use
		/// </summary>
		public string? Password { get; }

		/// <summary>
		/// Name of the client to use
		/// </summary>
		public string? ClientName { get; }

		/// <summary>
		/// The invoking application name
		/// </summary>
		public string? AppName { get; }

		/// <summary>
		/// The invoking application version
		/// </summary>
		public string? AppVersion { get; }

		/// <summary>
		/// Whether to create a native client rather than running the p4 child process, if possible
		/// </summary>
		public bool PreferNativeClient { get; }
	}

	/// <summary>
	/// Settings for a new Perforce connection
	/// </summary>
	public class PerforceSettings : IPerforceSettings
	{
		/// <summary>
		/// The default settings
		/// </summary>
		public static IPerforceSettings Default { get; } = new PerforceSettings(PerforceEnvironment.Default);

		/// <inheritdoc/>
		public string ServerAndPort { get; set; }

		/// <inheritdoc/>
		public string UserName { get; set; }

		/// <inheritdoc/>
		public string? Password { get; set; }

		/// <inheritdoc/>
		public string? ClientName { get; set; }

		/// <inheritdoc/>
		public string? AppName { get; set; }

		/// <inheritdoc/>
		public string? AppVersion { get; set; }

		/// <inheritdoc/>
		public bool PreferNativeClient { get; set; }

		/// <summary>
		/// Default constructor
		/// </summary>
		public PerforceSettings(IPerforceEnvironment Environment)
		{
			ServerAndPort = Environment.GetValue("P4PORT") ?? "perforce:1666";
			UserName = Environment.GetValue("P4USER") ?? System.Environment.UserName;
			Password = Environment.GetValue("P4PASSWD");
			ClientName = Environment.GetValue("P4CLIENT");

			AssemblyName EntryAssemblyName = Assembly.GetEntryAssembly()!.GetName();
			if (EntryAssemblyName.Name != null)
			{
				AppName = EntryAssemblyName.Name;
				AppVersion = EntryAssemblyName.Version?.ToString() ?? String.Empty;
			}
		}

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="ServerAndPort">Server and port to connect with</param>
		/// <param name="UserName">Username to connect with</param>
		public PerforceSettings(string ServerAndPort, string UserName)
		{
			this.ServerAndPort = ServerAndPort;
			this.UserName = UserName;
			this.AppName = Default.AppName;
			this.AppVersion = Default.AppVersion;
		}

		/// <summary>
		/// Copy constructor
		/// </summary>
		public PerforceSettings(IPerforceSettings Other)
		{
			ServerAndPort = Other.ServerAndPort;
			UserName = Other.UserName;
			Password = Other.Password;
			ClientName = Other.ClientName;
			AppName = Other.AppName;
			AppVersion = Other.AppVersion;
			PreferNativeClient = Other.PreferNativeClient;
		}

		/// <summary>
		/// Get the Perforce settings for a particular directory, reading any necessary P4CONFIG and P4ENVIRO files
		/// </summary>
		/// <param name="Directory"></param>
		/// <returns></returns>
		public static PerforceSettings FromDirectory(DirectoryReference Directory)
		{
			IPerforceEnvironment Environment = PerforceEnvironment.FromDirectory(Directory);
			return new PerforceSettings(Environment);
		}
	}

	/// <summary>
	/// Extension methods for setting objects
	/// </summary>
	public static class PerforceSettingExtensions
	{
		/// <summary>
		/// Update common fields in a IPerforceSettings object
		/// </summary>
		public static IPerforceSettings MergeWith(this IPerforceSettings Settings, string? NewServerAndPort = null, string? NewUserName = null, string? NewClientName = null)
		{
			PerforceSettings NewSettings = new PerforceSettings(Settings);
			if (!String.IsNullOrEmpty(NewServerAndPort))
			{
				NewSettings.ServerAndPort = NewServerAndPort;
			}
			if (!String.IsNullOrEmpty(NewUserName))
			{
				NewSettings.UserName = NewUserName;
			}
			if (!String.IsNullOrEmpty(NewClientName))
			{
				NewSettings.ClientName = NewClientName;
			}
			return NewSettings;
		}

		/// <summary>
		/// Gets the command line arguments to launch an external program, such as P4V or P4VC
		/// </summary>
		/// <param name="Settings"></param>
		/// <param name="bIncludeClient"></param>
		/// <returns></returns>
		public static string GetArgumentsForExternalProgram(this IPerforceSettings Settings, bool bIncludeClient)
		{
			StringBuilder BasicCommandArgs = new StringBuilder();
			
			BasicCommandArgs.AppendFormat("-p \"{0}\"", Settings.ServerAndPort);
			BasicCommandArgs.AppendFormat(" -u \"{0}\"", Settings.UserName);

			if (bIncludeClient && Settings.ClientName != null)
			{
				BasicCommandArgs.AppendFormat(" -c \"{0}\" ", Settings.ClientName);
			}

			return BasicCommandArgs.ToString();
		}

	}
}
