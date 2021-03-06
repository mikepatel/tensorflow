/* Copyright 2018 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "tensorflow/contrib/tensorrt/convert/convert_graph.h"

#include "tensorflow/contrib/tensorrt/convert/convert_nodes.h"
#include "tensorflow/core/common_runtime/device_mgr.h"
#include "tensorflow/core/common_runtime/device_set.h"
#include "tensorflow/core/grappler/clusters/cluster.h"
#include "tensorflow/core/grappler/grappler_item.h"
#include "tensorflow/core/lib/core/status.h"
#include "tensorflow/core/lib/core/status_test_util.h"
#include "tensorflow/core/platform/test.h"
#include "tensorflow/core/protobuf/config.pb.h"  // NOLINT
#include "tensorflow/core/public/session.h"

#if GOOGLE_CUDA
#if GOOGLE_TENSORRT

namespace tensorflow {
namespace tensorrt {
namespace convert {

class FakeCluster : public grappler::Cluster {
 public:
  FakeCluster() : Cluster(0) {}

  void SetDeviceSet(const DeviceSet* device_set) { device_set_ = device_set; }

  const DeviceSet* GetDeviceSet() const override { return device_set_; }

  string type() const override { return ""; }
  Status Provision() override { return Status::OK(); }
  Status Initialize(const grappler::GrapplerItem& item) override {
    return Status::OK();
  }
  Status Run(const GraphDef& graph_def,
             const std::vector<std::pair<string, Tensor>>& feed,
             const std::vector<string>& fetch,
             RunMetadata* metadata) override {
    return Status::OK();
  }

 private:
  const DeviceSet* device_set_;
};

TEST(ConvertGraphTest, GetDeviceAndAllocator) {
  ConversionParams params;
  EngineInfo engine_info;
  {
    // params.cluster is not set, and no gpu device is available.
    auto result = GetDeviceAndAllocator(params, engine_info);
    EXPECT_EQ(-1, result.first);
    EXPECT_EQ(nullptr, result.second);
  }

  // Create a session with two (virtual) gpu device.
  SessionOptions options;
  ConfigProto* config = &options.config;
  GPUOptions* gpu_options = config->mutable_gpu_options();
  auto virtual_devices =
      gpu_options->mutable_experimental()->add_virtual_devices();
  virtual_devices->add_memory_limit_mb(200);
  virtual_devices->add_memory_limit_mb(200);
  std::unique_ptr<Session> session(NewSession(options));

  {
    // params.cluster is not set, should find and return first gpu id and
    // corresponding allocator.
    auto result = GetDeviceAndAllocator(params, engine_info);
    EXPECT_EQ(0, result.first);
    EXPECT_NE(nullptr, result.second);
    EXPECT_EQ("GPU_0_bfc", result.second->Name());
  }

  FakeCluster cluster;
  params.cluster = &cluster;
  {
    // params.cluster->GetDeviceSet() returns null, should find and return first
    // gpu id and corresponding allocator.
    auto result = GetDeviceAndAllocator(params, engine_info);
    EXPECT_EQ(0, result.first);
    EXPECT_NE(nullptr, result.second);
    EXPECT_EQ("GPU_0_bfc", result.second->Name());
  }

  // Build the DeviceSet.
  DeviceSet device_set;
  const DeviceMgr* device_mgr = nullptr;
  TF_ASSERT_OK(session->LocalDeviceManager(&device_mgr));
  for (auto d : device_mgr->ListDevices()) {
    device_set.AddDevice(d);
  }
  cluster.SetDeviceSet(&device_set);
  {
    // engine_info.device is not set, should find and return first gpu id and
    // corresponding allocator.
    auto result = GetDeviceAndAllocator(params, engine_info);
    EXPECT_EQ(0, result.first);
    EXPECT_NE(nullptr, result.second);
    EXPECT_EQ("GPU_0_bfc", result.second->Name());
  }

  engine_info.device = "/GPU:1";
  {
    // Set to use second device.
    auto result = GetDeviceAndAllocator(params, engine_info);
    EXPECT_EQ(0, result.first);
    EXPECT_NE(nullptr, result.second);
    EXPECT_EQ("GPU_1_bfc", result.second->Name());
  }

  engine_info.device = "/GPU:3";
  {
    // Set to use nonexistent device.
    auto result = GetDeviceAndAllocator(params, engine_info);
    EXPECT_EQ(-1, result.first);
    EXPECT_EQ(nullptr, result.second);
  }
}

}  // namespace convert
}  // namespace tensorrt
}  // namespace tensorflow

#endif  // GOOGLE_TENSORRT
#endif  // GOOGLE_CUDA
