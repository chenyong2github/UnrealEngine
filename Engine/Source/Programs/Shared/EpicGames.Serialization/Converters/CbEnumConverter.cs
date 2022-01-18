// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Linq.Expressions;
using System.Reflection;
using System.Reflection.Emit;
using System.Text;

namespace EpicGames.Serialization.Converters
{
	class CbEnumConverter<T> : CbConverterBase<T>, ICbConverterMethods where T : Enum
	{
		Func<CbField, T> ReadFunc;
		Action<CbWriter, T> WriteFunc;
		Action<CbWriter, Utf8String, T> WriteNamedFunc;

		public CbEnumConverter()
		{
			Type Type = typeof(T);

			ReadMethod = new DynamicMethod($"Read_{Type.Name}", Type, new Type[] { typeof(CbField) });
			CreateEnumReader(Type, ReadMethod.GetILGenerator());
			ReadFunc = (Func<CbField, T>)ReadMethod.CreateDelegate(typeof(Func<CbField, T>));

			WriteMethod = new DynamicMethod($"Write_{Type.Name}", null, new Type[] { typeof(CbWriter), Type });
			CreateEnumWriter(Type, WriteMethod.GetILGenerator());
			WriteFunc = (Action<CbWriter, T>)WriteMethod.CreateDelegate(typeof(Action<CbWriter, T>));

			WriteNamedMethod = new DynamicMethod($"WriteNamed_{Type.Name}", null, new Type[] { typeof(CbWriter), typeof(Utf8String), Type });
			CreateNamedEnumWriter(Type, WriteNamedMethod.GetILGenerator());
			WriteNamedFunc = (Action<CbWriter, Utf8String, T>)WriteNamedMethod.CreateDelegate(typeof(Action<CbWriter, Utf8String, T>));
		}

		public DynamicMethod ReadMethod { get; }
		MethodInfo ICbConverterMethods.ReadMethod => ReadMethod;

		public DynamicMethod WriteMethod { get; }
		MethodInfo ICbConverterMethods.WriteMethod => WriteMethod;

		public DynamicMethod WriteNamedMethod { get; }
		MethodInfo ICbConverterMethods.WriteNamedMethod => WriteNamedMethod;

		public override T Read(CbField Field) => ReadFunc(Field);

		public override void Write(CbWriter Writer, T Value) => WriteFunc(Writer, Value);

		public override void WriteNamed(CbWriter Writer, Utf8String Name, T Value) => WriteNamedFunc(Writer, Name, Value);

		static void CreateEnumReader(Type Type, ILGenerator Generator)
		{
			Generator.Emit(OpCodes.Ldarg_0);
			Generator.EmitCall(OpCodes.Call, GetMethodInfo<CbField>(x => x.AsInt32()), null);
			Generator.Emit(OpCodes.Ret);
		}

		static void CreateEnumWriter(Type Type, ILGenerator Generator)
		{
			Generator.Emit(OpCodes.Ldarg_1);

			Label SkipLabel = Generator.DefineLabel();
			Generator.Emit(OpCodes.Brfalse, SkipLabel);

			Generator.Emit(OpCodes.Ldarg_0);
			Generator.Emit(OpCodes.Ldarg_1);
			Generator.Emit(OpCodes.Conv_I8);
			Generator.EmitCall(OpCodes.Call, GetMethodInfo<CbWriter>(x => x.WriteIntegerValue(0L)), null);

			Generator.MarkLabel(SkipLabel);
			Generator.Emit(OpCodes.Ret);
		}

		static void CreateNamedEnumWriter(Type Type, ILGenerator Generator)
		{
			Generator.Emit(OpCodes.Ldarg_2);

			Label SkipLabel = Generator.DefineLabel();
			Generator.Emit(OpCodes.Brfalse, SkipLabel);

			Generator.Emit(OpCodes.Ldarg_0);
			Generator.Emit(OpCodes.Ldarg_1);
			Generator.Emit(OpCodes.Ldarg_2);
			Generator.Emit(OpCodes.Conv_I8);
			Generator.EmitCall(OpCodes.Call, GetMethodInfo<CbWriter>(x => x.WriteInteger(default, 0L)), null);

			Generator.MarkLabel(SkipLabel);
			Generator.Emit(OpCodes.Ret);
		}

		static MethodInfo GetMethodInfo<TArg>(Expression<Action<TArg>> Expr)
		{
			return ((MethodCallExpression)Expr.Body).Method;
		}
	}

	class CbEnumConverterFactory : CbConverterFactory
	{
		public override ICbConverter? CreateConverter(Type Type)
		{
			ICbConverter? Converter = null;
			if (Type.IsEnum)
			{
				Type ConverterType = typeof(CbEnumConverter<>).MakeGenericType(Type);
				Converter = (ICbConverter?)Activator.CreateInstance(ConverterType);
			}
			return Converter;
		}
	}
}
