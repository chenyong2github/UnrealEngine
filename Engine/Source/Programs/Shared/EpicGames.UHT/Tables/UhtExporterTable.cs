// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Utils;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Reflection;

namespace EpicGames.UHT.Tables
{

	/// <summary>
	/// Delegate to invoke to run exporter
	/// </summary>
	/// <param name="Factory">Factory used to generate export tasks and outputs</param>
	public delegate void UhtExporterDelegate(IUhtExportFactory Factory);

	/// <summary>
	/// Export options
	/// </summary>
	[Flags]
	public enum UhtExporterOptions
	{

		/// <summary>
		/// No options
		/// </summary>
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

	/// <summary>
	/// Defines an exporter
	/// </summary>
	[AttributeUsage(AttributeTargets.Method, AllowMultiple = true)]
	public class UhtExporterAttribute : Attribute
	{

		/// <summary>
		/// Name of the exporter
		/// </summary>
		public string Name = string.Empty;

		/// <summary>
		/// Description of the export.  Used to display help
		/// </summary>
		public string Description = string.Empty;

		/// <summary>
		/// Exporter options
		/// </summary>
		public UhtExporterOptions Options = UhtExporterOptions.None;

		/// <summary>
		/// Collection of filters used to delete old cpp files
		/// </summary>
		public string[]? CppFilters = null;

		/// <summary>
		/// Collection of filters used to delete old h files
		/// </summary>
		public string[]? HeaderFilters = null;

		/// <summary>
		/// Collection of filters for other file types
		/// </summary>
		public string[]? OtherFilters = null;
	}

	/// <summary>
	/// Defines an exporter in the table
	/// </summary>
	public struct UhtExporter
	{

		/// <summary>
		/// Name of the exporter
		/// </summary>
		public string Name;

		/// <summary>
		/// Description of the export.  Used to display help
		/// </summary>
		public string Description;

		/// <summary>
		/// Exporter options
		/// </summary>
		public UhtExporterOptions Options;

		/// <summary>
		/// Delegate to invoke to start export
		/// </summary>
		public UhtExporterDelegate Delegate;

		/// <summary>
		/// Collection of filters used to delete old cpp files
		/// </summary>
		public string[]? CppFilters;

		/// <summary>
		/// Collection of filters used to delete old h files
		/// </summary>
		public string[]? HeaderFilters;

		/// <summary>
		/// Collection of filters for other file types
		/// </summary>
		public string[]? OtherFilters;
	}

	/// <summary>
	/// Exporter table
	/// </summary>
	public class UhtExporterTable : IEnumerable<UhtExporter>
	{

		/// <summary>
		/// Global instance of the table
		/// </summary>
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

		/// <summary>
		/// Handle an exporter attribute
		/// </summary>
		/// <param name="Type">Containing type</param>
		/// <param name="MethodInfo">Method info</param>
		/// <param name="ExporterAttribute">Defining attribute</param>
		/// <exception cref="UhtIceException">Thrown if the attribute doesn't properly define an exporter.</exception>
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

		/// <summary>
		/// Return an enumerator for all the defined exporters
		/// </summary>
		/// <returns>Enumerator</returns>
		public IEnumerator<UhtExporter> GetEnumerator()
		{
			foreach (KeyValuePair<string, UhtExporter> KVP in this.ExporterValues)
			{
				yield return KVP.Value;
			}
		}

		/// <summary>
		/// Return an enumerator for all the defined exporters
		/// </summary>
		/// <returns>Enumerator</returns>
		IEnumerator IEnumerable.GetEnumerator()
		{
			foreach (KeyValuePair<string, UhtExporter> KVP in this.ExporterValues)
			{
				yield return KVP.Value;
			}
		}
	}
}
