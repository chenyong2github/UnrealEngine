// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;
using UnrealBuildTool;

namespace UnrealBuildTool
{
	/// <summary>
	/// Stores parsed values from XML config files which can be applied to a configurable type. Can be serialized to disk in binary form as a cache.
	/// </summary>
	class XmlConfigData
	{
		/// <summary>
		/// The current cache serialization version
		/// </summary>
		const int SerializationVersion = 2;

		/// <summary>
		/// List of input files. Stored to allow checking cache validity.
		/// </summary>
		public FileReference[] InputFiles;

		public class ValueInfo
		{
			public FieldInfo FieldInfo;
			public object Value;
			public FileReference SourceFile;
			public XmlConfigFileAttribute XmlConfigAttribute;

			public ValueInfo(FieldInfo FieldInfo, object Value, FileReference SourceFile, XmlConfigFileAttribute XmlConfigAttribute)
			{
				this.FieldInfo = FieldInfo;
				this.Value = Value;
				this.SourceFile = SourceFile;
				this.XmlConfigAttribute = XmlConfigAttribute;
			}
		}
		
		/// <summary>
		/// Stores a mapping from type -> field -> value, with all the config values for configurable fields.
		/// </summary>
		public Dictionary<Type, ValueInfo[]> TypeToValues;

		/// <summary>
		/// Constructor
		/// </summary>
		/// <param name="InputFiles"></param>
		/// <param name="TypeToValues"></param>
		public XmlConfigData(FileReference[] InputFiles, Dictionary<Type, ValueInfo[]> TypeToValues)
		{
			this.InputFiles = InputFiles;
			this.TypeToValues = TypeToValues;
		}

		/// <summary>
		/// Attempts to read a previous block of config values from disk
		/// </summary>
		/// <param name="Location">The file to read from</param>
		/// <param name="Types">Array of valid types. Used to resolve serialized type names to concrete types.</param>
		/// <param name="Data">On success, receives the parsed data</param>
		/// <returns>True if the data was read and is valid</returns>
		public static bool TryRead(FileReference Location, IEnumerable<Type> Types, [NotNullWhen(true)] out XmlConfigData? Data)
		{
			// Check the file exists first
			if(!FileReference.Exists(Location))
			{
				Data = null;
				return false;
			}

			// Read the cache from disk
			using (BinaryReader Reader = new BinaryReader(File.Open(Location.FullName, FileMode.Open, FileAccess.Read, FileShare.Read)))
			{
				// Check the serialization version matches
				if(Reader.ReadInt32() != SerializationVersion)
				{
					Data = null;
					return false;
				}

				// Read the input files
				FileReference[] InputFiles = Reader.ReadArray(() => Reader.ReadFileReference())!;

				// Read the types
				int NumTypes = Reader.ReadInt32();
				Dictionary<Type, ValueInfo[]> TypeToValues = new Dictionary<Type, ValueInfo[]>(NumTypes);
				for(int TypeIdx = 0; TypeIdx < NumTypes; TypeIdx++)
				{
					// Read the type name
					string TypeName = Reader.ReadString();

					// Try to find it in the list of configurable types
					Type Type = Types.FirstOrDefault(x => x.Name == TypeName);
					if(Type == null)
					{
						Data = null;
						return false;
					}

					// Read all the values
					ValueInfo[] Values = new ValueInfo[Reader.ReadInt32()];
					for(int ValueIdx = 0; ValueIdx < Values.Length; ValueIdx++)
					{
						string FieldName = Reader.ReadString();

						// Find the matching field on the output type
						FieldInfo? Field = Type.GetField(FieldName, BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static);
						XmlConfigFileAttribute? XmlConfigAttribute = Field?.GetCustomAttribute<XmlConfigFileAttribute>();
						if(Field == null || XmlConfigAttribute == null)
						{
							Data = null;
							return false;
						}

						// Try to parse the value and add it to the output array
						object Value = Reader.ReadObject(Field.FieldType)!;
						
						// Read the path of the config file that provided this setting
						FileReference SourceFile = Reader.ReadFileReference();

						Values[ValueIdx] = new ValueInfo(Field, Value, SourceFile, XmlConfigAttribute);
					}

					// Add it to the type map
					TypeToValues.Add(Type, Values);
				}

				// Return the parsed data
				Data = new XmlConfigData(InputFiles.ToArray(), TypeToValues);
				return true;
			}
		}

		/// <summary>
		/// Writes the coalesced config hierarchy to disk
		/// </summary>
		/// <param name="Location">File to write to</param>
		public void Write(FileReference Location)
		{
			DirectoryReference.CreateDirectory(Location.Directory);
			using (BinaryWriter Writer = new BinaryWriter(File.Open(Location.FullName, FileMode.Create, FileAccess.Write, FileShare.Read)))
			{
				Writer.Write(SerializationVersion);

				// Save all the input files. The cache will not be valid if these change.
				Writer.Write(InputFiles, Item => Writer.Write(Item));

				// Write all the categories
				Writer.Write(TypeToValues.Count);
				foreach(KeyValuePair<Type, ValueInfo[]> TypePair in TypeToValues)
				{
					Writer.Write(TypePair.Key.Name);
					Writer.Write(TypePair.Value.Length);
					foreach(ValueInfo FieldPair in TypePair.Value)
					{
						Writer.Write(FieldPair.FieldInfo.Name);
						Writer.Write(FieldPair.FieldInfo.FieldType, FieldPair.Value);
						Writer.Write(FieldPair.SourceFile);
					}
				}
			}
		}
	}
}
