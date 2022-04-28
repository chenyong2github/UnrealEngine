// Copyright Epic Games, Inc. All Rights Reserved.

using System.Text;

namespace EpicGames.UHT.Types
{

	/// <summary>
	/// Base class for all numeric properties
	/// </summary>
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

		/// <summary>
		/// Construct new property
		/// </summary>
		/// <param name="PropertySettings">Property settings</param>
		/// <param name="IntType">Type of integer</param>
		protected UhtNumericProperty(UhtPropertySettings PropertySettings, UhtPropertyIntType IntType) : base(PropertySettings)
		{
			this.IntType = IntType;
			this.PropertyCaps |= UhtPropertyCaps.RequiresNullConstructorArg | UhtPropertyCaps.SupportsRigVM;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendNullConstructorArg(StringBuilder Builder, bool bIsInitializer)
		{
			Builder.Append('0');
			return Builder;
		}
	}
}
