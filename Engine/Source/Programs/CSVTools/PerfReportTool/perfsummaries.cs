// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Xml.Linq;
using System.IO;
using CSVStats;
using PerfReportTool;

namespace PerfSummaries
{

    class Colour
    {
		public Colour(string str)
		{
			string hexStr = str.TrimStart('#');
			int hexValue = Convert.ToInt32(hexStr, 16);
			byte rb = (byte)((hexValue >> 16) & 0xff);
			byte gb = (byte)((hexValue >> 8) & 0xff);
			byte bb = (byte)((hexValue >> 0) & 0xff);
			r = ((float)rb) / 255.0f;
			g = ((float)gb) / 255.0f;
			b = ((float)bb) / 255.0f;
			alpha = 1.0f;
		}
		public Colour(uint hex, float alphaIn = 1.0f)
        {
            byte rb = (byte)((hex >> 16) & 0xff);
            byte gb = (byte)((hex >> 8) & 0xff);
            byte bb = (byte)((hex >> 0) & 0xff);

            r = ((float)rb) / 255.0f;
            g = ((float)rb) / 255.0f;
            b = ((float)rb) / 255.0f;

            alpha = alphaIn;
        }
        public Colour(Colour colourIn) { r = colourIn.r; g = colourIn.g; b = colourIn.b; alpha = colourIn.alpha; }
        public Colour(float rIn, float gIn, float bIn, float aIn = 1.0f) { r = rIn; g = gIn; b = bIn; alpha = aIn; }

        public static Colour Lerp(Colour Colour0, Colour Colour1, float t)
        {
            return new Colour(
                Colour0.r * (1.0f - t) + Colour1.r * t,
                Colour0.g * (1.0f - t) + Colour1.g * t,
                Colour0.b * (1.0f - t) + Colour1.b * t,
                Colour0.alpha * (1.0f - t) + Colour1.alpha * t);

        }

        public string ToHTMLString()
        {
            int rI = (int)(r * 255.0f);
            int gI = (int)(g * 255.0f);
            int bI = (int)(b * 255.0f);
            int aI = (int)(alpha * 255.0f);
            return "'#" + rI.ToString("x2") + gI.ToString("x2") + bI.ToString("x2") /*+ aI.ToString("X")*/ + "'";
        }


        public static Colour White = new Colour(1.0f, 1.0f, 1.0f, 1.0f);
        public static Colour Black = new Colour(0, 0, 0, 1.0f);
        public static Colour Orange = new Colour(1.0f, 0.5f, 0.0f, 1.0f);
        public static Colour Red = new Colour(1.0f, 0.0f, 0.0f, 1.0f);
        public static Colour Green = new Colour(0.0f, 1.0f, 0.0f, 1.0f);

        public float r, g, b;
        public float alpha;
    };


	class ThresholdInfo
	{
		public ThresholdInfo(double inValue, Colour inColour)
		{
			value = inValue;
			colour = inColour;
		}
		public double value;
		public Colour colour;
	};

	class ColourThresholdList
	{
		public static string GetThresholdColour(double value, double redValue, double orangeValue, double yellowValue, double greenValue,
			Colour redOverride = null, Colour orangeOverride = null, Colour yellowOverride = null, Colour greenOverride = null)
		{
			Colour green = (greenOverride != null) ? greenOverride : new Colour(0.0f, 1.0f, 0.0f, 1.0f);
			Colour orange = (orangeOverride != null) ? orangeOverride : new Colour(1.0f, 0.5f, 0.0f, 1.0f);
			Colour yellow = (yellowOverride != null) ? yellowOverride : new Colour(1.0f, 1.0f, 0.0f, 1.0f);
			Colour red = (redOverride != null) ? redOverride : new Colour(1.0f, 0.0f, 0.0f, 1.0f);

			if (redValue > orangeValue)
			{
				redValue = -redValue;
				orangeValue = -orangeValue;
				yellowValue = -yellowValue;
				greenValue = -greenValue;
				value = -value;
			}

			Colour col = null;
			if (value <= redValue)
			{
				col = red;
			}
			else if (value <= orangeValue)
			{
				double t = (value - redValue) / (orangeValue - redValue);
				col = Colour.Lerp(red, orange, (float)t);
			}
			else if (value <= yellowValue)
			{
				double t = (value - orangeValue) / (yellowValue - orangeValue);
				col = Colour.Lerp(orange, yellow, (float)t);
			}
			else if (value <= greenValue)
			{
				float t = (float)(value - yellowValue) / (float)(greenValue - yellowValue);
				col = Colour.Lerp(yellow, green, t);
			}
			else
			{
				col = green;
			}
			return col.ToHTMLString();
		}

		public void Add(ThresholdInfo info)
		{
			if (Thresholds.Count < 4)
			{
				Thresholds.Add(info);
			}
		}
		public int Count
		{
			get { return Thresholds.Count; }
		}

		public string GetColourForValue(string value)
		{
			try
			{
				return GetColourForValue(Convert.ToDouble(value, System.Globalization.CultureInfo.InvariantCulture));
			}
			catch
			{
				return "'#ffffff'";
			}
		}

		public string GetColourForValue(double value)
		{
			if (Thresholds.Count == 4)
			{
				return GetThresholdColour(value, Thresholds[3].value, Thresholds[2].value, Thresholds[1].value, Thresholds[0].value, Thresholds[3].colour, Thresholds[2].colour, Thresholds[1].colour, Thresholds[0].colour);
			}
			return "'#ffffff'";
		}

		public static string GetSafeColourForValue(ColourThresholdList list, string value)
		{
			if (list == null)
			{
				return "'#ffffff'";
			}
			return list.GetColourForValue(value);
		}
		public List<ThresholdInfo> Thresholds = new List<ThresholdInfo>();
	};

	class TableUtil
	{
		public static string FormatStatName(string inStatName)
		{
			return inStatName.Replace("/", "/ ");
		}

		public static string SanitizeHtmlString(string str)
		{
			return str.Replace("<", "&lt;").Replace(">", "&gt;");
		}

		public static string SafeTruncateHtmlTableValue(string inValue, int maxLength)
		{
			if (inValue.StartsWith("<a") && inValue.EndsWith("</a>"))
			{
				// Links require special handling. Only truncate what's inside
				int openAnchorEndIndex = inValue.IndexOf(">");
				int closeAnchorStartIndex = inValue.IndexOf("</a>");
				if (openAnchorEndIndex > 2 && closeAnchorStartIndex > openAnchorEndIndex)
				{
					string anchor = inValue.Substring(0, openAnchorEndIndex + 1);
					string text = inValue.Substring(openAnchorEndIndex+1, closeAnchorStartIndex - (openAnchorEndIndex+1));
					if (text.Length>maxLength)
					{
						text = SanitizeHtmlString(text.Substring(0, maxLength)) + "...";
					}
					return anchor + text + "</a>";
				}
			}
			return SanitizeHtmlString(inValue.Substring(0, maxLength))+"...";
		}
	}

	class Summary
    {

		public class CaptureRange
        {
            public string name;
            public string startEvent;
            public string endEvent;
            public bool includeFirstFrame;
            public bool includeLastFrame;
            public CaptureRange(string inName, string start, string end)
            {
                name = inName;
                startEvent = start;
                endEvent = end;
                includeFirstFrame = false;
                includeLastFrame = false;
            }
        }
        public class CaptureData
        {
            public int startIndex;
            public int endIndex;
            public List<float> Frames;
            public CaptureData(int start, int end, List<float> inFrames)
            {
                startIndex = start;
                endIndex = end;
                Frames = inFrames;
            }
        }

        public Summary()
        {
            stats = new List<string>();
            captures = new List<CaptureRange>();
            StatThresholds = new Dictionary<string, ColourThresholdList>();

        }
        public virtual void WriteSummaryData(System.IO.StreamWriter htmlFile, CsvStats csvStats, bool bIncludeSummaryCsv, SummaryMetadata metadata, string htmlFileName)
        { }

        public virtual void PostInit(ReportTypeInfo reportTypeInfo, CsvStats csvStats)
        {
			// Resolve wildcards and remove duplicates
			stats = csvStats.GetStatNamesMatchingStringList(stats.ToArray());
		}

        public void ReadStatsFromXML(XElement element)
        {
			useUnstrippedCsvStats = element.GetSafeAttibute<bool>("useUnstrippedCsvStats", false);
			XElement statsElement = element.Element("stats");
			if (statsElement != null)
			{
				stats = statsElement.Value.Split(',').ToList();
			}
            foreach (XElement child in element.Elements())
            {
                if (child.Name == "capture")
                {
                    string captureName = child.Attribute("name").Value;
                    string captureStart = child.Attribute("startEvent").Value;
                    string captureEnd = child.Attribute("endEvent").Value;
                    bool incFirstFrame = Convert.ToBoolean(child.Attribute("includeFirstFrame").Value);
                    bool incLastFrame = Convert.ToBoolean(child.Attribute("includeLastFrame").Value);
                    CaptureRange newRange = new CaptureRange(captureName, captureStart, captureEnd);
                    newRange.includeFirstFrame = incFirstFrame;
                    newRange.includeLastFrame = incLastFrame;
                    captures.Add(newRange);
                }
                else if (child.Name == "colourThresholds")
                {
                    if (child.Attribute("stat") == null)
                    {
                        continue;
                    }
                    string statName = child.Attribute("stat").Value;
                    string[] hitchThresholdsStrList = child.Value.Split(',');
					ColourThresholdList HitchThresholds = new ColourThresholdList();
					for (int i = 0; i < hitchThresholdsStrList.Length; i++)
                    {
						string hitchThresholdStr = hitchThresholdsStrList[i];
						double thresholdValue = 0.0;
						string hitchThresholdNumStr = hitchThresholdStr;
						Colour thresholdColour = null;

						int openBracketIndex = hitchThresholdStr.IndexOf('(');
						if (openBracketIndex != -1 )
						{
							hitchThresholdNumStr = hitchThresholdStr.Substring(0, openBracketIndex);
							int closeBracketIndex = hitchThresholdStr.IndexOf(')');
							if (closeBracketIndex > openBracketIndex)
							{
								string colourString = hitchThresholdStr.Substring(openBracketIndex+1, closeBracketIndex - openBracketIndex-1);
								thresholdColour = new Colour(colourString);
							}
						}
						thresholdValue = Convert.ToDouble(hitchThresholdNumStr, System.Globalization.CultureInfo.InvariantCulture);

						HitchThresholds.Add(new ThresholdInfo(thresholdValue, thresholdColour));
                    }
                    if (HitchThresholds.Count == 4)
                    {
                        StatThresholds.Add(statName, HitchThresholds);
                    }
                }
            }
        }
        public CaptureData GetFramesForCapture(CaptureRange inCapture, List<float> FrameTimes, List<CsvEvent> EventsCaptured)
        {
            List<float> ReturnFrames = new List<float>();
            int startFrame = -1;
            int endFrame = FrameTimes.Count;
            for (int i = 0; i < EventsCaptured.Count; i++)
            {
                if (startFrame < 0 && EventsCaptured[i].Name.ToLower().Contains(inCapture.startEvent.ToLower()))
                {
                    startFrame = EventsCaptured[i].Frame;
                    if (!inCapture.includeFirstFrame)
                    {
                        startFrame++;
                    }
                }
                else if (endFrame >= FrameTimes.Count && EventsCaptured[i].Name.ToLower().Contains(inCapture.endEvent.ToLower()))
                {
                    endFrame = EventsCaptured[i].Frame;
                    if (!inCapture.includeLastFrame)
                    {
                        endFrame--;
                    }
                }
            }
            if (startFrame == -1 || endFrame == FrameTimes.Count || endFrame < startFrame)
            {
                return null;
            }
            ReturnFrames = FrameTimes.GetRange(startFrame, (endFrame - startFrame));
            CaptureData CaptureToUse = new CaptureData(startFrame, endFrame, ReturnFrames);
            return CaptureToUse;
        }

        public string[] GetUniqueStatNames()
        {
            HashSet<string> uniqueStats = new HashSet<string>();
            foreach (string stat in stats)
            {
                if (!uniqueStats.Contains(stat))
                {
                    uniqueStats.Add(stat);
                }
            }
            return uniqueStats.ToArray();
        }

		protected double [] ReadColourThresholdsXML(XElement colourThresholdEl)
		{
			if (colourThresholdEl != null)
			{
				string[] colourStrings = colourThresholdEl.Value.Split(',');
				if (colourStrings.Length != 4)
				{
					throw new Exception("Incorrect number of colourthreshold entries. Should be 4.");
				}
				double [] colourThresholds = new double[4];
				for (int i = 0; i < colourStrings.Length; i++)
				{
					colourThresholds[i] = Convert.ToDouble(colourStrings[i], System.Globalization.CultureInfo.InvariantCulture);
				}
				return colourThresholds;
			}
			return null;
		}

        public string GetStatThresholdColour(string StatToUse, double value)
        {
			ColourThresholdList Thresholds = GetStatColourThresholdList(StatToUse);
			if (Thresholds != null)
            {
				return Thresholds.GetColourForValue(value);
            }
			return "'#ffffff'";
        }

		public ColourThresholdList GetStatColourThresholdList(string StatToUse)
		{
			if (StatThresholds.ContainsKey(StatToUse))
			{
				return StatThresholds[StatToUse];
			}
			return null;
		}

        public List<CaptureRange> captures;
        public List<string> stats;
        public Dictionary<string, ColourThresholdList> StatThresholds;
		public bool useUnstrippedCsvStats;
    };

