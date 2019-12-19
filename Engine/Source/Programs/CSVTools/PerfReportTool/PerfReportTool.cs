// Copyright (C) Microsoft. All rights reserved.
// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Xml;
using System.Xml.Linq;
using System.IO;
using System.Diagnostics;
using CSVStats;
using System.Threading;

using PerfSummaries;

namespace PerfReportTool
{
    class Version
    {
        private static string VersionString = "4.02";

        public static string Get() { return VersionString; }
    };

	class XmlHelper
    { 
        public static int ReadAttributeInt(XElement element, string AttributeName, int DefaultValue)
        {
            try
            {
                return Convert.ToInt32(element.Attribute(AttributeName).Value);
            }
            catch
            {
            }
            return DefaultValue;
        }

        public static double ReadAttributeDouble(XElement element, string AttributeName, double DefaultValue)
        {
            try
            {
                if (element.Attribute(AttributeName) != null)
                {
                    return Convert.ToDouble(element.Attribute(AttributeName).Value, System.Globalization.CultureInfo.InvariantCulture);
                }
            }
            catch { }
            return DefaultValue;
        }

        public static bool ReadAttributeBool(XElement element, string AttributeName, bool DefaultValue )
        {
            try
            {
                if (element.Attribute(AttributeName) != null)
                {
                    return Convert.ToInt32(element.Attribute(AttributeName).Value) == 1;
                }
            }
            catch { }
            return DefaultValue;
        }

        public static string ReadAttribute(XElement element, string AttributeName, string DefaultValue )
        {
            if (element.Attribute(AttributeName) != null)
            {
                return element.Attribute(AttributeName).Value;
            }
            return DefaultValue;
        }
    };

	class OptionalString
    {
        public OptionalString(string valueIn)
        {
            value = valueIn;
            isSet = true;
        }
        public OptionalString()
        {
            isSet = false;
        }
        public OptionalString( XElement element, string Name, bool IsElement = false )
        {
            isSet = false;
            if (IsElement )
            {
                XElement child = element.Element(Name);
                if (child != null)
                {
                    value = child.Value;
                    isSet = true;
                }
            }
            else
            {
                XAttribute child = element.Attribute(Name);
                if (child != null)
                {
                    value = child.Value;
                    isSet = true;
                }
            }
        }

        public void InheritFrom(OptionalString baseVersion) { if (!isSet) { isSet = baseVersion.isSet; value = baseVersion.value; } }
        public bool isSet;
        public string value;
    };

    class OptionalBool
    {
        public OptionalBool(bool valueIn)
        {
            value = valueIn;
            isSet = true;
        }
        public OptionalBool()
        {
            isSet = false;
        }
        public OptionalBool(XElement element, string AttributeName)
        {
            isSet = false;
            try
            {
                if (element.Attribute(AttributeName) != null)
                {
                    value = Convert.ToInt32(element.Attribute(AttributeName).Value) == 1;
                    isSet = true;
                }
            }
            catch {}
        }
        public void InheritFrom(OptionalBool baseVersion) { if (!isSet) { isSet = baseVersion.isSet; value = baseVersion.value; } }

        public bool isSet;
        public bool value;
    };

    class OptionalInt
    {
        public OptionalInt(int valueIn)
        {
            value = valueIn;
            isSet = true;
        }
        public OptionalInt()
        {
            isSet = false;
        }
        public OptionalInt(XElement element, string AttributeName)
        {
            isSet = false;
            try
            {
                if (element.Attribute(AttributeName) != null)
                {
                    value = Convert.ToInt32(element.Attribute(AttributeName).Value);
                    isSet = true;
                }
            }
            catch { }
        }
        public void InheritFrom(OptionalInt baseVersion) { if (!isSet) { isSet = baseVersion.isSet; value = baseVersion.value; } }

        public bool isSet;
        public int value;
    };

    class OptionalDouble
    {
        public OptionalDouble(int valueIn)
        {
            value = valueIn;
            isSet = true;
        }
        public OptionalDouble()
        {
            isSet = false;
        }
        public OptionalDouble(XElement element, string AttributeName)
        {
            isSet = false;
            try
            {
                if (element.Attribute(AttributeName) != null)
                {
                    value = Convert.ToDouble(element.Attribute(AttributeName).Value, System.Globalization.CultureInfo.InvariantCulture);
                    isSet = true;
                }
            }
            catch {}
        }

        public void InheritFrom(OptionalDouble baseVersion) { if (!isSet) { isSet = baseVersion.isSet; value = baseVersion.value; } }

        public bool isSet;
        public double value;
    };

	static class OptionalHelper
	{
		public static string GetDoubleSetting(OptionalDouble setting, string cmdline)
		{
			return (setting.isSet ? (cmdline + setting.value.ToString()) : "");
		}

		public static string GetStringSetting(OptionalString setting, string cmdline)
		{
			return (setting.isSet ? (cmdline + setting.value) : "");
		}
	};

    class CachedCsvFile
    {
        public CachedCsvFile(string inFilename)
        {
            lines = File.ReadAllLines(inFilename);
            filename = inFilename;
            if (lines.Length >= 2)
            {
                // Read CSV stats without data
                dummyCsvStats = CsvStats.ReadCSVFromLines(lines, null, 0, true);
				metadata = dummyCsvStats.metaData;
            }
        }
        public string filename;
        public string[] lines;
        public CsvStats dummyCsvStats;
        public CsvMetadata metadata;
    };


    class Program : CommandLineTool
    {
		static string formatString =
			"PerfReportTool v" + Version.Get() + "\n" +
			"\n" +
			"Format: \n" +
			"       -csv <filename> or -csvdir <directory path>\n" +
			"       -o <dir name>: output directory (will be created if necessary)\n" +
			"\n" +
			"Optional Args:\n" +
			"       -reportType <e.g. flythrough, playthrough, playthroughmemory>\n" +
			"       -reportTypeCompatCheck : do a compatibility if when specifying a report type (rather than forcing)\n"+
			"       -graphXML <xmlfilename>\n" +
			"       -reportXML <xmlfilename>\n" +
			"       -reportxmlbasedir <folder>\n" +
			"       -title <name>\n" +
			"       -minx <frameNumber>\n" +
			"       -maxx <frameNumber>\n" +
			"       -maxy <value>\n" +
			"       -graphScale <value>\n" +
            "       -noStripEvents : if specified, don't strip out excluded events from the stats\n" +
			"       -perfLog : output performance logging information\n" +
			"       -writeSummaryCsv : if specified, a csv file containing summary information will be generated. Not available in bulk mode.\n" +
			"       -beginEvent <event> : strip data before this event\n"+
			"       -endEvent <event> : strip data after this event\n" +
			"       -noBatchedGraphs : disable batched/multithreaded graph generation (default is enabled)\n" +
			"       -graphThreads : use with -batchedGraphs to control the number of threads per CsvToSVG instance (default: PC core count/2)\n" +
			"       -nocommandlineEmbed : don't embed the commandline "+
			"\n" +
			"Optional bulk mode args: (use with -csvdir)\n" +
			"       -recurse \n" +
			"       -searchpattern <pattern>, e.g -searchpattern csvprofile*\n" +
			"       -customTable <comma seprated fields>\n" +
			"       -customTableSort <comma seprated field row sort order> (use with -customTable)\n" +
			"       -noDetailedReports : skips individual report generation\n" +
			"       -collateTable : writes a collated table in addition to the main one, merging by row sort\n" +
			"       -emailTable : writes a condensed email-friendly table (see the 'condensed' summary table)\n" +
			"       -csvTable : writes the summary table in CSV format instead of html\n" +
			"       -summaryTable <name> :\n" +
			"           Selects a custom summary table type from the list in reportTypes.xml \n"+
			"           (if not specified, 'default' will be used)\n" +
			"       -summaryTableFilename <name> : use the specified filename for the summary table (instead of SummaryTable.html)\n"+
            "       -metadataFilter <key=value,key=value...> : filters based on CSV metadata\n" +
            "       -precacheCount <n> : number of CSV files to precache in the lookahead cache (0 for no precache)\n" +
            "       -precacheThreadCount <n> : number of threads to use for the CSV lookahead cache (default 8)\n" +
			"       -readAllStats : reads all stats so that any stat can be output to the summary table. Useful with -customtable in bulk mode (off by default)\n" +
			"       -externalGraphs : enables external graphs (off by default)\n" +
			"";
			/*
			"Note on custom tables:\n" +
			"       The -customTable and -customTableSort args allow you to generate a custom summary table\n" +
			"       This is an alternativee to using preset summary tables (see -summarytable)\n" +
			"       Example:\n"+
			"               -customTableSort \"deviceprofile,buildversion\" -customTable \"deviceprofile,buildversion,memoryfreeMB*\" \n"+
			"       This outputs a table containing deviceprofile, buildversion and memoryfree stats, sorted by deviceprofile and then buildversion\n" +
			""
			*/

		Dictionary<string, string> statDisplaynameMapping;
		ReportXML reportXML;

