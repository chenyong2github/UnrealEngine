// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Buffers.Text;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using System.Reflection.Metadata.Ecma335;
using System.Text;
using System.Text.RegularExpressions;
using EpicGames.Core;

namespace EpicGames.Perforce
{
	/// <summary>
	/// String constants for perforce values
	/// </summary>
	static class StringConstants
	{
		public static readonly Utf8String True = new Utf8String("true");
		public static readonly Utf8String New = new Utf8String("new");
		public static readonly Utf8String None = new Utf8String("none");
		public static readonly Utf8String Default = new Utf8String("default");
	}

	/// <summary>
	/// Stores cached information about a field with a P4Tag attribute
	/// </summary>
	class CachedTagInfo
	{
		/// <summary>
		/// Name of the tag. Specified in the attribute or inferred from the field name.
		/// </summary>
		public Utf8String Name;

		/// <summary>
		/// Whether this tag is optional or not.
		/// </summary>
		public bool Optional;

		/// <summary>
		/// The field containing the value of this data.
		/// </summary>
		public FieldInfo Field;

		/// <summary>
		/// Parser for this field type
		/// </summary>
		public Action<object, int> SetFromInteger;

		/// <summary>
		/// Parser for this field type
		/// </summary>
		public Action<object, Utf8String> SetFromString;

		/// <summary>
		/// Index into the bitmask of required types
		/// </summary>
		public ulong RequiredTagBitMask;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Name"></param>
		/// <param name="Optional"></param>
		/// <param name="Field"></param>
		/// <param name="RequiredTagBitMask"></param>
		public CachedTagInfo(Utf8String Name, bool Optional, FieldInfo Field, ulong RequiredTagBitMask)
		{
			this.Name = Name;
			this.Optional = Optional;
			this.Field = Field;
			this.RequiredTagBitMask = RequiredTagBitMask;
			this.SetFromInteger = (Obj, Value) => throw new PerforceException($"Field {Name} was not expecting an integer value.");
			this.SetFromString = (Obj, String) => throw new PerforceException($"Field {Name} was not expecting a string value.");
		}
	}

	/// <summary>
	/// Stores cached information about a record
	/// </summary>
	class CachedRecordInfo
	{
		/// <summary>
		/// Delegate type for creating a record instance
		/// </summary>
		/// <returns>New instance</returns>
		public delegate object CreateRecordDelegate();

		/// <summary>
		/// Type of the record
		/// </summary>
		public Type Type;

		/// <summary>
		/// Method to construct this record
		/// </summary>
		public CreateRecordDelegate CreateInstance;

		/// <summary>
		/// List of fields in the record. These should be ordered to match P4 output for maximum efficiency.
		/// </summary>
		public List<CachedTagInfo> Fields = new List<CachedTagInfo>();

		/// <summary>
		/// Map of name to tag info
		/// </summary>
		public Dictionary<Utf8String, CachedTagInfo> NameToInfo = new Dictionary<Utf8String, CachedTagInfo>();

		/// <summary>
		/// Bitmask of all the required tags. Formed by bitwise-or'ing the RequiredTagBitMask fields for each required CachedTagInfo.
		/// </summary>
		public ulong RequiredTagsBitMask;

		/// <summary>
		/// The type of records to create for subelements
		/// </summary>
		public Type? SubElementType;

		/// <summary>
		/// The cached record info for the subelement type
		/// </summary>
		public CachedRecordInfo? SubElementRecordInfo;

		/// <summary>
		/// Field containing subelements
		/// </summary>
		public FieldInfo? SubElementField;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="Type">The record type</param>
		public CachedRecordInfo(Type Type)
		{
			this.Type = Type;

			ConstructorInfo? Constructor = Type.GetConstructor(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance, null, Type.EmptyTypes, null);
			if(Constructor == null)
			{
				throw new PerforceException($"Unable to find default constructor for {Type}");
			}

			DynamicMethod DynamicMethod = new DynamicMethod("_", Type, null);
			ILGenerator Generator = DynamicMethod.GetILGenerator();
			Generator.Emit(OpCodes.Newobj, Constructor);
			Generator.Emit(OpCodes.Ret);
			CreateInstance = (CreateRecordDelegate)DynamicMethod.CreateDelegate(typeof(CreateRecordDelegate));
		}
	}

	/// <summary>
	/// Information about an enum
	/// </summary>
	class CachedEnumInfo
	{
		/// <summary>
		/// The enum type
		/// </summary>
		public Type EnumType;

