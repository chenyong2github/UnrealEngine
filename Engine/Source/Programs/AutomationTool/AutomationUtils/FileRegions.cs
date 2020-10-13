// Copyright Epic Games, Inc. All Rights Reserved.

using System.Collections.Generic;
using System.Linq;
using System.IO;
using System;

namespace AutomationTool
{
	/// <summary>
	/// Describes the way regions of cooked data should be rearranged to achieve higher compression ratios.
	/// NOTE: Enum values here must match those in Serialization/FileRegions.h
	/// </summary>
	public enum DataShufflePattern : byte
	{
		None = 0,

		// 8 Byte Vectors
		Pattern_44 = 1,
		Pattern_224 = 2,
		Pattern_116 = 3,
		Pattern_11111111 = 4,

		// 16 Byte Vectors
		Pattern_8224 = 5,
		Pattern_116224 = 6,
		Pattern_116116 = 7,
		Pattern_4444 = 8
	}

	public struct FileRegion
	{
		public const string RegionsFileExtension = ".uregs";

		public ulong Offset;
		public ulong Length;
		public DataShufflePattern Pattern;

		public static List<FileRegion> ReadRegionsFromFile(string Filename)
		{
			// This serialization function must match FFileRegion::SerializeFileRegions in Serialization/FileRegions.cpp
			try
			{
				using (var Reader = new BinaryReader(File.OpenRead(Filename)))
				{
					int NumRegions = Reader.ReadInt32();
					var Results = new List<FileRegion>(NumRegions);
					for (int Index = 0; Index < NumRegions; ++Index)
					{
						FileRegion Region;
						Region.Offset = Reader.ReadUInt64();
						Region.Length = Reader.ReadUInt64();
						Region.Pattern = (DataShufflePattern)Reader.ReadByte();
						Results.Add(Region);
					}

					return Results;
				}
			}
			catch (Exception Ex)
			{
				throw new AutomationException(Ex, "Failed to read regions from file \"{0}\".", Filename);
			}
		}

		public static void PrintSummaryTable(IEnumerable<FileRegion> Regions)
        {
			var RegionsByPattern = (from Region in Regions group Region by Region.Pattern).ToDictionary(g => g.Key, g => g.ToList());

			CommandUtils.LogInformation(" +-------------------+----------------------------------+---------------+");
			CommandUtils.LogInformation(" |      Pattern      |           Total Length           |  Num Regions  |");
			CommandUtils.LogInformation(" +-------------------+----------------------------------+---------------+");
			foreach (var Pair in RegionsByPattern)
			{
				// Find the total number of bytes covered by regions with this pattern
				long TotalSizeInBytes = Pair.Value.Sum(r => (long)r.Length);

				var Suffixes = new[] { "  B", "KiB", "MiB", "GiB", "TiB" };
				int Suffix = 0;

				double TotalSizeInUnits = TotalSizeInBytes;
				while (TotalSizeInUnits > 1024 && (Suffix + 1) < Suffixes.Length)
				{
					TotalSizeInUnits = (double)TotalSizeInBytes / (1024L << (Suffix * 10));
					Suffix++;
				}

				CommandUtils.LogInformation(" | {0,-17} | {1,18:#,##} ({2,7:0.00} {3}) | {4,13:#,##} |",
					Pair.Key.ToString().ToUpperInvariant(),
					TotalSizeInBytes,
					TotalSizeInUnits,
					Suffixes[Suffix],
					Pair.Value.Count);
			}
			CommandUtils.LogInformation(" +-------------------+----------------------------------+---------------+");
		}
	}
}
