#pragma once

#include <cstdint>
#include <list>
#include <memory>
#include <string>

#include "envoy/network/filter.h"
#include "envoy/redis/codec.h"
#include "envoy/redis/command_splitter.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/buffer/buffer_impl.h"
#include "common/json/json_loader.h"
#include "common/json/json_validator.h"

namespace Envoy {
namespace Redis {

/**
 * All redis proxy stats. @see stats_macros.h
 */
// clang-format off
#define ALL_REDIS_PROXY_STATS(COUNTER, GAUGE)                                                      \
  COUNTER(downstream_cx_rx_bytes_total)                                                            \
  GAUGE  (downstream_cx_rx_bytes_buffered)                                                         \
  COUNTER(downstream_cx_tx_bytes_total)                                                            \
  GAUGE  (downstream_cx_tx_bytes_buffered)                                                         \
  COUNTER(downstream_cx_protocol_error)                                                            \
  COUNTER(downstream_cx_total)                                                                     \
  GAUGE  (downstream_cx_active)                                                                    \
  COUNTER(downstream_rq_total)                                                                     \
  GAUGE  (downstream_rq_active)
// clang-format on

/**
 * Struct definition for all redis proxy stats. @see stats_macros.h
 */
struct ProxyStats {
  ALL_REDIS_PROXY_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Configuration for the redis proxy filter.
 */
class ProxyFilterConfig : Json::Validator {
public:
  ProxyFilterConfig(const Json::Object& config, Upstream::ClusterManager& cm, Stats::Scope& scope);

  const std::string& clusterName() { return cluster_name_; }
  const std::string& statPrefix() { return stat_prefix_; }
  ProxyStats& stats() { return stats_; }

private:
  static ProxyStats generateStats(const std::string& prefix, Stats::Scope& scope);

  const std::string cluster_name_;
  const std::string stat_prefix_;
  ProxyStats stats_;
};

typedef std::shared_ptr<ProxyFilterConfig> ProxyFilterConfigSharedPtr;

/**
 * A redis multiplexing proxy filter. This filter will take incoming redis pipelined commands, and
 * mulitplex them onto a consistently hashed connection pool of backend servers.
 */
class ProxyFilter : public Network::ReadFilter,
                    public DecoderCallbacks,
                    public Network::ConnectionCallbacks {
public:
  ProxyFilter(DecoderFactory& factory, EncoderPtr&& encoder, CommandSplitter::Instance& splitter,
              ProxyFilterConfigSharedPtr config);
  ~ProxyFilter();

  // Network::ReadFilter
  void initializeReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;
  Network::FilterStatus onData(Buffer::Instance& data) override;
  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }

  // Network::ConnectionCallbacks
  void onEvent(Network::ConnectionEvent event) override;
  void onAboveWriteBufferHighWatermark() override {}
  void onBelowWriteBufferLowWatermark() override {}

  // Redis::DecoderCallbacks
  void onRespValue(RespValuePtr&& value) override;

private:
  struct PendingRequest : public CommandSplitter::SplitCallbacks {
    PendingRequest(ProxyFilter& parent);
    ~PendingRequest();

    // Redis::CommandSplitter::SplitCallbacks
    void onResponse(RespValuePtr&& value) override { parent_.onResponse(*this, std::move(value)); }

    ProxyFilter& parent_;
    RespValuePtr pending_response_;
    CommandSplitter::SplitRequestPtr request_handle_;
  };

  void onResponse(PendingRequest& request, RespValuePtr&& value);

  DecoderPtr decoder_;
  EncoderPtr encoder_;
  CommandSplitter::Instance& splitter_;
  ProxyFilterConfigSharedPtr config_;
  Buffer::OwnedImpl encoder_buffer_;
  Network::ReadFilterCallbacks* callbacks_{};
  std::list<PendingRequest> pending_requests_;
};

} // Redis
} // namespace Envoy