		string GetBaseDirectory()
        {
            string location = System.Reflection.Assembly.GetEntryAssembly().Location.ToLower();

            string baseDirectory = location;
            baseDirectory = baseDirectory.Replace("perfreporttool.exe", "");

            string debugSubDir = "\\bin\\debug\\";
            if (baseDirectory.ToLower().EndsWith(debugSubDir))
            {
                // Might be best to use the CSVToSVG from source, but that might not be built, so use the one checked into binaries instead
                baseDirectory = baseDirectory.Substring(0, baseDirectory.Length - debugSubDir.Length);
                baseDirectory += "\\..\\..\\..\\..\\Binaries\\DotNET\\CsvTools";
            }
            return baseDirectory;
        }

		void Run(string[] args)
		{
            // Read the command line
            if (args.Length < 1)
			{
				WriteLine("Invalid args");
				WriteLine(formatString);
				return;
			}
			WriteLine("PerfReportTool v" + Version.Get());

			ReadCommandLine(args);
            PerfLog perfLog = new PerfLog(GetBoolArg("perfLog"));

            bool bBulkMode = false;
			// Read CSV filenames from a directory or list
			string[] csvFilenames;
			if (args.Length == 1)
			{
				// Simple mode: just pass a csv name
				csvFilenames = new string[] { args[0] };
			}
			else
			{
				string csvDir = GetArg("csvDir");
				if (csvDir.Length > 0)
				{
					DirectoryInfo di = new DirectoryInfo(csvDir);
					bool recurse = GetBoolArg("recurse");
					string searchPattern = GetArg("searchPattern", false);
					if (searchPattern == "")
					{
						searchPattern = "*.csv";
					}
					else if (!searchPattern.Contains('.'))
					{
						searchPattern += ".csv";
					}
					var files = di.GetFiles(searchPattern, recurse ? SearchOption.AllDirectories : SearchOption.TopDirectoryOnly);
					csvFilenames = new string[files.Length];
					int i = 0;
					foreach (FileInfo csvFile in files)
					{
						csvFilenames[i] = csvFile.FullName;
						i++;
					}
					// We don't write summary CSVs in bulk mode
					bBulkMode = true;
                    perfLog.LogTiming("DirectoryScan");
                }
                else
				{
					string csvFilenamesStr = GetArg("csv");
					if (csvFilenamesStr.Length == 0)
					{
						csvFilenamesStr = GetArg("csvs", true);
						if (csvFilenamesStr.Length == 0)
						{
							System.Console.Write(formatString);
							return;
						}
					}
					csvFilenames = csvFilenamesStr.Split(';');
				}
			}

			// Load the report + graph XML data
			reportXML = new ReportXML(GetArg("graphxml", false), GetArg("reportxml", false), GetArg("reportxmlbasedir", false));
			statDisplaynameMapping = reportXML.GetDisplayNameMapping();

            // Create the output directory if requested
            string outputDir = GetArg("o", false).ToLower();
            if (!string.IsNullOrEmpty(outputDir))
            {
                if (!Directory.Exists(outputDir))
                {
                    Directory.CreateDirectory(outputDir);
                }
            }

			int precacheCount = GetIntArg("precacheCount", 8);
			int precacheThreads = GetIntArg("precacheThreads", 8);
			bool bBatchedGraphs = true;
			if ( GetBoolArg("noBatchedGraphs") )
			{
				bBatchedGraphs = false;
			}
			if (bBatchedGraphs)
			{
				WriteLine("Batched graph generation enabled.");
			}
			else
			{
				WriteLine("Batched graph generation disabled.");
			}

			perfLog.LogTiming("Initialization");

			string metadataFilterString = GetArg("metadataFilter", "");

			CsvFileCache csvFileCache = new CsvFileCache(csvFilenames, precacheCount, precacheThreads);
            SummaryMetadataTable metadataTable = new SummaryMetadataTable();
            for ( int i=0; i<csvFilenames.Length; i++)
			{
                try
                {
                    CachedCsvFile cachedCsvFile = csvFileCache.GetNextCachedCsvFile();
					reportXML.ApplyDerivedMetadata(cachedCsvFile.metadata);
                    Console.WriteLine("-------------------------------------------------");
                    Console.WriteLine("CSV " + (i+1) + "/" + csvFilenames.Length ) ;
                    Console.WriteLine(csvFilenames[i] );
                    SummaryMetadata metadata = null;
					if (bBulkMode)
					{
						metadata = new SummaryMetadata();
					}

                    bool bGenerateReport = true;
                    if (metadataFilterString.Length > 0)
                    {
                        bGenerateReport = DoesCsvMetadataMatchFilter(cachedCsvFile, metadataFilterString);
                    }
                    perfLog.LogTiming("  CsvCacheRead");

                    if (bGenerateReport)
                    {
                        GenerateReport(cachedCsvFile, outputDir, bBulkMode, metadata, bBatchedGraphs);
                        if (metadata != null)
                        {
                            metadataTable.Add(metadata);
                        }
                        perfLog.LogTiming("  GenerateReport");
                    }
                }
				catch (Exception e)
				{
					if (bBulkMode)
					{
						Console.Out.WriteLine("[ERROR] : "+ e.Message);
					}
					else
					{
						// If we're not in bulk mode, exceptions are fatal
						throw e;
					}
				}
			}

            Console.WriteLine("-------------------------------------------------");

            // Write out the metadata table, if there is one
            if (metadataTable.Count > 0)
			{
				string summaryTableFilename = GetArg("summaryTableFilename", "SummaryTable");
				if ( summaryTableFilename.ToLower().EndsWith(".html"))
				{
					summaryTableFilename = summaryTableFilename.Substring(0, summaryTableFilename.Length - 5);
				}
				string customSummaryTableFilter = GetArg("customTable");
				bool bCsvTable = GetBoolArg("csvTable");
				bool bCollateTable = GetBoolArg("collateTable");
				if (customSummaryTableFilter.Length > 0)
				{
					string customSummaryTableRowSort = GetArg("customTableSort");
					if (customSummaryTableRowSort.Length == 0)
					{
						customSummaryTableRowSort = "buildversion,deviceprofile";
					}
					WriteMetadataTableReport(outputDir, summaryTableFilename, metadataTable, customSummaryTableFilter.Split(',').ToList(), customSummaryTableRowSort.Split(',').ToList(), false, bCsvTable);

					if (bCollateTable)
					{
						WriteMetadataTableReport(outputDir, summaryTableFilename+"_Collated", metadataTable, customSummaryTableFilter.Split(',').ToList(), customSummaryTableRowSort.Split(',').ToList(), true, bCsvTable);
					}
				}
				else
				{
					string summaryTableName = GetArg("summaryTable");
					if (summaryTableName.Length == 0)
					{
						summaryTableName = "default";
					}
					SummaryTableInfo tableInfo = reportXML.GetSummaryTable(summaryTableName);
					WriteMetadataTableReport(outputDir, summaryTableFilename, metadataTable, tableInfo, false, bCsvTable);
					if (bCollateTable)
					{
						WriteMetadataTableReport(outputDir, summaryTableFilename, metadataTable, tableInfo, true, bCsvTable);
					}
				}

				// EmailTable is hardcoded to use the condensed report type
				if (GetBoolArg("emailSummary") || GetBoolArg("emailTable"))
				{
					SummaryTableInfo tableInfo = reportXML.GetSummaryTable("condensed");
					WriteMetadataTableReport(outputDir, summaryTableFilename+"_Email", metadataTable, tableInfo, true);
				}
                perfLog.LogTiming("WriteSummaryTable");
            }

            perfLog.LogTotalTiming();
        }

        void WriteMetadataTableReport(string outputDir, string filenameWithoutExtension, SummaryMetadataTable table, SummaryTableInfo tableInfo, bool bCollated = false , bool bToCSV = false )
		{
			WriteMetadataTableReport(outputDir, filenameWithoutExtension, table, tableInfo.columnFilterList, tableInfo.rowSortList, bCollated, bToCSV);
		}

		void WriteMetadataTableReport(string outputDir, string filenameWithoutExtension, SummaryMetadataTable table, List<string> columnFilterList, List<string> rowSortList, bool bCollated = false, bool bToCSV = false)
		{
            if (!string.IsNullOrEmpty(outputDir))
            {
                filenameWithoutExtension = Path.Combine(outputDir, filenameWithoutExtension);
            }

            SummaryMetadataTable filteredTable = table.SortAndFilter(columnFilterList, rowSortList);
			if (bCollated)
			{
				filteredTable = filteredTable.CollateSortedTable(rowSortList);
			}
			if (bToCSV)
			{
				filteredTable.WriteToCSV(filenameWithoutExtension+".csv");
			}
			else
			{
				filteredTable.ApplyDisplayNameMapping(statDisplaynameMapping);
				filteredTable.WriteToHTML(filenameWithoutExtension+".html", Version.Get());
			}
		}

		string ReplaceFileExtension( string path, string newExtension )
        {
            int lastDotIndex = path.LastIndexOf('.');
            if ( path.EndsWith("\""))
            {
                newExtension = newExtension + "\"";
                if (lastDotIndex == -1)
                {
                    lastDotIndex = path.Length - 1;
                }
            }
            else if ( lastDotIndex == -1 )
            {
                lastDotIndex = path.Length;
            }

            return path.Substring(0, lastDotIndex) + newExtension; 
        }

		static Dictionary<string, bool> UniqueHTMLFilemameLookup = new Dictionary<string, bool>();