    class FPSChartSummary : Summary
    {
        public FPSChartSummary(XElement element, string baseXmlDirectory)
        {
            ReadStatsFromXML(element);
            fps = Convert.ToInt32(element.Attribute("fps").Value);
            hitchThreshold = (float)Convert.ToDouble(element.Attribute("hitchThreshold").Value, System.Globalization.CultureInfo.InvariantCulture);
			bUseEngineHitchMetric = element.GetSafeAttibute<bool>("useEngineHitchMetric", false);
			if (bUseEngineHitchMetric)
			{
				engineHitchToNonHitchRatio = element.GetSafeAttibute<float>("engineHitchToNonHitchRatio", 1.5f);
				engineMinTimeBetweenHitchesMs = element.GetSafeAttibute<float>("engineMinTimeBetweenHitchesMs", 200.0f);
			}

			bIgnoreHitchTimePercent = element.GetSafeAttibute<bool>("ignoreHitchTimePercent", false);
			bIgnoreMVP = element.GetSafeAttibute<bool>("ignoreMVP", false);
		}

		float GetEngineHitchToNonHitchRatio()
		{
			float MinimumRatio = 1.0f;
			float targetFrameTime = 1000.0f / fps;
			float MaximumRatio = hitchThreshold / targetFrameTime;

			return Math.Min( Math.Max(engineHitchToNonHitchRatio, MinimumRatio), MaximumRatio );
		}

		struct FpsChartData
		{
			public float MVP;
			public float HitchesPerMinute;
			public float HitchTimePercent;
			public int HitchCount;
			public float TotalTimeSeconds;
		};

		FpsChartData ComputeFPSChartDataForFrames(List<float> frameTimes, bool skiplastFrame)
		{
			double totalFrametime = 0.0;
			int hitchCount = 0;
			double totalHitchTime = 0.0;

			int frameCount = skiplastFrame ? frameTimes.Count - 1 : frameTimes.Count;

			// Count hitches
			if (bUseEngineHitchMetric)
			{
				// Minimum time passed before we'll record a new hitch
				double CurrentTime = 0.0;
				double LastHitchTime = float.MinValue;
				double LastFrameTime = float.MinValue;
				float HitchMultiplierAmount = GetEngineHitchToNonHitchRatio();

				for ( int i=0; i< frameCount; i++)
				{
					float frametime = frameTimes[i];
					// How long has it been since the last hitch we detected?
					if (frametime >= hitchThreshold)
					{
						double TimeSinceLastHitch = (CurrentTime - LastHitchTime);
						if (TimeSinceLastHitch >= engineMinTimeBetweenHitchesMs)
						{
							// For the current frame to be considered a hitch, it must have run at least this many times slower than
							// the previous frame

							// If our frame time is much larger than our last frame time, we'll count this as a hitch!
							if (frametime > (LastFrameTime * HitchMultiplierAmount))
							{
								LastHitchTime = CurrentTime;
								hitchCount++;
							}
						}
						totalHitchTime += frametime;
					}
					LastFrameTime = frametime;
					CurrentTime += (double)frametime;
				}
				totalFrametime = CurrentTime;
			}
			else
			{
				for (int i = 0; i < frameCount; i++)
				{
					float frametime = frameTimes[i];
					totalFrametime += frametime;
					if (frametime >= hitchThreshold)
					{
						hitchCount++;
					}
				}
			}
			float TotalSeconds = (float)totalFrametime / 1000.0f;
			float TotalMinutes = TotalSeconds / 60.0f;

			FpsChartData outData = new FpsChartData();
			outData.HitchCount = hitchCount;
			outData.TotalTimeSeconds = TotalSeconds;
			outData.HitchesPerMinute = (float)hitchCount / TotalMinutes;
			outData.HitchTimePercent = (float)(totalHitchTime / totalFrametime) * 100.0f;

			int TotalTargetFrames = (int)((double)fps * (TotalSeconds));
			int MissedFrames = Math.Max(TotalTargetFrames - frameTimes.Count, 0);
			outData.MVP = (((float)MissedFrames * 100.0f) / (float)TotalTargetFrames);
			return outData;
		}


		public override void WriteSummaryData(System.IO.StreamWriter htmlFile, CsvStats csvStats, bool bIncludeSummaryCsv, SummaryMetadata metadata, string htmlFileName)
        {
            System.IO.StreamWriter statsCsvFile = null;
            if (bIncludeSummaryCsv)
            {
                string csvPath = Path.Combine(Path.GetDirectoryName(htmlFileName), "FrameStats_colored.csv");
                statsCsvFile = new System.IO.StreamWriter(csvPath, false);
            }
			// Compute MVP30 and MVP60. Note: we ignore the last frame because fpscharts can hitch
            List<float> frameTimes = csvStats.Stats["frametime"].samples;
			FpsChartData fpsChartData = ComputeFPSChartDataForFrames(frameTimes,true);

            // Write the averages
            List<string> ColumnNames = new List<string>();
            List<string> ColumnValues = new List<string>();
			List<string> ColumnColors = new List<string>();
			List<ColourThresholdList> ColumnColorThresholds = new List<ColourThresholdList>();

            ColumnNames.Add("Total Time (s)");
			ColumnColorThresholds.Add(new ColourThresholdList());
            ColumnValues.Add(fpsChartData.TotalTimeSeconds.ToString("0.0"));
			ColumnColors.Add(ColourThresholdList.GetSafeColourForValue( ColumnColorThresholds.Last(), ColumnValues.Last()));

			ColumnNames.Add("Hitches/Min");
			ColumnColorThresholds.Add(GetStatColourThresholdList(ColumnNames.Last()));
			ColumnValues.Add(fpsChartData.HitchesPerMinute.ToString("0.00"));
			ColumnColors.Add(ColourThresholdList.GetSafeColourForValue(ColumnColorThresholds.Last(), ColumnValues.Last()));

			if (!bIgnoreHitchTimePercent)
			{
				ColumnNames.Add("HitchTimePercent");
				ColumnColorThresholds.Add(GetStatColourThresholdList(ColumnNames.Last()));
				ColumnValues.Add(fpsChartData.HitchTimePercent.ToString("0.00"));
				ColumnColors.Add(ColourThresholdList.GetSafeColourForValue(ColumnColorThresholds.Last(), ColumnValues.Last()));
			}

			if (!bIgnoreMVP)
			{
				ColumnNames.Add("MVP" + fps.ToString());
				ColumnColorThresholds.Add(GetStatColourThresholdList(ColumnNames.Last()));
				ColumnValues.Add(fpsChartData.MVP.ToString("0.00"));
				ColumnColors.Add(ColourThresholdList.GetSafeColourForValue(ColumnColorThresholds.Last(), ColumnValues.Last()));
			}

			List<bool> ColumnIsAvgValueList = new List<bool>();
			for ( int i=0; i<ColumnNames.Count; i++)
			{
				ColumnIsAvgValueList.Add(false);
			}
			foreach (string statName in stats)
            {
                string[] StatTokens = statName.Split('(');

                float value = 0;
                string ValueType = " Avg";
				bool bIsAvg = false;
                if (!csvStats.Stats.ContainsKey(StatTokens[0].ToLower()))
                {
                    continue;
                }
                if (StatTokens.Length > 1 && StatTokens[1].ToLower().Contains("min"))
                {
                    value = csvStats.Stats[StatTokens[0].ToLower()].ComputeMinValue();
					ValueType = " Min";
                }
                else if (StatTokens.Length > 1 && StatTokens[1].ToLower().Contains("max"))
                {
                    value = csvStats.Stats[StatTokens[0].ToLower()].ComputeMaxValue();
                    ValueType = " Max";
                }
                else
                {
                    value = csvStats.Stats[StatTokens[0].ToLower()].average;
					bIsAvg = true;
                }
				ColumnIsAvgValueList.Add(bIsAvg);
				ColumnNames.Add(StatTokens[0] + ValueType);
                ColumnValues.Add(value.ToString("0.00"));
				ColumnColorThresholds.Add(GetStatColourThresholdList(statName));
				ColumnColors.Add(ColourThresholdList.GetSafeColourForValue(ColumnColorThresholds.Last(), ColumnValues.Last()));
			}

			// Output metadata
			if (metadata != null)
            {
                for (int i = 0; i < ColumnNames.Count; i++)
                {
                    string columnName = ColumnNames[i];

                    // Output simply MVP to metadata instead of MVP30 etc
                    if ( columnName.StartsWith("MVP"))
                    {
                        columnName = "MVP";
                    }
					// Hide pre-existing stats with the same name
					if (ColumnIsAvgValueList[i] && columnName.EndsWith(" Avg"))
					{
						string originalStatName = columnName.Substring(0, columnName.Length - 4).ToLower();
						SummaryMetadataValue smv;
						if ( metadata.dict.TryGetValue(originalStatName, out smv) )
						{
							if (smv.type == SummaryMetadataValue.Type.CsvStatAverage)
							{
								smv.SetFlag(SummaryMetadataValue.Flags.Hidden, true);
							}
						}
					}
					metadata.Add(SummaryMetadataValue.Type.SummaryTableMetric, columnName, ColumnValues[i], ColumnColorThresholds[i]);
                }
                metadata.Add(SummaryMetadataValue.Type.SummaryTableMetric, "TargetFPS", fps.ToString());
            }

            // Output HTML
			if ( htmlFile != null )
            {
                string HeaderRow = "";
                string ValueRow = "";
                HeaderRow += "<th>Section Name</th>";
                ValueRow += "<td>Entire Run</td>";
                for (int i = 0; i < ColumnNames.Count; i++)
                {
                    string columnName = ColumnNames[i];
                    if (columnName.ToLower().EndsWith("time"))
                    {
                        columnName += " (ms)";
                    }
                    HeaderRow += "<th>" + TableUtil.FormatStatName(columnName) + "</th>";
                    ValueRow += "<td bgcolor=" + ColumnColors[i] + ">" + ColumnValues[i] + "</td>";
                }
				htmlFile.WriteLine("  <h2>FPSChart</h2>");
				htmlFile.WriteLine("<table border='0' style='width:400'>");
                htmlFile.WriteLine("  <tr>" + HeaderRow + "</tr>");
                htmlFile.WriteLine("  <tr>" + ValueRow + "</tr>");
            }

            // Output CSV
            if (statsCsvFile != null)
            {
                statsCsvFile.Write("Section Name,");
                statsCsvFile.WriteLine(string.Join(",", ColumnNames));

                statsCsvFile.Write("Entire Run,");
                statsCsvFile.WriteLine(string.Join(",", ColumnValues));

                // Pass through color data as part of database-friendly stuff.
                statsCsvFile.Write("Entire Run BGColors,");
				statsCsvFile.WriteLine(string.Join(",", ColumnColors));
            }

            if (csvStats.Events.Count > 0)
            {
                // Per-event breakdown
                foreach (CaptureRange CapRange in captures)
                {
                    ColumnValues.Clear();
                    ColumnColors.Clear();
                    CaptureData CaptureFrameTimes = GetFramesForCapture(CapRange, frameTimes, csvStats.Events);

                    if (CaptureFrameTimes == null)
                    {
                        continue;
                    }
					FpsChartData captureFpsChartData = ComputeFPSChartDataForFrames(CaptureFrameTimes.Frames,true);
				
                    if (captureFpsChartData.TotalTimeSeconds == 0.0f)
                    {
                        continue;
                    }

                    ColumnValues.Add(captureFpsChartData.TotalTimeSeconds.ToString("0.0"));
                    ColumnColors.Add("\'#ffffff\'");

                    ColumnValues.Add(captureFpsChartData.HitchesPerMinute.ToString("0.00"));
                    ColumnColors.Add(GetStatThresholdColour("Hitches/Min", captureFpsChartData.HitchesPerMinute));

					if (!bIgnoreHitchTimePercent)
					{
						ColumnValues.Add(captureFpsChartData.HitchTimePercent.ToString("0.00"));
						ColumnColors.Add(GetStatThresholdColour("HitchTimePercent", captureFpsChartData.HitchTimePercent));
					}

					if (!bIgnoreMVP)
					{
						ColumnValues.Add(captureFpsChartData.MVP.ToString("0.00"));
						ColumnColors.Add(GetStatThresholdColour("MVP" + fps.ToString(), captureFpsChartData.MVP));
					}

                    foreach (string statName in stats)
                    {
                        string StatToCheck = statName.Split('(')[0];
                        if (!csvStats.Stats.ContainsKey(StatToCheck.ToLower()))
                        {
                            continue;
                        }

                        string[] StatTokens = statName.Split('(');

                        float value = 0;
                        if (StatTokens.Length > 1 && StatTokens[1].ToLower().Contains("min"))
                        {
                            value = csvStats.Stats[StatTokens[0].ToLower()].ComputeMinValue(CaptureFrameTimes.startIndex, CaptureFrameTimes.endIndex);
						}
						else if (StatTokens.Length > 1 && StatTokens[1].ToLower().Contains("max"))
                        {
                            value = csvStats.Stats[StatTokens[0].ToLower()].ComputeMaxValue(CaptureFrameTimes.startIndex, CaptureFrameTimes.endIndex);
                        }
                        else
                        {
                            value = csvStats.Stats[StatTokens[0].ToLower()].ComputeAverage(CaptureFrameTimes.startIndex, CaptureFrameTimes.endIndex);
                        }

                        ColumnValues.Add(value.ToString("0.00"));
                        ColumnColors.Add(GetStatThresholdColour(statName, value));
                    }

                    // Output HTML
					if ( htmlFile != null )
                    {
                        string ValueRow = "";
                        ValueRow += "<td>"+ CapRange.name + "</td>";
                        for (int i = 0; i < ColumnNames.Count; i++)
                        {
                            ValueRow += "<td bgcolor=" + ColumnColors[i] + ">" + ColumnValues[i] + "</td>";
                        }
                        htmlFile.WriteLine("  <tr>" + ValueRow + "</tr>");
					}

					// Output CSV
					if (statsCsvFile != null)
                    {
                        statsCsvFile.Write(CapRange.name+",");
                        statsCsvFile.WriteLine(string.Join(",", ColumnValues));

                        // Pass through color data as part of database-friendly stuff.
                        statsCsvFile.Write(CapRange.name + " colors,");
                        statsCsvFile.WriteLine(string.Join(",", ColumnColors));
                    }
                }
            }

			if (htmlFile != null)
			{
				htmlFile.WriteLine("</table>");
				htmlFile.WriteLine("<p style='font-size:8'>Engine hitch metric: " + (bUseEngineHitchMetric ? "enabled" : "disabled") + "</p>");
			}

			if (statsCsvFile != null)
            {
                statsCsvFile.Close();
            }
        }
        public override void PostInit(ReportTypeInfo reportTypeInfo, CsvStats csvStats)
        {
        }

