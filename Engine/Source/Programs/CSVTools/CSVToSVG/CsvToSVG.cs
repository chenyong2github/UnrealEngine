// Copyright (C) Microsoft. All rights reserved.
// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Drawing;
using System.IO;
using System.Globalization;
using System.Diagnostics;
using System.Security.Cryptography;
using CSVStats;

// 2.1 TODO
// toggle stats by clicking on legend
// zoom X/Y via transform
// would need to implement:
//  fully dynamic events
//  fully dynamic axes
//  fully dynamic graphs? Might be able to use viewBox or something to redefine the coordinate space

namespace CSVTools
{
    class Version
    {
        private static string VersionString = "2.42";
        
        public static string Get() { return VersionString; }
    };

    class Rect
    {
        public Rect(float xIn, float yIn, float widthIn, float heightIn)
        {
            x = xIn; y = yIn; width = widthIn; height = heightIn;
        }
        public float x, y;
        public float width, height;
    };

    class Range
    {
        public const float Auto = -100000.0f;

        public Range() { }
        public Range(float minx, float maxx, float miny, float maxy) { MinX = minx; MaxX = maxx; MinY = miny; MaxY = maxy; }
        public float MinX, MaxX;
        public float MinY, MaxY;
    };

    class Theme
    {
        // TODO: make this data driven
        public Theme( string Name )
        {
            GraphColours = new Colour[32];
            uint[] GraphColoursInt = null;
            if (Name == "light")
            {
                BackgroundColour = new Colour(255,255,255);
                BackgroundColourCentre = new Colour(255,255,255);
                LineColour = new Colour(0, 0, 0);
              
                GraphColoursInt = new uint[16]
                {
                    0x0000C0, 0x8000C0, 0xFF4000, 0xC00000,
                    0x4040A0, 0x008080, 0x200080, 0x408060,
                    0x008040, 0x00008C, 0x60A880, 0x325000,
                    0xA040A0, 0x808000, 0x005050, 0x606060,
                };

                TextColour = new Colour(0, 0, 0);
                MediumTextColour = new Colour(64, 64, 64);
                MinorTextColour = new Colour(128, 128, 128);
                AxisLineColour = new Colour(128, 128, 128);
                MajorGridlineColour = new Colour(128, 128, 128);
                MinorGridlineColour = new Colour(160, 160, 160);
                BudgetLineColour = new Colour(0, 196, 0);
                EventTextColour = new Colour(0);
                BudgetLineThickness = 1.0f;
            }
            else if (Name == "pink")
            {
                BackgroundColour = new Colour(255, 128, 128);
                BackgroundColourCentre = new Colour(255, 255, 255);
                LineColour = new Colour(0, 0, 0);
                GraphColoursInt = new uint[16]
                {
                    0x8080FF, 0xFF8C8C, 0xFFFF8C, 0x20C0C0,
                    0x808000, 0xFF8C8C, 0x20FF8C, 0x408060,
                    0xFF8040, 0xFFFF8C, 0x60008C, 0x3250FF,
                    0x008000, 0x8C8CFF, 0xFF5050, 0x606060,
                };

                TextColour = new Colour(0, 0, 0);
                MediumTextColour = new Colour(192, 192, 192);
                MinorTextColour = new Colour(128, 128, 128);
                AxisLineColour = new Colour(128, 128, 128);
                MajorGridlineColour = new Colour(128, 128, 128);
                MinorGridlineColour = new Colour(160, 160, 160);
                BudgetLineColour = new Colour(0, 196, 0);
                EventTextColour = new Colour(0);
                BudgetLineThickness = 1.0f;
            }
            else // "dark"
            {
                BackgroundColour = new Colour(16,16,16);
                BackgroundColourCentre = new Colour(80, 80, 80);
                LineColour = new Colour(255, 255, 255);

                GraphColoursInt = new uint[16]
                {//0x11bbbb
                    0x0080FF, 0x66cdFF, 0xFF6600, 0xFFFF8C,
                    0x60f060, 0xFFFF00, 0x99CC00, 0xCC6600, 
                    0xCC3300, 0xCCFF66, 0x60008C, 0x3250FF,
                    0x008000, 0x11bbbb, 0xFF5050, 0x606060,
                };
                for ( int i=0; i<16; i++)
                    GraphColours[i]=new Colour(GraphColoursInt[i]);

                TextColour = new Colour(255, 255, 255);
                MediumTextColour = new Colour(192, 192, 192);
                MinorTextColour = new Colour(128, 128, 128);
                AxisLineColour = new Colour(128, 128, 128);
                MajorGridlineColour = new Colour(128, 128, 128);
                MinorGridlineColour = new Colour(96, 96, 96);
                BudgetLineColour = new Colour(128, 255, 128);
                EventTextColour = new Colour(255, 255, 255, 0.75f);
                BudgetLineThickness = 0.5f;
            }

            for (int i = 0; i < GraphColours.Length; i++)
            {
                int repeat = i / GraphColoursInt.Length;
                float alpha = 1.0f - (float)repeat*0.25f;
                GraphColours[i] = new Colour(GraphColoursInt[i % GraphColoursInt.Length], alpha);

            }

        }
        public Colour BackgroundColour;
        public Colour BackgroundColourCentre;
        public Colour LineColour;
        public Colour TextColour;
        public Colour MinorTextColour;
        public Colour MediumTextColour;
        public Colour AxisLineColour;
        public Colour MinorGridlineColour;
        public Colour MajorGridlineColour;
        public Colour BudgetLineColour;
        public Colour EventTextColour;
        public float BudgetLineThickness;
        public Colour[] GraphColours;
    };

	class CsvCache
	{
		public CsvStats ReadCSVFile(string csvFilename, string[] statNames, int numRowsToSkip)
		{
			lock (readLock)
			{
				if (csvFilename != filename || skipRows != numRowsToSkip)
				{
					csvStats = CsvStats.ReadCSVFile(csvFilename, null, numRowsToSkip);
					filename = csvFilename;
					skipRows = numRowsToSkip;
				}
				// Copy just the stats we're interested in from the cached file
				CsvStats outStats = new CsvStats(csvStats, statNames);
				return outStats;
			}
		}
		public CsvStats csvStats;
		public string filename;
		public int skipRows;
		private readonly object readLock = new object();
	};


	class Program : CSVStats.CommandLineTool
    {
        System.IO.StreamWriter svgFile;
        Rect dimensions = new Rect(0, 0, 1000, 500);
        List<CsvStats> csvStats;
        float budget = 33.33f;
        Theme theme;
        float threshold = -float.MaxValue;
        float averageThreshold= -float.MaxValue;
        string[] ignoreStats;
        string[] showEventNames;
        string stackTotalStat;
		bool stackTotalStatIsAutomatic =false;
        int maxHierarchyDepth = -1;
        char hierarchySeparator = '/';
        int colourOffset = 0;

		static string formatString =
            "Format: \n" +
            "       -csvs <list> OR -csv <list> OR -csvDir <path>\n" +
            "       [ -o <svgFilename> ]\n" +
            "       -stats <stat names> (can include wildcards)\n" +
			"     OR \n" +
			"       -batchCommands <response file with commandlines>\n" +
			"       -mt <number of threads> \n" +
			"     OR \n" +
			"       -updatesvg <svgFilename>\n" +
			"       NOTE: this updates an svg by regenerating it with the original commandline parameters - requires original data\n\n" +
			"\nOptional Args: \n" +
			"       -averageThreshold <value>\n" +
			"       -budget <ms>\n" +
			"       -colourOffset <value>\n" +
			"       -compression <pixel error value>\n" +
			"       -discardLastFrame <1|0>\n" +
			"       -filterOutZeros\n" +
			"       -graphOnly\n" +
			"       -hideEventNames <1|0>\n" +
			"       -hideStatPrefix <list>\n" +
			"       -hierarchySeparator <character>\n" +
			"       -highlightEventRegions <startEventName,endEventName>\n" +
			"       -ignoreStats <list> (can include wildcards. Separate states with stat1;stat2;etc)\n" +
			"       -interactive\n" +
			"       -legend <list> \n" +
			"       -maxHierarchyDepth <depth>\n" +
			"       -minX <value> -maxX <value> -minY <value> -maxY <value>\n" +
			"       -noMetadata\n" +
			"       -noSnap\n" +
			"       -recurse\n" +
			"       -showAverages \n" +
			"       -showEvents <names> (can include wildcards)\n" +
			"       -showTotals \n" +
			"       -skipRows <n>\n" +
			"       -smooth\n" +
			"       -smoothKernelPercent <percentage>\n" +
			"       -smoothKernelSize <numFrames>\n" +
			"       -stacked\n" +
			"       -stackTotalStat <stat name>\n" +
			"		-minFilterStatValue <value>\n" +
			"		-minFilterStatName <stat name>\n" +
			"       -stackedUnsorted\n" +
			"       -statMultiplier <multiplier>\n" +
			"       -theme <dark|light>\n" +
			"       -thickness <multipler>\n" +
			"       -threshold <value>\n" +
			"       -title <name>\n" +
			"       -forceLegendSort\n" +
			"       -width <value> -height <value>\n" +
            "       -writeErrorsToSVG\n" +
            "       -percentile \n" +
            "       -percentile90 \n" +
            "       -percentile99 \n" +
			"       -uniqueId <string> : unique ID for JS (needed if this is getting embedded in HTML alongside other graphs)\n" +
			"       -nocommandlineEmbed : don't embed the commandline in the SVG" +
			"       -lineDecimalPlaces <N> (default 3)" +
			"       -frameOffset <N> : offset used for frame display name (default 0)" +
            "";

		void Run(string[] args)
		{
			System.Globalization.CultureInfo.DefaultThreadCurrentCulture = System.Globalization.CultureInfo.InvariantCulture;

			if (args.Length < 2)
			{
				WriteLine("CsvToSVG " + Version.Get());
				WriteLine(formatString);
				return;
			}

			// Read the command line
			ReadCommandLine(args);

			string svgToUpdate = GetArg("updatesvg");
			if (svgToUpdate.Length > 0)
			{
				if (args.Length > 2)
				{
					WriteLine("-UpdateSVG <svgFilename> must be the only argument!");
					return;
				}
				string newCommandLine = "";
				string[] svgLines = ReadLinesFromFile(svgToUpdate);
				for (int i = 0; i < svgLines.Length - 2; i++)
				{
					if (svgLines[i].StartsWith("<![CDATA[") && svgLines[i + 1].StartsWith("Created with CSVtoSVG ") && svgLines[i + 1].EndsWith(" with commandline:"))
					{
						newCommandLine = svgLines[i + 2];
						break;
					}
				}

				ReadCommandLine(MakeArgsArray(newCommandLine));
			}

			MakeGraph();
		}

		string GenerateIDFromString(string str)
		{
			string id = "";
			HashAlgorithm algorithm = SHA256.Create();
			byte[] hash = algorithm.ComputeHash(Encoding.UTF8.GetBytes(commandLine.GetCommandLine()));
			for ( int i=24; i<32; i++ )
			{
				id += hash[i].ToString("x2");
			}
			return id;
		}


