// Copyright (c) 2022 PaddlePaddle Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "paddle/fluid/distributed/collective/process_group_custom.h"

#include "paddle/fluid/distributed/collective/common.h"
#include "paddle/fluid/distributed/collective/custom_ccl_tools.h"
#include "paddle/fluid/distributed/collective/utils.h"
#include "paddle/fluid/memory/malloc.h"
#include "paddle/fluid/platform/device_context.h"
#include "paddle/fluid/platform/place.h"
#include "paddle/phi/api/lib/utils/allocator.h"
#include "paddle/phi/common/place.h"
#include "paddle/phi/core/distributed/check/static_check.h"

DECLARE_bool(xccl_blocking_wait);

constexpr int64_t kWaitBlockTImeout = 10;

namespace paddle {
namespace distributed {

void SyncDefaultStream(
    const std::vector<Place>& places,
    std::vector<CustomEventManager>& cclEvents,                    // NOLINT
    std::vector<std::unique_ptr<CustomDeviceContext>>& dev_ctx) {  // NOLINT
  for (size_t i = 0; i < places.size(); ++i) {
    auto* default_ctx = static_cast<platform::CustomDeviceContext*>(
        platform::DeviceContextPool::Instance().Get(places[i]));
    cclEvents[i].Record(*default_ctx);
    cclEvents[i].Block(*dev_ctx[i]);
  }
}

std::shared_ptr<ProcessGroupCustom::CustomTask> ProcessGroupCustom::CreateTask(
    std::vector<Place> places,
    int rank,
    CommType comm_type,
    const std::vector<phi::DenseTensor>& inputs) {
  return std::make_shared<ProcessGroupCustom::CustomTask>(
      places, rank, comm_type, inputs);
}

ProcessGroupCustom::CustomTask::CustomTask(
    const std::vector<Place>& places,
    int rank,
    CommType CommType,
    const std::vector<phi::DenseTensor>& inputs)
    : Task(rank, inputs, CommType), places_(places) {
  control_events_.resize(places.size());
  cclComms_.resize(places.size());
}

ProcessGroupCustom::CustomTask::~CustomTask() {}

void ProcessGroupCustom::CustomTask::SetOutputs(
    std::vector<phi::DenseTensor>& outputs) {  // NOLINT
  outputs_ = std::make_shared<std::vector<phi::DenseTensor>>(outputs);
}

void ProcessGroupCustom::CustomTask::SynchronizeStreams() {
  for (size_t i = 0; i < places_.size(); ++i) {
    auto* default_ctx = static_cast<platform::CustomDeviceContext*>(
        platform::DeviceContextPool::Instance().Get(places_[i]));
    phi::DeviceGuard guard(default_ctx->GetPlace());
    control_events_[i].Block(*default_ctx);
  }
}

bool ProcessGroupCustom::CustomTask::IsCompleted() {
  for (size_t i = 0; i < places_.size(); ++i) {
    if (!control_events_[i].Query()) {
      return false;
    }
  }

  return true;
}

bool ProcessGroupCustom::CustomTask::Wait(std::chrono::milliseconds timeout) {
  SynchronizeStreams();
  while (!IsCompleted()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(kWaitBlockTImeout));
  }
  return true;
}

// Same as Wait
void ProcessGroupCustom::CustomTask::Synchronize() { Wait(kWaitTimeout); }

void ProcessGroupCustom::CustomTask::UpdateWaitChain(
    const phi::DeviceContext& ctx) {
  PADDLE_ENFORCE_NE(
      std::find(places_.cbegin(), places_.cend(), ctx.GetPlace()),
      places_.cend(),
      phi::errors::NotFound("Cannot find the device context in this task."));
  auto index = std::find(places_.cbegin(), places_.cend(), ctx.GetPlace()) -
               places_.cbegin();
  control_events_[index].Record(
      reinterpret_cast<const phi::CustomContext&>(ctx));
}

ProcessGroupCustom::ProcessGroupCustom(
    const std::shared_ptr<phi::distributed::Store>& store,
    const std::string& device_type,
    int rank,
    int size,
    int gid)
    : ProcessGroupWithStream(rank, size, gid),
      store_(store),
      device_type_(device_type) {}

void ProcessGroupCustom::BroadcastUniqueCustomID(
    std::vector<phi::ccl::CCLRootId>& ccl_ids) {  // NOLINT
  if (rank_ == 0) {
    for (size_t i = 0; i < ccl_ids.size(); i++) {
      auto key = "ProcessGroupCustom/ccl_ids/" + std::to_string(gid_) + "/" +
                 std::to_string(i);
      store_->set(key, ccl_ids[i]);
    }
  } else {
    for (size_t i = 0; i < ccl_ids.size(); i++) {
      auto key = "ProcessGroupCustom/ccl_ids/" + std::to_string(gid_) + "/" +
                 std::to_string(i);
      ccl_ids[i] = store_->get(key);
    }
  }
}

// create CustomCCLManager cache for places_key
void ProcessGroupCustom::CreateCustomManagerCache(
    const std::string& places_key, const std::vector<Place>& places) {
  PADDLE_ENFORCE_EQ(
      places_key.empty(),
      false,
      platform::errors::PreconditionNotMet(
          "Not able to create/get the CustomCCL Communicator since "
          "the NPU place are not known"));
  const std::string device_type = places.back().GetDeviceType();

  std::vector<std::shared_ptr<CustomCCLCommManager>> ccl_comms;
  ccl_comms.resize(places.size());

  // using vector just for broadcast
  std::vector<phi::ccl::CCLRootId> ccl_ids;
  ccl_ids.resize(1);
  auto& ccl_id = ccl_ids.front();

  if (rank_ == 0) {
    phi::DeviceManager::CCLGetUniqueId(device_type, &ccl_id);
  }
  BroadcastUniqueCustomID(ccl_ids);

  VLOG(3) << "init custom ccl rank: " << rank_ << ", nranks: " << size_
          << ", place: " << places_key
          << ", custom ccl uniqueid: " << SerializeCustomCCLUniqueId(ccl_id);

  std::vector<std::unique_ptr<CustomDeviceContext>> dev_ctx;
  dev_ctx.resize(places.size());

  for (size_t i = 0; i < places.size(); ++i) {
    phi::DeviceGuard guard(places[i]);
    ccl_comms[i] = CustomCCLCommManager::Create(
        device_type, GetSize(), GetRank(), &ccl_id, new phi::ccl::CCLComm);
    dev_ctx[i].reset(new CustomDeviceContext(places[i]));
  }

  std::vector<CustomEventManager> events;
  events.resize(places.size());

  // These caches will be useful to process sync/wait/communicate
  places_to_events_.emplace(places_key, std::move(events));
  places_to_customcomm_.emplace(places_key, std::move(ccl_comms));
  places_to_ctx_.emplace(places_key, std::move(dev_ctx));
}

template <typename Fn>
std::shared_ptr<ProcessGroup::Task> ProcessGroupCustom::Collective(
    std::vector<phi::DenseTensor>& inputs,
    std::vector<phi::DenseTensor>& outputs,
    Fn fn,
    CommType op_type,
    bool sync_op UNUSED,
    bool use_calc_stream) {
  const auto places = GetPlaceList(inputs);
  const auto key = GetKeyFromPlaces(places);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (places_to_customcomm_.find(key) == places_to_customcomm_.end()) {
      CreateCustomManagerCache(key, places);
    }
  }

