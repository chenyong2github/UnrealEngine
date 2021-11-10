// Copyright Epic Games, Inc. All Rights Reserved.

using Microsoft.AspNetCore.Mvc;
using Microsoft.AspNetCore.Mvc.ModelBinding;
using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;

namespace HordeServer.Utilities
{
	/// <summary>
	/// Utility class to filter a response to include a subset of fields
	/// </summary>
	[TypeConverter(typeof(PropertyFilterTypeConverter))]
	public class PropertyFilter
	{
		/// <summary>
		/// Specific filters for properties of this object. A key of '*' will match any property not matched by anything else, and a value of null will include the whole object.
		/// </summary>
		Dictionary<string, PropertyFilter?> NameToFilter = new Dictionary<string, PropertyFilter?>(StringComparer.OrdinalIgnoreCase);

		/// <summary>
		/// Parse a list of fields as a property filter
		/// </summary>
		/// <param name="Fields">List of fields to parse, separated by commas</param>
		/// <returns>New property filter</returns>
		public static PropertyFilter Parse(string? Fields)
		{
			PropertyFilter RootFilter = new PropertyFilter();
			if(Fields != null)
			{
				foreach (string Field in Fields.Split(','))
				{
					// Split the field name into segments
					string[] Segments = Field.Split('.');

					// Create the tree of segments
					Dictionary<string, PropertyFilter?> NameToFilter = RootFilter.NameToFilter;
					for (int Idx = 0; Idx < Segments.Length - 1; Idx++)
					{
						PropertyFilter? NextFilter;
						if (!NameToFilter.TryGetValue(Segments[Idx], out NextFilter))
						{
							NextFilter = new PropertyFilter();
							NameToFilter[Segments[Idx]] = NextFilter;
						}
						else if (NextFilter == null)
						{
							break;
						}
						NameToFilter = NextFilter.NameToFilter;
					}
					NameToFilter[Segments[Segments.Length - 1]] = null;
				}
			}
			return RootFilter;
		}

		/// <summary>
		/// Checks whether the filter contains the given field
		/// </summary>
		/// <param name="Filter">The filter to check</param>
		/// <param name="Field">Name of the field to check for</param>
		/// <returns>True if the filter includes the given field</returns>
		public static bool Includes(PropertyFilter? Filter, string Field)
		{
			return Filter == null || Filter.Includes(Field);
		}

		/// <summary>
		/// Attempts to apply a property filter to a response object, where the property filter may be null.
		/// </summary>
		/// <param name="Response">Response object</param>
		/// <param name="Filter">The filter to apply</param>
		/// <returns>Filtered object</returns>
		public static object Apply(object Response, PropertyFilter? Filter)
		{
			return (Filter == null) ? Response : Filter.ApplyTo(Response);
		}

		/// <summary>
		/// Checks whether the filter contains the given field
		/// </summary>
		/// <param name="Field">Name of the field to check for</param>
		/// <returns>True if the filter includes the given field</returns>
		public bool Includes(string Field)
		{
			return NameToFilter.Count == 0 || NameToFilter.ContainsKey(Field) || NameToFilter.ContainsKey("*");
		}

		/// <summary>
		/// Extract the given fields from the response
		/// </summary>
		/// <param name="Response">The response to filter</param>
		/// <returns>Filtered response</returns>
		public object ApplyTo(object Response)
		{
			// If there are no filters at this depth, return the whole object
			if (NameToFilter.Count == 0)
			{
				return Response;
			}

			// Check if this object is a list. If it is, we'll apply the filter to each element in the list.
			IList? List = Response as IList;
			if (List != null)
			{
				List<object?> NewResponse = new List<object?>();
				for (int Index = 0; Index < List.Count; Index++)
				{
					object? Value = List[Index];
					if (Value == null)
					{
						NewResponse.Add(Value);
					}
					else
					{
						NewResponse.Add(ApplyTo(Value));
					}
				}
				return NewResponse;
			}
			else
			{
				Dictionary<string, object?> NewResponse = new Dictionary<string, object?>(StringComparer.Ordinal);

				// Check if this object is a dictionary.
				IDictionary? Dictionary = Response as IDictionary;
				if (Dictionary != null)
				{
					foreach (DictionaryEntry? Entry in Dictionary)
					{
						if (Entry.HasValue)
						{
							string? Key = Entry.Value.Key.ToString();
							if (Key != null)
							{
								Key = ConvertPascalToCamelCase(Key);

								PropertyFilter? Filter;
								if (NameToFilter.TryGetValue(Key, out Filter))
								{
									object? Value = Entry.Value.Value;
									if (Filter == null)
									{
										NewResponse[Key] = Value;
									}
									else if (Value != null)
									{
										NewResponse[Key] = Filter.ApplyTo(Value);
									}
								}
							}
						}
					}
				}
				else
				{
					// Otherwise try to get the properties of this object
					foreach (PropertyInfo Property in Response.GetType().GetProperties(BindingFlags.Public | BindingFlags.GetProperty | BindingFlags.Instance))
					{
						PropertyFilter? Filter;
						if (NameToFilter.TryGetValue(Property.Name, out Filter))
						{
							string Key = ConvertPascalToCamelCase(Property.Name);
							object? Value = Property.GetValue(Response);
							if (Filter == null)
							{
								NewResponse[Key] = Value;
							}
							else if (Value != null)
							{
								NewResponse[Key] = Filter.ApplyTo(Value);
							}
						}
					}
				}
				return NewResponse;
			}
		}

		/// <summary>
		/// Convert a property name to camel case
		/// </summary>
		/// <param name="Name">The name to convert to camelCase</param>
		/// <returns>Camelcase version of the name</returns>
		static string ConvertPascalToCamelCase(string Name)
		{
			if (Name[0] >= 'A' && Name[0] <= 'Z')
			{
				return (char)(Name[0] - 'A' + 'a') + Name.Substring(1);
			}
			else
			{
				return Name;
			}
		}
	}

	/// <summary>
	/// Type converter from strings to PropertyFilter objects
	/// </summary>
	public class PropertyFilterTypeConverter : TypeConverter
	{
		/// <inheritdoc/>
		public override bool CanConvertFrom(ITypeDescriptorContext? Context, Type SourceType)
		{
			return SourceType == typeof(string);
		}

		/// <inheritdoc/>
		public override object ConvertFrom(ITypeDescriptorContext? Context, CultureInfo? Culture, object Value)
		{
			return PropertyFilter.Parse((string)Value);
		}
	}

	/// <summary>
	/// Extension methods for property filters
	/// </summary>
	public static class PropertyFilterExtensions
	{
		/// <summary>
		/// Apply a filter to a response object
		/// </summary>
		/// <param name="Obj">The object to filter</param>
		/// <param name="Filter">The filter to apply</param>
		/// <returns>The filtered object</returns>
		public static object ApplyFilter(this object Obj, PropertyFilter? Filter)
		{
			if (Filter == null)
			{
				return Obj;
			}
			else
			{
				return Filter.ApplyTo(Obj);
			}
		}
	}
}