        int fps;
        float hitchThreshold;
		bool bUseEngineHitchMetric;
		bool bIgnoreHitchTimePercent;
		bool bIgnoreMVP;
		float engineHitchToNonHitchRatio;
		float engineMinTimeBetweenHitchesMs;
	};

    class EventSummary : Summary
    {
        public EventSummary(XElement element, string baseXmlDirectory)
        {
            title = element.GetSafeAttibute("title","Events");
            metadataKey = element.Attribute("metadataKey").Value;
            events = element.Element("events").Value.Split(',');
			colourThresholds = ReadColourThresholdsXML(element.Element("colourThresholds"));
        }

        public override void WriteSummaryData(System.IO.StreamWriter htmlFile, CsvStats csvStats, bool bIncludeSummaryCsv, SummaryMetadata metadata, string htmlFileName)
        {
            Dictionary<string, int> eventCountsDict = new Dictionary<string, int>();
            int eventCount= 0;
            foreach (CsvEvent ev in csvStats.Events)
            {
                foreach (string eventName in events)
                {
                    if (CsvStats.DoesSearchStringMatch(ev.Name, eventName))
                    {
						int len = eventName.Length;
						if ( eventName.EndsWith("*"))
						{
							len--;
						}
						string eventContent = ev.Name.Substring(len).Trim();
                        if ( eventCountsDict.ContainsKey(eventContent))
                        {
                            eventCountsDict[eventContent]++;
                        }
                        else
                        {
                            eventCountsDict.Add(eventContent, 1);
                        }
                        eventCount++;
                    }
                }
            }
             
            // Output HTML
            if (htmlFile != null && eventCountsDict.Count > 0)
            {
                htmlFile.WriteLine("  <h2>" + title + "</h2>");
                htmlFile.WriteLine("  <table border='0' style='width:1200'>");
                htmlFile.WriteLine("  <tr><th>Name</th><th><b>Count</th></tr>");
                foreach (KeyValuePair<string,int> pair in eventCountsDict.ToList() )
                {
                    htmlFile.WriteLine("  <tr><td>"+pair.Key+"</td><td>"+pair.Value+"</td></tr>");
                }
                htmlFile.WriteLine("  </table>");
            }

            // Output metadata
            if (metadata != null)
            {

                ColourThresholdList thresholdList = null;

                if (colourThresholds != null)
                {
                    thresholdList = new ColourThresholdList();
                    for (int i = 0; i < colourThresholds.Length; i++)
                    {
                        thresholdList.Add(new ThresholdInfo(colourThresholds[i], null));
                    }
                }
                metadata.Add(SummaryMetadataValue.Type.SummaryTableMetric, metadataKey, eventCount.ToString(), thresholdList);
            }
        }
        public override void PostInit(ReportTypeInfo reportTypeInfo, CsvStats csvStats)
        {
        }
        string[] events;
        double[] colourThresholds;
        string title;
        string metadataKey;
    };

    class HitchSummary : Summary
    {
        public HitchSummary(XElement element, string baseXmlDirectory)
        {
            ReadStatsFromXML(element);

            string[] hitchThresholds = element.Element("hitchThresholds").Value.Split(',');
            HitchThresholds = new double[hitchThresholds.Length];
            for (int i = 0; i < hitchThresholds.Length; i++)
            {
                HitchThresholds[i] = Convert.ToDouble(hitchThresholds[i], System.Globalization.CultureInfo.InvariantCulture);
            }
        }

        public override void WriteSummaryData(System.IO.StreamWriter htmlFile, CsvStats csvStats, bool bIncludeSummaryCsv, SummaryMetadata metadata, string htmlFileName)
        {
			// Only HTML reporting is supported (does not output metadata)
			if (htmlFile == null)
			{
				return;
			}

			htmlFile.WriteLine("  <h2>Hitches</h2>");
            htmlFile.WriteLine("  <table border='0' style='width:800'>");
            htmlFile.WriteLine("  <tr><td></td>");

            StreamWriter statsCsvFile = null;
            if (bIncludeSummaryCsv)
            {
                string csvPath = Path.Combine(Path.GetDirectoryName(htmlFileName), "HitchStats.csv");
                statsCsvFile = new System.IO.StreamWriter(csvPath, false);
            }

            List<string> Thresholds = new List<string>();
            List<string> Hitches = new List<string>();
            Thresholds.Add("Hitch Size");
            foreach (float thresh in HitchThresholds)
            {
                htmlFile.WriteLine("  <th> >" + thresh.ToString("0") + "ms</b></td>");
                Thresholds.Add(thresh.ToString("0"));
            }
            if (statsCsvFile != null)
            {
                statsCsvFile.WriteLine(string.Join(",", Thresholds));
            }
            htmlFile.WriteLine("  </tr>");

            foreach (string unitStat in stats)
            {
                string StatToCheck = unitStat.Split('(')[0];
				StatSamples statSample = csvStats.GetStat(StatToCheck.ToLower());
				if (statSample == null)
				{
					continue;
				}

				Hitches.Clear();
				htmlFile.WriteLine("  <tr><td><b>" + StatToCheck + "</b></td>");
                Hitches.Add(StatToCheck);
                int thresholdIndex = 0;

                foreach (float threshold in HitchThresholds)
                {				
					float count = (float)statSample.GetCountOfFramesOverBudget(threshold);
                    int numSamples = csvStats.GetStat(StatToCheck.ToLower()).GetNumSamples();
                    // if we have 20k frames in a typical flythrough then 20 frames would be red
                    float redThresholdFor50ms = (float)numSamples / 500.0f;
                    float redThreshold = (redThresholdFor50ms * 50.0f) / threshold; // Adjust the colour threshold based on the current threshold
                    string colour = ColourThresholdList.GetThresholdColour(count, redThreshold, redThreshold * 0.66, redThreshold * 0.33, 0.0f);
                    htmlFile.WriteLine("  <td bgcolor=" + colour + ">" + count.ToString("0") + "</td>");
                    Hitches.Add(count.ToString("0"));
                    thresholdIndex++;
                }
                if (statsCsvFile != null)
                {
                    statsCsvFile.WriteLine(string.Join(",", Hitches));
                }

                htmlFile.WriteLine("  </tr>");
            }
            if (statsCsvFile != null)
            {
                statsCsvFile.Close();
            }
            htmlFile.WriteLine("  </table>");
			htmlFile.WriteLine("<p style='font-size:8'>Note: Simplified hitch metric. All frames over threshold are counted" + "</p>");
        }
        public double[] HitchThresholds;
    };

    class HistogramSummary : Summary
    {
        public HistogramSummary(XElement element, string baseXmlDirectory)
        {
            ReadStatsFromXML(element);

			ColourThresholds = ReadColourThresholdsXML(element.Element("colourThresholds"));

            string[] histogramStrings = element.Element("histogramThresholds").Value.Split(',');
            HistogramThresholds = new double[histogramStrings.Length];
            for (int i = 0; i < histogramStrings.Length; i++)
            {
                HistogramThresholds[i] = Convert.ToDouble(histogramStrings[i], System.Globalization.CultureInfo.InvariantCulture);
            }

            string[] hitchThresholds = element.Element("hitchThresholds").Value.Split(',');
            HitchThresholds = new double[hitchThresholds.Length];
            for (int i = 0; i < hitchThresholds.Length; i++)
            {
                HitchThresholds[i] = Convert.ToDouble(hitchThresholds[i], System.Globalization.CultureInfo.InvariantCulture);
            }

            foreach (XElement child in element.Elements())
            {
                if (child.Name == "budgetOverride")
                {
                    BudgetOverrideStatName = child.Attribute("stat").Value;
                    BudgetOverrideStatBudget = Convert.ToDouble(child.Attribute("budget").Value, System.Globalization.CultureInfo.InvariantCulture);
                }
            }
        }

        public override void WriteSummaryData(System.IO.StreamWriter htmlFile, CsvStats csvStats, bool bIncludeSummaryCsv, SummaryMetadata metadata, string htmlFileName)
        {
			// Only HTML reporting is supported (does not output metadata)
			if (htmlFile == null)
			{
				return;
			}
			// Write the averages
			htmlFile.WriteLine("  <h2>Stat unit averages</h2>");
            htmlFile.WriteLine("  <table border='0' style='width:400'>");
            htmlFile.WriteLine("  <tr><td></td><th>ms</b></th>");
            foreach (string stat in stats)
            {
                string StatToCheck = stat.Split('(')[0];
                if (!csvStats.Stats.ContainsKey(StatToCheck.ToLower()))
                {
                    continue;
                }
                float val = csvStats.Stats[StatToCheck.ToLower()].average;
                string colour = ColourThresholdList.GetThresholdColour(val, ColourThresholds[0], ColourThresholds[1], ColourThresholds[2], ColourThresholds[3]);
                htmlFile.WriteLine("  <tr><td><b>" + StatToCheck + "</b></td><td bgcolor=" + colour + ">" + val.ToString("0.00") + "</td></tr>");
            }
            htmlFile.WriteLine("  </table>");

            // Hitches
            double[] thresholds = HistogramThresholds;
            htmlFile.WriteLine("  <h2>Frames in budget</h2>");
            htmlFile.WriteLine("  <table border='0' style='width:800'>");

            htmlFile.WriteLine("  <tr><td></td>");

            // Display the override stat budget first
            bool HasBudgetOverrideStat = false;
            if (BudgetOverrideStatName != null)
            {
                htmlFile.WriteLine("  <td><b><=" + BudgetOverrideStatBudget.ToString("0") + "ms</b></td>");
                HasBudgetOverrideStat = true;
            }

            foreach (float thresh in thresholds)
            {
                htmlFile.WriteLine("  <td><b><=" + thresh.ToString("0") + "ms</b></td>");
            }
            htmlFile.WriteLine("  </tr>");

            foreach (string unitStat in stats)
            {
                string StatToCheck = unitStat.Split('(')[0];

                htmlFile.WriteLine("  <tr><td><b>" + StatToCheck + "</b></td>");
                int thresholdIndex = 0;

                // Display the render thread budget column (don't display the other stats)
                if (HasBudgetOverrideStat)
                {
                    if (StatToCheck.ToLower() == BudgetOverrideStatName)
                    {
                        float pc = csvStats.GetStat(StatToCheck.ToLower()).GetRatioOfFramesInBudget((float)BudgetOverrideStatBudget) * 100.0f;
                        string colour = ColourThresholdList.GetThresholdColour(pc, 50.0f, 65.0f, 80.0f, 100.0f);
                        htmlFile.WriteLine("  <td bgcolor=" + colour + ">" + pc.ToString("0.00") + "%</td>");
                    }
                    else
                    {
                        htmlFile.WriteLine("  <td></td>");
                    }
                }

                foreach (float thresh in thresholds)
                {
                    float threshold = (float)thresholds[thresholdIndex];
                    float pc = csvStats.GetStat(StatToCheck.ToLower()).GetRatioOfFramesInBudget(threshold) * 100.0f;
                    string colour = ColourThresholdList.GetThresholdColour(pc, 50.0f, 65.0f, 80.0f, 100.0f);
                    htmlFile.WriteLine("  <td bgcolor=" + colour + ">" + pc.ToString("0.00") + "%</td>");
                    thresholdIndex++;
                }
                htmlFile.WriteLine("  </tr>");
            }
            htmlFile.WriteLine("  </table>");


            // Hitches
            htmlFile.WriteLine("  <h2>Hitches - Overall</h2>");
            htmlFile.WriteLine("  <table border='0' style='width:800'>");
            htmlFile.WriteLine("  <tr><td></td>");

            foreach (float thresh in HitchThresholds)
            {
                htmlFile.WriteLine("  <td><b> >" + thresh.ToString("0") + "ms</b></td>");
            }

            htmlFile.WriteLine("  </tr>");

            foreach (string unitStat in stats)
            {
                string StatToCheck = unitStat.Split('(')[0];
                htmlFile.WriteLine("  <tr><td><b>" + unitStat + "</b></td>");
                int thresholdIndex = 0;

                foreach (float threshold in HitchThresholds)
                {
                    float count = (float)csvStats.GetStat(StatToCheck.ToLower()).GetCountOfFramesOverBudget(threshold);
                    int numSamples = csvStats.GetStat(StatToCheck.ToLower()).GetNumSamples();
                    // if we have 20k frames in a typical flythrough then 20 frames would be red
                    float redThresholdFor50ms = (float)numSamples / 500.0f;
                    float redThreshold = (redThresholdFor50ms * 50.0f) / threshold; // Adjust the colour threshold based on the current threshold
                    string colour = ColourThresholdList.GetThresholdColour(count, redThreshold, redThreshold * 0.66, redThreshold * 0.33, 0.0f);
                    htmlFile.WriteLine("  <td bgcolor=" + colour + ">" + count.ToString("0") + "</td>");
                    thresholdIndex++;
                }
                htmlFile.WriteLine("  </tr>");
            }
            htmlFile.WriteLine("  </table>");

        }

        public double[] ColourThresholds;
        public double[] HistogramThresholds;
        public double[] HitchThresholds;
        public string BudgetOverrideStatName;
        public double BudgetOverrideStatBudget;
    };


    class PeakSummary : Summary
    {
        public PeakSummary(XElement element, string baseXmlDirectory)
        {
			hidePrefixes = new List<string>();
			sectionPrefixes = new List<string>();

			//read the child elements (mostly for colourThresholds)
			ReadStatsFromXML(element);

            foreach (XElement child in element.Elements())
            {
                if (child.Name == "hidePrefix")
                {
                    hidePrefixes.Add(child.Value.ToLower());
                }
                else if (child.Name == "sectionPrefix")
                {
                    sectionPrefixes.Add(child.Value.ToLower());
                }
            }
        }

