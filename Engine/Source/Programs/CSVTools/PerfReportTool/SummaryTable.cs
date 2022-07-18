// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Xml.Linq;
using PerfReportTool;
using System.IO;

namespace PerfSummaries
{
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
					string text = inValue.Substring(openAnchorEndIndex + 1, closeAnchorStartIndex - (openAnchorEndIndex + 1));
					if (text.Length > maxLength)
					{
						text = SanitizeHtmlString(text.Substring(0, maxLength)) + "...";
					}
					return anchor + text + "</a>";
				}
			}
			return SanitizeHtmlString(inValue.Substring(0, maxLength)) + "...";
		}
	}

	class SummarySectionBoundaryInfo
	{
		public SummarySectionBoundaryInfo(string inStatName, string inStartToken, string inEndToken, int inLevel, bool inInCollatedTable, bool inInFullTable)
		{
			statName = inStatName;
			startToken = inStartToken;
			endToken = inEndToken;
			level = inLevel;
			inCollatedTable = inInCollatedTable;
			inFullTable = inInFullTable;
		}
		public string statName;
		public string startToken;
		public string endToken;
		public int level;
		public bool inCollatedTable;
		public bool inFullTable;
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
			XAttribute weightByColumnAt = tableElement.Attribute("weightByColumn");
			if (weightByColumnAt != null)
			{
				weightByColumn = weightByColumnAt.Value.ToLower();
			}

			XElement filterEl = tableElement.Element("filter");
			if (filterEl != null)
			{
				columnFilterList.AddRange(filterEl.Value.Split(','));
			}

			bReverseSortRows = tableElement.GetSafeAttibute<bool>("reverseSortRows", false);
			bScrollableFormatting = tableElement.GetSafeAttibute<bool>("scrollableFormatting", false);
			bAutoColorize = tableElement.GetSafeAttibute<bool>("autoColorize", false);
			statThreshold = tableElement.GetSafeAttibute<float>("statThreshold", 0.0f);
			hideStatPrefix = tableElement.GetSafeAttibute<string>("hideStatPrefix");

			foreach (XElement sectionBoundaryEl in tableElement.Elements("sectionBoundary"))
			{
				if (sectionBoundaryEl != null)
				{
					SummarySectionBoundaryInfo sectionBoundary = new SummarySectionBoundaryInfo(
						sectionBoundaryEl.GetSafeAttibute<string>("statName"),
						sectionBoundaryEl.GetSafeAttibute<string>("startToken"),
						sectionBoundaryEl.GetSafeAttibute<string>("endToken"),
						sectionBoundaryEl.GetSafeAttibute<int>("level", 0),
						sectionBoundaryEl.GetSafeAttibute<bool>("inCollatedTable", true),
						sectionBoundaryEl.GetSafeAttibute<bool>("inFullTable", true)
						);
					sectionBoundaries.Add(sectionBoundary);
				}
			}
		}

		public SummaryTableInfo(string filterListStr, string rowSortStr)
		{
			columnFilterList.AddRange(filterListStr.Split(','));
			rowSortList.AddRange(rowSortStr.Split(','));
		}

		public SummaryTableInfo()
		{
		}

		public List<string> rowSortList = new List<string>();
		public List<string> columnFilterList = new List<string>();
		public List<SummarySectionBoundaryInfo> sectionBoundaries = new List<SummarySectionBoundaryInfo>();
		public bool bReverseSortRows;
		public bool bScrollableFormatting;
		public bool bAutoColorize;
		public float statThreshold;
		public string hideStatPrefix = null;
		public string weightByColumn = null;
	}


	class SummaryTableColumnFormatInfoCollection
	{
		public SummaryTableColumnFormatInfoCollection(XElement element)
		{
			foreach (XElement child in element.Elements("columnInfo"))
			{
				columnFormatInfoList.Add(new SummaryTableColumnFormatInfo(child));
			}
		}

		public SummaryTableColumnFormatInfo GetFormatInfo(string columnName)
		{
			string lowerColumnName = columnName.ToLower();
			if (lowerColumnName.StartsWith("avg ") || lowerColumnName.StartsWith("min ") || lowerColumnName.StartsWith("max "))
			{
				lowerColumnName = lowerColumnName.Substring(4);
			}
			foreach (SummaryTableColumnFormatInfo columnInfo in columnFormatInfoList)
			{
				int wildcardIndex = columnInfo.name.IndexOf('*');
				if (wildcardIndex == -1)
				{
					if (columnInfo.name == lowerColumnName)
					{
						return columnInfo;
					}
				}
				else
				{
					string prefix = columnInfo.name.Substring(0, wildcardIndex);
					if (lowerColumnName.StartsWith(prefix))
					{
						return columnInfo;
					}
				}
			}
			return defaultColumnInfo;
		}

		public static SummaryTableColumnFormatInfo DefaultColumnInfo
		{
			get { return defaultColumnInfo; }
		}

		List<SummaryTableColumnFormatInfo> columnFormatInfoList = new List<SummaryTableColumnFormatInfo>();
		static SummaryTableColumnFormatInfo defaultColumnInfo = new SummaryTableColumnFormatInfo();
	};

	enum AutoColorizeMode
	{
		Off,
		HighIsBad,
		LowIsBad,
	};

	class SummaryTableColumnFormatInfo
	{
		public SummaryTableColumnFormatInfo()
		{
			name = "Default";
			maxStringLength = Int32.MaxValue;
			maxStringLengthCollated = Int32.MaxValue;
		}
		public SummaryTableColumnFormatInfo(XElement element)
		{
			name = element.Attribute("name").Value.ToLower();

			string autoColorizeStr = element.GetSafeAttibute<string>("autoColorize", "highIsBad").ToLower();
			var modeList = Enum.GetValues(typeof(AutoColorizeMode));
			foreach (AutoColorizeMode mode in modeList)
			{
				if (mode.ToString().ToLower() == autoColorizeStr)
				{
					autoColorizeMode = mode;
					break;
				}
			}
			numericFormat = element.GetSafeAttibute<string>("numericFormat");
			maxStringLength = element.GetSafeAttibute<int>("maxStringLength", Int32.MaxValue );
			maxStringLengthCollated = element.GetSafeAttibute<int>("maxStringLengthCollated", Int32.MaxValue );
			if (maxStringLengthCollated == Int32.MaxValue)
			{
				maxStringLengthCollated = maxStringLength;
			}
		}
		public AutoColorizeMode autoColorizeMode = AutoColorizeMode.HighIsBad;
		public string name;
		public string numericFormat;
		public int maxStringLength;
		public int maxStringLengthCollated;
	};

	class SummaryTableColumn
	{
		public string name;
		public bool isNumeric = false;
		public string displayName;
		public bool isRowWeightColumn = false;
		List<double> doubleValues = new List<double>();
		List<string> stringValues = new List<string>();
		List<string> toolTips = new List<string>();
		public SummaryTableElement.Type elementType;

		List<ColourThresholdList> colourThresholds = new List<ColourThresholdList>();
		ColourThresholdList colourThresholdOverride = null;
		public SummaryTableColumn(string inName, bool inIsNumeric, string inDisplayName, bool inIsRowWeightColumn, SummaryTableElement.Type inElementType)
		{
			name = inName;
			isNumeric = inIsNumeric;
			displayName = inDisplayName;
			isRowWeightColumn = inIsRowWeightColumn;
			elementType = inElementType;
		}
		public SummaryTableColumn Clone()
		{
			SummaryTableColumn newColumn = new SummaryTableColumn(name, isNumeric, displayName, isRowWeightColumn, elementType);
			newColumn.doubleValues.AddRange(doubleValues);
			newColumn.stringValues.AddRange(stringValues);
			newColumn.colourThresholds.AddRange(colourThresholds);
			newColumn.toolTips.AddRange(toolTips);
			return newColumn;
		}

		public string GetDisplayName(string hideStatPrefix=null)
		{
			string nameOut;
			if (displayName == null)
			{
				// Trim the stat name suffix if necessary
				string statName = name;
				if (hideStatPrefix != null)
				{
					string prefix = "";
					string suffix = name;
					if (name.StartsWith("Min ") || name.StartsWith("Max ") || name.StartsWith("Avg "))
					{
						prefix = name.Substring(0, 4);
						suffix = name.Substring(4);
					}
					if ( suffix.ToLower().StartsWith(hideStatPrefix.ToLower() ) )
					{
						statName = prefix + suffix.Substring(hideStatPrefix.Length);
					}
				}
				nameOut = TableUtil.FormatStatName(statName);
			}
			else
			{
				nameOut = displayName;
			}
			return nameOut;
		}

		public void SetValue(int index, double value)
		{
			if (!isNumeric)
			{
				// This is already a non-numeric column. Better treat this as a string value
				SetStringValue(index, value.ToString());
				return;
			}
			// Grow to fill if necessary
			if (index >= doubleValues.Count)
			{
				for (int i = doubleValues.Count; i <= index; i++)
				{
					doubleValues.Add(double.MaxValue);
				}
			}
			doubleValues[index] = value;
		}

		void convertToStrings()
		{
			if (isNumeric)
			{
				stringValues = new List<string>();
				foreach (float f in doubleValues)
				{
					stringValues.Add(f.ToString());
				}
				doubleValues = new List<double>();
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
			double value = GetValue(index);
			if (value == double.MaxValue)
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
			return thresholds.GetColourForValue(value);
		}

		public void ComputeAutomaticColourThresholds(AutoColorizeMode autoColorizeMode)
		{
			if (autoColorizeMode == AutoColorizeMode.Off)
			{
				return;
			}
			colourThresholds = new List<ColourThresholdList>();
			double maxValue = -double.MaxValue;
			double minValue = double.MaxValue;
			double totalValue = 0.0f;
			double validCount = 0.0f;
			for (int i = 0; i < doubleValues.Count; i++)
			{
				double val = doubleValues[i];
				if (val != double.MaxValue)
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

			double averageValue = totalValue / validCount; // TODO: Weighted average 
			colourThresholdOverride = new ColourThresholdList();
			colourThresholdOverride.Add(new ThresholdInfo(minValue, (autoColorizeMode == AutoColorizeMode.HighIsBad) ? green : red));
			colourThresholdOverride.Add(new ThresholdInfo(averageValue, yellow));
			colourThresholdOverride.Add(new ThresholdInfo(averageValue, yellow));
			colourThresholdOverride.Add(new ThresholdInfo(maxValue, (autoColorizeMode == AutoColorizeMode.HighIsBad) ? red : green));
		}

		public int GetCount()
		{
			return Math.Max(doubleValues.Count, stringValues.Count);
		}
		public double GetValue(int index)
		{
			if (index >= doubleValues.Count)
			{
				return double.MaxValue;
			}
			return doubleValues[index];
		}

		public bool AreAllValuesOverThreshold(double threshold)
		{
			if (!isNumeric)
			{
				return true;
			}
			foreach(double value in doubleValues)
			{
				if ( value > threshold && value != double.MaxValue)
				{
					return true;
				}
			}
			return false;
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
		public string GetStringValue(int index, bool roundNumericValues = false, string forceNumericFormat = null)
		{
			if (isNumeric)
			{
				if (index >= doubleValues.Count || doubleValues[index] == double.MaxValue)
				{
					return "";
				}
				double val = doubleValues[index];
				if (forceNumericFormat != null)
				{
					return val.ToString(forceNumericFormat);
				}
				else if (roundNumericValues)
				{
					double absVal = Math.Abs(val);
					double frac = absVal - (double)Math.Truncate(absVal);
					if (absVal >= 250.0f || frac < 0.0001f)
					{
						return val.ToString("0");
					}
					if (absVal >= 50.0f)
					{
						return val.ToString("0.0");
					}
					if (absVal >= 0.1)
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
				if (forceNumericFormat != null)
				{
					// We're forcing a numeric format on something that's technically a string, but since we were asked, we'll try to do it anyway 
					// Note: this is not ideal, but it's useful for collated table columns, which might get converted to non-numeric during collation
					try
					{
						return Convert.ToDouble(stringValues[index], System.Globalization.CultureInfo.InvariantCulture).ToString(forceNumericFormat);
					}
					catch { } // Ignore. Just fall through...
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



	class SummaryTable
	{
		public SummaryTable()
		{
		}

		public SummaryTable CollateSortedTable(List<string> collateByList, bool addMinMaxColumns)
		{
			int numSubColumns = addMinMaxColumns ? 3 : 1;

			// Find all the columns in collateByList
			HashSet<SummaryTableColumn> collateByColumns = new HashSet<SummaryTableColumn>();
			foreach (string collateBy in collateByList)
			{
				string key = collateBy.ToLower();
				if (columnLookup.ContainsKey(key))
				{
					collateByColumns.Add(columnLookup[key]);
				}
			}
			if (collateByColumns.Count == 0)
			{
				throw new Exception("None of the metadata strings were found:" + string.Join(", ", collateByList));
			}

			// Add the new collateBy columns in the order they appear in the original column list
			List<SummaryTableColumn> newColumns = new List<SummaryTableColumn>();
			List<string> finalSortByList = new List<string>();
			foreach (SummaryTableColumn srcColumn in columns)
			{
				if (collateByColumns.Contains(srcColumn))
				{
					newColumns.Add(new SummaryTableColumn(srcColumn.name, false, srcColumn.displayName, false, srcColumn.elementType));
					finalSortByList.Add(srcColumn.name.ToLower());
					// Early out if we've found all the columns
					if (finalSortByList.Count == collateByColumns.Count)
					{
						break;
					}
				}
			}

			newColumns.Add(new SummaryTableColumn("Count", true, null, false, SummaryTableElement.Type.ToolMetadata));
			int countColumnIndex = newColumns.Count - 1;

			int numericColumnStartIndex = newColumns.Count;
			List<int> srcToDestBaseColumnIndex = new List<int>();
			foreach (SummaryTableColumn column in columns)
			{
				// Add avg/min/max columns for this column if it's numeric and we didn't already add it above 
				if (column.isNumeric && !collateByColumns.Contains(column))
				{
					srcToDestBaseColumnIndex.Add(newColumns.Count);
					newColumns.Add(new SummaryTableColumn("Avg " + column.name, true, null, false, column.elementType));
					if (addMinMaxColumns)
					{
						newColumns.Add(new SummaryTableColumn("Min " + column.name, true, null, false, column.elementType));
						newColumns.Add(new SummaryTableColumn("Max " + column.name, true, null, false, column.elementType));
					}
				}
				else
				{
					srcToDestBaseColumnIndex.Add(-1);
				}
			}

			List<double> RowMaxValues = new List<double>();
			List<double> RowTotals = new List<double>();
			List<double> RowMinValues = new List<double>();
			List<int> RowCounts = new List<int>();
			List<double> RowWeights = new List<double>();
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
					RowWeights.Clear();
					RowColourThresholds.Clear();
					for (int j = 0; j < columns.Count; j++)
					{
						if (addMinMaxColumns)
						{
							RowMaxValues.Add(-double.MaxValue);
							RowMinValues.Add(double.MaxValue);
						}
						RowTotals.Add(0.0f);
						RowCounts.Add(0);
						RowWeights.Add(0.0);
						RowColourThresholds.Add(null);
					}
					mergedRowsCount = 0;
					reset = false;
				}

				// Compute min/max/total for all numeric columns
				for (int j = 0; j < columns.Count; j++)
				{
					SummaryTableColumn column = columns[j];
					if (column.isNumeric)
					{
						double value = column.GetValue(i);
						if (value != double.MaxValue)
						{
							if (addMinMaxColumns)
							{
								RowMaxValues[j] = Math.Max(RowMaxValues[j], value);
								RowMinValues[j] = Math.Min(RowMinValues[j], value);
							}
							RowColourThresholds[j] = column.GetColourThresholds(i);
							RowCounts[j]++;
							double rowWeight = (rowWeightings != null) ? rowWeightings[i] : 1.0;
							RowWeights[j] += rowWeight;
							RowTotals[j] += value * rowWeight;
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
					for (int j = 0; j < countColumnIndex; j++)
					{
						string key = newColumns[j].name.ToLower();
						newColumns[j].SetStringValue(destRowIndex, columnLookup[key].GetStringValue(i));
					}
					// Commit the row 
					newColumns[countColumnIndex].SetValue(destRowIndex, (double)mergedRowsCount);
					for (int j = 0; j < columns.Count; j++)
					{
						int destColumnBaseIndex = srcToDestBaseColumnIndex[j];
						if (destColumnBaseIndex != -1 && RowCounts[j] > 0)
						{
							newColumns[destColumnBaseIndex].SetValue(destRowIndex, RowTotals[j] / RowWeights[j]);
							if (addMinMaxColumns)
							{
								newColumns[destColumnBaseIndex + 1].SetValue(destRowIndex, RowMinValues[j]);
								newColumns[destColumnBaseIndex + 2].SetValue(destRowIndex, RowMaxValues[j]);
							}

							// Set colour thresholds based on the source column
							ColourThresholdList Thresholds = RowColourThresholds[j];
							for (int k = 0; k < numSubColumns; k++)
							{
								newColumns[destColumnBaseIndex + k].SetColourThresholds(destRowIndex, Thresholds);
							}
						}
					}
					reset = true;
					destRowIndex++;
				}
				CurrentRowSortKey = nextSortKey;
			}

			SummaryTable newTable = new SummaryTable();
			newTable.columns = newColumns;
			newTable.InitColumnLookup();
			newTable.rowCount = destRowIndex;
			newTable.firstStatColumnIndex = numericColumnStartIndex;
			newTable.isCollated = true;
			newTable.hasMinMaxColumns = addMinMaxColumns;
			return newTable;
		}

		public SummaryTable SortAndFilter(string customFilter, string customRowSort = "buildversion,deviceprofile", bool bReverseSort = false, string weightByColumnName = null)
		{
			return SortAndFilter(customFilter.Split(',').ToList(), customRowSort.Split(',').ToList(), bReverseSort, weightByColumnName);
		}

		public SummaryTable SortAndFilter(List<string> columnFilterList, List<string> rowSortList, bool bReverseSort, string weightByColumnName, float statThreshold = 0.0f)
		{
			SummaryTable newTable = SortRows(rowSortList, bReverseSort);

			// Make a list of all unique keys
			List<string> allMetadataKeys = new List<string>();
			Dictionary<string, SummaryTableColumn> nameLookup = new Dictionary<string, SummaryTableColumn>();
			foreach (SummaryTableColumn col in newTable.columns)
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
				bool startWild = filterStrLower.StartsWith("*");
				bool endWild = filterStrLower.EndsWith("*");
				filterStrLower = filterStrLower.Trim('*');
				if (startWild && endWild)
                {
					orderedKeysWithDupes.AddRange(allMetadataKeys.Where(x => x.Contains(filterStrLower)));
                }
				else if(startWild)
				{
					orderedKeysWithDupes.AddRange(allMetadataKeys.Where(x => x.EndsWith(filterStrLower)));
				}
				else if(endWild)
				{
					// Linear search through the sorted key list
					bool bFound = false;
					for (int wildcardSearchIndex = 0; wildcardSearchIndex < allMetadataKeys.Count; wildcardSearchIndex++)
					{
						if (allMetadataKeys[wildcardSearchIndex].StartsWith(filterStrLower))
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

			// Compute row weights
			if (weightByColumnName != null && nameLookup.ContainsKey(weightByColumnName))
			{
				SummaryTableColumn rowWeightColumn = nameLookup[weightByColumnName];
				newTable.rowWeightings = new List<double>(rowWeightColumn.GetCount());
				for (int i = 0; i < rowWeightColumn.GetCount(); i++)
				{
					newTable.rowWeightings.Add(rowWeightColumn.GetValue(i));
				}
			}

			List<SummaryTableColumn> newColumnList = new List<SummaryTableColumn>();
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


			// Filter out csv stat or metric columns below the specified threshold
			if (statThreshold > 0.0f)
			{
				List<SummaryTableColumn> oldColumnList = newColumnList;
				newColumnList = new List<SummaryTableColumn>();
				foreach (SummaryTableColumn column in oldColumnList)
				{
					if (!column.isNumeric || 
						( column.elementType != SummaryTableElement.Type.CsvStatAverage && column.elementType != SummaryTableElement.Type.SummaryTableMetric ) || 
						column.AreAllValuesOverThreshold((double)statThreshold))
					{
						newColumnList.Add(column);
					}
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
			foreach (SummaryTableColumn column in columns)
			{
				if (statDisplaynameMapping != null && column.displayName == null)
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
			foreach (SummaryTableColumn column in columns)
			{
				headerRow.Add(column.name);
			}
			csvFile.WriteLine(string.Join(",", headerRow));

			for (int i = 0; i < rowCount; i++)
			{
				List<string> rowStrings = new List<string>();
				foreach (SummaryTableColumn column in columns)
				{
					string cell = column.GetStringValue(i, false);
					// Sanitize so it opens in a spreadsheet (e.g. for buildversion) 
					cell = cell.TrimStart('+');
					rowStrings.Add(cell);
				}
				csvFile.WriteLine(string.Join(",", rowStrings));
			}
			csvFile.Close();
		}


		public void WriteToHTML(
			string htmlFilename, 
			string VersionString, 
			bool bSpreadsheetFriendlyStrings, 
			List<SummarySectionBoundaryInfo> sectionBoundaries, 
			bool bScrollableTable, 
			bool bAutoColorizeTable,
			bool bAddMinMaxColumns, 
			string hideStatPrefix,
			int maxColumnStringLength, 
			SummaryTableColumnFormatInfoCollection columnFormatInfoList, 
			string weightByColumnName, 
			string title)
		{
			System.IO.StreamWriter htmlFile = new System.IO.StreamWriter(htmlFilename, false);
			int statColSpan = hasMinMaxColumns ? 3 : 1;
			int cellPadding = 2;
			if (isCollated)
			{
				cellPadding = 4;
			}

			// Generate an automatic title
			if (title==null)
			{
				title = htmlFilename.Replace("_Email.html", "").Replace(".html", "").Replace("\\", "/");
				title = title.Substring(title.LastIndexOf('/') + 1);
			}

			htmlFile.WriteLine("<html>");
			htmlFile.WriteLine("<head><title>Perf Summary: "+ title + "</title>");

			// Figure out the sticky column count
			int stickyColumnCount = 0;
			if (bScrollableTable)
			{
				stickyColumnCount = 1;
				if (isCollated)
				{
					for (int i = 0; i < columns.Count; i++)
					{
						if (columns[i].name == "Count")
						{
							stickyColumnCount = i + 1;
							break;
						}
					}
				}
			}

			// Get format info for the columns
			Dictionary<SummaryTableColumn, SummaryTableColumnFormatInfo> columnFormatInfoLookup = new Dictionary<SummaryTableColumn, SummaryTableColumnFormatInfo>();
			foreach (SummaryTableColumn column in columns)
			{
				columnFormatInfoLookup[column] = (columnFormatInfoList != null) ? columnFormatInfoList.GetFormatInfo(column.name) : SummaryTableColumnFormatInfoCollection.DefaultColumnInfo;
			}

			// Automatically colourize the table if requested
			if (bAutoColorizeTable)
			{
				foreach (SummaryTableColumn column in columns)
				{
					if (column.isNumeric)
					{
						column.ComputeAutomaticColourThresholds(columnFormatInfoLookup[column].autoColorizeMode);
					}
				}
			}

			if (bScrollableTable)
			{
				// Insert some javascript to make the columns sticky. It's not possible to do this for multiple columns with pure CSS, since you need to compute the X offset dynamically
				// We need to do this when the page is loaded or the window is resized
				htmlFile.WriteLine("<script>");

				htmlFile.WriteLine(
					"var originalStyleElement = null; \n" +
					"document.addEventListener('DOMContentLoaded', function(event) { regenerateStickyColumnCss(); }) \n" +
					"window.addEventListener('resize', function(event) { regenerateStickyColumnCss(); }) \n" +
					"\n" +
					"function regenerateStickyColumnCss() { \n" +
					"  var styleElement=document.getElementById('pageStyle'); \n" +
					"  var table=document.getElementById('mainTable'); \n" +
					"  if ( table.rows.length < 2 ) \n" +
					"	return; \n" +
					"  if (originalStyleElement == null) \n" +
					"    originalStyleElement = styleElement.textContent; \n" +
					"  else \n" +
					"    styleElement.textContent = originalStyleElement  \n"
				);

				// Make the columns Sticky and compute their X offsets
				htmlFile.WriteLine(
					"  var numStickyCols=" + stickyColumnCount + "; \n" +
					"  var xOffset=0; \n" +
					"  for (var i=0;i<numStickyCols;i++) \n" +
					"  { \n" +
					"	 var rBorderParam=(i==numStickyCols-1) ? 'border-right: 2px solid black;':''; \n" +
					"	 styleElement.textContent+='tr.lastHeaderRow th:nth-child('+(i+1)+') {  z-index: 8;  border-top: 2px solid black;  font-size: 11px;  left: '+xOffset+'px;'+rBorderParam+'}'; \n" +
					"	 styleElement.textContent+='td:nth-child('+(i+1)+') {  position: -webkit-sticky;  position: sticky; z-index: 7;  left: '+xOffset+'px; '+rBorderParam+'}'; \n" +
					"	 xOffset+=table.rows[1].cells[i].offsetWidth; \n" +
					"  } \n"
					);
				// Make all sticky columns except the count column opaque
				htmlFile.WriteLine(
					"  for (var i=0;i<numStickyCols-1;i++) \n" +
					"  { \n" +
					"    styleElement.textContent+='tr:nth-child(odd) td:nth-child('+(i+1)+') { background-color: #e2e2e2; }'; \n" +
					"    styleElement.textContent+='tr:nth-child(even) td:nth-child('+(i+1)+') { background-color: #ffffff; }'; \n" +
					"  } \n" +
					"} \n"
				);
				htmlFile.WriteLine("</script>");
			}

			htmlFile.WriteLine("<style type='text/css' id='pageStyle'>");
			htmlFile.WriteLine("p {  font-family: 'Verdana', Times, serif; font-size: 12px }");
			htmlFile.WriteLine("h3 {  font-family: 'Verdana', Times, serif; font-size: 14px }");
			htmlFile.WriteLine("h2 {  font-family: 'Verdana', Times, serif; font-size: 16px }");
			htmlFile.WriteLine("h1 {  font-family: 'Verdana', Times, serif; font-size: 20px }");
			string tableCss = "";
			if (bScrollableTable)
			{
				int firstColMaxStringLength = 0;
				if (columns.Count > 0)
				{
					for (int i = 0; i < columns[0].GetCount(); i++)
					{
						firstColMaxStringLength = Math.Max(firstColMaxStringLength, columns[0].GetStringValue(i).Length);
					}
				}
				int firstColWidth = (int)(firstColMaxStringLength * 6.5);
				tableCss =
					"table {table-layout: fixed;} \n" +
					"table, th, td { border: 0px solid black; border-spacing: 0; border-collapse: separate; padding: " + cellPadding + "px; vertical-align: center; font-family: 'Verdana', Times, serif; font-size: 10px;} \n" +
					"td {" +
					"  border-right: 1px solid black;" +
					"  max-width: 400;" +
					"} \n" +
					"tr:first-element { border-top: 2px; border-bottom: 2px } \n" +
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
					"} \n";

				// Top-left cell of the table is always on top, big font, thick border
				tableCss += "tr:first-child th:first-child { z-index: 100;  border-right: 2px solid black; border-top: 2px solid black; font-size: 11px; top:0; left: 0px; } \n";

				// Fix the first column width
				tableCss += "th:first-child, td:first-child { border-left: 2px solid black; min-width: " + firstColWidth + ";} \n";

				if (bAddMinMaxColumns && isCollated)
				{
					tableCss += "tr.lastHeaderRow th { top:60px; height:20px; } \n";
				}

				if (!isCollated)
				{
					tableCss += "td { max-height: 40px; height:40px } \n";
				}
				tableCss += "tr:last-child td{border-bottom: 2px solid black;} \n";

			}
			else
			{
				tableCss =
					"table, th, td { border: 2px solid black; border-collapse: collapse; padding: " + cellPadding + "px; vertical-align: center; font-family: 'Verdana', Times, serif; font-size: 11px;} \n";
			}


			bool bOddRowsGray = !(!bAddMinMaxColumns || !isCollated);
			tableCss += "tr:nth-child(" + (bOddRowsGray ? "odd" : "even") + ") {background-color: #e2e2e2;} \n";
			tableCss += "tr:nth-child(" + (bOddRowsGray ? "even" : "odd") + ") {background-color: #ffffff;} \n";
			tableCss += "tr:first-child {background-color: #ffffff;} \n";
			tableCss += "tr.lastHeaderRow th { border-bottom: 2px solid black; } \n";

			// Section start row styles
			tableCss += "tr.sectionStartLevel0 td { border-top: 2px solid black; } \n";
			tableCss += "tr.sectionStartLevel1 td { border-top: 1px solid black; } \n";
			tableCss += "tr.sectionStartLevel2 td { border-top: 1px dashed black; } \n";

			htmlFile.WriteLine(tableCss);

			htmlFile.WriteLine("</style>");
			htmlFile.WriteLine("</head><body>");
			htmlFile.WriteLine("<table id='mainTable'>");

			string HeaderRow = "";
			if (isCollated)
			{
				string TopHeaderRow = "";
				if (bScrollableTable)
				{
					TopHeaderRow += "<th colspan='" + firstStatColumnIndex + "'><h3>" + title + "</h3></th>";
				}
				else
				{
					TopHeaderRow += "<th colspan='" + firstStatColumnIndex + "'/>";
				}

				for (int i = 0; i < firstStatColumnIndex; i++)
				{
					HeaderRow += "<th>" + columns[i].GetDisplayName(hideStatPrefix) + "</th>";
				}
				if (!bAddMinMaxColumns)
				{
					TopHeaderRow = HeaderRow;
				}

				for (int i = firstStatColumnIndex; i < columns.Count; i++)
				{
					string prefix = "";
					string suffix = "";
					string statName = GetStatNameWithPrefixAndSuffix(columns[i].GetDisplayName(hideStatPrefix), out prefix, out suffix);
					if ((i - 1) % statColSpan == 0)
					{
						TopHeaderRow += "<th colspan='" + statColSpan + "' >" + statName + suffix + "</th>";
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
				foreach (SummaryTableColumn column in columns)
				{
					HeaderRow += "<th>" + column.GetDisplayName(hideStatPrefix) + "</th>";
				}
				htmlFile.WriteLine("  <tr class='lastHeaderRow'>" + HeaderRow + "</tr>");
			}
			string[] stripeColors = { "'#e2e2e2'", "'#ffffff'" };

			// Work out which rows are major/minor section boundaries
			Dictionary<int, int> rowSectionBoundaryLevel = new Dictionary<int, int>();
			if (sectionBoundaries != null)
			{
				foreach (SummarySectionBoundaryInfo sectionBoundaryInfo in sectionBoundaries)
				{
					// Skip this section boundary info if it's not in this table type
					if (isCollated && !sectionBoundaryInfo.inCollatedTable)
					{
						continue;
					}
					if (!isCollated && !sectionBoundaryInfo.inFullTable)
					{
						continue;
					}
					string prevSectionName = "";
					for (int i = 0; i < rowCount; i++)
					{
						int boundaryLevel = 0;
						if (sectionBoundaryInfo != null)
						{
							// Work out the section name if we have section boundary info. When it changes, apply the sectionStart CSS class
							string sectionName = "";
							if (sectionBoundaryInfo != null && columnLookup.ContainsKey(sectionBoundaryInfo.statName))
							{
								// Get the section name
								if (!columnLookup.ContainsKey(sectionBoundaryInfo.statName))
								{
									continue;
								}
								SummaryTableColumn col = columnLookup[sectionBoundaryInfo.statName];
								sectionName = col.GetStringValue(i);

								// if we have a start token then strip before it
								if (sectionBoundaryInfo.startToken != null)
								{
									int startTokenIndex = sectionName.IndexOf(sectionBoundaryInfo.startToken);
									if (startTokenIndex != -1)
									{
										sectionName = sectionName.Substring(startTokenIndex + sectionBoundaryInfo.startToken.Length);
									}
								}

								// if we have an end token then strip after it
								if (sectionBoundaryInfo.endToken != null)
								{
									int endTokenIndex = sectionName.IndexOf(sectionBoundaryInfo.endToken);
									if (endTokenIndex != -1)
									{
										sectionName = sectionName.Substring(0, endTokenIndex);
									}
								}
							}
							if (sectionName != prevSectionName && i > 0)
							{
								// Update the row's boundary type info
								boundaryLevel = sectionBoundaryInfo.level;
								if (rowSectionBoundaryLevel.ContainsKey(i))
								{
									// Lower level values override higher ones
									boundaryLevel = Math.Min(rowSectionBoundaryLevel[i], boundaryLevel);
								}
								rowSectionBoundaryLevel[i] = boundaryLevel;
							}
							prevSectionName = sectionName;
						}
					}
				}

			}

			// Add the rows to the table
			for (int i = 0; i < rowCount; i++)
			{
				string rowClassStr = "";

				// Is this a major/minor section boundary
				if (rowSectionBoundaryLevel.ContainsKey(i))
				{
					int sectionLevel = rowSectionBoundaryLevel[i];
					if (sectionLevel < 3)
					{
						rowClassStr = " class='sectionStartLevel" + sectionLevel + "'";
					}
				}

				htmlFile.Write("<tr" + rowClassStr + ">");
				int columnIndex = 0;
				foreach (SummaryTableColumn column in columns)
				{
					// Add the tooltip for non-collated tables
					string toolTipString = "";
					if (!isCollated)
					{
						string toolTip = column.GetToolTipValue(i);
						if (toolTip == "")
						{
							toolTip = column.GetDisplayName();
						}
						toolTipString = " title='" + toolTip + "'";
					}
					string colour = column.GetColour(i);

					// Alternating row colours are normally handled by CSS, but we need to handle it explicitly if we have frozen first columns
					if (columnIndex < stickyColumnCount && colour == null)
					{
						colour = stripeColors[i % 2];
					}
					string bgColorString = (colour == null ? "" : " bgcolor=" + colour);
					bool bold = false;

					SummaryTableColumnFormatInfo columnFormat = columnFormatInfoLookup[column];
					int maxStringLength = Math.Min( isCollated ? columnFormat.maxStringLengthCollated : columnFormat.maxStringLength, maxColumnStringLength);

					string numericFormat = columnFormat.numericFormat;
					string stringValue = column.GetStringValue(i, true, numericFormat);
					if (stringValue.Length > maxStringLength)
					{
						stringValue = TableUtil.SafeTruncateHtmlTableValue(stringValue, maxStringLength);
					}
					if (bSpreadsheetFriendlyStrings && !column.isNumeric)
					{
						stringValue = "'" + stringValue;
					}
					string columnString = "<td" + toolTipString + bgColorString + "> " + (bold ? "<b>" : "") + stringValue + (bold ? "</b>" : "") + "</td>";
					htmlFile.Write(columnString);
					columnIndex++;
				}
				htmlFile.WriteLine("</tr>");
			}
			htmlFile.WriteLine("</table>");
			string extraString = "";
			if (isCollated && weightByColumnName != null)
			{
				extraString += " - weighted avg";
				//htmlFile.WriteLine("<p style='font-size:8'>Weighted by " + weightByColumnName +"</p>");
			}

			htmlFile.WriteLine("<p style='font-size:8'>Created with PerfReportTool " + VersionString + extraString + "</p>");
			htmlFile.WriteLine("</font></body></html>");

			htmlFile.Close();
		}

		public SummaryTable SortRows(List<string> rowSortList, bool reverseSort)
		{
			List<KeyValuePair<string, int>> columnRemapping = new List<KeyValuePair<string, int>>();
			for (int i = 0; i < rowCount; i++)
			{
				string key = "";
				foreach (string s in rowSortList)
				{
					if (columnLookup.ContainsKey(s.ToLower()))
					{
						SummaryTableColumn column = columnLookup[s.ToLower()];
						key += "{" + column.GetStringValue(i,false,"0.0000000000") + "}";
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
			List<SummaryTableColumn> newColumns = new List<SummaryTableColumn>();
			foreach (SummaryTableColumn srcCol in columns)
			{
				SummaryTableColumn destCol = new SummaryTableColumn(srcCol.name, srcCol.isNumeric, null, false, srcCol.elementType);
				for (int i = 0; i < rowCount; i++)
				{
					int srcIndex = columnRemapping[i].Value;
					int destIndex = reverseSort ? rowCount - 1 - i : i;
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
			SummaryTable newTable = new SummaryTable();
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
			foreach (SummaryTableColumn col in columns)
			{
				columnLookup.Add(col.name.ToLower(), col);
			}
		}

		public void AddRowData(SummaryTableRowData metadata, bool bIncludeCsvStatAverages, bool bIncludeHiddenStats)
		{
			foreach (string key in metadata.dict.Keys)
			{
				SummaryTableElement value = metadata.dict[key];
				if (value.type == SummaryTableElement.Type.CsvStatAverage && !bIncludeCsvStatAverages)
				{
					continue;
				}
				if (value.GetFlag(SummaryTableElement.Flags.Hidden) && !bIncludeHiddenStats)
				{
					continue;
				}
				SummaryTableColumn column = null;

				if (!columnLookup.ContainsKey(key))
				{
					column = new SummaryTableColumn(value.name, value.isNumeric, null, false, value.type);
					columnLookup.Add(key, column);
					columns.Add(column);
				}
				else
				{
					column = columnLookup[key];
				}

				if (value.isNumeric)
				{
					column.SetValue(rowCount, (double)value.numericValue);
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

		Dictionary<string, SummaryTableColumn> columnLookup = new Dictionary<string, SummaryTableColumn>();
		List<SummaryTableColumn> columns = new List<SummaryTableColumn>();
		List<double> rowWeightings = null;
		int rowCount = 0;
		int firstStatColumnIndex = 0;
		bool isCollated = false;
		bool hasMinMaxColumns = false;
	};

}