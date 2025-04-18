#include "KuksaClient.hpp"

// Include gRPC and Proto headers here.
#include <grpcpp/grpcpp.h>
#include "kuksa/val/v1/val.grpc.pb.h"
#include "kuksa/val/v1/types.pb.h"

#include <sstream>

namespace KuksaClient {

std::string DataPointToString(const kuksa::val::v1::Datapoint &dp) {
  std::ostringstream oss;
  switch (dp.value_case()) {
    case kuksa::val::v1::Datapoint::kString:
        oss << dp.string();
        break;
    case kuksa::val::v1::Datapoint::kBool:
        oss << (dp.bool_() ? "true" : "false");
        break;
    case kuksa::val::v1::Datapoint::kInt32:
        oss << dp.int32();
        break;
    case kuksa::val::v1::Datapoint::kInt64:
        oss << dp.int64();
        break;
    case kuksa::val::v1::Datapoint::kUint32:
        oss << dp.uint32();
        break;
    case kuksa::val::v1::Datapoint::kUint64:
        oss << dp.uint64();
        break;
    case kuksa::val::v1::Datapoint::kFloat:
        oss << dp.float_();
        break;
    case kuksa::val::v1::Datapoint::kDouble:
        oss << dp.double_();
        break;
    default:
        oss << "unknown or unset";
        break;
  }
  return oss.str();
}

//=============================================================================
// Overloaded setValueImpl Definitions
//=============================================================================
void setValueImpl(kuksa::val::v1::Datapoint *dp, const std::string &value) {
  dp->set_string(value);
}
void setValueImpl(kuksa::val::v1::Datapoint *dp, bool value) {
  dp->set_bool_(value);
}
void setValueImpl(kuksa::val::v1::Datapoint *dp, int32_t value) {
  dp->set_int32(value);
}
void setValueImpl(kuksa::val::v1::Datapoint *dp, int64_t value) {
  dp->set_int64(value);
}
void setValueImpl(kuksa::val::v1::Datapoint *dp, uint32_t value) {
  dp->set_int32(value);
}
void setValueImpl(kuksa::val::v1::Datapoint *dp, uint64_t value) {
  dp->set_int64(value);
}
void setValueImpl(kuksa::val::v1::Datapoint *dp, float value) {
  dp->set_float_(value);
}
void setValueImpl(kuksa::val::v1::Datapoint *dp, double value) {
  dp->set_double_(value);
}

//=============================================================================
// Private Implementation (pImpl)
//=============================================================================
struct KuksaClient::Impl {
  // Create the gRPC channel and stub.
  std::shared_ptr<grpc::Channel> channel;
  std::unique_ptr<kuksa::val::v1::VAL::Stub> stub;
};

//=============================================================================
// Constructors & Destructor
//=============================================================================
KuksaClient::KuksaClient(const Config &config)
    : serverURI_(config.serverURI),
      debug_(config.debug),
      signalPaths_(config.signalPaths),
      config_(config),
      pImpl(std::make_unique<Impl>()) {
}

KuksaClient::KuksaClient(const std::string &configFile) : pImpl(std::make_unique<Impl>()) {
  if (!parseConfig(configFile, config_)) {
    throw std::runtime_error("Failed to load configuration from " + configFile);
  }
  serverURI_   = config_.serverURI;
  debug_       = config_.debug;
  signalPaths_ = config_.signalPaths;
}

KuksaClient::~KuksaClient() {
  joinAllSubscriptions();
}

//=============================================================================
// Public Member Functions Implementations
//=============================================================================

void KuksaClient::connect() {
  pImpl->channel = grpc::CreateChannel(serverURI_, grpc::InsecureChannelCredentials());
  pImpl->stub = kuksa::val::v1::VAL::NewStub(pImpl->channel);
  std::cout << "Connected to " << serverURI_ << std::endl;
}

std::string KuksaClient::getCurrentValue(const std::string &entryPath) {
  return getValue(entryPath, GV_CURRENT, false);
}

std::string KuksaClient::getTargetValue(const std::string &entryPath) {
  return getValue(entryPath, GV_TARGET, true);
}

std::string KuksaClient::getValue(const std::string &entryPath, GetView view, bool target) {
  std::string valueStr = "";
  if (!pImpl->stub) {
      std::cerr << "Client not connected. Aborting " << std::endl;
      return valueStr;
  }
  kuksa::val::v1::GetRequest request;
  auto* entryReq = request.add_entries();
  entryReq->set_path(entryPath);

  if (view == GV_TARGET) {
    entryReq->set_view(kuksa::val::v1::VIEW_TARGET_VALUE);
  } else { // FT_VALUE
    entryReq->set_view(kuksa::val::v1::VIEW_CURRENT_VALUE);
  }
  
  kuksa::val::v1::GetResponse response;
  grpc::ClientContext context;
  grpc::Status status = pImpl->stub->Get(&context, request, &response);
  if (!status.ok()) {
      std::cerr << " Get() RPC failed: " << status.error_message() << std::endl;
      return valueStr;
  }
  if (response.error().code() != 0) {
      std::cerr << " Get() global error: " << response.error().message() << std::endl;
      return valueStr;
  }
  if (response.entries_size() == 0) {
      std::cerr << " Get(): No entries returned for \"" << entryPath << "\"." << std::endl;
      return valueStr;
  }
  
  // Retrieve and convert the DataEntry value.
  const auto &dataEntry = response.entries(0);
  if (view == GV_TARGET) {
    valueStr = DataPointToString(dataEntry.actuator_target());
  } else { // FT_VALUE
    valueStr = DataPointToString(dataEntry.value());
  }
  
  if (view == GV_TARGET) {
    std::cout << " GetTargetValue(): Value for \"" << entryPath << "\": " << valueStr << std::endl;
  } else { // FT_VALUE
    std::cout << " GetCurrentValue(): Value for \"" << entryPath << "\": " << valueStr << std::endl;
  }

  return valueStr;
}

void KuksaClient::streamUpdate(const std::string &entryPath, float newValue) {
  if (!pImpl->stub) {
    std::cerr << "Client not connected. Aborting streamUpdate()." << std::endl;
    return;
  }
  grpc::ClientContext context;
  auto stream = pImpl->stub->StreamedUpdate(&context);
  kuksa::val::v1::StreamedUpdateRequest request;
  auto* update = request.add_updates();
  auto* dataEntry = update->mutable_entry();
  dataEntry->set_path(entryPath);
  // We assume FIELD_VALUE (from the proto) for streaming updates.
  dataEntry->mutable_value()->set_float_(newValue);
  update->add_fields(kuksa::val::v1::FIELD_VALUE);

  if (!stream->Write(request)) {
    std::cerr << "streamUpdate: Failed to write request." << std::endl;
    return;
  }
  stream->WritesDone();

  kuksa::val::v1::StreamedUpdateResponse response;
  while (stream->Read(&response)) {
    if (response.error().code() != 0) {
      std::cerr << "streamUpdate response error: " << response.error().message() << std::endl;
    } else {
      std::cout << "streamUpdate: Received a response." << std::endl;
    }
  }
  grpc::Status status = stream->Finish();
  if (!status.ok()) {
    std::cerr << "streamUpdate RPC failed: " << status.error_message() << std::endl;
  } else {
    std::cout << "streamUpdate: Completed successfully." << std::endl;
  }
}

void KuksaClient::subscribeTargetValue(const std::string &entryPath,
  std::function<void(const std::string &, const std::string &, const int &)> userCallback) {
  subscribe(entryPath, userCallback, FT_ACTUATOR_TARGET);
}

void KuksaClient::subscribeCurrentValue(const std::string &entryPath,
  std::function<void(const std::string &, const std::string &, const int &)> userCallback) {
  subscribe(entryPath, userCallback, FT_VALUE);
}

void KuksaClient::subscribe(const std::string &entryPath,
    std::function<void(const std::string &, const std::string &, const int &)> userCallback, int field) {
  if (!pImpl->stub) {
    std::cerr << "Client not connected. Aborting subscribe()." << std::endl;
    return;
  }
  kuksa::val::v1::SubscribeRequest request;
  auto* subEntry = request.add_entries();
  subEntry->set_path(entryPath);
  // Here we use the proto’s own view for subscription.
  subEntry->set_view(kuksa::val::v1::VIEW_ALL);

  if (field == FT_ACTUATOR_TARGET) {
    subEntry->add_fields(kuksa::val::v1::FIELD_ACTUATOR_TARGET);
  } else { // FT_VALUE
    subEntry->add_fields(kuksa::val::v1::FIELD_VALUE);
  }
  
  grpc::ClientContext context;
  auto reader = pImpl->stub->Subscribe(&context, request);
  std::cout << "Subscription: Listening on \"" << entryPath << "\"." << std::endl;
  kuksa::val::v1::SubscribeResponse response;
  int updateCount = 0;
  while (reader->Read(&response)) {
    ++updateCount;
    std::cout << "Subscription: Received update #" << updateCount << " for \"" << entryPath << "\"" << std::endl;
    for (int i = 0; i < response.updates_size(); ++i) {
      const auto &upd = response.updates(i);
      std::string updatePath = upd.entry().path();
      // dataPointToString is used internally.
      std::string updateValue;
      if (field == FT_ACTUATOR_TARGET) {
        updateValue = getTargetValue(entryPath);
        std::cout << "  Update TargetValue: " << updatePath << " -> " << updateValue << std::endl;
      } else { // FT_VALUE
        updateValue = getCurrentValue(entryPath);
        std::cout << "  Update CurrentValue: " << updatePath << " -> " << updateValue << std::endl;
      }

      if (userCallback)
        userCallback(updatePath, updateValue, field);
    }
  }
  grpc::Status status = reader->Finish();
  if (!status.ok()) {
    std::cerr << "subscribe RPC failed: " << status.error_message() << std::endl;
  } else {
    std::cout << "Subscription: Stream finished for \"" << entryPath << "\"." << std::endl;
  }
}

void KuksaClient::subscribeAll(std::function<void(const std::string &, const std::string &, const int &)> userCallback) {
  for (const auto &path : signalPaths_) {
    subscriptionThreads_.emplace_back([this, path, userCallback]() {
      subscribeTargetValue(path, userCallback);
    });
  }
  for (const auto &path : signalPaths_) {
    subscriptionThreads_.emplace_back([this, path, userCallback]() {
      subscribeCurrentValue(path, userCallback);
    });
  }
}

void KuksaClient::joinAllSubscriptions() {
  for (auto &t : subscriptionThreads_) {
    if (t.joinable())
      t.join();
  }
  subscriptionThreads_.clear();
}

void KuksaClient::detachAllSubscriptions() {
  for (auto &t : subscriptionThreads_) {
    if (t.joinable())
      t.detach();
  }
  subscriptionThreads_.clear();
}

void KuksaClient::getServerInfo() {
  if (!pImpl->stub) {
    std::cerr << "Client not connected. Aborting getServerInfo()." << std::endl;
    return;
  }
  kuksa::val::v1::GetServerInfoRequest request;
  kuksa::val::v1::GetServerInfoResponse response;
  grpc::ClientContext context;
  grpc::Status status = pImpl->stub->GetServerInfo(&context, request, &response);
  if (!status.ok()) {
    std::cerr << "getServerInfo RPC failed: " << status.error_message() << std::endl;
    return;
  }
  std::cout << "Server Info:" << std::endl;
  std::cout << "  Name: " << response.name() << std::endl;
  std::cout << "  Version: " << response.version() << std::endl;
}

//=============================================================================
// Static Helper: Configuration Parsing
//=============================================================================
bool KuksaClient::parseConfig(const std::string &filename, Config &config) {
  std::ifstream configFile(filename);
  if (!configFile.is_open()) {
    std::cerr << "Unable to open config file: " << filename << std::endl;
    return false;
  }
  try {
    json j;
    configFile >> j;
    config.serverURI = j.at("broker").at("serverURI").get<std::string>();
    config.debug     = j.value("debug", false);
    if (j.contains("signal") && j["signal"].is_array()) {
      for (const auto &item : j["signal"]) {
        if (item.contains("path"))
          config.signalPaths.push_back(item["path"].get<std::string>());
      }
    }
  } catch (const std::exception &e) {
    std::cerr << "Failed to parse " << filename << ": " << e.what() << std::endl;
    return false;
  }
  return true;
}


//=============================================================================
// Template Helper Implementations
//=============================================================================
template <typename T>
void KuksaClient::setValueInternalImpl(const std::string &entryPath, const T &newValue, int field) {
  kuksa::val::v1::SetRequest request;
  auto* update = request.add_updates();
  kuksa::val::v1::DataEntry* dataEntry = update->mutable_entry();
  dataEntry->set_path(entryPath);

  if (field == FT_ACTUATOR_TARGET) {
    setValueImpl(dataEntry->mutable_actuator_target(), newValue);
    update->add_fields(kuksa::val::v1::FIELD_ACTUATOR_TARGET);
  } else { // FT_VALUE
    setValueImpl(dataEntry->mutable_value(), newValue);
    update->add_fields(kuksa::val::v1::FIELD_VALUE);
  }

  kuksa::val::v1::SetResponse response;
  grpc::ClientContext context;
  grpc::Status status = pImpl->stub->Set(&context, request, &response);
  if (!status.ok()) {
    std::cerr << "Set() RPC failed: " << status.error_message() << std::endl;
    return;
  }
  if (response.error().code() != 0) {
    std::cerr << "Set() global error: " << response.error().message() << std::endl;
  } else {
    if (field == FT_ACTUATOR_TARGET) {
      std::cout << "SetTargetValue(): Updated \"" << entryPath << "\" - " << newValue << std::endl;
    } else { // FT_VALUE
      std::cout << "SetCurrentValue(): Updated \"" << entryPath << "\" - " << newValue << std::endl;
    }
  }
}

// Explicit instantiation for bool
template void KuksaClient::setValueInternalImpl<bool>(const std::string&, const bool&, int);
template void KuksaClient::setValueInternalImpl<int32_t>(const std::string&, const int32_t&, int);
template void KuksaClient::setValueInternalImpl<int64_t>(const std::string&, const int64_t&, int);
template void KuksaClient::setValueInternalImpl<uint32_t>(const std::string&, const uint32_t&, int);
template void KuksaClient::setValueInternalImpl<uint64_t>(const std::string&, const uint64_t&, int);
template void KuksaClient::setValueInternalImpl<float>(const std::string&, const float&, int);
template void KuksaClient::setValueInternalImpl<double>(const std::string&, const double&, int);

// Note: If you plan to use setValueInternalImpl with additional types,
// you can explicitly instantiate them here.

//=============================================================================
// Conversion Specializations Implementations
//=============================================================================
bool KuksaClient::convertString(const std::string &str, bool &out) {
  if (str == "true") {
    out = true;
    return true;
  } else if (str == "false") {
    out = false;
    return true;
  }
  std::istringstream iss(str);
  int temp = 0;
  iss >> temp;
  if (iss.fail() || !iss.eof())
    return false;
  out = (temp != 0);
  return true;
}

bool KuksaClient::convertString(const std::string &str, uint8_t &out) {
  uint32_t temp = 0;
  if (!convertString(str, temp))
    return false;
  if (temp > std::numeric_limits<uint8_t>::max())
    return false;
  out = static_cast<uint8_t>(temp);
  return true;
}

bool KuksaClient::convertString(const std::string &str, uint16_t &out) {
  uint32_t temp = 0;
  if (!convertString(str, temp))
    return false;
  if (temp > std::numeric_limits<uint16_t>::max())
    return false;
  out = static_cast<uint16_t>(temp);
  return true;
}

bool KuksaClient::convertString(const std::string &str, uint32_t &out) {
  std::istringstream iss(str);
  iss >> out;
  return !iss.fail() && iss.eof();
}

} // namespace KuksaClient