		void MakeGraph()
		{
			// Read CSV filenames from a directory or list
			string[] csvFilenames;
            string csvDir = GetArg("csvDir"); 
            if (csvDir.Length > 0)
            {
                DirectoryInfo di = new DirectoryInfo(csvDir);
                bool recurse = GetBoolArg("recurse");
                FileInfo[] csvFiles = di.GetFiles("*.csv", recurse ? SearchOption.AllDirectories : SearchOption.TopDirectoryOnly);
				FileInfo[] binFiles = di.GetFiles("*.csv.bin", recurse ? SearchOption.AllDirectories : SearchOption.TopDirectoryOnly);
				List<FileInfo> allFiles = new List<FileInfo>(csvFiles);
				allFiles.AddRange(binFiles);
				csvFilenames = new string[allFiles.Count];
                int i = 0;
                foreach (FileInfo csvFile in allFiles)
                {
                    csvFilenames[i] = csvFile.FullName;
                    i++;
                }
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

            string statNamesStr = GetArg("stats", true);
            if (statNamesStr.Length == 0)
            {
                System.Console.Write(formatString);
                return;
            }
            string[] statNames = statNamesStr.Split(';');

            if (statNames.Length > 1 && csvFilenames.Length > 1)
            {
                WriteLine("Can't display multiple stats and multiple CSVs");
                return;
            }

			// Figure out the filename, if it wasn't provided
			string svgFilename = GetArg("o", false);
            if (svgFilename.Length == 0)
            {
                if (csvFilenames.Length == 1)
                {
                    int index = csvFilenames[0].LastIndexOf('.');
                    if (index >= 0)
                    {
                        svgFilename = csvFilenames[0].Substring(0, index);
                    }
                    else
                    {
                        svgFilename = csvFilenames[0];
                    }
                }
                if (statNames.Length == 1)
                {
                    if ( svgFilename.Length > 0 )
                    {
                        svgFilename += "_";
                    }
                    svgFilename += statNames[0].Replace('*','X').Replace('/','_');
                }
                svgFilename += ".svg";
            }
            if (svgFilename.Length == 0)
            {
                WriteLine("Couldn't figure out an appropriate filename, and no filename was provided with -o");
            }

			// Generate a unique ID based on the commandline. This is needed in order to embed in HTML alongside other graphs
			dimensions.width = GetIntArg("width", 1800);
            dimensions.height = GetIntArg("height", 550);

            svgFile = new System.IO.StreamWriter(svgFilename);
            SvgWrite("<svg width='" + dimensions.width + "' height='" + dimensions.height + "' viewPort='0 0 " + dimensions.height + " " + dimensions.width + "' version='1.1' xmlns='http://www.w3.org/2000/svg'");
            if ( GetBoolArg("interactive") )
            {
                SvgWrite(" onLoad='OnLoaded<UNIQUE>(evt)'");
            }
            SvgWriteLine(">");

			if (GetBoolArg("nocommandlineEmbed"))
			{
				SvgWriteLine("<![CDATA[ \nCreated with CSVtoSVG " + Version.Get() );
			}
			else
			{
				SvgWriteLine("<![CDATA[ \nCreated with CSVtoSVG " + Version.Get() + " with commandline:");
				SvgWriteLine(commandLine.GetCommandLine());
			}
			SvgWriteLine("]]>");

            bool writeErrorsToSVG = GetIntArg("writeErrorsToSVG",1) == 1;
            try
            {
                GenerateSVG(csvFilenames, statNames);
            }
            catch (System.Exception e)
            {
                // Write the error to the SVG
                string errorString = e.ToString();
                if (writeErrorsToSVG)
                {
                    errorString = errorString.Replace(" at", " at<br/>\n");
                    //errorString = errorString.Replace(" in", " in<br/>\n  ");
                    errorString += "<br/><br/>CSVtoSVG " + Version.Get() + "<br/>Commandline:<br/>" + commandLine;
                    float MessageWidth = dimensions.width - 20;
                    float MessageHeight = dimensions.height - 20;

                    SvgWriteLine("<switch>");
                    // Text wrapping (if supported)
                    //SvgWriteLine("<g requiredFeatures='http://www.w3.org/Graphics/SVG/feature/1.2/#TextFlow'><textArea width='" + MessageWidth + "' height='" + MessageHeight + "'>" +
                    //     errorString + "</textArea></g>");
                    SvgWriteLine("<foreignObject x='10' y='10' color='#ffffff' font-size='12' width='" + MessageWidth + "' height='" + MessageHeight + "'><p xmlns='http://www.w3.org/1999/xhtml'>" + errorString + "</p></foreignObject>'");
                    SvgWriteLine("<text x='10' y='10' fill='rgb(255, 255, 255)' font-size='10' font-family='Helvetica' > ERROR: " + errorString + "</text>");
                    SvgWriteLine("</switch>");


                    SvgWriteLine("</svg>");
                }
                svgFile.Close();
                Console.Out.WriteLine("Error: " + e.ToString());
            }
        }

		string GetGraphUniqueId()
		{
			return GetArg("uniqueID", "ID");
		}

        void SvgWrite(string str, bool replaceUniqueNames=true)
        {
            if (replaceUniqueNames)
            {
                svgFile.Write(str.Replace("<UNIQUE>", "_" + GetGraphUniqueId()));
            }
            else
            {
                svgFile.Write(str);
            }
        }
        void SvgWriteLine(string str,bool replaceUniqueNames=true)
        {
            if (replaceUniqueNames)
            {
                svgFile.WriteLine(str.Replace("<UNIQUE>", "_" + GetGraphUniqueId()));
            }
            else
            {
                svgFile.WriteLine(str);
            }
        }

        static string[] MakeArgsArray(string argsStr)
        {
            List<string> argsOut = new List<string>();
            string currentArg = "";
            bool bInQuotes = false;
            for ( int i=0; i< argsStr.Length;i++ )
            {
                bool flush = false;
                char c = argsStr[i];
                if ( c == '"')
                {
                    if ( bInQuotes )
                    {
                        flush = true;
                    }
                    bInQuotes = !bInQuotes;
                }
                else if ( c==' ' )
                {
                    if ( bInQuotes )
                    {
                        currentArg += c;
                    }
                    else
                    {
                        flush = true;
                    }
                }
                else
                {
                    currentArg += c;
                }

                if (flush && currentArg.Length > 0)
                {
                    argsOut.Add(currentArg);
                    currentArg = "";
                }
            }
            return argsOut.ToArray();
        }
        void GenerateSVG(string [] csvFilenames, string[] statNames)
        {
            // Read graph params from the commandline
            string ignoreStatsStr = GetArg("ignoreStats", false);
            ignoreStats = ignoreStatsStr.Split(';');

            string graphTitle = GetArg("title");

            string themeName = GetArg("theme").ToLower();
            theme = new Theme(themeName);

            string eventNamesStr = GetArg("showEvents", false).ToLower();
            if (eventNamesStr.Length > 0)
            {
                showEventNames = eventNamesStr.Split(';');
            }
            int hideEventNames = GetIntArg("hideEventNames", 0);

            colourOffset = GetIntArg("colourOffset", 0);

            bool smooth = GetArg("smooth") == "1";
            bool noMetadata = GetArg("nometadata") == "1";
            bool graphOnly = GetArg("graphOnly") == "1";
            float compression = GetFloatArg("compression", 0.0f);
            bool interactive = GetArg("interactive") == "1";
            bool stackedUnsorted = GetArg("stackedUnsorted") == "1";
            bool percentileTop90 = GetArg("percentile90") == "1";
            bool percentileTop99 = GetArg("percentile99") == "1";
            bool percentile = GetArg("percentile") == "1" || percentileTop90 || percentileTop99;

            maxHierarchyDepth = GetIntArg("maxHierarchyDepth", -1);
            string hierarchySeparatorStr = GetArg("hierarchySeparator");
            if (hierarchySeparatorStr.Length > 0) hierarchySeparator = hierarchySeparatorStr[0];

            string[] hideStatPrefixes = GetArg("hideStatPrefix").ToLower().Split(';');

            bool stacked = GetArg("stacked") == "1";

            if(percentile && (stacked || interactive))
            {
                Console.Out.WriteLine("Warning: percentile graph not compatible with stacked & interactive");
                stacked = false;
                interactive = false;
            }

			if (stacked)
            {
				stackTotalStat = GetArg("stackTotalStat").ToLower();
				if (stackTotalStat != "")
				{
					List<string> newStatNames = statNames.ToList();
					newStatNames.Add(stackTotalStat);
					statNames = newStatNames.ToArray();
				}
            }

            Range range = new Range();
            range.MinX = GetFloatArg("minx", Range.Auto);
            range.MaxX = GetFloatArg("maxx", Range.Auto);
            range.MinY = GetFloatArg("miny", Range.Auto);
            range.MaxY = GetFloatArg("maxy", Range.Auto);
            if (range.MaxX <= 0.0f) range.MaxX = Range.Auto;
            if (range.MaxY <= 0.0f) range.MaxY = Range.Auto;
            budget = GetFloatArg("budget", 33.333f);
            float thicknessFactor = GetFloatArg("thickness", 1.0f);

            threshold = GetFloatArg("threshold", -float.MaxValue);
            averageThreshold = GetFloatArg("averageThreshold", -float.MaxValue);

            string[] customLegendNames = null;
            string legendStr = GetArg("legend");
            if (legendStr.Length > 0) customLegendNames = legendStr.Split(';');

            Rect graphRect = new Rect(50, 42, dimensions.width - 100, dimensions.height - 115);

            // Write defs
            SvgWriteLine("<defs>");
            SvgWriteLine("<radialGradient id='radialGradient1'");
            SvgWriteLine("fx='50%' fy='50%' r='65%'");
            SvgWriteLine("spreadMethod='pad'>");
            SvgWriteLine("<stop offset='0%'   stop-color=" + theme.BackgroundColourCentre.SVGString() + " stop-opacity='1'/>");
            SvgWriteLine("<stop offset='100%' stop-color=" + theme.BackgroundColour.SVGString() + " stop-opacity='1' />");
            SvgWriteLine("</radialGradient>");

            SvgWriteLine("<linearGradient id = 'linearGradient1' x1 = '0%' y1 = '0%' x2 = '100%' y2 = '100%'>");
            SvgWriteLine("<stop stop-color = 'black' offset = '0%'/>");
            SvgWriteLine("<stop stop-color = 'white' offset = '100%'/>");
            SvgWriteLine("</linearGradient>");

            SvgWriteLine("<clipPath id='graphArea'>");
            SvgWriteLine("<rect  x='" + graphRect.x + "' y='" + graphRect.y + "' width='" + graphRect.width + "' height='" + graphRect.height + "'/>");
            SvgWriteLine("</clipPath>");

            SvgWriteLine("<filter id='dropShadowFilter' x='-20%' width='130%' height='130%'>");
            SvgWriteLine("<feOffset result='offOut' in='SourceAlpha' dx='-2' dy='2' />");
            SvgWriteLine("<feGaussianBlur result='blurOut' in='offOut' stdDeviation='1.1' />");
            SvgWriteLine("<feBlend in='SourceGraphic' in2='blurOut' mode='normal' />");
            SvgWriteLine("</filter>");

            SvgWriteLine("</defs>");

            //Colour[] graphColours = theme.GraphColours;

            DrawGraphArea(theme.BackgroundColour, dimensions, true);

            int csvIndex = 0;
            csvStats = new List<CsvStats>();

            bool bDiscardLastFrame = ( GetIntArg("discardLastFrame", 1) == 1 );
			int frameOffset = GetIntArg("frameOffset", 0);

			int firstFileNumSamples = -1;
            foreach (string csvFilename in csvFilenames)
            {
                CsvStats csv = ProcessCSV(csvFilename, statNames, bDiscardLastFrame);

				if (stacked && stackTotalStat == "")
				{
					// Make a total stat by summing each frame
					StatSamples totalStat = new StatSamples("Total");
					totalStat.samples.Capacity = csv.SampleCount;
					for (int i=0;i<csv.SampleCount;i++)
					{
						float totalValue = 0.0f;
						foreach( StatSamples stat in csv.Stats.Values)
						{
							totalValue += stat.samples[i];
						}
						totalStat.samples.Add(totalValue);
					}
					totalStat.ComputeAverageAndTotal();
					totalStat.colour = new Colour(0x6E6E6E);
					csv.AddStat(totalStat);
					stackTotalStat = "total";
					stackTotalStatIsAutomatic = true;
				}


				if (firstFileNumSamples == -1)
                {
                    foreach (StatSamples stat in csv.Stats.Values)
                    {
                        firstFileNumSamples = stat.samples.Count;
                        break;
                    }

                }

                SetLegend(csv, customLegendNames, csvFilename, hideStatPrefixes, csvFilenames.Length > 1);

                if (smooth)
                {
                    int kernelSize = GetIntArg("smoothKernelSize", -1);
                    if (kernelSize == -1)
                    {
                        float percent = (float)GetFloatArg("smoothKernelPercent", -1.0f);
                        if (percent == -1.0f)
                        {
                            percent = 2.0f;
                        }
                        percent = Math.Min(percent, 100.0f);
                        percent = Math.Max(percent, 0.0f);
                        float kernelSizeF = percent * 0.01f * (float)firstFileNumSamples + 0.5f;
                        kernelSize = (int)kernelSizeF;
                    }

                    csv = SmoothStats(csv, kernelSize);
                }
                if (!stacked)
                {
                    AssignColours(csv, false);
                }
                csvStats.Add(csv);
            }

            // Compute the X range 
            range = ComputeAdjustedXRange(range, graphRect);

            // Recompute the averages based on the X range. This is necessary for the legend and accurate sorting
            for (int i = 0; i < csvStats.Count; i++)
            {
                RecomputeStatAveragesForRange(csvStats[i], range);
            }

            // Handle stacking. Note that if we're not stacking, unstackedCsvStats will be a copy of csvStats
            List<CsvStats> unstackedCsvStats = new List<CsvStats>();
            for (int i=0; i<csvStats.Count; i++ )
            {
                unstackedCsvStats.Add(csvStats[i]);

                if (stacked)
                {
                    csvStats[i] = StackStats(csvStats[i], range, stackedUnsorted);
                    AssignColours(csvStats[i], true);

                    // Copy the colours to the unstacked stats
                    foreach (StatSamples samples in unstackedCsvStats[i].Stats.Values)
                    {
                        string statName = samples.Name;
                        if (stackTotalStatIsAutomatic == false && samples.Name.ToLower() == stackTotalStat.ToLower())
                        {
                            statName = "other";
                        }
                        samples.colour = csvStats[i].GetStat(statName).colour;
                    }
                }
            }


            // Adjust range
            range = ComputeAdjustedYRange(range, graphRect);

            if (graphOnly)
            {
                graphRect = dimensions;
            }

            // Adjust thickness depending on sample density, smoothness etc
            float thickness = smooth ? 0.33f : 0.1f;
            float thicknessMultiplier = (12000.0f / (range.MaxX - range.MinX)) * thicknessFactor;
            thicknessMultiplier *= (graphRect.width / 400.0f);
            thickness *= thicknessMultiplier;
            thickness = Math.Max(Math.Min(thickness, 1.5f), 0.11f);

            // Get the title
            if (graphTitle.Length == 0 && statNames.Length == 1 && csvStats.Count > 0 && !statNames[0].Contains("*"))
            {
                StatSamples stat = csvStats[0].GetStat(statNames[0]);
                if (stat == null)
				{
                    Console.Out.WriteLine("Warning: Could not find stat {0}", statNames[0]);
                    graphTitle = string.Format("UnknownStat {0}", statNames[0]);
                }
                else
                { 
                    graphTitle = stat.Name;
                }
            }

            // Combine and validate metadata
            // Assign metadata based on the first CSV with metadata
            CsvMetadata metaData = null;
            if (!noMetadata && !graphOnly)
            {
                foreach (CsvStats stats in csvStats)
                {
                    if (stats.metaData != null)
                    {
                        metaData = stats.metaData.Clone();
                        break;
                    }
                }
                if (metaData != null)
                {
                    // Combine all metadata
                    foreach (CsvStats stats in csvStats)
                    {
                        metaData.CombineAndValidate(stats.metaData);
                    }
                }
            }

            // Draw the event colouration

            // Draw the graphs
            uint colourIndex = 0;
            SvgWriteLine("<g id='graphArea'>");
            if(percentile)
            {
                range.MinX = percentileTop99 ? 99 : (percentileTop90 ? 90 : 0);
                range.MaxX = 100;
                DrawGridLines(graphRect, range, graphOnly, 1.0f, true, frameOffset);
                csvIndex = 0;
                foreach (CsvStats csvStat in csvStats)
                {
                    foreach (StatSamples stat in csvStat.Stats.Values)
                    {
                        string statID = "Stat_" + csvIndex + "_" + stat.Name + "<UNIQUE>";
                        DrawPercentileGraph(stat.samples, stat.colour, graphRect, range, statID);
                        colourIndex++;
                    }
                    csvIndex++;
                }
            }
            else
            {
                DrawGridLines(graphRect, range, graphOnly, 1.0f, stacked, frameOffset);
                csvIndex = 0;
                foreach (CsvStats csvStat in csvStats)
                {
                    DrawEventLines(csvStat.Events, graphRect, range);
                    DrawEventHighlightRegions(csvStat, graphRect, range);

                    foreach (StatSamples stat in csvStat.Stats.Values)
                    {
                        string statID = "Stat_" + csvIndex + "_" + stat.Name + "<UNIQUE>";
                        DrawGraph(stat.samples, stat.colour, graphRect, range, thickness, stacked, compression, statID);
                        colourIndex++;
                    }

                    if (hideEventNames == 0)
                    {
                        Colour eventColour = theme.EventTextColour;
                        DrawEventText(csvStat.Events, eventColour, graphRect, range );
                    }

                    csvIndex++;
                }
            }

            SvgWriteLine("</g>");

            // If we're stacked, we need to redraw the grid lines
            if (stacked)
            {
                DrawGridLines(graphRect, range, true, 0.75f, false, frameOffset);
            }

            // Draw legend, metadata and title
            if (!graphOnly)
            {
                DrawLegend(csvStats, dimensions);
                if (smooth)
                {
                    DrawText("(smoothed)", 50.0f, 33, 10.0f, dimensions, theme.MinorTextColour, "start");
                }
                DrawTitle(graphTitle, dimensions);
            }


            if (metaData != null)
            {
                DrawMetadata(svgFile, metaData, dimensions, theme.TextColour);
            }

            DrawText("CSVToSVG " + Version.Get(), dimensions.width - 102, 15, 7.0f, dimensions, theme.MinorTextColour);

            // Draw the interactive elements
            if (interactive)
            {
                AddInteractiveScripting(svgFile, graphRect, range, unstackedCsvStats);
            }

            SvgWriteLine("</svg>");
            svgFile.Close();
        }


        bool IsEventShown(string eventString)
        {
            if (showEventNames == null)
            {
                return false;
            }
            if (showEventNames.Length == 0)
            {
                return false;
            }
            if (eventString.Length == 0)
            {
                return false;
            }

            eventString = eventString.ToLower();
            foreach (string showEventName in showEventNames)
            {
                string showEventNameLower = showEventName.ToLower();
                if (showEventNameLower.EndsWith("*"))
                {
                    int index = showEventNameLower.LastIndexOf('*');
                    string prefix = showEventNameLower.Substring(0, index);
                    if (eventString.StartsWith(prefix))
                    {
                        return true;
                    }
                }
                else if (eventString == showEventNameLower)
                {
                    return true;
                }
            }
            return false;
        }

        bool IsStatIgnored( string statName )
        {
            statName = statName.ToLower();

            if (maxHierarchyDepth != -1)
            {
                int Depth = 0;
                foreach (char c in statName)
                {
                    if (c == hierarchySeparator)
                    {
                        Depth++;
                        if (Depth > maxHierarchyDepth)
                        {
                            return true;
                        }
                    }
                }
            }

            if (ignoreStats.Length == 0)
            {
                return false;
            }

            foreach (string ignoreStat in ignoreStats)
            {
                string ignoreStatLower = ignoreStat.ToLower();
                if (ignoreStatLower.EndsWith("*"))
                {
                    int index = ignoreStatLower.LastIndexOf('*');
                    string prefix = ignoreStatLower.Substring(0, index);
                    if (statName.StartsWith(prefix))
                    {
                        return true;
                    }
                }
                else if (statName == ignoreStatLower)
                {
                    return true;
                }
            }

            return false;
        }

		CsvStats ProcessCSV(string csvFilename, string[] statNames, bool bDiscardLastFrame )
        {
            int numRowsToSkip = 0;
            numRowsToSkip = GetIntArg("skipRows",0);

            bool filterOutZeros = false;
            filterOutZeros = GetBoolArg("filterOutZeros");

			CsvStats csvStats = null;
			if (csvCache != null)
			{
				// Copy the data from the source CSV
				csvStats = csvCache.ReadCSVFile(csvFilename, statNames, numRowsToSkip);
			}
			else
			{
				csvStats = CsvStats.ReadCSVFile(csvFilename, statNames, numRowsToSkip);
			}
			// Sometimes the last frame is garbage. We might want to remove it
			if (bDiscardLastFrame)
            {
                foreach (StatSamples stat in csvStats.Stats.Values.ToArray())
                {
                    if (stat.samples.Count > 0)
                    {
                        stat.samples.RemoveAt(stat.samples.Count - 1);
                    }
                }
            }

            float multiplier = GetFloatArg("statMultiplier", 1.0f);

			// Set to zero all entries where the filter stat is below the threshold
			float minFilterStatValue = GetFloatArg("minFilterStatValue", -float.MaxValue);

			StatSamples minFilterStat = null;
			if (minFilterStatValue > -float.MaxValue)
			{
				string minFilterStatName = GetArg("minFilterStatName");
				minFilterStat = csvStats.GetStat(minFilterStatName);
				if (minFilterStat != null)
				{
					for (int i = 0; i < minFilterStat.samples.Count; ++i)
					{
						if (minFilterStat.samples[i] < minFilterStatValue)
						{
							minFilterStat.samples[i] = 0.0f;
						}
					}

					minFilterStat.ComputeAverageAndTotal();
				}
				else
				{
					// Need a proper stat name for the min filter to work
					minFilterStatValue = -float.MaxValue;
				}
			}

            // Filter out stats which are ignored etc
            List<StatSamples> FilteredStats = new List<StatSamples>();
            foreach (StatSamples stat in csvStats.Stats.Values.ToArray() )
            {
                if ( IsStatIgnored( stat.Name ) )
                {
                    continue;
                }

				if (minFilterStatValue > -float.MaxValue )
				{
					if (stat != minFilterStat)
					{
						// Reset other stats when the filter stat entry was below the threshold
						for (int i = 0; i < stat.samples.Count; i++)
						{
							if (minFilterStat.samples[i] == 0.0f)
							{
								stat.samples[i] = 0.0f;
							}
						}
					}
				}

                if ( filterOutZeros )
                {
                    float lastNonZero = 0.0f;
                    for (int i = 0; i < stat.samples.Count; i++)
                    {
                        if ( stat.samples[i] == 0.0f )
                        {
                            stat.samples[i] = lastNonZero;
                        }
                        else
                        {
                            lastNonZero = stat.samples[i];
                        }
                    }
                    stat.ComputeAverageAndTotal();
                }

                if (multiplier != 1.0f)
                {
                    for (int i = 0; i < stat.samples.Count; i++)
                    {
                        stat.samples[i] *= multiplier;
                    }
                }

                // Filter out stats where the average below averageThreshold
                if (averageThreshold > -float.MaxValue)
                {
                    if (stat.average < averageThreshold)
                    {
                        continue;
                    }
                }

                // Filter out stats below the threshold
                if (threshold > -float.MaxValue)
                {
                    bool aboveThreshold = false;
                    foreach (float val in stat.samples)
                    {
                        if (val > threshold)
                        {
                            aboveThreshold = true;
                            break;
                        }
                    }

                    if ( !aboveThreshold )
                    {
                        continue;
                    }
                }

                // LLM specific, spaces in LLM seem to be $32$.
                // This is a temp fix until LLM outputs without $32$.
                stat.Name = stat.Name.Replace("$32$", " ");
                // If we get here, the stat wasn't filtered
                FilteredStats.Add(stat);
            }

            // Have any stats actually been filtered? If so, replace the list
            if ( FilteredStats.Count < csvStats.Stats.Count )
            {
                csvStats.Stats.Clear();
                foreach (StatSamples stat in FilteredStats)
                {
                    csvStats.AddStat(stat);
                }
            }

            // Filter out events
            List<CsvEvent> FilteredEvents = new List<CsvEvent>();
            foreach (CsvEvent ev in csvStats.Events)
            {
                if ( IsEventShown(ev.Name) )
                {
                    FilteredEvents.Add( ev );
                }
            }
            csvStats.Events = FilteredEvents;

            return csvStats;
        }

        void RecomputeStatAveragesForRange(CsvStats stats, Range range)
        {
            foreach (StatSamples stat in stats.Stats.Values.ToArray())
            {
                int rangeStart = Math.Max(0, (int)range.MinX);
                int rangeEnd = Math.Min(stat.samples.Count, (int)range.MaxX);

                stat.ComputeAverageAndTotal(rangeStart, rangeEnd);
            }
        }


        Range ComputeAdjustedYRange(Range range, Rect rect)
        {
            Range newRange = new Range(range.MinX, range.MaxX, range.MinY, range.MaxY);
            int maxNumSamples = 0;
            float maxSample = -10000000.0f;
            float minSample =  10000000.0f;

            foreach (CsvStats stats in csvStats) 
            {
                foreach (StatSamples samples in stats.Stats.Values)
                {
                    maxNumSamples = Math.Max(maxNumSamples, samples.samples.Count);
                    foreach( float sample in samples.samples )
                    {
                        if ( sample > maxSample )
                        {
                            maxSample = sample;
                        }
                        minSample = Math.Min(minSample, sample);
                        maxSample = Math.Max(maxSample, sample);
                    }
                }
            }

            // HACK: Clamp to 1m to prevent craziness
            if (maxSample > 1000000) maxSample = 1000000;

            if (range.MinY == Range.Auto) newRange.MinY = 0.0f;
            if (range.MaxY == Range.Auto) newRange.MaxY = maxSample * 1.05f;

            // Quantise based on yincrement
            if (rect != null)
            {
                float yInc = GetYAxisIncrement(rect, newRange);
                float difY = newRange.MaxY - newRange.MinY;
                float newDifY = (int)(0.9999 + difY / yInc) * yInc;
                newRange.MaxY = newRange.MinY + newDifY;
            }

            return newRange;
        }

        Range ComputeAdjustedXRange(Range range, Rect rect)
        {
            Range newRange = new Range(range.MinX, range.MaxX, range.MinY, range.MaxY);
            int maxNumSamples = 0;
            foreach (CsvStats stats in csvStats)
            {
                foreach (StatSamples samples in stats.Stats.Values)
                {
                    maxNumSamples = Math.Max(maxNumSamples, samples.samples.Count);
                }
            }

            if (range.MinX == Range.Auto) newRange.MinX = 0;
            if (range.MaxX == Range.Auto) newRange.MaxX = maxNumSamples;

            // Quantize based on xincrement
            if (rect != null)
            {
                float xInc = GetXAxisIncrement(rect, newRange);
                float difX = newRange.MaxX - newRange.MinX;
                float newDifX = (int)(0.9999 + difX / xInc) * xInc;
                newRange.MaxX = newRange.MinX + newDifX;
            }

            return newRange;
        }



        CsvStats SmoothStats(CsvStats stats, int KernelSize)
        {
            // Compute Gaussian Weights
            float [] Weights = new float[KernelSize];
            float TotalWeight = 0.0f;
            for (int i = 0; i < KernelSize; i++)
            {
                double df = (double)i+0.5;
                double b = (double)KernelSize/2;
                double c = (double)KernelSize/8;
                double weight = Math.Exp(-(Math.Pow(df - b, 2.0) / Math.Pow(2 * c, 2.0)));
                Weights[i] = (float)weight;
                TotalWeight += (float)weight;
            }

            for (int i = 0; i < KernelSize; i++)
            {
                Weights[i] /= TotalWeight;
            }

            List<StatSamples> statSampleList = stats.Stats.Values.ToList();

            // Add the stats to smoothstats before the parallel for, so we can preserve the order
            CsvStats smoothStats = new CsvStats();
            foreach( StatSamples srcStatSamples in stats.Stats.Values )
            {
                StatSamples NewStatSamples = new StatSamples(srcStatSamples,false);
                smoothStats.AddStat(NewStatSamples);
            }
            StatSamples[] SmoothSamplesArray = smoothStats.Stats.Values.ToArray();

            //foreach( StatSamples srcStatSamples in stats.Stats.Values )
            Parallel.For(0, stats.Stats.Values.Count, i =>
            {
                StatSamples srcStatSamples = statSampleList[i];
                StatSamples destStatSamples = SmoothSamplesArray[i];// new StatSamples(srcStatSamples);

                int maxSampleIndex = srcStatSamples.samples.Count - 1;
                for (int j = 0; j < srcStatSamples.samples.Count; j++)
                {
                    float sum = 0.0f;
                    for (int k = 0; k < KernelSize; k++)
                    {
                        int SampleIndex = j - (KernelSize / 2) + k;

                        // If the index is out of range, mirror it (so as not to bias the result)
                        if (SampleIndex < 0)
                        {
                            SampleIndex = -SampleIndex;
                        }
                        else if (SampleIndex > maxSampleIndex)
                        {
                            SampleIndex = maxSampleIndex - (SampleIndex - maxSampleIndex);
                        }
                        // clamp the src index to a valid range
                        int srcIndex = Math.Min(Math.Max(0, SampleIndex), maxSampleIndex);

                        sum += srcStatSamples.samples[srcIndex] * Weights[k];
                    }
                    destStatSamples.samples.Add(sum);
                }
            }
            );
            smoothStats.metaData = stats.metaData;
            smoothStats.Events = stats.Events;
            return smoothStats;
        }

        void SetLegend(CsvStats csvStats, string[] customLegendNames, string csvFilename, string[] hideStatPrefixes, bool UseFilename)
        {
            int customIndex = 0;
            foreach (StatSamples stat in csvStats.Stats.Values)
            {
                stat.LegendName = stat.Name;
                if (UseFilename)
                {
                    stat.LegendName = MakeShortFilename(csvFilename);
                }
                if (customLegendNames != null && customIndex < customLegendNames.Length)
                {
                    stat.LegendName = customLegendNames[customIndex];
                    customIndex++;
                }
                foreach (string hideStatPrefix in hideStatPrefixes)
                {
                    if (hideStatPrefix.Length > 0)
                    {
                        if (stat.LegendName.ToLower().StartsWith(hideStatPrefix))
                        {
                            stat.LegendName = stat.LegendName.Substring(hideStatPrefix.Length);
                        }
                    }
                }
            }
        }

        void AssignColours(CsvStats stats, bool reverseOrder)
        {
            if (reverseOrder)
                colourOffset = stats.Stats.Values.Count - 1;

            foreach (StatSamples stat in stats.Stats.Values)
            {
                if (stat.colour.alpha == 0.0f )
                {
                    stat.colour = theme.GraphColours[colourOffset % theme.GraphColours.Length];
                }
                if (reverseOrder)
                    colourOffset--;
                else
                    colourOffset++;
            }
        }
        CsvStats StackStats(CsvStats stats, Range range, bool stackedUnsorted)
        {
            List<float> SumList = new List<float>();

            // Find our stack total stat
            StatSamples totalStat = null;
            if (stackTotalStat.Length > 0)
            {
                foreach (StatSamples stat in stats.Stats.Values)
                {
                    if (stat.Name.ToLower() == stackTotalStat)
                    {
                        totalStat = stat;
                        break;
                    }
                }
            }

            // sort largest->smallest (based on average)
            List<StatSamples> StatSamplesSorted = new List<StatSamples>();
            foreach (StatSamples stat in stats.Stats.Values)
            {
                if (stat != totalStat)
                {
                    StatSamplesSorted.Add(stat);
                }
            }

            if (!stackedUnsorted)
            {
                StatSamplesSorted.Sort();
            }

            StatSamples OtherStat = null;
            StatSamples OtherUnstacked = null;
            int rangeStart = Math.Max(0, (int)range.MinX);
            int rangeEnd = (int)range.MaxX;

            if (!stackTotalStatIsAutomatic && totalStat != null)
            {
                OtherStat = new StatSamples("Other");
                OtherStat.colour = new Colour(0x6E6E6E);
                StatSamplesSorted.Add(OtherStat);
                foreach (float value in totalStat.samples)
                    OtherStat.samples.Add(value);
                OtherUnstacked = new StatSamples(OtherStat,false);
                rangeEnd = Math.Min(OtherStat.samples.Count, (int)range.MaxX);
            }

            // Make the stats cumulative
            List<StatSamples> StatSamplesList = new List<StatSamples>();
            foreach (StatSamples srcStatSamples in StatSamplesSorted)
            {
                StatSamples destStatSamples = new StatSamples(srcStatSamples,false);
                for (int j = 0; j < srcStatSamples.samples.Count; j++)
                {
                    float value=srcStatSamples.samples[j]; 
                    if (j > SumList.Count - 1) // Grow the list as necessary
                    {
                        SumList.Add(value);
                    }
                    else
                    {
                        if (srcStatSamples == OtherStat)
                        {
                            // The previous value of value is the total. If there are no other stats, then the total is "other"
                            value = Math.Max(value - SumList[j], 0.0f);
                        }
                        SumList[j] += value;
                    }
                    if (srcStatSamples == OtherStat)
                    {
                        OtherUnstacked.samples.Add(value);
                    }
                    destStatSamples.samples.Add(SumList[j]);
                }

                // If this is the "other" stat, recompute the average based on the unstacked value and apply that to the dest stat (this is used in the legend)
                if (srcStatSamples == OtherStat)
                {
                    OtherUnstacked.ComputeAverageAndTotal(rangeStart, rangeEnd);
                    destStatSamples.average = OtherUnstacked.average;
                }

                StatSamplesList.Add(destStatSamples);
            }

            // Compute the other stat average for the selected range

            // Copy out the list in reverse order (so they get rendered front->back)
            CsvStats stackedStats = new CsvStats();
			if (stackTotalStatIsAutomatic)
			{
				stackedStats.AddStat(totalStat);
			}

			for (int i = StatSamplesList.Count-1; i >= 0; i--)
            {
                stackedStats.AddStat(StatSamplesList[i]);
            }
            
            stackedStats.metaData = stats.metaData;
            stackedStats.Events = stats.Events;
            return stackedStats;
        }

        float GetXAxisIncrement(Rect rect, Range range)
        {
            float[] xIncrements = { 0.1f, 1.0f, 5.0f, 10.0f, 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f };
            float xIncrement = xIncrements[0];

            for (int i = 0; i < xIncrements.Length - 1; i++)
            {
                float gap = ToSvgXScale(xIncrement, rect, range);
                if (gap < 25)
                {
                    xIncrement = xIncrements[i + 1];
                }
                else
                {
                    break;
                }

            }
            int xRange = (int)range.MaxX - (int)range.MinX;
            //xIncrement = xIncrement - (xRange % (int)xIncrement);
            return xIncrement;
        }

        float GetYAxisIncrement(Rect rect, Range range)
        {
            float[] yIncrements = { 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000, 5000, 10000, 20000, 50000, 100000, 200000, 500000, 1000000, 2000000, 5000000, 10000000 };

            // Find a good increment
            float yIncrement = yIncrements[0];
            for (int i = 0; i < yIncrements.Length - 1; i++)
            {
                float gap = ToSvgYScale(yIncrement, rect, range);
                if (gap < 25)
                {
                    yIncrement = yIncrements[i + 1];
                }
                else
                {
                    break;
                }
            }
            return yIncrement;
        }

        void DrawGridLines(Rect rect, Range range, bool graphOnly, float alpha=1.0f, bool extendLines = false, int frameOffset = 0)
        {
            float xIncrement = GetXAxisIncrement(rect, range);
            float yIncrement = GetYAxisIncrement(rect, range);
			float yScale = 1.0f;

            // Draw 1ms grid lines
            float minorYAxisIncrement = 1.0f;
            if ((range.MaxY - range.MinY) > 100) minorYAxisIncrement = yIncrement / 10.0f;
            minorYAxisIncrement = Math.Max(minorYAxisIncrement, yIncrement/5.0f);

            Colour colour = new Colour(theme.MinorGridlineColour);
            colour.alpha = alpha;

            // TODO: replace with <pattern>
            SvgWriteLine("<g id='gridLines<UNIQUE>'>");
            for (int i = 0; i < 5000; i++)
            {
                float y = range.MinY + (float)i * minorYAxisIncrement;
                DrawHorizLine(y, colour, rect, range);
                if (y >= range.MaxY) break;
            }

            colour = new Colour(theme.MajorGridlineColour);
            colour.alpha = (alpha+3.0f)/4.0f;
            for (int i = 0; i < 2000; i++)
            {
                float y = range.MinY + (float)i * yIncrement;
                DrawHorizLine(y, colour, rect, range,false, 0.25f, false);
                yScale += yIncrement;
                if (y >= range.MaxY) break;
            }
            SvgWriteLine("</g>");

            // Draw the axis text
            if (!graphOnly)
            {
                SvgWriteLine("<g id='xAxis<UNIQUE>'>");
                for (int i = 0; i < 2000; i++)
                {
                    float x = range.MinX + i * xIncrement;
                    if (x > (range.MaxX)) break;
					int displayFrame = (int) Math.Floor(x) + frameOffset;
                    DrawHorizontalAxisText(displayFrame.ToString(), x, theme.TextColour, rect, range);
                }
                SvgWriteLine("</g>");

                SvgWriteLine("<g id='yAxis<UNIQUE>'>");
                for (int i = 0; i < 2000; i++)
                {
                    float y = range.MinY + (float)i * yIncrement;
                    DrawVerticalAxisText(y.ToString(), y, theme.TextColour, rect, range);
                    DrawVerticalAxisNotch(y, theme.LineColour, rect, range);
                    if (y >= range.MaxY) break;
                }
                SvgWriteLine("</g>");
            }

            DrawVerticalLine(0, theme.AxisLineColour, rect, range, 0.5f );
            DrawHorizLine(0.0f, theme.AxisLineColour, rect, range, false, 0.5f);

            float budgetLineThickness = theme.BudgetLineThickness;
            Colour budgetLineColour = new Colour(theme.BudgetLineColour);
            bool dropShadow = false;
            if (alpha != 1.0f)
            {
                dropShadow = true;
                budgetLineThickness *= 2.0f;
                budgetLineColour.r = (byte)((float)budgetLineColour.r * 0.9f);
                budgetLineColour.g = (byte)((float)budgetLineColour.g * 0.9f);
                budgetLineColour.b = (byte)((float)budgetLineColour.b * 0.9f);
            }
            DrawHorizLine(budget, budgetLineColour, rect, range, true, budgetLineThickness, dropShadow);
        }

		void DrawText(string text, float x, float y, float size, Rect rect, Colour colour, string anchor = "start", string font="Helvetica", string id="", bool dropShadow = false)
        {

            SvgWriteLine("<text x='" + (rect.x + x) + "' y='"+ (rect.y + y) +"' fill="+colour.SVGString()+
                      (id.Length > 0 ? " id='"+id+"'" : "") +
                      (dropShadow ? " filter='url(#dropShadowFilter)'" : "" ) +
                      " font-size='" + size + "' font-family='" + font + "' style='text-anchor: " + anchor + "'>" + text + "</text> >");
        }

        void DrawTitle( string title, Rect rect )
        {
            DrawText(title, (float)(rect.x + rect.width * 0.5), 34, 14, rect, theme.TextColour, "middle", "Helvetica");
        }


        void DrawGraphArea(Colour colour, Rect rect, bool gradient, bool interactive = false )
        {
            SvgWriteLine("<rect x='0' y='0' width='" + (dimensions.width) + "' height='" + (dimensions.height) + "' fill="+colour.SVGString()+" stroke-width='1' stroke="+theme.BackgroundColour.SVGString() );

            if (gradient)
            {
                SvgWrite("style='fill:url(#radialGradient1);' ");
            }
            if (interactive)
            {
                SvgWrite(" onmousemove='OnGraphAreaClicked<UNIQUE>(evt)'");
                //SvgWrite(" onmousewheel='OnGraphAreaWheeled<UNIQUE>(evt)'");
            }
            SvgWrite( "/>" );
        }

        float ToSvgX(float graphX, Rect rect, Range range)
        {
            float scaleX = rect.width / (range.MaxX - range.MinX);
            return rect.x + ((float)graphX - range.MinX) * scaleX;
        }

        float ToSvgY(float graphY, Rect rect, Range range)
        {
            float svgY = rect.height - (graphY - range.MinY) / (range.MaxY - range.MinY) * rect.height + rect.y;

            float scaleY = rect.height / (range.MaxY - range.MinY);
            float svgY2 = rect.y + rect.height - (graphY - range.MinY) * scaleY;
            return svgY;
        }

        float ToSvgXScale(float graphX, Rect rect, Range range)
        {
            float scaleX = rect.width / (range.MaxX - range.MinX);
            return graphX * scaleX;
        }

        float ToSvgYScale(float graphY, Rect rect, Range range)
        {
            float scaleY = rect.height / (range.MaxY - range.MinY);
            return graphY * scaleY;
        }

        float ToCsvXScale(float graphX, Rect rect, Range range)
        {
            float scaleX = rect.width / (range.MaxX - range.MinX);
            return graphX / scaleX;
        }

        float ToCsvYScale(float graphY, Rect rect, Range range)
        {
            float scaleY = rect.height / (range.MaxY - range.MinY);
            return graphY / scaleY;
        }

        float ToCsvX(float svgX, Rect rect, Range range) 
        { 
            return (svgX - rect.x) * (range.MaxX - range.MinX) / rect.width + range.MinX;
        }

        float ToCsvY(float svgY, Rect rect, Range range)
        {
            return (svgY - rect.y) * (range.MaxY - range.MinY) / rect.height + range.MinY;
        }

        struct Vec2
        {
            public Vec2(float inX, float inY) { X = inX; Y = inY; }
            public float X;
            public float Y;
        };

        class PointInfo
        {
            public void AddPoint(Vec2 pt)
            {
                Count++;
                Max.X = Math.Max(pt.X, Max.X);
                Max.Y = Math.Max(pt.Y, Max.Y);
                Min.X = Math.Min(pt.X, Min.X);
                Min.Y = Math.Min(pt.Y, Min.Y);
                Total.X += pt.X;
                Total.Y += pt.Y;
            }
            public Vec2 ComputeAvg()
            {
                Vec2 avg;
                avg.X = Total.X / (float)Count;
                avg.Y = Total.Y / (float)Count;
                return avg;
            }
            public Vec2 Max = new Vec2(float.MinValue, float.MinValue);
            public Vec2 Min = new Vec2(float.MaxValue, float.MaxValue);
            public Vec2 Total = new Vec2(0, 0);
            public int Count = 0;
        };

        List<Vec2> RemoveRedundantPoints(List<Vec2> RawPoints, float compressionThreshold, int passIndex )
        {
            //return RawPoints;
            //compressionThreshold = 0;
            if (RawPoints.Count == 0)
            {
                return RawPoints;
            }
            List<Vec2> StrippedPoints = new List<Vec2>();

            int offset = passIndex % 2;

            // Add the first points
            int i = 0;
            for (i = 0; i <= offset; i++)
            {
                StrippedPoints.Add(RawPoints[i]);
            }
            for ( ; i < RawPoints.Count-1; i += 2)
            {
                Vec2 p1 = RawPoints[i - 1];
                Vec2 pCentre = RawPoints[i];
                Vec2 p2 = RawPoints[i + 1];

                // Interpolate to find where the line would be if we removed this point
                float dX = p2.X - p1.X;
                float centreDX = pCentre.X - p1.X;
                float lerpValue = centreDX / dX;
                float interpX = p1.X * (1.0f - lerpValue) + (p2.X * lerpValue);
                float interpY = p1.Y * (1.0f - lerpValue) + (p2.Y * lerpValue);

                //float distMh = Math.Abs(interpX - pCentre.X) + Math.Abs(interpY - pCentre.Y);
                float distMh = Math.Abs(interpY - pCentre.Y);
                if (distMh >= compressionThreshold)
                {
                    StrippedPoints.Add(pCentre);
                }
                StrippedPoints.Add(p2);
            }
            // Add the last points
            for (; i < RawPoints.Count; i++)
            {
                StrippedPoints.Add(RawPoints[i]);
            }
            return StrippedPoints;
        }

        List<Vec2> CompressPoints(List<Vec2> RawPoints, float pixelsPerPoint, Rect rect, Range range)
        {
            //return RawPoints;
            List<Vec2> CompressedPoints = new List<Vec2>();

            if (RawPoints.Count<3)
            {
                return RawPoints;
            }
            int numPointsAfterCompression = (int)((float)rect.width / pixelsPerPoint);
            if (numPointsAfterCompression >= RawPoints.Count)
            {
                return RawPoints;
            }
            float NumOutputPointsPerInputPoint = (float)numPointsAfterCompression/ (float)RawPoints.Count;

            List<PointInfo> pointInfoList = new List<PointInfo>(numPointsAfterCompression);
            for (int i = 0; i < numPointsAfterCompression; i++) pointInfoList.Add(new PointInfo());

            // Compute average, min, max for every point
            for ( int i=0; i< RawPoints.Count; i++ )
            {
                Vec2 point = RawPoints[i];
                float outPointX = (float)i * NumOutputPointsPerInputPoint;
                int outPointIndex = Math.Min((int)(outPointX),numPointsAfterCompression-1);
                pointInfoList[outPointIndex].AddPoint( point );
            }

            CompressedPoints.Add(pointInfoList[0].ComputeAvg());
            for (int i = 1; i < pointInfoList.Count - 1; i++)
            {
                PointInfo info = pointInfoList[i];
                PointInfo last = pointInfoList[i - 1];
                PointInfo next = pointInfoList[i + 1];
                if (info.Min.Y <= last.Min.Y && info.Min.Y < next.Min.Y)
                {
                    CompressedPoints.Add(info.Min);
                }
                else if (info.Max.Y >= last.Max.Y && info.Max.Y > next.Max.Y)
                {
                    CompressedPoints.Add(info.Max);
                }
                else
                {
                    CompressedPoints.Add(info.ComputeAvg());
                }
            }
            CompressedPoints.Add(pointInfoList.Last().ComputeAvg());
            float compressionThreshold = pixelsPerPoint;
            List<Vec2> StrippedPoints = new List<Vec2>();


            for (int i = 0; i < 8; i++)
            {
                CompressedPoints = RemoveRedundantPoints(CompressedPoints, compressionThreshold,i); 
            }
            return CompressedPoints;
        }

        void DrawGraph(List<float> samples, Colour colour, Rect rect, Range range, float thickness, bool filled, float compression, string id)
        {
            int n = Math.Min(samples.Count,(int)(range.MaxX+0.5));
            int start = (int)(range.MinX+0.5);

			int numDecimalPlaces = GetIntArg("lineDecimalPlaces", 3);

            if (start + 1 < n)
            {
                List<Vec2> RawPoints = new List<Vec2>();
                for (int i = start; i < n; i++)
                {
                    float sample = samples[i];
                    float x = ToSvgX((float)i, rect, range);
                    float y = ToSvgY(sample, rect, range);
                
                    RawPoints.Add( new Vec2(x,y) );
                }

                if (compression > 0.0f )
                {
                    RawPoints = CompressPoints(RawPoints, compression, rect, range);
                }

				string formatStr = "0";
				if (numDecimalPlaces>0)
				{
					formatStr += ".";
					for (int i=0; i<numDecimalPlaces;i++)
					{
						formatStr += "0";
					}
				}
                string idString = id.Length > 0 ? "id='" + id + "'" : "";
                SvgWriteLine("<polyline "+idString+" points='");
                foreach (Vec2 point in RawPoints)
                {
                    float x = point.X;
                    float y = point.Y;
                    SvgWrite(" " + x.ToString(formatStr) + "," + y.ToString(formatStr), false);
                }

                string fillString = "none";
                if (filled)
                {
                    thickness = 0.0f;// Math.Min(thickness, 1.0f);
                    float lastSample = samples[n-1];
                    SvgWrite(" " + ToSvgX((float)n + 20, rect, range) + "," + ToSvgY(lastSample, rect, range), false);
                    SvgWrite(" " + ToSvgX((float)n + 20, rect, range) + "," + ToSvgY(0.0f, rect, range), false);
                    SvgWrite(" " + ToSvgX((float)n - 20, rect, range) + "," + ToSvgY(0.0f, rect, range), false);
                    SvgWrite(" " + ToSvgX(start, rect, range) + "," + ToSvgY(0.0f, rect, range), false);

                    colour.alpha = 1.0f;
                    fillString = colour.SVGStringNoQuotes();
                }
                SvgWriteLine("' style='fill:" + fillString + ";stroke-width:" + thickness + "; clip-path: url(#graphArea)' stroke=" + colour.SVGString() + "/>");
            }
        }
        void DrawPercentileGraph(List<float> samples, Colour colour, Rect rect, Range range, string id)
        {
            samples.Sort();
            List<Vec2> RawPoints = new List<Vec2>();
            const int NUM_SAMPLES = 100;
            int Begin = (int)range.MinX;
            int End = (int)range.MaxX;
            int Delta = End - Begin;
            float Percentile, xbase, x, y;
            int Count = samples.Count;
            for (int i = NUM_SAMPLES*Begin; i < NUM_SAMPLES * End; i += Delta)
            {
                int sampleIndex = Count * i / (NUM_SAMPLES * 100);
                Percentile = samples[sampleIndex];
                xbase = (float)i / NUM_SAMPLES;
                x = ToSvgX(xbase, rect, range);
                y = ToSvgY(Percentile, rect, range);
                RawPoints.Add(new Vec2(x, y));
            }

            Percentile = samples[Count-1];
            xbase = 100.0F;
            x = ToSvgX(xbase, rect, range);
            y = ToSvgY(Percentile, rect, range);
            RawPoints.Add(new Vec2(x, y));
            string idString = id.Length > 0 ? "id='" + id + "'" : "";
            SvgWriteLine("<polyline " + idString + " points='");
            foreach (Vec2 point in RawPoints)
            {
                x = point.X;
                y = point.Y;

                SvgWrite(" " + x + "," + y, false);
            }
            SvgWriteLine("' style='fill:none;stroke-width:1.3; clip-path: url(#graphArea)' stroke=" + colour.SVGString() + "/>");

        }


		class CsvEventWithCount : CsvEvent
		{
			public CsvEventWithCount(CsvEvent ev)
			{
				base.Frame = ev.Frame;
				base.Name = ev.Name;
				count = 1;
			}
			public int count;
		};

		void DrawEventText(List<CsvEvent> events, Colour colour, Rect rect, Range range)
        {
			float LastEventX = -100000.0f;
			int lastFrame = 0;

			// Make a filtered list of events to display, grouping duplicates which are close together
			List<CsvEventWithCount> filteredEvents = new List<CsvEventWithCount>();
			CsvEventWithCount currentDisplayEvent = null;
			CsvEvent lastEvent = null;
			foreach (CsvEvent ev in events)
			{
				// Only draw events which are in the range
				if (ev.Frame >= range.MinX && ev.Frame <= range.MaxX)
				{
					// Merge with the current display event?
					bool bMerge = false;
					if (currentDisplayEvent != null && lastEvent != null && ev.Name == currentDisplayEvent.Name)
					{
						float DistToLastEvent = ToSvgX(ev.Frame, rect, range) - ToSvgX(lastEvent.Frame, rect, range);
						if (DistToLastEvent <= 8.5)
						{
							bMerge = true;
						}
					}

					if (bMerge)
					{
						currentDisplayEvent.count++;
					}
					else
					{
						currentDisplayEvent = new CsvEventWithCount(ev);
						filteredEvents.Add(currentDisplayEvent);
					}
					lastEvent = ev;
				}
			}

			foreach (CsvEventWithCount ev in filteredEvents)
            {
				float eventX = ToSvgX(ev.Frame, rect, range);
                string name = ev.Name;

				if ( ev.count > 1 )
				{
					name += " &#x00D7; " + ev.count;
				}

                // Space out the events (allow at least 8 pixels between them)
                if (eventX - LastEventX <= 8.5f)
                {
					// Add an arrow to indicate events were spaced out 
					name = "&#x21b7; " + name;
					if ( ev.count == 1 )
					{
						name += " (+" + (ev.Frame - lastFrame) + ")";
					}
					eventX = LastEventX + 9.0f;
                }
                float csvTextX = ToCsvX(eventX + 7, rect, range);

                DrawHorizontalAxisText(name, csvTextX, colour, rect, range, 10, true);
                LastEventX = eventX;
                lastFrame = ev.Frame;
            }
        }

        void DrawEventLines(List<CsvEvent> events, Rect rect, Range range)
        {
			float LastEventX = -100000.0f;
			float LastDisplayedEventX = -100000.0f;
			int i = 0;
			foreach (CsvEvent ev in events)
            {
                // Only draw events which are in the range
                if (ev.Frame >= range.MinX && ev.Frame <= range.MaxX)
                {
					float eventX = ToSvgX(ev.Frame, rect, range);
					// Space out the events (allow at least 2 pixels between them)
					if (eventX - LastDisplayedEventX > 3)
					{
						float alpha = (eventX - LastEventX < 1.0f) ? 0.5f : 1.0f;
						DrawVerticalLine(ev.Frame, theme.MajorGridlineColour, rect, range, alpha, true, true);
						LastDisplayedEventX = eventX;
					}
					LastEventX = eventX;
				}
				i++;
			}
        }


        void DrawEventHighlightRegions(CsvStats csvStats, Rect rect, Range range)
        {
            // Draw event regions if requested
            string eventRegionsStr = GetArg("highlightEventRegions");
            if (!string.IsNullOrEmpty(eventRegionsStr))
            {
                Colour highlightColour = new Colour(0xffffff, 0.1f);
                float y0 = rect.y;
                float y1 = rect.y + rect.height;
                float height = y1 - y0;

                string[] delimiters = eventRegionsStr.Split(',');
                int numPairs = delimiters.Length / 2;
                if (delimiters.Length >= 2 && delimiters.Length % 2 != 2)
                {  
                    for (int i = 0; i < numPairs; i++)
                    {
                        string startName = delimiters[i*2].ToLower().Trim();
                        string endName = delimiters[i*2+1].ToLower().Trim();

                        if (endName == "{NULL}")
                        {
                            endName = null;
                        }

                        List<int> startIndices = null;
                        List<int> endIndices = null;
                        csvStats.GetEventFrameIndexDelimiters(startName, endName, out startIndices, out endIndices);
                        for (int j = 0; j < startIndices.Count; j++)
                        {
                            float x0 = ToSvgX(startIndices[j], rect, range);
                            float x1 = ToSvgX(endIndices[j], rect, range);
                            float width = x1 - x0;

                            SvgWriteLine("<rect x='" + x0 + "' y='" + y0 + "' width='" + width + "' height='" + height + "' fill=" + highlightColour.SVGString() + "/>");
                        }
                    }
                }
            }
        }
        void DrawVerticalAxisNotch(float sample, Colour colour, Rect rect, Range range, float thickness = 0.25f )
        {
            if (sample > range.MaxY) return;
            if (sample < range.MinY) return;

            float y = ToSvgY(sample, rect, range);

            float x1 = rect.x - 2.5f;
            float x2 = rect.x;

            SvgWriteLine("<line x1='" + x1 + "' y1='" + y + "' x2='" + x2 + "' y2='" + y + "' style='fill:none;stroke-width:" + thickness + "'"
                + " stroke=" + colour.SVGString() + "/>", false);
        }

        void DrawHorizLine(float sample, Colour colour, Rect rect, Range range, bool dashed = false, float thickness = 0.25f, bool dropShadow = false )
        {
            if (sample > range.MaxY) return;
            if (sample < range.MinY) return;

            float y = ToSvgY(sample, rect, range);

            float x1 = rect.x;
            float x2 = rect.x + rect.width;

            if (dropShadow )
            {
                Colour shadowColour = new Colour(0, 0, 0, 0.33f);
                SvgWriteLine("<line x1='" + (x1-1) + "' y1='" + (y+1) + "' x2='" + (x2-1) + "' y2='" + (y+1) + "' style='fill:none;stroke-width:" + thickness * 1.5f + "'"
                    + " stroke=" + shadowColour.SVGString() + ""
                    + (dashed ? " stroke-dasharray='4,4' d='M5 10 l215 0' " : "")
                    + "/>", false);
            }

            SvgWriteLine("<line x1='" + x1 + "' y1='" + y + "' x2='" + x2 + "' y2='" + y + "' style='fill:none;stroke-width:"+thickness+"'"
                + " stroke=" + colour.SVGString() + ""
                + (dashed ? " stroke-dasharray='4,4' d='M5 10 l215 0' " : "") 
             //   + (dropShadow ? "filter='url(#dropShadowFilter)'>" : "" )
                + "/>", false);
        }

        void DrawVerticalAxisText(string text, float textY, Colour colour, Rect rect, Range range, float size=10.0f)
        {
            float y = ToSvgY(textY, rect, range)+4;
            float x = rect.x - 10.0f;
            SvgWriteLine("<text x='" + x + "' y='" + y + "' fill=" + theme.LineColour.SVGString() + " font-size='" + size + "' font-family='Helvetica' style='text-anchor: end'>" + text + "</text> >");
        }


        void DrawHorizontalAxisText(string text, float textX, Colour colour, Rect rect, Range range, float size = 10.0f, bool top = false )
        {
            float scaleX = rect.width / (float)(range.MaxX - range.MinX - 1);
            float x = ToSvgX(textX,rect,range) - 4;
            float y = rect.y + rect.height + 10;

            if (top)
            {
                y = rect.y + 5;
            }

            SvgWriteLine("<text x='" + y + "' y='" + (-x) + "' fill=" + colour.SVGString() + " font-size='" + size + "' font-family='Helvetica' transform='rotate(90)' >" + text + "</text>");
        }

        void DrawLegend(List<CsvStats> csvStats, Rect rect )
        {
            bool showAverages = GetBoolArg("showAverages");
            bool showTotals = GetBoolArg("showTotals");

            List<StatSamples> statSamples = new List<StatSamples>();

            foreach (CsvStats csvStat in csvStats )
            {
                foreach (StatSamples stat in csvStat.Stats.Values)
                {
					statSamples.Add(stat);
                }
            }

			// Stacked stats are already sorted in the right order
			bool forceLegendSort = GetBoolArg("forceLegendSort");
            bool stacked = GetBoolArg("stacked");
            if (forceLegendSort || ( ( showAverages || showTotals) && !stacked ) )
            {
				statSamples.Sort();
            }


            float x = rect.width + rect.x - 60;
            if (showAverages || showTotals)
            {
                x -= 30;
            }
            float y = 30; // rect.height - 25;

            SvgWriteLine("<g id='LegendPanel<UNIQUE>'>");// transform='translate(5) rotate(45 50 50)'> ");

            // Check if the total is a fraction
            bool legendValueIsWholeNumber = false;
            if (showTotals)
            {
                legendValueIsWholeNumber = true;
                foreach (StatSamples stat in statSamples)
                {
                    if ( stat.total != Math.Floor(stat.total) )
                    {
                        legendValueIsWholeNumber = false;
                        break;
                    }
                }
            }

			float legendAverageThreshold = GetFloatArg("legendAverageThreshold", -float.MaxValue);

            foreach (StatSamples stat in statSamples)
            {
				if (stat.average > legendAverageThreshold)
				{
					Colour colour = stat.colour;
					SvgWriteLine("<text x='" + (x - 5) + "' y='" + y + "' fill=" + theme.TextColour.SVGString() + " font-size='10' font-family='Helvetica' style='text-anchor: end' filter='url(#dropShadowFilter)'>" + stat.LegendName + "</text>");
					SvgWriteLine("<rect x='" + (x + 0) + "' y='" + (y - 8) + "' width='10' height='10' fill=" + colour.SVGString() + " filter='url(#dropShadowFilter)' stroke-width='1' stroke='" + theme.LineColour + "' />");

					if (showAverages || showTotals)
					{
						float legendValue = showTotals ? (float)stat.total : stat.average;
						string formatString = "0.00";
						if (showTotals && legendValueIsWholeNumber)
						{
							formatString = "0";
						}
						SvgWriteLine("<text x='" + (x + 15) + "' y='" + y + "' fill=" + theme.MediumTextColour.SVGString() + " font-size='10' font-family='Helvetica' style='text-anchor: start' filter='url(#dropShadowFilter)'>" + legendValue.ToString(formatString) + "</text>");
					}

					y += 16;
				}
			}

            if (showTotals || showAverages)
            {
                string legendValueTypeString = showTotals ? "(sum)" : "(avg)";
                SvgWriteLine("<text x='" + (x + 15) + "' y='" + y + "' fill=" + theme.MediumTextColour.SVGString() + " font-size='10' font-family='Helvetica' style='text-anchor: start' filter='url(#dropShadowFilter)'>" + legendValueTypeString + "</text>");
            }

            SvgWriteLine("</g>");
        }


        void DrawVerticalLine(float sampleX, Colour colour, Rect rect, Range range, float thickness = 0.25f, bool dashed=false, bool dropShadow = false, string id = "", bool noTranslate = false )
        {
            float x = noTranslate ? sampleX : ToSvgX(sampleX, rect, range);
            float y1 = rect.y;
            float y2 = rect.y + rect.height;

            if (dropShadow)
            {
                Colour shadowColour = new Colour(0, 0, 0, 0.33f);
                SvgWriteLine("<line x1='" + (x - 1) + "' y1='" + (y1 + 1) + "' x2='" + (x - 1) + "' y2='" + (y2 + 1) + "' style='fill:none;stroke-width:" + thickness * 1.5f + "' "
                      + " stroke=" + shadowColour.SVGString() + ""
                      + (dashed ? " stroke-dasharray='4,4' d='M5 10 l215 0' " : "")
                      + (id.Length > 0 ? " id='"+id+"_dropShadow'" : "")
                      + "/>");
            }

            SvgWriteLine("<line x1='" + x + "' y1='" + y1 + "' x2='" + x + "' y2='" + y2 + "' style='fill:none;stroke-width:" + thickness + "' "
                + " stroke=" + colour.SVGString() + ""
                + (dashed ? " stroke-dasharray='4,4' d='M5 10 l215 0' " : "")
                + (id.Length > 0 ? " id='" + id + "'" : "")
                + "/>");

        }

        string GetJSStatName(string statName)
        {
            uint hashCode = (uint)statName.GetHashCode();
            string newString = "_";
            foreach (char c in statName)
            {
                if (( c>='A' && c<='Z') || (c>='a' && c<='z'))
                {
                    newString += c;
                }
                else
                {
                    newString += "_";
                }
            }
            newString += "_" + hashCode.ToString();
            return newString;
        }

        class InteractiveStatInfo
        {
            public string friendlyName;
            public string jsVarName;
            public string jsTextElementId;
            public string jsGroupElementId;
            public string graphID;
            public Colour colour;
            public StatSamples statSamples;
            public StatSamples originalStatSamples;
        };

        void AddInteractiveScripting(System.IO.StreamWriter svgFile, Rect rect, Range range, List<CsvStats> csvStats )
        {
            bool snapToPeaks = GetArg("smooth") != "1" && !GetBoolArg("nosnap");

            // Todo: separate values, quantized frame pos, pass in X axis name
            List<InteractiveStatInfo> interactiveStats = new List<InteractiveStatInfo>();

            // Add the frame number stat
            {
                InteractiveStatInfo statInfo = new InteractiveStatInfo();
                statInfo.friendlyName = "Frame";
                statInfo.jsTextElementId = "csvFrameText<UNIQUE>";
                statInfo.jsGroupElementId = "csvFrameGroup<UNIQUE>";
                statInfo.statSamples = null;
                interactiveStats.Add(statInfo);
            }

            for (int i = 0; i < csvStats.Count; i++)
            {
                foreach (StatSamples stat in csvStats[i].Stats.Values)
                {
                    InteractiveStatInfo statInfo = new InteractiveStatInfo();
                    statInfo.colour = stat.colour;
                    statInfo.friendlyName = stat.LegendName;
                    statInfo.jsVarName = "statData_" + i + "_" + GetJSStatName(stat.Name) + "<UNIQUE>";
                    statInfo.graphID = "Stat_" + i + "_" + stat.Name + "<UNIQUE>";
                    statInfo.jsTextElementId = "csvStatText__" + i + GetJSStatName(stat.Name) + "<UNIQUE>";
                    statInfo.jsGroupElementId = "csvStatGroup__" + i + GetJSStatName(stat.Name) + "<UNIQUE>";
                    statInfo.originalStatSamples = stat;
                    statInfo.statSamples = new StatSamples(stat,false);
                    interactiveStats.Add(statInfo);
                }
            }

            // Record the max value at each index
            // TODO: strip out data outside the range (needs an offset as well as a multiplier)
            int multiplier = 1;
            float numStatsPerPixel = (float)(range.MaxX - range.MinX) / rect.width;

            if (numStatsPerPixel > 1 )
            {
                multiplier = (int)numStatsPerPixel;

                // Compute max value for each frame
                List<float> maxValues = new List<float>();
                foreach (InteractiveStatInfo statInfo in interactiveStats)
                {
                    if (statInfo.originalStatSamples != null)
                    {
                        for (int i = 0; i < statInfo.originalStatSamples.samples.Count; i++)
                        {
                            float value = statInfo.originalStatSamples.samples[i];
                            if (i >= maxValues.Count)
                            {
                                maxValues.Add(value);
                            }
                            else
                            {
                                maxValues[i] = Math.Max(maxValues[i], value);
                            }
                        }
                    }
                }

                int filteredStatCount = maxValues.Count / multiplier;// (int)(range.MaxX) - (int)(range.MinX);

                // Create the filtered stat array
                int offset = multiplier / 2;
                foreach (InteractiveStatInfo statInfo in interactiveStats)
                {
                    if (statInfo.originalStatSamples == null)
                    {
                        continue;
                    }
                    for (int i = 0; i < filteredStatCount; i++)
                    {
                        int srcStartIndex = Math.Max(i * multiplier - offset, 0);
                        int srcEndIndex = Math.Min(i * multiplier + offset+1, maxValues.Count);

                        int bestIndex = -1;
                        float highestValue = float.MinValue;
                        // Find a good index based on nearby maxValues
                        for (int j = srcStartIndex; j < srcEndIndex; j++)
                        {
                            if (maxValues[j] > highestValue)
                            {
                                highestValue = maxValues[j];
                                bestIndex = j;
                            }
                        }
						if (bestIndex < statInfo.originalStatSamples.samples.Count)
						{
							statInfo.statSamples.samples.Add(statInfo.originalStatSamples.samples[bestIndex]);
						}
                    }
                }
            }
            else
            {
                foreach (InteractiveStatInfo statInfo in interactiveStats)
                {
                    statInfo.statSamples = statInfo.originalStatSamples;
                }
            }


            //style='text-anchor: "
            //SvgWriteLine("<g transform='translate(0,-24)'>");
            SvgWriteLine("<g id='interactivePanel<UNIQUE>' visibility='hidden'>");
            DrawVerticalLine(0, new Colour(255, 255, 255, 0.4f), rect, range, 1.0f, true, true, "", true);
            SvgWriteLine("<g id='interactivePanelInnerWithRect<UNIQUE>'>");
            SvgWriteLine("<rect x='0' y='0' width='100' height='100' fill='rgba(0,0,0,0.3)' blend='1' id='interactivePanelRect<UNIQUE>' rx='5' ry='5'/>");
            SvgWriteLine("<g id='interactivePanelInner<UNIQUE>'>");
            Rect zeroRect = new Rect(0, 0, 0, 0);
            foreach (InteractiveStatInfo statInfo in interactiveStats)
            {
                SvgWriteLine("<g id='"+ statInfo.jsGroupElementId + "' transform='translate(0,0)' visibility='hidden'>");
                int textXOffset = -5;
                if (statInfo.colour != null)
                {
                    SvgWriteLine("<rect x='-16' y='0' width='10' height='10' fill=" + statInfo.colour.SVGString() + " filter='url(#dropShadowFilter)' stroke-width='1' stroke='" + theme.LineColour + "'/>");
                    textXOffset = -20;
                }
                DrawText(statInfo.friendlyName, textXOffset, 9, 11, zeroRect, new Colour(255, 255, 255, 1.0f), "end", "Helvetica", "", true);
                DrawText(" ", 5, 9, 11, zeroRect, new Colour(255, 255, 255, 1.0f), "start", "Helvetica", statInfo.jsTextElementId, true);

                SvgWriteLine("</g>");
            }
            SvgWriteLine("</g>"); // interactivePanelInner
            SvgWriteLine("</g>"); // interactivePanelInnerWithRect
            SvgWriteLine("</g>"); // interactivepanel

            SvgWriteLine("<script type='application/ecmascript'> <![CDATA[");
            float oneOverMultiplier = 1.0f / (float)multiplier;

            // Write the data array for each stat
            foreach (InteractiveStatInfo statInfo in interactiveStats)
            {
                if (statInfo.statSamples != null)
                {
                    SvgWrite("var " + statInfo.jsVarName + " = [");
                    foreach (float value in statInfo.statSamples.samples)
                    {
                        SvgWrite(value.ToString("0.00") + ",");
                    }
                    SvgWriteLine("]");
                }
            }

            SvgWriteLine("function OnLoaded<UNIQUE>(evt)");
            SvgWriteLine("{");
            SvgWriteLine("}");

            if (snapToPeaks )
            {
                // Record the max value at each index
                List<float> maxValues = new List<float>();
                foreach (InteractiveStatInfo statInfo in interactiveStats)
                {
                    if (statInfo.statSamples != null)
                    {
                        for (int i = 0; i < statInfo.statSamples.samples.Count; i++)
                        {
                            float value = statInfo.statSamples.samples[i];
                            if (i >= maxValues.Count)
                            {
                                maxValues.Add(value);
                            }
                            else
                            {
                                maxValues[i] = Math.Max(maxValues[i], value);
                            }
                        }
                    }
                }

                SvgWrite("var maxValues<UNIQUE> = [");
                foreach (float value in maxValues)
                {
                    SvgWrite(value + ",");
                }
                SvgWriteLine("]");

                SvgWriteLine("function FindInterestingFrame<UNIQUE>(x,range)");
                SvgWriteLine("{");
                SvgWriteLine("    range *= " + oneOverMultiplier + ";");
                SvgWriteLine("    x = Math.round( x*" + oneOverMultiplier +" );");
                SvgWriteLine("    halfRange = Math.round(range/2.0);");
                SvgWriteLine("    startX = Math.round(Math.max(x-halfRange,0));");
                SvgWriteLine("    endX = Math.round(Math.min(x+halfRange,maxValues<UNIQUE>.length));");
                SvgWriteLine("    maxVal = 0.0;");
                SvgWriteLine("    bestIndex = x;");
                SvgWriteLine("    for ( i=startX;i<endX;i++) ");
                SvgWriteLine("    {");
                SvgWriteLine("        if (maxValues<UNIQUE>[i]>maxVal)");
                SvgWriteLine("        {");
                SvgWriteLine("            bestIndex=i;");
                SvgWriteLine("            maxVal = maxValues<UNIQUE>[i];");
                SvgWriteLine("        }");
                SvgWriteLine("     }");
                SvgWriteLine("     return Math.round( bestIndex *" +multiplier+");" );
                SvgWriteLine("}");
                SvgWriteLine("  ");
            }
            else
            {
                SvgWriteLine("function FindInterestingFrame<UNIQUE>(x,range) { return x; }");
            }




            SvgWriteLine("function GetGraphX(mouseX)");
            SvgWriteLine("{");
            SvgWriteLine("  return (mouseX - " + rect.x + ") * (" + range.MaxX + " - " + range.MinX + ") / " + rect.width + " + " + range.MinX + ";");
            SvgWriteLine("}");
            SvgWriteLine("  ");

            SvgWriteLine("function compareSamples(a, b)");
            SvgWriteLine("{");
            SvgWriteLine("      if (a.value > b.value)");
            SvgWriteLine("          return -1;");
            SvgWriteLine("      if (a.value < b.value)");
            SvgWriteLine("          return 1;");
            SvgWriteLine("      return 0;");
            SvgWriteLine("}");

            SvgWriteLine("function ToSvgX(graphX)");
            SvgWriteLine("{");
            SvgWriteLine("    scaleX = "+rect.width / (range.MaxX - range.MinX)+";");
            SvgWriteLine("    return "+rect.x+" + (graphX - "+range.MinX+") * scaleX;");
            SvgWriteLine("}");

            /*
            SvgWriteLine("var graphView = { offsetX:0.0, offsetY:0.0, scaleX:1.0, scaleY:1.0 };");
            SvgWriteLine("function OnGraphAreaWheeled<UNIQUE>(evt)");
            SvgWriteLine("{");
            SvgWriteLine("     var graphAreaElement = document.getElementById('graphAreaTransform');");
            SvgWriteLine("     graphView.scaleX+=0.1;");
            SvgWriteLine("     ");
            SvgWriteLine("     graphAreaElement.setAttribute('transform','scale('+graphView.scaleX+','+graphView.scaleY+')')");
            SvgWriteLine("}");
*/

            int onePixelFrameCount = (int)(ToCsvXScale(1.0f, rect, range) * 8.0f);
            float rightEdgeX = ToSvgX(range.MaxX, rect, range);

            SvgWriteLine("function OnGraphAreaClicked<UNIQUE>(evt)");
            SvgWriteLine("{");
            SvgWriteLine("  graphX = GetGraphX(evt.offsetX); ");
            SvgWriteLine("  var interactivePanel = document.getElementById('interactivePanel<UNIQUE>');");
            SvgWriteLine("  var legendPanel = document.getElementById('LegendPanel<UNIQUE>');");
            // Snap to an interesting frame (the max value under the pixel)

            SvgWriteLine("  var frameNum = FindInterestingFrame<UNIQUE>( Math.round(graphX+0.5), " + onePixelFrameCount + " );");
            SvgWriteLine("  if (frameNum >= " + range.MinX + " && frameNum < " + range.MaxX+")");
            SvgWriteLine("  {");
            SvgWriteLine("    var xOffset = 0;");
            SvgWriteLine("    var lineX = ToSvgX(frameNum);");
            SvgWriteLine("    var textX = lineX + xOffset;");
            SvgWriteLine("    var textY = " + rect.y + " - 20");

            SvgWriteLine("    var interactivePanelInner = document.getElementById('interactivePanelInner<UNIQUE>');");
            SvgWriteLine("    legendPanel.setAttribute('visibility','hidden')");
            SvgWriteLine("    interactivePanel.setAttribute('visibility','visible')");
            SvgWriteLine("    interactivePanel.setAttribute('transform','translate('+textX+',0)')");
            SvgWriteLine("    var dataIndex = Math.round( frameNum * " + oneOverMultiplier + " );");

            // Fill out the sample data array
            SvgWriteLine("    var samples = [");
            foreach (InteractiveStatInfo statInfo in interactiveStats)
            {
                string groupElementString = "document.getElementById('" + statInfo.jsGroupElementId + "')";
                string textElementString = "document.getElementById('" + statInfo.jsTextElementId + "')";
                if ( statInfo.jsVarName == null )
                {
                    SvgWrite("            { value: frameNum, name: '" + statInfo.friendlyName + "', colour: 'rgb(0,0,0)'");
                }
                else
                {
                    string valueStr = statInfo.jsVarName + "[dataIndex]";
                    SvgWrite("            { value: " + valueStr + ", name: '" + statInfo.friendlyName + "', colour: " + statInfo.colour.SVGString() );
                }
                SvgWriteLine(", groupElement: " + groupElementString + ", textElement: " + textElementString + " },");
            }
            SvgWriteLine("    ];");

            SvgWriteLine("    samples.sort(compareSamples);");
            SvgWriteLine("    for ( i=0;i<samples.length;i++ )");
            SvgWriteLine("    {");
            SvgWriteLine("        var groupElement = samples[i].groupElement;");
            SvgWriteLine("        if ( samples[i].value > 0 )");
            SvgWriteLine("        {");
            SvgWriteLine("            groupElement.setAttribute('visibility','inherit');");
            SvgWriteLine("            groupElement.setAttribute('transform','translate(0,'+textY+')');");
            SvgWriteLine("            var textElement = samples[i].textElement;");
            //            SvgWriteLine("            var textNode = document.createTextNode(interactivePanelInner.getBBox().x);");
            SvgWriteLine("            var textNode = document.createTextNode(samples[i].value);");
            SvgWriteLine("            textElement.replaceChild(textNode,textElement.childNodes[0]); ");
            SvgWriteLine("            textY += 15;");
            SvgWriteLine("        }");
            SvgWriteLine("        else");
            SvgWriteLine("        {");
            SvgWriteLine("            groupElement.setAttribute('visibility','hidden')");
            SvgWriteLine("        }");
            SvgWriteLine("    }");

            SvgWriteLine("    var panelRect = document.getElementById('interactivePanelRect<UNIQUE>');");
            SvgWriteLine("    var bbox = interactivePanelInner.getBBox();");
            SvgWriteLine("    panelRect.setAttribute('width',bbox.width+32);");
            SvgWriteLine("    panelRect.setAttribute('height',textY+8);");
            SvgWriteLine("    panelRect.setAttribute('x',bbox.x-16);");
            SvgWriteLine("    panelRect.setAttribute('y',bbox.y-8);");

            float maxX = rect.x + rect.width;
            SvgWriteLine("    var panelOffset = 0;");
            SvgWriteLine("    if ( bbox.x + textX < 0 )");
            SvgWriteLine("    {");
            SvgWriteLine("        panelOffset = -(bbox.x + textX);");
            SvgWriteLine("    }");
            SvgWriteLine("    else if ( bbox.x+bbox.width + textX > " + maxX+" ) ");
            SvgWriteLine("    {    ");
            SvgWriteLine("        panelOffset = "+maxX+"-(bbox.x+bbox.width + textX);");
            SvgWriteLine("    }   ");
            SvgWriteLine("    var interactivePanelInnerWithRect = document.getElementById('interactivePanelInnerWithRect<UNIQUE>');");
            SvgWriteLine("    interactivePanelInnerWithRect.setAttribute('transform','translate('+panelOffset+',0)');");
            SvgWriteLine("  }");
            SvgWriteLine("  else");
            SvgWriteLine("  {");
            SvgWriteLine("    interactivePanel.setAttribute('visibility','hidden')");
            SvgWriteLine("    legendPanel.setAttribute('visibility','visible')");
            SvgWriteLine("  }");
            SvgWriteLine("}");
            SvgWriteLine("]]> </script>");

            DrawGraphArea(new Colour(0, 0, 0, 0.0f), rect, false, true);

        }

        void DrawMetadata(System.IO.StreamWriter svgFile, CsvMetadata metadata, Rect rect, Colour colour)
        {
            float y = rect.height - 15;

            string Platform = metadata.GetValue("platform","[Unknown platform]");
            string BuildConfiguration = metadata.GetValue("config", "[Unknown config]");
            string BuildVersion = metadata.GetValue("buildversion", "[Unknown version]");
            string SessionId = metadata.GetValue("sessionid", "");
            string PlaylistId = metadata.GetValue("playlistid", "");
            // If we have a device profile, write that out instead of the platform
            Platform = metadata.GetValue("deviceprofile", Platform);
            BuildVersion = metadata.GetValue("buildversion", BuildVersion);

            string Commandline = metadata.GetValue("commandline", "");
            SvgWriteLine("<text x='" + 10 + "' y='" + y + "' fill=" + colour.SVGString() + " font-size='9' font-family='Helvetica' >" + BuildConfiguration + " " + Platform + " " + BuildVersion + " " + SessionId + " " + PlaylistId + "</text>");
            y += 10;
            SvgWriteLine("<text x='" + 10 + "' y='" + y + "' fill=" + colour.SVGString() + " font-size='9' font-family='Courier New' >" + Commandline  + "</text>");
        }


		static CsvCache csvCache = null;

		static void Main(string[] args)
        {
			CommandLine commandline = new CommandLine(args);
			string batchCommandsFilename = commandline.GetArg("batchCommands");

			if (batchCommandsFilename.Length > 0)
			{
				// In batch mode, just output a list of commandlines
				string[] commandlines = System.IO.File.ReadAllLines(batchCommandsFilename);
				if (commandlines.Length > 1)
				{
					// Enable caching
					csvCache = new CsvCache();
				}
				int numThreads = commandline.GetIntArg("mt", 4);
				if (numThreads>1)
				{
					var result = Parallel.For(0, commandlines.Length, new ParallelOptions { MaxDegreeOfParallelism = numThreads }, i =>
					{
						Program program = new Program();
						program.Run(MakeArgsArray(commandlines[i]));
					});
				}
				else
				{
					foreach (string cmdLine in commandlines)
					{
						Program program = new Program();
						program.Run(MakeArgsArray(cmdLine));
					}
				}
			}
			else
			{
				Program program = new Program();
				try
				{
					program.Run(args);
				}
				catch (System.Exception e)
				{
					Console.WriteLine("[ERROR] " + e.Message);
					if (Debugger.IsAttached)
					{
						throw e;
					}
				}
			}
        }
     }
}