        bool DoesCsvMetadataMatchFilter(CachedCsvFile csvFile, string metadataFilterString)
        {
            CsvMetadata metadata = csvFile.metadata;
            if (metadata == null)
            {
                Console.WriteLine("CSV " + csvFile.filename + " has no metadata");
                return false;
            }
			return CsvStats.DoesMetadataMatchFilter(metadata, metadataFilterString);
        }
        class PerfLog
        {
            public PerfLog(bool inLoggingEnabled)
            {
                stopWatch = Stopwatch.StartNew();
                lastTimeElapsed = 0.0;
                loggingEnabled = inLoggingEnabled;
            }

            public double LogTiming(string description, bool newLine=false)
            {
                double elapsed = stopWatch.Elapsed.TotalSeconds-lastTimeElapsed;
                if (loggingEnabled)
                {
                    Console.WriteLine("[PerfLog] "+
                        String.Format("{0,-25} : {1,-10}", description , elapsed.ToString("0.0") + "s"), 70);
                    if ( newLine )
                    {
                        Console.WriteLine();
                    }
                }
                lastTimeElapsed = stopWatch.Elapsed.TotalSeconds;
                return elapsed;
            }

            public double LogTotalTiming()
            {
                double elapsed = stopWatch.Elapsed.TotalSeconds;
                if (loggingEnabled)
                {
                    Console.WriteLine("[PerfLog] TOTAL: " + elapsed.ToString("0.0") + "s\n");
                }
                return elapsed;
            }
            Stopwatch stopWatch;
            double lastTimeElapsed;
            bool loggingEnabled;
        }

        void GenerateReport(CachedCsvFile csvFile, string outputDir, bool bBulkMode, SummaryMetadata summaryMetadata, bool bBatchedGraphs)
        {
            PerfLog perfLog = new PerfLog(GetBoolArg("perfLog"));
            string shortName = ReplaceFileExtension(MakeShortFilename(csvFile.filename), "");
            string title = GetArg("title", false);
            if (title.Length == 0)
            {
                title = shortName;
            }

            char c = title[0];
            c = char.ToUpper(c);
            title = c + title.Substring(1);

            int minX = GetIntArg("minx", 0);
            int maxX = GetIntArg("maxx", Int32.MaxValue);

            string reportType = GetArg("reportType", false).ToLower();
            bool forceReportType = !GetBoolArg("reportTypeCompatCheck");

            ReportTypeInfo reportTypeInfo = reportXML.GetReportTypeInfo(reportType, csvFile, bBulkMode, forceReportType);

            bool writeDetailedReport = !GetBoolArg("noDetailedReports");

			string htmlFilename = null;
			if (writeDetailedReport)
			{
				htmlFilename = shortName;
				// Make sure the HTML filename is unique
				if (bBulkMode)
				{
					int index = 0;
					while (UniqueHTMLFilemameLookup.ContainsKey(htmlFilename.Trim().ToLower()))
					{
						if (htmlFilename.EndsWith("]") && htmlFilename.Contains('['))
						{
							int strIndex = htmlFilename.LastIndexOf('[');
							htmlFilename = htmlFilename.Substring(0, strIndex);
						}
						htmlFilename += "[" + index.ToString() + "]";
						index++;
					}
					UniqueHTMLFilemameLookup.Add(htmlFilename.Trim().ToLower(), true);
				}
				htmlFilename += ".html";
            }



            // Read the CSV file so we can generate the table of frames in budget etc
            List<string> statsToSummariseList = null;
            if ( reportTypeInfo.summaries.Count >0 )
            {
				statsToSummariseList = new List<string>();
				foreach (var summary in reportTypeInfo.summaries)
				{
					statsToSummariseList.AddRange(summary.GetUniqueStatNames());
				}
                for (int i = 0; i < statsToSummariseList.Count; i++)
                {
                    // Remove all Min/max sort tags before checking stats against existing stats.
                    statsToSummariseList[i] = statsToSummariseList[i].Split('(')[0];
                }
                statsToSummariseList.Add("frametime");
            }

            string [] statsToSummarise = statsToSummariseList.ToArray();

            float thickness = 1.0f;
			List<string> csvToSvgCommandlines = new List<string>();
			List<string> svgFilenames = new List<string>();
			string responseFilename = null;
			List<Process> csvToSvgProcesses = new List<Process>();
            if (writeDetailedReport)
            {
				// Generate all the graphs asyncronously
                foreach (ReportGraph graph in reportTypeInfo.graphs)
                {
					string svgFilename = String.Empty;
					if (graph.isExternal && !GetBoolArg("externalGraphs"))
					{
						svgFilenames.Add(svgFilename);
						continue;
					}
					bool bFoundStat = false;
					foreach (string statString in graph.settings.statString.value.Split(' '))
                    {
                        List<StatSamples> matchingStats = csvFile.dummyCsvStats.GetStatsMatchingString(statString);
                        if (matchingStats.Count > 0)
                        {
                            bFoundStat = true;
                            break;
                        }

                    }
					if (bFoundStat)
					{
						float graphScale = GetFloatArg("graphScale", 1.0f);
						svgFilename = GetTempFilename(csvFile.filename) + ".svg";
						string args = GetCsvToSvgArgs(csvFile.filename, svgFilename, graph, thickness, minX, maxX, false, svgFilenames.Count);
						if (bBatchedGraphs)
						{
							csvToSvgCommandlines.Add(args);
						}
						else
						{
							Process csvToSvgProcess = LaunchCsvToSvgAsync(args);
							csvToSvgProcesses.Add(csvToSvgProcess);
						}
					}
					svgFilenames.Add(svgFilename);
                }

				if (bBatchedGraphs)
				{
					// Save the response file
					responseFilename = GetTempFilename(csvFile.filename) + "_response.txt";
					System.IO.File.WriteAllLines(responseFilename, csvToSvgCommandlines);
					Process csvToSvgProcess = LaunchCsvToSvgAsync("-batchCommands "+responseFilename +" -mt " + GetIntArg("graphThreads", Environment.ProcessorCount/2).ToString() );
					csvToSvgProcesses.Add(csvToSvgProcess);
				}
			}
            perfLog.LogTiming("    Initial Processing");

            // Read the csv stats while we wait for the graphs to complete
            int numFramesStripped = 0;
            CsvStats csvStats = ReadCsvStats(csvFile, minX, maxX);
			CsvStats unstrippedCsvStats = csvStats;

			if (!GetBoolArg("noStripEvents"))
			{
				CsvStats strippedCsvStats = StripCsvStatsByEvents(unstrippedCsvStats, out numFramesStripped);
				csvStats = strippedCsvStats;
			}

			perfLog.LogTiming("    ReadCsvStats");

            if ( writeDetailedReport )
            { 
                // wait on the graph processes to complete
                foreach (Process process in csvToSvgProcesses)
				{
					process.WaitForExit();
				}
                perfLog.LogTiming("    WaitForAsyncGraphs");
            }

            for (int i = 0; i < statsToSummarise.Length; i++)
            {
                statsToSummarise[i] = statsToSummarise[i].ToLower();
            }

			if (!GetBoolArg("readAllStats"))
			{
				// Strip out stats if they're not in the summary
				List<string> ListOfKeys = new List<string>(csvStats.Stats.Keys);
				for (int i = csvStats.Stats.Keys.Count - 1; i >= 0; i--)
				{
					if (!statsToSummarise.Contains(ListOfKeys[i]))
					{
						csvStats.Stats.Remove(ListOfKeys[i]);
						if (csvStats != unstrippedCsvStats)
						{
							unstrippedCsvStats.Stats.Remove(ListOfKeys[i]);
						}
					}
				}
			}

            // Generate CSV metadata
			if (summaryMetadata != null)
			{
                Uri currentDirUri = new Uri(Directory.GetCurrentDirectory() + "/", UriKind.Absolute);
                if ( outputDir.Length > 0 && !outputDir.EndsWith("/"))
                {
                    outputDir += "/";
                }
                Uri optionalDirUri = new Uri(outputDir, UriKind.RelativeOrAbsolute);
                Uri finalDirUri;
                if (optionalDirUri.IsAbsoluteUri)
                {
                    finalDirUri = optionalDirUri;
                }
                else
                {
                    finalDirUri = new Uri(currentDirUri,outputDir);
                }
                Uri csvFileUri = new Uri(csvFile.filename, UriKind.Absolute);

                string relativeCsvPath = finalDirUri.MakeRelativeUri(csvFileUri).ToString();
                summaryMetadata.Add("Csv File", "<a href='" + relativeCsvPath + "'>"+ shortName + ".csv" + "</a>", null, relativeCsvPath );

				if (htmlFilename != null)
				{
					summaryMetadata.Add("Report", "<a href='" + htmlFilename + "'>Link</a>");
				}
				// Pass through all the metadata from the CSV
				if (csvStats.metaData != null)
				{
					foreach (KeyValuePair<string, string> pair in csvStats.metaData.Values.ToList())
					{
						summaryMetadata.Add(pair.Key.ToLower(), pair.Value);
					}
				}

				// Add every stat avg value to the metadata
				if (GetBoolArg("readAllStats") )
				{
					foreach ( StatSamples stat in csvStats.Stats.Values )
					{
						summaryMetadata.Add( stat.Name.ToLower(), stat.average.ToString());
					}
				}

			}

			if (htmlFilename != null && !string.IsNullOrEmpty(outputDir))
            {
                htmlFilename = Path.Combine(outputDir, htmlFilename);
            }

            // Write the report
            WriteReport(htmlFilename, title, svgFilenames, reportTypeInfo, csvStats, unstrippedCsvStats, numFramesStripped, minX, maxX, bBulkMode, summaryMetadata);
            perfLog.LogTiming("    WriteReport");

            // Delete the temp files
            foreach (string svgFilename in svgFilenames)
            {
				if(svgFilename != String.Empty && File.Exists(svgFilename))
				{
					File.Delete(svgFilename);
				}
			}
			if (responseFilename != null && File.Exists(responseFilename))
			{
				File.Delete(responseFilename);
			}
		}
		CsvStats ReadCsvStats(CachedCsvFile csvFile, int minX, int maxX)
		{
			CsvStats csvStats = CsvStats.ReadCSVFromLines(csvFile.lines, null);
			reportXML.ApplyDerivedMetadata(csvStats.metaData);

			if (csvStats.metaData == null)
			{
				csvStats.metaData = new CsvMetadata();
			}
			csvStats.metaData.Values.Add("csvfilename", csvFile.filename);

			// Adjust min/max x based on the event delimiters
			string beginEventStr = GetArg("beginEvent").ToLower();
			if (beginEventStr != "")
			{
				foreach (CsvEvent ev in csvStats.Events)
				{
					if (ev.Name.ToLower() == beginEventStr)
					{
						minX = Math.Max(minX, ev.Frame);
						break;
					}
				}
			}
			string endEventStr = GetArg("endEvent").ToLower();
			if (endEventStr != "")
			{
				for (int i = csvStats.Events.Count - 1; i >= 0; i--)
				{
					CsvEvent ev = csvStats.Events[i];
					if (ev.Name.ToLower() == endEventStr)
					{
						maxX = Math.Min(maxX, ev.Frame);
						break;
					}
				}
			}

			// Strip out all stats with a zero total
			List<StatSamples> allStats = new List<StatSamples>();
			foreach (StatSamples stat in csvStats.Stats.Values)
			{
				allStats.Add(stat);
			}
			csvStats.Stats.Clear();
			foreach (StatSamples stat in allStats)
			{
				if (stat.total != 0.0f)
				{
					csvStats.AddStat(stat);
				}
			}

			// Crop the stats to the range
			csvStats.CropStats(minX, maxX);
			return csvStats;
		}