		/// <summary>
		/// Whether the enum has the [Flags] attribute
		/// </summary>
		public bool bHasFlagsAttribute;

		/// <summary>
		/// Map of name to value
		/// </summary>
		public Dictionary<Utf8String, int> NameToValue = new Dictionary<Utf8String, int>();

		/// <summary>
		/// List of name/value pairs
		/// </summary>
		public List<KeyValuePair<string, int>> NameValuePairs = new List<KeyValuePair<string, int>>();

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="EnumType">The type to construct from</param>
		public CachedEnumInfo(Type EnumType)
		{
			this.EnumType = EnumType;

			bHasFlagsAttribute = EnumType.GetCustomAttribute<FlagsAttribute>() != null;

			FieldInfo[] Fields = EnumType.GetFields(BindingFlags.Public | BindingFlags.Static);
			foreach (FieldInfo Field in Fields)
			{
				PerforceEnumAttribute? Attribute = Field.GetCustomAttribute<PerforceEnumAttribute>();
				if (Attribute != null)
				{
					object? Value = Field.GetValue(null);
					if (Value != null)
					{
						Utf8String Name = new Utf8String(Attribute.Name);
						NameToValue[Name] = (int)Value;

						NameValuePairs.Add(new KeyValuePair<string, int>(Attribute.Name, (int)Value));
					}
				}
			}
		}

		/// <summary>
		/// Parses the given integer as an enum
		/// </summary>
		/// <param name="Value">The value to convert to an enum</param>
		/// <returns>The enum value corresponding to the given value</returns>
		public object ParseInteger(int Value)
		{
			return Enum.ToObject(EnumType, Value);
		}

		/// <summary>
		/// Parses the given text as an enum
		/// </summary>
		/// <param name="Text">The text to parse</param>
		/// <returns>The enum value corresponding to the given text</returns>
		public object ParseString(Utf8String Text)
		{
			return Enum.ToObject(EnumType, ParseToInteger(Text));
		}
			
		/// <summary>
		/// Parses the given text as an enum
		/// </summary>
		/// <param name="Name"></param>
		/// <returns></returns>
		public int ParseToInteger(Utf8String Name)
		{
			if (bHasFlagsAttribute)
			{
				int Result = 0;
				for (int Offset = 0; Offset < Name.Length;)
				{
					if (Name.Span[Offset] == (byte)' ')
					{
						Offset++;
					}
					else
					{
						// Find the end of this name
						int StartOffset = ++Offset;
						while (Offset < Name.Length && Name.Span[Offset] != (byte)' ')
						{
							Offset++;
						}

						// Take the subset
						Utf8String Item = Name.Slice(StartOffset, Offset - StartOffset);

						// Get the value
						int ItemValue;
						if (NameToValue.TryGetValue(Item, out ItemValue))
						{
							Result |= ItemValue;
						}
					}
				}
				return Result;
			}
			else
			{
				int Result;
				NameToValue.TryGetValue(Name, out Result);
				return Result;
			}
		}

		/// <summary>
		/// Parses an enum value, using PerforceEnumAttribute markup for names.
		/// </summary>
		/// <param name="Value">Value of the enum.</param>
		/// <returns>Text for the enum.</returns>
		public string GetEnumText(int Value)
		{
			if (bHasFlagsAttribute)
			{
				List<string> Names = new List<string>();

				int CombinedIntegerValue = 0;
				foreach (KeyValuePair<string, int> Pair in NameValuePairs)
				{
					if ((Value & Pair.Value) != 0)
					{
						Names.Add(Pair.Key);
						CombinedIntegerValue |= Pair.Value;
					}
				}

				if (CombinedIntegerValue != Value)
				{
					throw new ArgumentException($"Invalid enum value {Value}");
				}

				return String.Join(" ", Names);
			}
			else
			{
				string? Name = null;
				foreach (KeyValuePair<string, int> Pair in NameValuePairs)
				{
					if (Value == Pair.Value)
					{
						Name = Pair.Key;
						break;
					}
				}

				if (Name == null)
				{
					throw new ArgumentException($"Invalid enum value {Value}");
				}
				return Name;
			}
		}
	}

	/// <summary>
	/// Utility methods for converting to/from native types
	/// </summary>
	static class PerforceReflection
	{
		/// <summary>
		/// Unix epoch; used for converting times back into C# datetime objects
		/// </summary>
		public static readonly DateTime UnixEpoch = new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc);

