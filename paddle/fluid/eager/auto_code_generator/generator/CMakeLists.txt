set(api_yaml_path
    "${PADDLE_SOURCE_DIR}/paddle/phi/api/yaml/ops.yaml,${PADDLE_SOURCE_DIR}/paddle/phi/api/yaml/legacy_ops.yaml,${PADDLE_SOURCE_DIR}/paddle/phi/api/yaml/sparse_ops.yaml,${PADDLE_SOURCE_DIR}/paddle/phi/api/yaml/fused_ops.yaml"
)
set(backward_yaml_path
    "${PADDLE_SOURCE_DIR}/paddle/phi/api/yaml/backward.yaml,${PADDLE_SOURCE_DIR}/paddle/phi/api/yaml/legacy_backward.yaml,${PADDLE_SOURCE_DIR}/paddle/phi/api/yaml/sparse_backward.yaml,${PADDLE_SOURCE_DIR}/paddle/phi/api/yaml/fused_backward.yaml"
)
set(tmp_forwards_cc_path
    "${PADDLE_SOURCE_DIR}/paddle/fluid/eager/api/generated/eager_generated/forwards/tmp_dygraph_functions.cc"
)
set(tmp_forwards_h_path
    "${PADDLE_SOURCE_DIR}/paddle/fluid/eager/api/generated/eager_generated/forwards/tmp_dygraph_functions.h"
)
set(tmp_nodes_cc_path
    "${PADDLE_SOURCE_DIR}/paddle/fluid/eager/api/generated/eager_generated/backwards/tmp_nodes.cc"
)
set(tmp_nodes_h_path
    "${PADDLE_SOURCE_DIR}/paddle/fluid/eager/api/generated/eager_generated/backwards/tmp_nodes.h"
)
set(forwards_cc_path
    "${PADDLE_SOURCE_DIR}/paddle/fluid/eager/api/generated/eager_generated/forwards/dygraph_functions.cc"
)
set(forwards_h_path
    "${PADDLE_SOURCE_DIR}/paddle/fluid/eager/api/generated/eager_generated/forwards/dygraph_functions.h"
)
set(nodes_cc_path
    "${PADDLE_SOURCE_DIR}/paddle/fluid/eager/api/generated/eager_generated/backwards/nodes.cc"
)
set(nodes_h_path
    "${PADDLE_SOURCE_DIR}/paddle/fluid/eager/api/generated/eager_generated/backwards/nodes.h"
)
# StringTensor only needs forward api
set(fwd_api_yaml_path
    "${PADDLE_SOURCE_DIR}/paddle/phi/api/yaml/strings_ops.yaml")

message("Final State Eager CodeGen")
add_custom_target(
  eager_codegen
  COMMAND
    "${PYTHON_EXECUTABLE}"
    "${PADDLE_SOURCE_DIR}/paddle/fluid/eager/auto_code_generator/generator/eager_gen.py"
    "--api_yaml_path=${api_yaml_path},${fwd_api_yaml_path}"
    "--backward_yaml_path=${backward_yaml_path}"
    "--forwards_cc_path=${tmp_forwards_cc_path}"
    "--forwards_h_path=${tmp_forwards_h_path}"
    "--nodes_cc_path=${tmp_nodes_cc_path}" "--nodes_h_path=${tmp_nodes_h_path}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${tmp_forwards_cc_path}
          ${forwards_cc_path}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${tmp_forwards_h_path}
          ${forwards_h_path}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${tmp_nodes_cc_path}
          ${nodes_cc_path}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${tmp_nodes_h_path}
          ${nodes_h_path}
  VERBATIM)

set(tmp_python_c_output_path
    "${PADDLE_SOURCE_DIR}/paddle/fluid/pybind/eager_op_function.cc.tmp")
set(python_c_output_path
    "${PADDLE_SOURCE_DIR}/paddle/fluid/pybind/eager_op_function.cc")

add_custom_target(
  eager_python_c_codegen
  COMMAND
    "${PYTHON_EXECUTABLE}"
    "${PADDLE_SOURCE_DIR}/paddle/fluid/eager/auto_code_generator/generator/python_c_gen.py"
    "--api_yaml_path=${api_yaml_path},${fwd_api_yaml_path}"
    "--output_path=${tmp_python_c_output_path}"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${tmp_python_c_output_path}
          ${python_c_output_path}
  VERBATIM)
