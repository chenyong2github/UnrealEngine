// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Xml.Linq;
using PerfReportTool;
using System.IO;
using CSVStats;

namespace PerfSummaries
{
	class ExtraLinksSummary : Summary
	{
		class ExtraLink
		{
			public ExtraLink(string fileLine)
			{
				string[] Sections = fileLine.Split(',');
				if (Sections.Length != 3)
				{
					throw new Exception("Bad links line format: " + fileLine);
				}
				LongName = Sections[0];
				ShortName = Sections[1];
				LinkURL = Sections[2];
			}
			public string GetLinkString(bool bUseLongName)
			{
				string Text = bUseLongName ? LongName : ShortName;
				return "<a href='" + LinkURL + "'>" + Text + "</a>";
			}
			public string LongName;
			public string ShortName;
			public string LinkURL;
		};

		public ExtraLinksSummary(XElement element, string baseXmlDirectory)
		{
			title = "Links";
			if (element != null)
			{
				title = element.GetSafeAttibute("title", title);
			}
		}
		public ExtraLinksSummary() { }

		public override string GetName() { return "extralinks"; }

		public override void WriteSummaryData(System.IO.StreamWriter htmlFile, CsvStats csvStats, bool bWriteSummaryCsv, SummaryTableRowData rowData, string htmlFileName)
		{
			List<ExtraLink> links = new List<ExtraLink>();

			string csvFilename = csvStats.metaData.GetValue("csvfilename", null);
			if (csvFilename == null)
			{
				Console.WriteLine("Can't find CSV filename for ExtraLinks summary. Skipping");
				return;
			}

			string linksFilename = csvFilename + ".links";
			if (!File.Exists(linksFilename))
			{
				Console.WriteLine("Can't find file " + linksFilename + " for ExtraLinks summary. Skipping");
				return;
			}
			string[] lines = File.ReadAllLines(linksFilename);
			foreach (string line in lines)
			{
				links.Add(new ExtraLink(line));
			}
			if (links.Count == 0)
			{
				return;
			}

			// Output HTML
			if (htmlFile != null)
			{
				htmlFile.WriteLine("  <h2>" + title + "</h2>");
				htmlFile.WriteLine("  <ul>");
				foreach (ExtraLink link in links)
				{
					htmlFile.WriteLine("  <li>" + link.GetLinkString(true) + "</li>");
				}
				htmlFile.WriteLine("  </ul>");
			}

			// Output summary row data
			if (rowData != null)
			{
				foreach (ExtraLink link in links)
				{
					rowData.Add(SummaryTableElement.Type.SummaryTableMetric, link.LongName, link.GetLinkString(false), null);
				}
			}
		}
		public override void PostInit(ReportTypeInfo reportTypeInfo, CsvStats csvStats)
		{
		}
		string title;
	};

}