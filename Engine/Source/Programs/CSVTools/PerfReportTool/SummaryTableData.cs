// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Xml.Linq;
using PerfReportTool;
using System.IO;
using System.Text.Json;

namespace PerfSummaries
{

	class SummaryTableDataJsonHelper
	{
		public SummaryTableDataJsonHelper(string InJsonFilename, bool bInCsvMetadataOnly, bool bInWriteAllElementData)
		{
			JsonFilename = InJsonFilename;
			bCsvMetadataOnly = bInCsvMetadataOnly;
			bWriteAllElementData = bInWriteAllElementData;
		}
		public void AddRowData(SummaryTableRowData rowData)
		{
			if (!rowData.dict.ContainsKey("csvid"))
			{
				Console.WriteLine("Warning: SummaryTableDataJsonHelper.AddRowData - Row data contains no CSV ID! Skipping");
				return;
			}

			Dict.Add(rowData.dict["csvid"].value, rowData.ToJsonDict(bCsvMetadataOnly, bWriteAllElementData));
		}

		public void WriteJsonFile()
		{
			Console.WriteLine("Writing summary table row data to json: " + JsonFilename);
			JsonSerializerOptions options = new JsonSerializerOptions { WriteIndented = true };
			string jsonString = JsonSerializer.Serialize(Dict, options);

			// Write the file to Json and rename it when we're done
			File.WriteAllText(JsonFilename, jsonString);
		}

		Dictionary<string, dynamic> Dict = new Dictionary<string, dynamic>();
		string JsonFilename;
		bool bCsvMetadataOnly;
		bool bWriteAllElementData;
	}



	class SummaryTableElement
	{
		// Bump this when making changes!
		public static int CacheVersion = 1;

		// NOTE: this is serialized. Don't change the order!
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

		private SummaryTableElement()
		{

		}
		public SummaryTableElement(Type inType, string inName, double inValue, ColourThresholdList inColorThresholdList, string inToolTip, uint inFlags = 0)
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
		public SummaryTableElement(Type inType, string inName, string inValue, ColourThresholdList inColorThresholdList, string inToolTip, uint inFlags = 0)
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

