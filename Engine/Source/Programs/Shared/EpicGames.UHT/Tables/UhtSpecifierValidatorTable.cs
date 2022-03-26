// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System;
using System.Reflection;

namespace EpicGames.UHT.Tables
{

	/// <summary>
	/// Delegate used to validate a specifier
	/// </summary>
	/// <param name="Type">Containing type</param>
	/// <param name="MetaData">Containing meta data</param>
	/// <param name="Key">Key of the meta data entry</param>
	/// <param name="Value">Value of the meta data entry</param>
	public delegate void UhtSpecifierValidatorDelegate(UhtType Type, UhtMetaData MetaData, UhtMetaDataKey Key, StringView Value);

	/// <summary>
	/// Defines a specifier validated created from the attribute
	/// </summary>
	public class UhtSpecifierValidator
	{

		/// <summary>
		/// Name of the validator
		/// </summary>
		public string Name;

		/// <summary>
		/// Delegate for the validator
		/// </summary>
		public UhtSpecifierValidatorDelegate Delegate;

		/// <summary>
		/// Construct a new instance
		/// </summary>
		/// <param name="Name">Name of the validator</param>
		/// <param name="Delegate">Delegate of the validator</param>
		public UhtSpecifierValidator(string Name, UhtSpecifierValidatorDelegate Delegate)
		{
			this.Name = Name;
			this.Delegate = Delegate;
		}
	}

	/// <summary>
	/// Attribute used to create a specifier validator
	/// </summary>
	[AttributeUsage(AttributeTargets.Method, AllowMultiple = true)]
	public class UhtSpecifierValidatorAttribute : Attribute
	{

		/// <summary>
		/// Name of the validator. If not supplied &quot;SpecifierValidator&quot; will be removed from the end of the method name
		/// </summary>
		public string? Name;

		/// <summary>
		/// Name of the table/scope for the validator
		/// </summary>
		public string? Extends;
	}

	/// <summary>
	/// A table for validators for a given scope
	/// </summary>
	public class UhtSpecifierValidatorTable : UhtLookupTable<UhtSpecifierValidator>
	{

		/// <summary>
		/// Construct a new specifier table
		/// </summary>
		public UhtSpecifierValidatorTable() : base(StringViewComparer.OrdinalIgnoreCase)
		{
		}

		/// <summary>
		/// Add the given value to the lookup table.  It will throw an exception if it is a duplicate.
		/// </summary>
		/// <param name="Specifier">Validator to add</param>
		public UhtSpecifierValidatorTable Add(UhtSpecifierValidator Specifier)
		{
			base.Add(Specifier.Name, Specifier);
			return this;
		}
	}

	/// <summary>
	/// Collection of specifier validators
	/// </summary>
	public class UhtSpecifierValidatorTables : UhtLookupTables<UhtSpecifierValidatorTable>
	{

		/// <summary>
		/// Construct the validator tables
		/// </summary>
		public UhtSpecifierValidatorTables() : base("specifier validators")
		{
		}

		/// <summary>
		/// Handle the attribute appearing on a method
		/// </summary>
		/// <param name="Type">Type containing the method</param>
		/// <param name="MethodInfo">The method</param>
		/// <param name="SpecifierValidatorAttribute">Attribute</param>
		/// <exception cref="UhtIceException">Thrown if the validator isn't properly defined</exception>
		public void OnSpecifierValidatorAttribute(Type Type, MethodInfo MethodInfo, UhtSpecifierValidatorAttribute SpecifierValidatorAttribute)
		{
			string Name = UhtLookupTableBase.GetSuffixedName(Type, MethodInfo, SpecifierValidatorAttribute.Name, "SpecifierValidator");

			if (string.IsNullOrEmpty(SpecifierValidatorAttribute.Extends))
			{
				throw new UhtIceException($"The 'SpecifierValidator' attribute on the {Type.Name}.{MethodInfo.Name} method doesn't have a table specified.");
			}
			UhtSpecifierValidatorTable Table = Get(SpecifierValidatorAttribute.Extends);
			Table.Add(new UhtSpecifierValidator(Name, (UhtSpecifierValidatorDelegate)Delegate.CreateDelegate(typeof(UhtSpecifierValidatorDelegate), MethodInfo)));
		}
	}
}
