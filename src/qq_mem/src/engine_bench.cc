#include <algorithm>
#include <iostream>
#include <string>
#include <memory>
#include <thread>
#include <vector>
#include <climits>

#include <gperftools/profiler.h>
#include <glog/logging.h>
#include <gflags/gflags.h>

#include "qq_mem_engine.h"
#include "utils.h"
#include "grpc_client_impl.h"
#include "general_config.h"
#include "query_pool.h"
#include "engine_factory.h"


DEFINE_int32(n_threads, 1, "Number of client threads");
DEFINE_string(exp_mode, "local", "local/grpc/grpclog/localquerylog");
DEFINE_string(grpc_server, "localhost", "network address of the GRPC server, port not included");
DEFINE_int32(run_duration, 15, "number of seconds to run the client");
DEFINE_string(query_path, "/mnt/ssd/realistic_querylog", "path of the query log");


const int K = 1000;
const int M = 1000 * K;
const int B = 1000 * M;

std::string Concat(TermList terms) {
  std::string s;
  for (std::size_t i = 0; i < terms.size(); i++) {
    s += terms[i];
    if (i < terms.size() - 1) {
      // not the last term
      s += "+";
    }
  }
  return s;
}

std::string GetTargetUrl(std::string addr) {
  return addr + ":50051";
}

class Experiment {
 public:
  virtual void Before() {}
  virtual void BeforeEach(const int run_id) {}
  virtual void RunTreatment(const int run_id) = 0;
  virtual void AfterEach(const int run_id) {}
  virtual void After() {}

  virtual int NumberOfRuns() = 0;
  virtual void Run() {
    Before();
    for (int i = 0; i < NumberOfRuns(); i++) {
      BeforeEach(i);
      RunTreatment(i);
      AfterEach(i);
    }
    After();
  }
};


struct Treatment {
  Treatment() {}
  Treatment(TermList terms_in, 
            bool is_phrase_in, 
            int n_repeats_in, 
            bool return_snippets_in)
    :terms(terms_in), is_phrase(is_phrase_in), n_queries(n_repeats_in),
     return_snippets(return_snippets_in){}

  TermList terms;
  bool is_phrase;
  int n_queries;
  bool return_snippets;
  int n_passages = 3;
  int n_results = 5;

  std::string tag; //"" / "querylog"
  std::string query_log_path; //"" / "querylog"
};


class TreatmentExecutor {
 public:
  virtual utils::ResultRow Execute(Treatment treatment) = 0;
  virtual int NumberOfThreads() = 0;
  virtual std::unique_ptr<QueryProducerService> MakeProducer(Treatment treatment) {
    GeneralConfig config;
    config.SetBool("is_phrase", treatment.is_phrase);
    config.SetBool("return_snippets", treatment.return_snippets);
    config.SetInt("n_results", treatment.n_results);
    return CreateQueryProducer(treatment.terms, NumberOfThreads(), config);
  }
};

class GrpcTreatmentExecutor: public TreatmentExecutor {
 public:
  GrpcTreatmentExecutor(int n_threads){
    // ASYNC Streaming
    // client_config_.SetString("synchronization", "SYNC");
    // client_config_.SetString("rpc_arity", "STREAMING");
    // client_config_.SetString("target", "localhost:50051");
    // client_config_.SetInt("n_client_channels", 64);
    // client_config_.SetInt("n_rpcs_per_channel", 100);
    // client_config_.SetInt("n_messages_per_call", 100000);
    // client_config_.SetInt("n_threads", n_threads); 
    // client_config_.SetInt("n_threads_per_cq", 1);
    // client_config_.SetInt("benchmark_duration", 5);
    // client_config_.SetBool("save_reply", false);

    client_config_.SetString("synchronization", "SYNC");
    client_config_.SetString("rpc_arity", "STREAMING");
    client_config_.SetString("target", GetTargetUrl(FLAGS_grpc_server));
    client_config_.SetInt("n_client_channels", 64);
    client_config_.SetInt("n_threads", n_threads); 
    client_config_.SetInt("benchmark_duration", FLAGS_run_duration);
    // client_config_.SetBool("save_reply", true);
    client_config_.SetBool("save_reply", false);
  }

