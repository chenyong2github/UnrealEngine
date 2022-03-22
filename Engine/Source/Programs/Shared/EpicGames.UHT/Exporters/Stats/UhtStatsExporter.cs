using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.UHT.Exporters.Stats
{
	[UnrealHeaderTool]
	internal class UhtStatsExporter
	{
		[UhtExporter(Name = "Stats", Description = "Type Stats", Options = UhtExporterOptions.None)]
		private static void StatsExporter(IUhtExportFactory Factory)
		{
			SortedDictionary<string, int> CountByType = new SortedDictionary<string, int>();
			foreach (UhtType Type in Factory.Session.Packages)
			{
				Collect(CountByType, Type);
			}

			Log.TraceInformation("Counts by type:");

			foreach (KeyValuePair<string, int> Kvp in CountByType)
			{
				Log.TraceInformation($"{Kvp.Key} {Kvp.Value}");
			}
			Log.TraceInformation("");
		}

		private static void Collect(SortedDictionary<string, int> CountByType, UhtType Type)
		{
			int Count = 1;
			if (CountByType.TryGetValue(Type.EngineClassName, out Count))
			{
				CountByType[Type.EngineClassName] = Count + 1;
			}
			else
			{
				CountByType[Type.EngineClassName] = 1;
			}

			foreach (UhtType Child in Type.Children)
			{
				Collect(CountByType, Child);
			}
		}
	}
}
