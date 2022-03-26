// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Parsers; // It would be nice if we didn't need this here
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.Reflection;

namespace EpicGames.UHT.Tables
{

	/// <summary>
	/// Defines the different types specifiers relating to their allowed values
	/// </summary>
	public enum UhtSpecifierValueType
	{
		/// <summary>
		/// Internal value used to detect if the attribute has a valid value
		/// </summary>
		NotSet,

		/// <summary>
		/// No values of any type are allowed
		/// </summary>
		None,

		/// <summary>
		/// A string value but can not be in the form of a list (i.e. =(bob))
		/// </summary>
		String,

		/// <summary>
		/// An optional string value but can not be in the form of a list
		/// </summary>
		OptionalString,

		/// <summary>
		/// A string value or a single element string list
		/// </summary>
		SingleString,

		/// <summary>
		/// A list of values in key=value pairs
		/// </summary>
		KeyValuePairList,

		/// <summary>
		/// A list of values in key=value pairs but the equals is optional
		/// </summary>
		OptionalEqualsKeyValuePairList,

		/// <summary>
		/// A list of values.
		/// </summary>
		StringList,

		/// <summary>
		/// A list of values and must contain at least one entry
		/// </summary>
		NonEmptyStringList,

		/// <summary>
		/// Accepts a string list but the value is ignored by the specifier and is automatically deferred.  This is for legacy UHT support.
		/// </summary>
		Legacy,
	}

	/// <summary>
	/// Results from dispatching a specifier
	/// </summary>
	public enum UhtSpecifierDispatchResults
	{

		/// <summary>
		/// Specifier was known and parsed
		/// </summary>
		Known,

		/// <summary>
		/// Specified was unknown
		/// </summary>
		Unknown,
	}

	/// <summary>
	/// The specifier context provides the default and simplest information about the specifiers being processed
	/// </summary>
	public class UhtSpecifierContext
	{

		/// <summary>
		/// Current parsing scope (i.e. global, class, ...)
		/// </summary>
		public UhtParsingScope Scope;

		/// <summary>
		/// Message site for messages
		/// </summary>
		public IUhtMessageSite MessageSite;

		/// <summary>
		/// Meta data currently being parsed.
		/// </summary>
		public UhtMetaData MetaData;

		/// <summary>
		/// Make data key index utilized by enumeration values
		/// </summary>
		public int MetaNameIndex;

		/// <summary>
		/// Construct a new specifier context
		/// </summary>
		/// <param name="Scope"></param>
		/// <param name="MessageSite"></param>
		/// <param name="MetaData"></param>
		/// <param name="MetaNameIndex"></param>
		public UhtSpecifierContext(UhtParsingScope Scope, IUhtMessageSite MessageSite, UhtMetaData MetaData, int MetaNameIndex = UhtMetaData.INDEX_NONE)
		{
			this.Scope = Scope;
			this.MessageSite = MessageSite;
			this.MetaData = MetaData;
			this.MetaNameIndex = MetaNameIndex;
		}
	}

	/// <summary>
	/// Specifiers are either processed immediately when the declaration is parse or deferred until later in the parsing of the object
	/// </summary>
	public enum UhtSpecifierWhen
	{
		/// <summary>
		/// Specifier is parsed when the meta data section is parsed.
		/// </summary>
		Immediate,

		/// <summary>
		/// Specifier is executed after more of the object is parsed (but usually before members are parsed)
		/// </summary>
		Deferred,
	}

	/// <summary>
	/// The specifier table contains an instance of UhtSpecifier which is used to dispatch the parsing of
	/// a specifier to the implementation
	/// </summary>
	public abstract class UhtSpecifier
	{

		/// <summary>
		/// Name of the specifier
		/// </summary>
		public string Name = String.Empty;

		/// <summary>
		/// Expected value type
		/// </summary>
		public UhtSpecifierValueType ValueType;

		/// <summary>
		/// When is the specifier executed
		/// </summary>
		public UhtSpecifierWhen When = UhtSpecifierWhen.Deferred;

		/// <summary>
		/// Dispatch an instance of the specifier
		/// </summary>
		/// <param name="SpecifierContext">Current context</param>
		/// <param name="Value">Specifier value</param>
		/// <returns>Results of the dispatch</returns>
		public abstract UhtSpecifierDispatchResults Dispatch(UhtSpecifierContext SpecifierContext, object? Value);
	}

