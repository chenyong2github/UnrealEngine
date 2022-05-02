// Copyright Epic Games, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Threading.Tasks;
using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;

namespace EpicGames.UHT.Exporters.CodeGen
{
	[UnrealHeaderTool]
	class UhtCodeGenerator
	{
		[UhtExporter(Name = "CodeGen", Description = "Standard UnrealEngine code generation", Options = UhtExporterOptions.Default,
			CppFilters = new string[] { "*.generated.cpp", "*.generated.*.cpp", "*.gen.cpp", "*.gen.*.cpp" },
			HeaderFilters = new string[] { "*.generated.h" })]
		public static void CodeGenerator(IUhtExportFactory factory)
		{
			UhtCodeGenerator generator = new UhtCodeGenerator(factory);
			generator.Generate();
		}

		public struct PackageInfo
		{
			public string _strippedName;
			public string _api;
		}
		public PackageInfo[] _packageInfos;

		public struct HeaderInfo
		{
			public Task? _task;
			public string _includePath;
			public string _fileId;
			public uint _bodyHash;
			public bool _needsPushModelHeaders;
		}
		public HeaderInfo[] _headerInfos;

		public struct ObjectInfo
		{
			public string _registeredSingletonName;
			public string _unregisteredSingletonName;
			public string _regsiteredCrossReference;
			public string _unregsiteredCrossReference;
			public string _regsiteredExternalDecl;
			public string _unregisteredExternalDecl;
			public UhtClass? _nativeInterface;
			public uint _hash;
		}
		public ObjectInfo[] _objectInfos;

		public readonly IUhtExportFactory Factory;
		public UhtSession Session => Factory.Session;

		private UhtCodeGenerator(IUhtExportFactory factory)
		{
			this.Factory = factory;
			this._headerInfos = new HeaderInfo[this.Factory.Session.HeaderFileTypeCount];
			this._objectInfos = new ObjectInfo[this.Factory.Session.ObjectTypeCount];
			this._packageInfos = new PackageInfo[this.Factory.Session.PackageTypeCount];
		}

		private void Generate()
		{
			List<Task?> prereqs = new List<Task?>();

			// Perform some startup initialization to compute things we need over and over again
			if (this.Session.GoWide)
			{
				Parallel.ForEach(this.Factory.Session.Packages, package =>
				{
					InitPackageInfo(package);
				});
			}
			else
			{
				foreach (UhtPackage package in this.Factory.Session.Packages)
				{
					InitPackageInfo(package);
				}
			}

			// Generate the files for the header files
			foreach (UhtHeaderFile headerFile in this.Session.SortedHeaderFiles)
			{
				if (headerFile.ShouldExport)
				{
					UhtPackage package = headerFile.Package;
					UHTManifest.Module module = package.Module;

					prereqs.Clear();
					foreach (UhtHeaderFile referenced in headerFile.ReferencedHeadersNoLock)
					{
						if (headerFile != referenced)
						{
							prereqs.Add(this._headerInfos[referenced.HeaderFileTypeIndex]._task);
						}
					}

					this._headerInfos[headerFile.HeaderFileTypeIndex]._task = Factory.CreateTask(prereqs,
						(IUhtExportFactory factory) =>
						{
							new UhtHeaderCodeGeneratorHFile(this, package, headerFile).Generate(factory);
							new UhtHeaderCodeGeneratorCppFile(this, package, headerFile).Generate(factory);
						});
				}
			}

			// Generate the files for the packages
			List<Task?> generatedPackages = new List<Task?>(this.Session.PackageTypeCount);
			foreach (UhtPackage package in this.Session.Packages)
			{
				UHTManifest.Module module = package.Module;

				bool writeHeader = false;
				prereqs.Clear();
				foreach (UhtHeaderFile headerFile in package.Children)
				{
					prereqs.Add(this._headerInfos[headerFile.HeaderFileTypeIndex]._task);
					if (!writeHeader)
					{
						foreach (UhtType type in headerFile.Children)
						{
							if (type is UhtClass classObj)
							{
								if (classObj.ClassType != UhtClassType.NativeInterface &&
									classObj.ClassFlags.HasExactFlags(EClassFlags.Native | EClassFlags.Intrinsic, EClassFlags.Native) &&
									!classObj.ClassExportFlags.HasAllFlags(UhtClassExportFlags.NoExport))
								{
									writeHeader = true;
									break;
								}
							}
						}
					}
				}

				generatedPackages.Add(Factory.CreateTask(prereqs,
					(IUhtExportFactory factory) =>
					{
						List<UhtHeaderFile> packageSortedHeaders = GetSortedHeaderFiles(package);
						if (writeHeader)
						{
							new UhtPackageCodeGeneratorHFile(this, package).Generate(factory, packageSortedHeaders);
						}
						new UhtPackageCodeGeneratorCppFile(this, package).Generate(factory, packageSortedHeaders);
					}));
			}

			// Wait for all the packages to complete
			List<Task> packageTasks = new List<Task>(this.Session.PackageTypeCount);
			foreach (Task? output in generatedPackages)
			{
				if (output != null)
				{
					packageTasks.Add(output);
				}
			}
			Task.WaitAll(packageTasks.ToArray());
		}

