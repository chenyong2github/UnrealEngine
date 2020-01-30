// Copyright (C) Microsoft. All rights reserved.
// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.IO;
using System.Diagnostics;

namespace CSVStats
{
	public class CommandLine
	{
		protected string commandLine = "";

		protected Dictionary<string, string> CommandLineArgs;

		public string GetCommandLine()
		{
			return commandLine;
		}

		public CommandLine(string[] args)
		{
			commandLine = "";
			foreach (string arg in args)
			{
				if (arg.Contains(' ') || arg.Contains('\t'))
				{
					commandLine += "\"" + arg + "\" ";
				}
				else
				{
					commandLine += arg + " ";
				}
			}
			CommandLineArgs = new Dictionary<string, string>();
			for (int i = 0; i < args.Length; i++)
			{
				string arg = args[i];
				if (arg[0] == '-')
				{
					string val = "1";

					// If there's a value, read it
					if (i < args.Length - 1 && args[i + 1][0] != '-')
					{
						bool first = true;
						for (int j = i + 1; j < args.Length; j++)
						{
							string str = args[j];
							if (str.Length > 0 && str[0] == '-')
								break;
							if (first)
							{
								val = str;
								first = false;
							}
							else
							{
								val += ";" + str;
							}
						}
						i++;
					}

					string argKey = arg.Substring(1).ToLower();
					if (CommandLineArgs.ContainsKey(argKey))
					{
						Console.Out.WriteLine("Duplicate commandline argument found for " + arg + ". Overriding value from " + CommandLineArgs[argKey] + " to " + val);
						CommandLineArgs.Remove(argKey);
					}
					CommandLineArgs.Add(arg.Substring(1).ToLower(), val);
				}
			}
		}

		public int GetIntArg(string key, int defaultValue)
		{
			string val = GetArg(key, false);
			if (val != "") return Convert.ToInt32(val);
			return defaultValue;
		}

		public float GetFloatArg(string key, float defaultValue)
		{
			string val = GetArg(key, false);
			if (val != "") return (float)Convert.ToDouble(val, System.Globalization.CultureInfo.InvariantCulture);
			return defaultValue;
		}

		public bool GetBoolArg(string key)
		{
			return CommandLineArgs.ContainsKey(key.ToLower());
		}

		public string GetArg(string key, string defaultValue)
		{
			string lowerKey = key.ToLower();

			if (CommandLineArgs.ContainsKey(lowerKey))
			{
				return CommandLineArgs[lowerKey];
			}
			return defaultValue;
		}

		public string GetArg(string key, bool mandatory = false)
		{
			string lowerKey = key.ToLower();

			if (CommandLineArgs.ContainsKey(lowerKey))
			{
				return CommandLineArgs[lowerKey];
			}
			else if (mandatory)
			{
				Console.WriteLine("Missing parameter " + key);
			}

			return "";
		}

	}

	public class CommandLineTool
	{
		protected enum HostPlatform
		{
			Windows,
			Mac,
			Linux
		}

		protected CommandLine commandLine;

		private readonly static bool bIsMac = File.Exists("/System/Library/CoreServices/SystemVersion.plist");

		protected static HostPlatform Host
		{
			get 
			{
				PlatformID Platform = Environment.OSVersion.Platform;
				switch (Platform)
				{
					case PlatformID.Win32NT:
						return HostPlatform.Windows;
					case PlatformID.Unix:
						return bIsMac? HostPlatform.Mac : HostPlatform.Linux;
					case PlatformID.MacOSX:
						return HostPlatform.Mac;
					default:
						throw new Exception("Unhandled runtime platform " + Platform);
				}
			}
		}

		protected static string MakeShortFilename(string filename)
        {
            int index = filename.LastIndexOf(Path.DirectorySeparatorChar);
            if (index == -1)
            {
                return filename;
            }
            else
            {
                return filename.Substring(index + 1);
            }
        }
        protected int GetIntArg(string key, int defaultValue)
        {
			return commandLine.GetIntArg(key, defaultValue);
		}

		protected float GetFloatArg(string key, float defaultValue)
        {
			return commandLine.GetFloatArg(key, defaultValue);
		}

		protected bool GetBoolArg(string key)
        {
			return commandLine.GetBoolArg(key);
        }

		protected string GetArg(string key, string defaultValue)
		{
			return commandLine.GetArg(key, defaultValue);
		}

		protected string GetArg(string key, bool mandatory = false)
        {
			return commandLine.GetArg(key, mandatory);
        }

		protected void WriteLine(String message, params object[] args)
        {
            String formatted = String.Format(message, args);
            Console.WriteLine(formatted);
        }


        protected string[] ReadLinesFromFile(string filename)
        {
            StreamReader reader = new StreamReader(filename, true);
            List<string> lines = new List<string>();

            // Detect unicode
            string line = reader.ReadLine();

            bool bIsUnicode = false;
            for (int i = 0; i < line.Length - 1; i++)
            {
                if (line[i] == '\0')
                {
                    bIsUnicode = true;
                    break;
                }
            }
            if (bIsUnicode)
            {
                reader = new StreamReader(filename, Encoding.Unicode, true);
            }
            else
            {
                lines.Add(line);
            }

            while ((line = reader.ReadLine()) != null)
            {
                if (line.Trim().Length > 0)
                {
                    lines.Add(line);
                }
            }
            reader.Close();
            return lines.ToArray();
        }

		protected void ReadCommandLine(string[] args)
		{
			commandLine = new CommandLine(args);
		}
	}
}
