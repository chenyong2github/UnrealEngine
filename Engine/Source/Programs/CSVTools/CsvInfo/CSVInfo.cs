// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Diagnostics;
using CSVStats;

namespace CSVInfo
{
    class Version
    {
        private static string VersionString = "1.02";

        public static string Get() { return VersionString; }
    };

    class Program : CommandLineTool
    {
        static int Main(string[] args)
        {
            Program program = new Program();
            if (Debugger.IsAttached)
            {
                program.Run(args);
            }
            else
            {
                try
                {
                    program.Run(args);
                }
                catch (System.Exception e)
                {
                    Console.WriteLine("[ERROR] " + e.Message);
                    return 1;
                }
            }

            return 0;
        }

		string Quotify(string s)
		{
			return "\"" + s + "\"";
		}

		string Sanitize(string s)
		{
			return s.Replace("\\","\\\\").Replace("\"", "\\\"");
		}

		string ToJsonString(string s)
		{
			return Quotify(Sanitize(s));
		}

		string ToJsonStringList(List<string> list)
		{
			List<string> safeList = new List<string>();
			foreach(string s in list)
			{
				safeList.Add(ToJsonString(s));
			}
			return "[" + String.Join(",", safeList) + "]";
		}

        void Run(string[] args)
        {
            string formatString =
                "Format: \n" +
                "  <csvfilename>\n"+
				"  [-showaverages]\n"+
				"  [-toJson <filename>]";

			// Read the command line
			if (args.Length < 1)
            {
				WriteLine("CsvInfo " + Version.Get());
                WriteLine(formatString);
                return;
            }

            string csvFilename = args[0];

            ReadCommandLine(args);

            bool showAverages = GetBoolArg("showAverages");
			string jsonFilename = GetArg("toJson",false);

			CsvStats csvStats = CsvStats.ReadCSVFile(csvFilename, null);
			List<string> statLines = new List<string>();
			foreach (StatSamples stat in csvStats.Stats.Values.ToArray())
			{
				string statLine = stat.Name;
				if (showAverages)
				{
					statLine += " (" + stat.average.ToString() + ")";
				}
				statLines.Add(statLine);
			}
			statLines.Sort();


			if (jsonFilename != "")
			{
				// We just write the lines raw, since this version of .Net doesn't have a json serializer. 
				// TODO: Fix this when we upgrade to .Net 5.0 and use System.Text.Json
				List<string> jsonLines = new List<string>();
				jsonLines.Add("{");
				if (csvStats.metaData != null)
				{
					jsonLines.Add("  \"metadata\":\n  {");
					Dictionary<string, string> metadata = csvStats.metaData.Values;
					int count = metadata.Count;
					int index = 0;
					foreach (string key in metadata.Keys)
					{
						string line = "    " + ToJsonString(key) + ":" + ToJsonString(metadata[key]);
						if (index < count-1)
						{
							line += ",";
						}
						jsonLines.Add(line);
						index++;
					}
					jsonLines.Add("  },");
				}
				jsonLines.Add("  \"stats\":\n  {");
				jsonLines.Add("    "+ToJsonStringList(statLines));
				jsonLines.Add("  }");

				jsonLines.Add("}");
				System.IO.File.WriteAllLines(jsonFilename,jsonLines);
				Console.Out.WriteLine("Wrote csv info to " + jsonFilename);
			}
			else
			{
				// Write out the sorted stat names
				Console.Out.WriteLine("Stats:");
				foreach (string statLine in statLines)
				{
					Console.Out.WriteLine("  " + statLine);
				}

				if (csvStats.metaData != null)
				{
					// Write out the metadata, if it exists
					Console.Out.WriteLine("\nMetadata:");
					foreach (KeyValuePair<string, string> pair in csvStats.metaData.Values.ToArray())
					{
						string key = pair.Key.PadRight(20);
						Console.Out.WriteLine("  " + key + ": " + pair.Value);
					}
				}

			}
		}
    }
}