        void WriteStatsToHTML(StreamWriter htmlFile, CsvStats csvStats, bool isMainSummary, string sectionPrefix, string htmlFileName, bool bIncludeSummaryCsv )
        {
            // Write the averages
            // We are splitting the peaks summary into several different tables based on the hidePrefix and whether the
            // stat should be in the main summary.
            bool bSummaryStatsFound = false;


            for (int j = 0; j < stats.Count; j++)
            {
				PeakStatInfo statInfo = getOrAddStatInfo(stats[j]);

				if (statInfo.isInMainSummary == isMainSummary)
                {
                    bSummaryStatsFound = true;
                    break;
                }
            }

            if (!bSummaryStatsFound && !bIncludeSummaryCsv)
            {
                return;
            }

			StreamWriter LLMCsvData = null;
			if (bIncludeSummaryCsv)
			{
				string LLMCsvPath = Path.Combine(Path.GetDirectoryName(htmlFileName), "LLMStats_colored.csv");
				LLMCsvData = new StreamWriter(LLMCsvPath);
			}
            List<string> StatValues = new List<string>();
            List<string> ColumnColors = new List<string>();

            // Here we are deciding which title we have and write it to the file.
            String titlePrefix = isMainSummary ? "Main Peaks" : sectionPrefix + " Peaks";
            htmlFile.WriteLine("<h2>" + titlePrefix + "</h2>");
            htmlFile.WriteLine("  <table border='0' style='width:400'>");

            //Hard-coded start of the table.
            htmlFile.WriteLine("    <tr><td style='width:200'></td><td style='width:75'><b>Average</b></td><td style='width:75'><b>Peak</b></td><td style='width:75'><b>Budget</b></td></tr>");
			if (LLMCsvData != null)
			{
				LLMCsvData.WriteLine("Stat,Average,Peak,Budget");
			}
            int i = 0;

            // Then for each stat we have registered in peak summary using AddStat, we
            // decide whether it should be in the current table based on its hidePrefix and
            // whether it's in the main summary.
            foreach (string stat in stats)
            {
                string statName = stat.Split('(')[0];

				PeakStatInfo statInfo = getOrAddStatInfo(stat);

				if ((csvStats.Stats.ContainsKey(statName.ToLower())) && // If the main stats table contains this stat AND
                     (sectionPrefix == null || statName.StartsWith(sectionPrefix, true, null)) // If there is no hide prefix just display the stat, otherwise, make sure they match.
                   )
                {
                    // Do the calculations for the averages and peak, and then write it to the table along with the budget.
                    StatSamples csvStat = csvStats.Stats[statName.ToLower()];
                    double peak = (double)csvStat.ComputeMaxValue() * statInfo.multiplier;
                    double average = (double)csvStat.average * statInfo.multiplier;
                    double budget = statInfo.budget;

                    float redValue = (float)budget * 1.5f;
                    float orangeValue = (float)budget * 1.25f;
                    float yellowValue = (float)budget * 1.0f;
                    float greenValue = (float)budget * 0.9f;
                    string peakColour = ColourThresholdList.GetThresholdColour(peak, redValue, orangeValue, yellowValue, greenValue);
                    string averageColour = ColourThresholdList.GetThresholdColour(average, redValue, orangeValue, yellowValue, greenValue);
                    string cleanStatName = stats[i].Replace('/', ' ').Replace("$32$", " ");

					if (statInfo.isInMainSummary == isMainSummary) // If we are in the main summary then this stat appears ONLY in the main summary
					{
						htmlFile.WriteLine("    <tr><td>" + cleanStatName + "</td><td bgcolor=" + averageColour + ">" + average.ToString("0") + "</td><td bgcolor=" + peakColour + ">" + peak.ToString("0") + "</td><td>" + budget.ToString("0") + "</td></tr>");
					}

					if (LLMCsvData != null)
					{
						LLMCsvData.WriteLine(string.Format("{0},{1},{2},{3}", cleanStatName, average.ToString("0"), peak.ToString("0"), budget.ToString("0"), averageColour, peakColour));
						// Pass through color data as part of database-friendly stuff.
						LLMCsvData.WriteLine(string.Format("{0}_Colors,{1},{2},#aaaaaa", cleanStatName, averageColour, peakColour));
					}
                }
                i++;
            }
			if (LLMCsvData != null)
			{
				LLMCsvData.Close();
			}
            htmlFile.WriteLine("  </table>");
        }

        public override void WriteSummaryData(System.IO.StreamWriter htmlFile, CsvStats csvStats, bool bIncludeSummaryCsv, SummaryMetadata metadata, string htmlFileName)
        {
			// Only HTML reporting is supported (does not output metadata)
			if (htmlFile == null)
			{
				return;
			}

			//update metadata
			if (metadata != null)
			{
				foreach (string statName in stats)
				{
					if (!csvStats.Stats.ContainsKey(statName.ToLower()))
					{
						continue;
					}

					var statValue = csvStats.Stats[statName.ToLower()];
					metadata.Add(SummaryMetadataValue.Type.SummaryTableMetric, statName + " Avg", statValue.average.ToString("0.00"), GetStatColourThresholdList(statName));
					metadata.Add(SummaryMetadataValue.Type.SummaryTableMetric, statName + " Max", statValue.ComputeMaxValue().ToString("0.00"), GetStatColourThresholdList(statName));
					metadata.Add(SummaryMetadataValue.Type.SummaryTableMetric, statName + " Min", statValue.ComputeMinValue().ToString("0.00"), GetStatColourThresholdList(statName));
				}
			}

			// The first thing we always write is the main summary.
			WriteStatsToHTML(htmlFile, csvStats, true, null, htmlFileName, bIncludeSummaryCsv);

            // Then we loop through all of the hide prefixes and write their individual table.
            // However, we have to make sure we at least have the empty string in the array to print out the whole list.
            if (sectionPrefixes.Count() == 0) { sectionPrefixes.Add(""); }
            int i = 0;
            for (i = 0; i < sectionPrefixes.Count(); i++)
            {
                string currentPrefix = sectionPrefixes[i];
                WriteStatsToHTML(htmlFile, csvStats, false, currentPrefix, htmlFileName, false);
            }
        }



        void AddStat(string statName, double budget, double multiplier, bool bIsInMainSummary)
        {
            stats.Add(statName);

			PeakStatInfo info = getOrAddStatInfo(statName);
			info.isInMainSummary = bIsInMainSummary;
			info.multiplier = multiplier;
			info.budget = budget;
		}
		
        public override void PostInit(ReportTypeInfo reportTypeInfo, CsvStats csvStats)
        {
            // Find the stats by spinning through the graphs in this reporttype
            foreach (ReportGraph graph in reportTypeInfo.graphs)
            {
                if (graph.inSummary)
                {
                    // If the graph has a mainstat, use that
                    if (graph.settings.mainStat.isSet)
                    {
                        AddStat(graph.settings.mainStat.value, graph.budget, graph.settings.statMultiplier.isSet ? graph.settings.statMultiplier.value : 1.0, graph.isInMainSummary);
                    }
                    else if (graph.settings.statString.isSet)
                    {
                        string statString = graph.settings.statString.value;
                        string[] statNames = statString.Split(' ');
                        foreach (string stat in statNames)
                        {
                            AddStat(stat, graph.budget, graph.settings.statMultiplier.isSet ? graph.settings.statMultiplier.value : 1.0, graph.isInMainSummary);
                        }
                    }
                }
            }

			base.PostInit(reportTypeInfo, csvStats);
		}

        public List<string> sectionPrefixes;

		Dictionary<string, PeakStatInfo> statInfoLookup = new Dictionary<string, PeakStatInfo>();
		class PeakStatInfo
		{
			public PeakStatInfo(string inName, string inShortName)
			{
				isInMainSummary = false;
				multiplier = 1.0;
				budget = 0.0;
				name = inName;
				shortName = inShortName;
			}
			public string name;
			public string shortName;
			public bool isInMainSummary;
			public double multiplier;
			public double budget;
		};

		PeakStatInfo getOrAddStatInfo(string statName)
		{
			if ( statInfoLookup.ContainsKey(statName) )
			{
				return statInfoLookup[statName];
			}
			// Find the best (longest) prefix which matches this stat, and strip it off
			int bestPrefixIndex = -1;
			int bestPrefixLength = 0;
			string shortStatName = statName;
			for (int i = 0; i < hidePrefixes.Count; i++)
			{
				string prefix = hidePrefixes[i];
				if (statName.ToLower().StartsWith(prefix) && prefix.Length > bestPrefixLength)
				{
					bestPrefixIndex = i;
					bestPrefixLength = prefix.Length;
				}
			}
			if (bestPrefixIndex >= 0)
			{
				shortStatName = statName.Substring(bestPrefixLength);
			}

			PeakStatInfo statInfo = new PeakStatInfo(statName,shortStatName);
			statInfoLookup.Add(statName, statInfo);
			return statInfo;
		}

        List<string> hidePrefixes;
    };

	class BoundedStatValuesSummary : Summary
	{
		class Column
		{
			public string name;
			public string formula;
			public double value;
			public string metadataKey;
			public string statName;
			public bool perSecond;
			public bool filterOutZeros;
			public bool applyEndOffset;
			public double multiplier;
			public double threshold;
			public ColourThresholdList colourThresholdList;
		};
		public BoundedStatValuesSummary(XElement element, string baseXmlDirectory)
		{
			ReadStatsFromXML(element);
			if (stats.Count != 0)
			{
				throw new Exception("<stats> element is not supported");
			}

			title = element.GetSafeAttibute("title", "Events");
			beginEvent = element.GetSafeAttibute<string>("beginevent");
			endEvent = element.GetSafeAttibute<string>("endevent");

			endOffsetPercentage = 0.0;
			XAttribute endOffsetAtt = element.Attribute("endoffsetpercent");
			if ( endOffsetAtt != null )
			{
				endOffsetPercentage = double.Parse(endOffsetAtt.Value);
			}
			columns = new List<Column>();

			foreach (XElement columnEl in element.Elements("column"))
			{
				Column column = new Column();
				double[] colourThresholds = ReadColourThresholdsXML(columnEl.Element("colourThresholds"));
				if (colourThresholds != null)
				{
					column.colourThresholdList = new ColourThresholdList();
					for (int i = 0; i < colourThresholds.Length; i++)
					{
						column.colourThresholdList.Add(new ThresholdInfo(colourThresholds[i], null));
					}
				}

				XAttribute metadataKeyAtt = columnEl.Attribute("metadataKey");
				if (metadataKeyAtt!=null)
				{
					column.metadataKey = metadataKeyAtt.Value;
				}
				column.statName = columnEl.Attribute("stat").Value.ToLower();
				if ( !stats.Contains(column.statName) )
				{
					stats.Add(column.statName);
				}

				column.name = columnEl.Attribute("name").Value;
				column.formula = columnEl.Attribute("formula").Value;
				column.filterOutZeros= columnEl.GetSafeAttibute<bool>("filteroutzeros", false);
				column.perSecond = columnEl.GetSafeAttibute<bool>("persecond", false);
				column.multiplier = columnEl.GetSafeAttibute<double>("multiplier", 1.0);
				column.threshold = columnEl.GetSafeAttibute<double>("threshold", 0.0);
				column.applyEndOffset = columnEl.GetSafeAttibute<bool>("applyEndOffset", true);
				columns.Add(column);
			}
		}