		CsvStats StripCsvStatsByEvents(CsvStats csvStats, out int numFramesStripped)
		{
			numFramesStripped = 0;
            List<CsvEventStripInfo> eventsToStrip = reportXML.GetCsvEventsToStrip();
			CsvStats strippedStats = csvStats;
            if (eventsToStrip != null)
            {
				// This can have issues if we strip events and then subsequently strip overlapping events. We'd get better results if we did it in a single pass
				foreach (CsvEventStripInfo eventStripInfo in eventsToStrip)
                {
					int numFramesStrippedThisStage = 0;
					strippedStats = strippedStats.StripByEvents(eventStripInfo.beginName, eventStripInfo.endName, false, out numFramesStrippedThisStage);
					numFramesStripped += numFramesStrippedThisStage;
				}
            }

            if (numFramesStripped > 0 )
            {
                Console.WriteLine("CSV frames excluded : " + numFramesStripped);
            }
            return strippedStats;
        }

      


        void WriteReport(string htmlFilename, string title, List<string> svgFilenames, ReportTypeInfo reportTypeInfo, CsvStats csvStats, CsvStats unstrippedCsvStats, int numFramesStripped, int minX, int maxX, bool bBulkMode, SummaryMetadata summaryMetadata)
        {
 
            ReportGraph[] graphs = reportTypeInfo.graphs.ToArray();
            string titleStr = reportTypeInfo.title + " : " + title;
            System.IO.StreamWriter htmlFile = null;

			if (htmlFilename != null)
			{
				htmlFile = new System.IO.StreamWriter(htmlFilename);
				htmlFile.WriteLine("<html>");
				htmlFile.WriteLine("  <head>");
				htmlFile.WriteLine("    <meta http-equiv='X-UA-Compatible' content='IE=edge'/>");
				if ( GetBoolArg("nocommandlineEmbed"))
				{
					htmlFile.WriteLine("    <![CDATA[ \nCreated with PerfReportTool " + Version.Get() );
				}
				else
				{
					htmlFile.WriteLine("    <![CDATA[ \nCreated with PerfReportTool " + Version.Get() + " with commandline:");
					htmlFile.WriteLine(commandLine.GetCommandLine());
				}
				htmlFile.WriteLine("    ]]>");
				htmlFile.WriteLine("    <title>" + titleStr + "</title>");
				htmlFile.WriteLine("  </head>");
				htmlFile.WriteLine("  <body><font face='verdana'>");
				htmlFile.WriteLine("  <h1>" + titleStr + "</h1>");

				// show the range
				if (minX > 0 || maxX < Int32.MaxValue)
				{
					htmlFile.WriteLine("<br><br><font size='1.5'>(CSV cropped to range " + minX + "-");
					if (maxX < Int32.MaxValue)
					{
						htmlFile.WriteLine(maxX);
					}
					htmlFile.WriteLine(")</font>");
				}

				htmlFile.WriteLine("  <h2>Summary</h2>");

				htmlFile.WriteLine("<table border='0'  bgcolor='#000000' style='width:800'>");

				if ( reportTypeInfo.metadataToShowList != null )
				{
				    Dictionary<string, string> displayNameMapping = reportXML.GetDisplayNameMapping();
    
				    foreach (string metadataStr in reportTypeInfo.metadataToShowList)
				    {
					    string value = csvStats.metaData.GetValue(metadataStr, null);
					    if (value != null)
					    {
						    string friendlyName = metadataStr;
						    if (displayNameMapping.ContainsKey(metadataStr.ToLower()))
						    {
							    friendlyName = displayNameMapping[metadataStr];
						    }
						    htmlFile.WriteLine("<tr bgcolor='#ffffff'><td bgcolor='#F0F0F0'>" + friendlyName + "</td><td><b>" + value + "</b></td></tr>");
					    }
				    }
				}
				htmlFile.WriteLine("<tr bgcolor='#ffffff'><td bgcolor='#F0F0F0'>Frame count</td><td>" + csvStats.SampleCount + " (" + numFramesStripped + " excluded)</td></tr>");
				htmlFile.WriteLine("</table>");

			}

			if (summaryMetadata != null)
            {
                summaryMetadata.Add("framecount", csvStats.SampleCount.ToString());
                if (numFramesStripped > 0)
                {
                    summaryMetadata.Add("framecountExcluded", numFramesStripped.ToString());
                }
            }

			bool bIncludeSummaryCsv = GetBoolArg("writeSummaryCsv") && !bBulkMode;

            // If the reporttype has summary info, then write out the summary]
            PeakSummary peakSummary = null;
            foreach (Summary summary in reportTypeInfo.summaries)
            {
                summary.WriteSummaryData(htmlFile, summary.useUnstrippedCsvStats ? unstrippedCsvStats : csvStats, bIncludeSummaryCsv, summaryMetadata, htmlFilename);
                if ( summary.GetType() == typeof(PeakSummary) )
                {
                    peakSummary = (PeakSummary)summary;
                }
            }

            if (htmlFile != null)
			{
				// Output the list of graphs
				htmlFile.WriteLine("<br><h3>Graphs</h3>");

				// If we are using a peak summary then we can separate the links into categories.
				// To do that we piggy back off of the information in the hidePrefixes list in the peak summary.
				List<string> sections = (peakSummary != null) ? peakSummary.sectionPrefixes : new List<string>(new string[] { "" });

				// We have to at least have the empty string in this array so that we can print the list of links.
				if (sections.Count() == 0) { sections.Add(""); }

				for (int index = 0; index < sections.Count; index++)
				{
					htmlFile.WriteLine("<ul>");
					string currentCategory = sections[index];
					if (currentCategory.Length > 0)
					{
						htmlFile.WriteLine("<h4>" + currentCategory + " Graphs</h4>");
					}
					for (int i = 0; i < svgFilenames.Count(); i++)
					{
						string svgFilename = svgFilenames[i];
						if (string.IsNullOrEmpty(svgFilename))
						{
							continue;
						}

						ReportGraph graph = graphs[i];
						string svgTitle = graph.title;
						//if (reportTypeInfo.summary.stats[i].ToLower().StartsWith(currentCategory))
						{
							htmlFile.WriteLine("<li><a href='#" + StripSpaces(svgTitle) + "'>" + svgTitle + "</a></li>");
						}
					}
					htmlFile.WriteLine("</ul>");
				}


				// Output the Graphs
				for(int svgFileIndex = 0; svgFileIndex < svgFilenames.Count; svgFileIndex++)
				{
					string svgFilename = svgFilenames[svgFileIndex];
					if (String.IsNullOrEmpty(svgFilename))
					{
						continue;
					}
					ReportGraph graph = graphs[svgFileIndex];

					string svgTitle = graph.title;
					htmlFile.WriteLine("  <br><br><a name='" + StripSpaces(svgTitle) + "'></a> <h2>" + svgTitle + "</h2>");
					if (graph.isExternal)
					{
						string outFilename = htmlFilename.Replace(".html", "_" + svgTitle.Replace(" ", "_") + ".svg");
						File.Copy(svgFilename, outFilename, true);
						htmlFile.WriteLine("<a href='" + outFilename + "'>" + svgTitle + " (external)</a>");
					}
					else
					{
						string[] svgLines = ReadLinesFromFile(svgFilename);
						foreach (string line in svgLines)
						{
							string modLine = line.Replace("__MAKEUNIQUE__", "U_" + svgFileIndex.ToString());
							htmlFile.WriteLine(modLine);
						}
					}
				}

				htmlFile.WriteLine("<p style='font-size:8'>Created with PerfReportTool " + Version.Get() + "</p>");
				htmlFile.WriteLine("  </font>");
				htmlFile.WriteLine("  </body>");
				htmlFile.WriteLine("</html>");
				htmlFile.Close();
				string ForEmail = GetArg("foremail", false);
				if (ForEmail != "")
				{
					WriteEmail(htmlFilename, title, svgFilenames, reportTypeInfo, csvStats, minX, maxX, bBulkMode);
				}
			}
        }


