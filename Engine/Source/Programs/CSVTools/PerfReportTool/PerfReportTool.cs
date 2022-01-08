// Copyright (C) Microsoft. All rights reserved.
// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Xml.Linq;
using System.IO;
using System.Diagnostics;
using CSVStats;
using System.Collections;
using System.Security.Cryptography;

using PerfSummaries;
using System.Globalization;

namespace PerfReportTool
{
    class Version
    {
        private static string VersionString = "4.69";

        public static string Get() { return VersionString; }
    };

	class HashHelper
	{
		public static string StringToHashStr(string strIn, int maxCharsOut=-1)
		{
			HashAlgorithm algorithm = SHA256.Create();
			StringBuilder sb = new StringBuilder();

			byte[] hash = algorithm.ComputeHash(Encoding.UTF8.GetBytes(strIn));
			StringBuilder sbOut = new StringBuilder();
			foreach (byte b in hash)
			{
				sbOut.Append(b.ToString("X2"));
			}
			string strOut = sbOut.ToString();
			if (maxCharsOut > 0)
			{
				return strOut.Substring(0, maxCharsOut);
			}
			return strOut;
		}
	}

	class SummaryTableCacheStats
	{
		public int WriteCount = 0;
		public int HitCount = 0;
		public int MissCount = 0;
		public int PurgeCount = 0;

		public void LogStats()
		{
			Console.WriteLine("Summary Table Cache stats:");
			Console.WriteLine("  Cache hits      : " + HitCount);
			Console.WriteLine("  Cache misses    : " + MissCount);
			Console.WriteLine("  Cache writes    : " + WriteCount);
			if (PurgeCount > 0)
			{
				Console.WriteLine("  Files purged    : " + PurgeCount);
			}
			if (HitCount > 0 || MissCount > 0)
			{
				Console.WriteLine("  Hit percentage  : " + ((float)HitCount * 100.0f / ((float)MissCount+(float)HitCount)).ToString("0.0") + "%");
			}
		}
	};

