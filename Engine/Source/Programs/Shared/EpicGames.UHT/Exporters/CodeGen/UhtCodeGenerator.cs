// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Threading.Tasks;

namespace EpicGames.UHT.Exporters.CodeGen
{
	[UnrealHeaderTool]
	class UhtCodeGenerator
	{
		[UhtExporter(Name = "CodeGen", Description = "Standard UnrealEngine code generation", Options = UhtExporterOptions.Default,
			CppFilters = new string[] { "*.generated.cpp", "*.generated.*.cpp", "*.gen.cpp", "*.gen.*.cpp" },
			HeaderFilters = new string[] { "*.generated.h" })]
		public static void CodeGenerator(IUhtExportFactory Factory)
		{
			UhtCodeGenerator Generator = new UhtCodeGenerator(Factory);
			Generator.Generate();
		}

		public struct PackageInfo
		{
			public string StrippedName;
			public string Api;
		}
		public PackageInfo[] PackageInfos;

		public struct HeaderInfo
		{
			public IUhtExportTask? Task;
			public string IncludePath;
			public string FileId;
			public uint BodyHash;
			public bool bNeedsPushModelHeaders;
		}
		public HeaderInfo[] HeaderInfos;

		public struct ObjectInfo
		{
			public string RegisteredSingletonName;
			public string UnregisteredSingletonName;
			public string RegsiteredCrossReference;
			public string UnregsiteredCrossReference;
			public string RegsiteredExternalDecl;
			public string UnregisteredExternalDecl;
			public UhtClass? NativeInterface;
			public uint Hash;
		}
		public ObjectInfo[] ObjectInfos;

		public readonly IUhtExportFactory Factory;
		public UhtSession Session => Factory.Session;
		public UhtExportOptions Options => Factory.Options;

		private UhtCodeGenerator(IUhtExportFactory Factory)
		{
			this.Factory = Factory;
			this.HeaderInfos = new HeaderInfo[this.Factory.Session.HeaderFileTypeCount];
			this.ObjectInfos = new ObjectInfo[this.Factory.Session.ObjectTypeCount];
			this.PackageInfos = new PackageInfo[this.Factory.Session.PackageTypeCount];
		}

		private static UhtExportOptions GetAdditionalOptions(UHTManifest.Module Module)
		{
			UhtExportOptions Options = UhtExportOptions.None;
			if (Module.SaveExportedHeaders)
			{
				Options |= UhtExportOptions.WriteOutput;
			}
			return Options;
		}

