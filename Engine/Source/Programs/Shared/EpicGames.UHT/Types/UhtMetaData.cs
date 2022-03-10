// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;

namespace EpicGames.UHT.Types
{

	/// <summary>
	/// UhtEnum types need to convert a meta data name index to the enum name
	/// </summary>
	public interface IUhtMetaDataKeyConversion
	{

		/// <summary>
		/// Convert a name and index into a full meta data name
		/// </summary>
		/// <param name="Name">Base name of the meta data</param>
		/// <param name="NameIndex">Index of the meta data.  -1 for the root object.</param>
		/// <returns>Complete meta data key name</returns>
		string GetMetaDataKey(string Name, int NameIndex);
	}

	/// <summary>
	/// Uniquely identifies a meta data element
	/// </summary>
	public struct UhtMetaDataKey
	{

		/// <summary>
		/// The name of the meta data
		/// </summary>
		public string Name;

		/// <summary>
		/// The index of the meta data name (i.e. enum value index) or -1 for the owning object meta data
		/// </summary>
		public int Index;

		/// <summary>
		/// Construct a new meta data key
		/// </summary>
		/// <param name="Name">Meta data name</param>
		/// <param name="Index">Meta data index</param>
		public UhtMetaDataKey(string Name, int Index = UhtMetaData.INDEX_NONE)
		{
			this.Name = Name;
			this.Index = Index;
		}

		/// <summary>
		/// Convert the key to a string
		/// </summary>
		/// <returns>String representation</returns>
		public override string ToString()
		{
			if (Index == UhtMetaData.INDEX_NONE)
			{
				return this.Name.ToString();
			}
			else
			{
				return $"{this.Name}:{this.Index}";
			}
		}
	}

	/// <summary>
	/// Comparer for meta data keys
	/// </summary>
	class UhtMetaDataKeyComparer : IEqualityComparer<UhtMetaDataKey>, IComparer<UhtMetaDataKey>
	{
		/// <summary>
		/// Compare two keys
		/// </summary>
		/// <param name="x">First key</param>
		/// <param name="y">Second key</param>
		/// <returns>-1, 0, or 1 depending on the relationship</returns>
		public int Compare([AllowNull] UhtMetaDataKey x, [AllowNull] UhtMetaDataKey y)
		{
			if (x.Index < y.Index)
			{
				return -1;
			}
			else if (x.Index > y.Index)
			{
				return 1;
			}
			return string.Compare(x.Name, y.Name, StringComparison.OrdinalIgnoreCase);
		}

		/// <summary>
		/// Test to see if two meta data keys are equal
		/// </summary>
		/// <param name="x">First key</param>
		/// <param name="y">Second key</param>
		/// <returns>True if the keys match</returns>
		public bool Equals([AllowNull] UhtMetaDataKey x, [AllowNull] UhtMetaDataKey y)
		{
			return x.Index == y.Index && x.Name.Equals(y.Name, StringComparison.OrdinalIgnoreCase);
		}

		/// <summary>
		/// Get the hash code of the meta data key
		/// </summary>
		/// <param name="obj">Key</param>
		/// <returns>Hash code</returns>
		public int GetHashCode([DisallowNull] UhtMetaDataKey obj)
		{
			return obj.Name.GetHashCode() ^ obj.Name.GetHashCode(StringComparison.OrdinalIgnoreCase);
		}
	}

	/// <summary>
	/// Represents a collection of key/value pairs.  Each type has this collection and enumerations also
	/// have key/value pairs for each enumeration value index.
	/// </summary>
	public class UhtMetaData
	{

		/// <summary>
		/// Helper comparer for meta data keys
		/// </summary>
		private static UhtMetaDataKeyComparer Comparer = new UhtMetaDataKeyComparer();

		/// <summary>
		/// Empty collection of meta data
		/// </summary>
		public static UhtMetaData Empty = new UhtMetaData(null);

		/// <summary>
		/// The meta data of the outer object for the type that owns this meta data
		/// </summary>
		public UhtMetaData? Parent = null;

		/// <summary>
		/// Message site associated with the meta data.  That in combination with the line number is used to generate errors
		/// </summary>
		public IUhtMessageSite? MessageSite = null;
		
		/// <summary>
		/// Source code line number where the meta data is declared
		/// </summary>
		public int LineNumber { get; internal set; } = 1;

		/// <summary>
		/// Contains the meta data entries.  Due to the small size of these dictionaries, a SortedList performs
		/// better than a Dictionary.
		/// </summary>
		public SortedList<UhtMetaDataKey, string>? Dictionary = null;