	/// <summary>
	/// Delegate for a specifier with no value
	/// </summary>
	/// <param name="SpecifierContext"></param>
	public delegate void UhtSpecifierNoneDelegate(UhtSpecifierContext SpecifierContext);

	/// <summary>
	/// Specifier with no value
	/// </summary>
	public class UhtSpecifierNone : UhtSpecifier
	{
		private UhtSpecifierNoneDelegate Delegate;

		/// <summary>
		/// Construct the specifier
		/// </summary>
		/// <param name="Name">Name of the specifier</param>
		/// <param name="When">When the specifier is executed</param>
		/// <param name="Delegate">Delegate to invoke</param>
		public UhtSpecifierNone(string Name, UhtSpecifierWhen When, UhtSpecifierNoneDelegate Delegate)
		{
			this.Name = Name;
			this.ValueType = UhtSpecifierValueType.None;
			this.When = When;
			this.Delegate = Delegate;
		}

		/// <inheritdoc/>
		public override UhtSpecifierDispatchResults Dispatch(UhtSpecifierContext SpecifierContext, object? Value)
		{
			this.Delegate(SpecifierContext);
			return UhtSpecifierDispatchResults.Known;
		}
	}

	/// <summary>
	/// Specifier delegate with a string value
	/// </summary>
	/// <param name="SpecifierContext">Specifier context</param>
	/// <param name="Value">Specifier value</param>
	public delegate void UhtSpecifierStringDelegate(UhtSpecifierContext SpecifierContext, StringView Value);

	/// <summary>
	/// Specifier with a string value
	/// </summary>
	public class UhtSpecifierString : UhtSpecifier
	{
		private UhtSpecifierStringDelegate Delegate;

		/// <summary>
		/// Construct the specifier
		/// </summary>
		/// <param name="Name">Name of the specifier</param>
		/// <param name="When">When the specifier is executed</param>
		/// <param name="Delegate">Delegate to invoke</param>
		public UhtSpecifierString(string Name, UhtSpecifierWhen When, UhtSpecifierStringDelegate Delegate)
		{
			this.Name = Name;
			this.ValueType = UhtSpecifierValueType.String;
			this.When = When;
			this.Delegate = Delegate;
		}

		/// <inheritdoc/>
		public override UhtSpecifierDispatchResults Dispatch(UhtSpecifierContext SpecifierContext, object? Value)
		{
			if (Value == null)
			{
				throw new UhtIceException("Required value is null");
			}
			this.Delegate(SpecifierContext, (StringView)Value);
			return UhtSpecifierDispatchResults.Known;
		}
	}

	/// <summary>
	/// Specifier delegate with an optional string value
	/// </summary>
	/// <param name="SpecifierContext">Specifier context</param>
	/// <param name="Value">Specifier value</param>
	public delegate void UhtSpecifierOptionalStringDelegate(UhtSpecifierContext SpecifierContext, StringView? Value);

	/// <summary>
	/// Specifier with an optional string value
	/// </summary>
	public class UhtSpecifierOptionalString : UhtSpecifier
	{
		private UhtSpecifierOptionalStringDelegate Delegate;

		/// <summary>
		/// Construct the specifier
		/// </summary>
		/// <param name="Name">Name of the specifier</param>
		/// <param name="When">When the specifier is executed</param>
		/// <param name="Delegate">Delegate to invoke</param>
		public UhtSpecifierOptionalString(string Name, UhtSpecifierWhen When, UhtSpecifierOptionalStringDelegate Delegate)
		{
			this.Name = Name;
			this.ValueType = UhtSpecifierValueType.OptionalString;
			this.When = When;
			this.Delegate = Delegate;
		}

		/// <inheritdoc/>
		public override UhtSpecifierDispatchResults Dispatch(UhtSpecifierContext SpecifierContext, object? Value)
		{
			this.Delegate(SpecifierContext, (StringView?)Value);
			return UhtSpecifierDispatchResults.Known;
		}
	}

	/// <summary>
	/// Specifier delegate with a string value
	/// </summary>
	/// <param name="SpecifierContext">Specifier context</param>
	/// <param name="Value">Specifier value</param>
	public delegate void UhtSpecifierSingleStringDelegate(UhtSpecifierContext SpecifierContext, StringView Value);

	/// <summary>
	/// Specifier with a string value
	/// </summary>
	public class UhtSpecifierSingleString : UhtSpecifier
	{
		private UhtSpecifierSingleStringDelegate Delegate;

