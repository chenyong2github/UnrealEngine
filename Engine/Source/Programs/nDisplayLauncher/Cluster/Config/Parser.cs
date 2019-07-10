// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

using nDisplayLauncher.Cluster.Config.Entity;
using nDisplayLauncher.Log;


namespace nDisplayLauncher.Cluster.Config
{
	public static class Parser
	{
		// Returns version of specified config file
		public static ConfigurationVersion GetVersion(string filePath)
		{
			try
			{
				foreach (string DirtyLine in File.ReadLines(filePath))
				{
					string Line = PreprocessConfigLine(DirtyLine);

					if (Line.ToLower().StartsWith("[info]"))
					{
						EntityInfo Info = new EntityInfo(Line);
						return Info.Version;
					}
				}
			}
			catch (Exception ex)
			{
				AppLogger.Log("ERROR! " + ex.Message);
			}

			return ConfigurationVersion.Ver21;
		}

		// Config file parser
		public static Configuration Parse(string filePath)
		{
			Configuration ParsedConfig = new Configuration();

			try
			{
				foreach (string DirtyLine in File.ReadLines(filePath))
				{
					string Line = PreprocessConfigLine(DirtyLine);

					if (string.IsNullOrEmpty(Line) || Line.First() == '#')
					{
						//Do nothing
					}
					else
					{
						if (Line.ToLower().StartsWith("[cluster_node]"))
						{
							EntityClusterNode ClusterNode = new EntityClusterNode(Line);
							if (!string.IsNullOrEmpty(ClusterNode.Id))
							{
								ParsedConfig.ClusterNodes.Add(ClusterNode.Id, ClusterNode);
							}
						}
						if (Line.ToLower().StartsWith("[window]"))
						{
							EntityWindow Window = new EntityWindow(Line);
							if (!string.IsNullOrEmpty(Window.Id))
							{
								ParsedConfig.Windows.Add(Window.Id, Window);
							}
						}
					}
				}
			}
			catch (Exception ex)
			{
				AppLogger.Log("ERROR! " + ex.Message);
				return null;
			}

			return ParsedConfig;
		}

		public static string PreprocessConfigLine(string text)
		{
			string CleanString = text;
			int len = 0;

			// Remove all spaces before '='
			do
			{
				len = CleanString.Length;
				CleanString = text.Replace(" =", "=");
			}
			while (len != CleanString.Length);

			// Remove all spaces after '='
			do
			{
				len = CleanString.Length;
				CleanString = text.Replace("= ", "=");
			}
			while (len != CleanString.Length);

			// Remove all spaces before ','
			do
			{
				len = CleanString.Length;
				CleanString = text.Replace(" ,", ",");
			}
			while (len != CleanString.Length);

			// Remove all spaces after ','
			do
			{
				len = CleanString.Length;
				CleanString = text.Replace(", ", ",");
			}
			while (len != CleanString.Length);

			// Convert everything to lower case
			CleanString = CleanString.ToLower();

			return CleanString;
		}

		public static string GetStringValue(string text, string ArgName, string DefaultValue = "")
		{
			// Break the string to separate key-value substrings
			string[] ArgValPairs = text.Split(' ');

			// Parse each key-value substring to find the requested argument (key)
			foreach (string Pair in ArgValPairs)
			{
				string[] KeyValue = Pair.Trim().Split('=');
				if (KeyValue[0].Equals(ArgName, StringComparison.InvariantCultureIgnoreCase))
				{
					char[] charsToTrim = { '\"' };
					return KeyValue[1].Trim(charsToTrim);
				}
			}

			return DefaultValue;
		}

		public static string SetValue<T>(string text, string ArgName, T ArgVal)
		{
			string Result = string.Empty;
			bool ValueFoundAndSet = false;

			// Break the string to separate key-value substrings
			string[] ArgValPairs = text.Split(' ');

			// Parse each key-value substring to find the requested argument (key)
			foreach (string Pair in ArgValPairs)
			{
				string[] KeyValue = Pair.Trim().Split('=');
				if (KeyValue[0].Equals(ArgName, StringComparison.InvariantCultureIgnoreCase))
				{
					Result += string.Format("{0}=\"{1}\" ", KeyValue[0], ArgVal.ToString());
					ValueFoundAndSet = true;
				}
				else
				{
					// Just add the pair
					Result += (Pair + " ");
				}
			}

			// If no value found, add it
			if (!ValueFoundAndSet)
			{
				Result += string.Format("{0}=\"{1}\" ", ArgName, ArgVal.ToString());
			}

			return Result.Trim();
		}

		public static bool GetBoolValue(string text, string ArgName, bool DefaultValue = false)
		{
			string Value = GetStringValue(text, ArgName);
			if (string.IsNullOrEmpty(Value))
			{
				return DefaultValue;
			}

			if (Value.Equals("true", StringComparison.InvariantCultureIgnoreCase) || Value.CompareTo("1") == 0)
			{
				return true;
			}
			else if (Value.Equals("false", StringComparison.InvariantCultureIgnoreCase) || Value.CompareTo("0") == 0)
			{
				return false;
			}
			else
			{
				return DefaultValue;
			}
		}

		public static int GetIntValue(string text, string ArgName, int DefaultValue = 0)
		{
			string StringValue = GetStringValue(text, ArgName);
			if (string.IsNullOrEmpty(StringValue))
			{
				return DefaultValue;
			}

			int IntValue = 0;

			try
			{
				IntValue = int.Parse(StringValue);
			}
			catch (Exception e)
			{
				AppLogger.Log(string.Format("An error occurred while parsing {0} to int:\n{1}", ArgName, e.Message));
				return DefaultValue;
			}

			return IntValue;
		}

		public static float GetFloatValue(string text, string ArgName, float DefaultValue = 0.0f)
		{
			string StringValue = GetStringValue(text, ArgName);
			if (string.IsNullOrEmpty(StringValue))
			{
				return DefaultValue;
			}

			float FloatValue = 0;

			try
			{
				FloatValue = float.Parse(StringValue);
			}
			catch (Exception e)
			{
				AppLogger.Log(string.Format("An error occurred while parsing {0} to float:\n{1}", ArgName, e.Message));
				return DefaultValue;
			}

			return FloatValue;
		}

		public static List<string> GetStringArrayValue(string text, string ArgName, List<string> DefaultValue = null)
		{
			string StringValue = GetStringValue(text, ArgName);
			if (string.IsNullOrEmpty(StringValue))
			{
				return DefaultValue;
			}

			string[] Values = null;

			try
			{
				// Remove quotes
				char[] charsToTrim = { '\"' };
				StringValue = StringValue.Trim(charsToTrim);

				// Split value parameters
				Values = StringValue.Split(',');
			}
			catch (Exception e)
			{
				AppLogger.Log(string.Format("An error occurred while parsing {0} to array:\n{1}", ArgName, e.Message));
				return DefaultValue;
			}

			return Values.ToList();
		}

		public static string RemoveArgument(string text, string ArgName)
		{
			string Result = string.Empty;

			// Break the string to separate key-value substrings
			string[] ArgValPairs = text.Split(' ');

			// Parse each key-value substring to find the requested argument (key)
			foreach (string Pair in ArgValPairs)
			{
				string[] KeyValue = Pair.Trim().Split('=');
				if (!KeyValue[0].Equals(ArgName, StringComparison.InvariantCultureIgnoreCase))
				{
					Result += (Pair + " ");
				}
			}

			return Result.Trim();
		}
	}
}