        void WriteEmail(string htmlFilename, string title, List<string> svgFilenames, ReportTypeInfo reportTypeInfo, CsvStats csvStats, int minX, int maxX, bool bBulkMode)
        {
			if (htmlFilename==null)
			{
				return;
			}
            ReportGraph[] graphs = reportTypeInfo.graphs.ToArray();
            string titleStr = reportTypeInfo.title + " : " + title;
            System.IO.StreamWriter htmlFile;
            htmlFile = new System.IO.StreamWriter(htmlFilename + "email");
            htmlFile.WriteLine("<html>");
            htmlFile.WriteLine("  <head>");
            htmlFile.WriteLine("    <meta http-equiv='X-UA-Compatible' content='IE=edge'/>");
            htmlFile.WriteLine("    <![CDATA[ \nCreated with PerfReportTool " + Version.Get() + " with commandline:");
            htmlFile.WriteLine(commandLine.GetCommandLine());
            htmlFile.WriteLine("    ]]>");
            htmlFile.WriteLine("    <title>" + titleStr + "</title>");
            htmlFile.WriteLine("  </head>");
            htmlFile.WriteLine("  <body><font face='verdana'>");
            htmlFile.WriteLine("  <h1>" + titleStr + "</h1>");

            // show the range
            if (minX > 0 || maxX < Int32.MaxValue)
            {
                htmlFile.WriteLine("<br><br><font size='1.5'>(CSV cropped to range " + minX + "-");
                if (maxX < Int32.MaxValue)
                {
                    htmlFile.WriteLine(maxX);
                }
                htmlFile.WriteLine(")</font>");
            }


            htmlFile.WriteLine("<a href=\"[Report Link Here]\">Click here for Report w/ interactive SVGs.</a>");
            htmlFile.WriteLine("  <h2>Summary</h2>");

            htmlFile.WriteLine("Overall Runtime: [Replace Me With Runtime]");

			bool bIncludeSummaryCsv = GetBoolArg("writeSummaryCsv") && !bBulkMode;

			// If the reporttype has summary info, then write out the summary]
			PeakSummary peakSummary = null;
            foreach (Summary summary in reportTypeInfo.summaries)
            {
                summary.WriteSummaryData(htmlFile, csvStats, bIncludeSummaryCsv, null, htmlFilename);
                if (summary.GetType() == typeof(PeakSummary))
                {
                    peakSummary = (PeakSummary)summary;
                }

            }

            htmlFile.WriteLine("  </font></body>");
            htmlFile.WriteLine("</html>");
            htmlFile.Close();

        }
        string StripSpaces( string str )
        {
            return str.Replace(" ", "");
        }

		string GetTempFilename(string csvFilename)
		{
			string shortFileName = MakeShortFilename(csvFilename).Replace(" ", "_");
			return Path.Combine( Path.GetTempPath(), shortFileName+"_"+Guid.NewGuid().ToString().Substring(26));
		}
		string GetCsvToSvgArgs(string csvFilename, string svgFilename, ReportGraph graph, double thicknessMultiplier, int minx, int maxx, bool multipleCSVs, int graphIndex, float scaleby = 1.0f)
		{
			string title = graph.title;

			GraphSettings graphSettings = graph.settings;
			string statString = graphSettings.statString.value;
			double thickness = graphSettings.thickness.value * thicknessMultiplier;
			float maxy = GetFloatArg("maxy", (float)graphSettings.maxy.value);
			bool smooth = graphSettings.smooth.value;
			double smoothKernelPercent = graphSettings.smoothKernelPercent.value;
			double smoothKernelSize = graphSettings.smoothKernelSize.value;
			double compression = graphSettings.compression.value;
			int width = graphSettings.width.value;
			int height = graphSettings.height.value;
			string additionalArgs = graphSettings.additionalArgs.value;
			bool stacked = graphSettings.stacked.value;
			bool showAverages = graphSettings.showAverages.value;
			bool filterOutZeros = graphSettings.filterOutZeros.value;
			bool snapToPeaks = false;
			if (graphSettings.snapToPeaks.isSet)
			{
				snapToPeaks = graphSettings.snapToPeaks.value;
			}

			int lineDecimalPlaces = graphSettings.lineDecimalPlaces.isSet ? graphSettings.lineDecimalPlaces.value : 1;
			int maxHierarchyDepth = graphSettings.maxHierarchyDepth.value;
			string hideStatPrefix = graphSettings.hideStatPrefix.value;
			string showEvents = graphSettings.showEvents.value;
			double statMultiplier = graphSettings.statMultiplier.isSet ? graphSettings.statMultiplier.value : 1.0;
			bool hideEventNames = false;
			if (multipleCSVs)
			{
				showEvents = "CSV:*";
				hideEventNames = true;
			}
			bool interactive = true;
			double budget = graph.budget;
			string smoothParams = "";
			if (smooth)
			{
				smoothParams = " -smooth";
				if (smoothKernelPercent >= 0.0f)
				{
					smoothParams += " -smoothKernelPercent " + smoothKernelPercent.ToString();
				}
				if (smoothKernelSize >= 0.0f)
				{
					smoothParams += " -smoothKernelSize " + smoothKernelSize.ToString();
				}
			}

			string highlightEventRegions = "";
			if (!GetBoolArg("noStripEvents"))
			{
				List<CsvEventStripInfo> eventsToStrip = reportXML.GetCsvEventsToStrip();
				if (eventsToStrip != null)
				{
					highlightEventRegions += "\"";
					for (int i = 0; i < eventsToStrip.Count; i++)
					{
						if (i > 0)
						{
							highlightEventRegions += ",";
						}
						string endEvent = (eventsToStrip[i].endName == null) ? "{NULL}" : eventsToStrip[i].endName;
						highlightEventRegions += eventsToStrip[i].beginName + "," + endEvent;
					}
					highlightEventRegions += "\"";
				}
			}

			OptionalDouble minFilterStatValueSetting = graph.minFilterStatValue.isSet ? graph.minFilterStatValue : graphSettings.minFilterStatValue;

			string args =
				" -csvs \"" + csvFilename + "\"" +
				" -title \"" + title + "\"" +
				" -o " + svgFilename +
				" -stats " + statString +
				" -width " + (width * scaleby).ToString() +
				" -height " + (height * scaleby).ToString() +
				" -budget " + budget.ToString() +
				" -maxy " + maxy.ToString() +
				" -uniqueID Graph_" + graphIndex.ToString() +
				" -lineDecimalPlaces " + lineDecimalPlaces.ToString() +
				" -nocommandlineEmbed "+

				((statMultiplier != 1.0) ? " -statMultiplier " + statMultiplier.ToString("0.0000000000000000000000") : "") +
				(hideEventNames ? " -hideeventNames 1" : "") +
				((minx > 0) ? (" -minx " + minx.ToString()) : "") +
				((maxx != Int32.MaxValue) ? (" -maxx " + maxx.ToString()) : "") +
				OptionalHelper.GetDoubleSetting(graphSettings.miny, " -miny ") +
				OptionalHelper.GetDoubleSetting(graphSettings.threshold, " -threshold ") +
				OptionalHelper.GetDoubleSetting(graphSettings.averageThreshold, " -averageThreshold ") +
				OptionalHelper.GetDoubleSetting(minFilterStatValueSetting, " -minFilterStatValue ") +
				OptionalHelper.GetStringSetting(graphSettings.minFilterStatName, " -minFilterStatName ") +
				(compression > 0.0 ? " -compression " + compression.ToString() : "") +
				(thickness > 0.0 ? " -thickness " + thickness.ToString() : "") +
				smoothParams +
				(interactive ? " -interactive" : "") +
				(stacked ? " -stacked -forceLegendSort" : "") +
				(showAverages ? " -showAverages" : "") +
				(snapToPeaks ? "" : " -nosnap") +
				(filterOutZeros ? " -filterOutZeros" : "") +
				(maxHierarchyDepth >= 0 ? " -maxHierarchyDepth " + maxHierarchyDepth.ToString() : "") +
				(hideStatPrefix.Length > 0 ? " -hideStatPrefix " + hideStatPrefix : "") +
				(graphSettings.mainStat.isSet ? " -stacktotalstat " + graphSettings.mainStat.value : "") +
				(showEvents.Length > 0 ? " -showevents " + showEvents : "") +
				(highlightEventRegions.Length > 0 ? " -highlightEventRegions " + highlightEventRegions : "") +
				(graphSettings.legendAverageThreshold.isSet ? " -legendAverageThreshold " + graphSettings.legendAverageThreshold.value : "") +

				(graphSettings.ignoreStats.isSet ? " -ignoreStats " + graphSettings.ignoreStats.value : "") +
				" " + additionalArgs;
			return args;
		}