		/// <summary>
		/// Constant for the default changelist, where valid.
		/// </summary>
		public const int DefaultChange = -2;

		/// <summary>
		/// Cached map of enum types to a lookup mapping from p4 strings to enum values.
		/// </summary>
		static ConcurrentDictionary<Type, CachedEnumInfo> EnumTypeToInfo = new ConcurrentDictionary<Type, CachedEnumInfo>();

		/// <summary>
		/// Cached set of record 
		/// </summary>
		static ConcurrentDictionary<Type, CachedRecordInfo> RecordTypeToInfo = new ConcurrentDictionary<Type, CachedRecordInfo>();

		/// <summary>
		/// Default type for info
		/// </summary>
		public static CachedRecordInfo InfoRecordInfo = GetCachedRecordInfo(typeof(PerforceInfo));

		/// <summary>
		/// Default type for errors
		/// </summary>
		public static CachedRecordInfo ErrorRecordInfo = GetCachedRecordInfo(typeof(PerforceError));

		/// <summary>
		/// Gets a mapping of flags to enum values for the given type
		/// </summary>
		/// <param name="EnumType">The enum type to retrieve flags for</param>
		/// <returns>Map of name to enum value</returns>
		static CachedEnumInfo GetCachedEnumInfo(Type EnumType)
		{
			CachedEnumInfo? EnumInfo;
			if (!EnumTypeToInfo.TryGetValue(EnumType, out EnumInfo))
			{
				EnumInfo = new CachedEnumInfo(EnumType);
				if (!EnumTypeToInfo.TryAdd(EnumType, EnumInfo))
				{
					EnumInfo = EnumTypeToInfo[EnumType];
				}
			}
			return EnumInfo;
		}

		/// <summary>
		/// Parses an enum value, using PerforceEnumAttribute markup for names.
		/// </summary>
		/// <param name="EnumType">Type of the enum to parse.</param>
		/// <param name="Value">Value of the enum.</param>
		/// <returns>Text for the enum.</returns>
		public static string GetEnumText(Type EnumType, object Value)
		{
			return GetCachedEnumInfo(EnumType).GetEnumText((int)Value);
		}

