// Copyright Epic Games, Inc. All Rights Reserved.

using System.Text;

namespace EpicGames.UHT.Types
{
	public abstract class UhtNumericProperty : UhtProperty
	{
		/// <inheritdoc/>
		protected override string PGetMacroText { get => "PROPERTY"; }

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument { get => UhtPGetArgumentType.EngineClass; }

		/// <summary>
		/// Describes the integer as either being sized on unsized
		/// </summary>
		public readonly UhtPropertyIntType IntType;

		protected UhtNumericProperty(UhtPropertySettings PropertySettings, UhtPropertyIntType IntType) : base(PropertySettings)
		{
			this.IntType = IntType;
			this.PropertyCaps |= UhtPropertyCaps.RequiresNullConstructorArg;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendNullConstructorArg(StringBuilder Builder, bool bIsInitializer)
		{
			Builder.Append("0");
			return Builder;
		}

		/// <inheritdoc/>
		public override string? GetRigVMType(ref UhtRigVMParameterFlags ParameterFlags)
		{
			return CppTypeText;
		}
	}
}