		public override void WriteSummaryData(System.IO.StreamWriter htmlFile, CsvStats csvStats, bool bIncludeSummaryCsv, SummaryMetadata metadata, string htmlFileName)
		{
			int startFrame = -1;
			int endFrame = int.MaxValue;

			// Find the start and end frames based on the events
			if (beginEvent != null)
			{
				foreach (CsvEvent ev in csvStats.Events)
				{
					if (CsvStats.DoesSearchStringMatch(ev.Name, beginEvent))
					{
						startFrame = ev.Frame;
						break;
					}
				}
				if (startFrame == -1)
				{
					Console.WriteLine("BoundedStatValuesSummary: Begin event " + beginEvent + " was not found");
					return;
				}
			}
			if (endEvent != null)
			{
				foreach (CsvEvent ev in csvStats.Events)
				{
					if (CsvStats.DoesSearchStringMatch(ev.Name, endEvent))
					{
						endFrame = ev.Frame;
						if ( endFrame > startFrame )
						{
							break;
						}
					}
				}
				if (endFrame == int.MaxValue)
				{
					Console.WriteLine("BoundedStatValuesSummary: End event " + endEvent + " was not found");
					return;
				}
			}
			if ( startFrame >= endFrame )
			{
				throw new Exception("BoundedStatValuesSummary: end event appeared before the start event");
			}
			endFrame = Math.Min(endFrame, csvStats.SampleCount - 1);
			startFrame = Math.Max(startFrame, 0);
			
			// Adjust the end frame based on the specified offset percentage, but cache the old value (some columns may need the unmodified one)
			int endEventFrame = Math.Min(csvStats.SampleCount, endFrame + 1);
			if (endOffsetPercentage > 0.0)
			{
				double multiplier = endOffsetPercentage / 100.0;
				endFrame += (int)((double)(endFrame-startFrame)*multiplier);
			}
			endFrame = Math.Min(csvStats.SampleCount, endFrame + 1);
			StatSamples frameTimeStat = csvStats.GetStat("frametime");
			List<float> frameTimes = frameTimeStat.samples;

			// Filter only columns with stats that exist in the CSV
			List<Column> filteredColumns = new List<Column>();
			foreach (Column col in columns)
			{
				if (csvStats.GetStat(col.statName) != null)
				{
					filteredColumns.Add(col);
				}
			}

			// Nothing to report, so bail out!
			if (filteredColumns.Count == 0)
			{
				return;
			}

			// Process the column values
			foreach (Column col in filteredColumns)
			{
				List<float> statValues = csvStats.GetStat(col.statName).samples;
				double value = 0.0;
				double totalFrameWeight = 0.0;
				int colEndFrame = col.applyEndOffset ? endFrame : endEventFrame;

				if ( col.formula == "average")
				{
					for (int i=startFrame; i< colEndFrame; i++)
					{
						if (col.filterOutZeros == false || statValues[i] > 0)
						{
							value += statValues[i] * frameTimes[i];
							totalFrameWeight += frameTimes[i];
						}
					}
				}
				else if (col.formula == "percentoverthreshold")
				{
					for (int i = startFrame; i < colEndFrame; i++)
					{
						if (statValues[i] > col.threshold)
						{
							value += frameTimes[i];
						}
						totalFrameWeight += frameTimes[i];
					}
					value *= 100.0;
				}
				else if (col.formula == "percentunderthreshold")
				{
					for (int i = startFrame; i < colEndFrame; i++)
					{
						if (statValues[i] < col.threshold)
						{
							value += frameTimes[i];
						}
						totalFrameWeight += frameTimes[i];
					}
					value *= 100.0;
				}
				else if (col.formula == "sum")
				{
					for (int i = startFrame; i < colEndFrame; i++)
					{
						value += statValues[i];
					}

					if (col.perSecond)
					{
						double totalTimeMS = 0.0;
						for (int i = startFrame; i < colEndFrame; i++)
						{
							if (col.filterOutZeros == false || statValues[i] > 0)
							{
								totalTimeMS += frameTimes[i];
							}
						}
						value /= (totalTimeMS / 1000.0);
					}
					totalFrameWeight = 1.0;
				}
				else if (col.formula == "streamingstressmetric")
				{
					// Note: tInc is scaled such that it hits 1.0 on the event frame, regardless of the offset
					double tInc = 1.0/(double)(endEventFrame - startFrame);
					double t = tInc*0.5;
					for (int i = startFrame; i < colEndFrame; i++)
					{
						if (col.filterOutZeros == false || statValues[i] > 0)
						{
							// Frame weighting is scaled to heavily favor final frames. Note that t can exceed 1 after the event frame if an offset percentage is specified, so we clamp it
							double frameWeight = Math.Pow(Math.Min(t,1.0), 4.0) * frameTimes[i];

							// If we're past the end event frame, apply a linear falloff to the weight
							if (i >= endEventFrame)
							{
								double falloff = 1.0 - (double)(i - endEventFrame) / (colEndFrame - endEventFrame);
								frameWeight *= falloff;
							}

							// The frame score takes into account the queue depth, but it's not massively significant
							double frameScore = Math.Pow(statValues[i], 0.25);
							value += frameScore * frameWeight;
							totalFrameWeight += frameWeight;
						}
						t += tInc;
					}
				}
				else
				{
					throw new Exception("BoundedStatValuesSummary: unexpected formula "+col.formula);
				}
				value *= col.multiplier;
				col.value = value / totalFrameWeight;
			}

			// Output HTML
			if (htmlFile != null)
			{
				htmlFile.WriteLine("  <h2>" + title + "</h2>");
				htmlFile.WriteLine("  <table border='0' style='width:1400'>");
				htmlFile.WriteLine("  <tr>");
				foreach (Column col in filteredColumns)
				{
					htmlFile.WriteLine("<th>" + col.name + "</th>");
				}
				htmlFile.WriteLine("  </tr>");
				htmlFile.WriteLine("  <tr>");
				foreach (Column col in filteredColumns)
				{
					string bgcolor = "'#ffffff'";
					if (col.colourThresholdList != null)
					{
						bgcolor = col.colourThresholdList.GetColourForValue(col.value);
					}
					htmlFile.WriteLine("<td bgcolor=" + bgcolor + ">" + col.value.ToString("0.00") + "</td>");
				}
				htmlFile.WriteLine("  </tr>");
				htmlFile.WriteLine("  </table>");
			}

			// Output metadata
			if (metadata != null)
			{
				foreach (Column col in filteredColumns)
				{
					if ( col.metadataKey != null )
					{
						metadata.Add(SummaryMetadataValue.Type.SummaryTableMetric, col.metadataKey, col.value.ToString("0.00"), col.colourThresholdList);
					}
				}
			}
		}
		public override void PostInit(ReportTypeInfo reportTypeInfo, CsvStats csvStats)
		{
		}
		string title;
		string beginEvent;
		string endEvent;
		double endOffsetPercentage;
		List<Column> columns;
	};

	class MapOverlaySummary : Summary
	{
		class MapOverlayEvent
		{
			public MapOverlayEvent(string inName)
			{
				name = inName;
			}
			public MapOverlayEvent(XElement element)
			{
			}
			public string name;
			public string metadataKey;
			public string shortName;
			public string lineColor;
		};

		class MapOverlay
		{
			public MapOverlay(XElement element)
			{
				positionStatNames[0] = element.GetSafeAttibute<string>("xStat");
				positionStatNames[1] = element.GetSafeAttibute<string>("yStat");
				positionStatNames[2] = element.GetSafeAttibute<string>("zStat");
				metadataPrefix = element.GetSafeAttibute<string>("metadataPrefix");
				lineColor = element.GetSafeAttibute<string>("lineColor","#ffffff");
				foreach (XElement eventEl in element.Elements("event"))
				{
					MapOverlayEvent ev = new MapOverlayEvent(eventEl.Attribute("name").Value);
					ev.shortName = eventEl.GetSafeAttibute<string>("shortName");
					ev.metadataKey = eventEl.GetSafeAttibute<string>("metadataKey");
					ev.lineColor = eventEl.GetSafeAttibute<string>("lineColor");
					if (eventEl.GetSafeAttibute<bool>("isStartEvent", false))
					{
						if (startEvent != null)
						{
							throw new Exception("Can't have multiple start events!");
						}
						startEvent = ev;
					}
					events.Add(ev);
				}

			}
			public string [] positionStatNames = new string[3];
			public string metadataPrefix;
			public MapOverlayEvent startEvent;
			public string lineColor;
			public List<MapOverlayEvent> events = new List<MapOverlayEvent>();
		}

		public MapOverlaySummary(XElement element, string baseXmlDirectory)
		{
			ReadStatsFromXML(element);
			if (stats.Count != 0)
			{
				throw new Exception("<stats> element is not supported");
			}

			sourceImagePath = element.GetSafeAttibute<string>("sourceImage");
			if ( !System.IO.Path.IsPathRooted(sourceImagePath))
			{
				sourceImagePath = System.IO.Path.GetFullPath(System.IO.Path.Combine(baseXmlDirectory,sourceImagePath));
			}

			offsetX = element.GetSafeAttibute<float>("offsetX",0.0f);
			offsetY = element.GetSafeAttibute<float>("offsetY",0.0f);
			scale = element.GetSafeAttibute<float>("scale",1.0f);
			title = element.GetSafeAttibute("title", "Events");
			destImageFilename = element.Attribute("destImage").Value;
			imageWidth = element.GetSafeAttibute<float>("width", 250.0f);
			imageHeight = element.GetSafeAttibute<float>("height", 250.0f);
			framesPerLineSegment = element.GetSafeAttibute<int>("framesPerLineSegment", 5);
			lineSplitDistanceThreshold = element.GetSafeAttibute<float>("lineSplitDistanceThreshold", float.MaxValue);

			foreach (XElement overlayEl in element.Elements("overlay"))
			{
				MapOverlay overlay = new MapOverlay(overlayEl);
				overlays.Add(overlay);
				stats.Add(overlay.positionStatNames[0]);
				stats.Add(overlay.positionStatNames[1]);
				stats.Add(overlay.positionStatNames[2]);
			}
		}

		int toSvgX(float worldX, float worldY)
		{
			float svgX = (worldY * scale + offsetX) * 0.5f + 0.5f;
			svgX *= imageWidth;
			return (int)(svgX + 0.5f);
		}

		int toSvgY(float worldX, float worldY)
		{ 
			float svgY = 1.0f - (worldX * scale + offsetY) * 0.5f - 0.5f;
			svgY *= imageHeight;
			return (int)(svgY + 0.5f);
		}

		public override void WriteSummaryData(System.IO.StreamWriter htmlFile, CsvStats csvStats, bool bIncludeSummaryCsv, SummaryMetadata metadata, string htmlFileName)
		{
			// Output HTML
			if (htmlFile != null)
			{
				string outputDirectory= System.IO.Path.GetDirectoryName(System.IO.Path.GetFullPath(htmlFileName));
				string outputMapFilename = System.IO.Path.Combine(outputDirectory, destImageFilename);

				if ( !System.IO.File.Exists(outputMapFilename))
				{
					System.IO.File.Copy(sourceImagePath, outputMapFilename);
				}

				// Check if the file exists in the output directory
				htmlFile.WriteLine("  <h2>" + title + "</h2>");
				htmlFile.WriteLine("<svg version='1.1' xmlns='http://www.w3.org/2000/svg' xmlns:xlink='http://www.w3.org/1999/xlink' width='" + imageWidth + "' height='" + imageHeight + "'>");
				htmlFile.WriteLine("<image href='" + destImageFilename + "' width='" + imageWidth + "' height='" + imageHeight + "' />");

				// Draw the overlays
				foreach (MapOverlay overlay in overlays)
				{
					StatSamples xStat = csvStats.GetStat(overlay.positionStatNames[0]);
					StatSamples yStat = csvStats.GetStat(overlay.positionStatNames[1]);

					if (xStat == null || yStat == null)
					{
						continue;
					}

					// If a startevent is specified, update the start frame
					int startFrame = 0;
					if (overlay.startEvent != null)
					{
						foreach (CsvEvent ev in csvStats.Events)
						{
							if (CsvStats.DoesSearchStringMatch(ev.Name, overlay.startEvent.name))
							{
								startFrame = ev.Frame;
								break;
							}
						}
					}

					// Make a mapping from frame to map indices
					List<KeyValuePair<int, MapOverlayEvent>> frameEvents = new List<KeyValuePair<int, MapOverlayEvent>>();
					foreach (MapOverlayEvent mapEvent in overlay.events)
					{
						foreach (CsvEvent ev in csvStats.Events)
						{
							if (CsvStats.DoesSearchStringMatch(ev.Name, mapEvent.name))
							{
								frameEvents.Add(new KeyValuePair<int, MapOverlayEvent>(ev.Frame, mapEvent));
							}
						}
					}
					frameEvents.Sort((pair0, pair1) => pair0.Key.CompareTo(pair1.Key));
					int eventIndex = 0;

					// Draw the lines
					string currentLineColor = overlay.lineColor;
					string lineStartTemplate = "<polyline style='fill:none;stroke-width:1.3;stroke:{LINECOLOUR}' points='";
					htmlFile.Write(lineStartTemplate.Replace("{LINECOLOUR}", currentLineColor));
					float adjustedLineSplitDistanceThreshold = lineSplitDistanceThreshold * framesPerLineSegment;
					float oldx = 0;
					float oldy = 0;
					int lastFrameIndex = 0;
					for (int i = startFrame; i < xStat.samples.Count; i += framesPerLineSegment)
					{
						float x = xStat.samples[i];
						float y = yStat.samples[i];
						string lineCoordsStr = toSvgX(x, y) + "," + toSvgY(x, y) + " ";

						// Figure out which event we're up to so we can do color changes
						bool restartLineStrip = false;
						while (eventIndex < frameEvents.Count && lastFrameIndex < frameEvents[eventIndex].Key && i >= frameEvents[eventIndex].Key)
						{
							MapOverlayEvent mapEvent = frameEvents[eventIndex].Value;
							string newLineColor = mapEvent.lineColor != null ? mapEvent.lineColor : overlay.lineColor;
							// If we changed color, restart the line strip
							if (newLineColor != currentLineColor)
							{
								currentLineColor = newLineColor;
								restartLineStrip = true;
							}
							eventIndex++;
						}

						// If the distance between this point and the last is over the threshold, restart the line strip
						float maxManhattanDist = Math.Max(Math.Abs(x - oldx), Math.Abs(y - oldy));
						if (maxManhattanDist > adjustedLineSplitDistanceThreshold)
						{
							restartLineStrip = true;
						}
						else
						{
							htmlFile.Write(lineCoordsStr);
						}

						if (restartLineStrip)
						{
							htmlFile.WriteLine("'/>");
							htmlFile.Write(lineStartTemplate.Replace("{LINECOLOUR}", currentLineColor));
							htmlFile.Write(lineCoordsStr);
						}
						oldx = x;
						oldy = y;
						lastFrameIndex = i;
					}
					htmlFile.WriteLine("'/>");

					// Plot the events 
					float circleRadius = 3;
					string eventColourString = "#ffffff";
					foreach (MapOverlayEvent mapEvent in overlay.events)
					{
						foreach (CsvEvent ev in csvStats.Events)
						{
							if (CsvStats.DoesSearchStringMatch(ev.Name, mapEvent.name))
							{
								string eventText = mapEvent.shortName != null ? mapEvent.shortName : ev.Name;
								float x = xStat.samples[ev.Frame];
								float y = yStat.samples[ev.Frame];
								int svgX = toSvgX(x, y);
								int svgY = toSvgY(x, y);
								htmlFile.Write("<circle cx='" + svgX + "' cy='" + svgY + "' r='" + circleRadius + "' fill='" + eventColourString + "' fill-opacity='1.0'/>");
								htmlFile.WriteLine("<text x='" + (svgX + 5) + "' y='" + svgY + "' text-anchor='left' style='font-family: Verdana;fill: #ffffff; font-size: " + 9 + "px;'>" + eventText + "</text>");
							}
						}
					}
				}

				//htmlFile.WriteLine("<text x='50%' y='" + (imageHeight * 0.05) + "' text-anchor='middle' style='font-family: Verdana;fill: #FFFFFF; stroke: #C0C0C0;  font-size: " + 20 + "px;'>" + title + "</text>");
				htmlFile.WriteLine("</svg>");
			}

			// Output metadata
			if (metadata != null)
			{
			}
		}
		public override void PostInit(ReportTypeInfo reportTypeInfo, CsvStats csvStats)
		{
		}
		string title;
		string sourceImagePath;
		float offsetX;
		float offsetY;
		float scale;
		string destImageFilename;
		float imageWidth;
		float imageHeight;
		float lineSplitDistanceThreshold;
		int framesPerLineSegment;

