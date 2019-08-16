// Copyright (C) Microsoft. All rights reserved.
// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.IO;
using System.Diagnostics;
using CSVStats;

namespace CSVTools
{
    class Version
    {
        private static string VersionString = "1.25";

        public static string Get() { return VersionString; }
    };

    class Program : CommandLineTool
    {
        static string formatString =
            "Format: \n" +
            "       -csv <filename> OR -csvDir <path>\n" +
            "       [-avg] <stats will be per frame averaged>\n" +
            "       -o <csvFilename> \n";

        void Run(string[] args)
        {
            // Read the command line
            if (args.Length < 1)
            {
                WriteLine("Invalid args");
                WriteLine(formatString);
                return;
            }

            ReadCommandLine(args);

            string csvOutFilename = GetArg("o", false);
            if (csvOutFilename.Length == 0)
            {
                WriteLine("Missing -o arg");
                WriteLine(formatString);
                return;
            }
            
            // Set the title
            string title = GetArg("title", false);
            if (title.Length == 0)
            {
                title = MakeShortFilename(csvOutFilename).ToLower().Replace(".csv", "");
            }
            char c = title[0];
            c = char.ToUpper(c);
            title = c + title.Substring(1);

            // Whether or not we want stats to be averaged rather than appended
            bool bAverage = GetBoolArg("avg");

            // Read CSV filenames from a directory or list
            string[] csvFilenames;
            string csvDir = GetArg("csvDir");
            if (csvDir.Length > 0)
            {
                DirectoryInfo di = new DirectoryInfo(csvDir);
                var files = di.GetFiles("*.csv", SearchOption.AllDirectories);
                csvFilenames = new string[files.Length];
                int i = 0;
                foreach (FileInfo csvFile in files)
                {
                    csvFilenames[i] = csvFile.FullName;
                    i++;
                }
            }
            else
            {
                string csvFilenamesStr = GetArg("csvs", true);
                if (csvFilenamesStr.Length == 0)
                {
                    System.Console.Write(formatString);
                    return;
                }
                csvFilenames = csvFilenamesStr.Split(';');
            }

            CsvStats combinedCsvStats = new CsvStats();

            // List of stats to be averaged
            CsvStats[] statsToAvg = new CsvStats[csvFilenames.Length];

            // Read all the CSVs into one big CSVStats class
            int csvIndex = 0;
            foreach (string csvFilename in csvFilenames)
            {
                CsvStats csvStats = CsvStats.ReadCSVFile(csvFilename, null);

                // Add the CSV filename as the first event (sanitised for CSV)
                CsvEvent firstEvent = null;
                firstEvent = new CsvEvent();
                firstEvent.Frame = 0;
                firstEvent.Name = "CSV:" + MakeShortFilename(csvFilename).Replace(' ', '_').Replace(',', '_').Replace('\n', '_');
                csvStats.Events.Insert(0, firstEvent);

                if (bAverage)
                {
                    statsToAvg[csvIndex] = csvStats;
                }
                else
                {
                    // Combine the stats
                    if (csvIndex == 0)
                    {
                        combinedCsvStats = csvStats;
                    }
                    else
                    {
                        CsvMetadata metadataA = combinedCsvStats.metaData;
                        CsvMetadata metadataB = csvStats.metaData;

                        // If there is metadata, it should match
                        if ( metadataA != null || metadataB != null)
                        {
                            if (!CsvMetadata.Matches(metadataA, metadataB))
                            {
                                WriteLine("Metadata mismatch!");
                            }
                        }
                        combinedCsvStats.Combine(csvStats, false);
                    }
                }

                csvIndex++;
            }

            if (bAverage)
            {
                // Average stats by frame
                combinedCsvStats = CsvStats.AverageByFrame(statsToAvg);

                if (combinedCsvStats.metaData != null)
                {
                  // Add few more meta information
                  combinedCsvStats.metaData.Values.Add("Averaged", statsToAvg.Length.ToString());
                }

                int fIt = 0;
                string[] filenames = new string[csvFilenames.Length];
                foreach (var filename in csvFilenames)
                {
                    filenames[fIt++] = Path.GetFileName(filename);
                }

                if (combinedCsvStats.metaData != null)
                {
                  combinedCsvStats.metaData.Values.Add("Files", string.Join(",", filenames));
                }
            }

            // Write the csv stats to a CSV
            combinedCsvStats.WriteToCSV(csvOutFilename);
        }


        static void Main(string[] args)
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
                }
            }
        }

    }
}
