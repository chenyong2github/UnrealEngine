// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using Microsoft.Win32;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;

namespace EpicGames.Perforce
{
	/// <summary>
	/// Interface describing a set of perforce environment settings
	/// </summary>
	public interface IPerforceEnvironment
	{
		/// <summary>
		/// Get the value of a particular variable
		/// </summary>
		/// <param name="Name">Name of the variable to retrieve</param>
		/// <returns>The variable value, or null if it's not set</returns>
		string? GetValue(string Name);
	}

	/// <summary>
	/// The global Perforce environment
	/// </summary>
	public class GlobalPerforceEnvironment : IPerforceEnvironment
	{
		/// <summary>
		/// Environment variables in the global environment
		/// </summary>
		protected Dictionary<string, string> Variables { get; } = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

		/// <summary>
		/// Constructor
		/// </summary>
		public GlobalPerforceEnvironment()
		{
			foreach (DictionaryEntry Entry in Environment.GetEnvironmentVariables().OfType<DictionaryEntry>())
			{
				(string? Key, string? Value) = ((string?)Entry.Key, (string?)Entry.Value);
				if (!String.IsNullOrEmpty(Key) && !String.IsNullOrEmpty(Value))
				{
					if (Key.StartsWith("P4", StringComparison.OrdinalIgnoreCase))
					{
						Variables[Key] = Value;
					}
				}
			}
		}

		/// <inheritdoc/>
		public string? GetValue(string Name)
		{
			Variables.TryGetValue(Name, out string? Value);
			return String.IsNullOrEmpty(Value) ? null : Value;
		}
	}

	/// <summary>
	/// Default global environment used by Linux and MacOS, which reads settings from the registry.
	/// </summary>
	class WindowsGlobalPerforceEnvironment : GlobalPerforceEnvironment
	{
		public WindowsGlobalPerforceEnvironment()
		{
			using (RegistryKey? Key = Registry.CurrentUser.OpenSubKey("SOFTWARE\\perforce\\environment", false))
			{
				if (Key != null)
				{
					foreach (string ValueName in Key.GetValueNames())
					{
						string? Value = Key.GetValue(ValueName) as string;
						if (!String.IsNullOrEmpty(Value) && !Variables.ContainsKey(ValueName))
						{
							Variables[ValueName] = Value;
						}
					}
				}
			}
		}
	}

	/// <summary>
	/// Environment variables read from a file
	/// </summary>
	[DebuggerDisplay("{Location}")]
	public class PerforceEnvironmentFile : IPerforceEnvironment
	{
		/// <summary>
		/// The parent environment block
		/// </summary>
		public IPerforceEnvironment Parent { get; }

		/// <summary>
		/// Location of the file containing these variables
		/// </summary>
		public FileReference Location { get; }

		Dictionary<string, string> Variables = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Parent"></param>
		/// <param name="Location"></param>
		internal PerforceEnvironmentFile(IPerforceEnvironment Parent, FileReference Location)
		{
			this.Parent = Parent;
			this.Location = Location;

			string[] Lines = FileReference.ReadAllLines(Location);
			foreach (string Line in Lines)
			{
				string TrimLine = Line.Trim();
				int EqualsIdx = TrimLine.IndexOf('=', StringComparison.Ordinal);
				if (EqualsIdx != -1)
				{
					string Name = TrimLine.Substring(0, EqualsIdx).TrimEnd();
					string Value = TrimLine.Substring(EqualsIdx + 1).TrimStart();
					Variables[Name] = Value;
				}
			}
		}

		/// <inheritdoc/>
		public string? GetValue(string Name)
		{
			if (Variables.TryGetValue(Name, out string? Value))
			{
				return Value;
			}
			else
			{
				return Parent.GetValue(Name);
			}
		}
	}

	/// <summary>
	/// Static methods for retrieving the Perforce environment
	/// </summary>
	public static class PerforceEnvironment
	{
		/// <summary>
		/// Default environment regardless of directroy.
		/// </summary>
		public static IPerforceEnvironment Default { get; } = CreateDefaultEnvironment();

		static Dictionary<DirectoryReference, IPerforceEnvironment> DirectoryToEnvironment = new Dictionary<DirectoryReference, IPerforceEnvironment>();

		static IPerforceEnvironment CreateDefaultEnvironment()
		{
			IPerforceEnvironment Environment;
			if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
			{
				Environment = new WindowsGlobalPerforceEnvironment();
			}
			else
			{
				Environment = new GlobalPerforceEnvironment();
			}

			string? EnviroValue = Environment.GetValue("P4ENVIRO");
			if (EnviroValue != null)
			{
				FileReference Location = new FileReference(EnviroValue);
				Environment = new PerforceEnvironmentFile(Environment, Location);
			}

			return Environment;
		}

		/// <summary>
		/// Read the default Perforce settings reading any config file from the given directory
		/// </summary>
		/// <param name="Directory">The directory to read from</param>
		/// <returns>Default settings for the given directory</returns>
		public static IPerforceEnvironment FromDirectory(DirectoryReference Directory)
		{
			IPerforceEnvironment? Environment;
			if (!DirectoryToEnvironment.TryGetValue(Directory, out Environment))
			{
				DirectoryReference? ParentDirectory = Directory.ParentDirectory;
				if (ParentDirectory == null)
				{
					Environment = Default;
				}
				else
				{
					Environment = FromDirectory(ParentDirectory);

					string? ConfigFileName = Environment.GetValue("P4CONFIG");
					if (ConfigFileName != null)
					{
						FileReference Location = FileReference.Combine(Directory, ConfigFileName);
						if (FileReference.Exists(Location))
						{
							Environment = new PerforceEnvironmentFile(Environment, Location);
						}
					}
				}
				DirectoryToEnvironment[Directory] = Environment;
			}
			return Environment;
		}
	}
}