  auto& ccl_comms = places_to_customcomm_[key];
  if (!use_calc_stream) {
    SyncDefaultStream(places, places_to_events_[key], places_to_ctx_[key]);
  }
  auto task = CreateTask(places, rank_, op_type, inputs);
  task->SetOutputs(outputs);

  for (size_t i = 0; i < inputs.size(); ++i) {
    phi::DeviceGuard guard(places[i]);
    const auto& ccl_stream =
        use_calc_stream ? reinterpret_cast<phi::CustomContext*>(
                              phi::DeviceContextPool::Instance().Get(places[i]))
                              ->stream()
                        : places_to_ctx_[key][i]->stream();
    phi::stream::Stream stream(places[i], ccl_stream);
    fn(inputs[i], outputs[i], ccl_comms[i]->GetCustomCCLComm(), stream);
  }

  if (!use_calc_stream) {
    for (size_t i = 0; i < inputs.size(); ++i) {
      phi::DeviceGuard guard(places[i]);
      task->control_events_[i].Record(*places_to_ctx_[key][i]);
    }
  }
  return task;
}

void* XcclGetPointerByOffset(void* raw_pointer,
                             size_t offset,
                             phi::DataType type) {
  if (type == phi::DataType::FLOAT32) {
    return reinterpret_cast<void*>(reinterpret_cast<float*>(raw_pointer) +
                                   offset);
  } else if (type == phi::DataType::FLOAT64) {
    return reinterpret_cast<void*>(reinterpret_cast<double*>(raw_pointer) +
                                   offset);
  } else if (type == phi::DataType::INT32) {
    return reinterpret_cast<void*>(reinterpret_cast<int32_t*>(raw_pointer) +
                                   offset);
  } else if (type == phi::DataType::INT64) {
    return reinterpret_cast<void*>(reinterpret_cast<int64_t*>(raw_pointer) +
                                   offset);
  } else if (type == phi::DataType::FLOAT16) {
    return reinterpret_cast<void*>(reinterpret_cast<int16_t*>(raw_pointer) +
                                   offset);
  } else {
    PADDLE_THROW(platform::errors::Unimplemented(
        "This datatype in xccl is not supported."));
  }
  return nullptr;
}