		private void Generate()
		{
			List<IUhtExportTask> Prereqs = new List<IUhtExportTask>();

			// Perform some startup initialization to compute things we need over and over again
			if (this.Session.bGoWide)
			{
				Parallel.ForEach(this.Factory.Session.Packages, Package =>
				{
					InitPackageInfo(Package);
				});
			}
			else
			{
				foreach (UhtPackage Package in this.Factory.Session.Packages)
				{
					InitPackageInfo(Package);
				}
			}

			// Generate the files for the header files
			foreach (UhtHeaderFile HeaderFile in this.Session.SortedHeaderFiles)
			{
				if (HeaderFile.bShouldExport)
				{
					UhtPackage Package = HeaderFile.Package;
					UHTManifest.Module Module = Package.Module;
					UhtExportOptions AdditionalOptions = GetAdditionalOptions(Module);

					string HeaderPath = this.Factory.MakePath(HeaderFile, ".generated.h");
					string CppPath = this.Factory.MakePath(HeaderFile, ".gen.cpp");

					Prereqs.Clear();
					foreach (UhtHeaderFile Referenced in HeaderFile.ReferencedHeadersNoLock)
					{
						if (HeaderFile != Referenced)
						{
							IUhtExportTask? GeneratedReferenced = this.HeaderInfos[Referenced.HeaderFileTypeIndex].Task;
							if (GeneratedReferenced != null)
							{
								Prereqs.Add(GeneratedReferenced);
							}
						}
					}

					this.HeaderInfos[HeaderFile.HeaderFileTypeIndex].Task = Factory.CreateTask(HeaderPath, CppPath, Prereqs, AdditionalOptions, 
						(IUhtExportOutput HeaderOutput, IUhtExportOutput CppOutput) =>
						{
							new UhtHeaderCodeGeneratorHFile(this, Package, HeaderFile).Generate(HeaderOutput);
							new UhtHeaderCodeGeneratorCppFile(this, Package, HeaderFile).Generate(CppOutput);
						});
				}
			}

			// Generate the files for the packages
			List<IUhtExportTask> GeneratedPackages = new List<IUhtExportTask>(this.Session.PackageTypeCount);
			foreach (UhtPackage Package in this.Session.Packages)
			{
				UHTManifest.Module Module = Package.Module;
				UhtExportOptions AdditionalOptions = GetAdditionalOptions(Module);

				string HeaderPath = this.Factory.MakePath(Package, "Classes.h");
				string CppPath = this.Factory.MakePath(Package, ".init.gen.cpp");

				bool bWriteHeader = false;
				Prereqs.Clear();
				foreach (UhtHeaderFile HeaderFile in Package.Children)
				{
					IUhtExportTask? GeneratedReferenced = this.HeaderInfos[HeaderFile.HeaderFileTypeIndex].Task;
					if (GeneratedReferenced != null)
					{
						Prereqs.Add(GeneratedReferenced);
					}
					if (!bWriteHeader)
					{
						foreach (UhtType Type in HeaderFile.Children)
						{
							if (Type is UhtClass Class)
							{
								if (Class.ClassType != UhtClassType.NativeInterface && 
									Class.ClassFlags.HasExactFlags(EClassFlags.Native | EClassFlags.NoExport | EClassFlags.Intrinsic, EClassFlags.Native))
								{
									bWriteHeader = true;
									break;
								}
							}
						}
					}
				}

				if (bWriteHeader)
				{
					GeneratedPackages.Add(Factory.CreateTask(HeaderPath, CppPath, Prereqs, AdditionalOptions,
						(IUhtExportOutput HeaderOutput, IUhtExportOutput CppOutput) =>
						{
							List<UhtHeaderFile> PackageSortedHeaders = GetSortedHeaderFiles(Package);
							new UhtPackageCodeGeneratorHFile(this, Package).Generate(HeaderOutput, PackageSortedHeaders);
							new UhtPackageCodeGeneratorCppFile(this, Package).Generate(CppOutput, PackageSortedHeaders);
						}));
				}
				else
				{
					GeneratedPackages.Add(Factory.CreateTask(CppPath, Prereqs, AdditionalOptions, 
						(IUhtExportOutput Output) =>
						{
							List<UhtHeaderFile> PackageSortedHeaders = GetSortedHeaderFiles(Package);
							new UhtPackageCodeGeneratorCppFile(this, Package).Generate(Output, PackageSortedHeaders);
						}));
				}
			}

			// Wait for all the packages to complete
			List<Task> PackageTasks = new List<Task>(this.Session.PackageTypeCount);
			foreach (IUhtExportTask Output in GeneratedPackages)
			{
				if (Output.ActionTask != null)
				{
					PackageTasks.Add(Output.ActionTask);
				}
			}
			Task.WaitAll(PackageTasks.ToArray());
		}

#region Utility functions
		/// <summary>
		/// Return the singleton name for an object
		/// </summary>
		/// <param name="Object">The object in question.</param>
		/// <param name="bRegistered">If true, return the registered singleton name.  Otherwise return the unregistered.</param>
		/// <returns>Singleton name or "nullptr" if Object is null</returns>
		public string GetSingletonName(UhtObject? Object, bool bRegistered)
		{
			if (Object == null)
			{
				return "nullptr";
			}
			return bRegistered ? this.ObjectInfos[Object.ObjectTypeIndex].RegisteredSingletonName : this.ObjectInfos[Object.ObjectTypeIndex].UnregisteredSingletonName;
		}

		/// <summary>
		/// Return the external declaration for an object
		/// </summary>
		/// <param name="Object">The object in question.</param>
		/// <param name="bRegistered">If true, return the registered external declaration.  Otherwise return the unregistered.</param>
		/// <returns>External declaration</returns>
		public string GetExternalDecl(UhtObject Object, bool bRegistered)
		{
			return GetExternalDecl(Object.ObjectTypeIndex, bRegistered);
		}

		/// <summary>
		/// Return the external declaration for an object
		/// </summary>
		/// <param name="ObjectIndex">The object in question.</param>
		/// <param name="bRegistered">If true, return the registered external declaration.  Otherwise return the unregistered.</param>
		/// <returns>External declaration</returns>
		public string GetExternalDecl(int ObjectIndex, bool bRegistered)
		{
			return bRegistered ? this.ObjectInfos[ObjectIndex].RegsiteredExternalDecl : this.ObjectInfos[ObjectIndex].UnregisteredExternalDecl;
		}

