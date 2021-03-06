#include "grpc_server_impl.h"

/*
Async server config must provide:
    GeneralConfig config;
    config.SetString("sync_type", "ASYNC");
    config.SetString("target", target);
    config.SetInt("n_threads_per_cq", n_threads_per_cq);
    config.SetInt("n_server_threads", n_server_threads);
    config.SetInt("server_duration", n_secs);
    config.SetString("line_doc_path", line_doc_path);
    config.SetInt("n_line_doc_rows", n_rows);

Sync Server config musth provide: 
    GeneralConfig config;
    config.SetString("sync_type", "SYNC");
    config.SetString("target", target);
*/
std::unique_ptr<ServerService> CreateServer(const GeneralConfig config) {
  int bloom_factor = config.HasKey("bloom_factor")? 
    config.GetInt("bloom_factor") : 10;
  std::unique_ptr<SearchEngineServiceNew> engine = CreateSearchEngine(
      config.GetString("engine_name"), bloom_factor);

  if (config.GetString("sync_type") == "SYNC") {
    std::unique_ptr<ServerService> server(new SyncServer(config, std::move(engine)));
    return server;
  } else if (config.GetString("sync_type") == "ASYNC") {
    std::unique_ptr<ServerService> server(new AsyncServer(config, std::move(engine)));
    return server;
  } else {
    throw std::runtime_error("sync_type " + config.GetString("sync_type") 
        + " is not supported.");
  }
}