		public static SummaryTableElement ReadFromCache(BinaryReader reader)
		{
			SummaryTableElement val = new SummaryTableElement();
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

		public SummaryTableElement Clone()
		{
			return (SummaryTableElement)MemberwiseClone();
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

		public Dictionary<string, dynamic> ToJsonDict(bool bWriteType)
		{
			Dictionary<string, dynamic> Dict = new Dictionary<string, dynamic>();
			if (bWriteType)
			{
				Dict.Add("type", type.ToString());
			}
			Dict.Add("value", DynamicValue);

			if (!string.IsNullOrEmpty(tooltip))
			{
				Dict.Add("tooltip", tooltip);
			}
			if ( colorThresholdList != null)
			{
				Dict.Add("colorThresholdList", colorThresholdList.ToJsonDict());
			}
			List<string> FlagStrings = new List<string>();
			var FlagValues = Enum.GetValues(typeof(Flags));
			foreach (var FlagValue in FlagValues)
			{
				if ( (flags & (uint)(int)FlagValue) != 0)
				{
					FlagStrings.Add(FlagValue.ToString());
				}
			}
			if (FlagStrings.Count > 0)
			{
				Dict.Add("flags", FlagStrings);
			}
			return Dict;
		}
		public dynamic DynamicValue
		{
			get
			{
				if (isNumeric)
				{
					return numericValue;
				}
				return value;
			}
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
	class SummaryTableRowData
	{
		public SummaryTableRowData()
		{
		}

		// TODO: If this is bumped beyond 6, we need to implement backwards compatibility
		static int CacheVersion = 6;

		public static SummaryTableRowData TryReadFromCache(string summaryTableCacheDir, string csvId)
		{
			string filename = Path.Combine(summaryTableCacheDir, csvId + ".prc");
			return TryReadFromCacheFile(filename);
		}

		public static SummaryTableRowData TryReadFromCacheFile(string filename, bool bReadJustInitialMetadata = false)
		{
			SummaryTableRowData metaData = null;
			if (!File.Exists(filename))
			{
				return null;
			}
			try
			{
				using (FileStream fileStream = new FileStream(filename, FileMode.Open, FileAccess.Read))
				{
					BinaryReader reader = new BinaryReader(fileStream);
					int version = reader.ReadInt32();
					int metadataValueVersion = reader.ReadInt32();
					if (version == CacheVersion && metadataValueVersion == SummaryTableElement.CacheVersion)
					{
						bool bEarlyOut = false;
						metaData = new SummaryTableRowData();
						int dictEntryCount = reader.ReadInt32();
						for (int i = 0; i < dictEntryCount; i++)
						{
							string key = reader.ReadString();
							SummaryTableElement value = SummaryTableElement.ReadFromCache(reader);
							// If we're just reading initial metadata then skip everything after ToolMetadata and CsvMetadata
							if (bReadJustInitialMetadata && value.type != SummaryTableElement.Type.ToolMetadata && value.type != SummaryTableElement.Type.CsvMetadata)
							{
								bEarlyOut = true;
								break;
							}
							metaData.dict.Add(key, value);
						}

						if (!bEarlyOut)
						{
							string endString = reader.ReadString();
							if (endString != "END")
							{
								Console.WriteLine("Corruption detected in " + filename + ". Skipping read");
								metaData = null;
							}
						}
					}
					reader.Close();
				}
			}
			catch (Exception e)
			{
				metaData = null;
				Console.WriteLine("Error reading from cache file " + filename + ": " + e.Message);
			}
			return metaData;
		}

		public bool WriteToCache(string summaryTableCacheDir, string csvId)
		{
			string filename = Path.Combine(summaryTableCacheDir, csvId + ".prc");
			try
			{
				using (FileStream fileStream = new FileStream(filename, FileMode.Create))
				{
					BinaryWriter writer = new BinaryWriter(fileStream);
					writer.Write(CacheVersion);
					writer.Write(SummaryTableElement.CacheVersion);

					writer.Write(dict.Count);
					foreach (KeyValuePair<string, SummaryTableElement> entry in dict)
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

		public int GetFrameCount()
		{
			if (!dict.ContainsKey("framecount"))
			{
				return 0;
			}
			return (int)dict["framecount"].numericValue;
		}

		public void RemoveSafe(string name)
		{
			string key = name.ToLower();
			if (dict.ContainsKey(key))
			{
				dict.Remove(key);
			}
		}

		public void Add(SummaryTableElement.Type type, string name, double value, ColourThresholdList colorThresholdList = null, string tooltip = "", uint flags = 0)
		{
			string key = name.ToLower();
			SummaryTableElement metadataValue = new SummaryTableElement(type, name, value, colorThresholdList, tooltip, flags);
			try
			{
				dict.Add(key, metadataValue);
			}
			catch (System.ArgumentException)
			{
				//throw new Exception("Summary metadata key " + key + " has already been added");
				Console.WriteLine("Warning: Key " + key + " has already been added. Ignoring...");
			}
		}

		public void Add(SummaryTableElement.Type type, string name, string value, ColourThresholdList colorThresholdList = null, string tooltip = "", uint flags = 0)
		{
			string key = name.ToLower();
			double numericValue = double.MaxValue;
			try
			{
				numericValue = Convert.ToDouble(value, System.Globalization.CultureInfo.InvariantCulture);
			}
			catch { }

			SummaryTableElement metadataValue = null;
			if (numericValue != double.MaxValue)
			{
				metadataValue = new SummaryTableElement(type, name, numericValue, colorThresholdList, tooltip, flags);
			}
			else
			{
				metadataValue = new SummaryTableElement(type, name, value, colorThresholdList, tooltip, flags);
			}

			try
			{
				dict.Add(key, metadataValue);
			}
			catch (System.ArgumentException)
			{
				//throw new Exception("Summary metadata key " + key + " has already been added");
				Console.WriteLine("Warning: Key " + key + " has already been added. Ignoring...");
			}
		}

		public void AddString(SummaryTableElement.Type type, string name, string value, ColourThresholdList colorThresholdList = null, string tooltip = "")
		{
			string key = name.ToLower();
			SummaryTableElement metadataValue = new SummaryTableElement(type, name, value, colorThresholdList, tooltip);
			dict.Add(key, metadataValue);
		}

		public Dictionary<string, dynamic> ToJsonDict(bool bCsvMetadataOnly, bool bWriteAllElementData)
		{
			Dictionary<string, dynamic> DictOut = new Dictionary<string, dynamic>();

			// Make a dictionary for each data type
			if (bCsvMetadataOnly)
			{
				DictOut[SummaryTableElement.Type.CsvMetadata.ToString()] = new Dictionary<string, dynamic>();
			}
			else
			{
				var DataTypes = Enum.GetValues(typeof(SummaryTableElement.Type));
				foreach (SummaryTableElement.Type dataType in DataTypes)
				{
					DictOut[dataType.ToString()] = new Dictionary<string, dynamic>();
				}
			}

			foreach (string key in dict.Keys)
			{
				SummaryTableElement Element = dict[key];
				if (bCsvMetadataOnly && Element.type != SummaryTableElement.Type.CsvMetadata)
				{
					continue;
				}
				if (bWriteAllElementData)
				{
					DictOut[Element.type.ToString()][key] = Element.ToJsonDict(false);
				}
				else
				{
					DictOut[Element.type.ToString()][key] = Element.DynamicValue;
				}
			}

			return DictOut;
		}

		public Dictionary<string, SummaryTableElement> dict = new Dictionary<string, SummaryTableElement>();
	};

}