		Process LaunchCsvToSvgAsync(string args)
		{
			string csvToolPath = GetBaseDirectory() + "/CSVToSVG.exe";
			string binary = csvToolPath;

			// run mono on non-Windows hosts
			if (Host != HostPlatform.Windows)
			{
				// note, on Mac mono will not be on path
				binary = Host == HostPlatform.Linux ? "mono" : "/Library/Frameworks/Mono.framework/Versions/Current/Commands/mono";
				args = csvToolPath + " " + args;
			}
            
            // Generate the SVGs, multithreaded
            ProcessStartInfo startInfo = new ProcessStartInfo(binary);
            startInfo.Arguments = args;
            startInfo.CreateNoWindow = true;
            startInfo.UseShellExecute = false;
            Process process = Process.Start(startInfo);
			return process;
        }

        int CountCSVs( CsvStats csvStats)
        {
            // Count the CSVs
            int csvCount = 0;
            foreach (CsvEvent ev in csvStats.Events)
            {
                string eventName = ev.Name;
                if (eventName.Length > 0)
                {

                    if (eventName.Contains("CSV:") && eventName.ToLower().Contains(".csv"))
                    {
                        csvCount++;
                    }
                }
            }
            if (csvCount == 0)
            {
                csvCount = 1;
            }
            return csvCount;
        }

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
    }

    class ReportTypeInfo
    {
        public ReportTypeInfo(XElement element, Dictionary<string,XElement> sharedSummaries, string baseXmlDirectory)
        {
            graphs = new List<ReportGraph>();
            summaries = new List<Summary>();
            title = element.Attribute("title").Value;
            foreach (XElement child in element.Elements())
            {
				if (child.Name == "graph")
				{
					ReportGraph graph = new ReportGraph(child);
					graphs.Add(graph);
				}
				else if (child.Name == "summary" || child.Name=="summaryRef")
				{
					XElement summaryElement = null;
					if (child.Name == "summaryRef")
					{
						summaryElement = sharedSummaries[child.Attribute("name").Value];
					}
					else
					{
						summaryElement = child;
					}
					string summaryType = summaryElement.Attribute("type").Value;
					if (summaryType == "histogram")
					{
						summaries.Add(new HistogramSummary(summaryElement, baseXmlDirectory));
					}
					else if (summaryType == "peak")
					{
						summaries.Add(new PeakSummary(summaryElement, baseXmlDirectory));
					}
					else if (summaryType == "fpschart")
					{
						summaries.Add(new FPSChartSummary(summaryElement, baseXmlDirectory));
					}
					else if (summaryType == "hitches")
					{
						summaries.Add(new HitchSummary(summaryElement, baseXmlDirectory));
					}
					else if (summaryType == "event")
					{
						summaries.Add(new EventSummary(summaryElement, baseXmlDirectory));
					}
					else if (summaryType == "boundedstatvalues")
					{
						summaries.Add(new BoundedStatValuesSummary(summaryElement, baseXmlDirectory));
					}
					else if (summaryType == "mapoverlay")
					{
						summaries.Add(new MapOverlaySummary(summaryElement, baseXmlDirectory));
					}
				}
				else if (child.Name == "metadataToShow")
				{
					metadataToShowList = child.Value.Split(',');
				}

            }
        }

		public List<ReportGraph> graphs;
        public List<Summary> summaries;
        public string title;
		public string [] metadataToShowList;
	};


    class ReportGraph
    {
        public ReportGraph(XElement element)
        {
            title = element.Attribute("title").Value;
            budget = Convert.ToDouble(element.Attribute("budget").Value, System.Globalization.CultureInfo.InvariantCulture);
            inSummary = XmlHelper.ReadAttributeBool(element, "inSummary", false);
            isInMainSummary = XmlHelper.ReadAttributeBool(element, "inMainSummary", false);
			isExternal = XmlHelper.ReadAttributeBool(element, "external", false);
			minFilterStatValue = new OptionalDouble(element, "minFilterStatValue");
		}
        public string title;
        public double budget;
        public bool inSummary;
		public bool isExternal;
		public bool isInMainSummary;
		public OptionalDouble minFilterStatValue;
        public GraphSettings settings;
    };

    class GraphSettings
    {
        public GraphSettings(XElement element)
        {
            smooth = new OptionalBool(element, "smooth");
            thickness = new OptionalDouble(element, "thickness");
			miny = new OptionalDouble(element, "miny");
            maxy = new OptionalDouble(element, "maxy");
			threshold = new OptionalDouble(element, "threshold");
			averageThreshold = new OptionalDouble(element, "averageThreshold");
			minFilterStatValue = new OptionalDouble(element, "minFilterStatValue");
			minFilterStatName = new OptionalString(element, "minFilterStatName");
			smoothKernelPercent = new OptionalDouble(element, "smoothKernelPercent");
            smoothKernelSize = new OptionalDouble(element, "smoothKernelSize");
            compression = new OptionalDouble(element, "compression");
            width = new OptionalInt(element, "width");
            height = new OptionalInt(element, "height");
            stacked = new OptionalBool(element, "stacked");
            showAverages = new OptionalBool(element, "showAverages");
            filterOutZeros = new OptionalBool(element, "filterOutZeros");
            maxHierarchyDepth = new OptionalInt(element, "maxHierarchyDepth");
            hideStatPrefix = new OptionalString(element, "hideStatPrefix");
            mainStat = new OptionalString(element, "mainStat");
            showEvents = new OptionalString(element, "showEvents");
            requiresDetailedStats = new OptionalBool(element, "requiresDetailedStats");
            ignoreStats = new OptionalString(element, "ignoreStats");

            statString = new OptionalString(element, "statString", true);
            additionalArgs = new OptionalString(element, "additionalArgs", true);
            statMultiplier = new OptionalDouble(element, "statMultiplier");
			legendAverageThreshold = new OptionalDouble(element, "legendAverageThreshold");
			snapToPeaks = new OptionalBool(element, "snapToPeaks");
			lineDecimalPlaces = new OptionalInt(element, "lineDecimalPlaces");
		}
		public void InheritFrom(GraphSettings baseSettings)
        {
            smooth.InheritFrom(baseSettings.smooth);
            statString.InheritFrom(baseSettings.statString);
            thickness.InheritFrom(baseSettings.thickness);
			miny.InheritFrom(baseSettings.miny);
            maxy.InheritFrom(baseSettings.maxy);
			threshold.InheritFrom(baseSettings.threshold);
			averageThreshold.InheritFrom(baseSettings.averageThreshold);
			minFilterStatValue.InheritFrom(baseSettings.minFilterStatValue);
			minFilterStatName.InheritFrom(baseSettings.minFilterStatName);
            smoothKernelSize.InheritFrom(baseSettings.smoothKernelSize);
            smoothKernelPercent.InheritFrom(baseSettings.smoothKernelPercent);
            compression.InheritFrom(baseSettings.compression);
            width.InheritFrom(baseSettings.width);
            height.InheritFrom(baseSettings.height);
            additionalArgs.InheritFrom(baseSettings.additionalArgs);
            stacked.InheritFrom(baseSettings.stacked);
            showAverages.InheritFrom(baseSettings.showAverages);
            filterOutZeros.InheritFrom(baseSettings.filterOutZeros);
            maxHierarchyDepth.InheritFrom(baseSettings.maxHierarchyDepth);
            hideStatPrefix.InheritFrom(baseSettings.hideStatPrefix);
            mainStat.InheritFrom(baseSettings.mainStat);
            showEvents.InheritFrom(baseSettings.showEvents);
            requiresDetailedStats.InheritFrom(baseSettings.requiresDetailedStats);
            statMultiplier.InheritFrom(baseSettings.statMultiplier);
            ignoreStats.InheritFrom(baseSettings.ignoreStats);
			legendAverageThreshold.InheritFrom(baseSettings.legendAverageThreshold);
			snapToPeaks.InheritFrom(baseSettings.snapToPeaks);
			lineDecimalPlaces.InheritFrom(baseSettings.lineDecimalPlaces);

		}
        public OptionalBool smooth;
        public OptionalString statString;
        public OptionalDouble thickness;
		public OptionalDouble miny;
        public OptionalDouble maxy;
		public OptionalDouble threshold;
		public OptionalDouble averageThreshold;
		public OptionalDouble minFilterStatValue;
		public OptionalString minFilterStatName;
		public OptionalDouble smoothKernelSize;
        public OptionalDouble smoothKernelPercent;
        public OptionalDouble compression;
        public OptionalInt width;
        public OptionalInt height;
        public OptionalString additionalArgs;
        public OptionalBool stacked;
        public OptionalBool showAverages;
        public OptionalBool filterOutZeros;
        public OptionalInt maxHierarchyDepth;
        public OptionalString hideStatPrefix;
        public OptionalString mainStat;
        public OptionalString showEvents;
        public OptionalString ignoreStats;
        public OptionalDouble statMultiplier;
		public OptionalDouble legendAverageThreshold;

		public OptionalBool requiresDetailedStats;
		public OptionalBool snapToPeaks;
		public OptionalInt lineDecimalPlaces;

	};