		/// <summary>
		/// Gets reflection data for the given record type
		/// </summary>
		/// <param name="RecordType">The type to retrieve record info for</param>
		/// <returns>The cached reflection information for the given type</returns>
		public static CachedRecordInfo GetCachedRecordInfo(Type RecordType)
		{
			CachedRecordInfo? Record;
			if (!RecordTypeToInfo.TryGetValue(RecordType, out Record))
			{
				Record = new CachedRecordInfo(RecordType);

				// Get all the fields for this type
				FieldInfo[] Fields = RecordType.GetFields(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance);

				// Build the map of all tags for this record
				foreach (FieldInfo Field in Fields)
				{
					PerforceTagAttribute? TagAttribute = Field.GetCustomAttribute<PerforceTagAttribute>();
					if (TagAttribute != null)
					{
						string TagName = TagAttribute.Name ?? Field.Name;

						ulong RequiredTagBitMask = 0;
						if (!TagAttribute.Optional)
						{
							RequiredTagBitMask = Record.RequiredTagsBitMask + 1;
							if (RequiredTagBitMask == 0)
							{
								throw new PerforceException("Too many required tags in {0}; max is {1}", RecordType.Name, sizeof(ulong) * 8);
							}
							Record.RequiredTagsBitMask |= RequiredTagBitMask;
						}

						CachedTagInfo TagInfo = new CachedTagInfo(new Utf8String(TagName), TagAttribute.Optional, Field, RequiredTagBitMask);

						Type FieldType = Field.FieldType;

						FieldInfo FieldCopy = Field;
						if (FieldType == typeof(DateTime))
						{
							TagInfo.SetFromString = (Obj, String) => FieldCopy.SetValue(Obj, ParseStringAsDateTime(String));
						}
						else if (FieldType == typeof(bool))
						{
							TagInfo.SetFromString = (Obj, String) => FieldCopy.SetValue(Obj, ParseStringAsBool(String));
						}
						else if (FieldType == typeof(Nullable<bool>))
						{
							TagInfo.SetFromString = (Obj, String) => FieldCopy.SetValue(Obj, ParseStringAsNullableBool(String));
						}
						else if (FieldType == typeof(int))
						{
							TagInfo.SetFromInteger = (Obj, Int) => FieldCopy.SetValue(Obj, Int);
							TagInfo.SetFromString = (Obj, String) => FieldCopy.SetValue(Obj, ParseStringAsInt(String));
						}
						else if (FieldType == typeof(long))
						{
							TagInfo.SetFromString = (Obj, String) => FieldCopy.SetValue(Obj, ParseStringAsLong(String));
						}
						else if (FieldType == typeof(string))
						{
							TagInfo.SetFromString = (Obj, String) => FieldCopy.SetValue(Obj, ParseString(String));
						}
						else if (FieldType == typeof(Utf8String))
						{
							TagInfo.SetFromString = (Obj, String) => FieldCopy.SetValue(Obj, String.Clone());
						}
						else if (FieldType.IsEnum)
						{
							CachedEnumInfo EnumInfo = GetCachedEnumInfo(FieldType);
							TagInfo.SetFromInteger = (Obj, Int) => FieldCopy.SetValue(Obj, EnumInfo.ParseInteger(Int));
							TagInfo.SetFromString = (Obj, String) => FieldCopy.SetValue(Obj, EnumInfo.ParseString(String));
						}
						else if (FieldType == typeof(DateTimeOffset?))
						{
							TagInfo.SetFromString = (Obj, String) => FieldCopy.SetValue(Obj, ParseStringAsNullableDateTimeOffset(String));
						}
						else if (FieldType == typeof(List<string>))
						{
							TagInfo.SetFromString = (Obj, String) => ((List<string>)FieldCopy.GetValue(Obj)!).Add(String.ToString());
						}
						else
						{
							throw new PerforceException("Unsupported type of {0}.{1} for tag '{2}'", RecordType.Name, FieldType.Name, TagName);
						}

						Record.Fields.Add(TagInfo);
					}

					Record.NameToInfo = Record.Fields.ToDictionary(x => x.Name, x => x);

					PerforceRecordListAttribute? SubElementAttribute = Field.GetCustomAttribute<PerforceRecordListAttribute>();
					if (SubElementAttribute != null)
					{
						Record.SubElementField = Field;
						Record.SubElementType = Field.FieldType.GenericTypeArguments[0];
						Record.SubElementRecordInfo = GetCachedRecordInfo(Record.SubElementType);
					}
				}

				// Try to save the record info, or get the version that's already in the cache
				if (!RecordTypeToInfo.TryAdd(RecordType, Record))
				{
					Record = RecordTypeToInfo[RecordType];
				}
			}
			return Record;
		}

		static object ParseString(Utf8String String)
		{
			return String.ToString();
		}

		static object ParseStringAsDateTime(Utf8String String)
		{
			string Text = String.ToString();

			DateTime Time;
			if (DateTime.TryParse(Text, out Time))
			{
				return Time;
			}
			else
			{
				return PerforceReflection.UnixEpoch + TimeSpan.FromSeconds(long.Parse(Text));
			}
		}

		static object ParseStringAsBool(Utf8String String)
		{
			return String.Length == 0 || String == StringConstants.True;
		}

		static object ParseStringAsNullableBool(Utf8String String)
		{
			return String == StringConstants.True;
		}

		static object ParseStringAsInt(Utf8String String)
		{
			int Value;
			int BytesConsumed;
			if (Utf8Parser.TryParse(String.Span, out Value, out BytesConsumed) && BytesConsumed == String.Length)
			{
				return Value;
			}
			else if(String == StringConstants.New || String == StringConstants.None)
			{
				return -1;
			}
			else if(String.Length > 0 && String[0] == '#')
			{
				return ParseStringAsInt(String.Slice(1));
			}
			else if(String == StringConstants.Default)
			{
				return DefaultChange;
			}
			else
			{
				throw new PerforceException($"Unable to parse {String} as an integer");
			}
		}

		static object ParseStringAsLong(Utf8String String)
		{
			long Value;
			int BytesConsumed;
			if (!Utf8Parser.TryParse(String.Span, out Value, out BytesConsumed) || BytesConsumed != String.Length)
			{
				throw new PerforceException($"Unable to parse {String} as a long value");
			}
			return Value;
		}

		static object ParseStringAsNullableDateTimeOffset(Utf8String String)
		{
			string Text = String.ToString();
			return DateTimeOffset.Parse(Regex.Replace(Text, "[a-zA-Z ]*$", "")); // Strip timezone name (eg. "EST")
		}
	}
}