		/// <summary>
		/// Construct the specifier
		/// </summary>
		/// <param name="Name">Name of the specifier</param>
		/// <param name="When">When the specifier is executed</param>
		/// <param name="Delegate">Delegate to invoke</param>
		public UhtSpecifierSingleString(string Name, UhtSpecifierWhen When, UhtSpecifierSingleStringDelegate Delegate)
		{
			this.Name = Name;
			this.ValueType = UhtSpecifierValueType.SingleString;
			this.When = When;
			this.Delegate = Delegate;
		}

		/// <inheritdoc/>
		public override UhtSpecifierDispatchResults Dispatch(UhtSpecifierContext SpecifierContext, object? Value)
		{
			if (Value == null)
			{
				throw new UhtIceException("Required value is null");
			}
			this.Delegate(SpecifierContext, (StringView)Value);
			return UhtSpecifierDispatchResults.Known;
		}
	}

	/// <summary>
	/// Specifier delegate with list of string keys and values 
	/// </summary>
	/// <param name="SpecifierContext">Specifier context</param>
	/// <param name="Value">Specifier value</param>
	public delegate void UhtSpecifierKeyValuePairListDelegate(UhtSpecifierContext SpecifierContext, List<KeyValuePair<StringView, StringView>> Value);

	/// <summary>
	/// Specifier with list of string keys and values 
	/// </summary>
	public class UhtSpecifierKeyValuePairList : UhtSpecifier
	{
		private UhtSpecifierKeyValuePairListDelegate Delegate;

		/// <summary>
		/// Construct the specifier
		/// </summary>
		/// <param name="Name">Name of the specifier</param>
		/// <param name="When">When the specifier is executed</param>
		/// <param name="bEqualsOptional">If true this has an optional KVP list</param>
		/// <param name="Delegate">Delegate to invoke</param>
		public UhtSpecifierKeyValuePairList(string Name, UhtSpecifierWhen When, bool bEqualsOptional, UhtSpecifierKeyValuePairListDelegate Delegate)
		{
			this.Name = Name;
			this.ValueType = bEqualsOptional ? UhtSpecifierValueType.OptionalEqualsKeyValuePairList : UhtSpecifierValueType.KeyValuePairList;
			this.When = When;
			this.Delegate = Delegate;
		}

		/// <inheritdoc/>
		public override UhtSpecifierDispatchResults Dispatch(UhtSpecifierContext SpecifierContext, object? Value)
		{
			if (Value == null)
			{
				throw new UhtIceException("Required value is null");
			}
			this.Delegate(SpecifierContext, (List<KeyValuePair<StringView, StringView>>)Value);
			return UhtSpecifierDispatchResults.Known;
		}
	}

	/// <summary>
	/// Specifier delegate with no value
	/// </summary>
	/// <param name="SpecifierContext">Specifier context</param>
	public delegate void UhtSpecifierLegacyDelegate(UhtSpecifierContext SpecifierContext);

	/// <summary>
	/// Specifier delegate for legacy UHT specifiers with no value.  Will generate a information/deprecation message
	/// is a value is supplied
	/// </summary>
	public class UhtSpecifierLegacy : UhtSpecifier
	{
		private UhtSpecifierLegacyDelegate Delegate;

		/// <summary>
		/// Construct the specifier
		/// </summary>
		/// <param name="Name">Name of the specifier</param>
		/// <param name="Delegate">Delegate to invoke</param>
		public UhtSpecifierLegacy(string Name, UhtSpecifierLegacyDelegate Delegate)
		{
			this.Name = Name;
			this.ValueType = UhtSpecifierValueType.StringList;
			this.When = UhtSpecifierWhen.Deferred;
			this.Delegate = Delegate;
		}

		/// <inheritdoc/>
		public override UhtSpecifierDispatchResults Dispatch(UhtSpecifierContext SpecifierContext, object? Value)
		{
			if (Value != null)
			{
				SpecifierContext.Scope.TokenReader.LogInfo($"Specifier '{this.Name}' has a value which is unused, future versions of UnrealHeaderTool will flag this as an error.");
			}
			this.Delegate(SpecifierContext);
			return UhtSpecifierDispatchResults.Known;
		}
	}