std::shared_ptr<ProcessGroup::Task> ProcessGroupCustom::AllGather(
    phi::DenseTensor* out_tensor,
    const phi::DenseTensor& in_tensor,
    int64_t offset,
    int64_t numel,
    bool sync_op,  // for compatibility, no use now
    bool use_calc_stream) {
  // numel > 0 indicates the tensor need to be sliced
  const phi::DenseTensor& in_tensor_maybe_partial =
      numel > 0
          ? paddle::distributed::GetPartialTensor(in_tensor, offset, numel)
          : in_tensor;
  phi::distributed::CommStaticCheck::GatherLikeShape(
      *out_tensor,
      in_tensor_maybe_partial,
      /*dst_rank*/ rank_,
      /*cur_rank*/ rank_,
      size_,
      phi::AllocationType::CUSTOM);
  std::vector<phi::DenseTensor> in_wrapper{in_tensor_maybe_partial};
  std::vector<phi::DenseTensor> out_wrapper{*out_tensor};

  return Collective(
      in_wrapper,
      out_wrapper,
      [&](phi::DenseTensor& input,
          phi::DenseTensor& output,
          phi::ccl::CCLComm comm,
          const phi::stream::Stream& stream) {
        return phi::DeviceManager::CCLAllGather(
            device_type_,
            input.data(),
            output.data(),
            input.numel(),
            phi::ccl::ToCCLDataType(input.dtype()),
            comm,
            stream);
      },
      CommType::ALLGATHER,
      sync_op,
      use_calc_stream);
}

std::shared_ptr<ProcessGroup::Task> ProcessGroupCustom::AllGather(
    phi::DenseTensor* out_tensor,
    const phi::DenseTensor& in_tensor,
    int64_t offset,
    int64_t numel,
    bool sync_op) {
  return AllGather(out_tensor, in_tensor, offset, numel, sync_op, false);
}

// TODO(sunyilun): methods below will be removed later
std::shared_ptr<ProcessGroup::Task> ProcessGroupCustom::AllGather(
    std::vector<phi::DenseTensor>& in_tensors,
    std::vector<phi::DenseTensor>& out_tensors) {
  PADDLE_ENFORCE_EQ(
      CheckTensorsInCustomPlace(in_tensors, device_type_),
      true,
      platform::errors::InvalidArgument(
          "All inputs should be in CustomPlace(%s).", device_type_));
  PADDLE_ENFORCE_EQ(
      CheckTensorsInCustomPlace(out_tensors, device_type_),
      true,
      platform::errors::InvalidArgument(
          "All outputs should be in CustomPlace(%s).", device_type_));
  return Collective(
      in_tensors,
      out_tensors,
      [&](phi::DenseTensor& input,
          phi::DenseTensor& output,
          phi::ccl::CCLComm comm,
          const phi::stream::Stream& stream) {
        return phi::DeviceManager::CCLAllGather(
            device_type_,
            input.data(),
            output.data(),
            input.numel(),
            phi::ccl::ToCCLDataType(input.dtype()),
            comm,
            stream);
      },
      CommType::ALLGATHER,
      false,
      false);
}

