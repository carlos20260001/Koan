/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


/*!	Operators that move the numbers of a tensor around and leave them
	alone: another shape, tensors joined, a border added, the dimensions in
	another order, a part cut out. They work on elements of any type, as
	bytes. (Joining unsigned 8 bit tensors that are quantized differently is
	the exception: those are brought to the scale of the output.)
*/


// Where real numbers are rounded to stored values the steps must not be
// merged into multiply-add instructions: the results would differ between
// processors.
#pragma GCC optimize("O3", "fp-contract=off")


#include "Kernels.h"

#include <stddef.h>


namespace BPrivate {


namespace {


// #pragma mark - RESHAPE, SQUEEZE


static status_t
prepare_copy(const NeuralGraphData* graph, const neural_graph_operator* op,
	void** _prepared)
{
	if (op->input_count < 1 || op->inputs[0] < 0 || op->output_count != 1)
		return B_NEURAL_UNSUPPORTED_OPERATOR;

	const neural_graph_tensor& input = graph->Tensor(op->inputs[0]);
	const neural_graph_tensor& output = graph->Tensor(op->outputs[0]);
	if (input.type != output.type || input.size != output.size)
		return B_NEURAL_UNSUPPORTED_OPERATOR;

	return B_OK;
}


static status_t
run_copy(const NeuralOpContext& context)
{
	memcpy(context.Output<uint8>(0), context.Input<uint8>(0),
		context.OutputInfo(0).size);
	return B_OK;
}


// #pragma mark - CONCATENATION


struct ConcatData {
	int32	outer_count;
	//! per input: bytes it gives to each of the outer_count stretches
	size_t*	slice_sizes;
	/*!	per input: NULL, or the table that takes its values to the
		quantization of the output */
	uint8**	tables;
};


static status_t
prepare_concat(const NeuralGraphData* graph, const neural_graph_operator* op,
	void** _prepared)
{
	if (op->input_count < 1 || !neural_has_inputs(op, op->input_count)
		|| op->output_count != 1 || op->params == NULL) {
		return B_NEURAL_UNSUPPORTED_OPERATOR;
	}

	const neural_concat_params& params
		= *(const neural_concat_params*)op->params;
	const neural_graph_tensor& output = graph->Tensor(op->outputs[0]);
	int32 rank = output.rank;
	int32 axis = params.axis < 0 ? params.axis + rank : params.axis;
	if (axis < 0 || axis >= rank
		|| params.activation != B_NEURAL_ACTIVATION_NONE
		|| neural_element_count(output) < 0) {
		return B_NEURAL_UNSUPPORTED_OPERATOR;
	}

	// All inputs have the shape of the output but along the axis, where
	// their sizes add up to its size.
	int64 axisSize = 0;
	int32 tableCount = 0;
	for (int32 i = 0; i < op->input_count; i++) {
		const neural_graph_tensor& input = graph->Tensor(op->inputs[i]);
		if (input.type != output.type || input.rank != rank)
			return B_NEURAL_UNSUPPORTED_OPERATOR;
		for (int32 j = 0; j < rank; j++) {
			if (j != axis && input.dimensions[j] != output.dimensions[j])
				return B_NEURAL_UNSUPPORTED_OPERATOR;
		}
		axisSize += input.dimensions[axis];

		bool sameQuantization = input.quantized == output.quantized
			&& (!input.quantized || (input.scale == output.scale
				&& input.zero_point == output.zero_point));
		if (sameQuantization)
			continue;

		// Only unsigned tensors are rescaled (as the reference has it);
		// others must agree with the output.
		if (output.type != B_NEURAL_UINT8 || !neural_is_quantized_8(input)
			|| !neural_is_quantized_8(output)) {
			return B_NEURAL_UNSUPPORTED_OPERATOR;
		}
		tableCount++;
	}
	if (axisSize != output.dimensions[axis])
		return B_NEURAL_UNSUPPORTED_OPERATOR;

	NeuralBlock block(sizeof(ConcatData));
	size_t sizesOffset = block.Reserve(op->input_count, sizeof(size_t));
	size_t tablesOffset = block.Reserve(op->input_count, sizeof(uint8*));
	size_t tableOffset = block.Reserve(tableCount, 256);
	ConcatData* data = (ConcatData*)block.Allocate();
	if (data == NULL)
		return B_NO_MEMORY;

	data->slice_sizes = block.At<size_t>(sizesOffset);
	data->tables = block.At<uint8*>(tablesOffset);
	uint8* table = block.At<uint8>(tableOffset);

	data->outer_count = 1;
	for (int32 i = 0; i < axis; i++)
		data->outer_count *= output.dimensions[i];
	size_t innerSize = NeuralGraphData::TypeSize(output.type);
	for (int32 i = axis + 1; i < rank; i++)
		innerSize *= output.dimensions[i];

	for (int32 i = 0; i < op->input_count; i++) {
		const neural_graph_tensor& input = graph->Tensor(op->inputs[i]);
		data->slice_sizes[i] = input.dimensions[axis] * innerSize;

		bool sameQuantization = input.quantized == output.quantized
			&& (!input.quantized || (input.scale == output.scale
				&& input.zero_point == output.zero_point));
		if (sameQuantization)
			continue;

		// in single precision, step by step as the reference does it
		float inverseScale = 1.0f / output.scale;
		float scale = input.scale * inverseScale;
		float bias = -input.zero_point * scale;
		for (int32 stored = 0; stored < 256; stored++) {
			float real = roundf(stored * scale + bias);
			// compared before it is made an integer: it may be anything
			float value = real + output.zero_point;
			table[stored] = !(value > 0) ? 0
				: (value > 255 ? 255 : (uint8)((int32)real
					+ output.zero_point));
		}
		data->tables[i] = table;
		table += 256;
	}

	*_prepared = data;
	return B_OK;
}


static status_t
run_concat(const NeuralOpContext& context)
{
	const ConcatData* data = (const ConcatData*)context.prepared;
	uint8* output = context.Output<uint8>(0);
	int32 inputCount = context.op->input_count;

	for (int32 outer = 0; outer < data->outer_count; outer++) {
		for (int32 i = 0; i < inputCount; i++) {
			size_t size = data->slice_sizes[i];
			const uint8* input = context.Input<uint8>(i) + outer * size;
			const uint8* table = data->tables[i];
			if (table == NULL) {
				memcpy(output, input, size);
			} else {
				for (size_t j = 0; j < size; j++)
					output[j] = table[input[j]];
			}
			output += size;
		}
	}
	return B_OK;
}


// #pragma mark - PAD


struct PadData {
	int32	rank;
	int32	input_size[B_NEURAL_MAX_RANK];
	//! elements between neighbours along each dimension of the output
	int32	output_step[B_NEURAL_MAX_RANK];
	//! where the first element of the input goes in the output
	int32	first;
	int32	output_elements;
	int32	element_size;
	//! what the border is made of when there is no third input
	uint8	value[8];
};


static status_t
prepare_pad(const NeuralGraphData* graph, const neural_graph_operator* op,
	void** _prepared)
{
	if (!neural_has_inputs(op, 2) || op->input_count > 3
		|| op->output_count != 1) {
		return B_NEURAL_UNSUPPORTED_OPERATOR;
	}

	const neural_graph_tensor& input = graph->Tensor(op->inputs[0]);
	const neural_graph_tensor& paddings = graph->Tensor(op->inputs[1]);
	const neural_graph_tensor& output = graph->Tensor(op->outputs[0]);
	int32 rank = input.rank;

	if (input.type != output.type || output.rank != rank
		|| neural_element_count(input) < 0
		|| neural_element_count(output) < 0
		|| !neural_is_constant(paddings)
		|| !neural_is_index_type(paddings.type)
		|| neural_element_count(paddings) != 2 * rank) {
		return B_NEURAL_UNSUPPORTED_OPERATOR;
	}

	PadData header;
	memset(&header, 0, sizeof(header));
	header.rank = rank;
	header.element_size = NeuralGraphData::TypeSize(input.type);
	header.output_elements = neural_element_count(output);

	int32 step = 1;
	for (int32 i = rank - 1; i >= 0; i--) {
		int64 before = neural_constant_int(paddings, 2 * i);
		int64 after = neural_constant_int(paddings, 2 * i + 1);
		if (before < 0 || after < 0
			|| input.dimensions[i] + before + after
				!= output.dimensions[i]) {
			return B_NEURAL_UNSUPPORTED_OPERATOR;
		}

		header.input_size[i] = input.dimensions[i];
		header.output_step[i] = step;
		header.first += (int32)before * step;
		step *= output.dimensions[i];
	}

	if (neural_has_input(op, 2)) {
		// one value of the type of the input
		const neural_graph_tensor& value = graph->Tensor(op->inputs[2]);
		if (value.type != input.type || neural_element_count(value) != 1)
			return B_NEURAL_UNSUPPORTED_OPERATOR;
	} else if (output.quantized && (output.type == B_NEURAL_UINT8
			|| output.type == B_NEURAL_INT8
			|| output.type == B_NEURAL_INT16)) {
		// a quantized border is the real number 0
		if (!neural_valid_zero_point(output))
			return B_NEURAL_UNSUPPORTED_OPERATOR;
		if (output.type == B_NEURAL_INT16) {
			int16 zero = (int16)output.zero_point;
			memcpy(header.value, &zero, sizeof(zero));
		} else
			header.value[0] = (uint8)output.zero_point;
	}

	PadData* data = (PadData*)malloc(sizeof(PadData));
	if (data == NULL)
		return B_NO_MEMORY;
	*data = header;
	*_prepared = data;
	return B_OK;
}


static status_t
run_pad(const NeuralOpContext& context)
{
	const PadData* data = (const PadData*)context.prepared;
	const uint8* input = context.Input<uint8>(0);
	uint8* output = context.Output<uint8>(0);
	size_t elementSize = data->element_size;
	const uint8* value = context.HasInput(2)
		? context.Input<uint8>(2) : data->value;

	// the border everywhere, then the input over it row by row
	if (elementSize == 1) {
		memset(output, value[0], data->output_elements);
	} else {
		for (int32 i = 0; i < data->output_elements; i++)
			memcpy(output + i * elementSize, value, elementSize);
	}

	int32 rank = data->rank;
	if (rank == 0) {
		memcpy(output, input, elementSize);
		return B_OK;
	}

	int32 index[B_NEURAL_MAX_RANK] = {};
	size_t rowSize = data->input_size[rank - 1] * elementSize;
	size_t offset = data->first;
	while (true) {
		memcpy(output + offset * elementSize, input, rowSize);
		input += rowSize;

		// on to the next row: count up the dimensions before the last
		int32 dimension = rank - 2;
		for (; dimension >= 0; dimension--) {
			offset += data->output_step[dimension];
			if (++index[dimension] < data->input_size[dimension])
				break;
			offset -= (size_t)index[dimension] * data->output_step[dimension];
			index[dimension] = 0;
		}
		if (dimension < 0)
			break;
	}
	return B_OK;
}


// #pragma mark - TRANSPOSE, STRIDED_SLICE


/*!	Both pick elements of the input at regular steps: the element of the
	output at an index comes from \c first plus the sum of index times
	step over the dimensions. */
struct GatherData {
	int32	rank;
	int32	size[B_NEURAL_MAX_RANK];
	//! in elements of the input; may be negative
	int32	step[B_NEURAL_MAX_RANK];
	int32	first;
	int32	element_size;
};


template<typename Type>
static inline void
gather_row(const Type* input, ptrdiff_t step, Type* output, int32 count)
{
	for (int32 i = 0; i < count; i++)
		output[i] = input[i * step];
}


template<typename Type>
static void
gather(const GatherData& data, const Type* input, Type* output)
{
	int32 rank = data.rank;
	int32 index[B_NEURAL_MAX_RANK] = {};
	int32 count = data.size[rank - 1];
	ptrdiff_t step = data.step[rank - 1];
	ptrdiff_t offset = data.first;

	while (true) {
		gather_row(input + offset, step, output, count);
		output += count;

		int32 dimension = rank - 2;
		for (; dimension >= 0; dimension--) {
			offset += data.step[dimension];
			if (++index[dimension] < data.size[dimension])
				break;
			offset -= (ptrdiff_t)index[dimension] * data.step[dimension];
			index[dimension] = 0;
		}
		if (dimension < 0)
			break;
	}
}


static status_t
run_gather(const NeuralOpContext& context)
{
	const GatherData& data = *(const GatherData*)context.prepared;
	switch (data.element_size) {
		case 1:
			gather(data, context.Input<uint8>(0), context.Output<uint8>(0));
			break;
		case 2:
			gather(data, context.Input<uint16>(0),
				context.Output<uint16>(0));
			break;
		case 4:
			gather(data, context.Input<uint32>(0),
				context.Output<uint32>(0));
			break;
		default:
			gather(data, context.Input<uint64>(0),
				context.Output<uint64>(0));
			break;
	}
	return B_OK;
}


static status_t
finish_gather(const GatherData& header, void** _prepared)
{
	GatherData* data = (GatherData*)malloc(sizeof(GatherData));
	if (data == NULL)
		return B_NO_MEMORY;
	*data = header;

	// a scalar is a row of one
	if (data->rank == 0) {
		data->rank = 1;
		data->size[0] = 1;
		data->step[0] = 0;
	}

	*_prepared = data;
	return B_OK;
}


static status_t
prepare_transpose(const NeuralGraphData* graph,
	const neural_graph_operator* op, void** _prepared)
{
	if (op->input_count != 2 || !neural_has_inputs(op, 2)
		|| op->output_count != 1) {
		return B_NEURAL_UNSUPPORTED_OPERATOR;
	}

	const neural_graph_tensor& input = graph->Tensor(op->inputs[0]);
	const neural_graph_tensor& order = graph->Tensor(op->inputs[1]);
	const neural_graph_tensor& output = graph->Tensor(op->outputs[0]);
	int32 rank = input.rank;

	if (input.type != output.type || output.rank != rank
		|| neural_element_count(input) < 0
		|| !neural_is_constant(order) || !neural_is_index_type(order.type)
		|| neural_element_count(order) != rank) {
		return B_NEURAL_UNSUPPORTED_OPERATOR;
	}

	int32 inputStep[B_NEURAL_MAX_RANK];
	int32 step = 1;
	for (int32 i = rank - 1; i >= 0; i--) {
		inputStep[i] = step;
		step *= input.dimensions[i];
	}

	// each dimension of the input exactly once
	GatherData header;
	memset(&header, 0, sizeof(header));
	header.rank = rank;
	header.element_size = NeuralGraphData::TypeSize(input.type);
	uint32 seen = 0;
	for (int32 i = 0; i < rank; i++) {
		int64 source = neural_constant_int(order, i);
		if (source < 0)
			source += rank;
		if (source < 0 || source >= rank || (seen & (1 << source)) != 0
			|| output.dimensions[i] != input.dimensions[source]) {
			return B_NEURAL_UNSUPPORTED_OPERATOR;
		}
		seen |= 1 << source;
		header.size[i] = output.dimensions[i];
		header.step[i] = inputStep[source];
	}

	return finish_gather(header, _prepared);
}


static status_t
prepare_strided_slice(const NeuralGraphData* graph,
	const neural_graph_operator* op, void** _prepared)
{
	if (op->input_count != 4 || !neural_has_inputs(op, 4)
		|| op->output_count != 1 || op->params == NULL) {
		return B_NEURAL_UNSUPPORTED_OPERATOR;
	}

	const neural_strided_slice_params& params
		= *(const neural_strided_slice_params*)op->params;
	const neural_graph_tensor& input = graph->Tensor(op->inputs[0]);
	const neural_graph_tensor& begin = graph->Tensor(op->inputs[1]);
	const neural_graph_tensor& end = graph->Tensor(op->inputs[2]);
	const neural_graph_tensor& strides = graph->Tensor(op->inputs[3]);
	const neural_graph_tensor& output = graph->Tensor(op->outputs[0]);
	int32 rank = input.rank;

	// Slices with an ellipsis or with new dimensions are not implemented.
	int32 count = neural_element_count(begin);
	if (input.type != output.type || neural_element_count(input) < 0
		|| params.ellipsis_mask != 0 || params.new_axis_mask != 0
		|| !neural_is_constant(begin) || !neural_is_constant(end)
		|| !neural_is_constant(strides)
		|| !neural_is_index_type(begin.type)
		|| !neural_is_index_type(end.type)
		|| !neural_is_index_type(strides.type)
		|| begin.rank != 1 || count > rank
		|| neural_element_count(end) != count
		|| neural_element_count(strides) != count) {
		return B_NEURAL_UNSUPPORTED_OPERATOR;
	}

	GatherData header;
	memset(&header, 0, sizeof(header));
	header.rank = rank;
	header.element_size = NeuralGraphData::TypeSize(input.type);

	int32 inputStep = 1;
	int32 outputRank = 0;
	int32 outputSize[B_NEURAL_MAX_RANK];
	for (int32 i = rank - 1; i >= 0; i--) {
		int64 size = input.dimensions[i];
		int64 first = 0;
		int64 last = size;
		int64 stride = 1;
		bool shrink = false;

		if (i < count) {
			stride = neural_constant_int(strides, i);
			if (stride == 0 || stride > INT_MAX || stride < -INT_MAX)
				return B_NEURAL_UNSUPPORTED_OPERATOR;

			// Indices count from the end when negative and are clipped
			// to the dimension; a mask bit means "from the very start"
			// or "to the very end" in the direction of the stride.
			first = neural_constant_int(begin, i);
			if ((params.begin_mask & (1 << i)) != 0)
				first = stride > 0 ? 0 : size - 1;
			else if (first < 0)
				first += size;
			if (stride > 0)
				first = first < 0 ? 0 : (first > size ? size : first);
			else
				first = first < -1 ? -1 : (first > size - 1 ? size - 1 : first);

			shrink = (params.shrink_axis_mask & (1 << i)) != 0;
			if (shrink) {
				// one element, and the dimension goes
				if (first < 0 || first >= size)
					return B_NEURAL_UNSUPPORTED_OPERATOR;
				last = first + 1;
				stride = 1;
			} else {
				last = neural_constant_int(end, i);
				if ((params.end_mask & (1 << i)) != 0)
					last = stride > 0 ? size : -1;
				else if (last < 0)
					last += size;
				if (stride > 0)
					last = last < 0 ? 0 : (last > size ? size : last);
				else
					last = last < -1 ? -1 : (last > size - 1 ? size - 1 : last);
			}
		}

		int64 elements = stride > 0 ? (last - first + stride - 1) / stride
			: (first - last - stride - 1) / -stride;
		if (elements <= 0)
			return B_NEURAL_UNSUPPORTED_OPERATOR;

		header.size[i] = (int32)elements;
		header.step[i] = (int32)(stride * inputStep);
		header.first += (int32)(first * inputStep);
		if (!shrink)
			outputSize[outputRank++] = (int32)elements;
		inputStep *= (int32)size;
	}

	// what is left must be the shape the output has (collected back to
	// front above)
	if (output.rank != outputRank)
		return B_NEURAL_UNSUPPORTED_OPERATOR;
	for (int32 i = 0; i < outputRank; i++) {
		if (output.dimensions[i] != outputSize[outputRank - 1 - i])
			return B_NEURAL_UNSUPPORTED_OPERATOR;
	}

	return finish_gather(header, _prepared);
}


}	// unnamed namespace


const NeuralKernel kReshapeKernel = {
	B_NEURAL_OP_RESHAPE, &prepare_copy, &run_copy
};

const NeuralKernel kSqueezeKernel = {
	B_NEURAL_OP_SQUEEZE, &prepare_copy, &run_copy
};

const NeuralKernel kConcatenationKernel = {
	B_NEURAL_OP_CONCATENATION, &prepare_concat, &run_concat
};

const NeuralKernel kPadKernel = {
	B_NEURAL_OP_PAD, &prepare_pad, &run_pad
};

const NeuralKernel kTransposeKernel = {
	B_NEURAL_OP_TRANSPOSE, &prepare_transpose, &run_gather
};

const NeuralKernel kStridedSliceKernel = {
	B_NEURAL_OP_STRIDED_SLICE, &prepare_strided_slice, &run_gather
};


}	// namespace BPrivate
