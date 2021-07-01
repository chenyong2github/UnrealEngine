// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Xml.Linq;
using PerfReportTool;
using CSVStats;

namespace PerfSummaries
{
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

		public HistogramSummary() { }

		public override string GetName() { return "histogram"; }

		public override void WriteSummaryData(System.IO.StreamWriter htmlFile, CsvStats csvStats, bool bWriteSummaryCsv, SummaryTableRowData metadata, string htmlFileName)
		{
			// Only HTML reporting is supported (does not output summary table row data)
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

}