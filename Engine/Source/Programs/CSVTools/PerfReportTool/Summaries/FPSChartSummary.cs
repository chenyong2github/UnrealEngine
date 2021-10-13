// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Collections.Generic;
using System.Linq;
using System.Xml.Linq;
using PerfReportTool;
using CSVStats;

namespace PerfSummaries
{
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

		public FPSChartSummary() { }

		public override string GetName() { return "fpschart"; }

		float GetEngineHitchToNonHitchRatio()
		{
			float MinimumRatio = 1.0f;
			float targetFrameTime = 1000.0f / fps;
			float MaximumRatio = hitchThreshold / targetFrameTime;

			return Math.Min(Math.Max(engineHitchToNonHitchRatio, MinimumRatio), MaximumRatio);
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

				for (int i = 0; i < frameCount; i++)
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

			// subtract hitch threshold to weight larger hitches
			totalHitchTime -= (hitchCount * hitchThreshold);
			outData.HitchTimePercent = (float)(totalHitchTime / totalFrametime) * 100.0f;

			int TotalTargetFrames = (int)((double)fps * (TotalSeconds));
			int MissedFrames = Math.Max(TotalTargetFrames - frameTimes.Count, 0);
			outData.MVP = (((float)MissedFrames * 100.0f) / (float)TotalTargetFrames);
			return outData;
		}


		public override void WriteSummaryData(System.IO.StreamWriter htmlFile, CsvStats csvStats, bool bWriteSummaryCsv, SummaryTableRowData rowData, string htmlFileName)
		{
			System.IO.StreamWriter statsCsvFile = null;
			if (bWriteSummaryCsv)
			{
				string csvPath = Path.Combine(Path.GetDirectoryName(htmlFileName), "FrameStats_colored.csv");
				statsCsvFile = new System.IO.StreamWriter(csvPath, false);
			}
			// Compute MVP30 and MVP60. Note: we ignore the last frame because fpscharts can hitch
			List<float> frameTimes = csvStats.Stats["frametime"].samples;
			FpsChartData fpsChartData = ComputeFPSChartDataForFrames(frameTimes, true);

			// Write the averages
			List<string> ColumnNames = new List<string>();
			List<double> ColumnValues = new List<double>();
			List<string> ColumnColors = new List<string>();
			List<ColourThresholdList> ColumnColorThresholds = new List<ColourThresholdList>();

			ColumnNames.Add("Total Time (s)");
			ColumnColorThresholds.Add(new ColourThresholdList());
			ColumnValues.Add(fpsChartData.TotalTimeSeconds);
			ColumnColors.Add(ColourThresholdList.GetSafeColourForValue(ColumnColorThresholds.Last(), ColumnValues.Last()));

			ColumnNames.Add("Hitches/Min");
			ColumnColorThresholds.Add(GetStatColourThresholdList(ColumnNames.Last()));
			ColumnValues.Add(fpsChartData.HitchesPerMinute);
			ColumnColors.Add(ColourThresholdList.GetSafeColourForValue(ColumnColorThresholds.Last(), ColumnValues.Last()));

			if (!bIgnoreHitchTimePercent)
			{
				ColumnNames.Add("HitchTimePercent");
				ColumnColorThresholds.Add(GetStatColourThresholdList(ColumnNames.Last()));
				ColumnValues.Add(fpsChartData.HitchTimePercent);
				ColumnColors.Add(ColourThresholdList.GetSafeColourForValue(ColumnColorThresholds.Last(), ColumnValues.Last()));
			}

			if (!bIgnoreMVP)
			{
				ColumnNames.Add("MVP" + fps.ToString());
				ColumnColorThresholds.Add(GetStatColourThresholdList(ColumnNames.Last()));
				ColumnValues.Add(fpsChartData.MVP);
				ColumnColors.Add(ColourThresholdList.GetSafeColourForValue(ColumnColorThresholds.Last(), ColumnValues.Last()));
			}

			List<bool> ColumnIsAvgValueList = new List<bool>();
			for (int i = 0; i < ColumnNames.Count; i++)
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
				ColumnValues.Add(value);
				ColumnColorThresholds.Add(GetStatColourThresholdList(statName));
				ColumnColors.Add(ColourThresholdList.GetSafeColourForValue(ColumnColorThresholds.Last(), ColumnValues.Last()));
			}

			// Output summary table row data
			if (rowData != null)
			{
				for (int i = 0; i < ColumnNames.Count; i++)
				{
					string columnName = ColumnNames[i];

					// Output simply MVP to rowData instead of MVP30 etc
					if (columnName.StartsWith("MVP"))
					{
						columnName = "MVP";
					}
					// Hide pre-existing stats with the same name
					if (ColumnIsAvgValueList[i] && columnName.EndsWith(" Avg"))
					{
						string originalStatName = columnName.Substring(0, columnName.Length - 4).ToLower();
						SummaryTableElement smv;
						if (rowData.dict.TryGetValue(originalStatName, out smv))
						{
							if (smv.type == SummaryTableElement.Type.CsvStatAverage)
							{
								smv.SetFlag(SummaryTableElement.Flags.Hidden, true);
							}
						}
					}
					rowData.Add(SummaryTableElement.Type.SummaryTableMetric, columnName, ColumnValues[i], ColumnColorThresholds[i]);
				}
				rowData.Add(SummaryTableElement.Type.SummaryTableMetric, "TargetFPS", (double)fps);
			}

			// Output HTML
			if (htmlFile != null)
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
					ValueRow += "<td bgcolor=" + ColumnColors[i] + ">" + ColumnValues[i].ToString("0.00") + "</td>";
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
					FpsChartData captureFpsChartData = ComputeFPSChartDataForFrames(CaptureFrameTimes.Frames, true);

					if (captureFpsChartData.TotalTimeSeconds == 0.0f)
					{
						continue;
					}

					ColumnValues.Add(captureFpsChartData.TotalTimeSeconds);
					ColumnColors.Add("\'#ffffff\'");

					ColumnValues.Add(captureFpsChartData.HitchesPerMinute);
					ColumnColors.Add(GetStatThresholdColour("Hitches/Min", captureFpsChartData.HitchesPerMinute));

					if (!bIgnoreHitchTimePercent)
					{
						ColumnValues.Add(captureFpsChartData.HitchTimePercent);
						ColumnColors.Add(GetStatThresholdColour("HitchTimePercent", captureFpsChartData.HitchTimePercent));
					}

					if (!bIgnoreMVP)
					{
						ColumnValues.Add(captureFpsChartData.MVP);
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

						ColumnValues.Add(value);
						ColumnColors.Add(GetStatThresholdColour(statName, value));
					}

					// Output HTML
					if (htmlFile != null)
					{
						string ValueRow = "";
						ValueRow += "<td>" + CapRange.name + "</td>";
						for (int i = 0; i < ColumnNames.Count; i++)
						{
							ValueRow += "<td bgcolor=" + ColumnColors[i] + ">" + ColumnValues[i].ToString("0.00") + "</td>";
						}
						htmlFile.WriteLine("  <tr>" + ValueRow + "</tr>");
					}

					// Output CSV
					if (statsCsvFile != null)
					{
						statsCsvFile.Write(CapRange.name + ",");
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

}