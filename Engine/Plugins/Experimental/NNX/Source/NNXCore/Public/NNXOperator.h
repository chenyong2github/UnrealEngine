// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

//enum class EMLBatchNormalizationMode : uint8
//{
//	OneDimension,
//	NDimensions,
//	
//	MAX
//};

//enum class EMLConvolutionMode : uint8
//{
//	D1, /** 1D convolution */
//	D2, /** 2D convolution */
//	D3, /** 3D convolution */
//	DN, /** nD convolution */
//	
//	MAX
//};

//enum class EMLGemmMode : uint8
//{
//	CTensor,
//	CScalar,
//	
//	MAX
//};

//One input element-wise operators
enum class EMLElementWiseUnaryOperatorType : uint8
{
	Abs,
	Acos,
	Acosh,
	Asin,
	Asinh,
	Atan,
	Atanh,
	//BitShift, //TODO need integer tensors
	//Cast,     //TODO ability to cast tensor type
	Ceil,
	//Clip,     //TODO need scalar tensor inputs
	Cos,
	Cosh,
	Elu,
	Erf,
	Exp,
	Floor,
	IsInf,
	IsNan,
	HardSigmoid,
	HardSwish,
	LeakyRelu,
	Log,
	Neg,
	//Not,      //TODO need bool tensors
	Reciprocal,
	Relu,
	Round,
	Selu,
	Sigmoid,
	Sign,
	Sin,
	Sinh,
	Softplus,
	Softsign,
	Sqrt,
	Tan,
	Tanh,
	
	MAX
};

//Two inputs element-wise operators with multi-directional broadcast
//see https://github.com/onnx/onnx/blob/main/docs/Broadcasting.md
enum class EMLElementWiseBinaryOperatorType : uint8
{
	Add,
	//And,           //TODO need boolean tensors
	Div,
	//Equal,         //TODO need boolean tensors
	//Greater,       //TODO need boolean tensors
	//GreaterOrEqual,//TODO need boolean tensors
	//Less,          //TODO need boolean tensors
	//LessOrEqual,   //TODO need boolean tensors
	Mod,
	Mul,
	//Or,            //TODO need boolean tensors
	Prelu,           //Note: only broadcast from slope to input.
	Pow,
	Sub,
	//Xor,           //TODO need boolean tensors
	
	MAX
};

//Variable number of inputs element-wise operators with multi-directional broadcast
//see https://github.com/onnx/onnx/blob/main/docs/Broadcasting.md
enum class EMLElementWiseVariadicOperatorType : uint8
{
	Max,
	Min,
	Mean,
	Sum,

	MAX
};

//enum class EMLMultidirectionalBroadcastOperator : uint8
//{
//	Add,
//	Div,
//	Mul,
//	Pow,
//	Sub,
//
//	MAX
//};

//enum class EMLMultidirectionalBroadcastInlinedMode : uint8
//{
//	A,
//	B,
//	None,
//	MAX
//};

//enum class EMLMultidirectionalBroadcastShapeMode : uint8
//{
//	ElementWise,
//	AScalar,
//	BScalar,
//	MultidirectionalBroadcast,
//	MAX
//};
