// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Utils;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Reflection;

namespace EpicGames.UHT.Tables
{
	public delegate void UhtExporterDelegate(IUhtExportFactory Factory);

	[Flags]
	public enum UhtExporterOptions
	{
		None = 0,

		/// <summary>
		/// The exporter should be run by default
		/// </summary>
		Default = 1 << 0,
	}

	/// <summary>
	/// Helper methods for testing flags.  These methods perform better than the generic HasFlag which hits
	/// the GC and stalls.
	/// </summary>
	public static class UhtExporterOptionsExtensions
	{

		/// <summary>
		/// Test to see if any of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if any of the flags are set</returns>
		public static bool HasAnyFlags(this UhtExporterOptions InFlags, UhtExporterOptions TestFlags)
		{
			return (InFlags & TestFlags) != 0;
		}

		/// <summary>
		/// Test to see if all of the specified flags are set
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <returns>True if all the flags are set</returns>
		public static bool HasAllFlags(this UhtExporterOptions InFlags, UhtExporterOptions TestFlags)
		{
			return (InFlags & TestFlags) == TestFlags;
		}

		/// <summary>
		/// Test to see if a specific set of flags have a specific value.
		/// </summary>
		/// <param name="InFlags">Current flags</param>
		/// <param name="TestFlags">Flags to test for</param>
		/// <param name="MatchFlags">Expected value of the tested flags</param>
		/// <returns>True if the given flags have a specific value.</returns>
		public static bool HasExactFlags(this UhtExporterOptions InFlags, UhtExporterOptions TestFlags, UhtExporterOptions MatchFlags)
		{
			return (InFlags & TestFlags) == MatchFlags;
		}
	}

	[AttributeUsage(AttributeTargets.Method, AllowMultiple = true)]
	public class UhtExporterAttribute : Attribute
	{
		public string Name = string.Empty;
		public string Description = string.Empty;
		public UhtExporterOptions Options = UhtExporterOptions.None;
		public string[]? CppFilters = null;
		public string[]? HeaderFilters = null;
		public string[]? OtherFilters = null;
	}

	public struct UhtExporter
	{
		public string Name;
		public string Description;
		public UhtExporterOptions Options;
		public UhtExporterDelegate Delegate;
		public string[]? CppFilters;
		public string[]? HeaderFilters;
		public string[]? OtherFilters;
	}

	public class UhtExporterTable : IEnumerable<UhtExporter>
	{
		public static UhtExporterTable Instance = new UhtExporterTable();

		private Dictionary<string, UhtExporter> ExporterValues = new Dictionary<string, UhtExporter>(StringComparer.OrdinalIgnoreCase);

		/// <summary>
		/// Return the exporter associated with the given name
		/// </summary>
		/// <param name="Name"></param>
		/// <param name="Value">Exporter associated with the name</param>
		/// <returns></returns>
		public bool TryGet(string Name, out UhtExporter Value)
		{
			return this.ExporterValues.TryGetValue(Name, out Value);
		}

		public void OnExporterAttribute(Type Type, MethodInfo MethodInfo, UhtExporterAttribute ExporterAttribute)
		{
			if (string.IsNullOrEmpty(ExporterAttribute.Name))
			{
				throw new UhtIceException("An exporter must have a name");
			}

			UhtExporter ExporterValue = new UhtExporter
			{
				Name = ExporterAttribute.Name,
				Description = ExporterAttribute.Description,
				Delegate = (UhtExporterDelegate)Delegate.CreateDelegate(typeof(UhtExporterDelegate), MethodInfo),
				Options = ExporterAttribute.Options,
				CppFilters = ExporterAttribute.CppFilters,
				HeaderFilters = ExporterAttribute.HeaderFilters,
				OtherFilters = ExporterAttribute.OtherFilters,
			};

			this.ExporterValues.Add(ExporterAttribute.Name, ExporterValue);
		}

		public IEnumerator<UhtExporter> GetEnumerator()
		{
			foreach (KeyValuePair<string, UhtExporter> KVP in this.ExporterValues)
			{
				yield return KVP.Value;
			}
		}

		IEnumerator IEnumerable.GetEnumerator()
		{
			foreach (KeyValuePair<string, UhtExporter> KVP in this.ExporterValues)
			{
				yield return KVP.Value;
			}
		}
	}
}
