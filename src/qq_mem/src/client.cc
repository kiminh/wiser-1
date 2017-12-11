#include <chrono>
#include <thread>

#include "qq_client.h"
#include "index_creator.h"


void make_queries(int n_queries) {
    std::string reply;

    auto qqengine = CreateSyncClient("localhost:50051");


    for (int i = 0; i < n_queries; i++) {
        std::vector<int> doc_ids;
        qqengine->Search("hello", doc_ids);
    }

}




int main(int argc, char** argv) {

  auto client = CreateSyncClient("localhost:50051");

  // Create index on server side
  IndexCreator index_creator(
        "src/testdata/enwiki-abstract_tokenized.linedoc.sample", *client);
  index_creator.DoIndex();

  // Search synchroniously
  std::vector<int> doc_ids;
  bool ret;
  ret = client->Search("multicellular", doc_ids);
  assert(ret == true);
  assert(doc_ids.size() >= 1);

  auto async_client = CreateAsyncClient("localhost:50051", 64, 100, 1000, 32, 1, 8);
  async_client->Wait();
  async_client->ShowStats();
  return 0;
}