		/// <summary>
		/// Return the cross reference for an object
		/// </summary>
		/// <param name="Object">The object in question.</param>
		/// <param name="bRegistered">If true, return the registered cross reference.  Otherwise return the unregistered.</param>
		/// <returns>Cross reference</returns>
		public string GetCrossReference(UhtObject Object, bool bRegistered)
		{
			return GetCrossReference(Object.ObjectTypeIndex, bRegistered);
		}

		/// <summary>
		/// Return the cross reference for an object
		/// </summary>
		/// <param name="ObjectIndex">The object in question.</param>
		/// <param name="bRegistered">If true, return the registered cross reference.  Otherwise return the unregistered.</param>
		/// <returns>Cross reference</returns>
		public string GetCrossReference(int ObjectIndex, bool bRegistered)
		{
			return bRegistered ? this.ObjectInfos[ObjectIndex].RegsiteredCrossReference : this.ObjectInfos[ObjectIndex].UnregsiteredCrossReference;
		}
#endregion

#region Information initialization
		private void InitPackageInfo(UhtPackage Package)
		{
			StringBuilder Builder = new StringBuilder();

			ref PackageInfo PackageInfo = ref this.PackageInfos[Package.PackageTypeIndex];
			PackageInfo.StrippedName = Package.SourceName.Replace('/', '_');
			PackageInfo.Api = $"{Package.ShortName.ToString().ToUpper()}_API ";

			// Construct the names used commonly during export
			ref ObjectInfo ObjectInfo = ref this.ObjectInfos[Package.ObjectTypeIndex];
			Builder.Append("Z_Construct_UPackage_");
			Builder.Append(PackageInfo.StrippedName);
			ObjectInfo.UnregisteredSingletonName = ObjectInfo.RegisteredSingletonName = Builder.ToString();
			ObjectInfo.UnregisteredExternalDecl = ObjectInfo.RegsiteredExternalDecl = $"\t{PackageInfo.Api}_API UPackage* {ObjectInfo.RegisteredSingletonName}();\r\n"; //COMPATIBILITY-TODO remove the extra _API
			ObjectInfo.UnregsiteredCrossReference = ObjectInfo.RegsiteredCrossReference = $"\tUPackage* {ObjectInfo.RegisteredSingletonName}();\r\n";

			foreach (UhtHeaderFile HeaderFile in Package.Children)
			{
				InitHeaderInfo(Builder, Package, ref PackageInfo, HeaderFile);
			}
		}

		private void InitHeaderInfo(StringBuilder Builder, UhtPackage Package, ref PackageInfo PackageInfo, UhtHeaderFile HeaderFile)
		{
			ref HeaderInfo HeaderInfo = ref this.HeaderInfos[HeaderFile.HeaderFileTypeIndex];

			HeaderInfo.IncludePath = Path.GetRelativePath(Package.Module.IncludeBase, HeaderFile.FilePath).Replace('\\', '/');

			// Convert the file path to a C identifier
			string FilePath = HeaderFile.FilePath;
			bool bIsRelative = !Path.IsPathRooted(FilePath);
			if (!bIsRelative && Session.EngineDirectory != null)
			{
				string? Directory = Path.GetDirectoryName(Session.EngineDirectory);
				if (!string.IsNullOrEmpty(Directory))
				{
					FilePath = Path.GetRelativePath(Directory, FilePath);
					bIsRelative = !Path.IsPathRooted(FilePath);
				}
			}
			if (!bIsRelative && Session.ProjectDirectory != null)
			{
				string? Directory = Path.GetDirectoryName(Session.ProjectDirectory);
				if (!string.IsNullOrEmpty(Directory))
				{
					FilePath = Path.GetRelativePath(Directory, FilePath);
					bIsRelative = !Path.IsPathRooted(FilePath);
				}
			}
			FilePath = FilePath.Replace('\\', '/');
			if (bIsRelative)
			{
				while (FilePath.StartsWith("../"))
				{
					FilePath = FilePath.Substring(3);
				}
			}

			char[] OutFilePath = new char[FilePath.Length + 4];
			OutFilePath[0] = 'F';
			OutFilePath[1] = 'I';
			OutFilePath[2] = 'D';
			OutFilePath[3] = '_';
			for (int Index = 0; Index < FilePath.Length; ++Index)
			{
				OutFilePath[Index+4] = UhtFCString.IsAlnum(FilePath[Index]) ? FilePath[Index] : '_';
			}
			HeaderInfo.FileId = new string(OutFilePath);

			foreach (UhtObject Object in HeaderFile.Children)
			{
				InitObjectInfo(Builder, Package, ref PackageInfo, ref HeaderInfo, Object);
				if (Object is UhtClass Class)
				{
				}
			}
		}