		#region Utility functions
		/// <summary>
		/// Return the singleton name for an object
		/// </summary>
		/// <param name="obj">The object in question.</param>
		/// <param name="registered">If true, return the registered singleton name.  Otherwise return the unregistered.</param>
		/// <returns>Singleton name or "nullptr" if Object is null</returns>
		public string GetSingletonName(UhtObject? obj, bool registered)
		{
			if (obj == null)
			{
				return "nullptr";
			}
			return registered ? this._objectInfos[obj.ObjectTypeIndex]._registeredSingletonName : this._objectInfos[obj.ObjectTypeIndex]._unregisteredSingletonName;
		}

		/// <summary>
		/// Return the external declaration for an object
		/// </summary>
		/// <param name="obj">The object in question.</param>
		/// <param name="registered">If true, return the registered external declaration.  Otherwise return the unregistered.</param>
		/// <returns>External declaration</returns>
		public string GetExternalDecl(UhtObject obj, bool registered)
		{
			return GetExternalDecl(obj.ObjectTypeIndex, registered);
		}

		/// <summary>
		/// Return the external declaration for an object
		/// </summary>
		/// <param name="objectIndex">The object in question.</param>
		/// <param name="registered">If true, return the registered external declaration.  Otherwise return the unregistered.</param>
		/// <returns>External declaration</returns>
		public string GetExternalDecl(int objectIndex, bool registered)
		{
			return registered ? this._objectInfos[objectIndex]._regsiteredExternalDecl : this._objectInfos[objectIndex]._unregisteredExternalDecl;
		}

		/// <summary>
		/// Return the cross reference for an object
		/// </summary>
		/// <param name="obj">The object in question.</param>
		/// <param name="registered">If true, return the registered cross reference.  Otherwise return the unregistered.</param>
		/// <returns>Cross reference</returns>
		public string GetCrossReference(UhtObject obj, bool registered)
		{
			return GetCrossReference(obj.ObjectTypeIndex, registered);
		}

		/// <summary>
		/// Return the cross reference for an object
		/// </summary>
		/// <param name="objectIndex">The object in question.</param>
		/// <param name="registered">If true, return the registered cross reference.  Otherwise return the unregistered.</param>
		/// <returns>Cross reference</returns>
		public string GetCrossReference(int objectIndex, bool registered)
		{
			return registered ? this._objectInfos[objectIndex]._regsiteredCrossReference : this._objectInfos[objectIndex]._unregsiteredCrossReference;
		}
		#endregion

		#region Information initialization
		private void InitPackageInfo(UhtPackage package)
		{
			StringBuilder builder = new StringBuilder();

			ref PackageInfo packageInfo = ref this._packageInfos[package.PackageTypeIndex];
			packageInfo._strippedName = package.SourceName.Replace('/', '_');
			packageInfo._api = $"{package.ShortName.ToString().ToUpper()}_API ";

			// Construct the names used commonly during export
			ref ObjectInfo objectInfo = ref this._objectInfos[package.ObjectTypeIndex];
			builder.Append("Z_Construct_UPackage_");
			builder.Append(packageInfo._strippedName);
			objectInfo._unregisteredSingletonName = objectInfo._registeredSingletonName = builder.ToString();
			objectInfo._unregisteredExternalDecl = objectInfo._regsiteredExternalDecl = $"\t{packageInfo._api}_API UPackage* {objectInfo._registeredSingletonName}();\r\n"; //COMPATIBILITY-TODO remove the extra _API
			objectInfo._unregsiteredCrossReference = objectInfo._regsiteredCrossReference = $"\tUPackage* {objectInfo._registeredSingletonName}();\r\n";

			foreach (UhtHeaderFile headerFile in package.Children)
			{
				InitHeaderInfo(builder, package, ref packageInfo, headerFile);
			}
		}

