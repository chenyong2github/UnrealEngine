// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Reflection;
using EpicGames.Core;
using EpicGames.UHT.Parsers;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Tables
{

	/// <summary>
	/// Invoke the given method when the keyword is parsed.
	/// </summary>
	[AttributeUsage(AttributeTargets.Method, AllowMultiple = true)]
	public class UhtKeywordAttribute : Attribute
	{
		public string? Extends;
		public string? Keyword = null;
		public string? AllowText = null;
		public bool AllScopes = false;
		public bool DisableUsageError = false;
	}

	[AttributeUsage(AttributeTargets.Method, AllowMultiple = true)]
	public class UhtKeywordCatchAllAttribute : Attribute
	{
		public string? Extends;
	}

	public delegate UhtParseResult UhtKeywordDelegate(UhtParsingScope TopScope, UhtParsingScope ActionScope, ref UhtToken Token);
	public delegate UhtParseResult UhtKeywordCatchAllDelegate(UhtParsingScope TopScope, ref UhtToken Token);

	public struct UhtKeyword
	{
		public readonly string Name;
		public readonly UhtKeywordDelegate Delegate;
		public readonly string? AllowText;
		public readonly bool bAllScopes;
		public readonly bool bDisableUsageError;

		public UhtKeyword(string Name, UhtKeywordDelegate Delegate, UhtKeywordAttribute? Attribute)
		{
			this.Name = Name;
			this.Delegate = Delegate;
			if (Attribute != null)
			{
				this.AllowText = Attribute.AllowText;
				this.bAllScopes = Attribute.AllScopes;
				this.bDisableUsageError = Attribute.DisableUsageError;
			}
			else
			{
				this.AllowText = null;
				this.bAllScopes = false;
				this.bDisableUsageError = false;
			}
		}
	}

	public class UhtKeywordTable : UhtLookupTable<UhtKeyword>
	{

		/// <summary>
		/// List of catch-alls associated with this table
		/// </summary>
		public List<UhtKeywordCatchAllDelegate> CatchAlls = new List<UhtKeywordCatchAllDelegate>();

		/// <summary>
		/// Construct a new keyword table
		/// </summary>
		public UhtKeywordTable() : base(StringViewComparer.Ordinal)
		{
		}

		/// <summary>
		/// Add the given value to the lookup table.  It will throw an exception if it is a duplicate.
		/// </summary>
		/// <param name="Value">Value to be added</param>
		public UhtKeywordTable Add(UhtKeyword Value)
		{
			base.Add(Value.Name, Value);
			return this;
		}

		/// <summary>
		/// Add the given catch-all to the table.
		/// </summary>
		/// <param name="CatchAll">The catch-all to be added</param>
		public UhtKeywordTable AddCatchAll(UhtKeywordCatchAllDelegate CatchAll)
		{
			this.CatchAlls.Add(CatchAll);
			return this;
		}

		/// <summary>
		/// Merge the given keyword table.  Duplicates in the BaseTypeTable will be ignored.
		/// </summary>
		/// <param name="BaseTypeTable">Base table being merged</param>
		public override void Merge(UhtLookupTableBase BaseTable)
		{
			base.Merge(BaseTable);
			this.CatchAlls.AddRange(((UhtKeywordTable)BaseTable).CatchAlls);
		}
	}

	public class UhtKeywordTables : UhtLookupTables<UhtKeywordTable>
	{
		public static UhtKeywordTables Instance = new UhtKeywordTables();

		public UhtKeywordTables() : base("keywords")
		{
		}

		public void OnKeywordCatchAllAttribute(Type Type, MethodInfo MethodInfo, UhtKeywordCatchAllAttribute KeywordCatchAllAttribute)
		{
			if (string.IsNullOrEmpty(KeywordCatchAllAttribute.Extends))
			{
				throw new UhtIceException($"The 'KeywordCatchAlll' attribute on the {Type.Name}.{MethodInfo.Name} method doesn't have a table specified.");
			}

			UhtKeywordTable Table = Get(KeywordCatchAllAttribute.Extends);
			Table.AddCatchAll((UhtKeywordCatchAllDelegate)Delegate.CreateDelegate(typeof(UhtKeywordCatchAllDelegate), MethodInfo));
		}

		public void OnKeywordAttribute(Type Type, MethodInfo MethodInfo, UhtKeywordAttribute KeywordAttribute)
		{
			string Name = UhtLookupTableBase.GetSuffixedName(Type, MethodInfo, KeywordAttribute.Keyword, "Keyword");

			if (string.IsNullOrEmpty(KeywordAttribute.Extends))
			{
				throw new UhtIceException($"The 'Keyword' attribute on the {Type.Name}.{MethodInfo.Name} method doesn't have a table specified.");
			}

			UhtKeywordTable Table = Get(KeywordAttribute.Extends);
			Table.Add(new UhtKeyword(Name, (UhtKeywordDelegate)Delegate.CreateDelegate(typeof(UhtKeywordDelegate), MethodInfo), KeywordAttribute));
		}

		public void LogUnhandledError(IUhtMessageSite MessageSite, UhtToken Token)
		{
			List<string>? Tables = null;
			foreach (KeyValuePair<string, UhtKeywordTable> KVP in this.Tables)
			{
				UhtKeywordTable KeywordTable = KVP.Value;
				if (KeywordTable.Internal)
				{
					continue;
				}
				UhtKeyword Info;
				if (KeywordTable.TryGetValue(Token.Value, out Info))
				{
					if (Info.bDisableUsageError)
					{
						// Do not log anything for this keyword in this table
					}
					else
					{
						if (Tables == null)
						{
							Tables = new List<string>();
						}
						if (Info.AllowText != null)
						{
							Tables.Add($"{KeywordTable.UserName} {Info.AllowText}");
						}
						else
						{
							Tables.Add(KeywordTable.UserName);
						}
					}
				}
			}

			if (Tables != null)
			{
				string Text = UhtUtilities.MergeTypeNames(Tables, "and");
				MessageSite.LogError(Token.InputLine, $"Invalid use of keyword '{Token.Value}'.  It may only appear in {Text} scopes");
			}
		}
	}
}