std::shared_ptr<ProcessGroup::Task> ProcessGroupCustom::AllReduce(
    phi::DenseTensor* out_tensor,
    const phi::DenseTensor& in_tensor,
    const AllreduceOptions& opts,
    bool sync_op,  // for compatibility, no use now
    bool use_calc_stream) {
  std::vector<phi::DenseTensor> in_wrapper{in_tensor};
  std::vector<phi::DenseTensor> out_wrapper{*out_tensor};
  PADDLE_ENFORCE_EQ(
      CheckTensorsInCustomPlace(in_wrapper, device_type_),
      true,
      platform::errors::InvalidArgument(
          "All inputs should be in CustomPlace(%s).", device_type_));
  PADDLE_ENFORCE_EQ(
      CheckTensorsInCustomPlace(out_wrapper, device_type_),
      true,
      platform::errors::InvalidArgument(
          "All outputs should be in CustomPlace(%s).", device_type_));
  return Collective(
      in_wrapper,
      out_wrapper,
      [&](phi::DenseTensor& input,
          phi::DenseTensor& output,
          phi::ccl::CCLComm comm,
          const phi::stream::Stream& stream) {
        return phi::DeviceManager::CCLAllReduce(
            device_type_,
            input.data(),
            output.data(),
            input.numel(),
            phi::ccl::ToCCLDataType(input.dtype()),
            ToCustomCCLRedType(opts.reduce_op),
            comm,
            stream);
      },
      CommType::ALLREDUCE,
      sync_op,
      use_calc_stream);
}

std::shared_ptr<ProcessGroup::Task> ProcessGroupCustom::AllReduce(
    phi::DenseTensor* out_tensor,
    const phi::DenseTensor& in_tensor,
    const AllreduceOptions& opts,
    bool sync_op  // for compatibility, no use now
) {
  return AllReduce(out_tensor, in_tensor, opts, sync_op, false);
}

std::shared_ptr<ProcessGroup::Task> ProcessGroupCustom::AllReduce(
    std::vector<phi::DenseTensor>& in_tensors,   // NOLINT
    std::vector<phi::DenseTensor>& out_tensors,  // NOLINT
    const AllreduceOptions& opts) {
  PADDLE_ENFORCE_EQ(
      CheckTensorsInCustomPlace(in_tensors, device_type_),
      true,
      platform::errors::InvalidArgument(
          "All inputs should be in CustomPlace(%s).", device_type_));
  PADDLE_ENFORCE_EQ(
      CheckTensorsInCustomPlace(out_tensors, device_type_),
      true,
      platform::errors::InvalidArgument(
          "All outputs should be in CustomPlace(%s).", device_type_));
  return Collective(
      in_tensors,
      out_tensors,
      [&](phi::DenseTensor& input,
          phi::DenseTensor& output,
          phi::ccl::CCLComm comm,
          const phi::stream::Stream& stream) {
        return phi::DeviceManager::CCLAllReduce(
            device_type_,
            input.data(),
            output.data(),
            input.numel(),
            phi::ccl::ToCCLDataType(input.dtype()),
            ToCustomCCLRedType(opts.reduce_op),
            comm,
            stream);
      },
      CommType::ALLREDUCE,
      false,
      false);
}