  int NumberOfThreads() {return client_config_.GetInt("n_threads");}

  utils::ResultRow Execute(Treatment treatment) {
    utils::ResultRow row;

    auto client = CreateClient(client_config_, MakeProducer(treatment));
    double dur = client->Wait();
    row = client->ShowStats(dur);

    return row;
  }

 private:
  GeneralConfig client_config_;
};


class GrpcLogTreatmentExecutor: public TreatmentExecutor {
 public:
  GrpcLogTreatmentExecutor(int n_threads){
    // ASYNC Streaming
    // client_config_.SetString("synchronization", "ASYNC");
    // client_config_.SetString("rpc_arity", "STREAMING");
    // client_config_.SetString("target", GetTargetUrl(FLAGS_grpc_server));
    // client_config_.SetInt("n_client_channels", 64);
    // client_config_.SetInt("n_rpcs_per_channel", 100);
    // client_config_.SetInt("n_messages_per_call", 1000000);
    // client_config_.SetInt("n_threads", n_threads); 
    // client_config_.SetInt("n_threads_per_cq", 1);
    // client_config_.SetInt("benchmark_duration", FLAGS_run_duration);
    // client_config_.SetBool("save_reply", false);

    client_config_.SetString("synchronization", "SYNC");
    client_config_.SetString("rpc_arity", "STREAMING");
    client_config_.SetString("target", GetTargetUrl(FLAGS_grpc_server));
    client_config_.SetInt("n_client_channels", 64);
    client_config_.SetInt("n_threads", n_threads); 
    client_config_.SetInt("benchmark_duration", FLAGS_run_duration);
    client_config_.SetBool("save_reply", false);
  }

  std::unique_ptr<QueryProducerService> MakeProducer(Treatment treatment) {
    if (treatment.tag != "querylog")
      LOG(FATAL) << "Tag must be set right";

    // return std::unique_ptr<QueryProducerService>(
        // new QueryProducerByLog(treatment.query_log_path, NumberOfThreads()));
    return std::unique_ptr<QueryProducerService>(
        new QueryProducerNoLoop(treatment.query_log_path));
  }

  int NumberOfThreads() {return client_config_.GetInt("n_threads");}

  utils::ResultRow Execute(Treatment treatment) {
    utils::ResultRow row;

    auto client = CreateClient(client_config_, MakeProducer(treatment));
    double dur = client->WaitUntilQueriesExhausted();
    row = client->ShowStats(dur);

    return row;
  }

 private:
  GeneralConfig client_config_;
};



class LocalTreatmentExecutor: public TreatmentExecutor {
 public:
  LocalTreatmentExecutor(SearchEngineServiceNew *engine)
     :engine_(engine) {}

  int NumberOfThreads() {return 1;}

  utils::ResultRow Execute(Treatment treatment) {
    const int n_queries = treatment.n_queries;
    utils::ResultRow row;
    auto query_producer = MakeProducer(treatment);

    auto start = utils::now();
    for (int i = 0; i < n_queries; i++) {
      auto query = query_producer->NextNativeQuery(0);
      query.n_snippet_passages = treatment.n_passages;
      // std::cout << query.ToStr() << std::endl;
      auto result = engine_->Search(query);

      // std::cout << result.ToStr() << std::endl;
    }
    auto end = utils::now();
    auto dur = utils::duration(start, end);

    auto count_map = engine_->PostinglistSizes(treatment.terms);
    std::cout << "--- Term Counts ---" << std::endl;
    for (auto pair : count_map) {
      std::cout << pair.first << " : " << pair.second << std::endl;
    }

    row["latency"] = std::to_string(dur / n_queries); 
    row["n_passages"] = std::to_string(treatment.n_passages);
    row["return_snippets"] = std::to_string(treatment.return_snippets);
    row["duration"] = std::to_string(dur); 
    row["QPS"] = std::to_string(round(100 * n_queries / dur) / 100.0);
    return row;
  }

 private:
  SearchEngineServiceNew *engine_;
};