		List<MapOverlay> overlays = new List<MapOverlay>();
	};

	class SummaryMetadataValue
    {
		// Bump this when making changes!
		public static int CacheVersion = 1;

		public enum Type
		{
			CsvStatAverage,
			CsvMetadata,
			SummaryTableMetric,
			ToolMetadata
		};

		public enum Flags
		{
			Hidden = 0x01
		};

		private SummaryMetadataValue()
		{

		}
		public SummaryMetadataValue(Type inType, string inName, double inValue, ColourThresholdList inColorThresholdList, string inToolTip, uint inFlags = 0)
		{
			type = inType;
			name = inName;
			isNumeric = true;
			numericValue = inValue;
			value = inValue.ToString();
			colorThresholdList = inColorThresholdList;
			tooltip = inToolTip;
			flags = inFlags;
		}
		public SummaryMetadataValue(Type inType, string inName, string inValue, ColourThresholdList inColorThresholdList, string inToolTip, uint inFlags = 0)
        {
			type = inType;
			name = inName;
			numericValue = 0.0;
			isNumeric = false;
			colorThresholdList = inColorThresholdList;
            value = inValue;
			tooltip = inToolTip;
			flags = inFlags;
		}

		public static SummaryMetadataValue ReadFromCache(BinaryReader reader)
		{
			SummaryMetadataValue val = new SummaryMetadataValue();
			val.type = (Type)reader.ReadUInt32();
			val.name = reader.ReadString();
			val.value = reader.ReadString();
			val.tooltip = reader.ReadString();
			val.numericValue = reader.ReadDouble();
			val.isNumeric = reader.ReadBoolean();
			val.flags = reader.ReadUInt32();
			bool hasThresholdList = reader.ReadBoolean();
			if (hasThresholdList)
			{
				int thresholdCount = reader.ReadInt32();
				val.colorThresholdList = new ColourThresholdList();
				for (int i = 0; i < thresholdCount; i++)
				{
					bool bHasColour = reader.ReadBoolean();
					Colour thresholdColour = null;
					if (bHasColour)
					{
						thresholdColour = new Colour(reader.ReadString());
					}
					double thresholdValue = reader.ReadDouble();
					ThresholdInfo info = new ThresholdInfo(thresholdValue, thresholdColour);
					val.colorThresholdList.Add(info);
				}
			}
			return val;
		}



		public void WriteToCache(BinaryWriter writer)
		{
			writer.Write((uint)type);
			writer.Write(name);
			writer.Write(value);
			writer.Write(tooltip);
			writer.Write(numericValue);
			writer.Write(isNumeric);
			writer.Write(flags);
			writer.Write(colorThresholdList != null);
			if (colorThresholdList != null)
			{
				writer.Write((int)colorThresholdList.Count);
				foreach (ThresholdInfo thresholdInfo in colorThresholdList.Thresholds)
				{
					writer.Write(thresholdInfo.colour != null);
					if (thresholdInfo.colour != null)
					{
						writer.Write(thresholdInfo.colour.ToString());
					}
					writer.Write(thresholdInfo.value);
				}
			}
		}

		public SummaryMetadataValue Clone()
		{
			return (SummaryMetadataValue)MemberwiseClone();
		}

		public void SetFlag(Flags flag, bool value)
		{
			if (value)
			{
				flags |= (uint)flag;
			}
			else
			{
				flags &= ~(uint)flag;
			}
		}
		public bool GetFlag(Flags flag)
		{
			return (flags & (uint)flag) != 0;
		}

		public Type type;
		public string name;
		public string value;
		public string tooltip;
        public ColourThresholdList colorThresholdList;
		public double numericValue;
		public bool isNumeric;
		public uint flags;
    }
    class SummaryMetadata 
    {
		public SummaryMetadata()
		{
		}

		static int CacheVersion = 6;

		public static SummaryMetadata TryReadFromCache(string metadataCacheDir, string csvId)
		{
			SummaryMetadata metaData = null;
			string filename = Path.Combine(metadataCacheDir, csvId + ".prc");

			if ( !File.Exists(filename) )
			{
				return null;
			}

			try
			{
				using (FileStream fileStream = new FileStream(filename, FileMode.Open))
				{
					BinaryReader reader = new BinaryReader(fileStream);
					int version = reader.ReadInt32();
					int metadataValueVersion = reader.ReadInt32();
					if (version == CacheVersion && metadataValueVersion == SummaryMetadataValue.CacheVersion)
					{
						metaData = new SummaryMetadata();
						int dictEntryCount = reader.ReadInt32();
						for (int i = 0; i < dictEntryCount; i++)
						{
							string key = reader.ReadString();
							SummaryMetadataValue value = SummaryMetadataValue.ReadFromCache(reader);
							metaData.dict.Add(key, value);
						}
						string endString = reader.ReadString();
						if (endString != "END")
						{
							Console.WriteLine("Corruption detected in " + filename + ". Skipping read");
							metaData = null;
						}
					}
					reader.Close();
				}
			}
			catch (Exception e)
			{
				metaData = null;
				Console.WriteLine("Error reading from cache file " + filename + ": "+e.Message);
			}
			return metaData;
		}
		public bool WriteToCache(string metadataCacheDir, string csvId)
		{
			string filename = Path.Combine(metadataCacheDir, csvId + ".prc");
			try
			{
				using (FileStream fileStream = new FileStream(filename, FileMode.Create))
				{
					BinaryWriter writer = new BinaryWriter(fileStream);
					writer.Write(CacheVersion);
					writer.Write(SummaryMetadataValue.CacheVersion);

					writer.Write(dict.Count);
					foreach (KeyValuePair<string, SummaryMetadataValue> entry in dict)
					{
						writer.Write(entry.Key);
						entry.Value.WriteToCache(writer);
					}
					writer.Write("END");
					writer.Close();
				}
			}
			catch (IOException)
			{
				Console.WriteLine("Failed to write to cache file " + filename + ".");
				return false;
			}
			return true;
		}

		public void RemoveSafe(string name)
		{
			string key = name.ToLower();
			if (dict.ContainsKey(key))
			{
				dict.Remove(key);
			}
		}

		public void Add(SummaryMetadataValue.Type type, string name, string value, ColourThresholdList colorThresholdList = null, string tooltip = "", uint flags=0)
        {
			string key = name.ToLower();
			double numericValue = double.MaxValue;
            try
            {
                numericValue = Convert.ToDouble(value, System.Globalization.CultureInfo.InvariantCulture);
            }
            catch { }

            SummaryMetadataValue metadataValue = null;
            if (numericValue != double.MaxValue)
            {
                metadataValue = new SummaryMetadataValue(type, name, numericValue, colorThresholdList, tooltip, flags);
            }
            else
            {
                metadataValue = new SummaryMetadataValue(type, name, value, colorThresholdList, tooltip, flags);
            }

			try
			{
				dict.Add(key, metadataValue);
			}
			catch (System.ArgumentException)
			{
				throw new Exception("Summary metadata key " + key + " has already been added");
			}
		}

        public void AddString(SummaryMetadataValue.Type type, string name, string value, ColourThresholdList colorThresholdList = null, string tooltip = "")
        {
			string key = name.ToLower();
			SummaryMetadataValue metadataValue = new SummaryMetadataValue(type, name, value, colorThresholdList, tooltip);
            dict.Add(key, metadataValue);
        }

        public Dictionary<string, SummaryMetadataValue> dict = new Dictionary<string, SummaryMetadataValue>();
	};

	class SummarySectionBoundaryInfo
	{
		public string startToken;
		public string endToken;
		public string statName;
	};
	class SummaryTableInfo
	{
		public SummaryTableInfo(XElement tableElement)
		{
			XAttribute rowSortAt = tableElement.Attribute("rowSort");
			if (rowSortAt != null)
			{
				rowSortList.AddRange(rowSortAt.Value.Split(','));
			}

			XElement filterEl = tableElement.Element("filter");
			if (filterEl != null)
			{
				columnFilterList.AddRange(filterEl.Value.Split(','));
			}
			XElement sectionBoundaryEl = tableElement.Element("sectionBoundary");
			if (sectionBoundaryEl != null)
			{
				sectionBoundary = new SummarySectionBoundaryInfo();
				sectionBoundary.statName = sectionBoundaryEl.Attribute("statName").Value;
				sectionBoundary.startToken = sectionBoundaryEl.Attribute("startToken").Value;
				sectionBoundary.endToken = sectionBoundaryEl.Attribute("endToken").Value;
			}
		}

		public SummaryTableInfo(string filterListStr, string rowSortStr)
		{
			columnFilterList.AddRange(filterListStr.Split(','));
			rowSortList.AddRange(rowSortStr.Split(','));
		}

		public List<string> rowSortList = new List<string>();
		public List<string> columnFilterList = new List<string>();
		public SummarySectionBoundaryInfo sectionBoundary = null;
	}


	class SummaryMetadataColumn
	{
		public string name;
		public bool isNumeric = false;
		public string displayName;
		List<float> floatValues = new List<float>();
		List<string> stringValues = new List<string>();
		List<string> toolTips = new List<string>();

		List<ColourThresholdList> colourThresholds = new List<ColourThresholdList>();
		ColourThresholdList colourThresholdOverride = null;
		public SummaryMetadataColumn(string inName, bool inIsNumeric, string inDisplayName = null)
		{
			name = inName;
			isNumeric = inIsNumeric;
			displayName = inDisplayName;
		}
		public SummaryMetadataColumn Clone()
		{
			SummaryMetadataColumn newColumn = new SummaryMetadataColumn(name, isNumeric, displayName);
			newColumn.floatValues.AddRange(floatValues);
			newColumn.stringValues.AddRange(stringValues);
			newColumn.colourThresholds.AddRange(colourThresholds);
			newColumn.toolTips.AddRange(toolTips);
			return newColumn;
		}

		public string GetDisplayName()
		{
			if ( displayName==null )
			{
				return TableUtil.FormatStatName(name);
			}
			return displayName;
		}

		public void SetValue(int index, float value)
		{
			if ( !isNumeric )
			{
				// This is already a non-numeric column. Better treat this as a string value
				SetStringValue(index, value.ToString());
				return;
			}
			// Grow to fill if necessary
			if ( index >= floatValues.Count )
			{
				for ( int i= floatValues.Count; i<=index; i++ )
				{
					floatValues.Add(float.MaxValue);
				}
			}
			floatValues[index] = value;
		}

		void convertToStrings()
		{
			if ( isNumeric )
			{
				stringValues = new List<string>();
				foreach (float f in floatValues)
				{
					stringValues.Add(f.ToString());
				}
				floatValues = new List<float>();
				isNumeric = false;
			}
		}

		public void SetColourThresholds(int index, ColourThresholdList value)
		{
			// Grow to fill if necessary
			if (index >= colourThresholds.Count)
			{
				for (int i = colourThresholds.Count; i <= index; i++)
				{
					colourThresholds.Add(null);
				}
			}
			colourThresholds[index] = value;
		}

		public ColourThresholdList GetColourThresholds(int index)
		{
			if (index < colourThresholds.Count)
			{
				return colourThresholds[index];
			}
			return null;
		}

		public string GetColour(int index)
		{
			ColourThresholdList thresholds = null;
			float value = GetValue(index);
			if (value==float.MaxValue)
			{
				return null;
			}
			if (colourThresholdOverride != null)
			{
				thresholds = colourThresholdOverride;
			}
			else
			{
				if (index < colourThresholds.Count)
				{
					thresholds = colourThresholds[index];
				}
				if (thresholds == null)
				{
					return null;
				}
			}
			return thresholds.GetColourForValue((double)value);
		}

		public void ComputeAutomaticColourThresholds(bool bHighIsRed)
		{
			colourThresholds = new List<ColourThresholdList>();
			float maxValue = -float.MaxValue;
			float minValue = float.MaxValue;
			float totalValue = 0.0f;
			float validCount = 0.0f;
			for ( int i=0; i<floatValues.Count; i++ )
			{
				float val = floatValues[i];
				if (val != float.MaxValue)
				{
					maxValue = Math.Max(val, maxValue);
					minValue = Math.Min(val, minValue);
					totalValue += val;
					validCount += 1.0f;
				}
			}
			if (minValue == maxValue)
			{
				return;
			}

			Colour green = new Colour(0.4f, 0.82f, 0.45f);
			Colour yellow = new Colour(1.0f, 1.0f, 0.5f);
			Colour red = new Colour(1.0f, 0.4f, 0.4f);

			float averageValue = totalValue / validCount; // TODO: Weighted average 
			colourThresholdOverride = new ColourThresholdList();
			colourThresholdOverride.Add(new ThresholdInfo(minValue, bHighIsRed ? green : red));
			colourThresholdOverride.Add(new ThresholdInfo(averageValue, yellow));
			colourThresholdOverride.Add(new ThresholdInfo(averageValue, yellow));
			colourThresholdOverride.Add(new ThresholdInfo(maxValue, bHighIsRed ? red : green));
		}

		public int GetCount()
		{
			return Math.Max(floatValues.Count, stringValues.Count);
		}
		public float GetValue(int index)
		{
			if ( index >= floatValues.Count )
			{
				return float.MaxValue;
			}
			return floatValues[index];
		}

