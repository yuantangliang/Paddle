set(eager_manual_nodes
    ${PADDLE_SOURCE_DIR}/paddle/fluid/eager/api/manual/eager_manual/nodes/conv2d_nodes.cc
    ${PADDLE_SOURCE_DIR}/paddle/fluid/eager/api/manual/eager_manual/nodes/add_n_node.cc
    ${PADDLE_SOURCE_DIR}/paddle/fluid/eager/api/manual/eager_manual/nodes/sync_batch_norm_node.cc
    ${PADDLE_SOURCE_DIR}/paddle/fluid/eager/api/manual/eager_manual/nodes/multiply_node.cc
    PARENT_SCOPE)
