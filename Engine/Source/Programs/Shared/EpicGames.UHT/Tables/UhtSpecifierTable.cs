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

	public enum UhtSpecifierDispatchResults
	{
		Known,
		Unknown,
	}

	/// <summary>
	/// The specifier context provides the default and simplest information about the specifiers being processed
	/// </summary>
	public class UhtSpecifierContext
	{
		public UhtParsingScope Scope;
		public IUhtMessageSite MessageSite;
		public UhtMetaData MetaData;
		public int MetaNameIndex;

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

	public abstract class UhtSpecifier
	{
		public string Name = String.Empty;
		public UhtSpecifierValueType ValueType;
		public UhtSpecifierWhen When = UhtSpecifierWhen.Deferred;

		public abstract UhtSpecifierDispatchResults Dispatch(UhtSpecifierContext SpecifierContext, object? Value);
	}

	public delegate void UhtSpecifierNoneDelegate(UhtSpecifierContext SpecifierContext);

	public class UhtSpecifierNone : UhtSpecifier
	{
		private UhtSpecifierNoneDelegate Delegate;

		public UhtSpecifierNone(string Name, UhtSpecifierWhen When, UhtSpecifierNoneDelegate Delegate)
		{
			this.Name = Name;
			this.ValueType = UhtSpecifierValueType.None;
			this.When = When;
			this.Delegate = Delegate;
		}

		public override UhtSpecifierDispatchResults Dispatch(UhtSpecifierContext SpecifierContext, object? Value)
		{
			this.Delegate(SpecifierContext);
			return UhtSpecifierDispatchResults.Known;
		}
	}

	public delegate void UhtSpecifierStringDelegate(UhtSpecifierContext SpecifierContext, StringView Value);

	public class UhtSpecifierString : UhtSpecifier
	{
		private UhtSpecifierStringDelegate Delegate;

		public UhtSpecifierString(string Name, UhtSpecifierWhen When, UhtSpecifierStringDelegate Delegate)
		{
			this.Name = Name;
			this.ValueType = UhtSpecifierValueType.String;
			this.When = When;
			this.Delegate = Delegate;
		}

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

	public delegate void UhtSpecifierOptionalStringDelegate(UhtSpecifierContext SpecifierContext, StringView? Value);

	public class UhtSpecifierOptionalString : UhtSpecifier
	{
		private UhtSpecifierOptionalStringDelegate Delegate;

		public UhtSpecifierOptionalString(string Name, UhtSpecifierWhen When, UhtSpecifierOptionalStringDelegate Delegate)
		{
			this.Name = Name;
			this.ValueType = UhtSpecifierValueType.OptionalString;
			this.When = When;
			this.Delegate = Delegate;
		}

		public override UhtSpecifierDispatchResults Dispatch(UhtSpecifierContext SpecifierContext, object? Value)
		{
			this.Delegate(SpecifierContext, (StringView?)Value);
			return UhtSpecifierDispatchResults.Known;
		}
	}

	public delegate void UhtSpecifierSingleStringDelegate(UhtSpecifierContext SpecifierContext, StringView Value);

	public class UhtSpecifierSingleString : UhtSpecifier
	{
		private UhtSpecifierSingleStringDelegate Delegate;

		public UhtSpecifierSingleString(string Name, UhtSpecifierWhen When, UhtSpecifierSingleStringDelegate Delegate)
		{
			this.Name = Name;
			this.ValueType = UhtSpecifierValueType.SingleString;
			this.When = When;
			this.Delegate = Delegate;
		}

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

	public delegate void UhtSpecifierKeyValuePairListDelegate(UhtSpecifierContext SpecifierContext, List<KeyValuePair<StringView, StringView>> Value);

	public class UhtSpecifierKeyValuePairList : UhtSpecifier
	{
		private UhtSpecifierKeyValuePairListDelegate Delegate;

		public UhtSpecifierKeyValuePairList(string Name, UhtSpecifierWhen When, bool bEqualsOptional, UhtSpecifierKeyValuePairListDelegate Delegate)
		{
			this.Name = Name;
			this.ValueType = bEqualsOptional ? UhtSpecifierValueType.OptionalEqualsKeyValuePairList : UhtSpecifierValueType.KeyValuePairList;
			this.When = When;
			this.Delegate = Delegate;
		}

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

	public delegate void UhtSpecifierLegacyDelegate(UhtSpecifierContext SpecifierContext);

	public class UhtSpecifierLegacy : UhtSpecifier
	{
		private UhtSpecifierLegacyDelegate Delegate;

		public UhtSpecifierLegacy(string Name, UhtSpecifierLegacyDelegate Delegate)
		{
			this.Name = Name;
			this.ValueType = UhtSpecifierValueType.StringList;
			this.When = UhtSpecifierWhen.Deferred;
			this.Delegate = Delegate;
		}

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

	public delegate void UhtSpecifierStringListDelegate(UhtSpecifierContext SpecifierContext, List<StringView>? Value);

	public class UhtSpecifierStringList : UhtSpecifier
	{
		private UhtSpecifierStringListDelegate Delegate;

		public UhtSpecifierStringList(string Name, UhtSpecifierWhen When, UhtSpecifierStringListDelegate Delegate)
		{
			this.Name = Name;
			this.ValueType = UhtSpecifierValueType.StringList;
			this.When = When;
			this.Delegate = Delegate;
		}

		public override UhtSpecifierDispatchResults Dispatch(UhtSpecifierContext SpecifierContext, object? Value)
		{
			this.Delegate(SpecifierContext, (List<StringView>?)Value);
			return UhtSpecifierDispatchResults.Known;
		}
	}

	public delegate void UhtSpecifierNonEmptyStringListDelegate(UhtSpecifierContext SpecifierContext, List<StringView> Value);

	public class UhtSpecifierNonEmptyStringList : UhtSpecifier
	{
		private UhtSpecifierNonEmptyStringListDelegate Delegate;

		public UhtSpecifierNonEmptyStringList(string Name, UhtSpecifierWhen When, UhtSpecifierNonEmptyStringListDelegate Delegate)
		{
			this.Name = Name;
			this.ValueType = UhtSpecifierValueType.NonEmptyStringList;
			this.When = When;
			this.Delegate = Delegate;
		}

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

	[AttributeUsage(AttributeTargets.Method, AllowMultiple = true)]
	public class UhtSpecifierAttribute : Attribute
	{
		public string? Name;
		public string? Extends;
		public UhtSpecifierValueType ValueType = UhtSpecifierValueType.NotSet;
		public UhtSpecifierWhen When = UhtSpecifierWhen.Deferred;
	}

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
		/// <param name="Value">Value to be added</param>
		public UhtSpecifierTable Add(UhtSpecifier Specifier)
		{
			base.Add(Specifier.Name, Specifier);
			return this;
		}
	}

	public class UhtSpecifierTables : UhtLookupTables<UhtSpecifierTable>
	{
		public static UhtSpecifierTables Instance = new UhtSpecifierTables();

		public UhtSpecifierTables() : base("specifiers")
		{
		}

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