		public void SetStringValue(int index, string value)
		{
			if (isNumeric)
			{
				// Better convert this to a string column, since we're trying to add a string to it
				convertToStrings();
			}
			// Grow to fill if necessary
			if (index >= stringValues.Count)
			{
				for (int i = stringValues.Count; i <= index; i++)
				{
					stringValues.Add("");
				}
			}
			stringValues[index] = value;
			isNumeric = false;
		}
		public string GetStringValue(int index, bool roundNumericValues = false)
		{
			if (isNumeric)
			{
				if (index >= floatValues.Count || floatValues[index] == float.MaxValue)
				{
					return "";
				}
				float val = floatValues[index];
				if (roundNumericValues)
				{
					float absVal = Math.Abs(val);
					float frac = absVal - (float)Math.Truncate(absVal);
					if (absVal >= 250.0f || frac < 0.0001f )
					{
						return val.ToString("0");
					}
					if ( absVal >= 50.0f )
					{
						return val.ToString("0.0");
					}
					if ( absVal >= 0.1 )
					{
						return val.ToString("0.00");
					}
					return val.ToString("0.000");
				}
				return val.ToString();
			}
			else
			{
				if (index >= stringValues.Count)
				{
					return "";
				}
				return stringValues[index];
			}
		}
		public void SetToolTipValue(int index, string value)
		{
			// Grow to fill if necessary
			if (index >= toolTips.Count)
			{
				for (int i = toolTips.Count; i <= index; i++)
				{
					toolTips.Add("");
				}
			}
			toolTips[index] = value;
		}
		public string GetToolTipValue(int index)
		{
			if (index >= toolTips.Count)
			{
				return "";
			}
			return toolTips[index];
		}

	};



	class SummaryMetadataTable
    {
        public SummaryMetadataTable()
        {
        }

		public SummaryMetadataTable CollateSortedTable(List<string> collateByList, bool addMinMaxColumns)
		{
			int numSubColumns=addMinMaxColumns ? 3 : 1;

			List<SummaryMetadataColumn> newColumns = new List<SummaryMetadataColumn>();
			List<string> finalSortByList = new List<string>();
			foreach (string collateBy in collateByList)
			{
				string key = collateBy.ToLower();
				if (columnLookup.ContainsKey(key))
				{
					newColumns.Add(new SummaryMetadataColumn(columnLookup[key].name, false, columnLookup[key].displayName));
					finalSortByList.Add(key);
				}
			}

			if ( finalSortByList.Count == 0 )
			{
				throw new Exception("None of the metadata strings were found:" + collateByList.ToString());
			}

            newColumns.Add(new SummaryMetadataColumn("Count", true));
            int countColumnIndex = newColumns.Count-1;

            int numericColumnStartIndex = newColumns.Count;
			List<int> srcToDestBaseColumnIndex = new List<int>();
			foreach ( SummaryMetadataColumn column in columns )
			{
                // Add avg/min/max columns for this column if it's numeric and we didn't already add it above 
				if ( column.isNumeric && !finalSortByList.Contains(column.name.ToLower()))
				{
					srcToDestBaseColumnIndex.Add( newColumns.Count );
					newColumns.Add(new SummaryMetadataColumn("Avg " + column.name, true));
					if (addMinMaxColumns)
					{
						newColumns.Add(new SummaryMetadataColumn("Min " + column.name, true));
						newColumns.Add(new SummaryMetadataColumn("Max " + column.name, true));
					}
				}
				else
				{
					srcToDestBaseColumnIndex.Add(-1);
				}
			}

			List<float> RowMaxValues = new List<float>();
			List<float> RowTotals = new List<float>();
			List<float> RowMinValues = new List<float>();
			List<int> RowCounts = new List<int>();
			List<ColourThresholdList> RowColourThresholds = new List<ColourThresholdList>();

			// Set the initial sort key
			string CurrentRowSortKey = "";
			foreach (string collateBy in finalSortByList)
			{
				CurrentRowSortKey += "{" + columnLookup[collateBy].GetStringValue(0) + "}";
			}

			int destRowIndex = 0;
			bool reset = true;
			int mergedRowsCount = 0;
			for (int i = 0; i < rowCount; i++)
			{
				if (reset)
				{
					RowMaxValues.Clear();
					RowMinValues.Clear();
					RowTotals.Clear();
					RowCounts.Clear();
					RowColourThresholds.Clear();
					for (int j = 0; j < columns.Count; j++)
					{
						if (addMinMaxColumns)
						{
							RowMaxValues.Add(-float.MaxValue);
							RowMinValues.Add(float.MaxValue);
						}
						RowTotals.Add(0.0f);
						RowCounts.Add(0);
						RowColourThresholds.Add(null);
					}
					mergedRowsCount = 0;
					reset = false;
				}

				// Compute min/max/total for all numeric columns
				for (int j = 0; j < columns.Count; j++)
				{
					SummaryMetadataColumn column = columns[j];
					if (column.isNumeric)
					{
						float value = column.GetValue(i);
						if (value != float.MaxValue)
						{
							if (addMinMaxColumns)
							{
								RowMaxValues[j] = Math.Max(RowMaxValues[j], value);
								RowMinValues[j] = Math.Min(RowMinValues[j], value);
							}
							RowTotals[j] += value;
							RowColourThresholds[j] = column.GetColourThresholds(i);
							RowCounts[j]++;
						}
					}
				}
				mergedRowsCount++;

				// Are we done?
				string nextSortKey = "";
				if (i < rowCount - 1)
				{
					foreach (string collateBy in finalSortByList)
					{
						nextSortKey += "{" + columnLookup[collateBy].GetStringValue(i + 1) + "}";
					}
				}

				// If this is the last row or if the sort key is different then write it out
				if (nextSortKey != CurrentRowSortKey)
				{
					for ( int j=0; j<countColumnIndex; j++ )
					{
						string key = newColumns[j].name.ToLower();
						newColumns[j].SetStringValue(destRowIndex, columnLookup[key].GetStringValue(i));
					}
					// Commit the row 
					newColumns[countColumnIndex].SetValue(destRowIndex, (float)mergedRowsCount);
					for (int j = 0; j < columns.Count; j++)
					{
						int destColumnBaseIndex = srcToDestBaseColumnIndex[j];
						if (destColumnBaseIndex != -1 && RowCounts[j]>0)
						{
							newColumns[destColumnBaseIndex].SetValue(destRowIndex, RowTotals[j] / (float)RowCounts[j]);
							if (addMinMaxColumns)
							{
								newColumns[destColumnBaseIndex + 1].SetValue(destRowIndex, RowMinValues[j]);
								newColumns[destColumnBaseIndex + 2].SetValue(destRowIndex, RowMaxValues[j]);
							}

							// Set colour thresholds based on the source column
							ColourThresholdList Thresholds = RowColourThresholds[j];
							for ( int k=0;k<numSubColumns;k++)
							{
								newColumns[destColumnBaseIndex+k].SetColourThresholds(destRowIndex, Thresholds);
							}
						}
					}
					reset = true;
					destRowIndex++;
				}
				CurrentRowSortKey = nextSortKey;
			}

			SummaryMetadataTable newTable = new SummaryMetadataTable();
			newTable.columns = newColumns;
			newTable.InitColumnLookup();
			newTable.rowCount = destRowIndex;
			newTable.firstStatColumnIndex = numericColumnStartIndex;
			newTable.isCollated = true;
			newTable.hasMinMaxColumns = addMinMaxColumns;
			return newTable;
		}

		public SummaryMetadataTable SortAndFilter(string customFilter, string customRowSort = "buildversion,deviceprofile", bool bReverseSort=false)
		{
			return SortAndFilter(customFilter.Split(',').ToList(), customRowSort.Split(',').ToList(), bReverseSort);
		}

		public SummaryMetadataTable SortAndFilter(List<string> columnFilterList, List<string> rowSortList, bool bReverseSort)
		{
			SummaryMetadataTable newTable = SortRows(rowSortList, bReverseSort);

			// Make a list of all unique keys
			List<string> allMetadataKeys = new List<string>();
			Dictionary<string, SummaryMetadataColumn> nameLookup = new Dictionary<string, SummaryMetadataColumn>();
			foreach (SummaryMetadataColumn col in newTable.columns)
			{
				string key = col.name.ToLower();
				if (!nameLookup.ContainsKey(key))
				{
					nameLookup.Add(key, col);
					allMetadataKeys.Add(key);
				}
			}
			allMetadataKeys.Sort();

			// Generate the list of requested metadata keys that this table includes
			List<string> orderedKeysWithDupes = new List<string>();

			// Add metadata keys from the column filter list in the order they appear
			foreach (string filterStr in columnFilterList)
			{
				string filterStrLower = filterStr.Trim().ToLower();
				// Find all matching
				if (filterStrLower.EndsWith("*"))
				{
					string prefix = filterStrLower.Trim('*');
					// Linear search through the sorted key list
					bool bFound = false;
					for (int wildcardSearchIndex = 0; wildcardSearchIndex < allMetadataKeys.Count; wildcardSearchIndex++)
					{
						if (allMetadataKeys[wildcardSearchIndex].StartsWith(prefix))
						{
							orderedKeysWithDupes.Add(allMetadataKeys[wildcardSearchIndex]);
							bFound = true;
						}
						else if (bFound)
						{
							// Early exit: already found one key. If the pattern no longer matches then we must be done
							break;
						}
					}
				}
				else
				{
					string key = filterStrLower;
					orderedKeysWithDupes.Add(key);
				}
			}

			List<SummaryMetadataColumn> newColumnList = new List<SummaryMetadataColumn>();
			// Add all the ordered keys that exist, ignoring duplicates
			foreach (string key in orderedKeysWithDupes)
			{
				if (nameLookup.ContainsKey(key))
				{
					newColumnList.Add(nameLookup[key]);
					// Remove from the list so it doesn't get counted again
					nameLookup.Remove(key);
				}
			}



			newTable.columns = newColumnList;
			newTable.rowCount = rowCount;
			newTable.InitColumnLookup();
			return newTable;
		}

		public void ApplyDisplayNameMapping(Dictionary<string, string> statDisplaynameMapping)
		{
			// Convert to a display-friendly name
			foreach (SummaryMetadataColumn column in columns)
			{
				if (statDisplaynameMapping != null && column.displayName == null )
				{
					string name = column.name;
					string suffix = "";
					string prefix = "";
					string statName = GetStatNameWithPrefixAndSuffix(name, out prefix, out suffix);
					if (statDisplaynameMapping.ContainsKey(statName.ToLower()))
					{
						column.displayName = prefix + statDisplaynameMapping[statName.ToLower()] + suffix;
					}
				}
			}
		}

		string GetStatNameWithoutPrefixAndSuffix(string inName)
		{
			string suffix = "";
			string prefix = "";
			return GetStatNameWithPrefixAndSuffix(inName, out prefix, out suffix);
		}

		string GetStatNameWithPrefixAndSuffix(string inName, out string prefix, out string suffix)
		{
			suffix = "";
			prefix = "";
			string statName = inName;
			if (inName.StartsWith("Avg ") || inName.StartsWith("Max ") || inName.StartsWith("Min "))
			{
				prefix = inName.Substring(0, 4);
				statName = inName.Substring(4);
			}
			if (statName.EndsWith(" Avg") || statName.EndsWith(" Max") || statName.EndsWith(" Min"))
			{
				suffix = statName.Substring(statName.Length - 4);
				statName = statName.Substring(0, statName.Length - 4);
			}
			return statName;
		}

		public void WriteToCSV(string csvFilename)
		{
			System.IO.StreamWriter csvFile = new System.IO.StreamWriter(csvFilename, false);
			List<string> headerRow = new List<string>(); 
			foreach (SummaryMetadataColumn column in columns)
			{
				headerRow.Add(column.name);
			}
			csvFile.WriteLine(string.Join(",", headerRow));

			for (int i = 0; i < rowCount; i++)
			{
				List<string> rowStrings= new List<string>();
				foreach (SummaryMetadataColumn column in columns)
				{
					string cell = column.GetStringValue(i, false);
					// Sanitize so it opens in a spreadsheet (e.g. for buildversion) 
					cell=cell.TrimStart('+');
					rowStrings.Add( cell );
				}
				csvFile.WriteLine(string.Join(",", rowStrings));
			}
			csvFile.Close();
		}

		private void AutoColorizeColumns(string[] summaryTableLowIsBadStatList)
		{
			foreach (SummaryMetadataColumn column in columns)
			{
				if (column.isNumeric)
				{
					bool highIsRed = true;
					if (summaryTableLowIsBadStatList != null)
					{
						// Determine if this is a low is red column
						string lowerColumnName = column.name.ToLower();
						if (lowerColumnName.StartsWith("avg ") || lowerColumnName.StartsWith("min ") || lowerColumnName.StartsWith("max "))
						{
							lowerColumnName = lowerColumnName.Substring(4);
						}
						foreach (string entry in summaryTableLowIsBadStatList)
						{
							string lowerEntry = entry.ToLower();
							int wildcardIndex = lowerEntry.IndexOf('*');
							if (wildcardIndex == -1)
							{
								if (lowerEntry == lowerColumnName)
								{
									highIsRed = false;
									break;
								}
							}
							else
							{
								string prefix = lowerEntry.Substring(0, wildcardIndex);
								if (lowerColumnName.StartsWith(prefix))
								{
									highIsRed = false;
									break;
								}
							}
						}
					}
					column.ComputeAutomaticColourThresholds(highIsRed);
				}
			}
		}

