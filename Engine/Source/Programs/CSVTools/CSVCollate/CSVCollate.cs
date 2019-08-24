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
        private static string VersionString = "1.26";

        public static string Get() { return VersionString; }
    };

    class Program : CommandLineTool
    {
        static string formatString =
            "Format: \n" +
            "       -csv <filename> OR -csvDir <path>\n" +
            "       [-avg] -stats will be per frame averaged\n" +
			"       [-filterOutlierStat <stat>] - discard CSVs if this stat has very high values\n" +
			"       [-filterOutlierThreshold <value>] - threshold for outliers (default:1000)\n" +
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

			string filterOutlierStat = GetArg("filterOutlierStat", false);
			float filterOutlierThreshold = GetFloatArg("filterOutlierThreshold", 1000.0f);

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
			//CsvStats[] statsToAvg = new CsvStats[csvFilenames.Length];

			// Read all the CSVs into one big CSVStats class
			List<int> frameCsvCounts=new List<int>();
			List<string> allCsvFilenames = new List<string>();
			int csvIndex = 0;
            foreach (string csvFilename in csvFilenames)
            {
                CsvStats srcCsvStats = CsvStats.ReadCSVFile(csvFilename, null);

				// Check for outliers
				bool skip = false;
				if ( filterOutlierStat != null )
				{
					StatSamples outlierStat = srcCsvStats.GetStat(filterOutlierStat);
					if ( outlierStat != null )
					{
						foreach (float sample in outlierStat.samples)
						{
							if (sample>filterOutlierThreshold)
							{
								WriteLine("CSV " + csvFilename + " ignored due to bad " + filterOutlierStat + " value: " + sample);
								skip = true;
								break;
							}
						}
					}
				}
				if ( skip )
				{
					continue;
				}

				// Add the CSV filename as the first event if we're not averaging
				if (!bAverage)
				{
					CsvEvent firstEvent = new CsvEvent();
					firstEvent.Frame = 0;
					firstEvent.Name = "CSV:" + MakeShortFilename(csvFilename).Replace(' ', '_').Replace(',', '_').Replace('\n', '_');
					srcCsvStats.Events.Insert(0, firstEvent);
				}

				// Combine the stats
				if (csvIndex == 0)
				{
					combinedCsvStats = srcCsvStats;
				}
				else
				{
					CsvMetadata metadataA = combinedCsvStats.metaData;
					CsvMetadata metadataB = srcCsvStats.metaData;

					// If there is metadata, it should match
					if (metadataA != null || metadataB != null)
					{
						metadataA.CombineAndValidate(metadataB);
					}
					combinedCsvStats.Combine(srcCsvStats, bAverage, false);
					if ( bAverage )
					{
						// Resize the framecount array to match
						for (int i= frameCsvCounts.Count; i<combinedCsvStats.SampleCount; i++)
						{
							frameCsvCounts.Add(0);
						}
						for (int i=0; i<srcCsvStats.SampleCount;i++)
						{
							frameCsvCounts[i] += 1;
						}
					}
				}
				allCsvFilenames.Add(Path.GetFileName(csvFilename));
				WriteLine("Csvs Processed: " + csvIndex + " / " + csvFilenames.Length);
				csvIndex++;
            }

			if (bAverage)
			{
				// Divide all samples by the total number of CSVs 
				foreach (StatSamples stat in combinedCsvStats.Stats.Values)
				{
					for (int i=0; i<stat.samples.Count;i++)
					{
						stat.samples[i] /= (float)(frameCsvCounts[i]);
					}
				}

				// Add a stat for the csv count
				string csvCountStatName = "csvCount";
				if (!combinedCsvStats.Stats.ContainsKey(csvCountStatName))
				{
					StatSamples csvCountStat = new StatSamples(csvCountStatName);
					foreach (int count in frameCsvCounts)
					{
						csvCountStat.samples.Add((int)count);
					}
					combinedCsvStats.Stats.Add(csvCountStatName, csvCountStat);
				}
				if (combinedCsvStats.metaData != null)
				{
					// Add some metadata
					combinedCsvStats.metaData.Values.Add("Averaged", allCsvFilenames.Count.ToString());
					combinedCsvStats.metaData.Values.Add("SourceFiles", string.Join(";", allCsvFilenames));
				}
			}

			combinedCsvStats.ComputeAveragesAndTotal();

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
