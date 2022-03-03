// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using EpicGames.UHT.Tables;
using EpicGames.UHT.Tokenizer;
using EpicGames.UHT.Utils;
using System.Collections.Generic;
using System.Text;
using System.Text.Json.Serialization;

namespace EpicGames.UHT.Types
{
	[UnrealHeaderTool]
	[UhtEngineClass(Name = "StructProperty", IsProperty = true)]
	public class UhtStructProperty : UhtProperty
	{
		/// <inheritdoc/>
		public override string EngineClassName { get => "StructProperty"; }

		/// <inheritdoc/>
		protected override string CppTypeText { get => "invalid"; }

		/// <inheritdoc/>
		protected override string PGetMacroText { get => "STRUCT"; }

		/// <inheritdoc/>
		protected override UhtPGetArgumentType PGetTypeArgument { get => UhtPGetArgumentType.TypeText; }

		[JsonConverter(typeof(UhtTypeSourceNameJsonConverter<UhtScriptStruct>))]
		public UhtScriptStruct ScriptStruct { get; set; }

		public UhtStructProperty(UhtPropertySettings PropertySettings, UhtScriptStruct ScriptStruct) : base(PropertySettings)
		{
			this.ScriptStruct = ScriptStruct;
			this.HeaderFile.AddReferencedHeader(ScriptStruct);
			if (this.ScriptStruct.bHasNoOpConstructor)
			{
				this.PropertyCaps |= UhtPropertyCaps.RequiresNullConstructorArg;
			}
			if (this.ScriptStruct.MetaData.GetBoolean(UhtNames.BlueprintType))
			{
				this.PropertyCaps |= UhtPropertyCaps.CanExposeOnSpawn | UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint;
			}
			else if (this.ScriptStruct.MetaData.GetBooleanHierarchical(UhtNames.BlueprintType))
			{
				this.PropertyCaps |= UhtPropertyCaps.IsParameterSupportedByBlueprint | UhtPropertyCaps.IsMemberSupportedByBlueprint;
			}
		}

		/// <inheritdoc/>
		protected override bool ResolveSelf(UhtResolvePhase Phase)
		{
			bool bResults = base.ResolveSelf(Phase);
			switch (Phase)
			{
				case UhtResolvePhase.Final:
					if (ScanForInstancedReferenced(true))
					{
						this.PropertyFlags |= EPropertyFlags.ContainsInstancedReference;
					}
					break;
			}
			return bResults;
		}

		/// <inheritdoc/>
		public override bool ScanForInstancedReferenced(bool bDeepScan)
		{
			return this.ScriptStruct.ScanForInstancedReferenced(bDeepScan);
		}

		/// <inheritdoc/>
		public override void CollectReferencesInternal(IUhtReferenceCollector Collector, bool bTemplateProperty)
		{
			Collector.AddCrossModuleReference(this.ScriptStruct, true);
		}

		/// <inheritdoc/>
		public override string? GetForwardDeclarations()
		{
			return !this.ScriptStruct.bIsCoreType ? $"struct {this.ScriptStruct.SourceName};" : null;
		}

