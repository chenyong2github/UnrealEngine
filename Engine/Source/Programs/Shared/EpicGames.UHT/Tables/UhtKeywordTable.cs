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
	public sealed class UhtKeywordAttribute : Attribute
	{

		/// <summary>
		/// Keyword table/scope being extended
		/// </summary>
		public string? Extends;

		/// <summary>
		/// Name of the keyword
		/// </summary>
		public string? Keyword = null;

		/// <summary>
		/// Text to be displayed to the user when referencing this keyword
		/// </summary>
		public string? AllowText = null;

		/// <summary>
		/// If true, this applies to all scopes
		/// </summary>
		public bool AllScopes = false;

		/// <summary>
		/// If true, do not include in usage errors
		/// </summary>
		public bool DisableUsageError = false;
	}

	/// <summary>
	/// Invoked as a last chance processor for a keyword
	/// </summary>
	[AttributeUsage(AttributeTargets.Method, AllowMultiple = true)]
	public sealed class UhtKeywordCatchAllAttribute : Attribute
	{

		/// <summary>
		/// Table/scope to be extended
		/// </summary>
		public string? Extends;
	}

	/// <summary>
	/// Delegate to notify a keyword was parsed
	/// </summary>
	/// <param name="TopScope">Current scope being parsed</param>
	/// <param name="ActionScope">The scope who's table was matched</param>
	/// <param name="Token">Matching token</param>
	/// <returns>Results of the parsing</returns>
	public delegate UhtParseResult UhtKeywordDelegate(UhtParsingScope TopScope, UhtParsingScope ActionScope, ref UhtToken Token);

	/// <summary>
	/// Delegate to invoke as a last chance processor for a keyword
	/// </summary>
	/// <param name="TopScope">Current scope being parsed</param>
	/// <param name="Token">Matching token</param>
	/// <returns>Results of the parsing</returns>
	public delegate UhtParseResult UhtKeywordCatchAllDelegate(UhtParsingScope TopScope, ref UhtToken Token);

	/// <summary>
	/// Defines a keyword
	/// </summary>
	public struct UhtKeyword
	{

		/// <summary>
		/// Name of the keyword
		/// </summary>
		public readonly string Name;

		/// <summary>
		/// Delegate to invoke
		/// </summary>
		public readonly UhtKeywordDelegate Delegate;

		/// <summary>
		/// Text to be displayed to the user when referencing this keyword
		/// </summary>
		public readonly string? AllowText;

		/// <summary>
		/// If true, this applies to all scopes
		/// </summary>
		public readonly bool bAllScopes;

		/// <summary>
		/// If true, do not include in usage errors
		/// </summary>
		public readonly bool bDisableUsageError;

		/// <summary>
		/// Construct a new keyword
		/// </summary>
		/// <param name="Name">Name of the keyword</param>
		/// <param name="Delegate">Delegate to invoke</param>
		/// <param name="Attribute">Defining attribute</param>
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

	/// <summary>
	/// Keyword table for a specific scope
	/// </summary>
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
		/// <param name="BaseTable">Base table being merged</param>
		public override void Merge(UhtLookupTableBase BaseTable)
		{
			base.Merge(BaseTable);
			this.CatchAlls.AddRange(((UhtKeywordTable)BaseTable).CatchAlls);
		}
	}

	/// <summary>
	/// Table of all keyword tables
	/// </summary>
	public class UhtKeywordTables : UhtLookupTables<UhtKeywordTable>
	{

		/// <summary>
		/// Construct the keyword tables
		/// </summary>
		public UhtKeywordTables() : base("keywords")
		{
		}

		/// <summary>
		/// Handle a keyword attribute
		/// </summary>
		/// <param name="Type">Containing type</param>
		/// <param name="MethodInfo">Method information</param>
		/// <param name="KeywordCatchAllAttribute">Defining attribute</param>
		/// <exception cref="UhtIceException">Thrown if the attribute isn't well defined</exception>
		public void OnKeywordCatchAllAttribute(Type Type, MethodInfo MethodInfo, UhtKeywordCatchAllAttribute KeywordCatchAllAttribute)
		{
			if (string.IsNullOrEmpty(KeywordCatchAllAttribute.Extends))
			{
				throw new UhtIceException($"The 'KeywordCatchAlll' attribute on the {Type.Name}.{MethodInfo.Name} method doesn't have a table specified.");
			}

			UhtKeywordTable Table = Get(KeywordCatchAllAttribute.Extends);
			Table.AddCatchAll((UhtKeywordCatchAllDelegate)Delegate.CreateDelegate(typeof(UhtKeywordCatchAllDelegate), MethodInfo));
		}

		/// <summary>
		/// Handle a keyword attribute
		/// </summary>
		/// <param name="Type">Containing type</param>
		/// <param name="MethodInfo">Method information</param>
		/// <param name="KeywordAttribute">Defining attribute</param>
		/// <exception cref="UhtIceException">Thrown if the attribute isn't well defined</exception>
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

		/// <summary>
		/// Log an unhandled error
		/// </summary>
		/// <param name="MessageSite">Destination message site</param>
		/// <param name="Token">Keyword</param>
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