		private void InitHeaderInfo(StringBuilder builder, UhtPackage package, ref PackageInfo packageInfo, UhtHeaderFile headerFile)
		{
			ref HeaderInfo headerInfo = ref this._headerInfos[headerFile.HeaderFileTypeIndex];

			headerInfo._includePath = Path.GetRelativePath(package.Module.IncludeBase, headerFile.FilePath).Replace('\\', '/');

			// Convert the file path to a C identifier
			string filePath = headerFile.FilePath;
			bool isRelative = !Path.IsPathRooted(filePath);
			if (!isRelative && Session.EngineDirectory != null)
			{
				string? directory = Path.GetDirectoryName(Session.EngineDirectory);
				if (!String.IsNullOrEmpty(directory))
				{
					filePath = Path.GetRelativePath(directory, filePath);
					isRelative = !Path.IsPathRooted(filePath);
				}
			}
			if (!isRelative && Session.ProjectDirectory != null)
			{
				string? directory = Path.GetDirectoryName(Session.ProjectDirectory);
				if (!String.IsNullOrEmpty(directory))
				{
					filePath = Path.GetRelativePath(directory, filePath);
					isRelative = !Path.IsPathRooted(filePath);
				}
			}
			filePath = filePath.Replace('\\', '/');
			if (isRelative)
			{
				while (filePath.StartsWith("../", StringComparison.Ordinal))
				{
					filePath = filePath.Substring(3);
				}
			}

			char[] outFilePath = new char[filePath.Length + 4];
			outFilePath[0] = 'F';
			outFilePath[1] = 'I';
			outFilePath[2] = 'D';
			outFilePath[3] = '_';
			for (int index = 0; index < filePath.Length; ++index)
			{
				outFilePath[index + 4] = UhtFCString.IsAlnum(filePath[index]) ? filePath[index] : '_';
			}
			headerInfo._fileId = new string(outFilePath);

			foreach (UhtObject obj in headerFile.Children)
			{
				InitObjectInfo(builder, package, ref packageInfo, ref headerInfo, obj);
			}
		}

		private void InitObjectInfo(StringBuilder builder, UhtPackage package, ref PackageInfo packageInfo, ref HeaderInfo headerInfo, UhtObject obj)
		{
			ref ObjectInfo objectInfo = ref this._objectInfos[obj.ObjectTypeIndex];

			builder.Clear();

			// Construct the names used commonly during export
			bool isNonIntrinsicClass = false;
			builder.Append("Z_Construct_U").Append(obj.EngineClassName).AppendOuterNames(obj);

			string engineClassName = obj.EngineClassName;
			if (obj is UhtClass classObj)
			{
				if (!classObj.ClassFlags.HasAnyFlags(EClassFlags.Intrinsic))
				{
					isNonIntrinsicClass = true;
				}
				if (classObj.ClassExportFlags.HasExactFlags(UhtClassExportFlags.HasReplciatedProperties, UhtClassExportFlags.SelfHasReplicatedProperties))
				{
					headerInfo._needsPushModelHeaders = true;
				}
				if (classObj.ClassType == UhtClassType.NativeInterface)
				{
					if (classObj.AlternateObject != null)
					{
						this._objectInfos[classObj.AlternateObject.ObjectTypeIndex]._nativeInterface = classObj;
					}
				}
			}
			else if (obj is UhtFunction)
			{
				// The method for EngineClassName returns type specific where in this case we need just the simple return type
				engineClassName = "Function";
			}

			if (isNonIntrinsicClass)
			{
				objectInfo._registeredSingletonName = builder.ToString();
				builder.Append("_NoRegister");
				objectInfo._unregisteredSingletonName = builder.ToString();

				objectInfo._unregisteredExternalDecl = $"\t{packageInfo._api}U{engineClassName}* {objectInfo._unregisteredSingletonName}();\r\n";
				objectInfo._regsiteredExternalDecl = $"\t{packageInfo._api}U{engineClassName}* {objectInfo._registeredSingletonName}();\r\n";
			}
			else
			{
				objectInfo._unregisteredSingletonName = objectInfo._registeredSingletonName = builder.ToString();
				objectInfo._unregisteredExternalDecl = objectInfo._regsiteredExternalDecl = $"\t{packageInfo._api}U{engineClassName}* {objectInfo._registeredSingletonName}();\r\n";
			}

			//COMPATIBILITY-TODO - The cross reference string should match the extern decl string always.  But currently, it is different for packages.
			objectInfo._unregsiteredCrossReference = objectInfo._unregisteredExternalDecl;
			objectInfo._regsiteredCrossReference = objectInfo._regsiteredExternalDecl;

			// Init the children
			foreach (UhtType child in obj.Children)
			{
				if (child is UhtObject childObject)
				{
					InitObjectInfo(builder, package, ref packageInfo, ref headerInfo, childObject);
				}
			}
		}
		#endregion

		#region Utility functions
		/// <summary>
		/// Return a package's sorted header file list of all header files that or referenced or have declarations.
		/// </summary>
		/// <param name="package">The package in question</param>
		/// <returns>Sorted list of the header files</returns>
		private static List<UhtHeaderFile> GetSortedHeaderFiles(UhtPackage package)
		{
			List<UhtHeaderFile> sortedHeaders = new List<UhtHeaderFile>(package.Children.Count);
			foreach (UhtHeaderFile headerFile in package.Children)
			{
				if (headerFile.ShouldExport)
				{
					sortedHeaders.Add(headerFile);
				}
			}
			sortedHeaders.Sort((lhs, rhs) => { return StringComparerUE.OrdinalIgnoreCase.Compare(lhs.FilePath, rhs.FilePath); });
			return sortedHeaders;
		}
		#endregion
	}
}