		public void WriteToHTML(string htmlFilename, string VersionString, bool bSpreadsheetFriendlyStrings, SummarySectionBoundaryInfo sectionBoundaryInfo, bool bScrollableTable, bool bAddMinMaxColumns, int maxColumnStringLength, string [] summaryTableLowIsBadStatList)
		{
			System.IO.StreamWriter htmlFile = new System.IO.StreamWriter(htmlFilename, false);
			int statColSpan = hasMinMaxColumns ? 3 : 1;
			int cellPadding = 2;
			if (isCollated)
			{
				cellPadding = 4;
			}

			htmlFile.WriteLine("<html>");
			htmlFile.WriteLine("<head><title>Performance summary</title>");
			//htmlFile.WriteLine("table, th, td { border: 0px solid black; border-collapse: separate; padding: 3px; vertical-align: top; font-family: 'Verdana', Times, serif; font-size: 12px;}");
			htmlFile.WriteLine("<style type='text/css'>");
			htmlFile.WriteLine("p {  font-family: 'Verdana', Times, serif; font-size: 12px }");
			htmlFile.WriteLine("h3 {  font-family: 'Verdana', Times, serif; font-size: 14px }");
			htmlFile.WriteLine("h2 {  font-family: 'Verdana', Times, serif; font-size: 16px }");
			htmlFile.WriteLine("h1 {  font-family: 'Verdana', Times, serif; font-size: 20px }");
			string tableCss = "";
			int frozenColumnCount = 0;
			if (bScrollableTable)
			{
				int firstColMaxStringLength = 0;
				if (columns.Count>0)
				{
					for (int i=0; i<columns[0].GetCount(); i++)
					{
						firstColMaxStringLength = Math.Max(firstColMaxStringLength,columns[0].GetStringValue(i).Length);
					}
				}
				frozenColumnCount = 1;
				// Freeze the second column if it's the "count" column
				if (columns.Count>1 && columns[1].name=="Count" && isCollated)
				{
					frozenColumnCount = 2;
				}
				int firstColWidth = firstColMaxStringLength * 7;
				tableCss =
					"table {table-layout: fixed;}"+
					"table, th, td { border: 0px solid black; border-spacing: 0; border-collapse: separate; padding: " + cellPadding + "px; vertical-align: center; font-family: 'Verdana', Times, serif; font-size: 10px;}" +
					"td {" +
					"  border-right: 1px solid black;"+
					"  max-width: 400;" +
					"}" +
					"tr:first-element { border-top: 2px; border-bottom: 2px }"+
					"th {" +
					"  width: 75px;" +
					"  max-width: 400;" +
					"  position: -webkit-sticky;" +
					"  position: sticky;" +
					"  border-right: 1px solid black;" +
					"  border-top: 2px solid black;" +
					"  z-index: 5;" +
					"  background-color: #ffffff;" +
					"  top:0;" +
					"  font-size: 9px;" +
					"  word-wrap: break-word;" +
					"  overflow: hidden;" +
					"  height: 60;" +
					"}";

				int xPos = 0;
				for (int i=0;i< frozenColumnCount; i++)
				{
					int colIndex = i + 1;
					tableCss +=
						"th:nth-child("+colIndex+") {" +
						"  position: -webkit-sticky;" +
						"  position: sticky;" +
						"  z-index: 7;" +
						"  background-color: #ffffff;" +
						"  border-right: 2px solid black;" +
						"  border-top: 2px solid black;" +
						"  font-size: 11px;" +
						"  top:0;" +
						"  left: " + xPos + "px;" +
						"}";

					tableCss +=
						"td:nth-child("+colIndex+") {" +
						"  position: -webkit-sticky;" +
						"  position: sticky;" +
						"  border-right: 2px solid black;" +
						"  z-index: 6;" +
						"  left: " + xPos + "px;" +
						" }";

					xPos += firstColWidth + 4 + cellPadding*2;
				}

				string firstChildCSS =
				tableCss +=
					"th:first-child, td:first-child {" +
					"  border-left: 2px solid black;" +
					"  width: " + firstColWidth + ";" +
					"  min-width: " + firstColWidth + ";" +
					"  max-width: " + firstColWidth + ";" +
					" }" +

					"tr.sectionStart td {" +
					"  border-top: 2px solid black;" +
					"}";

				if (bAddMinMaxColumns && isCollated)
				{
					tableCss += "tr.lastHeaderRow th { top:60px; height:20px; }";
				}
				if (bAddMinMaxColumns && isCollated)
				{
					// The top left cell is merged, so make sure the one to the right is not sticky horizontally
					tableCss += "tr:first-child th:nth-child(2) { left:auto; z-index:5; } ";
				}

				if (!isCollated)
				{
					tableCss += "td { max-height: 40px; height:40px } ";
				}
				tableCss += "tr:last-child td{border-bottom: 2px solid black;} ";

			}
			else 
			{
				tableCss =
					"table, th, td { border: 2px solid black; border-collapse: collapse; padding: "+ cellPadding + "px; vertical-align: center; font-family: 'Verdana', Times, serif; font-size: 11px;}";
			}


			bool bOddRowsGray = !(!bAddMinMaxColumns || !isCollated);
			tableCss += "tr:nth-child("+ (bOddRowsGray ? "odd" : "even") + ") {background-color: #e2e2e2;} ";
			tableCss += "tr:nth-child("+ (bOddRowsGray ? "even" : "odd") + ") {background-color: #ffffff;} ";
			tableCss += "tr:first-child {background-color: #ffffff;} ";
			tableCss += "tr.lastHeaderRow th { border-bottom: 2px solid black; }";

			htmlFile.WriteLine(tableCss);

			htmlFile.WriteLine("</style>");
			htmlFile.WriteLine("</head><body>");
			htmlFile.WriteLine("<table>");

			// Automatically colourize the table
			if (bScrollableTable)
			{
				AutoColorizeColumns(summaryTableLowIsBadStatList);
			}

			string HeaderRow = "";
			if (isCollated)
			{
				string TopHeaderRow = "";
				if (bScrollableTable)
				{
					// Generate an automatic title
					string title = htmlFilename.Replace("_Email.html", "").Replace(".html", "").Replace("\\","/");
					title = title.Substring(title.LastIndexOf('/') + 1);
					TopHeaderRow += "<th colspan='"+ firstStatColumnIndex + "'><h3>"+title+"</h3></th>";
				}
				else
				{
					TopHeaderRow += "<th colspan='"+firstStatColumnIndex+"'/>";
				}

				for (int i = 0; i < firstStatColumnIndex; i++)
				{
					HeaderRow += "<th>" + columns[i].GetDisplayName() + "</th>";
				}
				if (!bAddMinMaxColumns)
				{
					TopHeaderRow = HeaderRow;
				}

				for (int i = firstStatColumnIndex; i < columns.Count; i++)
				{
					string prefix = "";
					string suffix = "";
					string statName = GetStatNameWithPrefixAndSuffix(columns[i].GetDisplayName(), out prefix, out suffix);
					if ((i - 1) % statColSpan == 0)
					{
						TopHeaderRow += "<th colspan='"+statColSpan+"' >" + statName + suffix + "</th>";
					}
					HeaderRow += "<th>" + prefix.Trim() + "</th>";
				}
				if (bAddMinMaxColumns)
				{
					htmlFile.WriteLine("  <tr>" + TopHeaderRow + "</tr>");
					htmlFile.WriteLine("  <tr class='lastHeaderRow'>" + HeaderRow + "</tr>");
				}
				else
				{
					htmlFile.WriteLine("  <tr class='lastHeaderRow'>" + TopHeaderRow + "</tr>");
				}
			}
			else
			{
				foreach (SummaryMetadataColumn column in columns)
				{
					HeaderRow += "<th>" + column.GetDisplayName() + "</th>";
				}
				htmlFile.WriteLine("  <tr class='lastHeaderRow'>" + HeaderRow + "</tr>");
			}
			string[] stripeColors = { "'#e2e2e2'", "'#ffffff'" };

			string prevSectionName = "";
			for ( int i=0; i<rowCount; i++)
			{
				bool sectionStart = false;
				if (sectionBoundaryInfo != null)
				{
					// Work out the section name if we have section boundary info. When it changes, apply the sectionStart CSS class
					string sectionName = "";
					if (sectionBoundaryInfo != null && columnLookup.ContainsKey(sectionBoundaryInfo.statName))
					{
						SummaryMetadataColumn col = columnLookup[sectionBoundaryInfo.statName];
						string currentValue = col.GetStringValue(i);
						// Only use the start and end tokens if the table is collated
						if (isCollated)
						{
							int startTokenIndex = currentValue.IndexOf(sectionBoundaryInfo.startToken);
							int endTokenIndex = currentValue.IndexOf(sectionBoundaryInfo.endToken);
							if (startTokenIndex >= 0 && endTokenIndex > startTokenIndex)
							{
								sectionName = currentValue.Substring(startTokenIndex + sectionBoundaryInfo.startToken.Length, endTokenIndex - sectionBoundaryInfo.startToken.Length);
							}
						}
						else
						{
							sectionName = currentValue;
						}
					}
					if (sectionName != prevSectionName && i>0)
					{
						sectionStart = true;
					}
					prevSectionName = sectionName;
				}

				string rowClassStr = "";
				if (sectionStart)
				{
					rowClassStr = " class='sectionStart'";
				}

				htmlFile.Write("<tr"+rowClassStr+">");
				int columnIndex = 0;
				foreach (SummaryMetadataColumn column in columns)
				{
					// Add the tooltip for non-collated tables
					string toolTipString = "";
					if (!isCollated)
					{
						string toolTip = column.GetToolTipValue(i);
						if (toolTip=="")
						{
							toolTip = column.GetDisplayName();
						}
						toolTipString = "title = '" + toolTip + "'";
					}
					string colour = column.GetColour(i);

					// Alternating row colours are normally handled by CSS, but we need to handle it explicitly if we have frozen first columns
					if (columnIndex < frozenColumnCount && colour == null)
					{
						colour = stripeColors[i % 2];
					}
					string bgColorString = (colour==null ? "" : " bgcolor = " + colour);
					bool bold = false;
					string stringValue = column.GetStringValue(i, true);
					if (maxColumnStringLength > 0 && stringValue.Length > maxColumnStringLength)
					{
						stringValue = TableUtil.SafeTruncateHtmlTableValue(stringValue, maxColumnStringLength);
					}
					if (bSpreadsheetFriendlyStrings && !column.isNumeric)
					{
						stringValue = "'" + stringValue;
					}
					string columnString = "<td "+ toolTipString + bgColorString+"> " + (bold ? "<b>" : "") + stringValue + (bold ? "</b>" : "") + "</td>";
					htmlFile.Write(columnString);
					columnIndex++;
				}
				htmlFile.WriteLine("</tr>");
			}
			htmlFile.WriteLine("</table>");
			htmlFile.WriteLine("<p style='font-size:8'>Created with PerfReportTool " + VersionString + "</p>");
			htmlFile.WriteLine("</font></body></html>");

			htmlFile.Close();
        }

		SummaryMetadataTable SortRows(List<string> rowSortList, bool reverseSort)
		{
			List<KeyValuePair<string, int>> columnRemapping = new List<KeyValuePair<string, int>>();
			for (int i = 0; i < rowCount; i++)
			{
				string key = "";
				foreach (string s in rowSortList)
				{
                    if (columnLookup.ContainsKey(s.ToLower()))
                    {
                        SummaryMetadataColumn column = columnLookup[s.ToLower()];
                        key += "{" + column.GetStringValue(i) + "}";
                    }
                    else
                    {
                        key += "{}";
                    }
				}
				columnRemapping.Add(new KeyValuePair<string, int>(key, i));
			}

			columnRemapping.Sort(delegate (KeyValuePair<string, int> m1, KeyValuePair<string, int> m2)
			{
				return m1.Key.CompareTo(m2.Key);
			});

			// Reorder the metadata rows
			List<SummaryMetadataColumn> newColumns = new List<SummaryMetadataColumn>();
			foreach (SummaryMetadataColumn srcCol in columns)
			{
				SummaryMetadataColumn destCol = new SummaryMetadataColumn(srcCol.name, srcCol.isNumeric);
				for (int i = 0; i < rowCount; i++)
				{
					int srcIndex = columnRemapping[i].Value;
					int destIndex = reverseSort ? rowCount-1-i : i;
					if (srcCol.isNumeric)
					{
						destCol.SetValue(destIndex, srcCol.GetValue(srcIndex));
					}
					else
					{
						destCol.SetStringValue(destIndex, srcCol.GetStringValue(srcIndex));
					}
					destCol.SetColourThresholds(destIndex, srcCol.GetColourThresholds(srcIndex));
					destCol.SetToolTipValue(destIndex, srcCol.GetToolTipValue(srcIndex));
				}
				newColumns.Add(destCol);
			}
			SummaryMetadataTable newTable = new SummaryMetadataTable();
			newTable.columns = newColumns;
			newTable.rowCount = rowCount;
			newTable.firstStatColumnIndex = firstStatColumnIndex;
			newTable.isCollated = isCollated;
			newTable.InitColumnLookup();
			return newTable;
		}

		void InitColumnLookup()
		{
			columnLookup.Clear();
			foreach (SummaryMetadataColumn col in columns)
			{
				columnLookup.Add(col.name.ToLower(), col);
			}
		}
	
		public void AddMetadata(SummaryMetadata metadata, bool bIncludeCsvStatAverages, bool bIncludeHiddenStats)
		{
			foreach (string key in metadata.dict.Keys)
			{
				SummaryMetadataValue value = metadata.dict[key];
				if ( value.type == SummaryMetadataValue.Type.CsvStatAverage && !bIncludeCsvStatAverages )
				{
					continue;
				}
				if ( value.GetFlag(SummaryMetadataValue.Flags.Hidden) && !bIncludeHiddenStats)
				{
					continue;
				}
				SummaryMetadataColumn column = null;

				if (!columnLookup.ContainsKey(key))
				{
					column = new SummaryMetadataColumn(value.name, value.isNumeric);
					columnLookup.Add(key, column);
					columns.Add(column);
				}
				else
				{
					column = columnLookup[key];
				}

				if (value.isNumeric)
				{
					column.SetValue(rowCount, (float)value.numericValue);
				}
				else
				{
					column.SetStringValue(rowCount, value.value);
				}
				column.SetColourThresholds(rowCount, value.colorThresholdList);
				column.SetToolTipValue(rowCount, value.tooltip);
			}
			rowCount++;
		}

		public int Count
		{
			get { return rowCount; }
		}

		Dictionary<string, SummaryMetadataColumn> columnLookup = new Dictionary<string, SummaryMetadataColumn>();
		List<SummaryMetadataColumn> columns = new List<SummaryMetadataColumn>();
		int rowCount = 0;
		int firstStatColumnIndex = 0;
		bool isCollated = false;
		bool hasMinMaxColumns = false;
    };

}