	static class Extensions
	{
		public static T GetSafeAttibute<T>(this XElement element, string attributeName, T defaultValue = default(T))
		{
			XAttribute attribute = element.Attribute(attributeName);
			if (attribute != null)
			{
				if (typeof(T) == typeof(bool))
				{
					return (T)Convert.ChangeType(Convert.ChangeType(attribute.Value, typeof(int)), typeof(bool));
				}
				else
				{
					return (T)Convert.ChangeType(attribute.Value, typeof(T));
				}
			}
			return defaultValue;
		}


    };

    class CsvEventStripInfo
    {
        public string beginName;
        public string endName;
    };


	class DerivedMetadataEntry
	{
		public DerivedMetadataEntry(string inSourceName, string inSourceValue, string inDestName, string inDestValue )
		{
			sourceName = inSourceName;
			sourceValue = inSourceValue;
			destName = inDestName;
			destValue = inDestValue;
		}
		public string sourceName;
		public string sourceValue;
		public string destName;
		public string destValue;
	};

	class DerivedMetadataMappings
	{
		public DerivedMetadataMappings()
		{
			entries = new List<DerivedMetadataEntry>();
		}
		public void ApplyMapping(CsvMetadata csvMetadata)
		{
			if (csvMetadata != null)
			{
				foreach (DerivedMetadataEntry entry in entries)
				{
					if (csvMetadata.Values.ContainsKey(entry.sourceName.ToLowerInvariant()))
					{
						if (csvMetadata.Values[entry.sourceName].ToLowerInvariant() == entry.sourceValue.ToLowerInvariant())
						{
							csvMetadata.Values.Add(entry.destName.ToLowerInvariant(), entry.destValue);
						}
					}
				}
			}
		}
		public List<DerivedMetadataEntry> entries;
	}


	class ReportXML
	{
		bool IsAbsolutePath(string path)
		{
			if (path.Length > 3 && path[1] == ':' && (path[2] == '\\' || path[2] == '/'))
			{
				return true;
			}
			return false;
		}

		public ReportXML(string graphXMLFilenameIn, string reportXMLFilenameIn, string baseXMLDirectoryOverride)
		{
			string location = System.Reflection.Assembly.GetEntryAssembly().Location.ToLower();
			string baseDirectory = location.Replace("perfreporttool.exe", "");

            // Check if this is a debug build, and redirect base dir to binaries if so
            if ( baseDirectory.Contains("\\engine\\source\\programs\\") && baseDirectory.Contains("\\csvtools\\") && baseDirectory.Contains("\\bin\\debug\\"))
            {
                baseDirectory = baseDirectory.Replace("\\engine\\source\\programs\\", "\\engine\\binaries\\dotnet\\");
                int csvToolsIndex = baseDirectory.LastIndexOf("\\csvtools\\");
                baseDirectory = baseDirectory.Substring(0, csvToolsIndex + "\\csvtools\\".Length);
            }

			// Check if the base directory is being overridden
			if (baseXMLDirectoryOverride.Length > 0)
			{
				if (IsAbsolutePath(baseXMLDirectoryOverride))
				{
					baseDirectory = baseXMLDirectoryOverride;
				}
				else
				{
					baseDirectory = Path.Combine(baseDirectory, baseXMLDirectoryOverride);
				}
			}
			Console.Out.WriteLine("BaseDir: " + baseDirectory);

			baseXmlDirectory = baseDirectory;

			// Read the report type XML
			reportTypeXmlFilename = Path.Combine(baseDirectory, "reportTypes.xml" );
			if (reportXMLFilenameIn.Length > 0)
			{
				// Check if this is an absolute path
				if (IsAbsolutePath(reportXMLFilenameIn))
				{
					reportTypeXmlFilename = reportXMLFilenameIn;
				}
				else
				{
					reportTypeXmlFilename = Path.Combine(baseDirectory, reportXMLFilenameIn);
				}
			}
			XDocument reportTypesDoc = XDocument.Load(reportTypeXmlFilename);
			rootElement = reportTypesDoc.Element("root");
			if (rootElement == null)
			{
				throw new Exception("No root element found in report XML " + reportTypeXmlFilename);
			}

            reportTypesElement = rootElement.Element("reporttypes");
            if (reportTypesElement == null)
            {
                throw new Exception("No reporttypes element found in report XML " + reportTypeXmlFilename);
            }

            // Read the graph XML
            string graphsXMLFilename;
            if (graphXMLFilenameIn.Length > 0)
            {
                if (IsAbsolutePath(graphXMLFilenameIn))
                {
                    graphsXMLFilename = graphXMLFilenameIn;
                }
                else
                {
                    graphsXMLFilename = Path.Combine( baseDirectory, graphXMLFilenameIn );
                }
            }
            else
            {
                graphsXMLFilename = reportTypesElement.GetSafeAttibute<string>("reportGraphsFile");
                if (graphsXMLFilename != null)
                {
                    graphsXMLFilename = Path.GetDirectoryName(reportTypeXmlFilename) + "\\" + graphsXMLFilename;
                }
                else
                {
                    graphsXMLFilename = Path.Combine( baseDirectory, "reportGraphs.xml" );
                }

            }


			XDocument reportGraphsDoc = XDocument.Load(graphsXMLFilename);
			graphGroupsElement = reportGraphsDoc.Element("graphGroups");

			// Read the base settings - all other settings will inherit from this
			GraphSettings baseSettings = new GraphSettings(graphGroupsElement.Element("baseSettings"));
			if (reportTypesElement == null)
			{
				throw new Exception("No baseSettings element found in graph XML " + graphsXMLFilename);
			}

			graphs = new Dictionary<string, GraphSettings>();
			foreach (XElement graphGroupElement in graphGroupsElement.Elements())
			{
				if (graphGroupElement.Name == "graphGroup")
				{
					// Create the base settings
					XElement settingsElement = graphGroupElement.Element("baseSettings");
					GraphSettings groupSettings = new GraphSettings(settingsElement);
					groupSettings.InheritFrom(baseSettings);
					foreach (XElement graphElement in graphGroupElement.Elements())
					{
						if (graphElement.Name == "graph")
						{
							string title = graphElement.Attribute("title").Value.ToLower();
							GraphSettings graphSettings = new GraphSettings(graphElement);
							graphSettings.InheritFrom(groupSettings);
							graphs.Add(title, graphSettings);
						}
					}
				}
			}

			// Read the display name mapping
			statDisplayNameMapping = new Dictionary<string, string>();
			XElement displayNameElement = rootElement.Element("statDisplayNameMappings");
			if (displayNameElement != null)
			{
				foreach (XElement mapping in displayNameElement.Elements("mapping"))
				{
					string statName = mapping.GetSafeAttibute<string>("statName");
					string displayName = mapping.GetSafeAttibute<string>("displayName");
					if (statName != null && displayName != null)
					{
						statDisplayNameMapping.Add(statName.ToLower(), displayName);
					}
				}
			}

			// Read the derived metadata mappings
			derivedMetadataMappings = new DerivedMetadataMappings();
			XElement derivedMetadataMappingsElement = rootElement.Element("derivedMetadataMappings");
			if (derivedMetadataMappingsElement != null)
			{
				foreach (XElement mapping in derivedMetadataMappingsElement.Elements("mapping"))
				{
					string sourceName = mapping.GetSafeAttibute<string>("sourceName");
					string sourceValue = mapping.GetSafeAttibute<string>("sourceValue");
					string destName = mapping.GetSafeAttibute<string>("destName");
					string destValue = mapping.GetSafeAttibute<string>("destValue");
					if (sourceName == null || sourceValue == null || destName == null || destValue == null)
					{
						throw new Exception("Derivedmetadata mapping is missing a required attribute!\nRequired attributes: sourceName, sourceValue, destName, destValue.\nXML: "+mapping.ToString());
					}
					derivedMetadataMappings.entries.Add(new DerivedMetadataEntry(sourceName, sourceValue, destName, destValue));
				}
			}

			// Read events to strip
			XElement eventsToStripEl = rootElement.Element("csvEventsToStrip");
            if (eventsToStripEl != null)
            {
                csvEventsToStrip = new List<CsvEventStripInfo>();
                foreach (XElement eventPair in eventsToStripEl.Elements("eventPair"))
                {
                    CsvEventStripInfo eventInfo = new CsvEventStripInfo();
                    eventInfo.beginName = eventPair.GetSafeAttibute<string>("begin");
                    eventInfo.endName = eventPair.GetSafeAttibute<string>("end");

                    if (eventInfo.beginName == null)
                    {
                        throw new Exception("eventPair with no begin attribute found!");
                    }
                    csvEventsToStrip.Add(eventInfo);
                }
            }

			summaryTablesElement = rootElement.Element("summaryTables");
			if (summaryTablesElement != null)
			{
				summaryTables = new Dictionary<string, SummaryTableInfo>();
				foreach (XElement summaryElement in summaryTablesElement.Elements("summaryTable"))
				{
					SummaryTableInfo table = new SummaryTableInfo(summaryElement);
					summaryTables.Add(summaryElement.Attribute("name").Value.ToLower(), table);
				}
			}

			// Add any shared summaries
			XElement sharedSummariesElement = rootElement.Element("sharedSummaries");
			sharedSummaries = new Dictionary<string, XElement>();
			if (sharedSummariesElement != null)
			{
				foreach (XElement summaryElement in sharedSummariesElement.Elements("summary"))
				{
					sharedSummaries.Add(summaryElement.Attribute("refName").Value, summaryElement);
				}
			}

		}