class LocalLogTreatmentExecutor: public TreatmentExecutor {
 public:
  LocalLogTreatmentExecutor(SearchEngineServiceNew *engine)
     :engine_(engine) {}

  int NumberOfThreads() {return 1;}

  std::unique_ptr<QueryProducerService> MakeProducer(Treatment treatment) {
    if (treatment.tag != "querylog")
      LOG(FATAL) << "Tag must be set right";

    return std::unique_ptr<QueryProducerService>(
        new QueryProducerByLog(treatment.query_log_path, NumberOfThreads()));
  }

  utils::ResultRow Execute(Treatment treatment) {
    const int n_queries = treatment.n_queries;
    utils::ResultRow row;
    auto query_producer = MakeProducer(treatment);

    auto start = utils::now();
    for (int i = 0; query_producer->IsEnd() == false; i++) {
      auto query = query_producer->NextNativeQuery(0);
      // std::cout << query.ToStr() << std::endl;
      auto result = engine_->Search(query);

      // std::cout << result.ToStr() << std::endl;
      if (i % 1000 == 0) {
        std::cout << "Finished " << i << std::endl;
      }
    }
    auto end = utils::now();
    auto dur = utils::duration(start, end);

    row["latency"] = std::to_string(dur / n_queries); 
    row["duration"] = std::to_string(dur); 
    row["n_queries"] = std::to_string(n_queries); 
    row["QPS"] = std::to_string(round(100 * n_queries / dur) / 100.0);
    return row;
  }

 private:
  SearchEngineServiceNew *engine_;
};




class LocalStatsExecutor: public TreatmentExecutor {
 public:
  LocalStatsExecutor(SearchEngineServiceNew *engine)
     :engine_(engine) {}

  int NumberOfThreads() {return 1;}

  std::unique_ptr<QueryProducerService> MakeProducer(Treatment treatment) override {
    GeneralConfig config;
    config.SetBool("is_phrase", treatment.is_phrase);
    config.SetBool("return_snippets", treatment.return_snippets);
    config.SetInt("n_results", treatment.n_results);

    auto array = CreateTermPoolArray(
        "/mnt/ssd/downloads/wiki_QueryLog_tokenized", NumberOfThreads(), 100);

    return std::unique_ptr<QueryProducerService>(
        new QueryProducer(std::move(array), config));
  }

  utils::ResultRow Execute(Treatment treatment) {
    const int n_queries = treatment.n_queries;
    utils::ResultRow row;

    auto query_producer = MakeProducer(treatment);

    auto start = utils::now();
    for (int i = 0; i < n_queries; i++) {
      auto query = query_producer->NextNativeQuery(0);
      query.n_snippet_passages = treatment.n_passages;
      std::cout << query.ToStr() << std::endl;
      // auto result = engine_->Search(query);

      auto count_map = engine_->PostinglistSizes(query.terms);
      for (auto &pair : count_map) {
        std::cout << pair.first << " : " << pair.second << std::endl;
      }

      // std::cout << result.ToStr() << std::endl;
    }
    auto end = utils::now();
    auto dur = utils::duration(start, end);


    row["latency"] = std::to_string(dur / n_queries); 
    row["n_passages"] = std::to_string(treatment.n_passages);
    row["return_snippets"] = std::to_string(treatment.return_snippets);
    row["duration"] = std::to_string(dur); 
    row["QPS"] = std::to_string(round(100 * n_queries / dur) / 100.0);
    return row;
  }

 private:
  SearchEngineServiceNew *engine_;
};


class EngineExperiment: public Experiment {
 public:
  EngineExperiment(GeneralConfig config): config_(config) {
    MakeTreatments();
  }

  int NumberOfRuns() {
    return treatments_.size();
  }