std::shared_ptr<ProcessGroup::Task> ProcessGroupCustom::Broadcast(
    phi::DenseTensor* out_tensor,
    const phi::DenseTensor& in_tensor,
    const BroadcastOptions& opts,
    bool sync_op,  // for compatibility, no use now
    bool use_calc_stream) {
  std::vector<phi::DenseTensor> in_wrapper{in_tensor};
  std::vector<phi::DenseTensor> out_wrapper{*out_tensor};
  PADDLE_ENFORCE_EQ(
      CheckTensorsInCustomPlace(in_wrapper, device_type_),
      true,
      platform::errors::InvalidArgument(
          "All inputs should be in CustomPlace(%s).", device_type_));
  PADDLE_ENFORCE_EQ(
      CheckTensorsInCustomPlace(out_wrapper, device_type_),
      true,
      platform::errors::InvalidArgument(
          "All outputs should be in CustomPlace(%s).", device_type_));
  return Collective(
      in_wrapper,
      out_wrapper,
      [&](phi::DenseTensor& input,
          phi::DenseTensor& output,
          phi::ccl::CCLComm comm,
          const phi::stream::Stream& stream) {
        int root = opts.source_rank * in_wrapper.size() + opts.source_root;
        if (rank_ == root) {
          return phi::DeviceManager::CCLBroadcast(
              device_type_,
              input.data(),
              input.numel(),
              phi::ccl::ToCCLDataType(input.dtype()),
              root,
              comm,
              stream);
        } else {
          return phi::DeviceManager::CCLBroadcast(
              device_type_,
              output.data(),
              output.numel(),
              phi::ccl::ToCCLDataType(output.dtype()),
              root,
              comm,
              stream);
        }
      },
      CommType::BROADCAST,
      sync_op,
      use_calc_stream);
}

std::shared_ptr<ProcessGroup::Task> ProcessGroupCustom::Broadcast(
    phi::DenseTensor* out_tensor,
    const phi::DenseTensor& in_tensor,
    const BroadcastOptions& opts,
    bool sync_op) {
  return Broadcast(out_tensor, in_tensor, opts, sync_op, false);
}

std::shared_ptr<ProcessGroup::Task> ProcessGroupCustom::Barrier(
    const BarrierOptions& opts) {
  // Only support single card single process
  PADDLE_ENFORCE_GE(opts.device_id,
                    0,
                    platform::errors::PreconditionNotMet(
                        "The barrier device id must greater or equal than 0."));
  platform::CustomPlace place(device_type_, opts.device_id);
  auto allocator = std::unique_ptr<phi::Allocator>(
      new paddle::experimental::DefaultAllocator(place));
  phi::DenseTensorMeta meta(phi::DataType::FLOAT32, phi::DDim{1});
  phi::DenseTensor barrier_tensor{allocator.get(), meta};

  auto task = ProcessGroupCustom::AllReduce(&barrier_tensor,
                                            barrier_tensor,
                                            {},
                                            /*sync_op*/ true,
                                            false);
  auto xccl_task = dynamic_cast<ProcessGroupCustom::CustomTask*>(task.get());
  xccl_task->barrierTensors_ = {barrier_tensor};
  return task;
}

phi::DeviceContext* ProcessGroupCustom::GetDeviceContext(
    const Place& place) const {
  const std::string key = GetKeyFromPlace(place);
  const auto& iter = places_to_ctx_.find(key);
  PADDLE_ENFORCE_NE(
      iter,
      places_to_ctx_.end(),
      platform::errors::NotFound(
          "Cannot find the device context in this process group."));
  return iter->second[0].get();
}

phi::ccl::CCLComm ProcessGroupCustom::CustomCCLComm(const Place& place) const {
  std::vector<Place> places = {place};
  const auto& iter = places_to_customcomm_.find(GetKeyFromPlaces(places));
  PADDLE_ENFORCE_NE(iter,
                    places_to_customcomm_.end(),
                    platform::errors::InvalidArgument(
                        "Cannot find nccl comm in process group."));
  return iter->second[0]->GetCustomCCLComm();
}

