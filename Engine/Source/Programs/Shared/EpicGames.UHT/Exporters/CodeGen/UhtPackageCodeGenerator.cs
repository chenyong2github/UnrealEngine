// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Types;
using System.Text;

namespace EpicGames.UHT.Exporters.CodeGen
{
	internal class UhtPackageCodeGenerator
	{
		public static string HeaderCopyright =
			"// Copyright Epic Games, Inc. All Rights Reserved.\r\n" +
			"/*===========================================================================\r\n" +
			"\tGenerated code exported from UnrealHeaderTool.\r\n" +
			"\tDO NOT modify this manually! Edit the corresponding .h files instead!\r\n" +
			"===========================================================================*/\r\n" +
			"\r\n";

		public static string RequiredCPPIncludes = "#include \"UObject/GeneratedCppIncludes.h\"\r\n";

		public static string EnableDeprecationWarnings = "PRAGMA_ENABLE_DEPRECATION_WARNINGS";
		public static string DisableDeprecationWarnings = "PRAGMA_DISABLE_DEPRECATION_WARNINGS";

		public static string BeginEditorOnlyGuard = "#if WITH_EDITOR\r\n";
		public static string EndEditorOnlyGuard = "#endif //WITH_EDITOR\r\n"; //COMPATIBILITY-TODO - This does not match UhtMacroBlockEmitter

		public readonly UhtCodeGenerator CodeGenerator;
		public readonly UhtPackage Package;

		public Utils.UhtSession Session => this.CodeGenerator.Session;
		public UhtCodeGenerator.PackageInfo[] PackageInfos => this.CodeGenerator.PackageInfos;
		public UhtCodeGenerator.HeaderInfo[] HeaderInfos => this.CodeGenerator.HeaderInfos;
		public UhtCodeGenerator.ObjectInfo[] ObjectInfos => this.CodeGenerator.ObjectInfos;
		public string PackageApi => this.PackageInfos[this.Package.PackageTypeIndex].Api;
		public string PackageSingletonName => this.ObjectInfos[this.Package.ObjectTypeIndex].RegisteredSingletonName;

		public UhtPackageCodeGenerator(UhtCodeGenerator CodeGenerator, UhtPackage Package)
		{
			this.CodeGenerator = CodeGenerator;
			this.Package = Package;
		}

		#region Utility functions

		/// <summary>
		/// Return the singleton name for an object
		/// </summary>
		/// <param name="Object">The object in question.</param>
		/// <param name="bRegistered">If true, return the registered singleton name.  Otherwise return the unregistered.</param>
		/// <returns>Singleton name of "nullptr" if Object is null</returns>
		public string GetSingletonName(UhtObject? Object, bool bRegistered)
		{
			return this.CodeGenerator.GetSingletonName(Object, bRegistered);
		}

		/// <summary>
		/// Return the external declaration for an object
		/// </summary>
		/// <param name="Object">The object in question.</param>
		/// <param name="bRegistered">If true, return the registered external declaration.  Otherwise return the unregistered.</param>
		/// <returns>External declaration</returns>
		public string GetExternalDecl(UhtObject Object, bool bRegistered)
		{
			return this.CodeGenerator.GetExternalDecl(Object, bRegistered);
		}

		/// <summary>
		/// Return the external declaration for an object
		/// </summary>
		/// <param name="ObjectIndex">The object in question.</param>
		/// <param name="bRegistered">If true, return the registered external declaration.  Otherwise return the unregistered.</param>
		/// <returns>External declaration</returns>
		public string GetExternalDecl(int ObjectIndex, bool bRegistered)
		{
			return this.CodeGenerator.GetExternalDecl(ObjectIndex, bRegistered);
		}

		/// <summary>
		/// Return the cross reference for an object
		/// </summary>
		/// <param name="Object">The object in question.</param>
		/// <param name="bRegistered">If true, return the registered cross reference.  Otherwise return the unregistered.</param>
		/// <returns>Cross reference</returns>
		public string GetCrossReference(UhtObject Object, bool bRegistered)
		{
			return this.CodeGenerator.GetCrossReference(Object, bRegistered);
		}

		/// <summary>
		/// Return the cross reference for an object
		/// </summary>
		/// <param name="ObjectIndex">The object in question.</param>
		/// <param name="bRegistered">If true, return the registered cross reference.  Otherwise return the unregistered.</param>
		/// <returns>Cross reference</returns>
		public string GetCrossReference(int ObjectIndex, bool bRegistered)
		{
			return this.CodeGenerator.GetCrossReference(ObjectIndex, bRegistered);
		}

		/// <summary>
		/// Test to see if the given field is a delegate function
		/// </summary>
		/// <param name="Field">Field to be tested</param>
		/// <returns>True if the field is a delegate function</returns>
		public static bool IsDelegateFunction(UhtField Field)
		{
			if (Field is UhtFunction Function)
			{
				return Function.FunctionType.IsDelegate();
			}
			return false;
		}

		/// <summary>
		/// Combines two hash values to get a third.
		/// Note - this function is not commutative.
		///
		/// This function cannot change for backward compatibility reasons.
		/// You may want to choose HashCombineFast for a better in-memory hash combining function.
		/// 
		/// NOTE: This is a copy of the method in TypeHash.h
		/// </summary>
		/// <param name="A">Hash to merge</param>
		/// <param name="C">Previously combined hash</param>
		/// <returns>Resulting hash value</returns>
		public static uint HashCombine(uint A, uint C)
		{
			uint B = 0x9e3779b9;
			A += B;

			A -= B; A -= C; A ^= (C >> 13);
			B -= C; B -= A; B ^= (A << 8);
			C -= A; C -= B; C ^= (B >> 13);
			A -= B; A -= C; A ^= (C >> 12);
			B -= C; B -= A; B ^= (A << 16);
			C -= A; C -= B; C ^= (B >> 5);
			A -= B; A -= C; A ^= (C >> 3);
			B -= C; B -= A; B ^= (A << 10);
			C -= A; C -= B; C ^= (B >> 15);

			return C;
		}
		#endregion
	}

	internal static class UhtPackageCodeGeneratorStringBuilderExtensions
	{
		public static StringBuilder AppendBeginEditorOnlyGuard(this StringBuilder Builder, bool bEnable = true)
		{
			if (bEnable)
			{
				Builder.Append(UhtPackageCodeGenerator.BeginEditorOnlyGuard);
			}
			return Builder;
		}

		public static StringBuilder AppendEndEditorOnlyGuard(this StringBuilder Builder, bool bEnable = true)
		{
			if (bEnable)
			{
				Builder.Append(UhtPackageCodeGenerator.EndEditorOnlyGuard);
			}
			return Builder;
		}
	}
}