  void MakeTreatments() {
    // single term
    std::vector<Treatment> treatments {
      // Treatment({"hello"}, false, 1000, true),
      // Treatment({"from"}, false, 20, true),
      // Treatment({"ripdo"}, false, 10000, true),

      // Treatment({"hello", "world"}, false, 1000, true),
      // Treatment({"from", "also"}, false, 10, true),
      // Treatment({"ripdo", "liftech"}, false, 1000000, true),

      // Treatment({"hello", "world"}, true, 100, true),
      // Treatment({"barack", "obama"}, true, 1000, true),
      // Treatment({"from", "also"}, true, 10, true)
    };

    Treatment t;
    t.terms = {"empty"};
    t.tag = "querylog";
    t.n_queries = -1;
    // t.query_log_path = "/mnt/ssd/medium_log";
    // t.query_log_path = "/mnt/ssd/short_log";
    // t.query_log_path = "/mnt/ssd/hello_log";
    t.query_log_path = FLAGS_query_path;
    treatments.push_back(t);

    // for (auto &t : treatments) {
      // t.return_snippets = false;
    // }

    treatments_ = treatments;
  }

  void Before() {
    if (FLAGS_exp_mode == "grpc") {
      std::cout << "Using GRPC..." << std::endl;
      treatment_executor_.reset(new GrpcTreatmentExecutor(FLAGS_n_threads));
    } else if (FLAGS_exp_mode == "grpclog") {
      std::cout << "Using GRPC with query log..." << std::endl;
      treatment_executor_.reset(new GrpcLogTreatmentExecutor(FLAGS_n_threads));
    } else if (FLAGS_exp_mode == "local") {
      std::cout << "Using in-proc engine..." << std::endl;
      engine_ = std::move(CreateEngineFromFile());
      treatment_executor_.reset(new LocalTreatmentExecutor(engine_.get()));
    } else if (FLAGS_exp_mode == "locallog") {
      std::cout << "Using in-proc engine with query log..." << std::endl;
      engine_ = std::move(CreateEngineFromFile());
      treatment_executor_.reset(new LocalLogTreatmentExecutor(engine_.get()));
    } else if (FLAGS_exp_mode == "localquerylog") {
      std::cout << "Using in-proc engine with query log..." << std::endl;

      engine_ = std::move(CreateEngineFromFile());
      treatment_executor_.reset(new LocalStatsExecutor(engine_.get()));
    } else {
      LOG(FATAL) << "Mode not supported:mke engine_bench_build";
    }
  }

  void RunTreatment(const int run_id) {
    std::cout << "Running treatment " << run_id << " ...... " << std::endl;

    // auto row = Search(query_producers_[run_id].get(), run_id);
    auto row = treatment_executor_->Execute(treatments_[run_id]);

    row["n_docs"] = std::to_string(config_.GetInt("n_docs"));
    row["terms"] = Concat(treatments_[run_id].terms);
    row["is_phrase"] = std::to_string(treatments_[run_id].is_phrase);
    table_.Append(row);
  }

  void After() {
    std::cout << table_.ToStr();

    std::cout << "ExperimentFinished!!!" << std::endl;
  }

  std::unique_ptr<SearchEngineServiceNew> CreateEngineFromFile() {
    std::unique_ptr<SearchEngineServiceNew> engine = CreateSearchEngine(
        config_.GetString("engine_type"));
    engine->Load();

    std::cout << "IsVacuumUrl:" << IsVacuumUrl(config_.GetString("engine_type"))  << std::endl;
    if (IsVacuumUrl(config_.GetString("engine_type")) == false) {
      if (config_.GetString("load_source") == "linedoc") {
        engine->LoadLocalDocuments(config_.GetString("linedoc_path"), 
                                   config_.GetInt("n_docs"),
                                   config_.GetString("loader"));
      } else if (config_.GetString("load_source") == "dump") {
        std::cout << "Loading engine from dumpped data...." << std::endl;
        auto start = utils::now();
        engine->Deserialize(config_.GetString("dump_path"));
        auto end = utils::now();
        std::cout << "Time to load dumpped data: " << utils::duration(start, end) << std::endl;
      } else {
        std::cout << "You must speicify load_source" << std::endl;
      }
    }

    // engine->Serialize("/mnt/ssd/big-engine-char-length-04-19");

    std::cout << "Term Count: " << engine->TermCount() << std::endl;
    return engine;
  }

  void ShowEngineStatus(SearchEngineServiceNew *engine, const TermList &terms) {
    auto pl_sizes = engine->PostinglistSizes(terms); 
    for (auto pair : pl_sizes) {
      std::cout << pair.first << " : " << pair.second << std::endl;
    }
  }