		private void InitObjectInfo(StringBuilder Builder, UhtPackage Package, ref PackageInfo PackageInfo, ref HeaderInfo HeaderInfo, UhtObject Object)
		{
			ref ObjectInfo ObjectInfo = ref this.ObjectInfos[Object.ObjectTypeIndex];

			Builder.Clear();

			// Construct the names used commonly during export
			bool bIsNonIntrinsicClass = false;
			Builder.Append("Z_Construct_U").Append(Object.EngineClassName).AppendOuterNames(Object);

			string EngineClassName = Object.EngineClassName;
			if (Object is UhtClass Class)
			{
				if (!Class.ClassFlags.HasAnyFlags(EClassFlags.Intrinsic))
				{
					bIsNonIntrinsicClass = true;
				}
				if (Class.ClassExportFlags.HasExactFlags(UhtClassExportFlags.HasReplciatedProperties, UhtClassExportFlags.SelfHasReplicatedProperties))
				{
					HeaderInfo.bNeedsPushModelHeaders = true;
				}
				if (Class.ClassType == UhtClassType.NativeInterface)
				{
					if (Class.AlternateObject != null)
					{
						this.ObjectInfos[Class.AlternateObject.ObjectTypeIndex].NativeInterface = Class;
					}
				}
			}
			else if (Object is UhtFunction)
			{
				// The method for EngineClassName returns type specific where in this case we need just the simple return type
				EngineClassName = "Function";
			}

			if (bIsNonIntrinsicClass)
			{
				ObjectInfo.RegisteredSingletonName = Builder.ToString();
				Builder.Append("_NoRegister");
				ObjectInfo.UnregisteredSingletonName = Builder.ToString();

				ObjectInfo.UnregisteredExternalDecl = $"\t{PackageInfo.Api}U{EngineClassName}* {ObjectInfo.UnregisteredSingletonName}();\r\n";
				ObjectInfo.RegsiteredExternalDecl = $"\t{PackageInfo.Api}U{EngineClassName}* {ObjectInfo.RegisteredSingletonName}();\r\n";
			}
			else
			{
				ObjectInfo.UnregisteredSingletonName = ObjectInfo.RegisteredSingletonName = Builder.ToString();
				ObjectInfo.UnregisteredExternalDecl = ObjectInfo.RegsiteredExternalDecl = $"\t{PackageInfo.Api}U{EngineClassName}* {ObjectInfo.RegisteredSingletonName}();\r\n";
			}

			//COMPATIBILITY-TODO - The cross reference string should match the extern decl string always.  But currently, it is different for packages.
			ObjectInfo.UnregsiteredCrossReference = ObjectInfo.UnregisteredExternalDecl;
			ObjectInfo.RegsiteredCrossReference = ObjectInfo.RegsiteredExternalDecl;

			// Init the children
			foreach (UhtType Child in Object.Children)
			{
				if (Child is UhtObject ChildObject)
				{
					InitObjectInfo(Builder, Package, ref PackageInfo, ref HeaderInfo, ChildObject);
				}
			}
		}
#endregion

#region Utility functions
		/// <summary>
		/// Return a package's sorted header file list of all header files that or referenced or have declarations.
		/// </summary>
		/// <param name="Package">The package in question</param>
		/// <returns>Sorted list of the header files</returns>
		private static List<UhtHeaderFile> GetSortedHeaderFiles(UhtPackage Package)
		{
			List<UhtHeaderFile> Out = new List<UhtHeaderFile>(Package.Children.Count);
			foreach (UhtHeaderFile HeaderFile in Package.Children)
			{
				if (HeaderFile.bShouldExport)
				{
					Out.Add(HeaderFile);
				}
			}
			Out.Sort((Lhs, Rhs) => { return StringComparerUE.OrdinalIgnoreCase.Compare(Lhs.FilePath, Rhs.FilePath); });
			return Out;
		}
#endregion
	}
}
