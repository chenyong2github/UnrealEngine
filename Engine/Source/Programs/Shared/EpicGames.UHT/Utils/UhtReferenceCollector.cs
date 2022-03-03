// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Types;
using System;
using System.Collections.Generic;

namespace EpicGames.UHT.Utils
{
	/// <summary>
	/// Interface used to collect all the objects referenced by a given type.
	/// Not all types such as UhtPackage and UhtHeaderFile support collecting
	/// references due to assorted reasons.
	/// </summary>
	public interface IUhtReferenceCollector
	{

		/// <summary>
		/// Add a cross module reference to a given object type.
		/// </summary>
		/// <param name="Object">Object type being referenced</param>
		/// <param name="bRegistered">If true, the method being invoked must return the registered object.  This only applies to classes.</param>
		void AddCrossModuleReference(UhtObject? Object, bool bRegistered);

		/// <summary>
		/// Add an object declaration
		/// </summary>
		/// <param name="Object">Object in question</param>
		/// <param name="bRegistered">If true, the method being invoked must return the registered object.  This only applies to classes.</param>
		void AddDeclaration(UhtObject Object, bool bRegistered);

		/// <summary>
		/// Add a field as a singleton for exporting
		/// </summary>
		/// <param name="Field">Field to be added</param>
		void AddSingleton(UhtField Field);

		/// <summary>
		/// Add a field as a type being exported
		/// </summary>
		/// <param name="Field">Field to be added</param>
		void AddExportType(UhtField Field);

		/// <summary>
		/// Add a forward declaration.  The string can contain multiple declarations but must only exist on one line.
		/// </summary>
		/// <param name="Declaration">The declarations to add</param>
		void AddForwardDeclaration(string? Declaration);
	}

	/// <summary>
	/// Delegate used to fetch the string associated with a reference
	/// </summary>
	/// <param name="ObjectIndex">Index of the referenced object</param>
	/// <param name="bRegistered">If true return the registered string, otherwise the unregistered string.  Classes have an unregistered version.</param>
	/// <returns>The requested string</returns>
	public delegate string GetReferenceStringDelegate(int ObjectIndex, bool bRegistered);

	/// <summary>
	/// Maintains a list of referenced object indices.
	/// </summary>
	public class UhtUniqueReferenceCollection
	{
		/// <summary>
		/// Collection use to quickly detect if a reference is already in the collection
		/// </summary>
		private HashSet<int> Uniques = new HashSet<int>();

		/// <summary>
		/// List of all unique reference keys.  Use UngetKey to get the object index and the flag.
		/// </summary>
		public List<int> References = new List<int>();

		/// <summary>
		/// Return an encoded key that represents the object and registered flag. 
		/// If the object has the alternate object set (i.e. native interfaces), then
		/// that object's index is used to generate the key.
		/// </summary>
		/// <param name="Object">Object being referenced</param>
		/// <param name="bRegistered">If true, then the API that ensures the object is registered is returned.</param>
		/// <returns>Integer key value.</returns>
		public static int GetKey(UhtObject Object, bool bRegistered)
		{
			return Object.AlternateObject != null ? GetKey(Object.AlternateObject, bRegistered) : (Object.ObjectTypeIndex << 1) + (bRegistered ? 1 : 0);
		}

		/// <summary>
		/// Given a key, return the object index and registered flag
		/// </summary>
		/// <param name="Key">The key in question</param>
		/// <param name="ObjectIndex">Index of the referenced object</param>
		/// <param name="bRegistered">True if referencing the registered API.</param>
		public static void UngetKey(int Key, out int ObjectIndex, out bool bRegistered)
		{
			ObjectIndex = Key >> 1;
			bRegistered = (Key & 1) != 0;
		}

		/// <summary>
		/// Add the given object to the references
		/// </summary>
		/// <param name="Object">Object to be added</param>
		/// <param name="bRegistered">True if the registered API is being returned.</param>
		public void Add(UhtObject? Object, bool bRegistered)
		{
			if (Object != null)
			{
				int Key = GetKey(Object, bRegistered);
				if (this.Uniques.Add(Key))
				{
					this.References.Add(Key);
				}
			}
		}