		/// <inheritdoc/>
		public override IEnumerable<UhtType> EnumerateReferencedTypes()
		{
			yield return this.ScriptStruct;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendText(StringBuilder Builder, UhtPropertyTextType TextType, bool bIsTemplateArgument)
		{
			switch (TextType)
			{
				default:
					Builder.Append(this.ScriptStruct.SourceName);
					break;
			}
			return Builder;
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDecl(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, int Tabs)
		{
			return AppendMemberDecl(Builder, Context, Name, NameSuffix, Tabs, "FStructPropertyParams");
		}

		/// <inheritdoc/>
		public override StringBuilder AppendMemberDef(StringBuilder Builder, IUhtPropertyMemberContext Context, string Name, string NameSuffix, string? Offset, int Tabs)
		{
			AppendMemberDefStart(Builder, Context, Name, NameSuffix, Offset, Tabs, "FStructPropertyParams", "UECodeGen_Private::EPropertyGenFlags::Struct");
			AppendMemberDefRef(Builder, Context, this.ScriptStruct, true);
			AppendMemberDefEnd(Builder, Context, Name, NameSuffix);
			return Builder;
		}

		/// <inheritdoc/>
		public override void AppendObjectHashes(StringBuilder Builder, int StartingLength, IUhtPropertyMemberContext Context)
		{
			Builder.AppendObjectHash(StartingLength, this, Context, this.ScriptStruct);
		}

		/// <inheritdoc/>
		public override StringBuilder AppendNullConstructorArg(StringBuilder Builder, bool bIsInitializer)
		{
			bool bHasNoOpConstructor = this.ScriptStruct.bHasNoOpConstructor;
			if (bIsInitializer && bHasNoOpConstructor)
			{
				Builder.Append("ForceInit");
			}
			else
			{
				Builder.AppendPropertyText(this, UhtPropertyTextType.Construction);
				if (bHasNoOpConstructor)
				{
					Builder.Append("(ForceInit)");
				}
				else
				{
					Builder.Append("()");
				}
			}
			return Builder;
		}

		/// <inheritdoc/>
		public override bool SanitizeDefaultValue(IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			UhtStructDefaultValue StructDefaultValue;
			if (!UhtStructDefaultValueTable.Instance.TryGet(this.ScriptStruct.SourceName, out StructDefaultValue))
			{
				StructDefaultValue = UhtStructDefaultValueTable.Instance.Default;
			}
			return StructDefaultValue.Delegate(this, DefaultValueReader, InnerDefaultValue);
		}

		/// <inheritdoc/>
		public override bool IsSameType(UhtProperty Other)
		{
			if (Other is UhtStructProperty OtherObject)
			{
				return this.ScriptStruct == OtherObject.ScriptStruct;
			}
			return false;
		}

		/// <inheritdoc/>
		protected override void ValidateFunctionArgument(UhtFunction Function, UhtValidationOptions Options)
		{
			base.ValidateFunctionArgument(Function, Options);

			if (Function.FunctionFlags.HasAnyFlags(EFunctionFlags.Net))
			{
				if (!Function.FunctionFlags.HasAnyFlags(EFunctionFlags.NetRequest | EFunctionFlags.NetResponse))
				{
					this.Session.ValidateScriptStructOkForNet(this, this.ScriptStruct);
				}
			}
		}

		/// <inheritdoc/>
		protected override void ValidateMember(UhtStruct Struct, UhtValidationOptions Options)
		{
			if (this.PropertyFlags.HasAnyFlags(EPropertyFlags.Net))
			{
				this.Session.ValidateScriptStructOkForNet(this, this.ScriptStruct);
			}
		}

		///<inheritdoc/>
		public override bool ValidateStructPropertyOkForNet(UhtProperty ReferencingProperty)
		{
			return ReferencingProperty.Session.ValidateScriptStructOkForNet(ReferencingProperty, this.ScriptStruct);
		}

		/// <inheritdoc/>
		public override string? GetRigVMType(ref UhtRigVMParameterFlags ParameterFlags)
		{
			return this.ScriptStruct.SourceName;
		}

		#region Structure default value sanitizers
		[UhtStructDefaultValue(Name = "FVector")]
		public static bool VectorStructDefaultValue(UhtStructProperty Property, IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			string Format = "{0:F6},{1:F6},{2:F6}";

			DefaultValueReader.Require("FVector");
			if (DefaultValueReader.TryOptional("::"))
			{
				switch (DefaultValueReader.GetIdentifier().Value.ToString())
				{
					case "ZeroVector": return true;
					case "UpVector": InnerDefaultValue.AppendFormat(Format, 0, 0, 1); return true;
					case "ForwardVector": InnerDefaultValue.AppendFormat(Format, 1, 0, 0); return true;
					case "RightVector": InnerDefaultValue.AppendFormat(Format, 0, 1, 0); return true;
					default: return false;
				}
			}
			else
			{
				DefaultValueReader.Require("(");
				if (DefaultValueReader.TryOptional("ForceInit"))
				{
				}
				else
				{
					double X, Y, Z;
					X = Y = Z = DefaultValueReader.GetConstDoubleExpression();
					if (DefaultValueReader.TryOptional(','))
					{
						Y = DefaultValueReader.GetConstDoubleExpression();
						DefaultValueReader.Require(',');
						Z = DefaultValueReader.GetConstDoubleExpression();
					}
					InnerDefaultValue.AppendFormat(Format, X, Y, Z);
				}
				DefaultValueReader.Require(")");
				return true;
			}
		}

		[UhtStructDefaultValue(Name = "FRotator")]
		public static bool RotatorStructDefaultValue(UhtStructProperty Property, IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			DefaultValueReader.Require("FRotator");
			if (DefaultValueReader.TryOptional("::"))
			{
				switch (DefaultValueReader.GetIdentifier().Value.ToString())
				{
					case "ZeroRotator": return true;
					default: return false;
				}
			}
			else
			{
				DefaultValueReader.Require("(");
				if (DefaultValueReader.TryOptional("ForceInit"))
				{
				}
				else
				{
					double X = DefaultValueReader.GetConstDoubleExpression();
					DefaultValueReader.Require(',');
					double Y = DefaultValueReader.GetConstDoubleExpression();
					DefaultValueReader.Require(',');
					double Z = DefaultValueReader.GetConstDoubleExpression();
					InnerDefaultValue.AppendFormat("{0:F6},{1:F6},{2:F6}", X, Y, Z);
				}
				DefaultValueReader.Require(")");
				return true;
			}
		}

		[UhtStructDefaultValue(Name = "FVector2D")]
		public static bool Vector2DStructDefaultValue(UhtStructProperty Property, IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			string Format = "(X={0:F3},Y={1:F3})";

			DefaultValueReader.Require("FVector2D");
			if (DefaultValueReader.TryOptional("::"))
			{
				switch (DefaultValueReader.GetIdentifier().Value.ToString())
				{
					case "ZeroVector": return true;
					case "UnitVector": InnerDefaultValue.AppendFormat(Format, 1.0, 1.0); return true;
					default: return false;
				}
			}
			else
			{
				DefaultValueReader.Require("(");
				if (DefaultValueReader.TryOptional("ForceInit"))
				{
				}
				else
				{
					double X = DefaultValueReader.GetConstDoubleExpression();
					DefaultValueReader.Require(',');
					double Y = DefaultValueReader.GetConstDoubleExpression();
					InnerDefaultValue.AppendFormat(Format, X, Y);
				}
				DefaultValueReader.Require(")");
				return true;
			}
		}

		[UhtStructDefaultValue(Name = "FLinearColor")]
		public static bool LinearColorStructDefaultValue(UhtStructProperty Property, IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			string Format = "(R={0:F6},G={1:F6},B={2:F6},A={3:F6})";

			DefaultValueReader.Require("FLinearColor");
			if (DefaultValueReader.TryOptional("::"))
			{
				switch (DefaultValueReader.GetIdentifier().Value.ToString())
				{
					case "White": InnerDefaultValue.AppendFormat(Format, 1.0, 1.0, 1.0, 1.0); ; return true;
					case "Gray": InnerDefaultValue.AppendFormat(Format, 0.5, 0.5, 0.5, 1.0); ; return true;
					case "Black": InnerDefaultValue.AppendFormat(Format, 0.0, 0.0, 0.0, 1.0); ; return true;
					case "Transparent": InnerDefaultValue.AppendFormat(Format, 0.0, 0.0, 0.0, 0.0); ; return true;
					case "Red": InnerDefaultValue.AppendFormat(Format, 1.0, 0.0, 0.0, 1.0); ; return true;
					case "Green": InnerDefaultValue.AppendFormat(Format, 0.0, 1.0, 0.0, 1.0); ; return true;
					case "Blue": InnerDefaultValue.AppendFormat(Format, 0.0, 0.0, 1.0, 1.0); ; return true;
					case "Yellow": InnerDefaultValue.AppendFormat(Format, 1.0, 1.0, 0.0, 1.0); ; return true;
					default: return false;
				}
			}
			else
			{
				DefaultValueReader.Require("(");
				if (DefaultValueReader.TryOptional("ForceInit"))
				{
				}
				else
				{
					double R = DefaultValueReader.GetConstDoubleExpression();
					DefaultValueReader.Require(',');
					double G = DefaultValueReader.GetConstDoubleExpression();
					DefaultValueReader.Require(',');
					double B = DefaultValueReader.GetConstDoubleExpression();
					double A = 1.0;
					if (DefaultValueReader.TryOptional(','))
					{
						A = DefaultValueReader.GetConstDoubleExpression();
					}
					InnerDefaultValue.AppendFormat(Format, R, G, B, A);
				}
				DefaultValueReader.Require(")");
				return true;
			}
		}

		[UhtStructDefaultValue(Name = "FColor")]
		public static bool ColorStructDefaultValue(UhtStructProperty Property, IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			string Format = "(R={0},G={1},B={2},A={3})";

			DefaultValueReader.Require("FLinearColor");
			if (DefaultValueReader.TryOptional("::"))
			{
				switch (DefaultValueReader.GetIdentifier().Value.ToString())
				{
					case "White": InnerDefaultValue.AppendFormat(Format, 255, 255, 255, 255); ; return true;
					case "Black": InnerDefaultValue.AppendFormat(Format, 0, 0, 0, 255); ; return true;
					case "Red": InnerDefaultValue.AppendFormat(Format, 255, 0, 0, 255); ; return true;
					case "Green": InnerDefaultValue.AppendFormat(Format, 0, 255, 0, 255); ; return true;
					case "Blue": InnerDefaultValue.AppendFormat(Format, 0, 0, 255, 255); ; return true;
					case "Yellow": InnerDefaultValue.AppendFormat(Format, 255, 255, 0, 255); ; return true;
					case "Cyan": InnerDefaultValue.AppendFormat(Format, 0, 255, 255, 255); ; return true;
					case "Magenta": InnerDefaultValue.AppendFormat(Format, 255, 0, 255, 255); ; return true;
					default: return false;
				}
			}
			else
			{
				DefaultValueReader.Require("(");
				if (DefaultValueReader.TryOptional("ForceInit"))
				{
				}
				else
				{
					int R = DefaultValueReader.GetConstIntExpression();
					DefaultValueReader.Require(',');
					int G = DefaultValueReader.GetConstIntExpression();
					DefaultValueReader.Require(',');
					int B = DefaultValueReader.GetConstIntExpression();
					int A = 255;
					if (DefaultValueReader.TryOptional(','))
					{
						A = DefaultValueReader.GetConstIntExpression();
					}
					InnerDefaultValue.AppendFormat(Format, R, G, B, A);
				}
				DefaultValueReader.Require(")");
				return true;
			}
		}

		[UhtStructDefaultValue(Options = UhtStructDefaultValueOptions.Default)]
		public static bool DefaultStructDefaultValue(UhtStructProperty Property, IUhtTokenReader DefaultValueReader, StringBuilder InnerDefaultValue)
		{
			DefaultValueReader
				.Require(Property.ScriptStruct.SourceName)
				.Require('(')
				.Require(')');
			InnerDefaultValue.Append("()");
			return true;
		}
		#endregion
	}
}