std::shared_ptr<ProcessGroup::Task> ProcessGroupCustom::Broadcast(
    std::vector<phi::DenseTensor>& in_tensors,   // NOLINT
    std::vector<phi::DenseTensor>& out_tensors,  // NOLINT
    const BroadcastOptions& opts) {
  PADDLE_ENFORCE_EQ(
      CheckTensorsInCustomPlace(in_tensors, device_type_),
      true,
      platform::errors::InvalidArgument(
          "All inputs should be in CustomPlace(%s).", device_type_));
  PADDLE_ENFORCE_EQ(
      CheckTensorsInCustomPlace(out_tensors, device_type_),
      true,
      platform::errors::InvalidArgument(
          "All outputs should be in CustomPlace(%s).", device_type_));
  return Collective(
      in_tensors,
      out_tensors,
      [&](phi::DenseTensor& input,
          phi::DenseTensor& output,
          phi::ccl::CCLComm comm,
          const phi::stream::Stream& stream) {
        int root = opts.source_rank * in_tensors.size() + opts.source_root;
        if (rank_ == root) {
          return phi::DeviceManager::CCLBroadcast(
              device_type_,
              input.data(),
              input.numel(),
              phi::ccl::ToCCLDataType(input.dtype()),
              root,
              comm,
              stream);
        } else {
          return phi::DeviceManager::CCLBroadcast(
              device_type_,
              output.data(),
              output.numel(),
              phi::ccl::ToCCLDataType(output.dtype()),
              root,
              comm,
              stream);
        }
      },
      CommType::BROADCAST,
      false,
      false);
}

void CheckTensorsInDifferentCustomDevices(
    const std::vector<phi::DenseTensor>& tensors, const size_t num_devices) {
  PADDLE_ENFORCE_EQ(
      tensors.size() == 0,
      false,
      phi::errors::InvalidArgument("Tensor list must be nonempty."));
  PADDLE_ENFORCE_LE(
      tensors.size(),
      num_devices,
      phi::errors::InvalidArgument("Tensor list mustn't be larger than the "
                                   "number of available CustomDevice."));

  std::set<Place> used_devices;

  for (const auto& t : tensors) {
    PADDLE_ENFORCE_EQ(platform::is_custom_place(t.place()),
                      true,
                      phi::errors::InvalidArgument(
                          "Tensors must be CustomDevice and dense tensor."));

    const auto inserted = used_devices.insert(t.place()).second;
    PADDLE_ENFORCE_EQ(inserted,
                      true,
                      phi::errors::InvalidArgument(
                          "Tensors must be on distinct custom devices."));
  }
}

std::shared_ptr<ProcessGroup::Task> ProcessGroupCustom::Recv(
    phi::DenseTensor* tensor,
    int src_rank,
    int64_t offset,
    int64_t numel,
    bool sync_op,
    bool use_calc_stream) {
  // numel > 0 indicates the tensor need to be sliced
  phi::DenseTensor partial_tensor;
  if (numel > 0) {
    partial_tensor = GetPartialTensor(*tensor, offset, numel);
    tensor = &partial_tensor;
  }
  phi::distributed::CommStaticCheck::CheckShape(
      *tensor, rank_, size_, phi::AllocationType::CUSTOM);
  std::vector<phi::DenseTensor> in_wrapper{*tensor};
  std::vector<phi::DenseTensor> out_wrapper{*tensor};
  return Collective(
      in_wrapper,
      out_wrapper,
      [&](phi::DenseTensor& input,
          phi::DenseTensor& output,
          phi::ccl::CCLComm comm,
          const phi::stream::Stream& stream) {
        phi::DeviceManager::CCLRecv(device_type_,
                                    output.data(),
                                    output.numel(),
                                    phi::ccl::ToCCLDataType(output.dtype()),
                                    src_rank,
                                    comm,
                                    stream);
      },
      CommType::RECV,
      sync_op,
      use_calc_stream);
}

std::shared_ptr<ProcessGroup::Task> ProcessGroupCustom::Recv(
    std::vector<phi::DenseTensor>& tensors, int src_rank) {
  CheckTensorsInDifferentCustomDevices(tensors, static_cast<size_t>(GetSize()));
  return Collective(
      tensors,
      tensors,
      [&](phi::DenseTensor& input,
          phi::DenseTensor& output,
          phi::ccl::CCLComm comm,
          const phi::stream::Stream& stream) {
        phi::DeviceManager::CCLRecv(device_type_,
                                    output.data(),
                                    output.numel(),
                                    phi::ccl::ToCCLDataType(output.dtype()),
                                    src_rank,
                                    comm,
                                    stream);
      },
      CommType::RECV,
      false,
      false);
}

