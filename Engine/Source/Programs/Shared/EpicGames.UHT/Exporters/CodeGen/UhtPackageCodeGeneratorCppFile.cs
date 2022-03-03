// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Types;
using EpicGames.UHT.Utils;
using System;
using System.Collections.Generic;
using System.Text;

namespace EpicGames.UHT.Exporters.CodeGen
{
	internal class UhtPackageCodeGeneratorCppFile : UhtPackageCodeGenerator
	{
		/// <summary>
		/// Construct an instance of this generator object
		/// </summary>
		/// <param name="CodeGenerator">The base code generator</param>
		/// <param name="Package">Package being generated</param>
		public UhtPackageCodeGeneratorCppFile(UhtCodeGenerator CodeGenerator, UhtPackage Package)
			: base(CodeGenerator, Package)
		{
		}

		/// <summary>
		/// For a given UE header file, generated the generated cpp file
		/// </summary>
		/// <param name="CppOutput">Output object</param>
		/// <param name="PackageSortedHeaders">Sorted list of headers by name, in the package.</param>
		public void Generate(IUhtExportOutput CppOutput, List<UhtHeaderFile> PackageSortedHeaders)
		{
			using (BorrowStringBuilder Borrower = new BorrowStringBuilder(StringBuilderCache.Big))
			{
				StringBuilder Builder = Borrower.StringBuilder;
				const string MetaDataParamsName = "Package_MetaDataParams";

				// Collect information from all of the headers
				List<UhtField> Singletons = new List<UhtField>();
				StringBuilder Declarations = new StringBuilder();
				uint BodiesHash = 0;
				foreach (UhtHeaderFile HeaderFile in PackageSortedHeaders)
				{
					ref UhtCodeGenerator.HeaderInfo HeaderInfo = ref this.HeaderInfos[HeaderFile.HeaderFileTypeIndex];
					ReadOnlyMemory<string> Sorted = HeaderFile.References.Declaration.GetSortedReferences(
						(int ObjectIndex, bool bRegistered) => GetExternalDecl(ObjectIndex, bRegistered));
					foreach (string Declaration in Sorted.Span)
					{
						Declarations.Append(Declaration);
					}

					Singletons.AddRange(HeaderFile.References.Singletons);

					uint BodyHash = this.HeaderInfos[HeaderFile.HeaderFileTypeIndex].BodyHash;
					if (BodiesHash == 0)
					{
						// Don't combine in the first case because it keeps GUID backwards compatibility
						BodiesHash = BodyHash;
					}
					else
					{
						BodiesHash = HashCombine(BodyHash, BodiesHash);
					}
				}

				// No need to generate output if we have no declarations
				if (Declarations.Length == 0)
				{
					return;
				}
				uint DeclarationsHash = UhtHash.GenenerateTextHash(Declarations.ToString());

				string StrippedName = this.PackageInfos[this.Package.PackageTypeIndex].StrippedName;
				string SingletonName = this.PackageSingletonName;
				Singletons.Sort((UhtField Lhs, UhtField Rhs) =>
				{
					bool bLhsIsDel = IsDelegateFunction(Lhs);
					bool bRhsIsDel = IsDelegateFunction(Rhs);
					if (bLhsIsDel != bRhsIsDel)
					{
						return !bLhsIsDel ? -1 : 1;
					}
					return StringComparerUE.OrdinalIgnoreCase.Compare(
						this.ObjectInfos[Lhs.ObjectTypeIndex].RegisteredSingletonName,
						this.ObjectInfos[Rhs.ObjectTypeIndex].RegisteredSingletonName);
				});

				Builder.Append(HeaderCopyright);
				Builder.Append(RequiredCPPIncludes);
				Builder.Append(DisableDeprecationWarnings).Append("\r\n");
				Builder.Append("void EmptyLinkFunctionForGeneratedCode").Append(this.Package.ShortName).Append("_init() {}\r\n");

				if (this.Session.bIncludeDebugOutput)
				{
					Builder.Append("#if 0\r\n");
					Builder.Append(Declarations.ToString());
					Builder.Append("#endif\r\n");
				}

				foreach (UhtObject Object in Singletons)
				{
					ref UhtCodeGenerator.ObjectInfo Info = ref this.ObjectInfos[Object.ObjectTypeIndex];
					Builder.Append(Info.RegsiteredExternalDecl);
				}

				Builder.AppendMetaDataDef(this.Package, null, MetaDataParamsName, 3);

				Builder.Append("\tstatic FPackageRegistrationInfo Z_Registration_Info_UPackage_").Append(StrippedName).Append(";\r\n");
				Builder.Append("\tFORCENOINLINE UPackage* ").Append(SingletonName).Append("()\r\n");
				Builder.Append("\t{\r\n");
				Builder.Append("\t\tif (!Z_Registration_Info_UPackage_").Append(StrippedName).Append(".OuterSingleton)\r\n");
				Builder.Append("\t\t{\r\n");

				if (Singletons.Count != 0)
				{
					Builder.Append("\t\t\tstatic UObject* (*const SingletonFuncArray[])() = {\r\n");
					foreach (UhtField Field in Singletons)
					{
						Builder.Append("\t\t\t\t(UObject* (*)())").Append(this.ObjectInfos[Field.ObjectTypeIndex].RegisteredSingletonName).Append(",\r\n");
					}
					Builder.Append("\t\t\t};\r\n");
				}

				EPackageFlags Flags = this.Package.PackageFlags & (EPackageFlags.ClientOptional | EPackageFlags.ServerSideOnly | EPackageFlags.EditorOnly | EPackageFlags.Developer | EPackageFlags.UncookedOnly);
				Builder.Append("\t\t\tstatic const UECodeGen_Private::FPackageParams PackageParams = {\r\n");
				Builder.Append("\t\t\t\t").AppendUTF8LiteralString(this.Package.SourceName).Append(",\r\n");
				Builder.Append("\t\t\t\t").Append(Singletons.Count != 0 ? "SingletonFuncArray" : "nullptr").Append(",\r\n");
				Builder.Append("\t\t\t\t").Append(Singletons.Count != 0 ? "UE_ARRAY_COUNT(SingletonFuncArray)" : "0").Append(",\r\n");
				Builder.Append("\t\t\t\tPKG_CompiledIn | ").Append($"0x{(uint)Flags:X8}").Append(",\r\n");
				Builder.Append("\t\t\t\t").Append($"0x{BodiesHash:X8}").Append(",\r\n");
				Builder.Append("\t\t\t\t").Append($"0x{DeclarationsHash:X8}").Append(",\r\n");
				Builder.Append("\t\t\t\t").AppendMetaDataParams(this.Package, null, MetaDataParamsName).Append("\r\n");
				Builder.Append("\t\t\t};\r\n");
				Builder.Append("\t\t\tUECodeGen_Private::ConstructUPackage(Z_Registration_Info_UPackage_").Append(StrippedName).Append(".OuterSingleton, PackageParams);\r\n");
				Builder.Append("\t\t}\r\n");
				Builder.Append("\t\treturn Z_Registration_Info_UPackage_").Append(StrippedName).Append(".OuterSingleton;\r\n");
				Builder.Append("\t}\r\n");

				// Do not change the Z_CompiledInDeferPackage_UPackage_ without changing LC_SymbolPatterns
				Builder.Append("\tstatic FRegisterCompiledInInfo Z_CompiledInDeferPackage_UPackage_").Append(StrippedName).Append("(").Append(SingletonName)
					.Append(", TEXT(\"").Append(this.Package.SourceName).Append("\"), Z_Registration_Info_UPackage_").Append(StrippedName).Append(", CONSTRUCT_RELOAD_VERSION_INFO(FPackageReloadVersionInfo, ")
					.Append($"0x{BodiesHash:X8}, 0x{DeclarationsHash:X8}").Append("));\r\n");

				Builder.Append(EnableDeprecationWarnings).Append("\r\n");
				CppOutput.CommitOutput(Builder);
			}
		}
	}
}
