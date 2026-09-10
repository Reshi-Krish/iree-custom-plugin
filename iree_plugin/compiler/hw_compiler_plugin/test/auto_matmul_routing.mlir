// Same shape as two_device_affinity.mlir, but with NO hand-written
// `stream.affinity` attribute anywhere - this relies entirely on
// AnnotateMatmulAffinity.cpp (run during preprocessing) to recognize
// @on_hw's linalg.matmul and route the function to the "hw" device
// automatically, and to leave @on_cpu (no matmul) alone so it defaults to
// whichever device the module's default affinity resolves to.
//
// Run the same way as two_device_affinity.mlir (see that file for the full
// command lines and what to check in the output); the executable variants
// should come out identical.

func.func @on_hw(%arg0: tensor<2x2xf32>, %arg1: tensor<2x2xf32>) -> tensor<2x2xf32> {
  %init = tensor.empty() : tensor<2x2xf32>
  %cst = arith.constant 0.0 : f32
  %filled = linalg.fill ins(%cst : f32) outs(%init : tensor<2x2xf32>) -> tensor<2x2xf32>
  %result = linalg.matmul ins(%arg0, %arg1 : tensor<2x2xf32>, tensor<2x2xf32>)
      outs(%filled : tensor<2x2xf32>) -> tensor<2x2xf32>
  return %result : tensor<2x2xf32>
}

func.func @on_cpu(%arg0: tensor<4xf32>) -> tensor<4xf32> {
  %result = arith.addf %arg0, %arg0 : tensor<4xf32>
  return %result : tensor<4xf32>
}