		/// <summary>
		/// Enumerations implement this interface so that the index part of the key can be converted to a string
		/// </summary>
		internal IUhtMetaDataKeyConversion? KeyConversion = null;

		/// <summary>
		/// Index for a meta data key associated with the owning object
		/// </summary>
		public const int INDEX_NONE = -1;

		/// <summary>
		/// Construct new meta data
		/// </summary>
		/// <param name="MessageSite">Message site for generating errors</param>
		public UhtMetaData(IUhtMessageSite? MessageSite)
		{
			this.MessageSite = MessageSite;
		}

		/// <summary>
		/// Test to see if the meta data object contains no entries
		/// </summary>
		/// <returns>True if the meta data object contains no entries</returns>
		public bool IsEmpty()
		{
			return this.Dictionary == null || this.Dictionary.Count == 0;
		}

		/// <summary>
		/// Remove all meta data entries
		/// </summary>
		public void Clear()
		{
			if (this.Dictionary != null)
			{
				this.Dictionary.Clear();
			}
		}

		/// <summary>
		/// Copy the elements of the meta data
		/// </summary>
		/// <returns>A deep copy of the meta data</returns>
		public UhtMetaData Clone()
		{
			return (UhtMetaData)MemberwiseClone();
		}

		/// <summary>
		/// Test to see if the meta data contains the given key
		/// </summary>
		/// <param name="Name">Name of the meta data</param>
		/// <param name="NameIndex">Enumeration value index or -1 for the type's meta data</param>
		/// <returns>True if the key is found</returns>
		public bool ContainsKey(string Name, int NameIndex = INDEX_NONE)
		{
			if (this.Dictionary == null)
			{
				return false;
			}
			return this.Dictionary.ContainsKey(new UhtMetaDataKey(Name, NameIndex));
		}

		/// <summary>
		/// Test to see if the meta data or parent meta data contains the given key
		/// </summary>
		/// <param name="Name">Name of the meta data</param>
		/// <param name="NameIndex">Enumeration value index or -1 for the type's meta data</param>
		/// <returns>True if the key is found</returns>
		public bool ContainsKeyHierarchical(string Name, int NameIndex = INDEX_NONE)
		{
			for (UhtMetaData? Current = this; Current != null; Current = Current.Parent)
			{
				if (Current.ContainsKey(Name, NameIndex))
				{
					return true;
				}
			}
			return false;
		}

		/// <summary>
		/// Attempt to get the value associated with the key
		/// </summary>
		/// <param name="Name">Name of the meta data</param>
		/// <param name="Value">Found value</param>
		/// <returns>True if the key is found</returns>
		public bool TryGetValue(string Name, [NotNullWhen(true)] out string? Value)
		{
			return TryGetValue(Name, INDEX_NONE, out Value);
		}

		/// <summary>
		/// Attempt to get the value associated with the key including parent meta data
		/// </summary>
		/// <param name="Name">Name of the meta data</param>
		/// <param name="Value">Found value</param>
		/// <returns>True if the key is found</returns>
		public bool TryGetValueHierarchical(string Name, [NotNullWhen(true)] out string? Value)
		{
			return TryGetValueHierarchical(Name, INDEX_NONE, out Value);
		}

		/// <summary>
		/// Attempt to get the value associated with the key
		/// </summary>
		/// <param name="Name">Name of the meta data</param>
		/// <param name="NameIndex">Enumeration value index or -1 for the type's meta data</param>
		/// <param name="Value">Found value</param>
		/// <returns>True if the key is found</returns>
		public bool TryGetValue(string Name, int NameIndex, [NotNullWhen(true)] out string? Value)
		{
			if (this.Dictionary == null)
			{
				Value = String.Empty;
				return false;
			}
			return this.Dictionary.TryGetValue(new UhtMetaDataKey(Name, NameIndex), out Value);
		}

		/// <summary>
		/// Attempt to get the value associated with the key including parent meta data
		/// </summary>
		/// <param name="Name">Name of the meta data</param>
		/// <param name="NameIndex">Enumeration value index or -1 for the type's meta data</param>
		/// <param name="Value">Found value</param>
		/// <returns>True if the key is found</returns>
		public bool TryGetValueHierarchical(string Name, int NameIndex, [NotNullWhen(true)] out string? Value)
		{
			for (UhtMetaData? Current = this; Current != null; Current = Current.Parent)
			{
				if (Current.TryGetValue(Name, NameIndex, out Value))
				{
					return true;
				}
			}
			Value = String.Empty;
			return false;
		}