 private:
  GeneralConfig config_;
  std::vector<std::unique_ptr<QueryProducerService>> query_producers_;
  std::unique_ptr<SearchEngineServiceNew> engine_;
  std::vector<Treatment> treatments_;
  utils::ResultTable table_;
  std::unique_ptr<TreatmentExecutor> treatment_executor_;
};

void CheckConfig(GeneralConfig conf) {
  if (IsVacuumUrl(conf.GetString("engine_type")) && 
      conf.HasKey("load_source")) {
    std::cerr << "setting load_source has no effect!" << std::endl;
  }

  if (IsVacuumUrl(conf.GetString("engine_type")) && 
      conf.HasKey("loader")) {
    std::cerr << "setting loader has no effect!" << std::endl;
  }
  
  if (IsVacuumUrl(conf.GetString("engine_type")) && 
      conf.HasKey("linedoc_path")) {
    std::cerr << "setting linedoc_path has no effect!" << std::endl;
  }

  if (IsVacuumUrl(conf.GetString("engine_type")) && 
      conf.HasKey("dump_path")) {
    std::cerr << "setting dump_path has no effect!" << std::endl;
  }
}

GeneralConfig config_by_jun() {
/*
                                       /;    ;\
                                   __  \\____//
                                  /{_\_/   `'\____
                                  \___   (o)  (o  }
       _____________________________/          :--'   DRINKA
   ,-,'`@@@@@@@@       @@@@@@         \_    `__\
  ;:(  @@@@@@@@@        @@@             \___(o'o)
  :: )  @@@@          @@@@@@        ,'@@(  `===='        PINTA
  :: : @@@@@:          @@@@         `@@@:
  :: \  @@@@@:       @@@@@@@)    (  '@@@'
  ;; /\      /`,    @@@@@@@@@\   :@@@@@)                   MILKA
  ::/  )    {_----------------:  :~`,~~;
 ;;'`; :   )                  :  / `; ;
;;;; : :   ;                  :  ;  ; :                        DAY !!!
`'`' / :  :                   :  :  : :
    )_ \__;      ";"          :_ ;  \_\       `,','
    :__\  \    * `,'*         \  \  :  \   *  8`;'*  *
        `^'     \ :/           `^'  `-^-'   \v/ :  \/   -Bill Ames-

*/

  GeneralConfig config;
  // config.SetString("engine_type", "qq_mem_compressed");
  config.SetString("engine_type", "vacuum:vacuum_dump:/mnt/ssd/vacuum-wiki-06-24.baseline/");

  config.SetInt("n_docs", 100);
  config.SetString("linedoc_path", 
      "/mnt/ssd/downloads/enwiki.linedoc_tokenized.1");
  config.SetString("loader", "WITH_POSITIONS");


  config.SetInt("n_queries", 1000);
  config.SetInt("n_passages", 3);
  config.SetBool("enable_snippets", false);
  
  
  config.SetString("query_source", "hardcoded");
  config.SetStringVec("terms", std::vector<std::string>{"hello"});

  CheckConfig(config);
  return config;
}


GeneralConfig config_by_kan() {
/*
           ____
         / |   |\
  _____/ @ |   | \
 |> . .    |   |   \
  \  .     |||||     \________________________
   |||||||\                                    )
            \                                 |
             \                                |
               \                             /
                |   ____________------\     |
                |  | |                ||    /
                |  | |                ||  |
                |  | |                ||  |
                |  | |                ||  |  Glo Pearl
               (__/_/                ((__/

*/

  return GeneralConfig();
}


// To run a bench
// 
// 1. edit treatment
// 2. set FLAGS_exp_mode
// 3. run
//
// performance is reported per treatment
//


int main(int argc, char **argv) {
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1; // print to stderr instead of file
  FLAGS_minloglevel = 3; 

	gflags::ParseCommandLineFlags(&argc, &argv, true);

  // Separate our configurations to avoid messes
  auto config = config_by_jun();
  // auto config = config_by_kan();
  
  // qq_uncompressed_bench_wiki(config);

  EngineExperiment experiment(config);
  experiment.Run();
}