	/// <summary>
	/// Specifier delegate with an optional string list
	/// </summary>
	/// <param name="SpecifierContext">Specifier context</param>
	/// <param name="Value">Specifier value</param>
	public delegate void UhtSpecifierStringListDelegate(UhtSpecifierContext SpecifierContext, List<StringView>? Value);

	/// <summary>
	/// Specifier with an optional string list
	/// </summary>
	public class UhtSpecifierStringList : UhtSpecifier
	{
		private UhtSpecifierStringListDelegate Delegate;

		/// <summary>
		/// Construct the specifier
		/// </summary>
		/// <param name="Name">Name of the specifier</param>
		/// <param name="When">When the specifier is executed</param>
		/// <param name="Delegate">Delegate to invoke</param>
		public UhtSpecifierStringList(string Name, UhtSpecifierWhen When, UhtSpecifierStringListDelegate Delegate)
		{
			this.Name = Name;
			this.ValueType = UhtSpecifierValueType.StringList;
			this.When = When;
			this.Delegate = Delegate;
		}

		/// <inheritdoc/>
		public override UhtSpecifierDispatchResults Dispatch(UhtSpecifierContext SpecifierContext, object? Value)
		{
			this.Delegate(SpecifierContext, (List<StringView>?)Value);
			return UhtSpecifierDispatchResults.Known;
		}
	}

	/// <summary>
	/// Specifier delegate with a list of string views
	/// </summary>
	/// <param name="SpecifierContext">Specifier context</param>
	/// <param name="Value">Specifier value</param>
	public delegate void UhtSpecifierNonEmptyStringListDelegate(UhtSpecifierContext SpecifierContext, List<StringView> Value);

	/// <summary>
	/// Specifier with a list of string views
	/// </summary>
	public class UhtSpecifierNonEmptyStringList : UhtSpecifier
	{
		private UhtSpecifierNonEmptyStringListDelegate Delegate;

		/// <summary>
		/// Construct the specifier
		/// </summary>
		/// <param name="Name">Name of the specifier</param>
		/// <param name="When">When the specifier is executed</param>
		/// <param name="Delegate">Delegate to invoke</param>
		public UhtSpecifierNonEmptyStringList(string Name, UhtSpecifierWhen When, UhtSpecifierNonEmptyStringListDelegate Delegate)
		{
			this.Name = Name;
			this.ValueType = UhtSpecifierValueType.NonEmptyStringList;
			this.When = When;
			this.Delegate = Delegate;
		}

		/// <inheritdoc/>
		public override UhtSpecifierDispatchResults Dispatch(UhtSpecifierContext SpecifierContext, object? Value)
		{
			if (Value == null)
			{
				throw new UhtIceException("Required value is null");
			}
			this.Delegate(SpecifierContext, (List<StringView>)Value);
			return UhtSpecifierDispatchResults.Known;
		}
	}

	/// <summary>
	/// Defines a specifier method
	/// </summary>
	[AttributeUsage(AttributeTargets.Method, AllowMultiple = true)]
	public class UhtSpecifierAttribute : Attribute
	{
		/// <summary>
		/// Name of the specifier.   If not supplied, the method name must end in "Specifier" and the name will be the method name with "Specifier" stripped.
		/// </summary>
		public string? Name;

		/// <summary>
		/// Name of the table/scope this specifier applies
		/// </summary>
		public string? Extends;

		/// <summary>
		/// Value type of the specifier
		/// </summary>
		public UhtSpecifierValueType ValueType = UhtSpecifierValueType.NotSet;

		/// <summary>
		/// When the specifier is dispatched
		/// </summary>
		public UhtSpecifierWhen When = UhtSpecifierWhen.Deferred;
	}

	/// <summary>
	/// Collection of specifiers for a given scope
	/// </summary>
	public class UhtSpecifierTable : UhtLookupTable<UhtSpecifier>
	{

		/// <summary>
		/// Construct a new specifier table
		/// </summary>
		public UhtSpecifierTable() : base(StringViewComparer.OrdinalIgnoreCase)
		{
		}

		/// <summary>
		/// Add the given value to the lookup table.  It will throw an exception if it is a duplicate.
		/// </summary>
		/// <param name="Specifier">The specifier to add</param>
		public UhtSpecifierTable Add(UhtSpecifier Specifier)
		{
			base.Add(Specifier.Name, Specifier);
			return this;
		}
	}

	/// <summary>
	/// Collection of all specifier tables
	/// </summary>
	public class UhtSpecifierTables : UhtLookupTables<UhtSpecifierTable>
	{