std::shared_ptr<ProcessGroup::Task> ProcessGroupCustom::Send(
    const phi::DenseTensor& tensor,
    int dst_rank,
    int64_t offset,
    int64_t numel,
    bool sync_op,
    bool use_calc_stream) {
  // numel > 0 indicates the tensor need to be sliced
  const phi::DenseTensor& tensor_maybe_partial =
      numel > 0 ? GetPartialTensor(tensor, offset, numel) : tensor;
  phi::distributed::CommStaticCheck::CheckShape(
      tensor_maybe_partial, rank_, size_, phi::AllocationType::CUSTOM);
  std::vector<phi::DenseTensor> in_wrapper{tensor_maybe_partial};
  std::vector<phi::DenseTensor> out_wrapper{tensor_maybe_partial};
  return Collective(
      in_wrapper,
      out_wrapper,
      [&](phi::DenseTensor& input,
          phi::DenseTensor& output,
          phi::ccl::CCLComm comm,
          const phi::stream::Stream& stream) {
        phi::DeviceManager::CCLSend(device_type_,
                                    input.data(),
                                    input.numel(),
                                    phi::ccl::ToCCLDataType(input.dtype()),
                                    dst_rank,
                                    comm,
                                    stream);
      },
      CommType::SEND,
      sync_op,
      use_calc_stream);
}

std::shared_ptr<ProcessGroup::Task> ProcessGroupCustom::Send(
    std::vector<phi::DenseTensor>& tensors, int dst_rank) {
  CheckTensorsInDifferentCustomDevices(tensors, static_cast<size_t>(GetSize()));
  return Collective(
      tensors,
      tensors,
      [&](phi::DenseTensor& input,
          phi::DenseTensor& output,
          phi::ccl::CCLComm comm,
          const phi::stream::Stream& stream) {
        phi::DeviceManager::CCLSend(device_type_,
                                    input.data(),
                                    input.numel(),
                                    phi::ccl::ToCCLDataType(input.dtype()),
                                    dst_rank,
                                    comm,
                                    stream);
      },
      CommType::SEND,
      false,
      false);
}

std::shared_ptr<ProcessGroup::Task> ProcessGroupCustom::Reduce(
    phi::DenseTensor* out_tensor,
    const phi::DenseTensor& in_tensor,
    const ReduceOptions& opts,
    bool sync_op,
    bool use_calc_stream) {
  phi::distributed::CommStaticCheck::SameShape(*out_tensor,
                                               in_tensor,
                                               /*dst_rank*/ opts.root_rank,
                                               /*cur_rank*/ rank_,
                                               size_,
                                               phi::AllocationType::CUSTOM);
  std::vector<phi::DenseTensor> in_wrapper{in_tensor};
  std::vector<phi::DenseTensor> out_wrapper{*out_tensor};
  return Collective(
      in_wrapper,
      out_wrapper,
      [&](phi::DenseTensor& input,
          phi::DenseTensor& output,
          phi::ccl::CCLComm comm,
          const phi::stream::Stream& stream) {
        phi::DeviceManager::CCLReduce(device_type_,
                                      input.data(),
                                      output.data(),
                                      input.numel(),
                                      phi::ccl::ToCCLDataType(input.dtype()),
                                      ToCustomCCLRedType(opts.reduce_op),
                                      opts.root_rank,
                                      comm,
                                      stream);
      },
      CommType::REDUCE,
      sync_op,
      use_calc_stream);
}

std::shared_ptr<ProcessGroupCustom>
ProcessGroupCustom::CreateProcessGroupCustom(
    const std::shared_ptr<phi::distributed::Store>& store,
    const std::string& device_type,
    int rank,
    int size,
    int gid) {
  auto process_group =
      std::make_shared<ProcessGroupCustom>(store, device_type, rank, size, gid);
  ProcessGroupIdMap::GetInstance().emplace(gid, process_group);
  return process_group;
}

}  //  namespace distributed
}  //  namespace paddle