	class Program : CommandLineTool
    {
		static string formatString =
			"PerfReportTool v" + Version.Get() + "\n" +
			"\n" +
			"Format: \n" +
			"       -csv <filename> or -csvdir <directory path> or -summaryTableCacheIn <directory path>\n" +
			"       -o <dir name>: output directory (will be created if necessary)\n" +
			"\n" +
			"Optional Args:\n" +
			"       -reportType <e.g. flythrough, playthrough, playthroughmemory>\n" +
			"       -reportTypeCompatCheck : do a compatibility if when specifying a report type (rather than forcing)\n"+
			"       -graphXML <xmlfilename>\n" +
			"       -reportXML <xmlfilename>\n" +
			"       -reportxmlbasedir <folder>\n" +
			"       -title <name>\n" +
			"       -maxy <value> - forces all graphs to use this value\n" +
			"       -writeSummaryCsv : if specified, a csv file containing summary information will be generated.\n"+
			"          Not available in bulk mode.\n" +
			"       -noWatermarks : don't embed the commandline or version in reports\n"+
			"       -cleanCsvOut <filename> : write a standard format CSV after event stripping with metadata stripped out.\n"+
			"          Not available in bulk mode.\n" +
			"       -noSmooth : disable smoothing on all graphs\n" +
			"       -listSummaryTables: lists available summary tables from the current report XML\n" +
			"\n" +
			"Performance args:\n" +
			"       -perfLog : output performance logging information\n" +
			"       -noBatchedGraphs : disable batched/multithreaded graph generation (default is enabled)\n" +
			"       -graphThreads : use with -batchedGraphs to control the number of threads per CsvToSVG instance \n"+
			"                       (default: PC core count/2)\n" +
			"\n" +
			"Options to truncate or filter source data:\n" +
			"Warning: these options disable Summary Table caching\n" +
			"       -minx <frameNumber>\n" +
			"       -maxx <frameNumber>\n" +
			"       -beginEvent <event> : strip data before this event\n" +
			"       -endEvent <event> : strip data after this event\n" +
			"       -noStripEvents : if specified, don't strip out samples between excluded events from the stats\n" +
			"\n" +
			"Optional bulk mode args: (use with -csvdir or -summaryTableCacheIn)\n" +
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
			"       -condensedSummaryTable <name> :\n" +
			"           Selects a custom condensed summary table type from the list in reportTypes.xml \n"+
			"           (if not specified, 'condensed' will be used)\n" +
			"       -summaryTableFilename <name> : use the specified filename for the summary table (instead of SummaryTable.html)\n"+
			"       -metadataFilter <query> or <key0=value0,key1=value1...>: filters based on CSV metadata,\n"+
			"           e.g \"platform=ps4 AND deviceprofile=ps4_60\" \n" +
			"       -readAllStats : allows any CSV stat avg to appear in the summary table, not just those referenced in summaries\n" +
			"       -showHiddenStats : shows stats which have been automatically hidden (typically duplicate csv unit stats)\n" +
			"       -externalGraphs : enables external graphs (off by default)\n" +
			"       -spreadsheetfriendly: outputs a single quote before non-numeric entries in summary tables\n" +
			"       -noSummaryMinMax: don't make min/max columns for each stat in a condensed summary\n" +
			"       -reverseTable: Reverses the order of summary tables\n"+
			"       -scrollableTable: makes the summary table scrollable, with frozen first rows and columns\n" +
			"       -maxSummaryTableStringLength <n>: strings longer than this will get truncated\n" +
			"       -allowDuplicateCSVs : doesn't remove duplicate CSVs (Note: can cause summary table cache file locking issues)\n"+
			"       -requireMetadata : ignores CSVs without metadata\n" +
			"       -listFiles : just list all files that pass the metadata query. Don't generate any reports.\n" +
			"       -reportLinkRootPath <path> : Make report links relative to this\n" +
			"       -csvLinkRootPath <path> : Make CSV file links relative to this\n" +
			"       -weightByColumn : weight collated table averages by this column (overrides value specified in the report XML)\n" +
			"       -noWeightedAvg : Don't use weighted averages for the collated table\n" +
			"       -minFrameCount <n> : ignore CSVs without at least this number of valid frames\n" +
			"       -maxFileAgeDays <n> : max file age in days. CSV or PRC files older than this will be ignored\n" +
			"\n" +
			"Performance args for bulk mode:\n" +
			"       -precacheCount <n> : number of CSV files to precache in the lookahead cache (0 for no precache)\n" +
			"       -precacheThreads <n> : number of threads to use for the CSV lookahead cache (default 8)\n" +
			"       -summaryTableCache <dir> : specifies a directory for summary table data to be cached.\n"+
			"           This avoids processing csvs on subsequent runs when -noDetailedReports is specified\n" +
			"           Note: Enables -readAllStats implicitly. \n"+
			"       -summaryTableCacheInvalidate : regenerates summary table disk cache entries (ie write only)\n" +
			"       -summaryTableCacheReadOnly : only read from the cache, never write\n" +
			"       -summaryTableCachePurgeInvalid : Purges invalid PRCs from the cache folder\n" +
			"       -summaryTableCacheIn <dir> : reads data directly from the summary table cache instead of from CSVs\n" +
			"       -summaryTableCacheUseOnlyCsvID : only use the CSV ID for the summary table cacheID, ignoringthe report type hash\n"+
			"            Use this if you want to avoid cache data being invalidated by report changes\n" +
			"       -noCsvCacheFiles: disables usage of .csv.cache files. Cache files can be much faster if filtering on metadata\n" +
			"";
			/*
			"Note on custom tables:\n" +
			"       The -customTable and -customTableSort args allow you to generate a custom summary table\n" +
			"       This is an alternative to using preset summary tables (see -summarytable)\n" +
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

			SummaryFactory.Init();

			string csvDir = null;

			bool bBulkMode = false;
			bool bSummaryTableCacheOnlyMode = false;
			// Read CSV filenames from a directory or list
			string[] csvFilenames;
			if (args.Length == 1)
			{
				// Simple mode: just pass a csv name
				csvFilenames = new string[] { args[0] };
			}
			else
			{
				csvDir=GetArg("csvDir");
				int maxFileAgeDays = GetIntArg("maxFileAgeDays", -1);
				string summaryTableCacheInDir = GetArg("summaryTableCacheIn");
				if (csvDir.Length > 0)
				{
					bool recurse = GetBoolArg("recurse");
					string searchPattern = GetArg("searchPattern", false);
					if (searchPattern == "")
					{
						searchPattern = "*.csv;*.csv.bin";
					}
					else if (!searchPattern.Contains('.'))
					{
						searchPattern += ".csv;*.csv.bin";
					}

					System.IO.FileInfo[] files = GetFilesWithSearchPattern(csvDir, searchPattern, recurse, maxFileAgeDays);
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
				else if (summaryTableCacheInDir.Length>0)
				{
					bool recurse = GetBoolArg("recurse");
					System.IO.FileInfo[] files = GetFilesWithSearchPattern(summaryTableCacheInDir, "*.prc", recurse, maxFileAgeDays);
					csvFilenames = new string[files.Length];
					int i = 0;
					foreach (FileInfo csvFile in files)
					{
						csvFilenames[i] = csvFile.FullName;
						i++;
					}
					bBulkMode = true;
					bSummaryTableCacheOnlyMode = true;
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

			if (GetBoolArg("listSummaryTables"))
			{
				Console.WriteLine("Listing summary tables:");
				List<string> summaryTableNames=reportXML.GetSummaryTableNames();

				foreach (string name in summaryTableNames)
				{
					Console.WriteLine("  "+name);
				}
				return;
			}
			statDisplaynameMapping = reportXML.GetDisplayNameMapping();

			SummaryTableCacheStats summaryTableCacheStats = new SummaryTableCacheStats();

			perfLog.LogTiming("Initialization");

			string summaryTableCacheDir = null;
			if (bBulkMode)
			{
				summaryTableCacheDir = GetArg("summaryTableCache", null);
				if (summaryTableCacheDir != null)
				{
					// Check for incompatible options. Could just feed these into the metadata key eventually
					string incompatibleOptionsStr = "minx,maxx,beginevent,endevent,noStripEvents";
					string[] incompatibleOptionsList = incompatibleOptionsStr.Split(',');
					List<string> badOptions = new List<string>();
					foreach (string option in incompatibleOptionsList)
					{
						if ( GetArg(option, null) != null)
						{
							badOptions.Add(option);
						}
					}
					if (badOptions.Count>0)
					{
						Console.WriteLine("Warning: Summary Table cache disabled due to incompatible options ("+ string.Join(", ", badOptions) + "). See help for details.");
						summaryTableCacheDir = null;
					}
					else
					{
						Console.WriteLine("Using summary table cache: " + summaryTableCacheDir);
						Directory.CreateDirectory(summaryTableCacheDir);

						if ( GetBoolArg("summaryTableCachePurgeInvalid"))
						{
							Console.WriteLine("Purging invalid data from the summary table cache." );
							DirectoryInfo di = new DirectoryInfo(summaryTableCacheDir);
							FileInfo[] files = di.GetFiles("*.prc", SearchOption.TopDirectoryOnly);
							int numFilesDeleted=0;
							foreach (FileInfo file in files)
							{
								if ( SummaryTableRowData.TryReadFromCache(summaryTableCacheDir, file.Name.Substring(0,file.Name.Length-4))==null )
								{
									File.Delete(file.FullName);
									numFilesDeleted++;
								}
							}
							summaryTableCacheStats.PurgeCount = numFilesDeleted;
							Console.WriteLine(numFilesDeleted+" of "+files.Length+" cache entries deleted");
							perfLog.LogTiming("PurgeSummaryTableCache");
						}
					}
				}
			}

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

			// Read the metadata filter string
			string metadataFilterString = GetArg("metadataFilter", null);
			QueryExpression metadataQuery = null;
			if (metadataFilterString != null)
			{
				metadataQuery = MetadataQueryBuilder.BuildQueryExpressionTree(metadataFilterString);
			}

			bool writeDetailedReports = !GetBoolArg("noDetailedReports");

			// A csv hash for now can just be a filename/size. Replace with metadata later
			// Make PerfSummaryCache from: CSV hash + reporttype hash. If the cache is enabled then always -readAllStats

			bool bReadAllStats = GetBoolArg("readAllStats");

			bool bSummaryTableCacheReadonly = GetBoolArg("summaryTableCacheReadOnly");
			bool bSummaryTableCacheInvalidate = GetBoolArg("summaryTableCacheInvalidate");
			string cleanCsvOutputFilename = GetArg("cleanCsvOut", null);
			if (cleanCsvOutputFilename != null && bBulkMode)
			{
				throw new Exception("-cleanCsvOut is not compatible with bulk mode. Pass one csv with -csv <filename>");
			}

			bool bShowHiddenStats = GetBoolArg("showHiddenStats");
			string customSummaryTableFilter = GetArg("customTable");
			if (customSummaryTableFilter.Length > 0)
			{
				bShowHiddenStats = true;
			}

			string summaryTableCacheForRead = summaryTableCacheDir;
			if (bSummaryTableCacheInvalidate || writeDetailedReports )
			{
				// Don't read from the summary metadata cache if we're generating full reports
				summaryTableCacheForRead = null;
			}

			if (bSummaryTableCacheOnlyMode)
			{
				// Override these options in summaryTableCacheOnly mode
				bSummaryTableCacheReadonly = true;
				summaryTableCacheForRead = null;
				bSummaryTableCacheInvalidate = false;
			}

			ReportTypeParams reportTypeParams = new ReportTypeParams
			{
				reportTypeOverride = GetArg("reportType", false).ToLower(),
				forceReportType = !GetBoolArg("reportTypeCompatCheck")
			};

			bool bRemoveDuplicates = !GetBoolArg("allowDuplicateCSVs");
			bool bSummaryTableCacheUseOnlyCsvID = GetBoolArg("summaryTableCacheUseOnlyCsvID");
			bool bRequireMetadata = GetBoolArg("requireMetadata");
			bool bListFilesMode = GetBoolArg("listFiles");
			int frameCountThreshold = GetIntArg("minFrameCount", 0);
			if (bListFilesMode)
			{
				writeDetailedReports = false;
			}


			CsvFileCache csvFileCache = new CsvFileCache(
				csvFilenames, 
				precacheCount, 
				precacheThreads, 
				!GetBoolArg("noCsvCacheFiles"), 
				metadataQuery, 
				reportXML, 
				reportTypeParams, 
				bBulkMode, 
				bSummaryTableCacheOnlyMode, 
				bSummaryTableCacheUseOnlyCsvID, 
				bRemoveDuplicates,
				bRequireMetadata,
				summaryTableCacheForRead,
				bListFilesMode);

            SummaryTable summaryTable = new SummaryTable();
			bool bWriteToSummaryTableCache = summaryTableCacheDir != null && !bSummaryTableCacheReadonly;

			int csvCount = csvFilenames.Length;
			for ( int i=0; i<csvCount; i++)
			{
                try
                {
                    CachedCsvFile cachedCsvFile = csvFileCache.GetNextCachedCsvFile();
					if (cachedCsvFile == null)
					{
						continue;
					}
                    Console.WriteLine("-------------------------------------------------");
                    Console.WriteLine("CSV " + (i+1) + "/" + csvFilenames.Length ) ;
                    Console.WriteLine(cachedCsvFile.filename);

					perfLog.LogTiming("  CsvCacheRead");
					if (cachedCsvFile == null)
					{
						Console.WriteLine("Skipped!");
					}
					else
                    {
						SummaryTableRowData rowData = cachedCsvFile.cachedSummaryTableRowData;
						if (rowData == null)
						{
							if (summaryTableCacheForRead != null)
							{
								summaryTableCacheStats.MissCount++;
							}
							if (bBulkMode)
							{
								rowData = new SummaryTableRowData();
							}
							if (cleanCsvOutputFilename != null)
							{
								WriteCleanCsv(cachedCsvFile, cleanCsvOutputFilename, cachedCsvFile.reportTypeInfo);
								perfLog.LogTiming("  WriteCleanCsv");
							}
							else
							{
								GenerateReport(cachedCsvFile, outputDir, bBulkMode, rowData, bBatchedGraphs, writeDetailedReports, bReadAllStats || bWriteToSummaryTableCache, cachedCsvFile.reportTypeInfo, csvDir);
								perfLog.LogTiming("  GenerateReport");

								if (rowData != null && bWriteToSummaryTableCache)
								{
									if (rowData.WriteToCache(summaryTableCacheDir, cachedCsvFile.summaryTableCacheId))
									{
										Console.WriteLine("Cached summary rowData for CSV: " + cachedCsvFile.filename);
										summaryTableCacheStats.WriteCount++;
										perfLog.LogTiming("  WriteSummaryTableCache");
									}
								}
							}
						}
						else
						{
							summaryTableCacheStats.HitCount++;
						}

						if (rowData != null)
                        {
							// Filter row based on framecount if minFrameCount is specified
							bool bIncludeRowData = true;
							if (frameCountThreshold > 0 && rowData.GetFrameCount() < frameCountThreshold)
							{
								Console.WriteLine("CSV frame count below the threshold. Excluding from summary table:" + cachedCsvFile.filename);
								bIncludeRowData = false;
							}
							if (bIncludeRowData)
							{
								summaryTable.AddRowData(rowData, bReadAllStats, bShowHiddenStats);
							}
							perfLog.LogTiming("  AddRowData");
						}
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

            // Write out the summary table, if there is one
            if (summaryTable.Count > 0)
			{
				// Pre-sort the summary table to ensure determinism
				summaryTable = summaryTable.SortRows(new List<string>(new string[] { "csvfilename" }),true);
				perfLog.LogTiming("PreSort Summary table");

				string summaryTableFilename = GetArg("summaryTableFilename", "SummaryTable");
				if ( summaryTableFilename.ToLower().EndsWith(".html"))
				{
					summaryTableFilename = summaryTableFilename.Substring(0, summaryTableFilename.Length - 5);
				}
				bool bCsvTable = GetBoolArg("csvTable");
				bool bCollateTable = GetBoolArg("collateTable");
				bool bSpreadsheetFriendlyStrings = GetBoolArg("spreadsheetFriendly");
				string weightByColumnName = GetArg("weightByColumn", null);
				if (customSummaryTableFilter.Length > 0)
				{
					string customSummaryTableRowSort = GetArg("customTableSort");
					if (customSummaryTableRowSort.Length == 0)
					{
						customSummaryTableRowSort = "buildversion,deviceprofile";
					}
					WriteSummaryTableReport(outputDir, summaryTableFilename, summaryTable, customSummaryTableFilter.Split(',').ToList(), customSummaryTableRowSort.Split(',').ToList(), false, bCsvTable, bSpreadsheetFriendlyStrings, null, null);

					if (bCollateTable)
					{
						WriteSummaryTableReport(outputDir, summaryTableFilename+"_Collated", summaryTable, customSummaryTableFilter.Split(',').ToList(), customSummaryTableRowSort.Split(',').ToList(), true, bCsvTable, bSpreadsheetFriendlyStrings, null, weightByColumnName);
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
					WriteSummaryTableReport(outputDir, summaryTableFilename, summaryTable, tableInfo, false, bCsvTable, bSpreadsheetFriendlyStrings, null );
					if (bCollateTable)
					{
						WriteSummaryTableReport(outputDir, summaryTableFilename, summaryTable, tableInfo, true, bCsvTable, bSpreadsheetFriendlyStrings, weightByColumnName);
					}
				}

				// EmailTable is hardcoded to use the condensed type
				string condensedSummaryTable = GetArg("condensedSummaryTable",null);
				if (GetBoolArg("emailSummary") || GetBoolArg("emailTable") || condensedSummaryTable != null)
				{
					SummaryTableInfo tableInfo = reportXML.GetSummaryTable(condensedSummaryTable == null ? "condensed" : condensedSummaryTable);
					WriteSummaryTableReport(outputDir, summaryTableFilename+"_Email", summaryTable, tableInfo, true, false, bSpreadsheetFriendlyStrings, weightByColumnName);
				}
                perfLog.LogTiming("WriteSummaryTable");
            }

			if ( summaryTableCacheDir != null )
			{
				summaryTableCacheStats.LogStats();
			}
			Console.WriteLine("Duplicate CSVs skipped: " + csvFileCache.duplicateCount);
            perfLog.LogTotalTiming();
        }

		void WriteSummaryTableReport(string outputDir, string filenameWithoutExtension, SummaryTable table, List<string> columnFilterList, List<string> rowSortList, bool bCollated, bool bToCSV, bool bSpreadsheetFriendlyStrings, List<SummarySectionBoundaryInfo> sectionBoundaries, string weightByColumnName)
		{
			SummaryTableInfo tableInfo = new SummaryTableInfo();
			tableInfo.columnFilterList = columnFilterList;
			tableInfo.rowSortList = rowSortList;
			WriteSummaryTableReport(outputDir, filenameWithoutExtension, table, tableInfo, bCollated, bToCSV, bSpreadsheetFriendlyStrings, weightByColumnName);
		}

		void WriteSummaryTableReport(string outputDir, string filenameWithoutExtension, SummaryTable table, SummaryTableInfo tableInfo, bool bCollated, bool bToCSV, bool bSpreadsheetFriendlyStrings, string weightByColumnNameOverride)
		{
			string weightByColumnName = weightByColumnNameOverride != null ? weightByColumnNameOverride : tableInfo.weightByColumn;
			if (GetBoolArg("noWeightedAvg"))
			{
				weightByColumnName = null;
			}
			bool reverseSort = tableInfo.bReverseSortRows || GetBoolArg("reverseTable");
			bool bScrollableTable = tableInfo.bScrollableFormatting || GetBoolArg("scrollableTable");
			bool addMinMaxColumns = !GetBoolArg("noSummaryMinMax");
			if (!string.IsNullOrEmpty(outputDir))
            {
                filenameWithoutExtension = Path.Combine(outputDir, filenameWithoutExtension);
            }
            SummaryTable filteredTable = table.SortAndFilter(tableInfo.columnFilterList, tableInfo.rowSortList, reverseSort, weightByColumnName);
			if (bCollated)
			{
				filteredTable = filteredTable.CollateSortedTable(tableInfo.rowSortList, addMinMaxColumns);
			}
			if (bToCSV)
			{
				filteredTable.WriteToCSV(filenameWithoutExtension+".csv");
			}
			else
			{
				filteredTable.ApplyDisplayNameMapping(statDisplaynameMapping);
				string VersionString = GetBoolArg("noWatermarks") ? "" : Version.Get();
				filteredTable.WriteToHTML(filenameWithoutExtension+".html", VersionString, bSpreadsheetFriendlyStrings, tableInfo.sectionBoundaries, bScrollableTable, addMinMaxColumns, GetIntArg("maxSummaryTableStringLength", -1), reportXML.columnFormatInfoList, weightByColumnName);
			}
		}

		string ReplaceFileExtension( string path, string newExtension )
        {
			// Special case for .bin.csv
			if (path.ToLower().EndsWith(".csv.bin"))
			{
				return path.Substring(0, path.Length - 8)+ newExtension;
			}

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
                        String.Format("{0,-25} : {1,-10}", description , (elapsed*1000.0).ToString("0.0") + "ms"), 70);
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

		void WriteCleanCsv(CachedCsvFile csvFile, string outCsvFilename, ReportTypeInfo reportTypeInfo)
		{
			if ( File.Exists(outCsvFilename) )
			{
				throw new Exception("Clean csv file " + outCsvFilename + " already exists!");
			}
			Console.WriteLine("Writing clean (standard format, event stripped) csv file to " + outCsvFilename);
			int minX = GetIntArg("minx", 0);
			int maxX = GetIntArg("maxx", Int32.MaxValue);

			// Check if we're stripping stats
			bool bStripStatsByEvents = reportTypeInfo.bStripEvents;
			if (GetBoolArg("noStripEvents"))
			{
				bStripStatsByEvents = false;
			}

			int numFramesStripped;
			CsvStats unstrippedCsvStats;
			CsvStats csvStats = ProcessCsv(csvFile, out numFramesStripped, out unstrippedCsvStats, minX, maxX, null, bStripStatsByEvents);
			csvStats.WriteToCSV(outCsvFilename, false);
		}

		CsvStats ProcessCsv(CachedCsvFile csvFile, out int numFramesStripped, out CsvStats unstrippedCsvStats, int minX=0, int maxX=Int32.MaxValue, PerfLog perfLog=null, bool bStripStatsByEvents = true)
		{
			numFramesStripped = 0;
			CsvStats csvStats = ReadCsvStats(csvFile, minX, maxX);
			unstrippedCsvStats = csvStats;
			if (perfLog != null)
			{
				perfLog.LogTiming("    ReadCsvStats");
			}

			if (bStripStatsByEvents)
			{
				CsvStats strippedCsvStats = StripCsvStatsByEvents(unstrippedCsvStats, out numFramesStripped);
				csvStats = strippedCsvStats;
			}
			if (perfLog != null)
			{
				perfLog.LogTiming("    FilterStats");
			}
			return csvStats;
		}

		void GenerateReport(CachedCsvFile csvFile, string outputDir, bool bBulkMode, SummaryTableRowData rowData, bool bBatchedGraphs, bool writeDetailedReport, bool bReadAllStats, ReportTypeInfo reportTypeInfo, string csvDir)
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
					foreach (string statString in graph.settings.statString.value.Split(','))
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
					Process csvToSvgProcess = LaunchCsvToSvgAsync("-batchCommands \""+responseFilename +"\" -mt " + GetIntArg("graphThreads", Environment.ProcessorCount/2).ToString() );
					csvToSvgProcesses.Add(csvToSvgProcess);
				}
			}
            perfLog.LogTiming("    Initial Processing");

			// Check if we're stripping stats
			bool bStripStatsByEvents = reportTypeInfo.bStripEvents;
			if (GetBoolArg("noStripEvents"))
			{
				bStripStatsByEvents = false;
			}

			// Read the full csv while we wait for the graph processes to complete
			int numFramesStripped;
			CsvStats unstrippedCsvStats;
			CsvStats csvStats=ProcessCsv(csvFile, out numFramesStripped, out unstrippedCsvStats, minX, maxX, perfLog, bStripStatsByEvents);

            if ( writeDetailedReport )
            { 
                // wait on the graph processes to complete
                foreach (Process process in csvToSvgProcesses)
				{
					process.WaitForExit();
				}
                perfLog.LogTiming("    WaitForAsyncGraphs");
            }


			// Generate CSV metadata
			if (rowData != null)
			{
                Uri currentDirUri = new Uri(Directory.GetCurrentDirectory() + "/", UriKind.Absolute);
                if ( outputDir.Length > 0 && !outputDir.EndsWith("/"))
                {
                    outputDir += "/";
                }
                Uri optionalDirUri = new Uri(outputDir, UriKind.RelativeOrAbsolute);

				// Make a Csv URI that's relative to the report directory
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
				Uri relativeCsvUri = finalDirUri.MakeRelativeUri(csvFileUri);
				string csvPath = relativeCsvUri.ToString();

				// re-root the CSV path if requested
				string csvLinkRootPath = GetArg("csvLinkRootPath", null);
				if ( csvDir != null && csvLinkRootPath != null)
				{
					string csvDirFinal = csvDir.Replace("\\", "/");
					csvDirFinal += csvDirFinal.EndsWith("/") ? "" : "/";
					Uri csvDirUri = new Uri(csvDirFinal, UriKind.Absolute);
					Uri csvRelativeToCsvDirUri = csvDirUri.MakeRelativeUri(csvFileUri);
					csvPath = Path.Combine(csvLinkRootPath,csvRelativeToCsvDirUri.ToString());
					csvPath = new Uri(csvPath, UriKind.Absolute).ToString();
				}

				rowData.Add(SummaryTableElement.Type.ToolMetadata, "Csv File", "<a href='" + csvPath + "'>" + shortName + ".csv" + "</a>", null, csvPath);
				rowData.Add(SummaryTableElement.Type.ToolMetadata, "ReportType", reportTypeInfo.name);
				rowData.Add(SummaryTableElement.Type.ToolMetadata, "ReportTypeID", reportTypeInfo.summaryTableCacheID);
				if (htmlFilename != null)
				{
					string htmlUrl = htmlFilename;
					string reportLinkRootPath = GetArg("reportLinkRootPath", null);
					if (reportLinkRootPath != null)
					{
						htmlUrl = reportLinkRootPath + htmlFilename;
					}

					rowData.Add(SummaryTableElement.Type.ToolMetadata, "Report", "<a href='" + htmlUrl + "'>Link</a>");
				}
				// Pass through all the metadata from the CSV
				if (csvStats.metaData != null)
				{
					foreach (KeyValuePair<string, string> pair in csvStats.metaData.Values.ToList())
					{
						rowData.Add(SummaryTableElement.Type.CsvMetadata, pair.Key.ToLower(), pair.Value);
					}
				}

				if (bReadAllStats)
				{
					// Add every stat avg value to the metadata
					foreach ( StatSamples stat in csvStats.Stats.Values )
					{
						rowData.Add(SummaryTableElement.Type.CsvStatAverage, stat.Name, (double)stat.average);
					}
				}

			}

			if (htmlFilename != null && !string.IsNullOrEmpty(outputDir))
            {
                htmlFilename = Path.Combine(outputDir, htmlFilename);
            }

            // Write the report
            WriteReport(htmlFilename, title, svgFilenames, reportTypeInfo, csvStats, unstrippedCsvStats, numFramesStripped, minX, maxX, bBulkMode, rowData);
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
			CsvStats csvStats = csvFile.GetFinalCsv();
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
			// We want to run the mask apply in parallel if -nodetailedreports is specified. Otherwise leave cores free for graph generation
			bool doParallelMaskApply = GetBoolArg("noDetailedReports");
			CsvStats strippedStats = csvStats;

			if (eventsToStrip != null)
            {
				BitArray sampleMask = null;
				foreach (CsvEventStripInfo eventStripInfo in eventsToStrip)
				{
					csvStats.ComputeEventStripSampleMask(eventStripInfo.beginName, eventStripInfo.endName, ref sampleMask);
				}
				if (sampleMask != null)
				{
					numFramesStripped = sampleMask.Cast<bool>().Count(l => !l);
					strippedStats = csvStats.ApplySampleMask(sampleMask, doParallelMaskApply);
				}
			}

            if (numFramesStripped > 0 )
            {
                Console.WriteLine("CSV frames excluded : " + numFramesStripped);
            }
            return strippedStats;
        }

      


        void WriteReport(string htmlFilename, string title, List<string> svgFilenames, ReportTypeInfo reportTypeInfo, CsvStats csvStats, CsvStats unstrippedCsvStats, int numFramesStripped, int minX, int maxX, bool bBulkMode, SummaryTableRowData summaryRowData)
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
				if ( GetBoolArg("noWatermarks"))
				{
					htmlFile.WriteLine("    <![CDATA[ \nCreated with PerfReportTool" );
				}
				else
				{
					htmlFile.WriteLine("    <![CDATA[ \nCreated with PerfReportTool " + Version.Get() + " with commandline:");
					htmlFile.WriteLine(commandLine.GetCommandLine());
				}
				htmlFile.WriteLine("    ]]>");
				htmlFile.WriteLine("    <title>" + titleStr + "</title>");
				htmlFile.WriteLine("    <style type='text/css'>");
				htmlFile.WriteLine("      table, th, td { border: 2px solid black; border-collapse: collapse; padding: 3px; vertical-align: top; font-family: 'Verdana', Times, serif; font-size: 12px;}");
				htmlFile.WriteLine("      p {  font-family: 'Verdana', Times, serif; font-size: 12px }");
				htmlFile.WriteLine("      h1 {  font-family: 'Verdana', Times, serif; font-size: 20px; padding-top:10px }");
				htmlFile.WriteLine("      h2 {  font-family: 'Verdana', Times, serif; font-size: 18px; padding-top:20px }");
				htmlFile.WriteLine("      h3 {  font-family: 'Verdana', Times, serif; font-size: 16px; padding-top:20px }");
				htmlFile.WriteLine("    </style>");
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

				htmlFile.WriteLine("<table style='width:800'>");

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
						    htmlFile.WriteLine("<tr><td bgcolor='#F0F0F0'>" + friendlyName + "</td><td><b>" + value + "</b></td></tr>");
					    }
				    }
				}
				htmlFile.WriteLine("<tr><td bgcolor='#F0F0F0'>Frame count</td><td>" + csvStats.SampleCount + " (" + numFramesStripped + " excluded)</td></tr>");
				htmlFile.WriteLine("</table>");

			}

			if (summaryRowData != null)
            {
                summaryRowData.Add(SummaryTableElement.Type.ToolMetadata, "framecount", csvStats.SampleCount.ToString());
                if (numFramesStripped > 0)
                {
                    summaryRowData.Add(SummaryTableElement.Type.ToolMetadata, "framecountExcluded", numFramesStripped.ToString());
                }
			}

			bool bWriteSummaryCsv = GetBoolArg("writeSummaryCsv") && !bBulkMode;

			List<Summary> summaries = new List<Summary>(reportTypeInfo.summaries);
			bool bExtraLinksSummary = GetBoolArg("extraLinksSummary");
			if (bExtraLinksSummary)
			{
				summaries.Insert(0,new ExtraLinksSummary(null, null));
			}

			// If the reporttype has summary info, then write out the summary]
			PeakSummary peakSummary = null;
            foreach (Summary summary in summaries)
            {
                summary.WriteSummaryData(htmlFile, summary.useUnstrippedCsvStats ? unstrippedCsvStats : csvStats, bWriteSummaryCsv, summaryRowData, htmlFilename);
                if ( summary.GetType() == typeof(PeakSummary) )
                {
                    peakSummary = (PeakSummary)summary;
                }
            }

            if (htmlFile != null)
			{
				// Output the list of graphs
				htmlFile.WriteLine("<h2>Graphs</h2>");

				// TODO: support sections for graphs
				List<string> sections = new List<string>();

				//// We have to at least have the empty string in this array so that we can print the list of links.
				if (sections.Count() == 0) 
				{ 
					sections.Add(""); 
				}

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
					htmlFile.WriteLine("  <br><a name='" + StripSpaces(svgTitle) + "'></a> <h2>" + svgTitle + "</h2>");
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

				if (GetBoolArg("noWatermarks"))
				{
					htmlFile.WriteLine("<p style='font-size:8'>Created with PerfReportTool</p>");
				}
				else
				{
					htmlFile.WriteLine("<p style='font-size:8'>Created with PerfReportTool " + Version.Get() + "</p>");
				}
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
			if (GetBoolArg("noWatermarks"))
			{
				htmlFile.WriteLine("    <![CDATA[ \nCreated with PerfReportTool");
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


            htmlFile.WriteLine("<a href=\"[Report Link Here]\">Click here for Report w/ interactive SVGs.</a>");
            htmlFile.WriteLine("  <h2>Summary</h2>");

            htmlFile.WriteLine("Overall Runtime: [Replace Me With Runtime]");

			bool bWriteSummaryCsv = GetBoolArg("writeSummaryCsv") && !bBulkMode;

			// If the reporttype has summary info, then write out the summary]
			PeakSummary peakSummary = null;
            foreach (Summary summary in reportTypeInfo.summaries)
            {
                summary.WriteSummaryData(htmlFile, csvStats, bWriteSummaryCsv, null, htmlFilename);
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
			string[] statStringTokens = graphSettings.statString.value.Split(',');
			IEnumerable<string> quoteWrappedStatStrings = statStringTokens.Select(token => '"' + token + '"');
			string statString = String.Join(" ", quoteWrappedStatStrings);
			double thickness = graphSettings.thickness.value * thicknessMultiplier;
			float maxy = GetFloatArg("maxy", (float)graphSettings.maxy.value);
			bool smooth = graphSettings.smooth.value && !GetBoolArg("nosmooth");
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
				" -o \"" + svgFilename +"\"" +
				" -stats " + statString +
				" -width " + (width * scaleby).ToString() +
				" -height " + (height * scaleby).ToString() +
				OptionalHelper.GetDoubleSetting(graph.budget, " -budget ") + 
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

		bool matchesPattern(string str, string pattern)
		{
			string [] patternSections=pattern.ToLower().Split('*');
			// Check the substrings appear in order
			string remStr = str.ToLower();
			for (int i = 0;i<patternSections.Length; i++)
			{
				int idx = remStr.IndexOf(patternSections[i]);
				if (idx==-1)
				{
					return false;
				}
				remStr = remStr.Substring(idx+patternSections[i].Length);
			}
			return remStr.Length == 0;
		}

		System.IO.FileInfo[] GetFilesWithSearchPattern(string directory, string searchPatternStr, bool recurse, int maxFileAgeDays=-1)
		{
			List<System.IO.FileInfo> fileList = new List<FileInfo>();
			string[] searchPatterns = searchPatternStr.Split(';');
			DirectoryInfo di = new DirectoryInfo(directory);
			foreach (string searchPattern in searchPatterns)
			{
				System.IO.FileInfo[] files = di.GetFiles("*.*", recurse ? SearchOption.AllDirectories : SearchOption.TopDirectoryOnly);
				foreach (FileInfo file in files)
				{
					if (maxFileAgeDays >=0)
					{
						DateTime fileModifiedTime = file.LastWriteTimeUtc;
						DateTime currentTime = DateTime.UtcNow;
						TimeSpan elapsed = currentTime.Subtract(fileModifiedTime);
						if ( elapsed.TotalHours > (double)maxFileAgeDays*24.0 )
						{
							continue;
						}
					}

					if (matchesPattern(file.FullName, searchPattern))
					{
						fileList.Add(file);
					}
				}
			}
			return fileList.Distinct().ToArray();
		}

    }

	static class Extensions
	{
		public static T GetSafeAttibute<T>(this XElement element, string attributeName, T defaultValue = default(T))
		{
			XAttribute attribute = element.Attribute(attributeName);
			if (attribute == null)
			{
				return defaultValue;
			}

			try
			{
				switch (Type.GetTypeCode(typeof(T)))
				{
					case TypeCode.Boolean:
						return (T)Convert.ChangeType(Convert.ChangeType(attribute.Value, typeof(int)), typeof(bool));
					case TypeCode.Single:
					case TypeCode.Double:
					case TypeCode.Decimal:
						return (T)Convert.ChangeType(attribute.Value, typeof(T), CultureInfo.InvariantCulture.NumberFormat);
					default:
						return (T)Convert.ChangeType(attribute.Value, typeof(T));
				}
			}
			catch (FormatException e)
			{
				Console.WriteLine(string.Format("[Warning] Failed to convert XML attribute '{0}' ({1})", attributeName, e.Message));
				return defaultValue;
			}
		}
    };
}