		/// <summary>
		/// Construct the specifier table
		/// </summary>
		public UhtSpecifierTables() : base("specifiers")
		{
		}

		/// <summary>
		/// Invoke for a method that has the specifier attribute
		/// </summary>
		/// <param name="Type">Type containing the method</param>
		/// <param name="MethodInfo">Method info</param>
		/// <param name="SpecifierAttribute">Specified attributes</param>
		/// <exception cref="UhtIceException">Throw if the attribute isn't properly defined.</exception>
		public void OnSpecifierAttribute(Type Type, MethodInfo MethodInfo, UhtSpecifierAttribute SpecifierAttribute)
		{
			string Name = UhtLookupTableBase.GetSuffixedName(Type, MethodInfo, SpecifierAttribute.Name, "Specifier");

			if (string.IsNullOrEmpty(SpecifierAttribute.Extends))
			{
				throw new UhtIceException($"The 'Specifier' attribute on the {Type.Name}.{MethodInfo.Name} method doesn't have a table specified.");
			}
			else
			{
			}

			if (SpecifierAttribute.ValueType == UhtSpecifierValueType.NotSet)
			{
				throw new UhtIceException($"The 'Specifier' attribute on the {Type.Name}.{MethodInfo.Name} method doesn't have a value type specified.");
			}

			UhtSpecifierTable Table = Get(SpecifierAttribute.Extends);
			switch (SpecifierAttribute.ValueType)
			{
				case UhtSpecifierValueType.None:
					Table.Add(new UhtSpecifierNone(Name, SpecifierAttribute.When, (UhtSpecifierNoneDelegate)Delegate.CreateDelegate(typeof(UhtSpecifierNoneDelegate), MethodInfo)));
					break;
				case UhtSpecifierValueType.String:
					Table.Add(new UhtSpecifierString(Name, SpecifierAttribute.When, (UhtSpecifierStringDelegate)Delegate.CreateDelegate(typeof(UhtSpecifierStringDelegate), MethodInfo)));
					break;
				case UhtSpecifierValueType.OptionalString:
					Table.Add(new UhtSpecifierOptionalString(Name, SpecifierAttribute.When, (UhtSpecifierOptionalStringDelegate)Delegate.CreateDelegate(typeof(UhtSpecifierOptionalStringDelegate), MethodInfo)));
					break;
				case UhtSpecifierValueType.SingleString:
					Table.Add(new UhtSpecifierSingleString(Name, SpecifierAttribute.When, (UhtSpecifierSingleStringDelegate)Delegate.CreateDelegate(typeof(UhtSpecifierSingleStringDelegate), MethodInfo)));
					break;
				case UhtSpecifierValueType.KeyValuePairList:
					Table.Add(new UhtSpecifierKeyValuePairList(Name, SpecifierAttribute.When, false, (UhtSpecifierKeyValuePairListDelegate)Delegate.CreateDelegate(typeof(UhtSpecifierKeyValuePairListDelegate), MethodInfo)));
					break;
				case UhtSpecifierValueType.OptionalEqualsKeyValuePairList:
					Table.Add(new UhtSpecifierKeyValuePairList(Name, SpecifierAttribute.When, true, (UhtSpecifierKeyValuePairListDelegate)Delegate.CreateDelegate(typeof(UhtSpecifierKeyValuePairListDelegate), MethodInfo)));
					break;
				case UhtSpecifierValueType.StringList:
					Table.Add(new UhtSpecifierStringList(Name, SpecifierAttribute.When, (UhtSpecifierStringListDelegate)Delegate.CreateDelegate(typeof(UhtSpecifierStringListDelegate), MethodInfo)));
					break;
				case UhtSpecifierValueType.NonEmptyStringList:
					Table.Add(new UhtSpecifierNonEmptyStringList(Name, SpecifierAttribute.When, (UhtSpecifierNonEmptyStringListDelegate)Delegate.CreateDelegate(typeof(UhtSpecifierNonEmptyStringListDelegate), MethodInfo)));
					break;
				case UhtSpecifierValueType.Legacy:
					Table.Add(new UhtSpecifierLegacy(Name, (UhtSpecifierLegacyDelegate)Delegate.CreateDelegate(typeof(UhtSpecifierLegacyDelegate), MethodInfo)));
					break;
			}
		}
	}
}
