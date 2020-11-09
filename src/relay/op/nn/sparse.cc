/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*!
 * \file sparse.cc
 * \brief Property def of nn.sparse_dense operator.
 */

#include <tvm/relay/attrs/nn.h>
#include <tvm/relay/op.h>
#include <tvm/tir/data_layout.h>

#include <vector>

#include "../../transforms/infer_layout_utils.h"

namespace tvm {
namespace relay {

// relay.nn.sparse_dense
TVM_REGISTER_NODE_TYPE(SparseDenseAttrs);

bool SparseDenseRel(const Array<Type>& types, int num_inputs, const Attrs& attrs,
                    const TypeReporter& reporter) {
  ICHECK_EQ(types.size(), 5);
  const auto* param = attrs.as<SparseDenseAttrs>();
  ICHECK(param != nullptr);

  if (param->sparse_data) {
    const auto* weight = types[3].as<TensorTypeNode>();
    const auto* data_data = types[0].as<TensorTypeNode>();
    ICHECK(data_data->shape.size() == 1 || data_data->shape.size() == 3);
    const auto* data_indptr = types[2].as<TensorTypeNode>();
    if (weight == nullptr) return false;

    if (data_data->shape.size() == 1) {
      // CSR case.
      Array<IndexExpr> oshape({data_indptr->shape[0] - 1, weight->shape[0]});
      reporter->Assign(types[4], TensorType(oshape, weight->dtype));
      return true;
    }

    if (data_data->shape.size() == 3) {
      // BSR case.
      Array<IndexExpr> oshape(
          {(data_indptr->shape[0] - 1) * data_data->shape[1], weight->shape[0]});
      reporter->Assign(types[4], TensorType(oshape, weight->dtype));
      return true;
    }
    LOG(FATAL) << "Unknown data ndim for nn.sparse_dense, should be 1 (CSR) or 3 (BSR)";
    return false;

  } else {
    const auto* data = types[0].as<TensorTypeNode>();
    const auto* weight_data = types[1].as<TensorTypeNode>();
    ICHECK(weight_data->shape.size() == 1 || weight_data->shape.size() == 3);
    const auto* weight_indptr = types[3].as<TensorTypeNode>();
    if (data == nullptr) return false;

    if (weight_data->shape.size() == 1) {
      // CSR case.
      Array<IndexExpr> oshape({data->shape[0], weight_indptr->shape[0] - 1});
      reporter->Assign(types[4], TensorType(oshape, data->dtype));
      return true;
    }

    if (weight_data->shape.size() == 3) {
      // BSR case.
      Array<IndexExpr> oshape(
          {data->shape[0], (weight_indptr->shape[0] - 1) * weight_data->shape[1]});
      reporter->Assign(types[4], TensorType(oshape, data->dtype));
      return true;
    }
    LOG(FATAL) << "Unknown weight ndim for nn.sparse_dense, should be 1 (CSR) or 3 (BSR)";
    return false;
  }
}

// Positional relay function to create dense operator used by frontend FFI.
Expr MakeSparseDense(Expr data, Expr weight_data, Expr weight_indices, Expr weight_indptr,
                     bool sparse_data) {
  auto attrs = make_object<SparseDenseAttrs>();
  attrs->sparse_data = std::move(sparse_data);
  static const Op& op = Op::Get("nn.sparse_dense");
  return Call(op, {data, weight_data, weight_indices, weight_indptr}, Attrs(attrs), {});
}

TVM_REGISTER_GLOBAL("relay.op.nn._make.sparse_dense")
    .set_body([](const TVMArgs& args, TVMRetValue* rv) {
      runtime::detail::unpack_call<Expr, 5>(MakeSparseDense, args, rv);
    });

RELAY_REGISTER_OP("nn.sparse_dense")
    .describe(
        R"code(Applies a sparse linear transformation: :math:`Y = XW^T` with either X or W sparse.

- **data**: `(x1, x2, ..., xn, input_dim)`
- **weight**: `(units, input_dim)`
- **out**: `(x1, x2, ..., xn, units)`.

)code" TVM_ADD_FILELINE)
    .set_attrs_type<SparseDenseAttrs>()
    .set_num_inputs(4)
    .add_argument("input_tensor1", "nD Tensor",
                  "Input data if dense, otherwise data_data matrix if sparse.")
    .add_argument("input_tensor2", "nD Tensor", "Weight_data matrix or data_indices matrix.")
    .add_argument("input_tensor3", "nD Tensor", "Weight_indices matrix or data_indptr matrix.")
    .add_argument("input_tensor4", "nD Tensor", "Weight_indptr matrix or weight matrix if dense.")
    .set_support_level(1)
    .add_type_rel("SparseDense", SparseDenseRel);

Expr MakeSparseDensePadded(Expr data, Expr weight_data, Expr weight_indices, Expr weight_indptr) {
  auto attrs = make_object<SparseDenseAttrs>();
  static const Op& op = Op::Get("nn.internal.sparse_dense_padded");
  return Call(op, {data, weight_data, weight_indices, weight_indptr}, Attrs(attrs), {});
}

TVM_REGISTER_GLOBAL("relay.op.nn._make.sparse_dense_padded")
    .set_body([](const TVMArgs& args, TVMRetValue* rv) {
      runtime::detail::unpack_call<Expr, 4>(MakeSparseDensePadded, args, rv);
    });

RELAY_REGISTER_OP("nn.internal.sparse_dense_padded")
    .describe(
        R"code(Applies a sparse linear transformation: :math:`Y = XW^T` with W
sparse. This variation uses a matrix with row lengths padded to a
multiple of 32 for better GPU performance.

This op should not be directly used by a user. Instead, use `sparse_dense`
which will be converted to this op when running on the GPU.

- **data**: `(x1, x2, ..., xn, input_dim)`
- **weight**: `(units, input_dim)`
- **out**: `(x1, x2, ..., xn, units)`.

)code" TVM_ADD_FILELINE)
    .set_attrs_type<SparseDenseAttrs>()
    .set_num_inputs(4)
    .add_argument("data", "nD Tensor", "Input data.")
    .add_argument("weight_data", "1D Tensor", "Weight data matrix.")
    .add_argument("weight_indices", "1D Tensor", "Weight indices matrix.")
    .add_argument("weight_indptr", "1D Tensor", "Weight indptr matrix.")
    .set_support_level(1)
    .add_type_rel("SparseDense", SparseDenseRel);

// relay.nn.sparse_transpose
TVM_REGISTER_NODE_TYPE(SparseTransposeAttrs);

bool SparseTransposeRel(const Array<Type>& types, int num_inputs, const Attrs& attrs,
                        const TypeReporter& reporter) {
  ICHECK_EQ(types.size(), 4);
  const auto* sparse_data = types[0].as<TensorTypeNode>();
  ICHECK_EQ(sparse_data->shape.size(), 1);
  const auto* sparse_indices = types[1].as<TensorTypeNode>();
  ICHECK_EQ(sparse_indices->shape.size(), 1);
  const auto* sparse_indptr = types[2].as<TensorTypeNode>();

  std::vector<Type> output_types;
  output_types.push_back(TensorType(sparse_data->shape, sparse_data->dtype));
  output_types.push_back(TensorType(sparse_indices->shape, sparse_indices->dtype));
  output_types.push_back(TensorType(sparse_indptr->shape, sparse_indptr->dtype));

  reporter->Assign(types[3], TupleType(Array<Type>(output_types)));
  return true;
}

Expr MakeSparseTranspose(Expr sparse_data, Expr sparse_indices, Expr sparse_indptr) {
  auto attrs = make_object<SparseTransposeAttrs>();
  static const Op& op = Op::Get("nn.sparse_transpose");
  return Call(op, {sparse_data, sparse_indices, sparse_indptr}, Attrs(attrs), {});
}

TVM_REGISTER_GLOBAL("relay.op.nn._make.sparse_transpose").set_body_typed(MakeSparseTranspose);

RELAY_REGISTER_OP("nn.sparse_transpose")
    .describe(R"code(Transpose a sparse matrix X. Only support square sparse matrix

- **input**: `(N, N)`
- **out**: `(N, N)`.

)code" TVM_ADD_FILELINE)
    .set_attrs_type<SparseTransposeAttrs>()
    .set_num_inputs(3)
    .add_argument("sparse_data", "1D Tensor", "Sparse data matrix.")
    .add_argument("sparse_indices", "1D Tensor", "Sparse indices matrix.")
    .add_argument("sparse_indptr", "1D Tensor", "Sparse index pointer matrix.")
    .set_support_level(1)
    .add_type_rel("SparseTranspose", SparseTransposeRel);

}  // namespace relay
}  // namespace tvm