		public ReportTypeInfo GetReportTypeInfo(string reportType, CachedCsvFile csvFile, bool bBulkMode, bool forceReportType )
		{
			ReportTypeInfo reportTypeInfo = null;
			if (reportType == "")
			{
				// Attempt to determine the report type automatically based on the stats
				foreach (XElement element in reportTypesElement.Elements("reporttype"))
				{
					if (IsReportTypeXMLCompatibleWithStats(element, csvFile.dummyCsvStats))
					{
						reportTypeInfo = new ReportTypeInfo(element, sharedSummaries, baseXmlDirectory);
						break;
					}
				}
				if (reportTypeInfo == null)
				{
					throw new Exception("Compatible report type for CSV "+csvFile.filename+" could not be found in" + reportTypeXmlFilename);
				}
			}
			else
			{
				XElement foundReportTypeElement = null;
				foreach (XElement element in reportTypesElement.Elements("reporttype"))
				{
					if (element.Attribute("name").Value.ToLower() == reportType)
					{
						foundReportTypeElement = element;
					}
				}
				if (foundReportTypeElement == null)
				{
					throw new Exception("Report type " + reportType + " not found in " + reportTypeXmlFilename);
				}

                if (!IsReportTypeXMLCompatibleWithStats(foundReportTypeElement, csvFile.dummyCsvStats))
                {
                    if (forceReportType)
                    {
                        Console.Out.WriteLine("Report type " + reportType + " is not compatible with CSV " + csvFile.filename + ", but using it anyway");
                    }
                    else
                    {
                        throw new Exception("Report type " + reportType + " is not compatible with CSV " + csvFile.filename);
                    }
                }
                reportTypeInfo = new ReportTypeInfo(foundReportTypeElement, sharedSummaries, baseXmlDirectory);
            }

            // Load the graphs
            foreach (ReportGraph graph in reportTypeInfo.graphs)
			{
				string key = graph.title.ToLower();
				if (graphs.ContainsKey(key))
				{
					graph.settings = graphs[key];
				}
				else
				{
					throw new Exception("Graph with title \"" + graph.title + "\" was not found in graphs XML");
				}
			}

			foreach (Summary summary in reportTypeInfo.summaries)
			{
				summary.PostInit(reportTypeInfo, csvFile.dummyCsvStats);
			}
			return reportTypeInfo;
		} 

		bool IsReportTypeXMLCompatibleWithStats(XElement reportTypeElement, CsvStats csvStats)
		{
			XAttribute nameAt = reportTypeElement.Attribute("name");
			if (nameAt == null)
			{
				return false;
			}
			string reportTypeName = nameAt.Value;

			XElement autoDetectionEl = reportTypeElement.Element("autodetection");
			if (autoDetectionEl == null)
			{
				return false;
			}
			XAttribute requiredStatsAt = autoDetectionEl.Attribute("requiredstats");
			if (requiredStatsAt != null)
			{
				string[] requiredStats = requiredStatsAt.Value.Split(',');
				foreach (string stat in requiredStats)
				{
					if (csvStats.GetStatsMatchingString(stat).Count == 0)
					{
						return false;
					}
				}
			}
			foreach (XElement requiredMetadataEl in autoDetectionEl.Elements("requiredmetadata"))
			{
				XAttribute keyAt = requiredMetadataEl.Attribute("key");
				if (keyAt == null)
				{
					throw new Exception("Report type " + reportTypeName + " has no 'key' attribute!");
				}
				XAttribute allowedValuesAt = requiredMetadataEl.Attribute("allowedValues");
				if (allowedValuesAt == null)
				{
					throw new Exception("Report type " + reportTypeName + " has no 'allowedValues' attribute!");
				}

                bool ignoreIfKeyNotFound = requiredMetadataEl.GetSafeAttibute("ignoreIfKeyNotFound", true);
                bool stopIfKeyFound = requiredMetadataEl.GetSafeAttibute("stopIfKeyFound", false);


                string key = keyAt.Value.ToLower();
				if (csvStats.metaData.Values.ContainsKey(key))
				{
					string value = csvStats.metaData.Values[key].ToLower();
					string[] allowedValues = allowedValuesAt.Value.ToString().ToLower().Split(',');
					if (!allowedValues.Contains(value))
					{
						return false;
					}
                    if (stopIfKeyFound)
                    {
                        break;
                    }
                }
                else if (ignoreIfKeyNotFound == false)
                {
                    return false;
                }
            }

            Console.Out.WriteLine("Autodetected report type: " + reportTypeName);

			return true;
        } 


        public Dictionary<string, string> GetDisplayNameMapping() { return statDisplayNameMapping; }

		public SummaryTableInfo GetSummaryTable(string name)
		{
			if (summaryTables.ContainsKey(name))
			{
				return summaryTables[name.ToLower()];
			}
			else
			{
				throw new Exception("Requested summary table type '" + name + "' was not found in <summaryTables>");
			}
		}

        public List<CsvEventStripInfo> GetCsvEventsToStrip()
        {
            return csvEventsToStrip;
        }

		public void ApplyDerivedMetadata(CsvMetadata csvMetadata)
		{
			derivedMetadataMappings.ApplyMapping(csvMetadata);
		}


        Dictionary<string, SummaryTableInfo> summaryTables;

		XElement reportTypesElement;
		XElement rootElement;
		XElement graphGroupsElement;
		XElement summaryTablesElement;
		Dictionary<string,XElement> sharedSummaries;
		Dictionary<string, GraphSettings> graphs;
		Dictionary<string, string> statDisplayNameMapping;
		DerivedMetadataMappings derivedMetadataMappings;
		string baseXmlDirectory;

		List<CsvEventStripInfo> csvEventsToStrip;
        string reportTypeXmlFilename;
	}

    class CsvFileCache
    {
        public CsvFileCache( string[] inCsvFilenames, int inLookaheadCount, int inThreadCount )
        {
            csvFileInfos = new CsvFileInfo[inCsvFilenames.Length];
            for (int i = 0; i < inCsvFilenames.Length; i++)
            {
                csvFileInfos[i] = new CsvFileInfo(inCsvFilenames[i]);
            }
            fileCache = this;
            writeIndex = 0;
            readIndex = 0;
            lookaheadCount = inLookaheadCount;
            countFreedSinceLastGC = 0;

            // Kick off the workers (must be done last)
            if (inLookaheadCount > 0)
            {
                precacheThreads = new Thread[inThreadCount];
                precacheJobs = new ThreadStart[inThreadCount];
                for (int i = 0; i < precacheThreads.Length; i++)
                {
                    precacheJobs[i] = new ThreadStart(PrecacheThreadRun);
                    precacheThreads[i] = new Thread(precacheJobs[i]);
                    precacheThreads[i].Start();
                }
            }
        }

        public CachedCsvFile GetNextCachedCsvFile()
        {
            CachedCsvFile file = null;
            if ( readIndex >= csvFileInfos.Length)
            {
                // We're done
                return null;
            }
            CsvFileInfo fileInfo = csvFileInfos[readIndex];
            if (precacheThreads == null)
            {
                file = new CachedCsvFile(fileInfo.filename);
            }
            else
            {
                while (true)
                {
                    lock (fileInfo.cs)
                    {
                        if (fileInfo.isReady)
                        {
                            file = fileInfo.cachedFile;
                            fileInfo.cachedFile = null;
                            countFreedSinceLastGC++;
                            // Periodically GC
                            if (countFreedSinceLastGC>16)
                            {
                                GC.Collect();
                                GC.WaitForPendingFinalizers();
                                countFreedSinceLastGC = 0;
                            }
                            break;
                        }
                    }
					// The data isn't ready yet, so sleep for a bit
                    Thread.Sleep(1);
                }
            }
            readIndex++;
            return file;
        }

        static void PrecacheThreadRun()
        {
            fileCache.ThreadRun();
        }

        void ThreadRun()
        {
            int threadWriteIndex = 0;
            while (true)
            {
                threadWriteIndex = Interlocked.Increment(ref writeIndex)-1;
                if ( threadWriteIndex >= csvFileInfos.Length )
                {
                    // We're done
                    break;
                }
                CsvFileInfo fileInfo = csvFileInfos[threadWriteIndex];

                // If we're too far ahead of the read index, sleep. Not doing so could increase memory usage significantly
                while (threadWriteIndex - readIndex > lookaheadCount)
                {
                    Thread.Sleep(10);
                }
                // Process the file
                lock (fileInfo.cs)
                {
                    fileInfo.cachedFile = new CachedCsvFile(fileInfo.filename);
                    fileInfo.isReady = true;
                }
            }
        }



        ThreadStart [] precacheJobs;
        Thread [] precacheThreads;
        int writeIndex;
        int readIndex;
        int lookaheadCount;
        int countFreedSinceLastGC;

        static CsvFileCache fileCache;

        class CsvFileInfo
        {
            public CsvFileInfo(string inFilename)
            {
                filename = inFilename;
                cachedFile = null;
                isReady = false;
                cs = new object();
            }
            public CachedCsvFile cachedFile;
            public bool isReady;
            public object cs;
            public string filename;
        }
        CsvFileInfo[] csvFileInfos;

    }
}