		/// <summary>
		/// Get the string value of the given meta data key or the default value.
		/// </summary>
		/// <param name="Name">Name of the meta data key</param>
		/// <param name="NameIndex">Index of the meta data key</param>
		/// <returns>Meta data value or empty string if not found.</returns>
		public string GetValueOrDefault(string Name, int NameIndex = INDEX_NONE)
		{
			string? Out;
			if (TryGetValue(Name, NameIndex, out Out))
			{
				return Out;
			}
			return String.Empty;
		}

		/// <summary>
		/// Get the string value of the given meta data searching the whole meta data chain.
		/// </summary>
		/// <param name="Name">Name of the meta data key</param>
		/// <param name="NameIndex">Index of the meta data key</param>
		/// <returns>Meta data value or empty string if not found.</returns>
		public string GetValueOrDefaultHierarchical(string Name, int NameIndex = INDEX_NONE)
		{
			string? Out;
			if (TryGetValueHierarchical(Name, NameIndex, out Out))
			{
				return Out;
			}
			return string.Empty;
		}

		/// <summary>
		/// Get the boolean value of the given meta data.
		/// </summary>
		/// <param name="Name">Name of the meta data key</param>
		/// <param name="NameIndex">Index of the meta data key</param>
		/// <returns>Boolean value or false if not found</returns>
		public bool GetBoolean(string Name, int NameIndex = INDEX_NONE)
		{
			return UhtFCString.ToBool(GetValueOrDefault(Name, NameIndex));
		}

		/// <summary>
		/// Get the boolean value of the given meta data searching the whole meta data chain.
		/// </summary>
		/// <param name="Name">Name of the meta data key</param>
		/// <param name="NameIndex">Index of the meta data key</param>
		/// <returns>Boolean value or false if not found</returns>
		public bool GetBooleanHierarchical(string Name, int NameIndex = INDEX_NONE)
		{
			return UhtFCString.ToBool(GetValueOrDefaultHierarchical(Name, NameIndex));
		}

		/// <summary>
		/// Get the double value of the given meta data.
		/// </summary>
		/// <param name="Name">Name of the meta data key</param>
		/// <param name="NameIndex">Index of the meta data key</param>
		/// <returns>Double value or zero if not found</returns>
		public double GetDouble(string Name, int NameIndex = INDEX_NONE)
		{
			string Value = GetValueOrDefault(Name, NameIndex);
			double Result;
			if (!double.TryParse(Value, out Result))
			{
				Result = 0;
			}
			return Result;
		}

		/// <summary>
		/// Get the string array value of the given meta data.
		/// </summary>
		/// <param name="Name">Name of the meta data key</param>
		/// <param name="NameIndex">Index of the meta data key</param>
		/// <returns>String array or null if not found</returns>
		public string[]? GetStringArray(string Name, int NameIndex = INDEX_NONE)
		{
			string? Temp;
			if (TryGetValue(Name, NameIndex, out Temp))
			{
				return Temp.ToString().Split(' ', StringSplitOptions.RemoveEmptyEntries);
			}
			return null;
		}

		/// <summary>
		/// Get the string array value of the given meta data searching the whole meta data chain.
		/// </summary>
		/// <param name="Name">Name of the meta data key</param>
		/// <param name="NameIndex">Index of the meta data key</param>
		/// <returns>String array or null if not found</returns>
		public string[]? GetStringArrayHierarchical(string Name, int NameIndex = INDEX_NONE)
		{
			string? Temp;
			if (TryGetValueHierarchical(Name, NameIndex, out Temp))
			{
				return Temp.ToString().Split(' ', StringSplitOptions.RemoveEmptyEntries);
			}
			return null;
		}

		/// <summary>
		/// Add new meta data
		/// </summary>
		/// <param name="Name">Name of the meta data key</param>
		/// <param name="Value">Value of the meta data</param>
		public void Add(string Name, string Value)
		{
			Add(Name, INDEX_NONE, Value);
		}

		/// <summary>
		/// Add new meta data
		/// </summary>
		/// <param name="Name">Name of the meta data key</param>
		/// <param name="NameIndex">Index of the meta data key</param>
		/// <param name="Value">Value of the meta data</param>
		public void Add(string Name, int NameIndex, string Value)
		{
			string RemappedName;
			if (UhtConfig.Instance.RedirectMetaDataKey(Name, out RemappedName))
			{
				if (this.MessageSite != null)
				{
					this.MessageSite.LogWarning(this.LineNumber, $"Remapping old metadata key '{Name}' to new key '{RemappedName}', please update the declaration.");
				}
			}
			GetDictionary()[new UhtMetaDataKey(Name, NameIndex)] = Value;
		}

