// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System;
using System.Text;

namespace EpicGames.UHT.Types
{
	[UnrealHeaderTool]
	public class UhtVoidProperty : UhtProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "UHTVoidProperty"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "void"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => "invalid"; }

		public UhtVoidProperty(UhtPropertySettings PropertySettings) : base(PropertySettings)
		{
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public override StringBuilder AppendFunctionThunkParameterGet(StringBuilder Builder)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public override StringBuilder AppendNullConstructorArg(StringBuilder Builder, bool bIsInitializer)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public override bool SanitizeDefaultValue(IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			throw new NotImplementedException();
		}

		/// <inheritdoc/>
		public override bool IsSameType(UhtProperty Other)
		{
			return Other is UhtVoidProperty;
		}

		/// <inheritdoc/>
		public override string? GetRigVMType(ref UhtRigVMParameterFlags ParameterFlags)
		{
			throw new NotImplementedException();
		}

		#region Keyword
		[UhtPropertyType(Keyword = "void", Options = UhtPropertyTypeOptions.Simple | UhtPropertyTypeOptions.Immediate)]
		public static UhtProperty? VoidProperty(UhtPropertyResolvePhase ResolvePhase, UhtPropertySettings PropertySettings, IUhtTokenReader TokenReader, UhtToken MatchedToken)
		{
			if (PropertySettings.PropertyCategory != UhtPropertyCategory.Return)
			{
				TokenReader.LogError("void type is only valid as a return type");
				return null;
			}
			return new UhtVoidProperty(PropertySettings);
		}
		#endregion
	}
}
