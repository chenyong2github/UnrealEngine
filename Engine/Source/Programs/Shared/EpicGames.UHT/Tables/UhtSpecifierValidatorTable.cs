// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System;
using System.Reflection;

namespace EpicGames.UHT.Tables
{
	public delegate void UhtSpecifierValidatorDelegate(UhtType Type, UhtMetaData MetaData, UhtMetaDataKey Key, StringView Value);

	public class UhtSpecifierValidator
	{
		public string Name;
		public UhtSpecifierValidatorDelegate Delegate;

		public UhtSpecifierValidator(string Name, UhtSpecifierValidatorDelegate Delegate)
		{
			this.Name = Name;
			this.Delegate = Delegate;
		}
	}

	[AttributeUsage(AttributeTargets.Method, AllowMultiple = true)]
	public class UhtSpecifierValidatorAttribute : Attribute
	{
		public string? Name;
		public string? Extends;
	}

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
		/// <param name="Value">Value to be added</param>
		public UhtSpecifierValidatorTable Add(UhtSpecifierValidator Specifier)
		{
			base.Add(Specifier.Name, Specifier);
			return this;
		}
	}

	public class UhtSpecifierValidatorTables : UhtLookupTables<UhtSpecifierValidatorTable>
	{
		public static UhtSpecifierValidatorTables Instance = new UhtSpecifierValidatorTables();

		public UhtSpecifierValidatorTables() : base("specifier validators")
		{
		}

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