		/// <summary>
		/// Add new meta data
		/// </summary>
		/// <param name="Name">Name of the meta data key</param>
		/// <param name="Value">Value of the meta data</param>
		/// <param name="NameIndex">Index of the meta data key</param>
		public void Add(string Name, bool Value, int NameIndex = INDEX_NONE)
		{
			Add(Name, NameIndex, Value ? "true" : "false");
		}

		/// <summary>
		/// Add new meta data
		/// </summary>
		/// <param name="Name">Name of the meta data key</param>
		/// <param name="Strings">Value of the meta data</param>
		/// <param name="Separator">Separator to use to join the strings</param>
		public void Add(string Name, List<string> Strings, char Separator = ' ')
		{
			Add(Name, string.Join(Separator, Strings));
		}

		/// <summary>
		/// Add new meta data if there are strings in the value.
		/// </summary>
		/// <param name="Name">Name of the meta data key</param>
		/// <param name="Strings">Value of the meta data</param>
		/// <param name="Separator">Separator to use to join the strings</param>
		public void AddIfNotEmpty(string Name, List<string> Strings, char Separator = ' ')
		{
			if (Strings.Count != 0)
			{
				Add(Name, string.Join(Separator, Strings));
			}
		}

		/// <summary>
		/// Add the meta data from another meta data block
		/// </summary>
		/// <param name="MetaData"></param>
		public void Add(UhtMetaData MetaData)
		{
			if (MetaData.Dictionary != null)
			{
				SortedList<UhtMetaDataKey, string> Dictionary = GetDictionary();
				foreach (KeyValuePair<UhtMetaDataKey, string> Kvp in MetaData.Dictionary)
				{
					Dictionary[Kvp.Key] = Kvp.Value;
				}
			}
		}

		/// <summary>
		/// Remove the given meta data
		/// </summary>
		/// <param name="Name">Name of the meta data key</param>
		/// <param name="NameIndex">Index of the meta data key</param>
		public void Remove(string Name, int NameIndex = INDEX_NONE)
		{
			if (this.Dictionary != null)
			{
				this.Dictionary.Remove(new UhtMetaDataKey(Name, NameIndex));
			}
		}

		/// <summary>
		/// Given a key, return the full meta data name
		/// </summary>
		/// <param name="Key">Meta data key</param>
		/// <returns>Full meta data name</returns>
		public string GetKeyString(UhtMetaDataKey Key)
		{
			return GetKeyString(Key.Name, Key.Index);
		}

		/// <summary>
		/// Given a key, return the full meta data name
		/// </summary>
		/// <param name="Name">Name of the meta data key</param>
		/// <param name="NameIndex">Index of the meta data key</param>
		/// <returns>Full meta data name</returns>
		/// <exception cref="UhtIceException">Thrown if an index is supplied (not -1) and no key conversion interface is set</exception>
		public string GetKeyString(string Name, int NameIndex)
		{
			if (NameIndex == INDEX_NONE)
			{
				return Name;
			}
			else if (KeyConversion != null)
			{
				return KeyConversion.GetMetaDataKey(Name, NameIndex);
			}
			else
			{
				throw new UhtIceException("Attempt to generate an indexed meta data key name but no key conversion interface was set.");
			}
		}

		/// <summary>
		/// Given a type, return an array of all the meta data formatted and sorted by name
		/// </summary>
		/// <returns>List of meta data key and value pairs</returns>
		public List<KeyValuePair<string, string>> GetSorted()
		{
			List<KeyValuePair<string, string>> Out = new List<KeyValuePair<string, string>>(this.Dictionary != null ? this.Dictionary.Count : 0);
			if (this.Dictionary != null && this.Dictionary.Count > 0)
			{
				foreach (KeyValuePair<UhtMetaDataKey, string> Kvp in this.Dictionary)
				{
					Out.Add(new KeyValuePair<string, string>(this.GetKeyString(Kvp.Key), Kvp.Value));
				}

				Out.Sort((KeyValuePair<string, string> Lhs, KeyValuePair<string, string> Rhs) =>
				{
					return StringComparerUE.OrdinalIgnoreCase.Compare(Lhs.Key, Rhs.Key);
				});
			}
			return Out;
		}

		private SortedList<UhtMetaDataKey, string> GetDictionary()
		{
			if (this.Dictionary == null)
			{
				this.Dictionary = new SortedList<UhtMetaDataKey, string>(Comparer);
			}
			return this.Dictionary;
		}
	}
}