		/// <summary>
		/// Return the collection of references sorted by the API string returned by the delegate.
		/// </summary>
		/// <param name="Delegate">Delegate to invoke to return the requested object API string</param>
		/// <returns>Read only memory region of all the string.</returns>
		public ReadOnlyMemory<string> GetSortedReferences(GetReferenceStringDelegate Delegate)
		{
			// Collect the unsorted array
			string[] Sorted = new string[this.References.Count];
			for (int Index = 0; Index < this.References.Count; ++Index)
			{
				int Key = this.References[Index];
				int ObjectIndex;
				bool bRegistered;
				UngetKey(Key, out ObjectIndex, out bRegistered);
				Sorted[Index] = Delegate(ObjectIndex, bRegistered);
			}

			// Sort the array
			Array.Sort(Sorted, StringComparerUE.OrdinalIgnoreCase);

			// Remove duplicates.  In some instances the different keys might return the same string.
			// This removes those duplicates
			if (this.References.Count > 1)
			{
				int PriorOut = 0;
				for (int Index = 1; Index < Sorted.Length; ++Index)
				{
					if (Sorted[Index] != Sorted[PriorOut])
					{
						++PriorOut;
						Sorted[PriorOut] = Sorted[Index];
					}
				}
				return Sorted.AsMemory(0, PriorOut + 1);
			}
			else
			{
				return Sorted.AsMemory();
			}
		}
	}

	/// <summary>
	/// Standard implementation of the reference collector interface
	/// </summary>
	public class UhtReferenceCollector : IUhtReferenceCollector
	{

		/// <summary>
		/// Collection of unique cross module references
		/// </summary>
		public UhtUniqueReferenceCollection CrossModule = new UhtUniqueReferenceCollection();

		/// <summary>
		/// Collection of unique declarations
		/// </summary>
		public UhtUniqueReferenceCollection Declaration = new UhtUniqueReferenceCollection();

		/// <summary>
		/// Collection of singletons
		/// </summary>
		public List<UhtField> Singletons = new List<UhtField>();

		/// <summary>
		/// Collection of types to export
		/// </summary>
		public List<UhtField> ExportTypes = new List<UhtField>();

		/// <summary>
		/// Collection of forward declarations
		/// </summary>
		public HashSet<string> ForwardDeclarations = new HashSet<string>();

		/// <summary>
		/// Collection of referenced headers
		/// </summary>
		public HashSet<UhtHeaderFile> ReferencedHeaders = new HashSet<UhtHeaderFile>();

		/// <summary>
		/// Add a cross module reference
		/// </summary>
		/// <param name="Object">Object being referenced</param>
		/// <param name="bRegistered">True if the object being referenced must be registered</param>
		public void AddCrossModuleReference(UhtObject? Object, bool bRegistered)
		{
			CrossModule.Add(Object, bRegistered);
			if (Object != null && !(Object is UhtPackage) && bRegistered)
			{
				this.ReferencedHeaders.Add(Object.HeaderFile);
			}
		}

		/// <summary>
		/// Add a declaration
		/// </summary>
		/// <param name="Object">Object being declared</param>
		/// <param name="bRegistered">True if the object being declared must be registered</param>
		public void AddDeclaration(UhtObject Object, bool bRegistered)
		{
			this.Declaration.Add(Object, bRegistered);
		}

		/// <summary>
		/// Add a singleton.  These are added as forward declared functions in the package file.
		/// </summary>
		/// <param name="Field">Field being added</param>
		public void AddSingleton(UhtField Field)
		{
			this.Singletons.Add(Field);
		}

		/// <summary>
		/// Add a type to be exported.
		/// </summary>
		/// <param name="Field">Type to be exported</param>
		public void AddExportType(UhtField Field)
		{
			this.ExportTypes.Add(Field);
		}

		/// <summary>
		/// Add a symbol that must be forward declared
		/// </summary>
		/// <param name="Declaration">Symbol to be forward declared.</param>
		public void AddForwardDeclaration(string? Declaration)
		{
			if (!string.IsNullOrEmpty(Declaration))
			{
				this.ForwardDeclarations.Add(Declaration);
			}
		}
	}
}
