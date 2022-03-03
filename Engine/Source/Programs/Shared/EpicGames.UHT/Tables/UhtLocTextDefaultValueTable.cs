// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tokenizer;
using System;
using System.Collections.Generic;
using System.Reflection;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System.Text;

namespace EpicGames.UHT.Tables
{
	public delegate bool UhtLocTextDefaultValueDelegate(UhtTextProperty Property, IUhtTokenReader DefaultValueReader, ref UhtToken MacroToken, StringBuilder InnerDefaultValue);

	[AttributeUsage(AttributeTargets.Method, AllowMultiple = true)]
	public class UhtLocTextDefaultValueAttribute : Attribute
	{
		public string? Name;
	}

	public struct UhtLocTextDefaultValue
	{
		public UhtLocTextDefaultValueDelegate Delegate;
	}

	public class UhtLocTextDefaultValueTable
	{
		public static UhtLocTextDefaultValueTable Instance = new UhtLocTextDefaultValueTable();

		private Dictionary<StringView, UhtLocTextDefaultValue> LocTextDefaultValues = new Dictionary<StringView, UhtLocTextDefaultValue>();

		/// <summary>
		/// Return the loc text default value associated with the given name
		/// </summary>
		/// <param name="Name"></param>
		/// <param name="LocTextDefaultValue">Loc text default value handler</param>
		/// <returns></returns>
		public bool TryGet(StringView Name, out UhtLocTextDefaultValue LocTextDefaultValue)
		{
			return this.LocTextDefaultValues.TryGetValue(Name, out LocTextDefaultValue);
		}

		public void OnLocTextDefaultValueAttribute(Type Type, MethodInfo MethodInfo, UhtLocTextDefaultValueAttribute LocTextDefaultValueAttribute)
		{
			if (string.IsNullOrEmpty(LocTextDefaultValueAttribute.Name))
			{
				throw new UhtIceException("A loc text default value attribute must have a name");
			}

			UhtLocTextDefaultValue LocTextDefaultValue = new UhtLocTextDefaultValue
			{
				Delegate = (UhtLocTextDefaultValueDelegate)Delegate.CreateDelegate(typeof(UhtLocTextDefaultValueDelegate), MethodInfo)
			};

			this.LocTextDefaultValues.Add(new StringView(LocTextDefaultValueAttribute.Name), LocTextDefaultValue);
		}
	}
}
