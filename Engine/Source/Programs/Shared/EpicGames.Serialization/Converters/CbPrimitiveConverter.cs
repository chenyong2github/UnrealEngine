// Copyright Epic Games, Inc. All Rights Reserved.

using EpicGames.Core;
using System;
using System.Collections.Generic;
using System.Linq.Expressions;
using System.Reflection;
using System.Text;

namespace EpicGames.Serialization.Converters
{
	class CbPrimitiveConverter<T> : CbConverterBase<T>, ICbConverterMethods
	{
		public MethodInfo ReadMethod { get; }
		public Func<CbField, T> ReadFunc { get; }

		public MethodInfo WriteMethod { get; }
		public Action<CbWriter, T> WriteFunc { get; }

		public MethodInfo WriteNamedMethod { get; }
		public Action<CbWriter, Utf8String, T> WriteNamedFunc { get; }

		public CbPrimitiveConverter(Expression<Func<CbField, T>> Read, Expression<Action<CbWriter, T>> Write, Expression<Action<CbWriter, Utf8String, T>> WriteNamed)
		{
			this.ReadMethod = ((MethodCallExpression)Read.Body).Method;
			this.ReadFunc = Read.Compile();

			this.WriteMethod = ((MethodCallExpression)Write.Body).Method;
			this.WriteFunc = Write.Compile();

			this.WriteNamedMethod = ((MethodCallExpression)WriteNamed.Body).Method;
			this.WriteNamedFunc = WriteNamed.Compile();
		}

		public override T Read(CbField Field) => ReadFunc(Field);

		public override void Write(CbWriter Writer, T Value) => WriteFunc(Writer, Value);

		public override void WriteNamed(CbWriter Writer, Utf8String Name, T Value) => WriteNamedFunc(Writer, Name, Value);
	}
}
