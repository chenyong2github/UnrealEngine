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
	public sealed class UhtExporterAttribute : Attribute
	{

		/// <summary>
		/// Name of the exporter
		/// </summary>
		public string Name { get; set; } = string.Empty;

		/// <summary>
		/// Description of the export.  Used to display help
		/// </summary>
		public string Description { get; set; } = string.Empty;

		/// <summary>
		/// Exporters in plugins need to specify a module name
		/// </summary>
		public string ModuleName { get; set; } = string.Empty;

		/// <summary>
		/// Exporter options
		/// </summary>
		public UhtExporterOptions Options { get; set; } = UhtExporterOptions.None;

		/// <summary>
		/// Collection of filters used to delete old cpp files
		/// </summary>
		public string[]? CppFilters { get; set; }

		/// <summary>
		/// Collection of filters used to delete old h files
		/// </summary>
		public string[]? HeaderFilters { get; set; }

		/// <summary>
		/// Collection of filters for other file types
		/// </summary>
		public string[]? OtherFilters { get; set; }
	}

	/// <summary>
	/// Defines an exporter in the table
	/// </summary>
	public struct UhtExporter
	{

		/// <summary>
		/// Name of the exporter
		/// </summary>
		public string Name { get; }

		/// <summary>
		/// Description of the export.  Used to display help
		/// </summary>
		public string Description { get; }

		/// <summary>
		/// Exporters in plugins need to specify a module name
		/// </summary>
		public string ModuleName { get; }

		/// <summary>
		/// Exporter options
		/// </summary>
		public UhtExporterOptions Options { get; }

		/// <summary>
		/// Delegate to invoke to start export
		/// </summary>
		public UhtExporterDelegate Delegate { get; }

		/// <summary>
		/// Collection of filters used to delete old cpp files
		/// </summary>
		public IReadOnlyList<string> CppFilters { get; }

		/// <summary>
		/// Collection of filters used to delete old h files
		/// </summary>
		public IReadOnlyList<string> HeaderFilters { get; }

		/// <summary>
		/// Collection of filters for other file types
		/// </summary>
		public IReadOnlyList<string> OtherFilters { get; }

		/// <summary>
		/// Construct an exporter table instance
		/// </summary>
		/// <param name="Attribute">Source attribute</param>
		/// <param name="Delegate">Delegate to invoke</param>
		public UhtExporter(UhtExporterAttribute Attribute, UhtExporterDelegate Delegate)
		{
			this.Name = Attribute.Name;
			this.Description = Attribute.Description;
			this.ModuleName = Attribute.ModuleName;
			this.Options = Attribute.Options;
			this.Delegate = Delegate;
			this.CppFilters = Attribute.CppFilters != null ? new List<string>(Attribute.CppFilters) : new List<string>();
			this.HeaderFilters = Attribute.HeaderFilters != null ? new List<string>(Attribute.HeaderFilters) : new List<string>();
			this.OtherFilters = Attribute.OtherFilters != null ? new List<string>(Attribute.OtherFilters) : new List<string>();
		}
	}

	/// <summary>
	/// Exporter table
	/// </summary>
	public class UhtExporterTable : IEnumerable<UhtExporter>
	{

		private readonly Dictionary<string, UhtExporter> ExporterValues = new Dictionary<string, UhtExporter>(StringComparer.OrdinalIgnoreCase);

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

			if (Assembly.GetExecutingAssembly() != Type.Assembly)
			{
				if (string.IsNullOrEmpty(ExporterAttribute.ModuleName))
				{
					throw new UhtIceException("An exporter in a UBT plugin must specify a ModuleName");
				}
			}

			UhtExporter ExporterValue = new UhtExporter(ExporterAttribute, (UhtExporterDelegate)Delegate.CreateDelegate(typeof(UhtExporterDelegate), MethodInfo));
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
