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
        private static string VersionString = "1.01";

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


        void Run(string[] args)
        {
            string formatString =
                "Format: \n" +
                "<csvfilename>";

            // Read the command line
            if (args.Length < 1)
            {
                WriteLine(formatString);
                return;
            }

            string csvFilename = args[0];

            ReadCommandLine(args);

            bool showAverages = GetBoolArg("showAverages");

            // Write out the sorted stat names
            Console.Out.WriteLine("Stats:");
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
            foreach (string statLine in statLines)
            {
                Console.Out.WriteLine("  " + statLine);
            }

            if ( csvStats.metaData != null )
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
